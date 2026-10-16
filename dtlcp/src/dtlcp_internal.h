/*
 * dtlcp_internal.h — DTLCP 内部结构（记录层状态 / 密钥 / 上下文）
 */
#ifndef DTLCP_INTERNAL_H
#define DTLCP_INTERNAL_H

#include <stdint.h>
#include "dtlcp/dtlcp.h"
#include "smc/sm3.h"
#include "smc/sm4.h"
#include "smc/sm2.h"
#include "smc/hmac_sm3.h"
#include "sm2_internal.h"   /* sm2_fe/sm2_pt（内部曲线类型） */

/* ------------------------------------------------------------------ *
 * 单记录缓冲大小（编译期可配：-D 覆盖）
 *
 * 为什么必须可配：DTLCP_RECBUF 同时决定上下文 RAM 与记录/数据报容量——
 *   ① 上下文静态 RAM：ctx 里有 pending + txbuf + rxbuf **三份** = 3 × RECBUF
 *      → 1600 时是 4800 B；
 *   ② 握手临时缓冲已改为复用 ctx->rxbuf/txbuf，栈不再随 RECBUF 线性增长；
 *   ③ 单条记录可承载的明文上限（记录层有 `len > RECBUF - 77` 的硬检查）。
 *
 * 平台差异（实测，见 docs/09 §4）：
 *   - host x86-64：RAM 充足，默认保持 1600 以维持既有 MTU/基准口径；
 *   - **STM32F103C8T6：只有 20 KB RAM**，MCU 构建使用 `DTLCP_RECBUF=1024`。
 *     2026-10-16 ARM 实测门禁：ctx=5248 B，算法/协议静态 RAM=4104 B，
 *     最长调用路径栈下界=4872 B，总下界=14224 B，余量=6256 B。
 *
 * 用法：`bash embedded/cortex-m3/build.sh` 已在 CFLAGS 里带 -D DTLCP_RECBUF=1024；
 *      host 构建**不要**加（保持 1600 以维持既有测试与基准口径）。
 * ------------------------------------------------------------------ */
#ifndef DTLCP_RECBUF
#define DTLCP_RECBUF      1600   /* 单记录缓冲（UDP MTU 内）*/
#endif

/* 编译期自检：RECBUF 必须容纳组合握手 flight 与记录层开销。 */
#if DTLCP_RECBUF < 512
#error "DTLCP_RECBUF 过小：FULL 组合握手 flight 无法容纳"
#endif
/* 记录层明文上限 = RECBUF - 77（头部 13 + MAC 32 + CTR/CBC 尾部余量），
   必须至少能放下一条 SM4 分组 + 一点余量 */
#if (DTLCP_RECBUF - 77) < 64
#error "DTLCP_RECBUF 过小：扣除记录层开销后放不下一个 SM4 分组"
#endif

#define DTLCP_HDR_LEN     13     /* type|ver|epoch|seq|len(2) */
#define DTLCP_PSK_MAX     32
#define DTLCP_HS_BODY_MAX 224    /* ClientHello 最大约 201 B；留字段扩展余量 */

typedef struct {
    uint8_t master[48];
    uint8_t client_mac[32], server_mac[32];
    uint8_t client_key[16], server_key[16];
    uint8_t client_iv[8],  server_iv[8];   /* CTR nonce 前缀 */
} dtlcp_keys;

/* 单方向记录层状态 */
typedef struct {
    uint16_t suite;
    int      etm;                 /* 1 = Encrypt-then-MAC（CTR 套件） */
    uint8_t  mac_key[32];
    sm4_ctx  cipher;       /* 发送/CTR 加密核（正序轮密钥） */
    sm4_ctx  cipher_dec;   /* CBC 解密核（逆序轮密钥；FAST 内核复用加密核要求分离） */
    uint8_t  iv_prefix[8];
    uint64_t seq;                 /* 本端发送序号 */
    /* 防重放窗口（对端方向） */
    uint64_t window;
    uint64_t max_seq;
    int      have_max;
} dtlcp_rec;

struct dtlcp_ctx {
    int      role, mode, state;
    uint16_t suite;       /* 当前生效套件（服务端 = 客户端提议，须与 suite_cfg 一致） */
    uint16_t suite_cfg;   /* 本端配置的套件（白名单基准） */
    int      established;

    dtlcp_io io;
    dtlcp_keys keys;
    dtlcp_rec  tx, rx;

    uint8_t  cr[32], sr[32];
    uint8_t  session_id[DTLCP_SID_MAX];
    uint8_t  sid_len;

    sm3_ctx  hs_hash;             /* 握手 transcript */
    uint8_t  eph_priv[32];
    uint8_t  eph_pub[SMC_SM2_PUBKEY];
    uint8_t  peer_eph_pub[SMC_SM2_PUBKEY];

    dtlcp_cert cert, peer_cert, ca_cert;
    uint8_t    privkey[SMC_SM2_BYTES];
    int        verify_peer;

    uint8_t    psk[DTLCP_PSK_MAX];
    size_t     psk_len;
    char       identity[DTLCP_ID_MAX];
    size_t     identity_len;

    smc_entropy_fn entropy_fn;    /* 生产 TRNG；NULL=测试确定性路径 */
    void          *entropy_user;

    dtlcp_session session;        /* 恢复会话（服务端命中输出） */
    dtlcp_stats   stats;

    int        use_early;
    uint64_t   now_us;

    uint8_t  pending[DTLCP_RECBUF]; /* 0-RTT 早期应用数据暂存 */
    size_t   pending_len;

    uint8_t  txbuf[DTLCP_RECBUF];
    uint8_t  rxbuf[DTLCP_RECBUF];
};

/* 数据报游标：一个 UDP 数据报内多记录流式拆分 */
typedef struct {
    uint8_t *buf;                /* 指向 ctx->rxbuf，不在调用栈复制 RECBUF */
    size_t  len, off;
} dtlcp_datagram;

/* ---- dtlcp_crypto.c ---- */
void dtlcp_prf(const uint8_t *secret, size_t slen, const char *label,
               const uint8_t *seed, size_t seedlen, uint8_t *out, size_t olen);
void dtlcp_derive_keys(const uint8_t *pms, size_t pmslen,
                       const uint8_t cr[32], const uint8_t sr[32],
                       dtlcp_keys *out);
void dtlcp_expand_keys(const uint8_t master[48],
                       const uint8_t cr[32], const uint8_t sr[32],
                       dtlcp_keys *out);
void dtlcp_finished_verify(const dtlcp_keys *keys, int from_client,
                           const uint8_t hs_hash[32], uint8_t out[DTLCP_VERIFY_LEN]);

/* ---- dtlcp_record.c ---- */
void dtlcp_rec_init_tx(dtlcp_rec *r, uint16_t suite, const uint8_t mac_key[32],
                       const uint8_t key[16], const uint8_t iv_prefix[8]);
void dtlcp_rec_init_rx(dtlcp_rec *r, uint16_t suite, const uint8_t mac_key[32],
                       const uint8_t key[16], const uint8_t iv_prefix[8]);
/* 保护一个记录：out 需 len + 77 字节（13 头 + 16 IV + 32 MAC + 16 填充；CTR 套件更少） */
int  dtlcp_rec_protect(dtlcp_rec *r, uint8_t type,
                       const uint8_t *in, size_t len,
                       uint8_t *out, size_t *olen, uint64_t now_us);
/* 解保护一个记录；*consumed 返回该记录占用的输入字节 */
int  dtlcp_rec_unprotect(dtlcp_rec *r, const uint8_t *in, size_t len,
                         uint8_t *out, size_t cap, size_t *olen,
                         uint8_t *type, size_t *consumed);

#endif /* DTLCP_INTERNAL_H */
