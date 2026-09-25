/*
 * sm3.h — SM3 密码杂凑算法（GM/T 0004-2012）
 * REF/OPT/TINY 内核由 SMC_SM3_KERNEL 编译期选择，API 一致。
 */
#ifndef SMC_SM3_H
#define SMC_SM3_H

#include <stddef.h>
#include <stdint.h>
#include "smc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t h[8];            /* 链接变量 */
    uint64_t total;           /* 已吸收字节数 */
    uint8_t  buf[SMC_SM3_BLOCK];
    uint32_t buflen;
} sm3_ctx;

void sm3_init(sm3_ctx *ctx);
void sm3_update(sm3_ctx *ctx, const uint8_t *data, size_t len);
void sm3_final(sm3_ctx *ctx, uint8_t out[SMC_SM3_DIGEST]);
void sm3(const uint8_t *data, size_t len, uint8_t out[SMC_SM3_DIGEST]);

/* 压缩函数（供 HMAC/DRBG 复用与微基准） */
void sm3_compress(uint32_t h[8], const uint8_t block[SMC_SM3_BLOCK]);

#ifdef __cplusplus
}
#endif
#endif /* SMC_SM3_H */
