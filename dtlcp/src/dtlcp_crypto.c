/*
 * dtlcp_crypto.c — DTLCP 密钥调度
 *
 * PRF = TLS 风格 P_hash(HMAC-SM3)：
 *   P_hash(secret, seed) = HMAC_SM3(secret, A(1)‖seed) ‖ HMAC_SM3(secret, A(2)‖seed) ‖ …
 *   A(0) = seed,  A(i) = HMAC_SM3(secret, A(i−1))
 * master_secret = PRF(PMS, "master secret", cr‖sr)[:48]
 * key_block     = PRF(master, "dtlcp key expansion", sr‖cr)[:]
 */
#include <string.h>
#include "dtlcp_internal.h"

void dtlcp_prf(const uint8_t *secret, size_t slen, const char *label,
               const uint8_t *seed, size_t seedlen, uint8_t *out, size_t olen)
{
    uint8_t a[SMC_SM3_DIGEST];
    uint8_t buf[SMC_SM3_DIGEST];
    size_t  labellen = strlen(label);
    size_t  done = 0;

    /* A(1) = HMAC(secret, label‖seed) */
    {
        hmac_sm3_ctx c;
        hmac_sm3_init(&c, secret, slen);
        hmac_sm3_update(&c, (const uint8_t *)label, labellen);
        hmac_sm3_update(&c, seed, seedlen);
        hmac_sm3_final(&c, a);
    }
    while (done < olen) {
        hmac_sm3_ctx c;
        size_t n = olen - done;
        size_t i;
        hmac_sm3_init(&c, secret, slen);
        hmac_sm3_update(&c, a, SMC_SM3_DIGEST);
        hmac_sm3_update(&c, (const uint8_t *)label, labellen);
        hmac_sm3_update(&c, seed, seedlen);
        hmac_sm3_final(&c, buf);
        if (n > SMC_SM3_DIGEST) n = SMC_SM3_DIGEST;
        for (i = 0; i < n; i++) out[done + i] = buf[i];
        done += n;
        /* A(i+1) */
        hmac_sm3_init(&c, secret, slen);
        hmac_sm3_update(&c, a, SMC_SM3_DIGEST);
        hmac_sm3_final(&c, a);
    }
}

void dtlcp_expand_keys(const uint8_t master[48],
                       const uint8_t cr[32], const uint8_t sr[32],
                       dtlcp_keys *out)
{
    uint8_t seed[64];
    uint8_t block[128];

    memcpy(out->master, master, 48);
    memcpy(seed, sr, 32);          /* key_block 用 sr‖cr */
    memcpy(seed + 32, cr, 32);
    dtlcp_prf(out->master, 48, "dtlcp key expansion", seed, 64, block, sizeof block);

    memcpy(out->client_mac, block,       32);
    memcpy(out->server_mac, block + 32,  32);
    memcpy(out->client_key, block + 64,  16);
    memcpy(out->server_key, block + 80,  16);
    memcpy(out->client_iv,  block + 96,   8);
    memcpy(out->server_iv,  block + 104,  8);
}

void dtlcp_derive_keys(const uint8_t *pms, size_t pmslen,
                       const uint8_t cr[32], const uint8_t sr[32],
                       dtlcp_keys *out)
{
    uint8_t seed[64], master[48];
    memcpy(seed, cr, 32);
    memcpy(seed + 32, sr, 32);
    dtlcp_prf(pms, pmslen, "master secret", seed, 64, master, 48);
    dtlcp_expand_keys(master, cr, sr, out);
}

void dtlcp_finished_verify(const dtlcp_keys *keys, int from_client,
                           const uint8_t hs_hash[32], uint8_t out[DTLCP_VERIFY_LEN])
{
    const char *label = from_client ? "client finished" : "server finished";
    uint8_t v[32];
    dtlcp_prf(keys->master, 48, label, hs_hash, 32, v, 32);
    memcpy(out, v, DTLCP_VERIFY_LEN);
}
