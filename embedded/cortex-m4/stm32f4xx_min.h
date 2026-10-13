/*
 * stm32f4xx_min.h — STM32F407 裸机最小寄存器定义（**不依赖 CMSIS / 不需要下载 ST 头文件**）
 *
 * 只定义板级自检与基准真正用到的外设：DWT 周期计数、RNG、USART1、GPIOA、RCC、FLASH、SCB。
 * 目的：让 embedded/ 目录**自包含**——拿到板子的人 clone 仓库后只需一个 arm-none-eabi 工具链，
 * 无需再去找 stm32f4xx.h（这是本仓库此前的一个实际卡点）。
 *
 * 若你的工程已经有 CMSIS/HAL，请不要用本文件，直接把 smc_port_stm32.c 的 include 换成
 * "stm32f4xx.h" 与 "core_cm4.h" 即可（两者接口一致）。
 *
 * 地址来源：STM32F407 参考手册 RM0090（寄存器偏移按手册定义）。
 * 编译宿主仿真（SMC_PORT_HOST_SIM）时，"外设地址"映射到真实内存，从而可在 PC 上跑通板级代码。
 */
#ifndef STM32F4XX_MIN_H
#define STM32F4XX_MIN_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ *
 * 地址映射：把外设地址转成"可解引用的指针值"。
 *   目标机：就是字面地址（MMIO）。
 *   宿主仿真：映射到普通内存数组，从而能在 PC 上执行同一份板级代码
 *             （见 host_sim_main.c；用于上板前发现编译/流程/打印问题）。
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

/* ---------------- RCC（时钟使能） ---------------- */
#define RCC_BASE        SMC_PA(0x40023800u)
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30u))  /* GPIOAEN=bit0 */
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40u))  /* USART2EN=bit17, PWREN=bit28 */
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x44u))  /* USART1EN=bit4 */
#define RCC_AHB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x34u))  /* RNGEN=bit6 */
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x08u))  /* SWS=bit2..3 */

#define RCC_AHB1ENR_GPIOAEN     (1u << 0)
#define RCC_APB1ENR_USART2EN    (1u << 17)
#define RCC_APB1ENR_PWREN       (1u << 28)
#define RCC_APB2ENR_USART1EN    (1u << 4)
#define RCC_AHB2ENR_RNGEN       (1u << 6)

/* ---------------- FLASH（等待周期） ---------------- */
#define FLASH_BASE      SMC_PA(0x40023C00u)
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00u))

/* ---------------- GPIO（USART 引脚复用） ---------------- */
typedef struct {
    volatile uint32_t MODER, OTYPER, OSPEEDR, PUPDR, IDR, ODR, BSRR, LCKR, AFR[2];
} GPIO_Regs;

#define GPIOA_BASE      SMC_PA(0x40020000u)
#define GPIOA           ((GPIO_Regs *)GPIOA_BASE)
#define GPIO_AF7        0x7u                 /* USART1/2 的复用功能编号 */

/* ---------------- USART1（默认调试串口：PA9=TX, PA10=RX） ---------------- */
typedef struct {
    volatile uint32_t SR, DR, BRR, CR1, CR2, CR3, GTPR;
} USART_Regs;

#define USART1_BASE     SMC_PA(0x40011000u)
#define USART1          ((USART_Regs *)USART1_BASE)
#define USART_SR_TXE    (1u << 7)
#define USART_SR_RXNE   (1u << 5)
#define USART_SR_TC     (1u << 6)
#define USART_CR1_UE    (1u << 13)
#define USART_CR1_TE    (1u << 3)
#define USART_CR1_RE    (1u << 2)

/* ---------------- RNG（硬件真随机数） ---------------- */
typedef struct {
    volatile uint32_t CR, SR, DR;
} RNG_Regs;

#define RNG_BASE        SMC_PA(0x50060800u)
#define RNG             ((RNG_Regs *)RNG_BASE)
#define RNG_CR_RNGEN    (1u << 2)
#define RNG_CR_IE       (1u << 3)
#define RNG_SR_DRDY     (1u << 0)
#define RNG_SR_CECS     (1u << 1)   /* 时钟错误 */
#define RNG_SR_SECS     (1u << 2)   /* 种子错误 */
#define RNG_SR_CEIS     (1u << 5)
#define RNG_SR_SEIS     (1u << 6)

/* ---------------- IWDG（看门狗，可选） ---------------- */
#define IWDG_BASE       SMC_PA(0x40003000u)
#define IWDG_KR         (*(volatile uint32_t *)(IWDG_BASE + 0x00u))
#define IWDG_PR         (*(volatile uint32_t *)(IWDG_BASE + 0x04u))
#define IWDG_RLR        (*(volatile uint32_t *)(IWDG_BASE + 0x08u))
#define IWDG_SR         (*(volatile uint32_t *)(IWDG_BASE + 0x0Cu))
#define IWDG_KR_RELOAD  0xAAAAu
#define IWDG_KR_ENABLE  0xCCCCu

/* ---------------- 内核外设：DWT / SCB（周期计数、FPU） ---------------- */
typedef struct {
    volatile uint32_t CTRL, CYCCNT, CPICNT, EXCCNT, SLEEPCNT, LSUCNT, FOLDCNT, PCSR;
} DWT_Regs;

#define DWT_BASE        SMC_CA(0xE0001000u)
#define DWT             ((DWT_Regs *)DWT_BASE)
#define DWT_CTRL_CYCCNTENA  (1u << 0)

#define DEMCR           (*(volatile uint32_t *)SMC_CA(0xE000EDFCu))
#define DEMCR_TRCENA    (1u << 24)

#define SCB_CPACR       (*(volatile uint32_t *)SMC_CA(0xE000ED88u))

/* 编译器内存屏障（ARMCC/GCC 通用写法；宿主仿真下为空） */
#if defined(__arm__)
#define SMC_DSB()   __asm__ __volatile__("dsb" ::: "memory")
#define SMC_ISB()   __asm__ __volatile__("isb" ::: "memory")
#else
#define SMC_DSB()   ((void)0)
#define SMC_ISB()   ((void)0)
#endif

#endif /* STM32F4XX_MIN_H */
