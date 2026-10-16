/*
 * sm3.c — SM3 密码杂凑算法（GM/T 0004-2012）
 *
 * 四档内核（SMC_SM3_KERNEL）：
 *   REF  : 教科书式消息扩展（W[68] + W'[64] 全数组），性能基线
 *   FAST : 消息扩展 2 路批量 + 压缩 8 轮展开 + load/store 合并（默认，最快）
 *   TINY : 消息扩展滚动 16 字环形缓冲 + 4 字流水，压缩栈 < 100 B（最小内存）
 *   MCU  : 仅保留 W[68]，W' 即时异或；拆分两类轮函数，减少栈访存与每轮分支
 */
#include "smc/sm3.h"

#define ROTL(x, n) (((x) << ((n) & 31)) | ((x) >> ((32 - (n)) & 31)))

#define P0(x) ((x) ^ ROTL((x), 9) ^ ROTL((x), 17))
#define P1(x) ((x) ^ ROTL((x), 15) ^ ROTL((x), 23))

#define FF0(x, y, z) ((x) ^ (y) ^ (z))
#define GG0(x, y, z) ((x) ^ (y) ^ (z))
#define FF1(x, y, z) (((x) & (y)) | ((x) & (z)) | ((y) & (z)))
#define GG1(x, y, z) (((x) & (y)) | ((~(x)) & (z)))

#define T0 0x79CC4519u
#define T1 0x7A879D8Au

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

static void store_be64(uint8_t *p, uint64_t v)
{
    int i;
    for (i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; }
}

static const uint32_t IV[8] = {
    0x7380166Fu, 0x4914B2B9u, 0x172442D7u, 0xDA8A0600u,
    0xA96F30BCu, 0x163138AAu, 0xE38DEE4Du, 0xB0FB0E4Eu
};

/* ------------------------------------------------------------------ */
/* 压缩函数：REF（教科书基线）                                          */
/* ------------------------------------------------------------------ */
#if SMC_SM3_KERNEL == SMC_KERNEL_REF

void sm3_compress(uint32_t h[8], const uint8_t block[SMC_SM3_BLOCK])
{
    uint32_t w[68], wp[64];
    uint32_t a, b, c, d, e, f, g, hh;
    int j;

    for (j = 0; j < 16; j++) w[j] = load_be32(block + 4 * j);
    for (j = 16; j < 68; j++) {
        w[j] = P1(w[j - 16] ^ w[j - 9] ^ ROTL(w[j - 3], 15)) ^
               ROTL(w[j - 13], 7) ^ w[j - 6];
    }
    for (j = 0; j < 64; j++) wp[j] = w[j] ^ w[j + 4];

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    for (j = 0; j < 64; j++) {
        uint32_t tj  = (j < 16) ? T0 : T1;
        uint32_t ss1 = ROTL((ROTL(a, 12) + e + ROTL(tj, (unsigned)(j & 31))), 7);
        uint32_t ss2 = ss1 ^ ROTL(a, 12);
        uint32_t ff  = (j < 16) ? FF0(a, b, c) : FF1(a, b, c);
        uint32_t gg  = (j < 16) ? GG0(e, f, g) : GG1(e, f, g);
        uint32_t tt1 = ff + d + ss2 + wp[j];
        uint32_t tt2 = gg + hh + ss1 + w[j];
        d = c; c = ROTL(b, 9); b = a; a = tt1;
        hh = g; g = ROTL(f, 19); f = e; e = P0(tt2);
    }

    h[0] ^= a; h[1] ^= b; h[2] ^= c; h[3] ^= d;
    h[4] ^= e; h[5] ^= f; h[6] ^= g; h[7] ^= hh;
}

/* ------------------------------------------------------------------ */
/* 压缩函数：FAST（默认，指令级优化）                                    */
/* ------------------------------------------------------------------ */
#elif SMC_SM3_KERNEL == SMC_KERNEL_FAST

void sm3_compress(uint32_t h[8], const uint8_t block[SMC_SM3_BLOCK])
{
    uint32_t w[68], wp[64];
    uint32_t a, b, c, d, e, f, g, hh;
    int j;

    /* 消息扩展：2 路交织展开，摊薄循环开销 */
    for (j = 0; j < 16; j++) w[j] = load_be32(block + 4 * j);
    for (j = 16; j < 68; j += 2) {
        uint32_t w0 = w[j - 16] ^ w[j - 9] ^ ROTL(w[j - 3], 15);
        uint32_t w1 = w[j - 15] ^ w[j - 8] ^ ROTL(w[j - 2], 15);
        w[j]     = P1(w0) ^ ROTL(w[j - 13], 7) ^ w[j - 6];
        w[j + 1] = P1(w1) ^ ROTL(w[j - 12], 7) ^ w[j - 5];
    }
    for (j = 0; j < 64; j += 2) {
        wp[j] = w[j] ^ w[j + 4];
        wp[j + 1] = w[j + 1] ^ w[j + 5];
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    /* 压缩 8 轮一组展开 */
    for (j = 0; j < 64; j += 8) {
        int k;
        for (k = 0; k < 8; k++) {
            int r = j + k;
            uint32_t tj, ss1, tt1, tt2;
            if (r < 16) {
                tj  = ROTL(T0, (unsigned)(r & 31));
                ss1 = ROTL(ROTL(a, 12) + e + tj, 7);
                tt1 = FF0(a, b, c) + d + (ss1 ^ ROTL(a, 12)) + wp[r];
                tt2 = GG0(e, f, g) + hh + ss1 + w[r];
            } else {
                tj  = ROTL(T1, (unsigned)(r & 31));
                ss1 = ROTL(ROTL(a, 12) + e + tj, 7);
                tt1 = FF1(a, b, c) + d + (ss1 ^ ROTL(a, 12)) + wp[r];
                tt2 = GG1(e, f, g) + hh + ss1 + w[r];
            }
            d = c; c = ROTL(b, 9); b = a; a = tt1;
            hh = g; g = ROTL(f, 19); f = e; e = P0(tt2);
        }
    }

    h[0] ^= a; h[1] ^= b; h[2] ^= c; h[3] ^= d;
    h[4] ^= e; h[5] ^= f; h[6] ^= g; h[7] ^= hh;
}

/* ------------------------------------------------------------------ */
/* 压缩函数：TINY（最小栈：16 字环形缓冲滚动消息扩展）                    */
/* ------------------------------------------------------------------ */
#elif SMC_SM3_KERNEL == SMC_KERNEL_TINY

void sm3_compress(uint32_t h[8], const uint8_t block[SMC_SM3_BLOCK])
{
    /* 滚动调度：buf 环形 16 字；W[j]（j≥16）生成时立即完成 round j−4
       （W'[j−4] = W[j−4] ^ W[j]，恰为环形缓冲当前字与新字异或） */
    uint32_t buf[16];
    uint32_t a, b, c, d, e, f, g, hh;
    int j;

    for (j = 0; j < 16; j++) buf[j] = load_be32(block + 4 * j);

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

#define SM3_ROUND(r, wj, wpj)                                              \
    do {                                                                   \
        uint32_t tj  = ROTL(((r) < 16) ? T0 : T1, (unsigned)((r) & 31));   \
        uint32_t ss1 = ROTL(ROTL(a, 12) + e + tj, 7);                      \
        uint32_t tt1, tt2;                                                 \
        if ((r) < 16) {                                                    \
            tt1 = FF0(a, b, c) + d + (ss1 ^ ROTL(a, 12)) + (wpj);          \
            tt2 = GG0(e, f, g) + hh + ss1 + (wj);                          \
        } else {                                                           \
            tt1 = FF1(a, b, c) + d + (ss1 ^ ROTL(a, 12)) + (wpj);          \
            tt2 = GG1(e, f, g) + hh + ss1 + (wj);                          \
        }                                                                  \
        d = c; c = ROTL(b, 9); b = a; a = tt1;                             \
        hh = g; g = ROTL(f, 19); f = e; e = P0(tt2);                       \
    } while (0)

    /* round 0..11：W[0..15] 均为块字，W'[j] = buf[j] ^ buf[j+4] */
    for (j = 0; j < 12; j++) {
        SM3_ROUND(j, buf[j], buf[j] ^ buf[j + 4]);
    }

    /* W[16..67] 滚动生成（先读后写同一槽位），同步执行 round j−4 */
    for (j = 16; j < 68; j++) {
        uint32_t w16 = buf[(j - 16) & 15];
        uint32_t w9  = buf[(j -  9) & 15];
        uint32_t w3  = buf[(j -  3) & 15];
        uint32_t w13 = buf[(j - 13) & 15];
        uint32_t w6  = buf[(j -  6) & 15];
        uint32_t wj  = buf[(j -  4) & 15];
        uint32_t nw  = P1(w16 ^ w9 ^ ROTL(w3, 15)) ^ ROTL(w13, 7) ^ w6;
        buf[j & 15] = nw;
        SM3_ROUND(j - 4, wj, wj ^ nw);
    }

#undef SM3_ROUND

    h[0] ^= a; h[1] ^= b; h[2] ^= c; h[3] ^= d;
    h[4] ^= e; h[5] ^= f; h[6] ^= g; h[7] ^= hh;
}

/* ------------------------------------------------------------------ */
/* 压缩函数：MCU（减少 RAM 流量与每轮条件分支）                         */
/* ------------------------------------------------------------------ */
#elif SMC_SM3_KERNEL == SMC_KERNEL_MCU

void sm3_compress(uint32_t h[8], const uint8_t block[SMC_SM3_BLOCK])
{
    uint32_t w[68];
    uint32_t a, b, c, d, e, f, g, hh, tj;
    int j;

    for (j = 0; j < 16; j++) w[j] = load_be32(block + 4 * j);
    for (j = 16; j < 68; j++) {
        w[j] = P1(w[j - 16] ^ w[j - 9] ^ ROTL(w[j - 3], 15)) ^
               ROTL(w[j - 13], 7) ^ w[j - 6];
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3];
    e = h[4]; f = h[5]; g = h[6]; hh = h[7];

#define SM3_MCU_ROUND(ff_, gg_)                                             \
    do {                                                                    \
        uint32_t a12 = ROTL(a, 12);                                         \
        uint32_t ss1 = ROTL(a12 + e + tj, 7);                               \
        uint32_t tt1 = (ff_) + d + (ss1 ^ a12) + (w[j] ^ w[j + 4]);         \
        uint32_t tt2 = (gg_) + hh + ss1 + w[j];                             \
        d = c; c = ROTL(b, 9); b = a; a = tt1;                              \
        hh = g; g = ROTL(f, 19); f = e; e = P0(tt2);                        \
        tj = ROTL(tj, 1);                                                    \
    } while (0)

    tj = T0;
    for (j = 0; j < 16; j++) SM3_MCU_ROUND(FF0(a, b, c), GG0(e, f, g));

    tj = ROTL(T1, 16);
    for (j = 16; j < 64; j++) SM3_MCU_ROUND(FF1(a, b, c), GG1(e, f, g));

#undef SM3_MCU_ROUND

    h[0] ^= a; h[1] ^= b; h[2] ^= c; h[3] ^= d;
    h[4] ^= e; h[5] ^= f; h[6] ^= g; h[7] ^= hh;
}

#else
#error "bad SMC_SM3_KERNEL"
#endif

/* ------------------------------------------------------------------ */
/* 吸收/填充（三内核共用）                                               */
/* ------------------------------------------------------------------ */
void sm3_init(sm3_ctx *ctx)
{
    int i;
    for (i = 0; i < 8; i++) ctx->h[i] = IV[i];
    ctx->total = 0;
    ctx->buflen = 0;
}

void sm3_update(sm3_ctx *ctx, const uint8_t *data, size_t len)
{
    size_t i = 0;
    ctx->total += (uint64_t)len;
    if (ctx->buflen) {
        while (i < len && ctx->buflen < SMC_SM3_BLOCK) {
            ctx->buf[ctx->buflen++] = data[i++];
        }
        if (ctx->buflen == SMC_SM3_BLOCK) {
            sm3_compress(ctx->h, ctx->buf);
            ctx->buflen = 0;
        }
    }
    while (i + SMC_SM3_BLOCK <= len) {
        sm3_compress(ctx->h, data + i);
        i += SMC_SM3_BLOCK;
    }
    while (i < len) ctx->buf[ctx->buflen++] = data[i++];
}

void sm3_final(sm3_ctx *ctx, uint8_t out[SMC_SM3_DIGEST])
{
    uint8_t tail[SMC_SM3_BLOCK + 8];
    uint64_t bits = ctx->total << 3;
    size_t padlen;
    int i;

    padlen = (size_t)(SMC_SM3_BLOCK - ((ctx->total + 8) % SMC_SM3_BLOCK));
    tail[0] = 0x80;
    for (i = 1; i < (int)padlen; i++) tail[i] = 0;
    store_be64(tail + padlen, bits);
    sm3_update(ctx, tail, padlen + 8);

    /* sm3_update 改变了 total，但输出只依赖链式变量；tail 不再入 total 语义 */
    for (i = 0; i < 8; i++) store_be32(out + 4 * i, ctx->h[i]);
}

void sm3(const uint8_t *data, size_t len, uint8_t out[SMC_SM3_DIGEST])
{
    sm3_ctx ctx;
    sm3_init(&ctx);
    sm3_update(&ctx, data, len);
    sm3_final(&ctx, out);
}
