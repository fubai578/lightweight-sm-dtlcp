/*
 * smc_rng.c — HMAC-SM3 DRBG（NIST SP 800-90A HMAC-DRBG 结构）
 */
#include "smc/smc_rng.h"

#if SMC_DRBG

static void drbg_update(smc_drbg *d, const uint8_t *seed, size_t seedlen, int second)
{
    hmac_sm3_ctx c;
    uint8_t sep = (uint8_t)(second ? 0x01 : 0x00);

    hmac_sm3_init(&c, d->K, SMC_SM3_DIGEST);
    hmac_sm3_update(&c, d->V, SMC_SM3_DIGEST);
    hmac_sm3_update(&c, &sep, 1);
    if (seedlen) hmac_sm3_update(&c, seed, seedlen);
    hmac_sm3_final(&c, d->K);

    hmac_sm3_init(&c, d->K, SMC_SM3_DIGEST);
    hmac_sm3_update(&c, d->V, SMC_SM3_DIGEST);
    hmac_sm3_final(&c, d->V);

    if (seedlen) {
        sep = 0x01;
        hmac_sm3_init(&c, d->K, SMC_SM3_DIGEST);
        hmac_sm3_update(&c, d->V, SMC_SM3_DIGEST);
        hmac_sm3_update(&c, &sep, 1);
        hmac_sm3_update(&c, seed, seedlen);
        hmac_sm3_final(&c, d->K);

        hmac_sm3_init(&c, d->K, SMC_SM3_DIGEST);
        hmac_sm3_update(&c, d->V, SMC_SM3_DIGEST);
        hmac_sm3_final(&c, d->V);
    }
}

void smc_drbg_seed(smc_drbg *d, const uint8_t *seed, size_t seedlen)
{
    int i;
    for (i = 0; i < SMC_SM3_DIGEST; i++) { d->K[i] = 0x00; d->V[i] = 0x01; }
    drbg_update(d, seed, seedlen, 0);
}

void smc_drbg_reseed(smc_drbg *d, const uint8_t *seed, size_t seedlen)
{
    drbg_update(d, seed, seedlen, 0);
}

void smc_drbg_bytes(smc_drbg *d, uint8_t *out, size_t len)
{
    size_t done = 0;
    while (done < len) {
        hmac_sm3_ctx c;
        size_t n = len - done;
        size_t i;
        hmac_sm3_init(&c, d->K, SMC_SM3_DIGEST);
        hmac_sm3_update(&c, d->V, SMC_SM3_DIGEST);
        hmac_sm3_final(&c, d->V);
        if (n > SMC_SM3_DIGEST) n = SMC_SM3_DIGEST;
        for (i = 0; i < n; i++) out[done + i] = d->V[i];
        done += n;
    }
    drbg_update(d, NULL, 0, 1);
}

#endif /* SMC_DRBG */

int smc_random(smc_rng *rng, uint8_t *buf, size_t len)
{
    if (rng && rng->entropy) return rng->entropy(rng->user, buf, len);
    return -1;   /* 无熵源：调用方应失败或使用确定性路径 */
}
