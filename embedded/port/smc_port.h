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

/* ---- 硬件 RNG 的使能与健康自检（可选，但接 TRNG 时必须实现） ----
 * 随机数健康要求见 docs/03 §4：熵源健康测试通过后方可用于播种/取 nonce。
 *   smc_port_rng_enable()   : 使能外设时钟与 RNG（幂等）
 *   smc_port_rng_selftest() : 返回 0=通过；负值=失败（-2 种子/时钟错误、-4 样本重复、-5 比特失衡）
 *                             ones_pct_out 非空时回填"1 的比例(%)"，便于打印 */
int smc_port_rng_enable(void);
int smc_port_rng_selftest(int *ones_pct_out);

/* ---- 调试输出（可选；重定向到串口/ITM/SWO）----
 * 板级自检与基准的 printf 依赖它；不实现时需自行提供 _write()/__io_putchar()。 */
void smc_port_putc(int ch);

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
