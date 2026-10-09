/*
 * bench_gmssl.c — 第三方基线基准（GmSSL 3.1.1，纯 C 参考实现）
 *
 * 目的：本仓库此前的加速比全部是"本库 REF vs 本库 OPT"（自己比自己）。
 *       本程序用**纯 C 的第三方实现**做外部锚点——GmSSL 是国内事实上的国密参考实现，
 *       且与本项目同为可移植 C（非 OpenSSL 那种手写汇编），是更公平的对照。
 *
 * 口径对齐（与 apps/bench/bench_crypto.c 尽量一致）：
 *   SM3 hash 1KB/4KB      : sm3_digest()
 *   HMAC-SM3 (256B)       : sm3_hmac()
 *   SM4 ECB block         : sm4_encrypt()
 *   SM4 CBC/CTR 1KB       : sm4_cbc_encrypt() / sm4_ctr_encrypt()
 *   SM2 基点乘 k·G        : sm2_point_mul_generator()   ← 对应本库 sm2_scalarmult_base（comb）
 *   SM2 sign              : sm2_do_sign()（含随机 k 生成，对应本库 sm2_sign 的 nonce 派生）
 *   SM2 verify            : sm2_do_verify()
 *   SM2 ecdh              : sm2_do_ecdh()（变基点）
 *
 * 注意：GmSSL 与本库的 API 包装层次不完全相同（如 keygen 的随机数来源、密钥序列化），
 * 故本程序测量的是**对应原语**，不测 GmSSL 的密钥编解码/证书路径。
 * 构建与运行见 scripts/thirdparty_bench.sh。
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <gmssl/sm3.h>
#include <gmssl/sm4.h>
#include <gmssl/sm2.h>
#include <gmssl/rand.h>

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
    double us = (double)ns / iters / 1000.0;
    printf("| %s | %d | %.0f | %.2f | %.0f |\n", op, iters, (double)cyc / iters, us,
           us > 0 ? 1000000.0 / us : 0.0);
}

int main(void)
{
    uint64_t c0, n0;
    static uint8_t buf[4096], out[4096];

    setvbuf(stdout, NULL, _IONBF, 0);
    prand(buf, sizeof buf);

    printf("### 第三方基线：GmSSL 3.1.1（纯 C 参考实现）\n");
    printf("> 每项 %d 轮取最小值；与本库基准同一 host、同一口径逐项对照\n", BENCH_ROUNDS);

    printf("\n#### SM3 哈希\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        int iters = 2000;
        uint8_t dgst[32];
        MEASURE(sm3_digest(buf, 1024, dgst), iters, c0, n0);
        row("hash 1KB", iters, c0, n0);
        MEASURE(sm3_digest(buf, 4096, dgst), iters, c0, n0);
        row("hash 4KB", iters, c0, n0);
    }

    printf("\n#### SM4 分组密码\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[16], pt[16], ct[16], iv[16], ctr[16], cbuf[1024];
        SM4_KEY ek;
        int iters = 20000;

        prand(key, 16);
        sm4_set_encrypt_key(&ek, key);
        memcpy(pt, buf, 16);
        MEASURE(sm4_encrypt(&ek, pt, ct), iters, c0, n0);
        row("ECB encrypt block", iters, c0, n0);

        prand(iv, 16);
        /* 注意：GmSSL 的 sm4_cbc_encrypt 第 4 参数是**块数**（nblocks），不是字节数；
           传 1024 会越界写 16KB → 段错误。CTR 版本同参数位则是字节数（inlen），两者不一致。 */
        MEASURE((memcpy(iv, buf, 16), sm4_cbc_encrypt(&ek, iv, buf, 1024 / 16, cbuf)),
                2000, c0, n0);
        row("CBC encrypt 1KB", 2000, c0, n0);

        prand(ctr, 16);
        MEASURE((memcpy(ctr, buf + 16, 16), sm4_ctr_encrypt(&ek, ctr, buf, 1024, cbuf)),
                2000, c0, n0);
        row("CTR xor 1KB", 2000, c0, n0);
    }

    printf("\n#### HMAC-SM3\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t key[32], mac[32];
        int iters = 4000;
        prand(key, 32);
        MEASURE(sm3_hmac(key, 32, buf, 256, mac), iters, c0, n0);
        row("HMAC-SM3 (256B)", iters, c0, n0);
    }

    printf("\n#### SM2 椭圆曲线\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        uint8_t d1[32], dgst[32];
        SM2_KEY key1, key2;
        SM2_SIGNATURE sig;
        SM2_POINT R, sh;
        int iters_slow = 32;

        prand(d1, 32);
        d1[0] &= 0x7F;
        if (sm2_point_mul_generator(&key1.public_key, d1) != 1) {
            printf("（sm2_point_mul_generator 失败）\n");
            return 1;
        }
        memcpy(key1.private_key, d1, 32);

        prand(dgst, 32);
        prand(key2.private_key, 32);
        key2.private_key[0] &= 0x7F;
        if (sm2_point_mul_generator(&key2.public_key, key2.private_key) != 1) {
            printf("（第二个密钥生成失败）\n");
            return 1;
        }

        MEASURE(sm2_point_mul_generator(&R, d1), iters_slow, c0, n0);
        row("keygen (k·G 基点乘)", iters_slow, c0, n0);

        /* 变基点 k·P：与本库 sm2_ecdh 的标量乘同口径 */
        MEASURE(sm2_point_mul(&R, d1, &key2.public_key), iters_slow, c0, n0);
        row("scalarmult (k·P 变基点)", iters_slow, c0, n0);

        /* 熵源开销单列：GmSSL 的 do_sign 每次签名都向系统 CSPRNG 取 k（rand_win 为系统调用），
           而本库用 HMAC-SM3 派生 nonce（无系统调用）。此行的目的是让对照表可扣除该项，
           避免把"nonce 来源差异"误读为"签名运算差异"。 */
        {
            uint8_t rnd[32];
            MEASURE(rand_bytes(rnd, sizeof rnd), 2000, c0, n0);
            row("rand_bytes(32) 熵源系统调用（参考）", 2000, c0, n0);
        }

        MEASURE(sm2_do_sign(&key1, dgst, &sig), iters_slow, c0, n0);
        row("sign", iters_slow, c0, n0);

        (void)sm2_do_sign(&key1, dgst, &sig);   /* 确保 sig 与 key1/dgst 匹配 */
        MEASURE(sm2_do_verify(&key1, dgst, &sig), iters_slow, c0, n0);
        row("verify", iters_slow, c0, n0);

        MEASURE(sm2_do_ecdh(&key1, &key2.public_key, &sh), iters_slow, c0, n0);
        row("ecdh (k·P 变基点)", iters_slow, c0, n0);
    }

    /* 域算术单列：与本库同名行对照，用于定位标量乘差异来自"标量乘算法"还是"域运算本身" */
    printf("\n#### 域算术 Fp（根因定位用）\n| operation | iters | cycles/op | us/op | ops/s |\n|---|---|---|---|---|\n");
    {
        SM2_Fp fa, fb, fr;
        uint8_t tb[32];
        int iters = 20000;
        prand(tb, 32); sm2_bn_from_bytes(fa, tb);
        prand(tb, 32); sm2_bn_from_bytes(fb, tb);
        MEASURE(sm2_fp_mul(fr, fa, fb), iters, c0, n0);
        row("fe_mul", iters, c0, n0);
        MEASURE(sm2_fp_sqr(fr, fa), iters, c0, n0);
        row("fe_sqr", iters, c0, n0);
        MEASURE(sm2_fp_inv(fr, fa), 2000, c0, n0);
        row("fe_inv", 2000, c0, n0);
    }
    return 0;
}
