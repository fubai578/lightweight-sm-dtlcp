/*
 * uart_probe.c — 串口/固件存活定位固件（烧进 MCU 的诊断程序）
 *
 * 用途：当「烧录成功但串口零字节」时，本固件用三种可观测手段区分故障位置。
 * 它**不含任何算法、不含 printf、不含随机数**，只有裸寄存器操作，
 * 因此若它也无输出，可排除 printf/栈/算法路径，但仍须核对本文件的 GPIO/USART 配置。
 *
 * 三种可观测手段
 * --------------
 * ① 心跳灯：持续翻转 **PC13 与 PA5**。
 *    ⚠ 板载 LED 位置因板而异，实测有两种常见板：
 *      - **Blue Pill 克隆板（板上印 B10-F103，如照片所示）→ LED 接 PC13**
 *      - 立创·地阔星 F103C8T6 → LED 接 PA5
 *    本固件**同时驱动两个脚**，所以在任何一种板上都能看到闪烁。
 *    ⚠ Blue Pill 的 LED 是**低电平点亮**（LED 阳极经电阻接 V3.3，阴极接 MCU 脚），
 *      所以写 BRR（输出 0）才是"亮"。地阔星同为此接法。
 *    → 这是不需要任何软件的「固件是否在跑」判据。
 * ② USART1 输出：PA9=TX / PA10=RX，发 `U1=UART1_PA9` 及交替的 `1`。
 * ③ USART3 输出：PB10=TX / PB11=RX，发 `U3=UART3_PB10` 及交替的 `3`。
 *    → 两者**同时**输出，所以一次烧录就能判断「是特定引脚的问题」
 *      还是「整体接线思路的问题」。
 *
 * ★★ 最容易踩的坑：**BOOT0 跳线必须拨到 0**
 *   Blue Pill 顶部有两个跳线：**BOOT0** 与 **RST**。若 BOOT0 拨到「1」，
 *   芯片进 ISP 模式、**不从 Flash 启动**：此时 ST-Link 烧录照常成功（SWD 在
 *   ISP 下可用），但固件从未运行 → **串口一个字都没有**。
 *   现象与「接线错」完全一致，极易误判。本项目 2026-10-16 就卡在这里。
 *   ⇒ 烧任何固件前，先确认 BOOT0 = 0。
 *
 * 构建（在 embedded/cortex-m3/ 下）
 * -------------------------------
 *   bash build.sh uart_probe flash
 *   pyocd flash -M under-reset -f 100k -t stm32f103rc -e chip smc_m3_uart_probe.elf
 *   python ../../scripts/capture_board.py COM3 115200 15
 *
 * 判定树
 * ------
 *   LED 闪 + 无串口输出  → 固件正常，**纯接线问题**（TX/RX 未交叉或针脚位置错）
 *   LED 不闪            → 固件没跑起来：检查 BOOT0 是否拨到高（ISP 模式）、
 *                         复位电路、或 Flash 是否真的写入（用 pyocd 二次烧录看
 *                         是否报 skipped）
 *   LED 闪 + PA9 有输出但接 PB10 时无  → 换端口即可（用 build.sh ... uart2/uart3）
 */
#include "stm32f1xx_min.h"

#define BAUD_8M_115200 69u      /* BRR = round(8e6/115200) = 69 */

/* ---- USART1：PA9=TX(复用推挽)、PA10=RX ---- */
static void uart1_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN | RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;
    /* PA9/PA10 在 CRH；TX=复用推挽(0xB)，RX=浮空输入(0x4)。 */
    GPIOA->CRH = (GPIOA->CRH & ~((0xFu << 4) | (0xFu << 8)))
               | ((uint32_t)GPIO_UART_TX_CFG << 4)
               | ((uint32_t)GPIO_UART_RX_CFG << 8);
    USART1->BRR = BAUD_8M_115200;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

/* ---- USART3：PB10=TX、PB11=RX ---- */
static void uart3_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPBEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB2ENR;
    (void)RCC->APB1ENR;
    /* PB10/PB11 落在 CRH 的 [7:4] / [11:8] */
    GPIOB->CRH = (GPIOB->CRH & ~((0xFu << 8) | (0xFu << 12)))
               | ((uint32_t)GPIO_UART_TX_CFG << 8)
               | ((uint32_t)GPIO_UART_RX_CFG << 12);
    USART3->BRR = BAUD_8M_115200;
    USART3->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

static void send1(char c)
{
    while ((USART1->SR & USART_SR_TXE) == 0) { }
    USART1->DR = (uint32_t)(unsigned char)c;
}

static void send3(char c)
{
    while ((USART3->SR & USART_SR_TXE) == 0) { }
    USART3->DR = (uint32_t)(unsigned char)c;
}

int main(void)
{
    static const char banner[] =
        "\r\nU1=UART1_PA9  U3=UART3_PB10  LED=PC13+PA5\r\n";

    uart1_init();
    uart3_init();

    /* ---- 心跳灯：同时驱动 PC13(Blue Pill) 与 PA5(地阔星) ----
     *两者都是「低电平点亮」：写 BRR（置 0）= 亮，写 BSRR（置 1）= 灭。
     * PA5 落 GPIOA_CRL 的 [23:20]，PC13 落 GPIOC_CRH 的 [23:20]。
     * 均配成MODE=01(10MHz) + CNF=00(推挽) = 0x3。*/
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN | RCC_APB2ENR_IOPCEN;
    (void)RCC->APB2ENR;
    GPIOA->CRL = (GPIOA->CRL & ~(0xFu << 20)) | (0x3u << 20);   /* PA5  推挽 */
    GPIOC->CRH = (GPIOC->CRH & ~(0xFu << 20)) | (0x3u << 20);   /* PC13 推挽 */
    /* 上电初始置 1（灭），避免复位瞬间误亮造成误判 */
    GPIOA->BSRR = (1u << 5);
    GPIOC->BSRR = (1u << 13);

    /* 上电后主频需稳定（HSI 起振），稍等再发 */
    for (volatile long d = 0; d < 2000000L; d++) { }

    for (const char *p = banner; *p; p++) { send1(*p); send3(*p); }

    for (;;) {
        GPIOA->BRR  = (1u << 5);             /* PA5  = 0 → 亮 */
        GPIOC->BRR  = (1u << 13);            /* PC13 = 0 → 亮 */
        send1('1'); send3('3');
        for (volatile long d = 0; d < 400000L; d++) { }

        GPIOA->BSRR = (1u << 5);             /* 灭 */
        GPIOC->BSRR = (1u << 13);
        for (volatile long d = 0; d < 400000L; d++) { }
    }
}

/* ------------------------------------------------------------------ *
 * newlib 系统调用桩（链接必需，与 main.c 同一套）
 * 本固件不使用 printf，但 --gc-sections 之前链接器仍要解析这些符号。
 * _sbrk 返回失败：零动态内存是本工程的硬约束（赛题要求之一）。
 * ------------------------------------------------------------------ */
void *_sbrk(int incr)                { (void)incr; return (void *)-1; }
int  _close(int fd)                   { (void)fd; return -1; }
int  _fstat(int fd, void *st)         { (void)fd; (void)st; return -1; }
int  _isatty(int fd)                  { (void)fd; return 1; }
int  _lseek(int fd, int off, int wh)  { (void)fd; (void)off; (void)wh; return 0; }
int  _read(int fd, char *b, int n)    { (void)fd; (void)b; (void)n; return 0; }
int  _write(int fd, const char *b, int n) { (void)fd; (void)b; return n; }
int  _kill(int pid, int sig)          { (void)pid; (void)sig; return -1; }
int  _getpid(void)                    { return 1; }
void _exit(int code)                  { (void)code; for (;;) { } }
