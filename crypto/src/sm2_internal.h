/*
 * sm2_internal.h — SM2 曲线算术内部接口（域/标量/点）
 * 曲线参数：GB/T 32918.5 推荐曲线  y² = x³ + ax + b  over Fp
 */
#ifndef SMC_SM2_INTERNAL_H
#define SMC_SM2_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include "smc/smc_config.h"

#define FE_WORDS 8

typedef struct { uint32_t w[FE_WORDS]; } sm2_fe;        /* 256-bit 小端 limb */
typedef struct { sm2_fe x, y; }          sm2_pt;        /* 仿射点 */
typedef struct { sm2_fe x, y, z; int inf; } sm2_jac;    /* Jacobian 射影点 */

/* ---- 通用 ---- */
void sm2_fe_copy(sm2_fe *r, const sm2_fe *a);
int  sm2_fe_cmp(const sm2_fe *a, const sm2_fe *b);       /* -1/0/1 */
int  sm2_fe_is_zero(const sm2_fe *a);
void sm2_fe_from_bytes(sm2_fe *r, const uint8_t in[32]); /* 大端 */
void sm2_fe_to_bytes(uint8_t out[32], const sm2_fe *a);  /* 大端 */
void sm2_fe_set_zero(sm2_fe *r);
void sm2_fe_set_one(sm2_fe *r);

/* ---- Fp 算术（mod p）---- */
void sm2_fp_add(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);
void sm2_fp_sub(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);
void sm2_fp_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);
void sm2_fp_sqr(sm2_fe *r, const sm2_fe *a);
void sm2_fp_inv(sm2_fe *r, const sm2_fe *a);
void sm2_fp_reduce(sm2_fe *r);                            /* r ∈[0,2^256) → mod p */

/* ---- Fn 算术（mod n）---- */
void sm2_fn_add(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);  /* 输入需 < n */
void sm2_fn_sub(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);
void sm2_fn_mul(sm2_fe *r, const sm2_fe *a, const sm2_fe *b);
void sm2_fn_inv(sm2_fe *r, const sm2_fe *a);
void sm2_fn_neg(sm2_fe *r, const sm2_fe *a);
void sm2_fn_mod(sm2_fe *r, const sm2_fe *a);               /* 任意 256 位 → mod n */
int  sm2_fe_lt_n(const sm2_fe *a);
int  sm2_fn_in_range(const sm2_fe *a);                     /* 1 <= a <= n-1 */

/* ---- 曲线参数字节常量（ZA 计算用）---- */
extern const uint8_t SM2_PARAM_P[32], SM2_PARAM_A[32], SM2_PARAM_B[32];
extern const uint8_t SM2_PARAM_GX[32], SM2_PARAM_GY[32], SM2_PARAM_N[32];

/* ---- 点运算 ---- */
void sm2_jac_set_inf(sm2_jac *r);
void sm2_jac_from_affine(sm2_jac *r, const sm2_pt *p);
void sm2_jac_to_affine(sm2_pt *r, const sm2_jac *p);
void sm2_jac_double(sm2_jac *r, const sm2_jac *p);
void sm2_jac_add(sm2_jac *r, const sm2_jac *p, const sm2_jac *q);
void sm2_jac_add_mixed(sm2_jac *r, const sm2_jac *p, const sm2_pt *q);
int  sm2_pt_on_curve(const sm2_pt *p);
int  sm2_parse_pubkey(sm2_pt *p, const uint8_t pub[SMC_SM2_PUBKEY]);
void sm2_encode_pubkey(uint8_t pub[SMC_SM2_PUBKEY], const sm2_pt *p);

/* ---- 标量乘 ---- */
void sm2_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const sm2_pt *p);
void sm2_scalarmult(sm2_pt *r, const sm2_fe *k, const sm2_pt *p);
void sm2_scalarmult_base(sm2_pt *r, const sm2_fe *k);
/* 基点乘的 Jacobian 输出（不求逆）：供需要把结果再相加的路径复用（如验签 sG+tP） */
void sm2_scalarmult_base_jac(sm2_jac *r, const sm2_fe *k);
/* r = k1·p1 + k2·p2；无穷时返回 -1 */
int  sm2_scalarmult_add(sm2_pt *r, const sm2_fe *k1, const sm2_pt *p1,
                        const sm2_fe *k2, const sm2_pt *p2);

/* ---- 对端公钥预计算表（验签变基点 → 固定基点；输入全为公开数据，无侧信道顾虑） ---- */
#if SMC_SM2_PEER_TABLE
/* 表占用 4·TENT 个域元素：前 2·TENT 为仿射表项（构建后有效数据 2048 B），
   其余为构建期暂存（z 坐标 + 批量求逆前缀）。总大小见下 */
size_t sm2_peer_table_bytes(void);
/* 用对端公钥构建 comb 表；非法曲线点返回负值。
   tbl 需 ≥4 字节对齐、容量 ≥ sm2_peer_table_bytes()。 */
int  sm2_peer_table_build(void *tbl, const uint8_t pub[SMC_SM2_PUBKEY]);
/* r = k·P（P 为建表时的对端公钥）的 Jacobian 输出；表不匹配时 fail-closed（验签失败） */
void sm2_peer_scalarmult_jac(sm2_jac *r, const sm2_fe *k, const void *tbl);
#endif

#endif /* SMC_SM2_INTERNAL_H */
