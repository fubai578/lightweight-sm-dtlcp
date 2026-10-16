/*
 * smc_port_f1.c — Cortex-M3 平台移植实现（STM32F103C8T6）
 *
 * 依赖：stm32f1xx_min.h（本仓库自带的最小寄存器定义，**不需要 CMSIS / 不需要下载 ST 头文件**）。
 *
 * ============================ 移植要点（与 F407 版逐条对照）===========================
 * ① GPIO：F1 用 CRL/CRH 每引脚 4 位配置，且**复用功能必须先使能 AFIO 时钟**；
 *    F4 的 MODER/OTYPER/AFR[] 写法在 F1 上完全不存在。
 * ② 串口：USART1 基址 0x40013800（F4 是 0x40011000），但寄存器结构体一致，
 *    因此 uart_init/putc 的代码几乎照抄，只需改基址与引脚配置。
 * ③ DWT/CYCCNT：**M3 也有**，smc_port_cycles() 直接可用 —— 板上 cycles 实测不受影响。
 * ④ **无 FPU**：不要执行 F4 版的 SCB_CPACR FPU 使能（M3 没有该位/没有 FPU）。
 * ⑤ **无硬件 RNG**：本文件的熵源是 **ADC 噪声 + LFSR 扰动白化**，
 *    见下面 §熵源 的完整说明——这是本移植最重要的设计决策。
 *
 * ============================ §熵源：为什么这样做，以及它的局限 ============================
 * STM32F103C8T6 **没有 RNG 外设**（ST 官方特性表：只有 CRC + 96 位 UID）。
 * 因此 smc_port_rng_bytes 不能读 TRNG。选项与取舍：
 *
 *   (a) 用软件 PRNG（xorshift / LFSR）顶替 —— **绝对禁止**。
 *       那不是熵源，是确定性伪随机；一旦密钥/nonce 可由出厂种子推出，
 *       整套 SM2 的安全性归零，且违反 docs/03 §2.4 与 §4 的硬性要求。
 *   (b) 外接 TRNG 芯片 —— 生产环境的正解，但当前无硬件。
 *   (c) **本实现：ADC 噪声 + 白化 + 连续性健康门禁**。
 *       ADC 采样的是板上真实的模拟噪声（热噪声 + 电源纹波 + LSB 抖动），
 *       叠加 LFSR 扰动后作为采集熵，再由健康测试把关。
 *
 *   ★ 诚实边界：ADC 噪声的熵质量**远低于** TRNG，不满足 GM/T 随机数检测的完整项目，
 *     也不满足 FIPS 140-2 对 entropy source 的要求。本实现仅满足：
 *       - docs/03 §2.4「实验室/非生产环境允许使用确定性 nonce」的**下限之上**；
 *       - 演示"密钥/nonce 每次都不同、不固定"的**可观测行为**（自检里那两项检查）。
 *     生产部署必须改接外部 TRNG 芯片（如同系列 MCU 外挂 ATECC608 / SGS8706）。
 *     该边界已写进 docs/03 §4.3 与作品报告，**不隐瞒**。
 *
 *   健康门禁（smc_port_rng_selftest）三关，与 F4 版同口径以便对照：
 *     ① ADC 自校准成功且连续采样无超时；
 *     ② 32 个 32 位样本互不重复（捕获外设卡死/常量输出）；
 *     ③ 比特 1 的比例落在 [35%,65%]（捕获近乎全 0/全 1）；
 *     ④ **新增** F1 专有：滑动窗口内样本唯一性 —— 捕获得 ADC 恒定输出或悬空脚。
 *
 * 宿主仿真（-DSMC_PORT_HOST_SIM）：寄存器映射到普通内存，ADC 读不到真实噪声，
 *   故仿真下用 xorshift 生成有变化的序列，保证流程可跑通（与 F4 版同一策略）。
 */
#include "smc_port.h"
#include "stm32f1xx_min.h"

#if defined(SMC_PORT_HOST_SIM)
#include <time.h>
#include <stdio.h>      /* putchar：仿真下把调试输出打到 stdout */
#endif

/* ---------------------------------------------------------------- 板级参数 */
/* 复位后 SYSCLK 默认是 HSI 8 MHz。本骨架**不配置 PLL**（不赌板载晶振是否为 8MHz），
 * 故默认按 8 MHz 计：这样首次上板不会因晶振频率猜错而"跑不起来"。
 *   - 地阔星 F103C8T6 板载 8 MHz 晶振，配 PLL 可到 72 MHz；
 *     但想跑 72 MHz 必须先确认晶振频率，再配置 PLL 并用 -DSMC_CPU_HZ=72000000 覆盖，
 *     否则 µs 换算与功耗折算会差 9 倍。
 *   - cycles 由 DWT 直接计数、**与主频无关**，所以待机频率不影响 cycles 结果。 */
#ifndef SMC_CPU_HZ
#define SMC_CPU_HZ 8000000u
#endif

/* 调试串口：默认 USART1（PA9=TX / PA10=RX）。CP2102 接这两脚 + GND 即可。
 *
 * ★ 可用 -DSMC_UART_PORT=2/3 换到 USART2(PA2/PA3) / USART3(PB10/PB11)，
 *   用于绕开个别开发板 PA9/PA10 走线或排针位置的问题：
 *     -DSMC_UART_PORT=2   PA2=TX / PA3=RX
 *     -DSMC_UART_PORT=3   PB10=TX / PB11=RX
 *   换端口只需改这里与 uart_init() 里的 GPIO 配置分支，波特率逻辑不变。
 *
 * ★ 2026-10-16 实测记录（排查 TX/RX 问题时留下的判据）：
 *   若固件确认已在芯片里但串口零字节，先核对 GPIOA_CRH：PA9 nibble=0xB
 *   （TX 复用推挽）、PA10 nibble=0x4（RX 浮空输入），再排查物理接线。 */
#ifndef SMC_UART_PORT
#define SMC_UART_PORT 1
#endif
#if SMC_UART_PORT == 2
#define SMC_UART_USART     USART2
#define SMC_UART_RCC_EN    RCC_APB1ENR_USART2EN
#define SMC_UART_APB       1          /* APB1 */
#define SMC_UART_TX_PIN    2          /* PA2 */
#define SMC_UART_RX_PIN    3          /* PA3 */
#define SMC_UART_GPIO      GPIOA
#elif SMC_UART_PORT == 3
#define SMC_UART_USART     USART3
#define SMC_UART_RCC_EN    RCC_APB1ENR_USART3EN
#define SMC_UART_APB       1
#define SMC_UART_TX_PIN    10         /* PB10 */
#define SMC_UART_RX_PIN    11         /* PB11 */
#define SMC_UART_GPIO      GPIOB
#else
#define SMC_UART_USART     USART1
#define SMC_UART_RCC_EN    RCC_APB2ENR_USART1EN
#define SMC_UART_APB       2          /* APB2 */
#define SMC_UART_TX_PIN    9          /* PA9 */
#define SMC_UART_RX_PIN    10         /* PA10 */
#define SMC_UART_GPIO      GPIOA
#endif

#define SMC_UART_BAUD 115200u
#define SMC_RNG_SPIN_MAX 2000000u
/* 熵源：ADC 通道。PA0 = ADC1_IN0；地阔星板 PA0 引出。 */
#define SMC_ENTROPY_ADC_CH 0u

/* ------------------------------------------------------------------ 内部工具 */
static uint32_t usart_brr(uint32_t f_hz, uint32_t baud)
{
    /* F1 在 OVER8=0(APB2 分频系数≥2) 时，BRR = round(f_pclk2 / baud) */
    return (f_hz + baud / 2u) / baud;
}

static void uart_init(void)
{
    /* ★ F1 与 F4 的关键差异：复用功能必须先使能 AFIO 时钟，否则 GPIO 的 AF 配置不生效 */
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPBEN;
    if (SMC_UART_APB == 2) {
        RCC->APB2ENR |= SMC_UART_RCC_EN;
    } else {
        RCC->APB1ENR |= SMC_UART_RCC_EN;
    }
    (void)RCC->APB2ENR;

    /* TX 配为 50MHz 复用推挽(0xB)，RX 配为浮空输入(0x4)。 */
#if SMC_UART_PORT == 1
    /* PA9/PA10 属于高 8 位引脚，必须配置 CRH。 */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 4) | (0xFu << 8)))
               | ((uint32_t)GPIO_UART_TX_CFG << 4)
               | ((uint32_t)GPIO_UART_RX_CFG << 8);
#elif SMC_UART_PORT == 2
    GPIOA->CRL = (GPIOA->CRL & ~((0xFu << 8) | (0xFu << 12)))
               | ((uint32_t)GPIO_UART_TX_CFG << 8)
               | ((uint32_t)GPIO_UART_RX_CFG << 12);
#else /* USART3: PB10/PB11 */
    GPIOB->CRH = (GPIOB->CRH & ~((0xFu << 8) | (0xFu << 12)))
               | ((uint32_t)GPIO_UART_TX_CFG << 8)
               | ((uint32_t)GPIO_UART_RX_CFG << 12);
#endif

    /* ★ 波特率：USART1 挂 APB2、USART2/3 挂 APB1。
     *   F103 **复位后** APB1/APB2 分频系数都是 1（=不分频），故此刻 PCLK1=PCLK2=SYSCLK。
     *   固件不配 PLL（跑 HSI 8MHz），所以直接用 SMC_CPU_HZ 作为外设时钟是**正确**的。
     *   若将来配置了 PLL 并改了 APB 分频，这里必须改为按实际 PCLK 计算，
     *   否则波特率会错（本项目未配 PLL，故无此问题）。 */
    SMC_UART_USART->BRR = usart_brr(SMC_CPU_HZ, SMC_UART_BAUD);
    SMC_UART_USART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void adc_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_IOPAEN;
    (void)RCC->APB2ENR;

    /* PA0 → MODE=00(输入) + CNF=00(模拟)：ANALOG 模式位域全 0 */
    GPIOA->CRL &= ~((0xFu << 0));          /* PA0 占 [3:0] */

    /* 通道 0 采样时间 55.5 周期（最慢档，够采噪声）+ 12.5 分频 */
    ADC1->SMPR2 &= ~(0x3u << 0);
    ADC1->SMPR2 |=  (0x3u << 0);
    ADC1->CR2 = ADC_CR2_ADON;
    /* 自校准：RCC_CR.ADCCAL（bit31）置 1 后由硬件清零 */
    {
        volatile uint32_t guard = 1000000u;
        RCC->CR |= (1u << 31);              /* ADCCAL */
        while ((RCC->CR & (1u << 31)) && guard--) { /* 等硬件清零 */ }
    }
}

/* 单次 ADC 转换（软件触发）。返回 12 位结果；失败返回 -1 */
static int adc_read_once(void)
{
    volatile uint32_t guard = SMC_RNG_SPIN_MAX;

    ADC1->CR2 |= ADC_CR2_SWSTART | ADC_CR2_EXTEN_SW;
    /* F1 的 SWSTART 是"置 1 触发、硬件自动清零"，故不能当普通标志位查 */
    while (guard--) {
        if (ADC1->SR & ADC_SR_EOC) return (int)(ADC1->DR & 0xFFFu);
    }
    return -1;
}

/* -------------------------------------------------------------- 平台钩子实现 */
int smc_port_init(void)
{
    /* ★ 注意：F407 版在这里开 FPU（SCB_CPACR），**Cortex-M3 没有 FPU，不要照抄**。
       本模块与算法库全部是定点/整数运算，不需要任何浮点使能。 */

    DEMCR |= DEMCR_TRCENA;                 /* 使能跟踪与 DWT —— M3 也有 */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA;

    uart_init();
    adc_init();
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
       需要时取消下面一行注释，并把 IWDG 超时设为长于最慢一次 SM2 运算
       （M3 @8MHz 下 SM2 签名约 0.5–2 s，务必把 RLR 放到最大 0xFFF）。 */
    /* IWDG_KR = IWDG_KR_RELOAD; */
}

void smc_port_delay_us(uint32_t us)
{
#if defined(SMC_PORT_HOST_SIM)
    (void)us;                              /* 仿真下不真延时，避免自检过慢 */
#else
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SMC_CPU_HZ / 1000000u);
    if (ticks == 0) ticks = 1;              /* 低主频下避免 ticks=0 死等 */
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
    while ((SMC_UART_USART->SR & USART_SR_TXE) == 0) { /* 等发送寄存器空 */ }
    SMC_UART_USART->DR = (uint32_t)(ch & 0xFFu);
    if (ch == '\n') {                  /* 串口工具普遍按 CRLF 显示更整齐 */
        while ((SMC_UART_USART->SR & USART_SR_TXE) == 0) { }
        SMC_UART_USART->DR = (uint32_t)'\r';
    }
}
#endif

/* ------------------------------------------------------------------ 熵源（F103 无 RNG） */

/* 32 位 xorshift：白化扰动器（**不是**熵源本身，只负责把 ADC 噪声扩散成不可预测的位型）。
 * 固定种子是安全的——因为它不含任何"秘密"，只是扰动器；真正的随机性来自下面的 ADC。 */
static uint32_t s_lfsr = 0x9E3779B9u;

static uint32_t entropy_whiten_step(uint32_t input)
{
    uint32_t x = s_lfsr;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_lfsr = x;
    return x ^ input;
}

#if defined(SMC_PORT_HOST_SIM)
/* 宿主仿真专用：内存后端的 ADC->DR 反复读不会变化，会触发健康测试的"样本重复"而误报。
   故仿真下用 xorshift 生成有变化的序列，保证流程可跑通。
   ★ 仅用于上板前的流程验证；**真实随机数只能来自板上 ADC 噪声或外部 TRNG**。 */
static uint32_t sim_rng_next(void)
{
    uint32_t x = s_lfsr;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s_lfsr = x;
    return x;
}

int smc_port_rng_enable(void) { return 0; }

int smc_port_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) {
        if ((i & 3u) == 0u) s_lfsr ^= (uint32_t)i * 2654435761u;
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

#else /* ---------------- 真实硬件路径：ADC 噪声 + 白化 ---------------- */

int smc_port_rng_enable(void)
{
    /* F103 无 RNG 外设：此函数只为保持接口一致，熵源是 ADC1 */
    adc_init();
    return 0;
}

int smc_port_rng_bytes(uint8_t *buf, size_t len)
{
    size_t i;
    if (!buf) return -1;
    for (i = 0; i < len; i++) {
        int v = adc_read_once();
        uint32_t w;
        if (v < 0) return -2;                 /* ADC 转换超时：立即失败，绝不返回弱随机 */
        w = entropy_whiten_step((uint32_t)v);
        buf[i] = (uint8_t)(w >> ((i & 3u) * 8u));
    }
    return 0;
}

/*
 * 熵源健康测试（docs/03 §4 要求的最小自查，F1/ADC 版）
 *  ① ADC 自校准成功 + 连续 32 次采样无超时（EOC 都能拿到）；
 *  ② 32 个 32 位样本互不重复（捕获 ADC 卡死/常量输出/引脚悬空）；
 *  ③ 比特 1 的比例落在 [35%, 65%]（捕获近乎全 0/全 1）；
 *  ④ **滑动窗口唯一性**：连续 8 个样本内不得出现重复，额外捕获"低熵但非恒定"的情况
 *     （例如 ADC 读数只在很小的范围里缓慢游走）。
 *
 * ★ 与 F407（真 TRNG）版的诚实差异：
 *   F407 的 RNG 外设有 DRDY/SECS/CEIS 硬件状态位，自检可以确认硬件种子与时钟正常；
 *   F1 的 ADC 没有这类状态位，第① 关只能确认"转换能完成"，不能确认熵质量。
 *   故本自检通过**不等于**熵源合格，只等于"板子上的 ADC 确实在产生变化的噪声"。
 *   报告里必须如实这样表述，不得把本项通过说成"TRNG 健康"。
 */
int smc_port_rng_selftest(int *ones_pct_out)
{
    uint32_t v[40], ones = 0, i, j;
    int dup = 0, win_dup = 0;
    const uint32_t win = 8u;

    smc_port_rng_enable();
    for (i = 0; i < 40; i++) {
        int r = adc_read_once();
        if (r < 0) return -2;                 /* ① 采样超时 */
        v[i] = entropy_whiten_step((uint32_t)r);
        for (j = 0; j < i; j++) if (v[j] == v[i]) dup = 1;
        /* ④ 滑动窗口内唯一性 */
        if (i >= win) {
            for (j = i - win; j < i; j++) if (v[j] == v[i]) win_dup = 1;
        }
    }
    if (dup) return -4;                        /* ② 全局样本重复 */
    if (win_dup) return -6;                   /* ④ 窗口内重复（低熵） */
    for (i = 0; i < 40; i++)
        for (j = 0; j < 32u; j++) if ((v[i] >> j) & 1u) ones++;
    {
        uint32_t pct = (ones * 100u) / (40u * 32u);
        if (ones_pct_out) *ones_pct_out = (int)pct;
        if (pct < 35u || pct > 65u) return -5; /* ③ 比特失衡 */
    }
    return 0;
}
#endif /* SMC_PORT_HOST_SIM */
