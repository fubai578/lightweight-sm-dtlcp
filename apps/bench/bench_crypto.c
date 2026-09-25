/*
 * bench_crypto.c — 算法微基准（REF / OPT / TINY 内核对比）
 * 构建后分别链接各内核对象：bin/bench_{ref,opt,tiny}
 *
 * 方法学：每项测量跑 BENCH_ROUNDS 轮，取"最小的那一轮"作为结果
 * —— 最小值过滤系统调度与频率漂移噪声，是同机对比的稳健口径。
 * 输出 markdown 表：cycles/op、µs/op、ops/s；功耗由 scripts/power_model.py 换算。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "smc/sm3.h"
#include "smc/sm4.h"
#include "smc/sm2.h"
#include "smc/hmac_sm3.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
static uint64_t rdtsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static uint64_t rdtsc(void) { return 0; }
#endif

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static uint32_t lcg = 0x2468ACE1u;
static void prand(uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        lcg = lcg * 1103515245u + 12345u;
        b[i] = (uint8_t)(lcg >> 16);
    }
}

/* 稳健测量：BENCH_ROUNDS 轮取最小值（过滤调度/频率噪声） */
#define BENCH_ROUNDS 5
#define MEASURE(stmt, iters, out_cyc, out_ns)                          \
    do {                                                               \
        int _r;                                                        \
        uint64_t _bc = ~(uint64_t)0, _bn = ~(uint64_t)0;               \
        for (_r = 0; _r < BENCH_ROUNDS; _r++) {                        \
            uint64_t _c0, _c1, _n0, _n1;                               \
            _c0 = rdtsc(); _n0 = now_ns();                             \
            { int _i; for (_i = 0; _i < (iters); _i++) { stmt; } }     \
            _c1 = rdtsc(); _n1 = now_ns();                             \
            if (_c1 - _c0 < _bc) _bc = _c1 - _c0;                      \
            if (_n1 - _n0 < _bn) _bn = _n1 - _n0;                      \
        }                                                              \
        (out_cyc) = _bc; (out_ns) = _bn;                               \
    } while (0)

static void row(const char *op, int iters, uint64_t cyc, uint64_t ns)
{
    double cy = (double)cyc / iters;
    double us = (double)ns / iters / 1000.0;
    printf("| %s | %d | %.0f | %.2f | %.0f |\n", op, iters, cy, us,
           us > 0 ? 1000000.0 / us : 0.0);
}

int main(void)
{
    uint64_t c0, n0;
    static uint8_t buf[4096], out[4096];

    setvbuf(stdout, NULL, _IONBF, 0);
    prand(buf, sizeof buf);

#if SMC_SM3_KERNEL == SMC_KERNEL_REF
    printf("### 内核变体：REF（基线：教科书实现）\n");
#elif SMC_SM3_KERNEL == SMC_KERNEL_TINY
    printf("### 内核变体：TINY/SMALL（最小内存）\n");
#else
    printf("### 内核变体：OPT（指令级优化）\n");
#endif
    printf("> 每项 %d 轮取最小值；同一 host 连续测量，横向对比以同一次输出为准\n", BENCH_ROUNDS);

    printf("\n#### SM3 哈希\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        int iters = 2000;
        MEASURE(sm3(buf, 1024, out), iters, c0, n0);
        row("hash 1KB", iters, c0, n0);
        MEASURE(sm3(buf, 4096, out), iters, c0, n0);
        row("hash 4KB", iters, c0, n0);
    }

    printf("\n#### SM4 分组密码\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[16], pt[16], ct[16], iv[16], ctr[16], ctr2[16], cbuf[1024];
        sm4_ctx e4, d4;
        int iters = 20000;

        prand(key, 16);
        sm4_setkey_enc(&e4, key);
        sm4_setkey_dec(&d4, key);
        memcpy(pt, buf, 16);
        MEASURE(sm4_encrypt_block(&e4, pt, ct), iters, c0, n0);
        row("ECB encrypt block", iters, c0, n0);
        MEASURE(sm4_decrypt_block(&d4, ct, pt), iters, c0, n0);
        row("ECB decrypt block", iters, c0, n0);
#if SMC_SM4_MODE_CBC
        prand(iv, 16);
        MEASURE((memcpy(iv, buf, 16), sm4_cbc_encrypt(&e4, iv, buf, cbuf, 1024)),
                2000, c0, n0);
        row("CBC encrypt 1KB", 2000, c0, n0);
#endif
#if SMC_SM4_MODE_CTR
        prand(ctr, 16);
        MEASURE((memcpy(ctr2, ctr, 16), sm4_ctr_xor(&e4, ctr2, buf, cbuf, 1024)),
                2000, c0, n0);
        row("CTR xor 1KB", 2000, c0, n0);
#endif
    }

    printf("\n#### HMAC-SM3\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[32], mac[32];
        int iters = 4000;
        prand(key, 32);
        MEASURE(hmac_sm3(key, 32, buf, 256, mac), iters, c0, n0);
        row("HMAC-SM3 (256B)", iters, c0, n0);
    }

#if SMC_SM2_SIGN || SMC_SM2_ECDH
    printf("\n#### SM2 椭圆曲线\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t d1[32], p1[65], d2[32], p2[65], sig[64], dgst[32], shared[32];
        uint8_t kd[32], kp[65];
        int iters_slow = 32;

        prand(d1, 32); prand(d2, 32); prand(dgst, 32);
        sm2_pub_from_priv(p1, d1);
        sm2_pub_from_priv(p2, d2);

        MEASURE(sm2_keygen(kd, kp, NULL), iters_slow, c0, n0);
        row("keygen", iters_slow, c0, n0);

        MEASURE(sm2_sign(sig, dgst, d1, NULL), iters_slow, c0, n0);
        row("sign", iters_slow, c0, n0);

        (void)sm2_sign(sig, dgst, d1, NULL);   /* 确定性 nonce：sig 与 dgst 稳定匹配 */
        MEASURE(sm2_verify(sig, dgst, p1), iters_slow, c0, n0);
        row("verify", iters_slow, c0, n0);

        MEASURE(sm2_ecdh(shared, d1, p2), iters_slow, c0, n0);
        row("ecdh", iters_slow, c0, n0);
    }
#endif
    return 0;
}
