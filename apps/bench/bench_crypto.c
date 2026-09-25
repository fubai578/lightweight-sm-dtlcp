/*
 * bench_crypto.c — 算法微基准（REF / OPT 内核对比）
 * 构建两次分别链接 ref/opt 对象：bin/bench_ref、bin/bench_opt
 * 输出 markdown 表格：cycles/op、µs/op、吞吐；功耗由 power_model.py 换算
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

static void row(const char *op, int iters, uint64_t c0, uint64_t c1, uint64_t n0, uint64_t n1)
{
    double cy = (double)(c1 - c0) / iters;
    double us = (double)(n1 - n0) / iters / 1000.0;
    printf("| %s | %d | %.0f | %.2f | %.0f |\n", op, iters, cy, us,
           us > 0 ? 1000000.0 / us : 0.0);
}

int main(void)
{
    uint64_t c0, c1, n0, n1;
    uint8_t buf[4096], out[4096];
    sm4_ctx e4, d4;

    setvbuf(stdout, NULL, _IONBF, 0);
    prand(buf, sizeof buf);

#if SMC_SM3_KERNEL == SMC_KERNEL_REF
    printf("### 内核变体：REF（基线：教科书实现）\n");
#elif SMC_SM3_KERNEL == SMC_KERNEL_TINY
    printf("### 内核变体：TINY/SMALL（最小内存）\n");
#else
    printf("### 内核变体：OPT（指令级优化）\n");
#endif

    printf("\n#### SM3 哈希\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        int iters = 2000;
        c0 = rdtsc(); n0 = now_ns();
        { int i; for (i = 0; i < iters; i++) sm3(buf, 1024, out); }
        c1 = rdtsc(); n1 = now_ns();
        row("hash 1KB", iters, c0, c1, n0, n1);
        c0 = rdtsc(); n0 = now_ns();
        { int i; for (i = 0; i < iters; i++) sm3(buf, 4096, out); }
        c1 = rdtsc(); n1 = now_ns();
        row("hash 4KB", iters, c0, c1, n0, n1);
    }

    printf("\n#### SM4 分组密码\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[16], pt[16], ct[16];
        int iters = 20000;
        prand(key, 16);
        sm4_setkey_enc(&e4, key);
        sm4_setkey_dec(&d4, key);
        memcpy(pt, buf, 16);
        c0 = rdtsc(); n0 = now_ns();
        { int i; for (i = 0; i < iters; i++) sm4_encrypt_block(&e4, pt, ct); }
        c1 = rdtsc(); n1 = now_ns();
        row("ECB encrypt block", iters, c0, c1, n0, n1);
        c0 = rdtsc(); n0 = now_ns();
        { int i; for (i = 0; i < iters; i++) sm4_decrypt_block(&d4, ct, pt); }
        c1 = rdtsc(); n1 = now_ns();
        row("ECB decrypt block", iters, c0, c1, n0, n1);
#if SMC_SM4_MODE_CBC
        {
            uint8_t iv[16];
            int i;
            prand(iv, 16);
            memcpy(out, buf, 1024);
            c0 = rdtsc(); n0 = now_ns();
            for (i = 0; i < 2000; i++) {
                memcpy(iv, buf, 16);
                sm4_cbc_encrypt(&e4, iv, buf, out, 1024);
            }
            c1 = rdtsc(); n1 = now_ns();
            row("CBC encrypt 1KB", 2000, c0, c1, n0, n1);
        }
#endif
#if SMC_SM4_MODE_CTR
        {
            uint8_t ctr[16];
            int i;
            prand(ctr, 16);
            c0 = rdtsc(); n0 = now_ns();
            for (i = 0; i < 2000; i++) {
                uint8_t c2[16];
                memcpy(c2, ctr, 16);
                sm4_ctr_xor(&e4, c2, buf, out, 1024);
            }
            c1 = rdtsc(); n1 = now_ns();
            row("CTR xor 1KB", 2000, c0, c1, n0, n1);
        }
#endif
    }

    printf("\n#### HMAC-SM3 / PRF\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[32], mac[32];
        int iters = 4000;
        prand(key, 32);
        c0 = rdtsc(); n0 = now_ns();
        { int i; for (i = 0; i < iters; i++) hmac_sm3(key, 32, buf, 256, mac); }
        c1 = rdtsc(); n1 = now_ns();
        row("HMAC-SM3 (256B)", iters, c0, c1, n0, n1);
    }

#if SMC_SM2_SIGN || SMC_SM2_ECDH
    printf("\n#### SM2 椭圆曲线\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t d1[32], p1[65], d2[32], p2[65], sig[64], dgst[32], shared[32];
        int iters_slow = 16;
        int i;
        prand(d1, 32); prand(d2, 32); prand(dgst, 32);
        sm2_pub_from_priv(p1, d1);
        sm2_pub_from_priv(p2, d2);

        c0 = rdtsc(); n0 = now_ns();
        for (i = 0; i < iters_slow; i++) {
            uint8_t d[32], p[65];
            prand(d, 32);
            sm2_keygen(d, p, NULL);
        }
        c1 = rdtsc(); n1 = now_ns();
        row("keygen", iters_slow, c0, c1, n0, n1);

        c0 = rdtsc(); n0 = now_ns();
        for (i = 0; i < iters_slow; i++) sm2_sign(sig, dgst, d1, NULL);
        c1 = rdtsc(); n1 = now_ns();
        row("sign", iters_slow, c0, c1, n0, n1);

        c0 = rdtsc(); n0 = now_ns();
        for (i = 0; i < iters_slow; i++) sm2_verify(sig, dgst, p1);
        c1 = rdtsc(); n1 = now_ns();
        row("verify", iters_slow, c0, c1, n0, n1);

        c0 = rdtsc(); n0 = now_ns();
        for (i = 0; i < iters_slow; i++) sm2_ecdh(shared, d1, p2);
        c1 = rdtsc(); n1 = now_ns();
        row("ecdh", iters_slow, c0, c1, n0, n1);
    }
#endif
    return 0;
}
