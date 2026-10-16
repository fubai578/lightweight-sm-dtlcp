/*
 * stm32f1xx_min.h — STM32F103C8T6 裸机最小寄存器定义（**不依赖 CMSIS / 不需要下载 ST 头文件**）
 *
 * 与 stm32f4xx_min.h 同风格：只定义板级自检与基准真正用到的外设，
 * 让 embedded/ 目录自包含——拿到板子的人 clone 仓库后只需一个 arm-none-eabi 工具链。
 *
 * 目标器件：STM32F103C8T6（Cortex-M3 @72MHz，64KB Flash，20KB RAM，**无硬件 RNG**）
 *   地址来源：STM32F103x8 参考手册 RM0008（寄存器偏移按手册定义）。
 *
 * ★ 关键差异（F4 → F1，与移植相关的坑都列在这里）：
 *   ① GPIO 是 CRL/CRH（每引脚 4 位配置位域），**不是** F4 的 MODER/OTYPER/AFR[]；
 *      F1 的复用功能配置在 **AFIO 的 CRL/CRH** 里，必须先使能 AFIO 时钟。
 *   ② 寄存器基址全部不同：RCC 0x40021000、USART1 0x40013800、GPIOA 0x40010800、
 *      FLASH 0x40022000、AFIO 0x40010000、ADC1 0x40012400。
 *   ③ **无 RNG 外设**（0x50060800 是 F4/F1 无关的地址，F1 上不存在）。
 *      熵源改用 ADC 噪声 + 白化，见 smc_port_f1.c。
 *   ④ **无 FPU**（Cortex-M3 没有 SCB_CPACR 使能位，不要照抄 F4 的 FPU 使能代码）。
 *   ⑤ DWT/CYCCNT 存在（M3 也有），**cycles 计数完全可用**——这是本项目的关键结论。
 *
 * 编译宿主仿真（SMC_PORT_HOST_SIM）时，"外设地址"映射到普通内存，从而可在 PC 上跑通板级代码。
 */
#ifndef STM32F1XX_MIN_H
#define STM32F1XX_MIN_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ *
 * 地址映射：把外设地址转成"可解引用的指针值"。
 *   目标机：就是字面地址（MMIO）。
 *   宿主仿真：映射到普通内存数组，从而能在 PC 上执行同一份板级代码。
 * ------------------------------------------------------------------ */
#if defined(SMC_PORT_HOST_SIM)
extern uint8_t smc_sim_periph[];   /* 覆盖 0x40000000..0x5007FFFF */
extern uint8_t smc_sim_core[];     /* 覆盖 0xE0000000..0xE000FFFF */
#define SMC_PA(a) ((uintptr_t)(smc_sim_periph + ((uint32_t)(a) - 0x40000000u)))
#define SMC_CA(a) ((uintptr_t)(smc_sim_core   + ((uint32_t)(a) - 0xE0000000u)))
#else
#define SMC_PA(a) ((uintptr_t)(a))
#define SMC_CA(a) ((uintptr_t)(a))
#endif

/* ---------------- RCC（时钟使能）— F1 在 0x40021000 ----------------
 * F1 的 RCC 是一整块连续寄存器，用结构体指针访问比 F4 版的一堆离散宏更顺手。 */
typedef struct {
    volatile uint32_t CR, CFGR, CIR, APB2RSTR, APB1RSTR, AHBENR, APB2ENR, APB1ENR,
                     BDCR, CSR;
} RCC_Regs;

#define RCC_BASE        SMC_PA(0x40021000u)
#define RCC             ((RCC_Regs *)RCC_BASE)
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00u))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x04u))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x18u))
#define RCC_AHBENR      (*(volatile uint32_t *)(RCC_BASE + 0x14u))
/* RCC_CR：HSION=bit0(默认开)、HSIRDY=bit1、HSITRIM=bit7 写入时校准 HSI */
#define RCC_CR_HSION     (1u << 0)
#define RCC_CR_HSIRDY    (1u << 1)
#define RCC_CR_HSITRIM   (1u << 7)

#define RCC_AHBENR_FSMCEN   (1u << 8)
#define RCC_AHBENR_IOPAEN   (1u << 2)   /* GPIOA */
#define RCC_APB2ENR_AFIOEN  (1u << 0)   /* AFIO（复用功能映射必需） */
#define RCC_APB2ENR_IOPAEN  (1u << 2)
#define RCC_APB2ENR_IOPBEN  (1u << 3)
#define RCC_APB2ENR_IOPCEN  (1u << 4)
#define RCC_APB2ENR_USART1EN (1u << 14)
#define RCC_APB2ENR_ADC1EN  (1u << 9)
#define RCC_APB2ENR_SPI1EN  (1u << 12)
#define RCC_APB1ENR_USART2EN (1u << 17)
#define RCC_APB1ENR_USART3EN (1u << 18)
#define RCC_APB1ENR_IWDGEN   (1u << 0)

/* ---------------- FLASH 控制器（等待周期）— F1 在 0x40022000 ---------------- */
#define FLASH_R_BASE    SMC_PA(0x40022000u)
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_R_BASE + 0x00u))
#define FLASH_KEYR      (*(volatile uint32_t *)(FLASH_R_BASE + 0x04u))
#define FLASH_OPTKEYR   (*(volatile uint32_t *)(FLASH_R_BASE + 0x08u))
#define FLASH_SR        (*(volatile uint32_t *)(FLASH_R_BASE + 0x0Cu))
#define FLASH_CR        (*(volatile uint32_t *)(FLASH_R_BASE + 0x10u))
#define FLASH_AR        (*(volatile uint32_t *)(FLASH_R_BASE + 0x14u))

/* ---------------- GPIO（F1：CRL/CRH + BSRR/BRR/IDR/ODR） ----------------
 * F1 每引脚 4 位：MODE[1:0] + CNF[1:0]。引脚 0-7 在 CRL，8-15 在 CRH。
 * MODE: 00=输入 01=10M输出 10=2M输出 11=50M输出
 * CNF (输入): 00=模拟 01=浮空 10=上拉/下拉(由 ODR 决定) 11=保留
 * CNF (输出): 00=通用推挽 01=通用开漏 10=复用推挽 11=复用开漏 */
typedef struct {
    volatile uint32_t CRL, CRH, IDR, ODR, BSRR, BRR, LCKR;
} GPIO_Regs;

#define GPIOA_BASE      SMC_PA(0x40010800u)
#define GPIOB_BASE      SMC_PA(0x40010C00u)
#define GPIOC_BASE      SMC_PA(0x40011000u)
#define GPIOA           ((GPIO_Regs *)GPIOA_BASE)
#define GPIOB           ((GPIO_Regs *)GPIOB_BASE)
#define GPIOC           ((GPIO_Regs *)GPIOC_BASE)

#define GPIO_MODE_INPUT     0x0u
#define GPIO_MODE_OUT_10M   0x1u
#define GPIO_MODE_OUT_2M    0x2u
#define GPIO_MODE_OUT_50M   0x3u
#define GPIO_CNF_IN_ANALOG  0x0u
#define GPIO_CNF_IN_FLOAT   0x1u
#define GPIO_CNF_IN_PULL    0x2u
#define GPIO_CNF_OUT_PP     0x0u
#define GPIO_CNF_OUT_OD     0x1u
#define GPIO_CNF_AF_PP      0x2u
#define GPIO_CNF_AF_OD      0x3u
/* 组合成 4 位配置值 */
#define GPIO_CFG(mode, cnf)  (((mode) & 3u) | (((cnf) & 3u) << 2))
/* USART TX 为 50MHz 复用推挽，RX 为浮空输入。 */
#define GPIO_UART_TX_CFG     GPIO_CFG(GPIO_MODE_OUT_50M, GPIO_CNF_AF_PP) /* 0xB */
#define GPIO_UART_RX_CFG     GPIO_CFG(GPIO_MODE_INPUT, GPIO_CNF_IN_FLOAT) /* 0x4 */
#define GPIO_PA0_CFG_ANALOG GPIO_CFG(GPIO_MODE_INPUT, GPIO_CNF_IN_ANALOG) /* 0x0 */

/* ---------------- AFIO（F1 复用功能重映射） ---------------- */
#define AFIO_BASE       SMC_PA(0x40010000u)
#define AFIO_EVCR       (*(volatile uint32_t *)(AFIO_BASE + 0x00u))
#define AFIO_MAPR       (*(volatile uint32_t *)(AFIO_BASE + 0x04u))
#define AFIO_EXTICR     (*(volatile uint32_t *)(AFIO_BASE + 0x08u))
/* AFIO_EVCR = 0x0000FFFF → 释放 JTAG(SWDIO/SWCLK) 之外的全引脚？**不要这么做**：
 *   本骨架靠 SWD 烧录与调试。默认 0x00000000 即 JTAG+SWD 全部保留，最安全。 */

/* ---------------- USART1（默认调试串口：PA9=TX, PA10=RX）— F1 在 0x40013800 ---------------- */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_Regs;

#define USART1_BASE     SMC_PA(0x40013800u)
#define USART2_BASE     SMC_PA(0x40004400u)
#define USART3_BASE     SMC_PA(0x40004800u)
#define USART1          ((USART_Regs *)USART1_BASE)
#define USART2          ((USART_Regs *)USART2_BASE)
#define USART3          ((USART_Regs *)USART3_BASE)
#define USART_SR_TXE    (1u << 7)
#define USART_SR_RXNE   (1u << 5)
#define USART_SR_TC     (1u << 6)
#define USART_CR1_UE    (1u << 13)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_RE    (1u << 2)

/* ---------------- ADC1（F103 熵源：ADC 噪声 + 白化，**不是** TRNG） ----------------
 * ★ 重要：ADC 噪声**不构成合格的密码学熵源**，仅满足"实验室演示 / 非生产"用途。
 *   报告与固件规范必须按 docs/03 §2.4 如实标注：生产环境必须外接 TRNG。
 *   详见 smc_port_f1.c 的详细说明。 */
typedef struct {
    volatile uint32_t SR, CR1, CR2, SMPR1, SMPR2, IER, DR, LTR, HTR;
    volatile uint32_t SQR1, SQR2, SQR3, JSQR, JDR1, JDR2, JDR3, JDR4;
} ADC_Regs;

#define ADC1_BASE       SMC_PA(0x40012400u)
#define ADC1            ((ADC_Regs *)ADC1_BASE)
#define ADC_CR2_ADON    (1u << 0)
#define ADC_CR2_RSTCAL  (1u << 2)
#define ADC_CR2_SWSTART (1u << 30)   /* F1 软件启动置 1 后需硬件清零 */
#define ADC_CR2_EXTEN_SW (1u << 28)
#define ADC_SR_EOC      (1u << 1)
#define ADC_SR_CAL      (1u << 2)

/* ---------------- SPI1（W5500：PA5=SCK / PA6=MISO / PA7=MOSI，CS 用 PB10 或 PA4） ---------------- */
typedef struct {
    volatile uint32_t CR1, CR2, SR, DR, CRCPR, RXCRCR, TXCRCR, I2SCFGR, I2SPR;
} SPI_Regs;

#define SPI1_BASE       SMC_PA(0x40013000u)
#define SPI1            ((SPI_Regs *)SPI1_BASE)
#define SPI_CR1_CPHA    (1u << 0)
#define SPI_CR1_CPOL    (1u << 1)
#define SPI_CR1_MSTR    (1u << 2)
#define SPI_CR1_BR_SHIFT 3
#define SPI_CR1_SPE     (1u << 6)
#define SPI_CR1_SSI     (1u << 8)
#define SPI_CR1_SSM     (1u << 9)
#define SPI_SR_RXNE     (1u << 0)
#define SPI_SR_TXE      (1u << 1)
#define SPI_SR_BSY      (1u << 7)
#define SPI_CR2_DS      (1u << 11)  /* F1 的数据长度位在 CR2[11:8] */

/* ---------------- IWDG（看门狗，可选）— F1 在 0x40003000 ---------------- */
#define IWDG_BASE       SMC_PA(0x40003000u)
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00u))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04u))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08u))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0Cu))
#define IWDG_KR_RELOAD  0xAAAAu
#define IWDG_KR_ENABLE  0xCCCCu

/* ---------------- 内核外设：DWT / DEMCR（周期计数，M3 也有！） ----------------
 * ★ Cortex-M3 同样有 DWT->CYCCNT，所以本项目的"板上 cycles/op 实测"
 *   在 F103C8T6 上完全可行，不因换成 M3 而失效。 */
typedef struct {
    volatile uint32_t CTRL, CYCCNT, CPICNT, EXCCNT, SLEEPCNT, LSUCNT, FOLDCNT, PCSR;
} DWT_Regs;

#define DWT_BASE        SMC_CA(0xE0001000u)
#define DWT             ((DWT_Regs *)DWT_BASE)
#define DWT_CTRL_CYCCNTENA  (1u << 0)
#define DWT_CTRL_NOCYCCNT    (1u << 0)  /* 复位后 DWT_CTRL=0，此位读作 0 表示计数器存在 */

#define DEMCR           (*(volatile uint32_t *)SMC_CA(0xE000EDFCu))
#define DEMCR_TRCENA    (1u << 24)

/* 编译器内存屏障（ARMCC/GCC 通用写法；宿主仿真下为空） */
#if defined(__arm__) || defined(__thumb__)
#define SMC_DSB()   __asm__ __volatile__("dsb" ::: "memory")
#define SMC_ISB()   __asm__ __volatile__("isb" ::: "memory")
#else
#define SMC_DSB()   ((void)0)
#define SMC_ISB()   ((void)0)
#endif

#endif /* STM32F1XX_MIN_H */
