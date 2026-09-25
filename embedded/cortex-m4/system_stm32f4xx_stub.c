/*
 * system_stm32f4xx_stub.c — 系统初始化桩（时钟配置由 CubeMX/板级 BSP 完成）
 */
#include <stdint.h>

uint32_t SystemCoreClock = 168000000u;

void SystemInit(void)
{
    /* FPU 使能（Cortex-M4F） */
#if defined(__VFP_FP__) && !defined(__SOFTFP__)
    /* SCB->CPACR |= (0xF << 20); 由启动代码/CMSIS 完成 */
#endif
}
