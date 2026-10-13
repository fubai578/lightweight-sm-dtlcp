/*
 * host_sim_main.c — 宿主仿真入口：在 PC 上执行**同一份**板级代码
 *
 * 为什么需要：CI/开发机通常没有 arm-none-eabi 工具链。板级代码若有编译错误、KAT 写错、
 * printf 格式不对、流程漏初始化（例如漏设基点），都要等上板才发现，白跑一趟硬件。
 * 本文件把外设寄存器映射到普通内存、用宿主时钟伪造 DWT，从而在 PC 上跑通
 * board_selftest()：KAT 是否通过、表格打印是否正常、测量循环是否跑完，一次看清。
 *
 * 运行：bash embedded/cortex-m4/build.sh sim      （或 scripts/check_embedded.sh）
 * 注意：仿真输出的 cycles 是"宿主耗时按 SMC_CPU_HZ 折算"的结果，**不是板上实测**，
 *       只用于验证流程与量级；真实数据必须来自板上 DWT。
 */
#if !defined(SMC_PORT_HOST_SIM)
#error "host_sim_main.c 需要 -DSMC_PORT_HOST_SIM"
#endif

#include <stdio.h>
#include <stdint.h>

/* ---- 伪造的外设地址空间 ----
 * 必须覆盖 port 里用到的全部地址：
 *   0x40000000..0x5007FFFF（RCC/FLASH/GPIO/USART/RNG/IWDG）
 *   0xE0000000..0xE000FFFF（DWT @0xE0001000、SCB @0xE000ED88、DEMCR @0xE000EDFC） */
uint8_t smc_sim_periph[0x180000];      /* 1.5 MB */
uint8_t smc_sim_core[0x0010000];       /* 64 KB */

/* RNG 由 port 在仿真模式下用 xorshift 实现（见 smc_port_stm32.c），此处无需伪造。 */

void board_selftest(void);

int main(void)
{
    printf("[host_sim] 外设寄存器映射到普通内存；DWT 由宿主时钟伪造。\n");
    printf("[host_sim] 输出的 cycles **不是板上实测**，仅用于验证流程与量级。\n");
    board_selftest();
    return 0;
}
