/*
 * hmac_sm3.c — HMAC-SM3 与 SM3 计数器 KDF
 */
#include "smc/hmac_sm3.h"

void hmac_sm3_init(hmac_sm3_ctx *ctx, const uint8_t *key, size_t keylen)
{
    uint8_t k[SMC_SM3_BLOCK];
    uint8_t pad[SMC_SM3_BLOCK];
    size_t i;

    for (i = 0; i < SMC_SM3_BLOCK; i++) k[i] = 0;
    if (keylen > SMC_SM3_BLOCK) {
        sm3(key, keylen, k);            /* 密钥先哈希到块长 */
    } else {
        for (i = 0; i < keylen; i++) k[i] = key[i];
    }
    for (i = 0; i < SMC_SM3_BLOCK; i++) pad[i] = k[i] ^ 0x36;
    sm3_init(&ctx->inner);
    sm3_update(&ctx->inner, pad, SMC_SM3_BLOCK);
    for (i = 0; i < SMC_SM3_BLOCK; i++) pad[i] = k[i] ^ 0x5c;
    sm3_init(&ctx->outer);
    sm3_update(&ctx->outer, pad, SMC_SM3_BLOCK);
}

void hmac_sm3_update(hmac_sm3_ctx *ctx, const uint8_t *data, size_t len)
{
    sm3_update(&ctx->inner, data, len);
}

void hmac_sm3_final(hmac_sm3_ctx *ctx, uint8_t out[SMC_SM3_DIGEST])
{
    uint8_t ih[SMC_SM3_DIGEST];
    sm3_final(&ctx->inner, ih);
    sm3_update(&ctx->outer, ih, SMC_SM3_DIGEST);
    sm3_final(&ctx->outer, out);
}

void hmac_sm3(const uint8_t *key, size_t keylen,
              const uint8_t *msg, size_t msglen,
              uint8_t out[SMC_SM3_DIGEST])
{
    hmac_sm3_ctx ctx;
    hmac_sm3_init(&ctx, key, keylen);
    hmac_sm3_update(&ctx, msg, msglen);
    hmac_sm3_final(&ctx, out);
}

void smc_sm3_kdf(const uint8_t *z, size_t zlen, uint8_t *out, size_t klen)
{
    uint32_t ct = 1;
    size_t done = 0;
    while (done < klen) {
        sm3_ctx c;
        uint8_t ctr_be[4];
        uint8_t h[SMC_SM3_DIGEST];
        size_t n;
        ctr_be[0] = (uint8_t)(ct >> 24); ctr_be[1] = (uint8_t)(ct >> 16);
        ctr_be[2] = (uint8_t)(ct >> 8);  ctr_be[3] = (uint8_t)ct;
        sm3_init(&c);
        sm3_update(&c, z, zlen);
        sm3_update(&c, ctr_be, 4);
        sm3_final(&c, h);
        n = klen - done;
        if (n > SMC_SM3_DIGEST) n = SMC_SM3_DIGEST;
        { size_t i; for (i = 0; i < n; i++) out[done + i] = h[i]; }
        done += n;
        ct++;
    }
}
