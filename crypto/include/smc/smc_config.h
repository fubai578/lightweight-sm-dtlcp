/*
 * smc_config.h — 编译期裁剪与内核选择配置
 *
 * 可移植嵌入式 C：C99，无动态内存，无 libc 依赖（算法层）。
 * 通过 -D 覆盖默认值实现"轻量化裁剪"：
 *   SMC_SM2_ENC=0        裁掉 SM2 公钥加密/解密（DTLCP 不需要）
 *   SMC_SM2_SIGN=0       裁掉 SM2 签名/验签（纯 PSK 部署可裁）
 *   SMC_SM2_ECDH=0       裁掉 ECDH（纯 PSK 部署可裁）
 *   SMC_SM4_MODE_CBC=0   裁掉 SM4-CBC
 *   SMC_SM4_MODE_CTR=0   裁掉 SM4-CTR
 *   SMC_SM3_KERNEL       1=REF 2=FAST(默认) 3=TINY(最小栈)
 *   SMC_SM4_KERNEL       1=REF 2=FAST(默认) 3=SMALL(最小常量表)
 *   SMC_SM2_KERNEL       1=REF 2=OPT(默认)   SMC_SM2_CT=1 常量时间标量乘
 */
#ifndef SMC_CONFIG_H
#define SMC_CONFIG_H

/* ---- 功能裁剪 ---- */
#ifndef SMC_SM2_ENC
#define SMC_SM2_ENC 1
#endif
#ifndef SMC_SM2_SIGN
#define SMC_SM2_SIGN 1
#endif
#ifndef SMC_SM2_ECDH
#define SMC_SM2_ECDH 1
#endif
#ifndef SMC_SM4_MODE_CBC
#define SMC_SM4_MODE_CBC 1
#endif
#ifndef SMC_SM4_MODE_CTR
#define SMC_SM4_MODE_CTR 1
#endif
#ifndef SMC_HMAC_SM3
#define SMC_HMAC_SM3 1
#endif
#ifndef SMC_DRBG
#define SMC_DRBG 1
#endif

/* ---- 内核选择 ---- */
#define SMC_KERNEL_REF   1
#define SMC_KERNEL_FAST  2   /* SM4: T-table 展开 */
#define SMC_KERNEL_TINY  3   /* SM3: 最小栈 */
#define SMC_KERNEL_SMALL 3   /* SM4: 最小常量表 */
#define SMC_KERNEL_OPT   2

#ifndef SMC_SM3_KERNEL
#define SMC_SM3_KERNEL SMC_KERNEL_FAST
#endif
#ifndef SMC_SM4_KERNEL
#define SMC_SM4_KERNEL SMC_KERNEL_FAST
#endif
#ifndef SMC_SM2_KERNEL
#define SMC_SM2_KERNEL SMC_KERNEL_OPT
#endif

/* SM2 标量乘常量时间变体（抗时序/SPA，性能约 2x 代价） */
#ifndef SMC_SM2_CT
#define SMC_SM2_CT 0
#endif

/* ---- 尺寸常量 ---- */
#define SMC_SM3_DIGEST 32
#define SMC_SM3_BLOCK  64
#define SMC_SM4_BLOCK  16
#define SMC_SM4_KEY    16
#define SMC_SM2_BYTES  32
#define SMC_SM2_SIG    64   /* r||s */
#define SMC_SM2_PUBKEY 65   /* 0x04||x||y */

#endif /* SMC_CONFIG_H */
