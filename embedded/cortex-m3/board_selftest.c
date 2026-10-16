/*
 * board_selftest.c — 板上自检 + 微基准（板级代码与宿主仿真共用同一份实现）
 *
 * ★ 本副本由 embedded/cortex-m4/board_selftest.c 复制而来，仅改动**熵源相关措辞**：
 *   STM32F103C8T6 **没有硬件 RNG**，熵源是 ADC 噪声 + 白化（见 ../port/smc_port_f1.c），
 *   因此自检项不能叫"TRNG 健康自检"——那是虚假陈述。改为"熵源自检"并注明熵质量边界。
 *   其余（测量逻辑、RESULT 行格式、KAT 期望值）与 M4 版完全一致，保证两边数据可直接对照。
 *
 * 设计要点（都是为了让"上板那次"只花在测量上，而不是调代码）：
 *  ① **整数输出、不用浮点**：printf 的浮点格式化会拉进大量 libc，与 <32KB Flash 目标冲突；
 *     64KB 器件上也必须用 newlib-**nano**（-specs=nano.specs），full 版 printf 单独就要十几 KB。
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

/* ------------------------------------------------------------------ *
 * 精简输出模式（SMC_BOARD_TINY_PRINT）
 *
 * 为什么需要：UTF-8 里一个汉字 3 字节，banner 与说明文字里的中文会**实打实占用 Flash**
 * （实测全套中文 banner 约 1.5 KB）。做 32KB 适配证明时（docs/05 §3.2）这 1.5KB
 * 决定成败——lite 配置原本只差 664 B。
 *   做法：只在需要压缩时（SMC_BOARD_TINY_PRINT=1）换成等价的短英文串，
 *        打印的信息**完全相同**（同样的 RESULT 行、同样的失败项统计），
 *        只省掉解释性文字。证据性内容一律不删。
 * ------------------------------------------------------------------ */
#if defined(SMC_BOARD_TINY_PRINT)
#define P_BANNER      "\n==== SMC M3 (F103C8T6) board selftest ====\n"
#define P_KERNELS     "kernels: SM3=%d SM4=%d SM2=%d CT=%d (1=REF 2=OPT 3=TINY)\n"
#define P_CPUHZ       "SMC_CPU_HZ=%u Hz (HSI 8MHz default; DWT cycles are freq-independent)\n"
#define P_ENTROPY     "entropy: no RNG on this chip; ADC1_IN0(PA0) noise + LFSR whitening\n"
#define P_KAT_HDR     "\n== 1/3 KAT selftest ==\n"
#define P_BENCH_HDR   "\n== 2/3 micro-benchmark (min of %d rounds, DWT cycles) ==\n"
#define P_ENT_HDR     "\n== 3/3 entropy self-test (ADC noise; NOT a TRNG) ==\n"
#define P_ENT_RC      "  entropy rc=%d, bit1=%d%%\n"
#define P_ENT_NOTE    "  NOTE: only proves ADC produces varying noise; NOT TRNG-grade.\n"
#define P_DONE        "\n==== done: failures=%d ====\n"
#define P_SKIP_CBC    "(SM4-CBC trimmed)\n"
#define P_SKIP_CTR    "(SM4-CTR trimmed)\n"
#define P_SKIP_KEYGEN "(keygen failed)\n"
#define P_LOOP_HDR    "\n[loop] continuous benchmark loop (for power measurement)\n"
#else
#define P_BANNER      "\n==== SMC Cortex-M3 (STM32F103C8T6) 板上自检与基准 ====\n"
#define P_KERNELS     "编译期内核：SM3=%d SM4=%d SM2=%d CT=%d（1=REF 2=OPT 3=TINY/SMALL）\n"
#define P_CPUHZ       "编译期 SMC_CPU_HZ = %u Hz（复位后默认 HSI 8MHz；未配 PLL 时请勿改成 72M）\n"
#define P_ENTROPY     "熵源说明：本芯片无硬件 RNG，用 ADC1_IN0(PA0) 噪声 + LFSR 白化；**生产环境必须外接 TRNG**。\n"
#define P_KAT_HDR     "\n== 1/3 KAT 自检（与 OpenSSL 交叉验证过的期望值）==\n"
#define P_BENCH_HDR   "\n== 2/3 微基准（每项 %d 轮取最小；cycles 由 DWT 计数）==\n"
#define P_ENT_HDR     "\n== 3/3 熵源健康自检（ADC 噪声 + 白化；非 TRNG，生产须外接 TRNG）==\n"
#define P_ENT_RC      "  熵源自检返回码 = %d，比特 1 占比 = %d%%\n"
#define P_ENT_NOTE    "  说明：仅表示 ADC 确实在产生变化的噪声且无异常读数，**不代表**熵质量达到 TRNG 等级。\n"
#define P_DONE        "\n==== 完成：失败项 = %d ====\n"
#define P_SKIP_CBC    "（SM4-CBC 已裁剪，跳过 sm4_cbc_1KB 基准）\n"
#define P_SKIP_CTR    "（SM4-CTR 已裁剪，跳过 sm4_ctr_1KB 基准）\n"
#define P_SKIP_KEYGEN "（SM2 基准跳过：密钥生成失败）\n"
#define P_LOOP_HDR    "\n[loop] 进入连续基准循环（供功耗测量）；按复位键退出\n"
#endif

/* 测量轮数：与 host 基准一致（scripts/benchmark.sh 的 BENCH_ROUNDS=5），各轮取最小 */
#define ROUNDS 5

static int g_fail;

/* SWD 可读的板测结果块。串口适合现场展示，但长日志可能受 USB-UART 缓冲影响；
 * 结果块让自动化脚本按 ELF 符号读取同一份实测值，并用 state 判断是否完整结束。 */
#define SMC_BOARD_RESULTS_MAGIC   0x534D4331u /* "SMC1" */
#define SMC_BOARD_RESULTS_VERSION 1u
enum {
    BOARD_RESULT_EMPTY = 0,
    BOARD_RESULT_RUNNING = 1,
    BOARD_RESULT_DONE = 2
};
enum {
    BENCH_SM3_1KB = 0,
    BENCH_SM3_4KB,
    BENCH_HMAC_SM3_256B,
    BENCH_SM4_ECB_BLOCK,
    BENCH_SM4_CBC_1KB,
    BENCH_SM4_CTR_1KB,
    BENCH_SM2_KEYGEN,
    BENCH_SM2_SIGN,
    BENCH_SM2_VERIFY,
    BENCH_SM2_ECDH,
    BENCH_COUNT
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t state;
    uint32_t sm3_kernel;
    uint32_t sm4_kernel;
    uint32_t sm2_kernel;
    uint32_t sm2_ct;
    volatile uint32_t failures;
    volatile int32_t entropy_rc;
    volatile int32_t entropy_ones_pct;
    volatile uint64_t cycles[BENCH_COUNT];
} smc_board_results_t;

volatile smc_board_results_t smc_board_results;

/* ------------------------------------------------------------------ *
 * check() 的文案在精简模式下压缩。
 *
 * 为什么连自检项的名字也要压：32K 链接最后只差 208 B，而这些中文标签
 * （UTF-8 每字3 字节）加起来约 600+ B。**但绝不能删自检项**——它们本身就是
 * "板上验证了什么"的证据。这里用**英文短标签 + 相同判定逻辑**替代：
 *   - 判断条件（cond）完全不变，pass/fail 结果完全一致；
 *   - 每项的语义一一对应，docs/08 §5 的记录表可照常填写；
 *   - 完整中文版用 `flash` 模式产出（64KB 板日常调试用）。
 * ------------------------------------------------------------------ */
#if defined(SMC_BOARD_TINY_PRINT)
#define CHK(c, en) check((c), (en))
#else
#define CHK(c, cn) check((c), (cn))
#endif

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

static void save_cycle(unsigned index)
{
    if (index < BENCH_COUNT) smc_board_results.cycles[index] = g_cyc;
}

/* 64 位十进制打印。
 * ★ 为什么不用 "%llu"：newlib-**nano** 的 printf 只支持 "%l"（32 位 long），
 *   一旦出现 "%llu" 就会把整个格式化器**回退到完整版**（含 dtoa + 软浮点例程），
 *   实测在 Cortex-M3 上凭空多出约 18 KB Flash —— 直接导致 32KB 链接失败
 *   （region FLASH overflowed by 18804 bytes）。
 *   而 Cortex-M3 的 long 就是 32 位，故手工拆成两半打印：高 16 位 + 低 16 位。
 *   同理 "%02x" 之类 nano 都支持，无需改动。 */
static void print_u64_dec(uint64_t v)
{
    unsigned long hi = (unsigned long)(v / 1000000000UL);
    unsigned long lo = (unsigned long)(v % 1000000000UL);
    if (hi) printf("%lu%09lu", hi, lo);       /* ≥ 10^9 时分两段，避免溢出 */
    else    printf("%lu", lo);
}

static void report(const char *tag, const char *op, uint64_t cyc)
{
    printf("| %-22s | ", op);
    print_u64_dec(cyc);
    printf(" |\n");
    printf("RESULT %s.%s=", tag, op);
    print_u64_dec(cyc);
    printf("\n");
}

/* 熵源：板级熵源直接供给。F103 无 TRNG，此处是 ADC 噪声 + 白化（见 smc_port_f1.c 的边界说明） */
static int entropy_cb(void *user, uint8_t *buf, size_t len)
{
    (void)user;
    return smc_port_rng_bytes(buf, len);
}

static void kat(void)
{
    printf(P_KAT_HDR);

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
        #if defined(SMC_BOARD_TINY_PRINT)
        check(hex_eq(out, "681edf34d206965e86b3e94f536e4246", 16), "SM4 std vector");
#else
        check(hex_eq(out, "681edf34d206965e86b3e94f536e4246", 16), "SM4 标准向量");
#endif
    }
    {   /* HMAC-SM3 自洽（密钥/消息相同则输出相同，且与 host 基准同向量） */
        uint8_t m1[32], m2[32], key[32], msg[32];
        memset(key, 0x0b, sizeof key);
        memset(msg, 0x5a, sizeof msg);
        hmac_sm3(key, 32, msg, 32, m1);
        hmac_sm3(key, 32, msg, 32, m2);
        #if defined(SMC_BOARD_TINY_PRINT)
        check(memcmp(m1, m2, 32) == 0, "HMAC-SM3 reproducible");
#else
        check(memcmp(m1, m2, 32) == 0, "HMAC-SM3 可复现");
#endif
        #if defined(SMC_BOARD_TINY_PRINT)
        check(memcmp(m1, msg, 32) != 0, "HMAC-SM3 not constant");
#else
        check(memcmp(m1, msg, 32) != 0, "HMAC-SM3 非常量输出");
#endif
    }
    {   /* SM2：签名/验签往返 + ECDH 一致性（板级功能连通性） */
        static uint8_t d1[32], d2[32], p1[65], p2[65], sig[64], dgst[32];
        static uint8_t s1[32], s2[32];
        smc_rng rng;
        int i;
        rng.entropy = entropy_cb;
        rng.user = 0;
        for (i = 0; i < 32; i++) dgst[i] = (uint8_t)(0x30 + i);
        #if defined(SMC_BOARD_TINY_PRINT)
        check(sm2_keygen(d1, p1, &rng) == 0, "SM2 keygen (entropy)");
#else
        check(sm2_keygen(d1, p1, &rng) == 0, "熵源驱动的 SM2 密钥生成");
#endif
        #if defined(SMC_BOARD_TINY_PRINT)
        check(sm2_keygen(d2, p2, &rng) == 0, "2nd keygen");
#else
        check(sm2_keygen(d2, p2, &rng) == 0, "第二把密钥生成");
#endif
        #if defined(SMC_BOARD_TINY_PRINT)
        check(memcmp(p1, p2, 65) != 0, "pubkeys differ (entropy ok)");
#else
        check(memcmp(p1, p2, 65) != 0, "两次生成公钥不同（熵源生效）");
#endif
        #if defined(SMC_BOARD_TINY_PRINT)
        check(sm2_sign(sig, dgst, d1, NULL) == 0, "SM2 sign");
#else
        check(sm2_sign(sig, dgst, d1, NULL) == 0, "SM2 签名");
#endif
        #if defined(SMC_BOARD_TINY_PRINT)
        check(sm2_verify(sig, dgst, p1) == 0, "SM2 verify ok");
#else
        check(sm2_verify(sig, dgst, p1) == 0, "SM2 验签通过");
#endif
        sig[7] ^= 0x01;
        #if defined(SMC_BOARD_TINY_PRINT)
        check(sm2_verify(sig, dgst, p1) != 0, "tampered sig rejected");
#else
        check(sm2_verify(sig, dgst, p1) != 0, "篡改签名被拒绝");
#endif
        sig[7] ^= 0x01;
        check(sm2_ecdh(s1, d1, p2) == 0 && sm2_ecdh(s2, d2, p1) == 0 &&
              memcmp(s1, s2, 32) == 0, "ECDH 双方共享一致");
    }
}

static void bench(void)
{
    static uint8_t buf[4096], out[4096];

    printf(P_BENCH_HDR, ROUNDS);
    printf("| %-22s | %10s |\n", "operation", "cycles/op");
    printf("|------------------------|------------|\n");

    memset(buf, 0x5a, sizeof buf);

    MEASURE(sm3(buf, 1024, out), 20);
    save_cycle(BENCH_SM3_1KB);
    report("bench", "sm3_1KB", g_cyc);

    MEASURE(sm3(buf, 4096, out), 8);
    save_cycle(BENCH_SM3_4KB);
    report("bench", "sm3_4KB", g_cyc);

    {
        uint8_t key[32], mac[32];
        memset(key, 0x0b, 32);
        MEASURE(hmac_sm3(key, 32, buf, 256, mac), 20);
        save_cycle(BENCH_HMAC_SM3_256B);
        report("bench", "hmac_sm3_256B", g_cyc);
    }
    {
        uint8_t key[16], iv[16];
        sm4_ctx c;
        memset(key, 0x11, 16);
        memset(iv, 0x22, 16);
        sm4_setkey_enc(&c, key);

        MEASURE(sm4_encrypt_block(&c, buf, out), 200);
        save_cycle(BENCH_SM4_ECB_BLOCK);
        report("bench", "sm4_ecb_block", g_cyc);

#if SMC_SM4_MODE_CBC
        MEASURE(sm4_cbc_encrypt(&c, iv, buf, out, 1024), 20);
        save_cycle(BENCH_SM4_CBC_1KB);
        report("bench", "sm4_cbc_1KB", g_cyc);
#else
        printf(P_SKIP_CBC);
#endif
#if SMC_SM4_MODE_CTR
        MEASURE(sm4_ctr_xor(&c, iv, buf, out, 1024), 20);
        save_cycle(BENCH_SM4_CTR_1KB);
        report("bench", "sm4_ctr_1KB", g_cyc);
#else
        printf(P_SKIP_CTR);
#endif
    }
    {   /* SM2：与 host 基准同口径（keygen 含 TRNG、sign 含 nonce 派生） */
        static uint8_t d[32], pub[65], sig[64], dgst[32], sh[32];
        smc_rng rng;
        rng.entropy = entropy_cb;
        rng.user = 0;
        memset(dgst, 0x33, 32);
        if (sm2_keygen(d, pub, &rng) == 0) {
            MEASURE(sm2_keygen(d, pub, &rng), 4);
            save_cycle(BENCH_SM2_KEYGEN);
            report("bench", "sm2_keygen", g_cyc);

            MEASURE(sm2_sign(sig, dgst, d, NULL), 4);
            save_cycle(BENCH_SM2_SIGN);
            report("bench", "sm2_sign", g_cyc);

            (void)sm2_sign(sig, dgst, d, NULL);      /* 使 sig 与 dgst 匹配 */
            MEASURE(sm2_verify(sig, dgst, pub), 4);
            save_cycle(BENCH_SM2_VERIFY);
            report("bench", "sm2_verify", g_cyc);

            MEASURE(sm2_ecdh(sh, d, pub), 4);
            save_cycle(BENCH_SM2_ECDH);
            report("bench", "sm2_ecdh", g_cyc);
        } else {
            printf(P_SKIP_KEYGEN);
            g_fail++;
        }
    }
}

static void trng_report(void)
{
    int pct = -1, rc;
    /* ★ 如实措辞：F103 无硬件 RNG，这里是 ADC 噪声熵源的自检，
       通过**不等于**熵源达到 TRNG 等级（生产须外接 TRNG，见 docs/03 §2.4/§4.3）。 */
    printf(P_ENT_HDR);
    rc = smc_port_rng_selftest(&pct);
    smc_board_results.entropy_rc = rc;
    smc_board_results.entropy_ones_pct = pct;
    printf(P_ENT_RC, rc, pct);
    if (rc == 0) printf(P_ENT_NOTE);
    #if defined(SMC_BOARD_TINY_PRINT)
    check(rc == 0, "entropy selftest ok");
#else
    check(rc == 0, "熵源健康自检（无错误/无重复/比特均衡/窗口内唯一）");
#endif
}

void board_selftest(void)
{
    memset((void *)&smc_board_results, 0, sizeof smc_board_results);
    smc_board_results.magic = SMC_BOARD_RESULTS_MAGIC;
    smc_board_results.version = SMC_BOARD_RESULTS_VERSION;
    smc_board_results.sm3_kernel = SMC_SM3_KERNEL;
    smc_board_results.sm4_kernel = SMC_SM4_KERNEL;
    smc_board_results.sm2_kernel = SMC_SM2_KERNEL;
    smc_board_results.sm2_ct = SMC_SM2_CT;
    smc_board_results.entropy_rc = -999;
    smc_board_results.entropy_ones_pct = -1;
    smc_board_results.state = BOARD_RESULT_RUNNING;

    printf(P_BANNER);
    printf(P_KERNELS, SMC_SM3_KERNEL, SMC_SM4_KERNEL, SMC_SM2_KERNEL, SMC_SM2_CT);
    printf(P_CPUHZ, (unsigned)smc_port_cpu_hz());
#if !defined(SMC_BOARD_TINY_PRINT)
    printf("cycles 由 DWT 计数，与主频无关；主频只影响 µs 换算与功耗折算。\n");
#endif
    printf(P_ENTROPY);

    kat();
    bench();
    trng_report();

    smc_board_results.failures = (uint32_t)g_fail;
    smc_board_results.state = BOARD_RESULT_DONE;
    printf(P_DONE, g_fail);
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
        printf(P_LOOP_HDR);
        if (sm2_keygen(d, pub, &rng) != 0) {
            printf("keygen failed, exit loop\n");
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
