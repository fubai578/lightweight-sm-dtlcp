/*
 * sm4.c — SM4 分组密码（GM/T 0002-2012）
 *
 * 三档内核（SMC_SM4_KERNEL）：
 *   REF   : 严格按标准定义的 τ 与 L 分步实现，无查找表（性能基线）
 *   FAST  : T-table 合并 L∘τ（4 张 1KB 表）+ 轮函数紧凑化（默认，最快）
 *   SMALL : 仅 256B S-box，L 用移位异或展开，无 T 表（最小常量表）
 *
 * 指令级/尺寸优化点（相对 REF）：
 *   1) CK 常量按定义 (7*(4i+b)) mod 256 运行时生成 —— 省 128B 常量表；
 *   2) FAST 的 T 表合并线性变换与 S 盒（每轮 4 次访存替代 16 次位运算+查表）；
 *   3) OPT 解密 = 轮密钥逆序复用加密核（解密代码体积归零）；
 *   4) 模式层 CTR 免 padding、CBC 链式异或就地化。
 */
#include "smc/sm4.h"

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

/* SM4 S 盒（GM/T 0002-2012 附录 A，双射校验见 tests） */
static const uint8_t SM4_SBOX[256] = {
    0xd6,0x90,0xe9,0xfe,0xcc,0xe1,0x3d,0xb7,0x16,0xb6,0x14,0xc2,0x28,0xfb,0x2c,0x05,
    0x2b,0x67,0x9a,0x76,0x2a,0xbe,0x04,0xc3,0xaa,0x44,0x13,0x26,0x49,0x86,0x06,0x99,
    0x9c,0x42,0x50,0xf4,0x91,0xef,0x98,0x7a,0x33,0x54,0x0b,0x43,0xed,0xcf,0xac,0x62,
    0xe4,0xb3,0x1c,0xa9,0xc9,0x08,0xe8,0x95,0x80,0xdf,0x94,0xfa,0x75,0x8f,0x3f,0xa6,
    0x47,0x07,0xa7,0xfc,0xf3,0x73,0x17,0xba,0x83,0x59,0x3c,0x19,0xe6,0x85,0x4f,0xa8,
    0x68,0x6b,0x81,0xb2,0x71,0x64,0xda,0x8b,0xf8,0xeb,0x0f,0x4b,0x70,0x56,0x9d,0x35,
    0x1e,0x24,0x0e,0x5e,0x63,0x58,0xd1,0xa2,0x25,0x22,0x7c,0x3b,0x01,0x21,0x78,0x87,
    0xd4,0x00,0x46,0x57,0x9f,0xd3,0x27,0x52,0x4c,0x36,0x02,0xe7,0xa0,0xc4,0xc8,0x9e,
    0xea,0xbf,0x8a,0xd2,0x40,0xc7,0x38,0xb5,0xa3,0xf7,0xf2,0xce,0xf9,0x61,0x15,0xa1,
    0xe0,0xae,0x5d,0xa4,0x9b,0x34,0x1a,0x55,0xad,0x93,0x32,0x30,0xf5,0x8c,0xb1,0xe3,
    0x1d,0xf6,0xe2,0x2e,0x82,0x66,0xca,0x60,0xc0,0x29,0x23,0xab,0x0d,0x53,0x4e,0x6f,
    0xd5,0xdb,0x37,0x45,0xde,0xfd,0x8e,0x2f,0x03,0xff,0x6a,0x72,0x6d,0x6c,0x5b,0x51,
    0x8d,0x1b,0xaf,0x92,0xbb,0xdd,0xbc,0x7f,0x11,0xd9,0x5c,0x41,0x1f,0x10,0x5a,0xd8,
    0x0a,0xc1,0x31,0x88,0xa5,0xcd,0x7b,0xbd,0x2d,0x74,0xd0,0x12,0xb8,0xe5,0xb4,0xb0,
    0x89,0x69,0x97,0x4a,0x0c,0x96,0x77,0x7e,0x65,0xb9,0xf1,0x09,0xc5,0x6e,0xc6,0x84,
    0x18,0xf0,0x7d,0xec,0x3a,0xdc,0x4d,0x20,0x79,0xee,0x5f,0x3e,0xd7,0xcb,0x39,0x48
};

#define SM4_FK0 0xA3B1BAC6u
#define SM4_FK1 0x56AA3350u
#define SM4_FK2 0x677D9197u
#define SM4_FK3 0xB27022DCu

/* CK_i 按定义生成：字节 b = (7*(4i+b)) mod 256 —— 代替 128B 静态常量表 */
static uint32_t sm4_ck(int i)
{
    uint32_t v = 0;
    int b;
    for (b = 0; b < 4; b++) {
        v |= (uint32_t)((7 * (4 * i + b)) & 0xff) << (24 - 8 * b);
    }
    return v;
}

static uint32_t load_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

/* ---------------------------- 基本变换 ---------------------------- */
/* τ：并行 S 盒置换（REF/SMALL 共用） */
static uint32_t sm4_tau(uint32_t t)
{
    return ((uint32_t)SM4_SBOX[(t >> 24) & 0xff] << 24) |
           ((uint32_t)SM4_SBOX[(t >> 16) & 0xff] << 16) |
           ((uint32_t)SM4_SBOX[(t >>  8) & 0xff] <<  8) |
           ((uint32_t)SM4_SBOX[ t        & 0xff]);
}

/* L：加密轮线性变换 */
static uint32_t sm4_L(uint32_t b)
{
    return b ^ ROTL(b, 2) ^ ROTL(b, 10) ^ ROTL(b, 18) ^ ROTL(b, 24);
}

/* L'：密钥扩展线性变换 */
static uint32_t sm4_Lp(uint32_t b)
{
    return b ^ ROTL(b, 13) ^ ROTL(b, 23);
}

/* ---------------------------- 内核选择 ---------------------------- */
#if SMC_SM4_KERNEL == SMC_KERNEL_FAST

/* T-table：Te_i[x] = L(τ(第 i 字节为 x))，利用 L 的字节旋转交换性预计算 */
static uint32_t sm4_te0[256], sm4_te1[256], sm4_te2[256], sm4_te3[256];
static int sm4_te_ready;

static void sm4_te_init(void)
{
    int x;
    if (sm4_te_ready) return;
    for (x = 0; x < 256; x++) {
        uint32_t t = sm4_L((uint32_t)SM4_SBOX[x] << 24);
        /* S[x]<<16 = ROTL(S[x]<<24, 24)，故 te1 = ROTL(te0,24)；te3 同理反向 */
        sm4_te0[x] = t;
        sm4_te1[x] = ROTL(t, 24);
        sm4_te2[x] = ROTL(t, 16);
        sm4_te3[x] = ROTL(t, 8);
    }
    sm4_te_ready = 1;
}

static void sm4_crypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint32_t x0, x1, x2, x3, x4, t;
    const uint32_t *rk = ctx->rk;
    int i;

    x0 = load_be32(in);      x1 = load_be32(in + 4);
    x2 = load_be32(in + 8);  x3 = load_be32(in + 12);

    /* 每轮：t = x1^x2^x3^rk；x4 = x0 ^ T(t)，T 按 t 的 4 字节查表合并 L∘τ */
#define SM4_ROUND_T(rkv)                                        \
    t = x1 ^ x2 ^ x3 ^ (rkv);                                   \
    x4 = x0 ^ sm4_te0[t >> 24] ^ sm4_te1[(t >> 16) & 0xff] ^    \
              sm4_te2[(t >>  8) & 0xff] ^ sm4_te3[t & 0xff];    \
    x0 = x1; x1 = x2; x2 = x3; x3 = x4

    for (i = 0; i < 32; i += 4) {
        SM4_ROUND_T(rk[0]);
        SM4_ROUND_T(rk[1]);
        SM4_ROUND_T(rk[2]);
        SM4_ROUND_T(rk[3]);
        rk += 4;
    }
#undef SM4_ROUND_T

    store_be32(out,      x3);
    store_be32(out + 4,  x2);
    store_be32(out + 8,  x1);
    store_be32(out + 12, x0);
}

void sm4_encrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    sm4_te_init();
    sm4_crypt_block(ctx, in, out);
}

void sm4_decrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    /* 轮密钥已在 setkey_dec 中逆序，直接复用加密核（解密代码体积≈0） */
    sm4_te_init();
    sm4_crypt_block(ctx, in, out);
}

void sm4_setkey_enc(sm4_ctx *ctx, const uint8_t key[16])
{
    uint32_t k[4], t;
    int i;
    k[0] = load_be32(key)      ^ SM4_FK0;
    k[1] = load_be32(key + 4)  ^ SM4_FK1;
    k[2] = load_be32(key + 8)  ^ SM4_FK2;
    k[3] = load_be32(key + 12) ^ SM4_FK3;
    for (i = 0; i < 32; i++) {
        t = k[0] ^ sm4_Lp(sm4_tau(k[1] ^ k[2] ^ k[3] ^ sm4_ck(i)));
        ctx->rk[i] = t;
        k[0] = k[1]; k[1] = k[2]; k[2] = k[3]; k[3] = t;
    }
}

void sm4_setkey_dec(sm4_ctx *ctx, const uint8_t key[16])
{
    sm4_ctx tmp;
    int i;
    sm4_setkey_enc(&tmp, key);
    for (i = 0; i < 32; i++) ctx->rk[i] = tmp.rk[31 - i];
}

#elif SMC_SM4_KERNEL == SMC_KERNEL_SMALL

void sm4_encrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint32_t x0, x1, x2, x3, t, b;
    const uint32_t *rk = ctx->rk;
    int i;

    x0 = load_be32(in);      x1 = load_be32(in + 4);
    x2 = load_be32(in + 8);  x3 = load_be32(in + 12);

    for (i = 0; i < 32; i++) {
        t = x1 ^ x2 ^ x3 ^ rk[i];
        b = sm4_L(sm4_tau(t));                /* 4 次字节查表，无 T 表 */
        t = x0 ^ b;
        x0 = x1; x1 = x2; x2 = x3; x3 = t;
    }
    store_be32(out,      x3);
    store_be32(out + 4,  x2);
    store_be32(out + 8,  x1);
    store_be32(out + 12, x0);
}

void sm4_decrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    sm4_ctx rev;
    int i;
    for (i = 0; i < 32; i++) rev.rk[i] = ctx->rk[31 - i];
    sm4_encrypt_block(&rev, in, out);
}

void sm4_setkey_enc(sm4_ctx *ctx, const uint8_t key[16])
{
    uint32_t k[4], t;
    int i;
    k[0] = load_be32(key)      ^ SM4_FK0;
    k[1] = load_be32(key + 4)  ^ SM4_FK1;
    k[2] = load_be32(key + 8)  ^ SM4_FK2;
    k[3] = load_be32(key + 12) ^ SM4_FK3;
    for (i = 0; i < 32; i++) {
        t = k[0] ^ sm4_Lp(sm4_tau(k[1] ^ k[2] ^ k[3] ^ sm4_ck(i)));
        ctx->rk[i] = t;
        k[0] = k[1]; k[1] = k[2]; k[2] = k[3]; k[3] = t;
    }
}

void sm4_setkey_dec(sm4_ctx *ctx, const uint8_t key[16])
{
    sm4_setkey_enc(ctx, key);   /* SMALL 的解密核内部自行逆序 */
}

#elif SMC_SM4_KERNEL == SMC_KERNEL_REF

/* REF：严格按标准分步（τ 与 L 独立、32 轮显式循环、解密用逆变换） */
void sm4_encrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint32_t x[36];
    int i;
    for (i = 0; i < 4; i++) x[i] = load_be32(in + 4 * i);
    for (i = 0; i < 32; i++) {
        uint32_t t = x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ ctx->rk[i];
        x[i + 4] = x[i] ^ sm4_L(sm4_tau(t));
    }
    store_be32(out,      x[35]);
    store_be32(out + 4,  x[34]);
    store_be32(out + 8,  x[33]);
    store_be32(out + 12, x[32]);
}

void sm4_decrypt_block(const sm4_ctx *ctx, const uint8_t in[16], uint8_t out[16])
{
    uint32_t x[36];
    int i;
    for (i = 0; i < 4; i++) x[i] = load_be32(in + 4 * i);
    for (i = 0; i < 32; i++) {
        uint32_t t = x[i + 1] ^ x[i + 2] ^ x[i + 3] ^ ctx->rk[31 - i];
        x[i + 4] = x[i] ^ sm4_L(sm4_tau(t));
    }
    store_be32(out,      x[35]);
    store_be32(out + 4,  x[34]);
    store_be32(out + 8,  x[33]);
    store_be32(out + 12, x[32]);
}

void sm4_setkey_enc(sm4_ctx *ctx, const uint8_t key[16])
{
    uint32_t k[36];
    int i;
    k[0] = load_be32(key)      ^ SM4_FK0;
    k[1] = load_be32(key + 4)  ^ SM4_FK1;
    k[2] = load_be32(key + 8)  ^ SM4_FK2;
    k[3] = load_be32(key + 12) ^ SM4_FK3;
    for (i = 0; i < 32; i++) {
        k[i + 4] = k[i] ^ sm4_Lp(sm4_tau(k[i + 1] ^ k[i + 2] ^ k[i + 3] ^ sm4_ck(i)));
        ctx->rk[i] = k[i + 4];
    }
}

void sm4_setkey_dec(sm4_ctx *ctx, const uint8_t key[16])
{
    sm4_setkey_enc(ctx, key);
}

#else
#error "bad SMC_SM4_KERNEL"
#endif

/* ---------------------------- 工作模式 ---------------------------- */
#if SMC_SM4_MODE_CBC

void sm4_cbc_encrypt(const sm4_ctx *ctx, uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len)
{
    size_t off;
    for (off = 0; off < len; off += SMC_SM4_BLOCK) {
        int i;
        for (i = 0; i < 16; i++) iv[i] ^= in[off + i];
        sm4_encrypt_block(ctx, iv, out + off);
        for (i = 0; i < 16; i++) iv[i] = out[off + i];
    }
}

void sm4_cbc_decrypt(const sm4_ctx *ctx, uint8_t iv[16],
                     const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t prev[SMC_SM4_BLOCK];
    size_t off;
    int i;
    for (off = 0; off < len; off += SMC_SM4_BLOCK) {
        for (i = 0; i < 16; i++) prev[i] = in[off + i];
        sm4_decrypt_block(ctx, in + off, out + off);
        for (i = 0; i < 16; i++) out[off + i] ^= iv[i];
        for (i = 0; i < 16; i++) iv[i] = prev[i];
    }
}

#endif /* SMC_SM4_MODE_CBC */

#if SMC_SM4_MODE_CTR

/* 计数器自增：只动末尾 inc_bytes 字节（大端、进位不回卷到更高字节） */
static void sm4_ctr_inc(uint8_t ctr[SMC_SM4_BLOCK], unsigned inc_bytes)
{
    int i;
    for (i = (int)SMC_SM4_BLOCK - 1; i >= (int)(SMC_SM4_BLOCK - inc_bytes); i--) {
        if (++ctr[i] != 0) break;
    }
}

void sm4_ctr_xor_n(const sm4_ctx *ctx, uint8_t ctr[SMC_SM4_BLOCK], unsigned inc_bytes,
                   const uint8_t *in, uint8_t *out, size_t len)
{
    uint8_t ks[SMC_SM4_BLOCK];
    size_t off = 0;
    int i;

    if (inc_bytes == 0 || inc_bytes > SMC_SM4_BLOCK) inc_bytes = SMC_SM4_BLOCK;
    while (off < len) {
        size_t n = len - off;
        if (n > SMC_SM4_BLOCK) n = SMC_SM4_BLOCK;
        sm4_encrypt_block(ctx, ctr, ks);
        for (i = 0; i < SMC_SM4_BLOCK && (size_t)i < n; i++) out[off + i] = in[off + i] ^ ks[i];
        sm4_ctr_inc(ctr, inc_bytes);
        off += n;
    }
}

void sm4_ctr_xor(const sm4_ctx *ctx, uint8_t ctr[SMC_SM4_BLOCK],
                 const uint8_t *in, uint8_t *out, size_t len)
{
    sm4_ctr_xor_n(ctx, ctr, 8, in, out, len);   /* 默认：低 64 位整体自增 */
}

#endif /* SMC_SM4_MODE_CTR */
