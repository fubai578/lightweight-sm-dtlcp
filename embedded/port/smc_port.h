/*
 * smc_port.h — 平台移植层（嵌入式目标必须实现以下钩子）
 *
 * 算法库本身零平台依赖；本层提供：
 *   1) 随机数（TRNG）    —— 私钥/nonce/IV 的熵源，安全关键
 *   2) 周期计数          —— 基准与看门狗喂狗节拍
 *   3) 看门狗 / 延时     —— 长运算（SM2）期间的系统服务
 * 参考实现：smc_port_stm32.c（Cortex-M4 DWT + HAL RNG）
 */
#ifndef SMC_PORT_H
#define SMC_PORT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 熵源：填充 len 字节硬件随机数，0=成功 ---- */
int smc_port_rng_bytes(uint8_t *buf, size_t len);

/* ---- 周期计数（DWT->CYCCNT 或等价） ---- */
uint64_t smc_port_cycles(void);
uint32_t smc_port_cpu_hz(void);

/* ---- 系统服务（可在长运算中调用） ---- */
void smc_port_watchdog_feed(void);
void smc_port_delay_us(uint32_t us);

/* ---- 平台初始化（时钟/外设） ---- */
int smc_port_init(void);

/*
 * 与 smc_rng.h 的对接示例：
 *   static int entropy_cb(void *u, uint8_t *b, size_t n) { (void)u; return smc_port_rng_bytes(b, n); }
 *   smc_rng rng = { entropy_cb, NULL };
 *   sm2_keygen(d, pub, &rng);
 */

#ifdef __cplusplus
}
#endif
#endif /* SMC_PORT_H */
