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

/* ============================ 操作计数钩子（侧信道实验） ============================ */
/* SMC_SM2_OPCOUNT=1 时统计点运算次数与域乘法次数：
   ① 点加/倍点次数——标量乘的「结构」是否随秘密变化；
   ② 域乘法次数——实际执行的**工作量**。两者必须分别看：点运算内部还有
      「结果/操作数为无穷远」的提前返回，只数①会把被跳过的运算也计成一笔，
      从而看不出工作量差异（本项目正是这样漏掉过一次真实泄漏）。 */
#if defined(SMC_SM2_OPCOUNT) && SMC_SM2_OPCOUNT
uint64_t sm2_op_dbl, sm2_op_add, sm2_op_mul;
#define SMC_OP_DBL() (sm2_op_dbl++)
#define SMC_OP_ADD() (sm2_op_add++)
#define SMC_OP_MUL() (sm2_op_mul++)
#else
#define SMC_OP_DBL() ((void)0)
#define SMC_OP_ADD() ((void)0)
#define SMC_OP_MUL() ((void)0)
#endif

/* ================= 常量时间点运算开关 =================
 * 变时间构建保留「无穷远 / y=0」的提前返回（性能路径）。
 * CT 构建改为**无条件算完 + 掩码选择**：提前返回会随秘密选择不同的执行路径与
 * 不同的实际工作量（点加少做十余次域乘），而运算计数钩子看不到这一点。
 * 唯一的例外是 h = 0（P = ±Q）退化情形仍保留分支：它要求两点的 x 坐标相等，
 * 在标量乘路径上触发它等同于构造一个离散对数关系（或 2^-256 量级的巧合），
 * 攻击者不可构造——见 docs/04 §3.6「常量时间口径」。 */
#if SMC_SM2_CT && SMC_SM2_KERNEL == SMC_KERNEL_OPT
#define SMC_PT_UNIFORM 1
#else
#define SMC_PT_UNIFORM 0
#endif

/* ============================ 曲线常量 ============================ */
/* 是否使用基点 comb 预计算表（OPT 内核；CT 与非 CT 都可用，选表方式不同） */
#define SMC_SM2_USE_COMB_TABLE \
    (SMC_SM2_KERNEL == SMC_KERNEL_OPT && SMC_SM2_FIXED_TABLE)

/* 第 j 个平衡量 K_j（标量第 j+i·d 位组成的 t 位窗口）。
 * 放在文件级：comb 基点乘与对端公钥表两处都要用，且各自由独立开关控制 */
#if SMC_SM2_KERNEL == SMC_KERNEL_OPT
static unsigned comb_idx(const sm2_fe *k, int j)
{
    unsigned a = 0;
    int i;
    for (i = 0; i < SMC_SM2_TBITS; i++) {
        int b = j + i * SMC_SM2_DBLK;
        if (b < 256) a |= (unsigned)((k->w[b >> 5] >> (b & 31)) & 1u) << i;
    }
    return a;
}
#endif

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
#if !SMC_SM2_USE_COMB_TABLE
/* 启用 comb 表时基点只从表中取，这两个常量不再被引用 */
static const sm2_fe FE_GX = {{0x334C74C7u, 0x715A4589u, 0xF2660BE1u, 0x8FE30BBFu,
                              0x6A39C994u, 0x5F990446u, 0x1F198119u, 0x32C4AE2Cu}};
static const sm2_fe FE_GY = {{0x2139F0A0u, 0x02DF32E5u, 0xC62A4740u, 0xD0A9877Cu,
                              0x6B692153u, 0x59BDCEE3u, 0xF4F6779Cu, 0xBC3736A2u}};
#endif

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

#if SMC_PT_UNIFORM || (SMC_SM2_USE_COMB_TABLE && SMC_SM2_CT)
/* 无穷远点的规范表示（x = y = z = 1、inf = 1；与 sm2_jac_set_inf 一致） */
static const sm2_jac JAC_INF = {
    {{1,0,0,0,0,0,0,0}}, {{1,0,0,0,0,0,0,0}}, {{1,0,0,0,0,0,0,0}}, 1
};

/* 「空操作」占位点：inf = 1（真实结果由掩码处理：要么丢弃、要么保留累加器），
   但**坐标刻意不等于**无穷远的规范表示 (1,1,1)。否则当「累加器仍是无穷远」且
   「本窗口为空」时，两点坐标完全相同 → 触发 P = ±Q 的 h = 0 退化分支，
   使实际工作量随秘密（前导空窗口个数）变化——这正是本项修复要消除的泄漏面。 */
static void jac_set_noop(sm2_jac *r)
{
    sm2_fe two = {{2,0,0,0,0,0,0,0}};
    r->x = two; r->y = two; r->z = FE_ONE; r->inf = 1;
}

/* 掩码搬运 Jacobian 点（含 inf 标志）：m 全 1 时 dst = src */
static void jac_cmov(sm2_jac *dst, const sm2_jac *src, uint32_t m)
{
    cmov_fe(&dst->x, &src->x, m);
    cmov_fe(&dst->y, &src->y, m);
    cmov_fe(&dst->z, &src->z, m);
    dst->inf = (int)(((uint32_t)dst->inf & ~m) | ((uint32_t)src->inf & m));
}
#endif

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

/* -------------------- 固定结构 Solinas 约减（OPT） -------------------- */
#if SMC_SM2_KERNEL == SMC_KERNEL_OPT
/*
 * Solinas 折叠：2^256 ≡ 2^224 + 2^96 − 2^64 + 1 (mod p)
 * x_j (j>=8) -> 位置 j-8(+) j-5(+) j-6(-) j-1(+)。
 *
 * 从 j=15 向 j=8 递减处理时，所有新项都落在更低位置，因而会在同一趟
 * 后续迭代中继续折叠，不需要旧实现的多轮 17-limb 拷贝。折叠后的系数
 * 最大绝对值小于 2^36（16 个 32-bit limb 的极值组合验证），int64_t 足够。
 * 两次固定的 carry 回折收掉 2^256 位，最后结果 <2^256<2p，一次条件减即可。
 * 整条路径循环次数固定，也适用于 SMC_SM2_CT 构建。
 */
static void fe_reduce_solinas(uint32_t r[FE_WORDS], const uint32_t x[2 * FE_WORDS])
{
    int64_t a[2 * FE_WORDS + 1];
    int i, j, pass;

    for (i = 0; i < 2 * FE_WORDS; i++) a[i] = (int64_t)(uint64_t)x[i];
    a[2 * FE_WORDS] = 0;

    for (j = 2 * FE_WORDS - 1; j >= FE_WORDS; j--) {
        int64_t h = a[j];
        a[j] = 0;
        a[j - FE_WORDS] += h;
        a[j - 5]        += h;
        a[j - 6]        -= h;
        a[j - 1]        += h;
    }

    for (pass = 0; pass < 2; pass++) {
        int64_t h;
        for (i = 0; i < FE_WORDS; i++) {
            int64_t v = a[i];
            a[i] = (uint32_t)v;
            a[i + 1] += v >> 32; /* ARM GCC 对 signed 右移生成 ASR */
        }
        h = a[FE_WORDS];
        a[FE_WORDS] = 0;
        a[0] += h;
        a[3] += h;
        a[2] -= h;
        a[7] += h;
    }

    for (i = 0; i < FE_WORDS; i++) {
        int64_t v = a[i];
        r[i] = (uint32_t)v;
        a[i + 1] += v >> 32;
    }
    fe_mod_once(r, &FE_P);
}
#endif

/* -------------------- 乘法主体 -------------------- */
#if SMC_SM2_KERNEL == SMC_KERNEL_REF

void sm2_fp_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b)
{
    uint32_t x[2 * FE_WORDS];
    int i, j;
    SMC_OP_MUL();
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
    SMC_OP_MUL();

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

    fe_reduce_solinas(r->w, x);
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
    /* p 为无穷远或 y = 0（2-阶点）时的判据，先算成掩码 */
    uint32_t m_inf = (uint32_t)0 - (uint32_t)(p->inf | (int)sm2_fe_is_zero(&p->y));
    SMC_OP_DBL();
#if !SMC_PT_UNIFORM
    if (m_inf) { sm2_jac_set_inf(r); return; }
#endif
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
#if SMC_PT_UNIFORM
        jac_cmov(r, &JAC_INF, m_inf);      /* 与提前返回等价，但不分支 */
#endif
    }
}

/* add-2007-bl */
void sm2_jac_add(sm2_jac *r, const sm2_jac *p, const sm2_jac *q)
{
    sm2_fe z1z1, z2z2, u1, u2, s1, s2, h, i2, j2, rr, v;
    sm2_fe x3, y3, z3;
#if SMC_PT_UNIFORM
    uint32_t mp = (uint32_t)0 - (uint32_t)p->inf;
    uint32_t mq = (uint32_t)0 - (uint32_t)q->inf;
#endif
    SMC_OP_ADD();
#if !SMC_PT_UNIFORM
    if (p->inf) { *r = *q; return; }
    if (q->inf) { *r = *p; return; }
#endif

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

    /* h = 0 即 P = ±Q 的退化情形：CT 构建下同样保留这个提前返回。它不是
       「忽略」而是**不可触发**：需要累加点的 x 坐标恰好与目标点重合，
       等价于在标量乘路径上构造一个离散对数关系（或 2^-256 量级的巧合），
       而攻击者无法选择秘密标量。见 docs/04 §3.6「常量时间口径」。 */
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
        sm2_fe t1;
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
    }
    r->x = x3; r->y = y3; r->z = z3; r->inf = 0;
#if SMC_PT_UNIFORM
    /* P/Q 为无穷远时用掩码选择结果，不分支（原实现先判 p->inf 再判 q->inf，
       故 q 的掩码须排除 p 已命中的情形）。这是 CT 构建下**唯一常见**的
       数据相关分支——窗口值为 0 时被加点是无穷远，旧实现直接提前返回，
       使实际工作量随秘密变化（实测 3–7%，极端标量达 21×）。 */
    jac_cmov(r, q, mp);                /* P 无穷远 → Q */
    jac_cmov(r, p, mq & ~mp);          /* 否则 Q 无穷远 → P */
#endif
}

/* add-2007-bl mixed（Q 仿射） */
void sm2_jac_add_mixed(sm2_jac *r, const sm2_jac *p, const sm2_pt *q)
{
    sm2_fe z1z1, u2, s2, h, hh, i2, j2, rr, v;
#if SMC_PT_UNIFORM
    uint32_t mp = (uint32_t)0 - (uint32_t)p->inf;
#endif
    SMC_OP_ADD();
#if !SMC_PT_UNIFORM
    if (p->inf) { sm2_jac_from_affine(r, q); return; }
#endif

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
#if SMC_PT_UNIFORM
        {   /* P 无穷远 → 结果即 Q（掩码，不分支） */
            sm2_jac aq;
            sm2_jac_from_affine(&aq, q);
            jac_cmov(r, &aq, mp);
        }
#endif
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
#elif SMC_SM2_CT
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

#if !SMC_SM2_CT
/* ================= wNAF(w=4) 标量乘：平均加法密度 1/5 =================
 * 相比固定 4-bit 窗口（每 4 位 1 次加法 → 每 256 位约 63 次）：
 *   wNAF 平均每 5 位 1 次加法（约 51 次），且奇数倍表仅 4 点（384B Jacobian）。
 * 验签用交织双标量乘共享倍点循环，把两次独立 256 次倍点合并为一次。 */
#define WNAF_NT     4        /* 表：{1,3,5,7}·P */
#define WNAF_MAXLEN 262

static int w_is_zero9(const uint32_t *t)
{
    uint32_t acc = 0;
    int i;
    for (i = 0; i < 9; i++) acc |= t[i];
    return acc == 0;
}

static void w_add_small(uint32_t *t, uint32_t d)
{
    uint64_t c = d;
    int i;
    for (i = 0; i < 9; i++) {
        c += t[i];
        t[i] = (uint32_t)c;
        c >>= 32;
        if (!c) break;
    }
}

static void w_sub_small(uint32_t *t, uint32_t d)
{
    uint32_t borrow = d;
    int i;
    for (i = 0; i < 9 && borrow; i++) {
        uint32_t old = t[i];
        t[i] = old - borrow;
        borrow = (old < borrow) ? 1u : 0u;
    }
}

static void w_shr1(uint32_t *t)
{
    int i;
    for (i = 0; i < 8; i++) t[i] = (t[i] >> 1) | (t[i + 1] << 31);
    t[8] >>= 1;
}

/* k → wNAF（低位在前），digits ∈ {0, ±1, ±3, ±5, ±7} */
static int naf_w4(const sm2_fe *k, int8_t naf[WNAF_MAXLEN])
{
    uint32_t t[9];
    int len = 0, i;
    for (i = 0; i < 8; i++) t[i] = k->w[i];
    t[8] = 0;
    for (i = 0; i < WNAF_MAXLEN; i++) naf[i] = 0;   /* 保持零 libc 依赖 */
    while (!w_is_zero9(t)) {
        int d = 0;
        if (t[0] & 1u) {
            d = (int)(t[0] & 15u);
            if (d >= 8) d -= 16;                 /* 最近的有符号奇数 */
            if (d > 0) w_sub_small(t, (uint32_t)d);
            else       w_add_small(t, (uint32_t)(-d));
        }
        naf[len++] = (int8_t)d;
        w_shr1(t);
        if (len >= WNAF_MAXLEN) break;           /* 理论不可达，防御 */
    }
    return len;
}

static void jac_negate(sm2_jac *q)
{
    sm2_fe t;
    if (q->inf) return;
    sm2_fp_sub(&t, &FE_ZERO, &q->y);
    q->y = t;
}

/* 奇数倍表 tbl[j] = (2j+1)·P（Jacobian 链，无需求逆） */
static void build_odd_table(sm2_jac tbl[WNAF_NT], const sm2_pt *p)
{
    sm2_jac p2, acc, t;
    int j;
    sm2_jac_from_affine(&tbl[0], p);
    sm2_jac_double(&p2, &tbl[0]);                /* 2P */
    acc = tbl[0];
    for (j = 1; j < WNAF_NT; j++) {
        sm2_jac_add(&t, &acc, &p2);              /* (2j+1)P = (2j-1)P + 2P */
        acc = t;
        tbl[j] = acc;
    }
}

static void scalarmult_naf(sm2_jac *r, const sm2_fe *k, const sm2_pt *p)
{
    int8_t naf[WNAF_MAXLEN];
    sm2_jac tbl[WNAF_NT], acc, t;
    int len, i;

    len = naf_w4(k, naf);
    build_odd_table(tbl, p);
    sm2_jac_set_inf(&acc);
    for (i = len - 1; i >= 0; i--) {
        sm2_jac_double(&acc, &acc);
        if (naf[i]) {
            sm2_jac q = tbl[(naf[i] > 0 ? (int)naf[i] : -(int)naf[i]) / 2];
            if (naf[i] < 0) jac_negate(&q);
            sm2_jac_add(&t, &acc, &q);
            acc = t;
        }
    }
    *r = acc;
}

/* k1·P1 + k2·P2（交织共享倍点：验签主路径） */
static int scalarmult_add_naf(sm2_pt *rout, const sm2_fe *k1, const sm2_pt *p1,
                              const sm2_fe *k2, const sm2_pt *p2)
{
    int8_t n1[WNAF_MAXLEN], n2[WNAF_MAXLEN];
    sm2_jac t1[WNAF_NT], t2[WNAF_NT], acc, t;
    int l1, l2, L, i;

    l1 = naf_w4(k1, n1);
    l2 = naf_w4(k2, n2);
    L = (l1 > l2) ? l1 : l2;
    if (L == 0) return -1;
    build_odd_table(t1, p1);
    build_odd_table(t2, p2);
    sm2_jac_set_inf(&acc);
    for (i = L - 1; i >= 0; i--) {
        sm2_jac_double(&acc, &acc);
        if (n1[i]) {
            sm2_jac q = t1[(n1[i] > 0 ? (int)n1[i] : -(int)n1[i]) / 2];
            if (n1[i] < 0) jac_negate(&q);
            sm2_jac_add(&t, &acc, &q);
            acc = t;
        }
        if (n2[i]) {
            sm2_jac q = t2[(n2[i] > 0 ? (int)n2[i] : -(int)n2[i]) / 2];
            if (n2[i] < 0) jac_negate(&q);
            sm2_jac_add(&t, &acc, &q);
            acc = t;
        }
    }
    if (acc.inf) return -1;
    sm2_jac_to_affine(rout, &acc);
    return 0;
}

void sm2_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const sm2_pt *p)
{
    scalarmult_naf(r, k, p);
}

#else  /* SMC_SM2_CT：常量时间 4-bit 窗口 + 全表扫描 */

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
        {
            /* 常量时间：全表扫描 cmov 选表项（访存模式恒定），再做一次加法 */
            sm2_jac sel;
            uint32_t want = (uint32_t)(w ? (unsigned)(w - 1) : 0u);
            uint32_t any  = (uint32_t)(w != 0);
            jac_set_noop(&sel);          /* w = 0 时的占位（坐标异于无穷远表示） */
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
    }
    *r = acc;
}

#endif /* SMC_SM2_CT */

/* ================= 基点 comb 固定表（k·G 专用） =================
 * 表见 crypto/src/sm2_base_table.h（自动生成、只读常量、禁止手工编辑）。
 * k·G = Σ_{j=0}^{d-1} 2^j·T[K_j]：d−1 次倍点 + ≤d−1 次混合加（原 wNAF 约 257 次倍点）。
 * 两种选表方式（SMC_SM2_CT 决定编译哪一支）：
 *   非 CT：以 K_j 直接索引 T[K_j]（最快；属"秘密相关访存"，与 wNAF 同类别）
 *   CT   ：对全部 32 项做**扫描 + cmov**，且恒做一次加法后再用掩码决定是否采纳结果
 *          —— 访存模式与运算次数均与秘密无关，使 K_j=0 也不产生分支
 * 关键观察：扫描的代价是每窗口 32 项 × 2 域的 cmov（廉价字操作），
 *   远低于它省下的 206 次倍点 ⇒ **"用预计算表"与"常量时间"并不冲突**，
 *   实测 CT 的性能代价接近 0（见 docs/04 §3.6）。 */
#if SMC_SM2_USE_COMB_TABLE
#include "sm2_base_table.h"

#if !SMC_SM2_CT
/* ---- 非 CT：直接索引 32 项表 ---- */
static void scalarmult_base_comb(sm2_jac *r, const sm2_fe *k)
{
    sm2_jac acc, t;
    unsigned a;
    int j;

    a = comb_idx(k, SMC_SM2_DBLK - 1);
    if (a == 0) sm2_jac_set_inf(&acc);          /* K_j = 0：该项不贡献 */
    else        sm2_jac_from_affine(&acc, &SM2_COMB_TABLE[a]);

    for (j = SMC_SM2_DBLK - 2; j >= 0; j--) {
        sm2_jac_double(&t, &acc);
        acc = t;
        a = comb_idx(k, j);
        if (a == 0) continue;                   /* 占位项 [0] 不读取 */
        sm2_jac_add_mixed(&t, &acc, &SM2_COMB_TABLE[a]);
        acc = t;
    }
    *r = acc;
}

#else /* SMC_SM2_CT */
/* ---- CT：全表扫描 + cmov（访存模式与运算次数恒定）---- */

/* 全表扫描选出 T[K_j] 的仿射坐标，返回 nz = (K_j != 0)。
 * 地址序列与 K_j 无关：恒读 T[0..31]，且不做提前退出。 */
static uint32_t comb_scan_ct(sm2_pt *q, const sm2_fe *k, int j)
{
    unsigned a = comb_idx(k, j);
    uint32_t nz = (a | (0u - a)) >> 31;         /* a != 0 → 1 */
    sm2_pt sel;
    int i;
    sm2_fe two = {{2,0,0,0,0,0,0,0}};
    sel.x = two;
    sel.y = two;   /* 占位：K_j = 0 时结果被掩码丢弃；坐标异于无穷远表示 (1,1,1) */
    for (i = 0; i < (int)SMC_SM2_TENT; i++) {
        uint32_t m = (uint32_t)0 - (uint32_t)((unsigned)i == a);
        cmov_fe(&sel.x, &SM2_COMB_TABLE[i].x, m);
        cmov_fe(&sel.y, &SM2_COMB_TABLE[i].y, m);
    }
    *q = sel;
    return nz;
}

static void scalarmult_base_comb_ct(sm2_jac *r, const sm2_fe *k)
{
    sm2_jac acc, t;
    sm2_pt q;
    uint32_t nz;
    int j;

    /* 首项：acc = T[K_(d-1)]；K=0 时用掩码置无穷远（不分支） */
    nz = comb_scan_ct(&q, k, SMC_SM2_DBLK - 1);
    sm2_jac_from_affine(&acc, &q);
    acc.inf = (int)(1u - nz);

    for (j = SMC_SM2_DBLK - 2; j >= 0; j--) {
        sm2_jac_double(&t, &acc);
        acc = t;
        nz = comb_scan_ct(&q, k, j);
        sm2_jac_add_mixed(&t, &acc, &q);        /* 恒做一次加法 */
        jac_cmov(&acc, &t, (uint32_t)0 - nz);   /* K_j=0 时丢弃加法结果 */
    }
    *r = acc;
}
#endif /* SMC_SM2_CT */
#endif /* SMC_SM2_USE_COMB_TABLE */

#else
#error "bad SMC_SM2_KERNEL"
#endif

/* 基点乘（Jacobian 输出，不做求逆）：供需要把结果再相加的路径复用（如验签 sG+tP）。
 * 放在内核分支之外——REF 内核同样需要该入口（走通用标量乘） */
void sm2_scalarmult_base_jac(sm2_jac *r, const sm2_fe *k)
{
#if SMC_SM2_USE_COMB_TABLE
#if !SMC_SM2_CT
    scalarmult_base_comb(r, k);
#else
    scalarmult_base_comb_ct(r, k);
#endif
#else
    sm2_pt g;
    g.x = FE_GX; g.y = FE_GY;
    sm2_scalarmult_jac(r, k, &g);
#endif
}

void sm2_scalarmult(sm2_pt *r, const sm2_fe *k, const sm2_pt *p)
{
    sm2_jac j;
    sm2_scalarmult_jac(&j, k, p);
    sm2_jac_to_affine(r, &j);
}

void sm2_scalarmult_base(sm2_pt *r, const sm2_fe *k)
{
    sm2_jac j;
    sm2_scalarmult_base_jac(&j, k);
    sm2_jac_to_affine(r, &j);
}

int sm2_scalarmult_add(sm2_pt *r, const sm2_fe *k1, const sm2_pt *p1,
                       const sm2_fe *k2, const sm2_pt *p2)
{
#if SMC_SM2_KERNEL == SMC_KERNEL_OPT && !SMC_SM2_CT
    /* OPT 内核：交织双标量乘（共享倍点循环） */
    return scalarmult_add_naf(r, k1, p1, k2, p2);
#else
    sm2_jac j1, j2, s;
    sm2_scalarmult_jac(&j1, k1, p1);
    sm2_scalarmult_jac(&j2, k2, p2);
    sm2_jac_add(&s, &j1, &j2);
    if (s.inf) return -1;
    sm2_jac_to_affine(r, &s);
    return 0;
#endif
}

/* ================= 对端公钥预计算表（验签变基点 → 固定基点） =================
 * 理论依据（本项优化的立足点）：**验签的全部输入都是公开数据**——
 *   签名 (r,s)、消息摘要、公钥 P，以及由它们导出的 t=(r+s) mod n。
 *   因此"以 t 的秘密位索引 P 的表"不泄漏任何秘密，**无需常量时间**，
 *   与签名路径（k、d 保密 ⇒ 必须常量时间）形成明确的不对称。
 * 于是 R = sG + tP 的两条腿都能变成固定基点乘：
 *   sG 用静态 comb 表（sm2_scalarmult_base_jac），tP 用本表。
 * 表布局（域元素下标，共 4·TENT）：
 *   [0, 2T)   仿射表项 (x,y)  —— 构建后有效数据
 *   [2T, 3T)  z 坐标          —— 仅构建期使用
 *   [3T, 4T)  批量求逆前缀     —— 仅构建期使用
 * 构建代价 ≈ 260 次倍点 + 31 次加法 + 1 次求逆 ≈ 0.7 次普通验签 ⇒ 2 次验签后净赚。 */
#if SMC_SM2_PEER_TABLE

#define PTBL_X(t, a)   ((t) + 2 * (a))
#define PTBL_Y(t, a)   ((t) + 2 * (a) + 1)
#define PTBL_Z(t, a)   ((t) + 2 * (int)SMC_SM2_TENT + (a))
#define PTBL_PRE(t, a) ((t) + 3 * (int)SMC_SM2_TENT + (a))

static sm2_jac ptbl_get(const sm2_fe *t, int a)
{
    sm2_jac j;
    j.x = *PTBL_X(t, a);
    j.y = *PTBL_Y(t, a);
    j.z = *PTBL_Z(t, a);
    j.inf = 0;
    return j;
}

static void ptbl_put(sm2_fe *t, int a, const sm2_jac *j)
{
    *PTBL_X(t, a) = j->x;
    *PTBL_Y(t, a) = j->y;
    *PTBL_Z(t, a) = j->z;
}

size_t sm2_peer_table_bytes(void) { return (size_t)SMC_SM2_PEER_TABLE_BYTES; }

int sm2_peer_table_build(void *tbl, const uint8_t pub[SMC_SM2_PUBKEY])
{
    sm2_fe *t = (sm2_fe *)tbl;
    sm2_jac B[SMC_SM2_TBITS];
    sm2_pt P;
    int i, alpha;

    if (!tbl) return -1;
    if (sm2_parse_pubkey(&P, pub) != 0) return -1;   /* 非法曲线点拒绝 */

    /* B_i = 2^(i·d)·P：反复倍点直接构造（与生成器同构，不经标量乘） */
    sm2_jac_from_affine(&B[0], &P);
    for (i = 1; i < (int)SMC_SM2_TBITS; i++) {
        sm2_jac c = B[i - 1];
        int b;
        for (b = 0; b < (int)SMC_SM2_DBLK; b++) {
            sm2_jac d;
            sm2_jac_double(&d, &c);
            c = d;
        }
        B[i] = c;
    }

    /* T[α] = Σ α_i·B_i（Jacobian 链，免求逆）；α=0 为占位、不参与 */
    for (alpha = 1; alpha < (int)SMC_SM2_TENT; alpha++) {
        int low = 0;
        int rest;
        while (((alpha >> low) & 1) == 0) low++;
        rest = alpha & (alpha - 1);
        if (rest == 0) {
            ptbl_put(t, alpha, &B[low]);
        } else {
            sm2_jac acc = ptbl_get(t, rest), s;
            sm2_jac_add(&s, &acc, &B[low]);
            ptbl_put(t, alpha, &s);
        }
    }

    /* 一次 Montgomery 批量求逆把 31 项转仿射（替代 31 次单独求逆） */
    {
        sm2_fe inv;
        int a;
        *PTBL_PRE(t, 1) = *PTBL_Z(t, 1);
        for (a = 2; a < (int)SMC_SM2_TENT; a++) {
            sm2_fp_mul(PTBL_PRE(t, a), PTBL_PRE(t, a - 1), PTBL_Z(t, a));
        }
        sm2_fp_inv(&inv, PTBL_PRE(t, (int)SMC_SM2_TENT - 1));
        for (a = (int)SMC_SM2_TENT - 1; a >= 1; a--) {
            sm2_fe zi, z2, z3, x, y;
            if (a == 1) {
                zi = inv;
            } else {
                sm2_fp_mul(&zi, &inv, PTBL_PRE(t, a - 1));
                sm2_fp_mul(&inv, &inv, PTBL_Z(t, a));
            }
            sm2_fp_sqr(&z2, &zi);
            sm2_fp_mul(&z3, &z2, &zi);
            sm2_fp_mul(&x, PTBL_X(t, a), &z2);
            sm2_fp_mul(&y, PTBL_Y(t, a), &z3);
            *PTBL_X(t, a) = x;
            *PTBL_Y(t, a) = y;
        }
    }
    return 0;
}

void sm2_peer_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const void *tbl)
{
    const sm2_fe *t = (const sm2_fe *)tbl;
    sm2_jac acc, tmp;
    unsigned a;
    int j;

    /* 与静态 comb 表同构，但**不做常量时间处理**：验签标量与公钥均为公开数据
       （见本段开头的不对称性论证），直接索引既快又无泄漏面。 */
    a = comb_idx(k, SMC_SM2_DBLK - 1);
    if (a == 0) {
        sm2_jac_set_inf(&acc);
    } else {
        sm2_pt p;
        p.x = *PTBL_X(t, (int)a);
        p.y = *PTBL_Y(t, (int)a);
        sm2_jac_from_affine(&acc, &p);
    }
    for (j = SMC_SM2_DBLK - 2; j >= 0; j--) {
        sm2_jac_double(&tmp, &acc);
        acc = tmp;
        a = comb_idx(k, j);
        if (a == 0) continue;
        {
            sm2_pt p;
            p.x = *PTBL_X(t, (int)a);
            p.y = *PTBL_Y(t, (int)a);
            sm2_jac_add_mixed(&tmp, &acc, &p);
            acc = tmp;
        }
    }
    *r = acc;
}

#endif /* SMC_SM2_PEER_TABLE */
