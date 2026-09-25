/*
 * hmac_sm3.h — HMAC-SM3（RFC 2104 构造，GM/T 标准配套）
 */
#ifndef SMC_HMAC_SM3_H
#define SMC_HMAC_SM3_H

#include <stddef.h>
#include <stdint.h>
#include "smc_config.h"
#include "sm3.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    sm3_ctx inner;
    sm3_ctx outer;
} hmac_sm3_ctx;

void hmac_sm3_init(hmac_sm3_ctx *ctx, const uint8_t *key, size_t keylen);
void hmac_sm3_update(hmac_sm3_ctx *ctx, const uint8_t *data, size_t len);
void hmac_sm3_final(hmac_sm3_ctx *ctx, uint8_t out[SMC_SM3_DIGEST]);

/* 一次性接口 */
void hmac_sm3(const uint8_t *key, size_t keylen,
              const uint8_t *msg, size_t msglen,
              uint8_t out[SMC_SM3_DIGEST]);

/* KDF：SM3 计数器模式 KDF(Z, klen)（GB/T 32918.1 附录） */
void smc_sm3_kdf(const uint8_t *z, size_t zlen, uint8_t *out, size_t klen);

#ifdef __cplusplus
}
#endif
#endif /* SMC_HMAC_SM3_H */
