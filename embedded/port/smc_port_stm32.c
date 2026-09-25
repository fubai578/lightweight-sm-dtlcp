/*
 * smc_port_stm32.c — Cortex-M4 平台移植参考实现（STM32F4/L4 系列）
 *
 * 依赖 CMSIS（core_cm4.h）；RNG 使用芯片硬件 RNG（RNG_HandleTypeDef 由 CubeMX 生成），
 * 若无硬件 RNG，可用 ADC 噪声+环形振荡器方案替代，但必须通过健康测试（docs/03 §4）。
 */
#include "smc_port.h"

/* ---- CMSIS 头（按芯片型号调整） ---- */
#include "stm32f4xx.h"

/* CPU 频率（与 SystemCoreClock 保持一致） */
#ifndef SMC_CPU_HZ
#define SMC_CPU_HZ 168000000u
#endif

int smc_port_init(void)
{
    /* 使能 DWT 周期计数 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    return 0;
}

uint64_t smc_port_cycles(void)
{
    return (uint64_t)DWT->CYCCNT;
}

uint32_t smc_port_cpu_hz(void)
{
    return SMC_CPU_HZ;
}

void smc_port_watchdog_feed(void)
{
    /* IWDG->KR = 0xAAAA; */
}

void smc_port_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SMC_CPU_HZ / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { /* busy */ }
}

/*
 * 硬件 RNG（示例：STM32 RNG 外设；替换为平台 TRNG）
 * 要求（docs/03 §4）：熵源健康测试通过后方可播种 DRBG；
 * 签名 nonce 等安全关键随机数必须来自 TRNG 或 TRNG 播种的 DRBG。
 */
int smc_port_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        uint32_t r;
        int spins = 0;
        while (((RNG->SR & (RNG_SR_DRDY | RNG_SR_CECS | RNG_SR_SECS)) != RNG_SR_DRDY)) {
            if (++spins > 1000000u) return -1;   /* RNG 故障 */
        }
        r = RNG->DR;
        buf[i] = (uint8_t)(r >> ((i & 3) * 8));
    }
    return 0;
}
