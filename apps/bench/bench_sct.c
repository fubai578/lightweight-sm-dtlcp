/*
 * bench_sct.c — 标量乘执行路径与秘密的相关性实验（侧信道防护有效性验证）
 *
 * 两个指标：
 *  ① 操作计数（确定性、可复现）：统计不同 nonce 下标量乘的倍点/点加次数。
 *     判据：变时间构建（窗口/wNAF）的加法次数随 nonce 变化 → 存在执行路径泄漏面；
 *           常量时间构建（全表扫描）应**完全恒定**（访存与运算次数不随秘密变化）。
 *  ② 时间方差（辅助）：交错采样（外层轮次、内层输入）后取每输入最小值，
 *     规避 CPU 频率漂移；host 环境噪声地板较大，结论以指标①为准。
 *
 * 构建：SMC_SM2_OPCOUNT=1 提供计数器；SMC_SM2_CT=1 切换常量时间路径。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "smc/sm2.h"
#include "sm2_internal.h"

#if defined(SMC_SM2_OPCOUNT) && SMC_SM2_OPCOUNT
extern uint64_t sm2_op_dbl, sm2_op_add;
#define HAVE_COUNT 1
#else
#define HAVE_COUNT 0
#endif

#if defined(__x86_64__) || defined(_M_X64)
static uint64_t rdtsc(void)
{
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
#else
static uint64_t rdtsc(void) { return 0; }
#endif

#define N_IN  64
#define M_REP 12

static void trand(uint8_t *b, int n, uint32_t *st)
{
    int i;
    for (i = 0; i < n; i++) {
        *st = (*st) * 1103515245u + 12345u;
        b[i] = (uint8_t)(*st >> 16);
    }
}

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static double my_sqrt(double x)
{
    double r;
    int k;
    if (x <= 0) return 0.0;
    r = x;
    for (k = 0; k < 48; k++) r = 0.5 * (r + x / r);
    return r;
}

int main(void)
{
    static uint8_t nonces[N_IN][32];
    static uint64_t tmin[N_IN];
    static uint32_t add_cnt[N_IN];
    static uint32_t dbl_cnt[N_IN];
    sm2_pt g;
    uint32_t st = 0xC0FFEE01u;
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);

#if SMC_SM2_CT
    printf("### 标量乘执行路径相关性实验 — 构建 SMC_SM2_CT=1（常量时间全表扫描）\n\n");
#else
    printf("### 标量乘执行路径相关性实验 — 构建 SMC_SM2_CT=0（变时间窗口/wNAF）\n\n");
#endif

    sm2_fe_from_bytes(&g.x, SM2_PARAM_GX);
    sm2_fe_from_bytes(&g.y, SM2_PARAM_GY);

    /* nonce 集合：62 个随机（高位清零保证 < n）+ 2 个极端（n−1 / 稀疏） */
    for (i = 0; i < N_IN - 2; i++) {
        trand(nonces[i], 32, &st);
        nonces[i][0] &= 0x7F;
    }
    memcpy(nonces[N_IN - 2], SM2_PARAM_N, 32);
    nonces[N_IN - 2][31] -= 1;                    /* n − 1 */
    memset(nonces[N_IN - 1], 0, 32);
    nonces[N_IN - 1][0] = 0x80;
    nonces[N_IN - 1][31] = 0x01;                  /* 稀疏 0x8000…0001 */

    /* ---- 指标① 操作计数（确定性） ---- */
#if HAVE_COUNT
    for (i = 0; i < N_IN; i++) {
        sm2_fe k;
        sm2_jac j;
        sm2_fe_from_bytes(&k, nonces[i]);
        sm2_op_dbl = 0;
        sm2_op_add = 0;
        sm2_scalarmult_jac(&j, &k, &g);
        dbl_cnt[i] = (uint32_t)sm2_op_dbl;
        add_cnt[i] = (uint32_t)sm2_op_add;
    }
    {
        uint32_t amin = add_cnt[0], amax = add_cnt[0];
        uint32_t dmin = dbl_cnt[0], dmax = dbl_cnt[0];
        for (i = 1; i < N_IN; i++) {
            if (add_cnt[i] < amin) amin = add_cnt[i];
            if (add_cnt[i] > amax) amax = add_cnt[i];
            if (dbl_cnt[i] < dmin) dmin = dbl_cnt[i];
            if (dbl_cnt[i] > dmax) dmax = dbl_cnt[i];
        }
        printf("#### 指标①：操作计数（%d 个 nonce，含随机与极端值）\n\n", N_IN);
        printf("| 指标 | 值 |\n|---|---|\n");
        printf("| 点加次数 最小/最大/极差 | %u / %u / %u |\n", amin, amax, amax - amin);
        printf("| 倍点次数 最小/最大/极差 | %u / %u / %u |\n", dmin, dmax, dmax - dmin);
        printf("| 极端 nonce（n−1） | add=%u dbl=%u |\n",
               add_cnt[N_IN - 2], dbl_cnt[N_IN - 2]);
        printf("| 极端 nonce（稀疏） | add=%u dbl=%u |\n",
               add_cnt[N_IN - 1], dbl_cnt[N_IN - 1]);
        printf("| 判定 | %s |\n",
               (amax == amin && dmax == dmin)
                   ? "**恒定** — 执行路径与秘密无关"
                   : "**随秘密变化** — 存在可被时序/功耗观测的执行路径差异");
    }
#else
    printf("（未启用 SMC_SM2_OPCOUNT，跳过操作计数）\n");
#endif

    /* ---- 指标② 时间方差（交错采样，辅助参考） ---- */
    for (i = 0; i < N_IN; i++) tmin[i] = ~(uint64_t)0;
    {
        int rep;
        for (rep = 0; rep < M_REP; rep++) {
            for (i = 0; i < N_IN; i++) {
                sm2_fe k;
                sm2_jac j;
                uint64_t c0, c1;
                sm2_fe_from_bytes(&k, nonces[i]);
                c0 = rdtsc();
                sm2_scalarmult_jac(&j, &k, &g);
                c1 = rdtsc();
                if (c1 - c0 < tmin[i]) tmin[i] = c1 - c0;
            }
        }
    }
    {
        uint64_t sum = 0, mn, mx;
        double mean, var = 0.0, sd, cv;
        qsort(tmin, N_IN, sizeof(uint64_t), cmp_u64);
        mn = tmin[0];
        mx = tmin[N_IN - 1];
        for (i = 0; i < N_IN; i++) sum += tmin[i];
        mean = (double)sum / N_IN;
        for (i = 0; i < N_IN; i++) {
            double d = (double)tmin[i] - mean;
            var += d * d;
        }
        var /= N_IN;
        sd = my_sqrt(var);
        cv = mean > 0 ? sd / mean : 0;
        printf("\n#### 指标②：时间分布（%d 输入 x %d 轮交错采样，取每输入最小值）\n\n", N_IN, M_REP);
        printf("| 指标 | cycles |\n|---|---|\n");
        printf("| 最小 / 中位 / 最大 | %llu / %llu / %llu |\n",
               (unsigned long long)mn, (unsigned long long)tmin[N_IN / 2],
               (unsigned long long)mx);
        printf("| 均值 | %.0f |\n", mean);
        printf("| 标准差 σ / 变异系数 CV | %.0f / %.4f |\n", sd, cv);
        printf("| 极差/均值 | %.4f |\n", mean > 0 ? (double)(mx - mn) / mean : 0);
        printf("\n> 注：host x86-64 存在频率漂移与调度噪声，CV 含测量噪声地板；\n");
        printf("> 判定以指标①（确定性操作计数）为准，板上实测方法见 docs/04。\n");
    }
    return 0;
}
