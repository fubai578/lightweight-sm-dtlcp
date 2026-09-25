/*
 * smc_rng.h — 随机数抽象 + HMAC-SM3 DRBG
 * 生产固件必须接入硬件 TRNG（见 docs/03 固件安全改造规范 §4），
 * 本模块提供确定性 DRBG 供测试/仿真与 TRNG 后处理使用。
 */
#ifndef SMC_RNG_H
#define SMC_RNG_H

#include <stddef.h>
#include <stdint.h>
#include "smc_config.h"
#include "hmac_sm3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 熵源回调：填充 len 字节熵，返回 0 成功 */
typedef int (*smc_entropy_fn)(void *user, uint8_t *buf, size_t len);

typedef struct {
    smc_entropy_fn entropy;   /* 可为 NULL（纯确定性，仅供测试） */
    void *user;
} smc_rng;

#if SMC_DRBG
typedef struct {
    uint8_t K[SMC_SM3_DIGEST];
    uint8_t V[SMC_SM3_DIGEST];
} smc_drbg;

void smc_drbg_seed(smc_drbg *d, const uint8_t *seed, size_t seedlen);
void smc_drbg_reseed(smc_drbg *d, const uint8_t *seed, size_t seedlen);
void smc_drbg_bytes(smc_drbg *d, uint8_t *out, size_t len);
#endif

/* 统一取随机接口：优先熵源，退化到 DRBG（由调用方持有） */
int smc_random(smc_rng *rng, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* SMC_RNG_H */
