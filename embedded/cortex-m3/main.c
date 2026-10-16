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
/* 少数场合还会引用 _read/_close，其完整桩集见文件末尾的"newlib 系统调用桩"一节 */
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

/* ------------------------------------------------------------------ *
 * newlib 需要的最小系统调用桩（裸机 **必须** 自备，否则链接失败）
 *
 * 现象：链接报 undefined reference to `_lseek' / `_fstat' / `_isatty' /
 *      `_sbrk' / `_exit' / `_kill' / `_getpid'。
 * 原因：newlib 的 stdio 会引用这批 POSIX 接口，即使你只调用 printf。
 *      交叉编译时链接的是 newlib 本身（不是 Linux libc），没有任何系统提供它们。
 * 处理：给全部桩**最小实现**。本工程不读写文件、不 fork、不用信号，
 *      所以除 `_sbrk` 外都可以简单地"什么都不做"。
 *
 * ⚠ _sbrk 是唯一有实际语义的：newlib 用它实现 malloc。这里**故意返回失败**
 *   （返回 (void*)-1），因为本工程零动态内存（赛题要求之一）：
 *   - 若代码路径里意外出现 malloc，它会**立刻失败并暴露**，
 *     而不是悄悄分配 20KB RAM 里根本不存在的大块内存；
 *   - 若确实需要动态内存，改成从一块静态池里切（并相应删掉这条注释）。
 * ------------------------------------------------------------------ */
void *_sbrk(ptrdiff_t incr)
{
    (void)incr;
    return (void *)-1;              /* 无堆：malloc 一律失败（见上） */
}

int _close(int fd)                { (void)fd; return -1; }
int _fstat(int fd, void *st)      { (void)fd; (void)st; return -1; }
int _isatty(int fd)               { (void)fd; return 1; }   /* 声称是终端：让 printf 走行缓冲策略 */
int _lseek(int fd, int off, int w){ (void)fd; (void)off; (void)w; return 0; }
int _read(int fd, char *b, int n) { (void)fd; (void)b; (void)n; return 0; }
int _kill(int pid, int sig)       { (void)pid; (void)sig; return -1; }
int _getpid(void)                 { return 1; }
void _exit(int code)              { (void)code; for (;;) { } }

int _times(void *t)               { (void)t; return -1; }
int _unlink(const char *n)        { (void)n; return -1; }
int _wait(int *st)                { (void)st; return -1; }
int _execve(const char *p, char *const a[], char *const e[])
{
    (void)p; (void)a; (void)e; return -1;
}
int _open(const char *p, int f, int m) { (void)p; (void)f; (void)m; return -1; }
