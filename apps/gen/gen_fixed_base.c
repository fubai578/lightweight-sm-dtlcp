/*
 * gen_fixed_base.c — 生成 SM2 基点 comb 预计算表（crypto/src/sm2_base_table.h）
 *
 * 表定义（Lim-Lee comb）：
 *     t = 表索引位宽，d = ceil(256/t)，平衡量 K_j = Σ_i bit(k, j+i·d)·2^i
 *     T[α] = Σ_{i=0}^{t-1} α_i · 2^{i·d} · G        （α ∈ [0, 2^t)）
 *     k·G  = Σ_{j=0}^{d-1} 2^j · T[K_j]             （Horner：d−1 次倍点 + ≤d−1 次混合加）
 *
 * 与 wNAF（约 257 次倍点 + 51 次加法）相比，comb 把倍点降到 d−1 ≈ 51 次。
 *
 * 生成方式：B_i = 2^{i·d}·G 由**反复倍点**独立算出（不经过标量乘），
 * T[α] 由 Jacobian 链累加后经一次**批量求逆**转仿射——避免 2^t 次单独求逆。
 * 生成后自检：① 每个表项在曲线上；② 随机标量下 comb 组合结果 == REF 二进制标量乘。
 *
 * 构建（链接 REF 内核对象，见 scripts/gen_fixed_base.sh）：
 *   gcc ... -DSMC_SM2_KERNEL=1 apps/gen/gen_fixed_base.c <ref objs> -o bin/gen_fixed_base
 */
#include <stdio.h>
#include <stdint.h>
#include "sm2_internal.h"

#ifndef GEN_TBITS
#define GEN_TBITS 5                       /* 表 2^5 = 32 项，仿射 2KB（只读，占 Flash） */
#endif
#define TENT  (1u << GEN_TBITS)
#define DBLK  ((256 + GEN_TBITS - 1) / GEN_TBITS)

static const sm2_fe FE_GX_C = {{0x334C74C7u, 0x715A4589u, 0xF2660BE1u, 0x8FE30BBFu,
                                0x6A39C994u, 0x5F990446u, 0x1F198119u, 0x32C4AE2Cu}};
static const sm2_fe FE_GY_C = {{0x2139F0A0u, 0x02DF32E5u, 0xC62A4740u, 0xD0A9877Cu,
                                0x6B692153u, 0x59BDCEE3u, 0xF4F6779Cu, 0xBC3736A2u}};

static sm2_jac B[GEN_TBITS];
static sm2_jac Tj[TENT];
static sm2_pt  Ta[TENT];

/* 标量第 b 位（b ≥ 256 视为 0） */
static int kbit(const sm2_fe *k, int b)
{
    if (b >= 256) return 0;
    return (int)((k->w[b >> 5] >> (b & 31)) & 1u);
}

/* 第 j 个平衡量 K_j（0 表示该项不参与，须跳过） */
static unsigned comb_idx(const sm2_fe *k, int j)
{
    unsigned a = 0;
    int i;
    for (i = 0; i < GEN_TBITS; i++) a |= (unsigned)kbit(k, j + i * DBLK) << i;
    return a;
}

/* 消费者侧算法（与运行时代码同构）：Σ 2^j·T[K_j] */
static void comb_mul(sm2_jac *r, const sm2_fe *k)
{
    sm2_jac acc;
    unsigned a;
    int j;

    a = comb_idx(k, DBLK - 1);
    if (a == 0) sm2_jac_set_inf(&acc);
    else        sm2_jac_from_affine(&acc, &Ta[a]);

    for (j = DBLK - 2; j >= 0; j--) {
        sm2_jac t;
        sm2_jac_double(&t, &acc);
        acc = t;
        a = comb_idx(k, j);
        if (a == 0) continue;                     /* K_j = 0：跳过该项 */
        sm2_jac_add_mixed(&t, &acc, &Ta[a]);
        acc = t;
    }
    *r = acc;
}

static void emit_fe(const sm2_fe *f)
{
    printf("{{0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu}}",
           f->w[0], f->w[1], f->w[2], f->w[3], f->w[4], f->w[5], f->w[6], f->w[7]);
}

int main(void)
{
    sm2_fe cur;
    sm2_pt g;
    int i, alpha;
    int selfcheck_ok = 1;

    /* ---- 1. B_i = 2^{i·d}·G（反复倍点，独立的构造路径） ---- */
    g.x = FE_GX_C; g.y = FE_GY_C;
    if (!sm2_pt_on_curve(&g)) { fprintf(stderr, "G 不在曲线上\n"); return 1; }
    sm2_jac_from_affine(&B[0], &g);
    for (i = 1; i < GEN_TBITS; i++) {
        sm2_jac t = B[i - 1];
        int c;
        for (c = 0; c < DBLK; c++) {
            sm2_jac d;
            sm2_jac_double(&d, &t);
            t = d;
        }
        B[i] = t;
    }

    /* ---- 2. T[α] = Σ α_i·B_i（Jacobian 链；α=0 为占位项） ---- */
    Tj[0] = B[0];                                  /* 占位：α=0 不参与运算 */
    for (alpha = 1; alpha < (int)TENT; alpha++) {
        int low = 0;
        int rest;
        while (((alpha >> low) & 1) == 0) low++;
        rest = alpha & (alpha - 1);
        if (rest == 0) {
            Tj[alpha] = B[low];
        } else {
            sm2_jac t;
            sm2_jac_add(&t, &Tj[rest], &B[low]);
            Tj[alpha] = t;
        }
    }

    /* ---- 3. 批量求逆（Montgomery trick）：31 项求逆 → 1 次 ---- */
    {
        sm2_fe pre[TENT], zinv[TENT], inv;
        pre[1] = Tj[1].z;
        for (alpha = 2; alpha < (int)TENT; alpha++) {
            sm2_fp_mul(&pre[alpha], &pre[alpha - 1], &Tj[alpha].z);
        }
        sm2_fp_inv(&inv, &pre[TENT - 1]);
        for (alpha = (int)TENT - 1; alpha >= 1; alpha--) {
            if (alpha == 1) {
                zinv[alpha] = inv;
            } else {
                sm2_fp_mul(&zinv[alpha], &inv, &pre[alpha - 1]);
                sm2_fp_mul(&inv, &inv, &Tj[alpha].z);
            }
        }
        Ta[0] = g;                                 /* 占位 */
        for (alpha = 1; alpha < (int)TENT; alpha++) {
            sm2_fe z2, z3;
            if (sm2_fe_is_zero(&Tj[alpha].z)) {
                fprintf(stderr, "自检失败：T[%d].z = 0（批量求逆未定义）\n", alpha);
                return 1;
            }
            sm2_fp_sqr(&z2, &zinv[alpha]);
            sm2_fp_mul(&z3, &z2, &zinv[alpha]);
            sm2_fp_mul(&Ta[alpha].x, &Tj[alpha].x, &z2);
            sm2_fp_mul(&Ta[alpha].y, &Tj[alpha].y, &z3);
        }
    }

    /* ---- 4. 自检 ---- */
    for (alpha = 1; alpha < (int)TENT; alpha++) {
        if (!sm2_pt_on_curve(&Ta[alpha])) {
            fprintf(stderr, "自检失败：T[%d] 不在曲线上\n", alpha);
            selfcheck_ok = 0;
        }
    }
    {
        uint32_t st = 0x1234ABCDu;
        int trial;
        for (trial = 0; trial < 16 && selfcheck_ok; trial++) {
            sm2_jac j1;
            sm2_pt j1a, j2a;
            int b;
            for (b = 0; b < 8; b++) {
                st = st * 1103515245u + 12345u;
                cur.w[b] = st ^ (st << 13);
            }
            cur.w[7] &= 0x7FFFFFFFu;                /* 保证 < 2^255 < n */
            if (sm2_fe_is_zero(&cur)) { trial--; continue; }
            comb_mul(&j1, &cur);
            if (j1.inf) { fprintf(stderr, "自检失败：comb 结果为无穷远\n"); selfcheck_ok = 0; break; }
            sm2_jac_to_affine(&j1a, &j1);
            sm2_scalarmult(&j2a, &cur, &g);         /* REF 二进制倍点-点加（独立路径） */
            if (sm2_fe_cmp(&j1a.x, &j2a.x) != 0 || sm2_fe_cmp(&j1a.y, &j2a.y) != 0) {
                fprintf(stderr, "自检失败：第 %d 个随机标量 comb != 二进制标量乘\n", trial);
                selfcheck_ok = 0;
            }
        }
    }
    if (!selfcheck_ok) return 1;

    /* ---- 5. 输出 C 源 ---- */
    printf("/*\n");
    printf(" * sm2_base_table.h — SM2 基点 comb 预计算表【自动生成，请勿手工编辑】\n");
    printf(" *\n");
    printf(" * 生成器：apps/gen/gen_fixed_base.c（GEN_TBITS=%d → 2^%d 项，DBLK=%d）\n",
           GEN_TBITS, GEN_TBITS, DBLK);
    printf(" * 重新生成：bash scripts/gen_fixed_base.sh\n");
    printf(" *\n");
    printf(" * T[α] = Σ_{i=0}^{%d} α_i·2^(%d·i)·G，α ∈ [0,%u)；\n", GEN_TBITS - 1, DBLK, TENT);
    printf(" * k·G  = Σ_{j=0}^{%d} 2^j·T[K_j]，K_j 由 k 的第 j+i·%d 位组成（i=0..%d）。\n",
           DBLK - 1, DBLK, GEN_TBITS - 1);
    printf(" *\n");
    printf(" * 索引 0 为占位项（K_j = 0 时跳过，运行时不读取）。\n");
    printf(" * 表为只读常量：占 Flash %u B，静态 RAM 增量 0。\n", TENT * 64u);
    printf(" * 生成时经 1 次批量求逆转仿射（替代 %u 次单独求逆）。\n", TENT - 1);
    printf(" */\n");
    printf("#ifndef SMC_SM2_BASE_TABLE_H\n#define SMC_SM2_BASE_TABLE_H\n\n");
    printf("#include \"smc/smc_config.h\"\n\n");
    printf("/* 表参数以 smc_config.h 为唯一来源；本处只做一致性校验，避免代码与表错配 */\n");
    printf("#if SMC_SM2_TBITS != %d || SMC_SM2_TENT != %uu || SMC_SM2_DBLK != %d\n",
           GEN_TBITS, TENT, DBLK);
    printf("#error \"基点表参数与 smc_config.h 不一致：请同步 GEN_TBITS 或重新生成表\"\n");
    printf("#endif\n\n");
    printf("static const sm2_pt SM2_COMB_TABLE[SMC_SM2_TENT] = {\n");
    for (alpha = 0; alpha < (int)TENT; alpha++) {
        printf("    /* [%2d]%s */\n    { ", alpha,
               alpha == 0 ? " 占位（不读取）" : "");
        emit_fe(&Ta[alpha].x);
        printf(",\n      ");
        emit_fe(&Ta[alpha].y);
        printf(" },\n");
    }
    printf("};\n\n#endif /* SMC_SM2_BASE_TABLE_H */\n");
    fprintf(stderr, "已生成 %u 项表；自检通过：全部在曲线上 + 16 个随机标量与 REF 二进制标量乘一致。\n",
            TENT);
    return 0;
}
