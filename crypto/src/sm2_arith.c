/*
 * sm2_arith.c — SM2 域/标量/点算术
 *
 * 双内核（SMC_SM2_KERNEL）：
 *   REF : schoolbook 三重循环乘法 + 逐字试商长除法模约减 + 二进制倍点-点加标量乘
 *   OPT : 8x8 展开乘法 + Solinas 折叠快速模约减（2^256 ≡ 2^224+2^96−2^64+1 mod p）
 *         + 4-bit 固定窗口标量乘（SMC_SM2_CT=1 全表扫描常量时间选择）
 * 两内核 API 一致；差分测试保证逐位一致（tests/test_all.c 与 build.sh 的 ref/opt 双构建）。
 */
#include <stddef.h>
#include "sm2_internal.h"

/* ============================ 曲线常量 ============================ */
static const sm2_fe FE_P = {{0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu,
                             0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFEu}};
static const sm2_fe FE_A = {{0xFFFFFFFCu, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu,
                             0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFEu}};
static const sm2_fe FE_B = {{0x4D940E93u, 0xDDBCBD41u, 0x15AB8F92u, 0xF39789F5u,
                             0xCF6509A7u, 0x4D5A9E4Bu, 0x9D9F5E34u, 0x28E9FA9Eu}};
static const sm2_fe FE_N = {{0x39D54123u, 0x53BBF409u, 0x21C6052Bu, 0x7203DF6Bu,
                             0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFEu}};
static const sm2_fe FE_PM2 = {{0xFFFFFFFDu, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu,
                               0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFEu}};
static const sm2_fe FE_NM2 = {{0x39D54121u, 0x53BBF409u, 0x21C6052Bu, 0x7203DF6Bu,
                               0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFEu}};
static const sm2_fe FE_ONE = {{1, 0, 0, 0, 0, 0, 0, 0}};
static const sm2_fe FE_ZERO = {{0, 0, 0, 0, 0, 0, 0, 0}};
static const sm2_fe FE_GX = {{0x334C74C7u, 0x715A4589u, 0xF2660BE1u, 0x8FE30BBFu,
                              0x6A39C994u, 0x5F990446u, 0x1F198119u, 0x32C4AE2Cu}};
static const sm2_fe FE_GY = {{0x2139F0A0u, 0x02DF32E5u, 0xC62A4740u, 0xD0A9877Cu,
                              0x6B692153u, 0x59BDCEE3u, 0xF4F6779Cu, 0xBC3736A2u}};

const uint8_t SM2_PARAM_P[32] = {
    0xFF,0xFF,0xFF,0xFE, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF, 0x00,0x00,0x00,0x00, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF};
const uint8_t SM2_PARAM_A[32] = {
    0xFF,0xFF,0xFF,0xFE, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF, 0x00,0x00,0x00,0x00, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFC};
const uint8_t SM2_PARAM_B[32] = {
    0x28,0xE9,0xFA,0x9E, 0x9D,0x9F,0x5E,0x34, 0x4D,0x5A,0x9E,0x4B, 0xCF,0x65,0x09,0xA7,
    0xF3,0x97,0x89,0xF5, 0x15,0xAB,0x8F,0x92, 0xDD,0xBC,0xBD,0x41, 0x4D,0x94,0x0E,0x93};
const uint8_t SM2_PARAM_GX[32] = {
    0x32,0xC4,0xAE,0x2C, 0x1F,0x19,0x81,0x19, 0x5F,0x99,0x04,0x46, 0x6A,0x39,0xC9,0x94,
    0x8F,0xE3,0x0B,0xBF, 0xF2,0x66,0x0B,0xE1, 0x71,0x5A,0x45,0x89, 0x33,0x4C,0x74,0xC7};
const uint8_t SM2_PARAM_GY[32] = {
    0xBC,0x37,0x36,0xA2, 0xF4,0xF6,0x77,0x9C, 0x59,0xBD,0xCE,0xE3, 0x6B,0x69,0x21,0x53,
    0xD0,0xA9,0x87,0x7C, 0xC6,0x2A,0x47,0x40, 0x02,0xDF,0x32,0xE5, 0x21,0x39,0xF0,0xA0};
const uint8_t SM2_PARAM_N[32] = {
    0xFF,0xFF,0xFF,0xFE, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF,
    0x72,0x03,0xDF,0x6B, 0x21,0xC6,0x05,0x2B, 0x53,0xBB,0xF4,0x09, 0x39,0xD5,0x41,0x23};

/* ============================ 基础工具 ============================ */
void sm2_fe_copy(sm2_fe *r, const sm2_fe *a) { *r = *a; }
void sm2_fe_set_zero(sm2_fe *r) { *r = FE_ZERO; }
void sm2_fe_set_one(sm2_fe *r)  { *r = FE_ONE; }

int sm2_fe_cmp(const sm2_fe *a, const sm2_fe *b)
{
    int i;
    for (i = FE_WORDS - 1; i >= 0; i--) {
        if (a->w[i] < b->w[i]) return -1;
        if (a->w[i] > b->w[i]) return 1;
    }
    return 0;
}

int sm2_fe_is_zero(const sm2_fe *a)
{
    uint32_t t = 0;
    int i;
    for (i = 0; i < FE_WORDS; i++) t |= a->w[i];
    return t == 0;
}

void sm2_fe_from_bytes(sm2_fe *r, const uint8_t in[32])
{
    int i;
    for (i = 0; i < FE_WORDS; i++) {
        const uint8_t *p = in + 4 * (FE_WORDS - 1 - i);
        r->w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                  ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
    }
}

void sm2_fe_to_bytes(uint8_t out[32], const sm2_fe *a)
{
    int i, b;
    for (i = 0; i < FE_WORDS; i++) {
        uint32_t v = a->w[FE_WORDS - 1 - i];
        for (b = 0; b < 4; b++) out[4 * i + b] = (uint8_t)(v >> (24 - 8 * b));
    }
}

static uint32_t fe_add_raw(uint32_t r[FE_WORDS], const uint32_t a[FE_WORDS], const uint32_t b[FE_WORDS])
{
    uint64_t c = 0;
    int i;
    for (i = 0; i < FE_WORDS; i++) {
        c += (uint64_t)a[i] + b[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

static uint32_t fe_sub_raw(uint32_t r[FE_WORDS], const uint32_t a[FE_WORDS], const uint32_t b[FE_WORDS])
{
    uint64_t borrow = 0;
    int i;
    for (i = 0; i < FE_WORDS; i++) {
        uint64_t ai = (uint64_t)a[i];
        uint64_t bi = (uint64_t)b[i] + borrow;
        uint64_t diff = ai - bi;
        r[i] = (uint32_t)diff;
        borrow = (ai < bi) ? 1u : 0u;
    }
    return (uint32_t)borrow;
}

/* 条件搬运：mask 全 1 时 dst = src */
static void cmov_fe(sm2_fe *dst, const sm2_fe *src, uint32_t mask)
{
    int i;
    for (i = 0; i < FE_WORDS; i++) {
        dst->w[i] = (dst->w[i] & ~mask) | (src->w[i] & mask);
    }
}

/* r = r mod m（r < 2m 时一次条件减） */
static void fe_mod_once(uint32_t r[FE_WORDS], const sm2_fe *m)
{
    uint32_t t[FE_WORDS];
    uint32_t borrow = fe_sub_raw(t, r, m->w);
    uint32_t mask = (uint32_t)0 - (1u - borrow);   /* 无借位 → 采减法结果 */
    int i;
    for (i = 0; i < FE_WORDS; i++) r[i] = (r[i] & ~mask) | (t[i] & mask);
}

/* xw[0..8]（9 字窗口） -= q * m（q 为试商） */
static void mul_sub_mod(uint32_t *xw, const sm2_fe *m, uint32_t q)
{
    uint64_t borrow = 0;
    int j;
    for (j = 0; j < FE_WORDS; j++) {
        uint64_t sub = (uint64_t)m->w[j] * q + borrow;
        uint32_t s_lo = (uint32_t)sub, s_hi = (uint32_t)(sub >> 32);
        uint32_t a = xw[j], d1 = a - s_lo;
        uint32_t flag = (a < s_lo) ? 1u : 0u;
        xw[j] = d1;
        borrow = (uint64_t)s_hi + flag;
    }
    xw[FE_WORDS] = (uint32_t)(xw[FE_WORDS] - borrow);
}

/* 512 位 x[16] mod m → r[8]（教科书长除法：逐字试商） */
static void fe_reduce_long(uint32_t r[FE_WORDS], uint32_t x[2 * FE_WORDS], const sm2_fe *m)
{
    int i, k, j;
    for (i = 2 * FE_WORDS - 1; i >= FE_WORDS; i--) {
        for (k = 0; k < 4 && x[i]; k++) {
            mul_sub_mod(x + i - FE_WORDS, m, x[i]);
        }
        x[i] = 0;
    }
    {
        uint32_t t[FE_WORDS];
        uint32_t borrow = fe_sub_raw(t, x, m->w);
        for (j = 0; j < FE_WORDS; j++) r[j] = borrow ? x[j] : t[j];
    }
}

/* ============================ Fp ============================ */
void sm2_fp_add(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t t[FE_WORDS];
    uint32_t c = fe_add_raw(r->w, a->w, b->w);
    uint32_t borrow = fe_sub_raw(t, r->w, FE_P.w);
    uint32_t mask = (uint32_t)0 - (c | (1u - borrow));  /* c=1 或 r>=p 时采用 t */
    cmov_fe(r, (const sm2_fe *)(void *)t, mask);
}

void sm2_fp_sub(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t t[FE_WORDS];
    uint32_t borrow = fe_sub_raw(r->w, a->w, b->w);
    uint32_t mask;
    fe_add_raw(t, r->w, FE_P.w);
    mask = (uint32_t)0 - borrow;
    cmov_fe(r, (const sm2_fe *)(void *)t, mask);
}

void sm2_fp_reduce(sm2_fe *r) { fe_mod_once(r->w, &FE_P); }

/* -------------------- 快速折叠约减（OPT） -------------------- */
#if SMC_SM2_KERNEL == SMC_KERNEL_OPT
/*
 * Solinas 折叠：2^256 ≡ 2^224 + 2^96 − 2^64 + 1 (mod p)
 * x_j (j≥8) → 位置 j−8(+) j−5(+) j−6(−) j−1(+)；迭代至高 8 字清零。
 */
static void fold_round(uint32_t x[2 * FE_WORDS])
{
    int64_t acc[2 * FE_WORDS + 1];
    int i, j;
    for (i = 0; i < 2 * FE_WORDS; i++) acc[i] = (int64_t)(uint64_t)x[i];
    acc[2 * FE_WORDS] = 0;

    for (j = FE_WORDS; j < 2 * FE_WORDS; j++) {
        int64_t h = (int64_t)(uint64_t)x[j];
        acc[j - FE_WORDS] += h;
        acc[j - 5]        += h;
        acc[j - 6]        -= h;
        acc[j - 1]        += h;
        acc[j]             = 0;      /* 高位已折叠转移，必须清零 */
    }
    for (i = 0; i < 2 * FE_WORDS; i++) {
        /* 归一化：算术右移 = floor(v/2^32)（gcc/ARM 均为算术移位，见 docs/02 §3.3） */
        int64_t v = acc[i];
        x[i] = (uint32_t)v;
        acc[i + 1] += v >> 32;
    }
}

static void fe_reduce_fold(uint32_t r[FE_WORDS], uint32_t x[2 * FE_WORDS], const sm2_fe *m)
{
    int round, j;
    /* 折叠直至高 8 字清零（每轮值域至少降 32 位，16 轮为数学上限）；
       严禁在高位残留时提前退出——那会静默截断高位 */
    for (round = 0; round < 16; round++) {
        uint32_t hi = 0;
        for (j = FE_WORDS; j < 2 * FE_WORDS; j++) hi |= x[j];
        if (!hi) break;
        fold_round(x);
        /* 高位收缩到 3 字以内后改用试商收尾（常数更低） */
        if (round >= 1) {
            int nz = 0;
            for (j = 2 * FE_WORDS - 1; j >= FE_WORDS; j--) if (x[j]) { nz = j + 1; break; }
            if (nz <= FE_WORDS + 3) break;
        }
    }
    /* 试商收尾：处理残留高位（≤3 字） */
    for (j = 2 * FE_WORDS - 1; j >= FE_WORDS; j--) {
        int k;
        for (k = 0; k < 4 && x[j]; k++) mul_sub_mod(x + j - FE_WORDS, m, x[j]);
        x[j] = 0;
    }
    fe_mod_once(x, m);
    fe_mod_once(x, m);
    for (j = 0; j < FE_WORDS; j++) r[j] = x[j];
}
#endif

/* -------------------- 乘法主体 -------------------- */
#if SMC_SM2_KERNEL == SMC_KERNEL_REF

void sm2_fp_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t x[2 * FE_WORDS];
    int i, j;
    for (i = 0; i < 2 * FE_WORDS; i++) x[i] = 0;
    for (i = 0; i < FE_WORDS; i++) {
        uint64_t carry = 0;
        for (j = 0; j < FE_WORDS; j++) {
            uint64_t t = (uint64_t)x[i + j] + (uint64_t)a->w[i] * b->w[j] + carry;
            x[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        x[i + FE_WORDS] = (uint32_t)carry;
    }
    fe_reduce_long(r->w, x, &FE_P);
}

#elif SMC_SM2_KERNEL == SMC_KERNEL_OPT

void sm2_fp_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t x[2 * FE_WORDS];
    const uint32_t *A = a->w, *B = b->w;
    int i;

    /* 8x8 schoolbook 完全展开：消除索引计算与循环分支 */
#define MULADD(i_, j_)                                              \
    do { uint64_t t = (uint64_t)x[(i_) + (j_)] + (uint64_t)A[i_] * B[j_] + carry; \
         x[(i_) + (j_)] = (uint32_t)t; carry = t >> 32; } while (0)
#define MULROW(i_)                                                  \
    do { uint64_t carry = 0;                                        \
         MULADD(i_, 0); MULADD(i_, 1); MULADD(i_, 2); MULADD(i_, 3); \
         MULADD(i_, 4); MULADD(i_, 5); MULADD(i_, 6); MULADD(i_, 7); \
         x[(i_) + FE_WORDS] = (uint32_t)carry; } while (0)

    for (i = 0; i < 2 * FE_WORDS; i++) x[i] = 0;
    MULROW(0); MULROW(1); MULROW(2); MULROW(3);
    MULROW(4); MULROW(5); MULROW(6); MULROW(7);
#undef MULROW
#undef MULADD

    fe_reduce_fold(r->w, x, &FE_P);
}

#else
#error "bad SMC_SM2_KERNEL"
#endif

void sm2_fp_sqr(sm2_fe *r, const sm2_fe *a) { sm2_fp_mul(r, a, a); }

/* ============================ Fn（mod n） ============================ */
void sm2_fn_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t x[2 * FE_WORDS];
    int i, j;
    for (i = 0; i < 2 * FE_WORDS; i++) x[i] = 0;
    for (i = 0; i < FE_WORDS; i++) {
        uint64_t carry = 0;
        for (j = 0; j < FE_WORDS; j++) {
            uint64_t t = (uint64_t)x[i + j] + (uint64_t)a->w[i] * b->w[j] + carry;
            x[i + j] = (uint32_t)t;
            carry = t >> 32;
        }
        x[i + FE_WORDS] = (uint32_t)carry;
    }
    fe_reduce_long(r->w, x, &FE_N);
}

void sm2_fn_add(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t t[FE_WORDS];
    uint32_t c = fe_add_raw(r->w, a->w, b->w);
    uint32_t borrow = fe_sub_raw(t, r->w, FE_N.w);
    uint32_t mask = (uint32_t)0 - (c | (1u - borrow));
    cmov_fe(r, (const sm2_fe *)(void *)t, mask);
}

void sm2_fn_sub(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t t[FE_WORDS];
    uint32_t borrow = fe_sub_raw(r->w, a->w, b->w);
    uint32_t mask;
    fe_add_raw(t, r->w, FE_N.w);
    mask = (uint32_t)0 - borrow;
    cmov_fe(r, (const sm2_fe *)(void *)t, mask);
}

void sm2_fn_neg(sm2_fe *r, const sm2_fe *a)
{
    if (sm2_fe_is_zero(a)) { *r = FE_ZERO; return; }
    fe_sub_raw(r->w, FE_N.w, a->w);
}

void sm2_fn_mod(sm2_fe *r, const sm2_fe *a)
{
    /* 任意 256 位输入：2^256 < 2n，条件减至多两次即可归一 */
    *r = *a;
    fe_mod_once(r->w, &FE_N);
    fe_mod_once(r->w, &FE_N);
}

int sm2_fe_lt_n(const sm2_fe *a) { return sm2_fe_cmp(a, &FE_N) < 0; }

int sm2_fn_in_range(const sm2_fe *a)
{
    return !sm2_fe_is_zero(a) && sm2_fe_cmp(a, &FE_N) < 0;
}

/* -------------------- Fermat 求逆（4-bit 窗口） -------------------- */
static void mul_mod_m(sm2_fe *r, const sm2_fe *a, const sm2_fe *b, int mod_p)
{
    if (mod_p) sm2_fp_mul(r, a, b);
    else       sm2_fn_mul(r, a, b);
}

static void fe_pow_mod(sm2_fe *r, const sm2_fe *a, const sm2_fe *e, int mod_p)
{
    sm2_fe tbl[16];      /* tbl[i] = a^(i+1) mod m */
    sm2_fe acc;
    int i, w, started = 0;

    tbl[0] = *a;
    for (i = 1; i < 16; i++) mul_mod_m(&tbl[i], &tbl[i - 1], a, mod_p);

    acc = FE_ONE;
    for (w = FE_WORDS - 1; w >= 0; w--) {
        for (i = 28; i >= 0; i -= 4) {
            uint32_t nib = (e->w[w] >> (unsigned)i) & 0xFu;
            if (started) {
                /* 每窗口 4 次平方（acc = acc^16） */
                mul_mod_m(&acc, &acc, &acc, mod_p);
                mul_mod_m(&acc, &acc, &acc, mod_p);
                mul_mod_m(&acc, &acc, &acc, mod_p);
                mul_mod_m(&acc, &acc, &acc, mod_p);
            }
            if (nib) {
                sm2_fe t;
                mul_mod_m(&t, &acc, &tbl[nib - 1], mod_p);
                acc = t;
                started = 1;
            }
        }
    }
    *r = acc;
}

void sm2_fp_inv(sm2_fe *r, const sm2_fe *a) { fe_pow_mod(r, a, &FE_PM2, 1); }
void sm2_fn_inv(sm2_fe *r, const sm2_fe *a) { fe_pow_mod(r, a, &FE_NM2, 0); }

/* ============================ 点运算 ============================ */
void sm2_jac_set_inf(sm2_jac *r)
{
    r->x = FE_ONE; r->y = FE_ONE; r->z = FE_ONE; r->inf = 1;
}

void sm2_jac_from_affine(sm2_jac *r, const sm2_pt *p)
{
    r->x = p->x; r->y = p->y; r->z = FE_ONE; r->inf = 0;
}

void sm2_jac_to_affine(sm2_pt *r, const sm2_jac *p)
{
    sm2_fe zi, zi2, zi3;
    sm2_fp_inv(&zi, &p->z);
    sm2_fp_sqr(&zi2, &zi);
    sm2_fp_mul(&zi3, &zi2, &zi);
    sm2_fp_mul(&r->x, &p->x, &zi2);
    sm2_fp_mul(&r->y, &p->y, &zi3);
}

/* dbl-2009-l（a = −3 优化：E = 3(A − Z1⁴)） */
void sm2_jac_double(sm2_jac *r, const sm2_jac *p)
{
    sm2_fe a2, b2, c2, d, e, f;
    if (p->inf || sm2_fe_is_zero(&p->y)) { sm2_jac_set_inf(r); return; }

    sm2_fp_sqr(&a2, &p->x);
    sm2_fp_sqr(&b2, &p->y);
    sm2_fp_sqr(&c2, &b2);
    {
        sm2_fe t1, t2;
        sm2_fp_add(&t1, &p->x, &b2);
        sm2_fp_sqr(&t2, &t1);
        sm2_fp_sub(&t2, &t2, &a2);
        sm2_fp_sub(&t2, &t2, &c2);
        sm2_fp_add(&d, &t2, &t2);          /* D = 2((X1+B)²−A−C) */
    }
    {
        sm2_fe z4, t;
        sm2_fp_sqr(&z4, &p->z);
        sm2_fp_sqr(&z4, &z4);
        sm2_fp_sub(&t, &a2, &z4);          /* t = A − Z1⁴ */
        sm2_fp_add(&e, &t, &t);
        sm2_fp_add(&e, &e, &t);            /* E = 3(A − Z1⁴) (a = −3) */
    }
    sm2_fp_sqr(&f, &e);
    {
        sm2_fe t1, x3, y3, z3;
        sm2_fp_add(&t1, &d, &d);
        sm2_fp_sub(&x3, &f, &t1);          /* X3 = F − 2D */
        sm2_fp_sub(&t1, &d, &x3);
        sm2_fp_mul(&y3, &e, &t1);
        sm2_fp_add(&t1, &c2, &c2);
        sm2_fp_add(&t1, &t1, &t1);
        sm2_fp_add(&t1, &t1, &t1);
        sm2_fp_sub(&y3, &y3, &t1);         /* Y3 = E(D−X3) − 8C */
        sm2_fp_mul(&z3, &p->y, &p->z);
        sm2_fp_add(&z3, &z3, &z3);         /* Z3 = 2Y1Z1 */
        r->x = x3; r->y = y3; r->z = z3; r->inf = 0;
    }
}

/* add-2007-bl */
void sm2_jac_add(sm2_jac *r, const sm2_jac *p, const sm2_jac *q)
{
    sm2_fe z1z1, z2z2, u1, u2, s1, s2, h, i2, j2, rr, v;
    if (p->inf) { *r = *q; return; }
    if (q->inf) { *r = *p; return; }

    sm2_fp_sqr(&z1z1, &p->z);
    sm2_fp_sqr(&z2z2, &q->z);
    sm2_fp_mul(&u1, &p->x, &z2z2);
    sm2_fp_mul(&u2, &q->x, &z1z1);
    sm2_fp_mul(&s1, &p->y, &q->z);
    sm2_fp_mul(&s1, &s1, &z2z2);
    sm2_fp_mul(&s2, &q->y, &p->z);
    sm2_fp_mul(&s2, &s2, &z1z1);
    sm2_fp_sub(&h, &u2, &u1);
    sm2_fp_sub(&rr, &s2, &s1);

    if (sm2_fe_is_zero(&h)) {
        if (sm2_fe_is_zero(&rr)) { sm2_jac_double(r, p); return; }
        sm2_jac_set_inf(r); return;
    }
    {
        sm2_fe t;
        sm2_fp_add(&t, &h, &h);
        sm2_fp_sqr(&i2, &t);               /* I = (2H)² */
        sm2_fp_mul(&j2, &h, &i2);          /* J = H·I */
        sm2_fp_add(&rr, &rr, &rr);         /* r = 2(S2−S1) */
        sm2_fp_mul(&v, &u1, &i2);          /* V = U1·I */
    }
    {
        sm2_fe t1, x3, y3, z3;
        sm2_fp_sqr(&x3, &rr);
        sm2_fp_sub(&x3, &x3, &j2);
        sm2_fp_sub(&x3, &x3, &v);
        sm2_fp_sub(&x3, &x3, &v);
        sm2_fp_sub(&t1, &v, &x3);
        sm2_fp_mul(&y3, &rr, &t1);
        sm2_fp_mul(&t1, &s1, &j2);
        sm2_fp_add(&t1, &t1, &t1);
        sm2_fp_sub(&y3, &y3, &t1);
        sm2_fp_add(&t1, &p->z, &q->z);
        sm2_fp_sqr(&t1, &t1);
        sm2_fp_sub(&t1, &t1, &z1z1);
        sm2_fp_sub(&t1, &t1, &z2z2);
        sm2_fp_mul(&z3, &t1, &h);
        r->x = x3; r->y = y3; r->z = z3; r->inf = 0;
    }
}

/* add-2007-bl mixed（Q 仿射） */
void sm2_jac_add_mixed(sm2_jac *r, const sm2_jac *p, const sm2_pt *q)
{
    sm2_fe z1z1, u2, s2, h, hh, i2, j2, rr, v;
    if (p->inf) { sm2_jac_from_affine(r, q); return; }

    sm2_fp_sqr(&z1z1, &p->z);
    sm2_fp_mul(&u2, &q->x, &z1z1);
    sm2_fp_mul(&s2, &q->y, &p->z);
    sm2_fp_mul(&s2, &s2, &z1z1);
    sm2_fp_sub(&h, &u2, &p->x);
    sm2_fp_sub(&rr, &s2, &p->y);

    if (sm2_fe_is_zero(&h)) {
        if (sm2_fe_is_zero(&rr)) { sm2_jac_double(r, p); return; }
        sm2_jac_set_inf(r); return;
    }
    sm2_fp_sqr(&hh, &h);
    sm2_fp_add(&i2, &hh, &hh);
    sm2_fp_add(&i2, &i2, &i2);             /* I = 4HH */
    sm2_fp_mul(&j2, &h, &i2);
    sm2_fp_add(&rr, &rr, &rr);
    sm2_fp_mul(&v, &p->x, &i2);
    {
        sm2_fe t1, x3, y3, z3;
        sm2_fp_sqr(&x3, &rr);
        sm2_fp_sub(&x3, &x3, &j2);
        sm2_fp_sub(&x3, &x3, &v);
        sm2_fp_sub(&x3, &x3, &v);
        sm2_fp_sub(&t1, &v, &x3);
        sm2_fp_mul(&y3, &rr, &t1);
        sm2_fp_mul(&t1, &p->y, &j2);
        sm2_fp_add(&t1, &t1, &t1);
        sm2_fp_sub(&y3, &y3, &t1);
        sm2_fp_add(&t1, &p->z, &h);
        sm2_fp_sqr(&t1, &t1);
        sm2_fp_sub(&t1, &t1, &z1z1);
        sm2_fp_sub(&t1, &t1, &hh);          /* Z3 = (Z1+H)²−Z1Z1−HH */
        z3 = t1;
        r->x = x3; r->y = y3; r->z = z3; r->inf = 0;
    }
}

int sm2_pt_on_curve(const sm2_pt *p)
{
    sm2_fe t1, t2, t3;
    sm2_fp_sqr(&t1, &p->y);
    sm2_fp_sqr(&t2, &p->x);
    sm2_fp_mul(&t2, &t2, &p->x);
    sm2_fp_mul(&t3, &FE_A, &p->x);
    sm2_fp_add(&t2, &t2, &t3);
    sm2_fp_add(&t2, &t2, &FE_B);
    return sm2_fe_cmp(&t1, &t2) == 0;
}

int sm2_parse_pubkey(sm2_pt *p, const uint8_t pub[SMC_SM2_PUBKEY])
{
    sm2_fe px, py;
    if (pub[0] != 0x04) return -1;
    sm2_fe_from_bytes(&px, pub + 1);
    sm2_fe_from_bytes(&py, pub + 33);
    if (sm2_fe_cmp(&px, &FE_P) >= 0 || sm2_fe_cmp(&py, &FE_P) >= 0) return -2;
    p->x = px; p->y = py;
    if (!sm2_pt_on_curve(p)) return -3;
    return 0;
}

void sm2_encode_pubkey(uint8_t pub[SMC_SM2_PUBKEY], const sm2_pt *p)
{
    pub[0] = 0x04;
    sm2_fe_to_bytes(pub + 1, &p->x);
    sm2_fe_to_bytes(pub + 33, &p->y);
}

/* ============================ 标量乘 ============================ */
#if SMC_SM2_KERNEL == SMC_KERNEL_REF
static int fe_bit(const sm2_fe *k, int i)
{
    return (int)((k->w[i >> 5] >> (i & 31)) & 1u);
}
#else
static int fe_nibble(const sm2_fe *k, int i)
{
    return (int)((k->w[i >> 3] >> ((i & 7) * 4)) & 0xFu);
}
#endif

#if SMC_SM2_KERNEL == SMC_KERNEL_REF

/* 二进制倍点-点加（MSB 优先） */
void sm2_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const sm2_pt *p)
{
    sm2_jac acc, pt;
    int i;
    sm2_jac_set_inf(&acc);
    sm2_jac_from_affine(&pt, p);
    for (i = 8 * 32 - 1; i >= 0; i--) {
        sm2_jac_double(&acc, &acc);
        if (fe_bit(k, i)) {
            sm2_jac t;
            sm2_jac_add(&t, &acc, &pt);
            acc = t;
        }
    }
    *r = acc;
}

#elif SMC_SM2_KERNEL == SMC_KERNEL_OPT

void sm2_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const sm2_pt *p)
{
    sm2_jac tbl[15], acc, t;
    int i, wnd;

    /* 预计算 T[i] = (i+1)·P（混合加，链式保持 Jacobian） */
    sm2_jac_from_affine(&tbl[0], p);
    for (i = 1; i < 15; i++) sm2_jac_add_mixed(&tbl[i], &tbl[i - 1], p);

    sm2_jac_set_inf(&acc);
    for (wnd = 63; wnd >= 0; wnd--) {
        int w = fe_nibble(k, wnd);
        int b;
        for (b = 0; b < 4; b++) sm2_jac_double(&acc, &acc);
#if SMC_SM2_CT
        {
            /* 常量时间：全表扫描 cmov 选表项（访存模式恒定），再做一次加法 */
            sm2_jac sel;
            uint32_t want = (uint32_t)(w ? (unsigned)(w - 1) : 0u);
            uint32_t any  = (uint32_t)(w != 0);
            sm2_jac_set_inf(&sel);
            for (i = 0; i < 15; i++) {
                uint32_t m = (uint32_t)0 - ((want == (uint32_t)i) & any);
                cmov_fe(&sel.x, &tbl[i].x, m);
                cmov_fe(&sel.y, &tbl[i].y, m);
                cmov_fe(&sel.z, &tbl[i].z, m);
                sel.inf = (int)(((uint32_t)sel.inf & ~m) | ((uint32_t)tbl[i].inf & m));
            }
            sm2_jac_add(&t, &acc, &sel);   /* sel=inf 时返回 acc */
            acc = t;
        }
#else
        if (w) {
            sm2_jac_add(&t, &acc, &tbl[w - 1]);
            acc = t;
        }
#endif
    }
    *r = acc;
}

#else
#error "bad SMC_SM2_KERNEL"
#endif

void sm2_scalarmult(sm2_pt *r, const sm2_fe *k, const sm2_pt *p)
{
    sm2_jac j;
    sm2_scalarmult_jac(&j, k, p);
    sm2_jac_to_affine(r, &j);
}

void sm2_scalarmult_base(sm2_pt *r, const sm2_fe *k)
{
    sm2_pt g;
    g.x = FE_GX; g.y = FE_GY;
    sm2_scalarmult(r, k, &g);
}

int sm2_scalarmult_add(sm2_pt *r, const sm2_fe *k1, const sm2_pt *p1,
                       const sm2_fe *k2, const sm2_pt *p2)
{
    sm2_jac j1, j2, s;
    sm2_scalarmult_jac(&j1, k1, p1);
    sm2_scalarmult_jac(&j2, k2, p2);
    sm2_jac_add(&s, &j1, &j2);
    if (s.inf) return -1;
    sm2_jac_to_affine(r, &s);
    return 0;
}
