/*
 * board_selftest.c — 板上自检 + 微基准（板级代码与宿主仿真共用同一份实现）
 *
 * 设计要点（都是为了让"上板那次"只花在测量上，而不是调代码）：
 *  ① **整数输出、不用浮点**：printf 的浮点格式化会拉进大量 libc，与 <32KB Flash 目标冲突；
 *  ② **不用 sscanf**：自带的十六进制比较器替代，避免拉进整个 scanf 族；
 *  ③ 每项测量前后各打一行，并额外输出**机器可读的 RESULT 行**，便于抄写与脚本比对；
 *  ④ KAT 用 OpenSSL 交叉验证过的期望值（与 tests/test_all.c 同源），板上一跑就能确认
 *     "算法库在真机上算对了"；
 *  ⑤ 与 scripts/build.sh 的 host 基准同口径（同样操作、同样轮数），便于两边对照。
 *
 * 输出经 printf → smc_port_putc（目标机）或 stdout（宿主仿真）。
 */
#include <stdio.h>
#include <string.h>
#include "smc_port.h"
#include "smc/sm3.h"
#include "smc/sm4.h"
#include "smc/sm2.h"
#include "smc/hmac_sm3.h"
#include "smc/smc_rng.h"

/* 测量轮数：与 host 基准一致（scripts/benchmark.sh 的 BENCH_ROUNDS=5），各轮取最小 */
#define ROUNDS 5

static int g_fail;

static void check(int cond, const char *name)
{
    if (!cond) g_fail++;
    printf("  [%s] %s\n", cond ? " ok " : "FAIL", name);
}

/* 定长十六进制比较（替代 sscanf：不拉入 scanf 族） */
static int hex_eq(const uint8_t *buf, const char *hex, size_t n)
{
    static const char *d = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        if (d[buf[i] >> 4] != hex[2 * i] || d[buf[i] & 0xFu] != hex[2 * i + 1]) return 0;
    }
    return 1;
}

/* 测量宏：ROUNDS 轮取最小（过滤中断/调度噪声，与 host 基准同法） */
#define MEASURE(stmt, iters)                                                  \
    do {                                                                      \
        uint32_t _r;                                                          \
        uint64_t _best = ~(uint64_t)0, _c0, _c1;                              \
        for (_r = 0; _r < ROUNDS; _r++) {                                     \
            uint32_t _i;                                                      \
            _c0 = smc_port_cycles();                                          \
            for (_i = 0; _i < (iters); _i++) { stmt; }                        \
            _c1 = smc_port_cycles();                                          \
            if ((_c1 - _c0) < _best) _best = (_c1 - _c0);                     \
        }                                                                     \
        _best /= (iters);                                                      \
        g_cyc = _best;                                                        \
    } while (0)

static uint64_t g_cyc;

static void print_u64_dec(uint64_t v)
{
    unsigned long hi = (unsigned long)(v / 1000000000UL);
    unsigned long lo = (unsigned long)(v % 1000000000UL);
    if (hi) printf("%lu%09lu", hi, lo);
    else    printf("%lu", lo);
}

static void report(const char *tag, const char *op, uint64_t cyc)
{
    printf("| %-22s | ", op);
    print_u64_dec(cyc);
    printf(" |\nRESULT %s.%s=", tag, op);
    print_u64_dec(cyc);
    printf("\n");
}

/* 熵源：TRNG 直接供给（docs/03 §4 要求健康测试通过后使用） */
static int entropy_cb(void *user, uint8_t *buf, size_t len)
{
    (void)user;
    return smc_port_rng_bytes(buf, len);
}

static void kat(void)
{
    printf("\n== 1/3 KAT 自检（与 OpenSSL 交叉验证过的期望值）==\n");

    {   /* SM3("abc") */
        uint8_t d[32];
        sm3((const uint8_t *)"abc", 3, d);
        check(hex_eq(d, "66c7f0f462eeedd9d1f2d46bdc10e4e2"
                        "4167c4875cf2f7a2297da02b8f4ba8e0", 32), "SM3(\"abc\")");
    }
    {   /* SM4 标准向量：key=pt=0123456789abcdeffedcba9876543210 */
        static const uint8_t k[16] = {0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
                                      0xfe,0xdc,0xba,0x98,0x76,0x54,0x32,0x10};
        uint8_t out[16];
        sm4_ctx c;
        sm4_setkey_enc(&c, k);
        sm4_encrypt_block(&c, k, out);
        check(hex_eq(out, "681edf34d206965e86b3e94f536e4246", 16), "SM4 标准向量");
    }
    {   /* HMAC-SM3 自洽（密钥/消息相同则输出相同，且与 host 基准同向量） */
        uint8_t m1[32], m2[32], key[32], msg[32];
        memset(key, 0x0b, sizeof key);
        memset(msg, 0x5a, sizeof msg);
        hmac_sm3(key, 32, msg, 32, m1);
        hmac_sm3(key, 32, msg, 32, m2);
        check(memcmp(m1, m2, 32) == 0, "HMAC-SM3 可复现");
        check(memcmp(m1, msg, 32) != 0, "HMAC-SM3 非常量输出");
    }
    {   /* SM2：签名/验签往返 + ECDH 一致性（板级功能连通性） */
        static uint8_t d1[32], d2[32], p1[65], p2[65], sig[64], dgst[32];
        static uint8_t s1[32], s2[32];
        smc_rng rng;
        int i;
        rng.entropy = entropy_cb;
        rng.user = 0;
        for (i = 0; i < 32; i++) dgst[i] = (uint8_t)(0x30 + i);
        check(sm2_keygen(d1, p1, &rng) == 0, "TRNG 驱动的 SM2 密钥生成");
        check(sm2_keygen(d2, p2, &rng) == 0, "第二把密钥生成");
        check(memcmp(p1, p2, 65) != 0, "两次生成公钥不同（TRNG 生效）");
        check(sm2_sign(sig, dgst, d1, NULL) == 0, "SM2 签名");
        check(sm2_verify(sig, dgst, p1) == 0, "SM2 验签通过");
        sig[7] ^= 0x01;
        check(sm2_verify(sig, dgst, p1) != 0, "篡改签名被拒绝");
        sig[7] ^= 0x01;
        check(sm2_ecdh(s1, d1, p2) == 0 && sm2_ecdh(s2, d2, p1) == 0 &&
              memcmp(s1, s2, 32) == 0, "ECDH 双方共享一致");
    }
}

static void bench(void)
{
    static uint8_t buf[4096], out[4096];

    printf("\n== 2/3 微基准（每项 %d 轮取最小；cycles 由 DWT 计数）==\n", ROUNDS);
    printf("| %-22s | %10s |\n", "operation", "cycles/op");
    printf("|------------------------|------------|\n");

    memset(buf, 0x5a, sizeof buf);

    MEASURE(sm3(buf, 1024, out), 20);
    report("bench", "sm3_1KB", g_cyc);

    MEASURE(sm3(buf, 4096, out), 8);
    report("bench", "sm3_4KB", g_cyc);

    {
        uint8_t key[32], mac[32];
        memset(key, 0x0b, 32);
        MEASURE(hmac_sm3(key, 32, buf, 256, mac), 20);
        report("bench", "hmac_sm3_256B", g_cyc);
    }
    {
        uint8_t key[16], iv[16];
        sm4_ctx c;
        memset(key, 0x11, 16);
        memset(iv, 0x22, 16);
        sm4_setkey_enc(&c, key);

        MEASURE(sm4_encrypt_block(&c, buf, out), 200);
        report("bench", "sm4_ecb_block", g_cyc);

        MEASURE(sm4_cbc_encrypt(&c, iv, buf, out, 1024), 20);
        report("bench", "sm4_cbc_1KB", g_cyc);

        MEASURE(sm4_ctr_xor(&c, iv, buf, out, 1024), 20);
        report("bench", "sm4_ctr_1KB", g_cyc);
    }
    {   /* SM2：与 host 基准同口径（keygen 含 TRNG、sign 含 nonce 派生） */
        static uint8_t d[32], pub[65], sig[64], dgst[32], sh[32];
        smc_rng rng;
        rng.entropy = entropy_cb;
        rng.user = 0;
        memset(dgst, 0x33, 32);
        if (sm2_keygen(d, pub, &rng) == 0) {
            MEASURE(sm2_keygen(d, pub, &rng), 4);
            report("bench", "sm2_keygen", g_cyc);

            MEASURE(sm2_sign(sig, dgst, d, NULL), 4);
            report("bench", "sm2_sign", g_cyc);

            (void)sm2_sign(sig, dgst, d, NULL);      /* 使 sig 与 dgst 匹配 */
            MEASURE(sm2_verify(sig, dgst, pub), 4);
            report("bench", "sm2_verify", g_cyc);

            MEASURE(sm2_ecdh(sh, d, pub), 4);
            report("bench", "sm2_ecdh", g_cyc);
        } else {
            printf("（SM2 基准跳过：密钥生成失败）\n");
            g_fail++;
        }
    }
}

static void trng_report(void)
{
    int pct = -1, rc;
    printf("\n== 3/3 硬件熵源健康自检 ==\n");
    rc = smc_port_rng_selftest(&pct);
    printf("  RNG 自检返回码 = %d，比特 1 占比 = %d%%\n", rc, pct);
    check(rc == 0, "TRNG 健康自检（无错误/无重复/比特均衡）");
}

void board_selftest(void)
{
    printf("\n==== SMC Cortex-M4 板上自检与基准 ====\n");
    printf("编译期内核：SM3=%d SM4=%d SM2=%d CT=%d（1=REF 2=OPT 3=TINY/SMALL）\n",
           SMC_SM3_KERNEL, SMC_SM4_KERNEL, SMC_SM2_KERNEL, SMC_SM2_CT);
    printf("编译期 SMC_CPU_HZ = %u Hz（复位后默认 HSI 16MHz；未配 PLL 时请勿改成 168M）\n",
           (unsigned)smc_port_cpu_hz());
    printf("cycles 由 DWT 计数，与主频无关；主频只影响 µs 换算与功耗折算。\n");

    kat();
    bench();
    trng_report();

    printf("\n==== 完成：失败项 = %d ====\n", g_fail);
    printf("RESULT summary.fail=%d\n", g_fail);
    printf("\n把上面所有 RESULT 行抄进 docs/08 的记录表即可（或按指南用串口日志直接保存）。\n");

#if defined(SMC_BENCH_LOOP)
    /* 连续基准循环：**功耗测量用**。
       一次 SM2 只有几十毫秒，电流表/示波器难以稳定读数；本模式让 MCU 持续做同一种运算，
       形成接近 100% 的占空比，便于用"差值法"测平均电流与单次能量（见 docs/08 §6）。
       由 build.sh 的第三参数选择：loop(签名) | loop_verify | loop_ecdh | loop_sm4 */
    {
        static uint8_t d[32], pub[65], sig[64], dgst[32], sh[32], kbuf[1024];
        (void)d; (void)pub; (void)sig; (void)dgst; (void)sh; (void)kbuf;
        (void)d; (void)sig; (void)dgst; (void)sh; (void)kbuf;
        smc_rng rng;
        rng.entropy = entropy_cb;
        rng.user = 0;
        memset(dgst, 0x33, 32);
        memset(kbuf, 0x5a, sizeof kbuf);
        printf("\n[loop] 进入连续基准循环（供功耗测量）；按复位键退出\n");
        if (sm2_keygen(d, pub, &rng) != 0) {
            printf("\n[loop] 密钥生成失败, 退出\n");
            return;
        }
        for (;;) {
#if defined(SMC_BENCH_LOOP_SM4)
            { sm4_ctx c; uint8_t key[16], iv[16];
              memset(key, 0x11, 16); memset(iv, 0x22, 16);
              sm4_setkey_enc(&c, key);
              sm4_ctr_xor(&c, iv, kbuf, kbuf, 1024); }
#elif defined(SMC_BENCH_LOOP_VERIFY)
            (void)sm2_sign(sig, dgst, d, NULL);
            (void)sm2_verify(sig, dgst, pub);
#elif defined(SMC_BENCH_LOOP_ECDH)
            (void)sm2_ecdh(sh, d, pub);
#else
            (void)sm2_sign(sig, dgst, d, NULL);
#endif
            smc_port_watchdog_feed();
        }
    }
#endif
}
