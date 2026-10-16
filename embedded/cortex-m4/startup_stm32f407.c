/*
 * startup_stm32f407.c — 最小启动文件（向量表 + Reset_Handler）
 * 完整工程建议使用 CubeMX 生成的启动文件替换本骨架。
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

void Default_Handler(void) { for (;;) { } }

/* used 属性：保证向量表不被"未引用即丢弃"优化掉（F1 移植时踩过这个坑，
   漏掉会导致链接成功但 ELF 为 0 字节）。链接脚本侧另有 KEEP(*(.isr_vector)) 兜底。 */
__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler, /* NMI */
    Default_Handler, /* HardFault */
    Default_Handler, /* MemManage */
    Default_Handler, /* BusFault */
    Default_Handler, /* UsageFault */
    0, 0, 0, 0,
    Default_Handler, /* SVCall */
    Default_Handler, /* DebugMon */
    0,
    Default_Handler, /* PendSV */
    Default_Handler, /* SysTick */
    /* 外设中断按数据手册补充 */
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    main();
    for (;;) { }
}
