/*
 * startup_stm32f103c8.c — STM32F103C8T6 最小启动文件（向量表 + Reset_Handler）
 *
 * 与 F4 版的向量表结构一致（Cortex-M3 与 M4 的内核异常向量顺序相同），
 * 但 F1 的外设中断向量更少，且**多了 WWDG/IWDG** 两个位置差异，这里按 RM0008 排列。
 *
 * 完整工程建议使用 CubeMX 生成的启动文件替换本骨架。
 */
#include <stdint.h>

extern uint32_t _sidata, _sdata, _edata, _sbss, _ebss, _estack;
extern int main(void);

void Reset_Handler(void);
void Default_Handler(void);

void Default_Handler(void) { for (;;) { } }

/*
 * 向量表必须**不能被优化掉**。写 static 时编译器会看到"没人引用"而直接丢弃整个
 * section（链接结果 = 0 字节 ELF，--gc-sections 会把其余代码一并回收）。
 *   ① 用 attribute((used)) 保证对象被保留；
 *   ② 再用 KEEP(*(.isr_vector)) 在链接脚本侧兜底（双保险）。
 * 踩坑记录：早期版本漏了 used，链接成功但产物为 0 字节，非常难查。
 */
__attribute__((section(".isr_vector"), used))
static void (*const g_vectors[])(void) = {
    (void (*)(void))(&_estack),
    Reset_Handler,
    Default_Handler, /* NMI         */
    Default_Handler, /* HardFault   */
    Default_Handler, /* MemManage   */
    Default_Handler, /* BusFault    */
    Default_Handler, /* UsageFault  */
    0, 0, 0, 0,
    Default_Handler, /* SVCall      */
    Default_Handler, /* DebugMon    */
    0,
    Default_Handler, /* PendSV      */
    Default_Handler, /* SysTick     */
    /* 外设中断（RM0008 顺序）；本骨架全部用轮询，不使能任何外设中断 */
    Default_Handler, /* WWDG        */
    Default_Handler, /* PVD         */
    Default_Handler, /* TAMPER      */
    Default_Handler, /* RTC         */
    Default_Handler, /* FLASH       */
    Default_Handler, /* RCC         */
    Default_Handler, /* EXTI0       */
    Default_Handler, /* EXTI1       */
    Default_Handler, /* EXTI2       */
    Default_Handler, /* EXTI3       */
    Default_Handler, /* EXTI4       */
    Default_Handler, /* DMA1_Ch1    */
    Default_Handler, /* DMA1_Ch2    */
    Default_Handler, /* DMA1_Ch3    */
    Default_Handler, /* DMA1_Ch4    */
    Default_Handler, /* DMA1_Ch5    */
    Default_Handler, /* DMA1_Ch6    */
    Default_Handler, /* DMA1_Ch7    */
    Default_Handler, /* ADC1_2      */
    Default_Handler, /* USB_HP_CAN1_TX */
    Default_Handler, /* USB_LP_CAN1_RX0 */
    Default_Handler, /* CAN1_RX1    */
    Default_Handler, /* CAN1_SCE    */
    Default_Handler, /* EXTI9_5     */
    Default_Handler, /* TIM1_BRK    */
    Default_Handler, /* TIM1_UP     */
    Default_Handler, /* TIM1_TRG_COM */
    Default_Handler, /* TIM1_CC     */
    Default_Handler, /* TIM2        */
    Default_Handler, /* TIM3        */
    Default_Handler, /* TIM4        */
    Default_Handler, /* I2C1_EV     */
    Default_Handler, /* I2C1_ER     */
    Default_Handler, /* I2C2_EV     */
    Default_Handler, /* I2C2_ER     */
    Default_Handler, /* SPI1        */
    Default_Handler, /* SPI2        */
    Default_Handler, /* USART1      */
    Default_Handler, /* USART2      */
    Default_Handler, /* USART3      */
    Default_Handler, /* EXTI15_10   */
    Default_Handler, /* RTCAlarm    */
    Default_Handler, /* USBWakeUp   */
};

void Reset_Handler(void)
{
    uint32_t *src = &_sidata, *dst = &_sdata;
    while (dst < &_edata) *dst++ = *src++;
    for (dst = &_sbss; dst < &_ebss; ) *dst++ = 0;
    main();
    for (;;) { }
}