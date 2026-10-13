/*
 * main.c — Cortex-M4 裸机入口（精简版）
 *
 * 职责只有三件：初始化平台、调用共用自检（board_selftest.c）、循环喂狗。
 * 测量逻辑全部在 board_selftest.c 里，与宿主仿真共享同一份代码，
 * 因此上板前就能在 PC 上验证 KAT、打印与流程（见 host_sim_main.c / scripts/check_embedded.sh）。
 *
 * printf 的重定向：
 *   GCC + newlib(-nano) 走 _write()；ARMCC/MDK 走 __io_putchar()，两者都指向 smc_port_putc()。
 *   本自检**只打印整数**，因此不需要浮点格式化（无需 -u _printf_float），可显著省 Flash。
 */
#include "smc_port.h"

void board_selftest(void);

#if !defined(__ARMCC_VERSION) && !defined(__CC_ARM)
/* ---- newlib / GCC：重定向 _write（printf 的落点） ---- */
int _write(int fd, const char *buf, int len)
{
    int i;
    (void)fd;
    for (i = 0; i < len; i++) smc_port_putc(buf[i]);
    return len;
}
/* 少数场合还会引用 _read/_close，给最小实现避免链接报错 */
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _close(int fd) { (void)fd; return -1; }
#else
/* ---- ARMCC / MDK：__io_putchar ---- */
int __io_putchar(int ch)
{
    smc_port_putc(ch);
    return ch;
}
#endif

int main(void)
{
    smc_port_init();
    board_selftest();
    for (;;) {
        smc_port_watchdog_feed();
    }
}
