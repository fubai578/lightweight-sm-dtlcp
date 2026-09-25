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
