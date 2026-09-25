/*
 * dtlcp.h — DTLCP：面向 UDP 低功耗终端的国密传输协议
 *
 * 参照 TLCP（GB/T 38636-2020）语义 + DTLS 数据报机制实现兼容子集：
 *   - 记录层：epoch + 48bit 序列号 + 64 位滑动窗口防重放；单记录 = 单 UDP 数据报
 *   - 密码套件替换（要求②）：
 *       ECDHE_SM4_CBC_SM3   标准兼容套件（MAC-then-encrypt，与 TLCP 记录层同构）
 *       L_SM2_SM4_CTR_SM3   轻量套件（Encrypt-then-MAC，免 padding 低时延）
 *       L_PSK_SM4_CTR_SM3   预共享密钥套件（0 次 SM2 运算，支持 0-RTT 早期数据）
 *   - 握手精简（要求②）：FULL 1-RTT（key_share 前置到 ClientHello）
 *       RESUME 1-RTT（会话恢复，0 次 SM2）/ PSK 0-RTT
 * 与标准差异见 docs/02 §4。
 */
#ifndef DTLCP_H
#define DTLCP_H

#include <stddef.h>
#include <stdint.h>
#include "smc/smc_config.h"
#include "smc/smc_rng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DTLCP_VERSION        0x0101
#define DTLCP_RANDOM_LEN     32
#define DTLCP_VERIFY_LEN     16
#define DTLCP_MAC_LEN        32
#define DTLCP_IV_LEN         16
#define DTLCP_SID_MAX        32
#define DTLCP_ID_MAX         32

/* 记录类型 */
#define DTLCP_RT_HANDSHAKE   20
#define DTLCP_RT_CCS         21
#define DTLCP_RT_APPDATA     22
#define DTLCP_RT_ALERT       23

/* 握手消息类型 */
#define DTLCP_HS_CLIENT_HELLO   1
#define DTLCP_HS_SERVER_HELLO   2
#define DTLCP_HS_CERTIFICATE    11
#define DTLCP_HS_SERVER_KEY_EX  12
#define DTLCP_HS_CLIENT_KEY_EX  16
#define DTLCP_HS_CCS            20
#define DTLCP_HS_FINISHED       21

/* 密码套件 */
#define DTLCP_SUITE_ECDHE_SM4_CBC_SM3  0x0101  /* 标准：ECDHE + SM4-CBC + HMAC-SM3 (MtE) */
#define DTLCP_SUITE_L_SM2_SM4_CTR_SM3  0x0201  /* 轻量：ECDHE + SM4-CTR + HMAC-SM3 (EtM) */
#define DTLCP_SUITE_L_PSK_SM4_CTR_SM3  0x0301  /* PSK  ：SM4-CTR + HMAC-SM3 (EtM)，0-RTT */

/* 连接模式 */
#define DTLCP_MODE_FULL     1
#define DTLCP_MODE_RESUME   2
#define DTLCP_MODE_PSK      3

/* 角色 */
#define DTLCP_ROLE_CLIENT   0
#define DTLCP_ROLE_SERVER   1

/* 錯误码 */
#define DTLCP_OK            0
#define DTLCP_E_PARAM      -1
#define DTLCP_E_IO         -2
#define DTLCP_E_FORMAT     -3
#define DTLCP_E_CRYPTO     -4
#define DTLCP_E_VERIFY     -5
#define DTLCP_E_REPLAY     -6
#define DTLCP_E_STATE      -7
#define DTLCP_E_NOSUPPORT  -8

/* 紧凑证书（X.509 映射约束见 docs/02 §4.4） */
typedef struct {
    uint8_t  version;
    uint8_t  serial_len;
    uint8_t  subject_len;
    uint8_t  issuer_len;
    uint8_t  serial[20];
    uint8_t  subject[DTLCP_ID_MAX];
    uint8_t  issuer[DTLCP_ID_MAX];
    uint8_t  pubkey[SMC_SM2_PUBKEY];   /* 0x04‖x‖y */
    uint8_t  sig_alg;                  /* 0x01 = SM2-with-SM3 */
    uint8_t  sig[SMC_SM2_SIG];         /* r‖s */
} dtlcp_cert;

/* 会话缓存（服务端恢复用） */
typedef struct {
    uint8_t  master[48];
    uint8_t  session_id[DTLCP_SID_MAX];
    uint8_t  sid_len;
    uint16_t suite;
    int      valid;
} dtlcp_session;

/* 运行统计（demo/bench 采集） */
typedef struct {
    int      hs_flights;        /* 握手飞行数（RTT≈flights−1） */
    int      hs_messages;       /* 握手消息条数 */
    size_t   bytes_sent;        /* 线上字节（含头） */
    size_t   bytes_recv;
    size_t   app_sent, app_recv;
    int      sm2_ops;           /* 签名/验签/ECDH 次数 */
    int      sm3_ops;
    int      sm4_blocks;
    int      replays_dropped;
    uint64_t hs_us;             /* 握手耗时（µs，由平台时钟） */
} dtlcp_stats;

/* 传输抽象：demo 用 UDP socket，测试用内存环回，移植时替换 */
typedef struct dtlcp_io {
    /* 发送一个数据报，返回 0 成功 */
    int (*send)(void *user, const uint8_t *buf, size_t len);
    /* 接收一个数据报，>0 长度 / 0 超时 / <0 错误 */
    int (*recv)(void *user, uint8_t *buf, size_t cap);
    void *user;
} dtlcp_io;

/* 配置 */
typedef struct {
    int         role;                     /* DTLCP_ROLE_* */
    int         mode;                     /* DTLCP_MODE_* */
    uint16_t    suite;                    /* DTLCP_SUITE_* */
    const char *identity;                 /* 端身份（默认 "1234567812345678"） */

    /* 证书认证（FULL 模式） */
    dtlcp_cert  cert;                     /* 本端证书 */
    uint8_t privkey[SMC_SM2_BYTES];       /* 本端 SM2 私钥 */
    dtlcp_cert  ca_cert;                  /* 对端签发者（紧凑信任锚） */
    int         verify_peer;              /* 双向认证 */

    /* PSK 模式 */
    const uint8_t *psk;
    size_t         psk_len;
    const uint8_t *early_data;      /* PSK 0-RTT 早期数据（可为 NULL） */
    size_t         early_len;

    /* 会话恢复 */
    dtlcp_session *resume;                /* 客户端携带 / 服务端缓存命中 */

    /* 熵源（生产必须接 TRNG；为 NULL 时退化为确定性 DRBG，仅供测试） */
    smc_entropy_fn entropy;
    void          *entropy_user;
} dtlcp_cfg;

/* 内部上下文（大小固定，无动态内存） */
typedef struct dtlcp_ctx dtlcp_ctx;

/* ---- 生命周期 ---- */
void dtlcp_init(dtlcp_ctx *ctx);
/* 客户端/服务端握手；成功后可收发应用数据 */
int  dtlcp_connect(dtlcp_ctx *ctx, const dtlcp_cfg *cfg, dtlcp_io *io, uint64_t now_us);
int  dtlcp_accept(dtlcp_ctx *ctx, const dtlcp_cfg *cfg, dtlcp_io *io, uint64_t now_us);

/* ---- 数据传输 ---- */
int  dtlcp_send(dtlcp_ctx *ctx, const uint8_t *data, size_t len);
int  dtlcp_recv(dtlcp_ctx *ctx, uint8_t *buf, size_t cap);   /* 返回明文长度 */

/* ---- 观测 ---- */
void dtlcp_get_stats(const dtlcp_ctx *ctx, dtlcp_stats *st);
int  dtlcp_is_established(const dtlcp_ctx *ctx);
const dtlcp_session *dtlcp_get_session(const dtlcp_ctx *ctx);

/* ---- 紧凑证书工具 ---- */
int  dtlcp_cert_issue(dtlcp_cert *cert, const char *subject, const uint8_t serial,
                      const uint8_t subject_pub[SMC_SM2_PUBKEY],
                      const char *issuer, const uint8_t issuer_priv[SMC_SM2_BYTES]);
int  dtlcp_cert_check(const dtlcp_cert *cert, const dtlcp_cert *issuer_cert);
int  dtlcp_cert_encode(const dtlcp_cert *cert, uint8_t *out, size_t cap);
int  dtlcp_cert_parse(dtlcp_cert *cert, const uint8_t *in, size_t len);

#ifdef __cplusplus
}
#endif
#endif /* DTLCP_H */
