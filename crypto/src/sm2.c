/*
 * sm2.c — SM2 签名/验签/ECDH/公钥加密（GB/T 32918.2/.3/.4）
 *
 * 轻量化裁剪：SMC_SM2_ENC=0 时公钥加密模块整体消失（DTLCP 只用签名+ECDH）。
 * nonce 策略：k = HMAC-SM3(d, "SM2-NONCE"‖dgst‖rnd‖ctr) 重试至合法；
 * rnd=NULL 时完全确定性（测试复现用），生产环境必须传入 TRNG 随机数（docs/03 §4）。
 */
#include <string.h>
#include "smc/sm2.h"
#include "smc/sm3.h"
#include "smc/hmac_sm3.h"
#include "sm2_internal.h"

static void base_point(sm2_pt *g)
{
    sm2_fe_from_bytes(&g->x, SM2_PARAM_GX);
    sm2_fe_from_bytes(&g->y, SM2_PARAM_GY);
}

/* ---------------- 公钥与密钥生成 ---------------- */
int sm2_check_pubkey(const uint8_t pub[SMC_SM2_PUBKEY])
{
    sm2_pt p;
    return sm2_parse_pubkey(&p, pub);   /* 0 = 合法；负值 = 拒绝 */
}

int sm2_pub_from_priv(uint8_t pub[SMC_SM2_PUBKEY], const uint8_t d[SMC_SM2_BYTES])
{
    sm2_fe k;
    sm2_pt q;
    sm2_fe_from_bytes(&k, d);
    sm2_fn_mod(&k, &k);
    if (!sm2_fn_in_range(&k)) return -1;
    sm2_scalarmult_base(&q, &k);
    sm2_encode_pubkey(pub, &q);
    return 0;
}

int sm2_keygen(uint8_t d[SMC_SM2_BYTES], uint8_t pub[SMC_SM2_PUBKEY], smc_rng *rng)
{
    int tries;
    for (tries = 0; tries < 64; tries++) {
        sm2_fe k;
        if (rng && rng->entropy) {
            if (rng->entropy(rng->user, d, SMC_SM2_BYTES) != 0) return -1;
        } else {
            smc_drbg dr;
            uint8_t seed[8] = {'S','M','2','-','K','E','Y', (uint8_t)tries};
            smc_drbg_seed(&dr, seed, sizeof seed);
            smc_drbg_bytes(&dr, d, SMC_SM2_BYTES);
        }
        sm2_fe_from_bytes(&k, d);
        sm2_fn_mod(&k, &k);
        if (sm2_fn_in_range(&k)) {
            sm2_fe_to_bytes(d, &k);
            return sm2_pub_from_priv(pub, d);
        }
    }
    return -1;
}

/* ---------------- ZA 计算 ---------------- */
void sm2_compute_za(uint8_t za[SMC_SM3_DIGEST], const char *id, size_t idlen,
                    const uint8_t pub[SMC_SM2_PUBKEY])
{
    sm3_ctx c;
    uint8_t entl[2];
    uint16_t bits;
    if (idlen > 8191) idlen = 8191;    /* ENTL 为 16 位比特长度 */
    bits = (uint16_t)(idlen * 8);

    entl[0] = (uint8_t)(bits >> 8);
    entl[1] = (uint8_t)bits;
    sm3_init(&c);
    sm3_update(&c, entl, 2);
    sm3_update(&c, (const uint8_t *)id, idlen);
    sm3_update(&c, SM2_PARAM_A, 32);
    sm3_update(&c, SM2_PARAM_B, 32);
    sm3_update(&c, SM2_PARAM_GX, 32);
    sm3_update(&c, SM2_PARAM_GY, 32);
    sm3_update(&c, pub + 1, 32);
    sm3_update(&c, pub + 33, 32);
    sm3_final(&c, za);
}

/* ---------------- nonce 派生 ---------------- */
static int derive_k(sm2_fe *k, const uint8_t *d, const uint8_t *dgst,
                    const uint8_t *rnd, uint8_t ctr)
{
    hmac_sm3_ctx c;
    uint8_t out[SMC_SM3_DIGEST];
    static const char label[9] = {'S','M','2','-','N','O','N','C','E'};

    hmac_sm3_init(&c, d, SMC_SM2_BYTES);
    hmac_sm3_update(&c, (const uint8_t *)label, 9);
    hmac_sm3_update(&c, dgst, SMC_SM3_DIGEST);
    if (rnd) hmac_sm3_update(&c, rnd, SMC_SM2_BYTES);
    hmac_sm3_update(&c, &ctr, 1);
    hmac_sm3_final(&c, out);

    sm2_fe_from_bytes(k, out);
    sm2_fn_mod(k, k);
    return sm2_fn_in_range(k);
}

#if SMC_SM2_SIGN

int sm2_sign_with_k(uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
                    const uint8_t d[SMC_SM2_BYTES], const uint8_t kbytes[SMC_SM2_BYTES])
{
    sm2_fe e, dk, kk, r, s, t1, t2, one, rk;
    sm2_pt R;
    uint8_t x1[32];

    sm2_fe_from_bytes(&e, dgst);
    sm2_fn_mod(&e, &e);                        /* e = H(ZA‖M) mod n */
    sm2_fe_from_bytes(&dk, d);
    sm2_fn_mod(&dk, &dk);
    sm2_fe_from_bytes(&kk, kbytes);
    sm2_fn_mod(&kk, &kk);
    if (!sm2_fn_in_range(&kk) || !sm2_fn_in_range(&dk)) return -1;

    sm2_scalarmult_base(&R, &kk);
    sm2_fe_to_bytes(x1, &R.x);
    sm2_fe_from_bytes(&r, x1);
    sm2_fn_mod(&r, &r);
    sm2_fn_add(&r, &r, &e);                    /* r = (e + x1) mod n */

    if (sm2_fe_is_zero(&r)) return -2;
    sm2_fn_add(&rk, &r, &kk);
    if (sm2_fe_is_zero(&rk)) return -2;        /* r + k ≡ 0 (mod n) 拒绝 */

    sm2_fe_set_one(&one);
    sm2_fn_add(&t1, &one, &dk);                /* (1 + d) */
    sm2_fn_inv(&t1, &t1);
    sm2_fn_mul(&t2, &r, &dk);                  /* r·d */
    sm2_fn_sub(&t2, &kk, &t2);                 /* k − r·d */
    sm2_fn_mul(&s, &t1, &t2);                  /* s = (1+d)^-1 (k − r·d) */

    if (sm2_fe_is_zero(&s)) return -3;
    sm2_fe_to_bytes(sig, &r);
    sm2_fe_to_bytes(sig + 32, &s);
    return 0;
}

int sm2_sign(uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
             const uint8_t d[SMC_SM2_BYTES], const uint8_t rnd[SMC_SM2_BYTES])
{
    uint8_t ctr;
    for (ctr = 0; ctr < 32; ctr++) {
        sm2_fe k;
        if (!derive_k(&k, d, dgst, rnd, ctr)) continue;
        {
            uint8_t kb[32];
            sm2_fe_to_bytes(kb, &k);
            if (sm2_sign_with_k(sig, dgst, d, kb) == 0) return 0;
        }
    }
    return -1;
}

int sm2_verify(const uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
               const uint8_t pub[SMC_SM2_PUBKEY])
{
    sm2_fe e, r, s, t, rx;
    sm2_pt G, PA, R;
    uint8_t x1[32];

    sm2_fe_from_bytes(&r, sig);
    sm2_fe_from_bytes(&s, sig + 32);
    if (!sm2_fn_in_range(&r) || !sm2_fn_in_range(&s)) return -1;

    sm2_fn_add(&t, &r, &s);                    /* t = (r + s) mod n */
    if (sm2_fe_is_zero(&t)) return -2;

    if (sm2_parse_pubkey(&PA, pub) != 0) return -3;
    base_point(&G);

    if (sm2_scalarmult_add(&R, &s, &G, &t, &PA) != 0) return -4;  /* R' = sG + tPA */

    sm2_fe_to_bytes(x1, &R.x);
    sm2_fe_from_bytes(&rx, x1);
    sm2_fn_mod(&rx, &rx);
    sm2_fe_from_bytes(&e, dgst);
    sm2_fn_mod(&e, &e);
    sm2_fn_add(&rx, &rx, &e);                  /* R = (e + x1') mod n */
    return (sm2_fe_cmp(&rx, &r) == 0) ? 0 : -5;
}

#endif /* SMC_SM2_SIGN */

#if SMC_SM2_ECDH

int sm2_ecdh(uint8_t shared[SMC_SM2_BYTES], const uint8_t d[SMC_SM2_BYTES],
             const uint8_t peer_pub[SMC_SM2_PUBKEY])
{
    sm2_fe dk;
    sm2_pt P, Q;

    sm2_fe_from_bytes(&dk, d);
    sm2_fn_mod(&dk, &dk);
    if (!sm2_fn_in_range(&dk)) return -1;
    if (sm2_parse_pubkey(&P, peer_pub) != 0) return -2;   /* 非法曲线点拒绝 */

    {
        sm2_jac j;
        sm2_scalarmult_jac(&j, &dk, &P);
        if (j.inf) return -3;
        sm2_jac_to_affine(&Q, &j);
    }
    sm2_fe_to_bytes(shared, &Q.x);
    return 0;
}

#endif /* SMC_SM2_ECDH */

#if SMC_SM2_ENC

/* GB/T 32918.4：C = C1‖C3‖C2（out 需 97+msglen 字节） */
int sm2_encrypt(uint8_t *out, size_t *outlen, const uint8_t *msg, size_t msglen,
                const uint8_t pub[SMC_SM2_PUBKEY], smc_rng *rng)
{
    sm2_pt PB, Q, C1p;
    sm2_fe dk;
    uint8_t x2b[32], y2b[32];
    int attempt;

    if (sm2_parse_pubkey(&PB, pub) != 0) return -1;

    for (attempt = 0; attempt < 32; attempt++) {
        uint8_t kb[32];
        uint8_t z[64];
        int allz = 1;
        size_t j;

        if (rng && rng->entropy && rng->entropy(rng->user, kb, 32) == 0) {
            smc_drbg dr;
            smc_drbg_seed(&dr, kb, 32);
            smc_drbg_bytes(&dr, kb, 32);
        } else {
            uint8_t seed[2] = {'E', (uint8_t)attempt};
            smc_drbg dr;
            smc_drbg_seed(&dr, seed, 2);
            smc_drbg_bytes(&dr, kb, 32);
        }
        sm2_fe_from_bytes(&dk, kb);
        sm2_fn_mod(&dk, &dk);
        if (!sm2_fn_in_range(&dk)) continue;

        {   /* C1 = kG, (x2,y2) = k·PB */
            sm2_jac j;
            sm2_scalarmult_base(&C1p, &dk);
            sm2_scalarmult_jac(&j, &dk, &PB);
            if (j.inf) continue;
            sm2_jac_to_affine(&Q, &j);
        }
        sm2_fe_to_bytes(x2b, &Q.x);
        sm2_fe_to_bytes(y2b, &Q.y);

        out[0] = 0x04;
        sm2_fe_to_bytes(out + 1, &C1p.x);
        sm2_fe_to_bytes(out + 33, &C1p.y);

        /* t = KDF(x2‖y2, msglen) 暂存于 out+96；全零 t 则重试 */
        memcpy(z, x2b, 32);
        memcpy(z + 32, y2b, 32);
        smc_sm3_kdf(z, 64, out + 97, msglen);
        for (j = 0; j < msglen; j++) if (out[97 + j]) allz = 0;
        if (allz && msglen) continue;
        for (j = 0; j < msglen; j++) out[97 + j] ^= msg[j];   /* C2 = M ^ t */

        /* C3 = SM3(x2 ‖ M ‖ y2) */
        {
            sm3_ctx c;
            sm3_init(&c);
            sm3_update(&c, x2b, 32);
            sm3_update(&c, msg, msglen);
            sm3_update(&c, y2b, 32);
            sm3_final(&c, out + 65);
        }
        *outlen = 97 + msglen;
        return 0;
    }
    return -1;
}

int sm2_decrypt(uint8_t *out, size_t *outlen, const uint8_t *in, size_t inlen,
                const uint8_t d[SMC_SM2_BYTES])
{
    sm2_pt C1, Q;
    sm2_fe dk;
    uint8_t x2b[32], y2b[32], u[32], z[64];
    size_t msglen, i;

    if (inlen < 97) return -1;
    msglen = inlen - 97;
    if (sm2_parse_pubkey(&C1, in) != 0) return -2;

    sm2_fe_from_bytes(&dk, d);
    sm2_fn_mod(&dk, &dk);
    if (!sm2_fn_in_range(&dk)) return -3;
    {
        sm2_jac j;
        sm2_scalarmult_jac(&j, &dk, &C1);
        if (j.inf) return -4;
        sm2_jac_to_affine(&Q, &j);
    }
    sm2_fe_to_bytes(x2b, &Q.x);
    sm2_fe_to_bytes(y2b, &Q.y);

    memcpy(z, x2b, 32);
    memcpy(z + 32, y2b, 32);
    smc_sm3_kdf(z, 64, out, msglen);
    for (i = 0; i < msglen; i++) out[i] ^= in[97 + i];

    {
        sm3_ctx c;
        sm3_init(&c);
        sm3_update(&c, x2b, 32);
        sm3_update(&c, out, msglen);
        sm3_update(&c, y2b, 32);
        sm3_final(&c, u);
    }
    if (memcmp(u, in + 65, 32) != 0) {
        for (i = 0; i < msglen; i++) out[i] = 0;
        return -5;
    }
    *outlen = msglen;
    return 0;
}

#endif /* SMC_SM2_ENC */
