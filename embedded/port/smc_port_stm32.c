/*
 * smc_port_stm32.c — Cortex-M4 平台移植参考实现（STM32F407）
 *
 * 依赖：stm32f4xx_min.h（本仓库自带的最小寄存器定义，**不需要 CMSIS / 不需要下载 ST 头文件**）。
 *   若你的工程已有 CMSIS/HAL：把第 20 行的 include 换成 "stm32f4xx.h" 即可，接口一致。
 *
 * 若芯片无硬件 RNG（如 STM32F401CCU6 / F103C8T6）：
 *   smc_port_rng_bytes 必须换成合格熵源（外部 TRNG 芯片 / ADC·环形振荡器 + 健康测试），
 *   且必须满足 docs/03 §4 的熵源要求；**不要**用软件 PRNG 顶替。
 *
 * 宿主仿真（-DSMC_PORT_HOST_SIM）：寄存器映射到普通内存、周期计数用宿主时钟换算，
 *   用于上板前验证板级代码的编译/流程/KAT/打印（见 host_sim_main.c）。
 */
#include "smc_port.h"
#include "stm32f4xx_min.h"

#if defined(SMC_PORT_HOST_SIM)
#include <time.h>
#include <stdio.h>      /* putchar：仿真下把调试输出打到 stdout */
#endif

/* ---------------------------------------------------------------- 板级参数 */
/* 复位后 SYSCLK 默认是 HSI 16 MHz。本骨架**不配置 PLL**，故默认按 16 MHz 计：
   这样首次上板不会因晶振频率猜错而"跑不起来"。
     - 想跑 168 MHz：确认板载晶振后配置 PLL，并用 -DSMC_CPU_HZ=168000000 覆盖。
     - cycles 由 DWT 直接计数、与频率无关；频率只影响 µs 换算与功耗折算。 */
#ifndef SMC_CPU_HZ
#define SMC_CPU_HZ 16000000u
#endif

/* 调试串口：默认 USART1（PA9=TX / PA10=RX），F407 最小系统板最常见的引出串口。
   若板载 CH340 接在别的串口，改本文件 uart_init() 与 docs/08 的对照表。 */
#define SMC_UART_BAUD 115200u
#define SMC_RNG_SPIN_MAX 1000000u

/* ------------------------------------------------------------------ 内部工具 */
static uint32_t usart_brr(uint32_t f_hz, uint32_t baud)
{
    /* BRR 数值上等于 round(f/baud)（低 4 位为小数部分） */
    return (f_hz + baud / 2u) / baud;
}

static void uart_init(void)
{
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC_APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC_APB2ENR;

    /* PA9/PA10 → AF7 推挽高速 */
    GPIOA->MODER = (GPIOA->MODER & ~((3u << 18) | (3u << 20))) | ((2u << 18) | (2u << 20));
    GPIOA->OTYPER &= ~((1u << 9) | (1u << 10));
    GPIOA->OSPEEDR |= ((3u << 18) | (3u << 20));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~((3u << 18) | (3u << 20))) | (1u << 18);
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFu << 4) | (0xFu << 8)))
                  | ((GPIO_AF7 << 4) | (GPIO_AF7 << 8));

    USART1->BRR = usart_brr(SMC_CPU_HZ, SMC_UART_BAUD);
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

/* -------------------------------------------------------------- 平台钩子实现 */
int smc_port_init(void)
{
    /* FPU 使能（Cortex-M4F）：printf 打印浮点会用到；本模块本身不用浮点 */
    SCB_CPACR |= (0xFu << 20);
    SMC_DSB();
    SMC_ISB();

    DEMCR |= DEMCR_TRCENA;                 /* 使能跟踪与 DWT */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA;

    uart_init();

    RCC_APB1ENR |= RCC_APB1ENR_PWREN;      /* 后续要调压/低功耗时用 */
    (void)RCC_APB1ENR;
    return 0;
}

#if defined(SMC_PORT_HOST_SIM)
/* 宿主仿真：宿主单调时钟 → 按 SMC_CPU_HZ 折算成 cycles，使同一份测量代码可跑通。
   该值**不是板上实测**，只用于上板前验证测量流程与量级。 */
uint64_t smc_port_cycles(void)
{
    struct timespec ts;
    uint64_t ns;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return ns * (SMC_CPU_HZ / 1000000u) / 1000u;
}
#else
uint64_t smc_port_cycles(void)
{
    return (uint64_t)DWT->CYCCNT;
}
#endif

uint32_t smc_port_cpu_hz(void)
{
    return SMC_CPU_HZ;
}

void smc_port_watchdog_feed(void)
{
    /* 默认不启看门狗：首次调试若启用，单步/断点会被复位。
       需要时取消下面一行注释，并把 IWDG 超时设为长于最慢一次 SM2 运算。 */
    /* IWDG_KR = IWDG_KR_RELOAD; */
}

void smc_port_delay_us(uint32_t us)
{
#if defined(SMC_PORT_HOST_SIM)
    (void)us;                              /* 仿真下不真延时，避免自检过慢 */
#else
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SMC_CPU_HZ / 1000000u);
    while ((uint32_t)(DWT->CYCCNT - start) < ticks) { /* busy wait */ }
#endif
}

/* ------------------------------------------------------------- 调试输出（串口） */
#if defined(SMC_PORT_HOST_SIM)
void smc_port_putc(int ch)
{
    putchar(ch);                       /* 宿主仿真：直接打到 stdout */
    if (ch == '\n') putchar('\r');
}
#else
void smc_port_putc(int ch)
{
    while ((USART1->SR & USART_SR_TXE) == 0) { /* 等发送寄存器空 */ }
    USART1->DR = (uint32_t)(ch & 0xFFu);
    if (ch == '\n') {                  /* 串口工具普遍按 CRLF 显示更整齐 */
        while ((USART1->SR & USART_SR_TXE) == 0) { }
        USART1->DR = (uint32_t)'\r';
    }
}
#endif

/* ------------------------------------------------------------------ 硬件 RNG */
#if defined(SMC_PORT_HOST_SIM)
/* 宿主仿真专用：内存后端的 RNG->DR 反复读不会变化，会触发健康测试的"样本重复"而误报。
   故仿真下用 xorshift 生成有变化的序列，保证流程可跑通。
   ★ 仅用于上板前的流程验证；**真实随机数只能来自板上 TRNG**。 */
static uint32_t sim_rng_state = 0x2545F491u;
static uint32_t sim_rng_next(void)
{
    uint32_t x = sim_rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    sim_rng_state = x;
    return x;
}

int smc_port_rng_enable(void) { return 0; }

int smc_port_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) {
        if ((i & 3u) == 0u) sim_rng_state ^= (uint32_t)i * 2654435761u;
        buf[i] = (uint8_t)(sim_rng_next() >> ((i & 3u) * 8u));
    }
    return 0;
}

int smc_port_rng_selftest(int *ones_pct_out)
{
    uint32_t v[32], ones = 0, i, j;
    int dup = 0;
    for (i = 0; i < 32; i++) {
        v[i] = sim_rng_next();
        for (j = 0; j < i; j++) if (v[j] == v[i]) dup = 1;
        for (j = 0; j < 32u; j++) if ((v[i] >> j) & 1u) ones++;
    }
    if (ones_pct_out) *ones_pct_out = (int)((ones * 100u) / (32u * 32u));
    if (dup) return -4;
    return 0;
}

#else /* ---------------- 真实硬件路径 ---------------- */

int smc_port_rng_enable(void)
{
    RCC_AHB2ENR |= RCC_AHB2ENR_RNGEN;
    (void)RCC_AHB2ENR;
    RNG->CR |= RNG_CR_RNGEN;
    return 0;
}

int smc_port_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) {
        uint32_t sr, spins = 0;
        do {
            sr = RNG->SR;
            if (sr & (RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) {
                return -2;                 /* 种子/时钟错误：立即失败，绝不返回弱随机 */
            }
            if (++spins > SMC_RNG_SPIN_MAX) return -1;
        } while ((sr & RNG_SR_DRDY) == 0);
        buf[i] = (uint8_t)(RNG->DR >> ((i & 3u) * 8u));
    }
    return 0;
}

/*
 * 熵源健康测试（docs/03 §4 要求的最小自查）
 *  ① 连续取样无 DRDY 超时、无 SECS/CECS；
 *  ② 32 个 32 位样本互不重复（捕获卡死/常量输出）；
 *  ③ 总体 1 的比例落在 [35%, 65%]（捕获近乎全 0/全 1）。
 * 注：这是**上板自检用的最小检查**，不等于 GM/T 要求的完整 BIST/PCT；
 *     正式产品应实现《随机数检测规范》的完整项目并在产线执行。
 */
int smc_port_rng_selftest(int *ones_pct_out)
{
    uint32_t v[32];
    uint32_t ones = 0, i, j;
    int dup = 0;

    smc_port_rng_enable();
    for (i = 0; i < 32; i++) {
        uint32_t spins = 0, sr;
        do {
            sr = RNG->SR;
            if (sr & (RNG_SR_SECS | RNG_SR_CEIS | RNG_SR_SEIS)) return -2;
            if (++spins > SMC_RNG_SPIN_MAX) return -3;
        } while ((sr & RNG_SR_DRDY) == 0);
        v[i] = RNG->DR;
        for (j = 0; j < i; j++) if (v[j] == v[i]) dup = 1;
        for (j = 0; j < 32u; j++) if ((v[i] >> j) & 1u) ones++;
    }
    if (dup) return -4;
    {
        uint32_t pct = (ones * 100u) / (32u * 32u);
        if (ones_pct_out) *ones_pct_out = (int)pct;
        if (pct < 35u || pct > 65u) return -5;
    }
    return 0;
}
#endif /* SMC_PORT_HOST_SIM */

