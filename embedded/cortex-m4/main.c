/*
 * main.c — Cortex-M4 裸机评测入口
 * 功能：KAT 自检 + 微基准（DWT 周期计数），结果经 UART/半主机输出
 * 本工程骨架展示移植方式；具体板级时钟/引脚按硬件调整。
 */
#include <stdio.h>
#include <string.h>
#include "smc_port.h"
#include "smc/sm3.h"
#include "smc/sm4.h"
#include "smc/sm2.h"

static volatile int g_ok;

static void check(int cond, const char *name)
{
    if (!cond) g_ok++;
    printf("[%s] %s\n", cond ? " ok " : "FAIL", name);
}

/* 简易 UART 输出桩：重定向到板级串口或 ITM/SWO */
int __io_putchar(int ch) { (void)ch; return ch; }   /* 板级实现替换 */

static void kat_sm3(void)
{
    uint8_t out[32], exp[32];
    /* SM3("abc") 标准向量（与 OpenSSL 交叉验证一致） */
    static const char *hex = "66c7f0f462eeedd9d1f2d46bdc10e4e2"
                             "4167c4875cf2f7a2297da02b8f4ba8e0";
    int i;
    sm3((const uint8_t *)"abc", 3, out);
    for (i = 0; i < 32; i++) {
        unsigned v;
        sscanf(hex + 2 * i, "%2x", &v);
        exp[i] = (uint8_t)v;
    }
    check(memcmp(out, exp, 32) == 0, "sm3 KAT 'abc'");
}

static void kat_sm4(void)
{
    uint8_t key[16], pt[16], ct[16];
    static const char *hex = "681edf34d206965e86b3e94f536e4246";
    uint8_t exp[16];
    int i;
    for (i = 0; i < 16; i++) {
        unsigned v;
        sscanf("0123456789abcdeffedcba9876543210" + 2 * i, "%2x", &v);
        key[i] = pt[i] = (uint8_t)v;
        sscanf(hex + 2 * i, "%2x", &v);
        exp[i] = (uint8_t)v;
    }
    {
        sm4_ctx c;
        sm4_setkey_enc(&c, key);
        sm4_encrypt_block(&c, pt, ct);
        check(memcmp(ct, exp, 16) == 0, "sm4 KAT 标准向量");
    }
}

static void bench(void)
{
    uint8_t buf[256], out[32];
    uint64_t c0, c1;
    int i;
    memset(buf, 0x5A, sizeof buf);

    c0 = smc_port_cycles();
    for (i = 0; i < 100; i++) sm3(buf, 256, out);
    c1 = smc_port_cycles();
    printf("sm3(256B)  : %llu cycles/op\n", (unsigned long long)((c1 - c0) / 100));

    {
        uint8_t key[16];
        sm4_ctx c;
        memset(key, 0x11, 16);
        sm4_setkey_enc(&c, key);
        c0 = smc_port_cycles();
        for (i = 0; i < 1000; i++) sm4_encrypt_block(&c, buf, out);
        c1 = smc_port_cycles();
        printf("sm4 block  : %llu cycles/op\n", (unsigned long long)((c1 - c0) / 1000));
    }

    {
        uint8_t d[32], pub[65], sig[64], dgst[32];
        smc_rng rng = { 0 };
        /* 板上接 TRNG：
           static int ent(void *u, uint8_t *b, size_t n){ (void)u; return smc_port_rng_bytes(b,n); }
           rng.entropy = ent; */
        memset(dgst, 0x33, 32);
        if (sm2_keygen(d, pub, &rng) == 0) {
            c0 = smc_port_cycles();
            sm2_sign(sig, dgst, d, NULL);
            c1 = smc_port_cycles();
            printf("sm2 sign   : %llu cycles\n", (unsigned long long)(c1 - c0));
            c0 = smc_port_cycles();
            check(sm2_verify(sig, dgst, pub) == 0, "sm2 verify (bench)");
            c1 = smc_port_cycles();
            printf("sm2 verify : %llu cycles\n", (unsigned long long)(c1 - c0));
        }
    }
}

int main(void)
{
    smc_port_init();
    printf("== SMC Cortex-M4 self-test & benchmark ==\n");
    kat_sm3();
    kat_sm4();
    bench();
    printf("== done, failures=%d ==\n", g_ok);
    for (;;) { smc_port_watchdog_feed(); }
    return 0;
}
