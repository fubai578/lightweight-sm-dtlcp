/*
 * sm4.h — SM4 分组密码（GM/T 0002-2012）
 * 内核由 SMC_SM4_KERNEL 选择：1=REF(教科书) 2=FAST(T-table 4KB, 默认) 3=SMALL(仅 256B S-box)
 */
#ifndef SMC_SM4_H
#define SMC_SM4_H

#include <stddef.h>
#include <stdint.h>
#include "smc_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t rk[32];   /* 加密轮密钥；解密时若为 OPT 内核则已逆序存放 */
} sm4_ctx;

/* 密钥扩展（一次性，128 位密钥） */
void sm4_setkey_enc(sm4_ctx *ctx, const uint8_t key[SMC_SM4_KEY]);
void sm4_setkey_dec(sm4_ctx *ctx, const uint8_t key[SMC_SM4_KEY]);

/* 单块（128 位）加解密 */
void sm4_encrypt_block(const sm4_ctx *ctx, const uint8_t in[SMC_SM4_BLOCK], uint8_t out[SMC_SM4_BLOCK]);
void sm4_decrypt_block(const sm4_ctx *ctx, const uint8_t in[SMC_SM4_BLOCK], uint8_t out[SMC_SM4_BLOCK]);

#if SMC_SM4_MODE_CBC
void sm4_cbc_encrypt(const sm4_ctx *ctx, uint8_t iv[SMC_SM4_BLOCK],
                     const uint8_t *in, uint8_t *out, size_t len);   /* len%16==0 */
void sm4_cbc_decrypt(const sm4_ctx *ctx, uint8_t iv[SMC_SM4_BLOCK],
                     const uint8_t *in, uint8_t *out, size_t len);   /* len%16==0 */
#endif

#if SMC_SM4_MODE_CTR
/* 流式 CTR：ctr[16] 为完整计数器块（大端，最后 8 字节递增），就地/离地异或 */
void sm4_ctr_xor(const sm4_ctx *ctx, uint8_t ctr[SMC_SM4_BLOCK],
                 const uint8_t *in, uint8_t *out, size_t len);

/* 同上，但只自增末尾 inc_bytes 字节（1..8），其余字节在所有块间保持不变。
 * 供调用方把「记录序号」这类跨记录唯一化字段放在自增域**之外**：一旦自增跨过
 * 序号，记录 s 的第 n 块就会与记录 s+n 的第 1 块共用密钥流（two-time pad）。 */
void sm4_ctr_xor_n(const sm4_ctx *ctx, uint8_t ctr[SMC_SM4_BLOCK], unsigned inc_bytes,
                   const uint8_t *in, uint8_t *out, size_t len);
#endif

#ifdef __cplusplus
}
#endif
#endif /* SMC_SM4_H */
