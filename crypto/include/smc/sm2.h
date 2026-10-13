/*
 * sm2.h — SM2 椭圆曲线公钥密码（GB/T 32918 / GM/T 0003）
 * 可裁剪：SMC_SM2_SIGN / SMC_SM2_ECDH / SMC_SM2_ENC
 */
#ifndef SMC_SM2_H
#define SMC_SM2_H

#include <stddef.h>
#include <stdint.h>
#include "smc_config.h"
#include "smc_rng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SM2_ID_DEFAULT      "1234567812345678"
#define SM2_ID_DEFAULT_LEN  16

/* 公钥合法性检查（on-curve / 坐标范围 / 未压缩格式） */
int sm2_check_pubkey(const uint8_t pub[SMC_SM2_PUBKEY]);

/* 由私钥导出公钥：pub = 0x04‖x‖y */
int sm2_pub_from_priv(uint8_t pub[SMC_SM2_PUBKEY], const uint8_t d[SMC_SM2_BYTES]);

/* 密钥生成（d ∈ [1,n-2]）；rng 为 NULL 时退化为确定性（仅供测试） */
int sm2_keygen(uint8_t d[SMC_SM2_BYTES], uint8_t pub[SMC_SM2_PUBKEY], smc_rng *rng);

/* ZA = SM3(ENTL‖ID‖a‖b‖Gx‖Gy‖xA‖yA)（GB/T 32918.2） */
void sm2_compute_za(uint8_t za[SMC_SM3_DIGEST], const char *id, size_t idlen,
                    const uint8_t pub[SMC_SM2_PUBKEY]);

#if SMC_SM2_SIGN
/* dgst = SM3(ZA‖M)；sig = r‖s（各 32 字节大端） */
int sm2_sign(uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
             const uint8_t d[SMC_SM2_BYTES], const uint8_t rnd[SMC_SM2_BYTES]);
/* 指定 nonce k（测试向量复现 / 确定性签名） */
int sm2_sign_with_k(uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
                    const uint8_t d[SMC_SM2_BYTES], const uint8_t k[SMC_SM2_BYTES]);
int sm2_verify(const uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
               const uint8_t pub[SMC_SM2_PUBKEY]);
#endif

#if SMC_SM2_PEER_TABLE
/*
 * 对端公钥预计算表（验签加速；用于网关等"同一公钥被反复验签"的场景）
 *
 * 为什么这里可以用查表、而签名路径必须常量时间——本项优化的立足点：
 *   验签的全部输入（签名 r‖s、消息摘要、公钥 P）都是**公开数据**，
 *   由它们导出的 t=(r+s) mod n 亦然。因此以 t 的位索引 P 的预计算表
 *   **不泄漏任何秘密**，无需常量时间；而签名路径的 k 与 d 是保密的，必须常量时间。
 * 效果：R = sG + tP 的变基点腿 tP 变成固定基点乘（与 sG 同为 comb 表驱动）。
 *
 * 用法（表由调用方提供存储，库不分配内存）：
 *   static uint32_t tbl[SMC_SM2_PEER_TABLE_BYTES / 4];   // 或用 malloc / 静态池
 *   sm2_peer_table_build(tbl, peer_pub);
 *   for (..) sm2_verify_with_table(sig, dgst, tbl);
 * 注意：表与公钥绑定；传错表会验签失败（fail-closed），不会误判为通过。
 */
size_t sm2_peer_table_bytes(void);   /* = SMC_SM2_PEER_TABLE_BYTES（见 smc_config.h） */
int sm2_peer_table_build(void *tbl, const uint8_t pub[SMC_SM2_PUBKEY]);
int sm2_verify_with_table(const uint8_t sig[SMC_SM2_SIG], const uint8_t dgst[SMC_SM3_DIGEST],
                          const void *tbl);
#endif

#if SMC_SM2_ECDH
/* ECDH：shared = x(d·Ppeer)；含公钥合法性检查（非法曲线/无穷远拒绝） */
int sm2_ecdh(uint8_t shared[SMC_SM2_BYTES], const uint8_t d[SMC_SM2_BYTES],
             const uint8_t peer_pub[SMC_SM2_PUBKEY]);
#endif

#if SMC_SM2_ENC
/* SM2 公钥加密（GB/T 32918.4，C = C1‖C3‖C2 编码，头 97 字节）；可整体裁剪 */
/* out 需 97 + msglen 字节 */
int sm2_encrypt(uint8_t *out, size_t *outlen, const uint8_t *msg, size_t msglen,
                const uint8_t pub[SMC_SM2_PUBKEY], smc_rng *rng);
int sm2_decrypt(uint8_t *out, size_t *outlen, const uint8_t *in, size_t inlen,
                const uint8_t d[SMC_SM2_BYTES]);
#endif

#ifdef __cplusplus
}
#endif
#endif /* SMC_SM2_H */
