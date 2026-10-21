/*
 * dtlcp_hs.c — DTLCP 握手状态机（FULL 1-RTT / RESUME 1-RTT / PSK 0-RTT）
 *
 * 精简设计（要求② 精简握手交互）：
 *   - key_share 前置到 ClientHello（TLS1.3 思想）：
 *       FULL   双向完成 1.5-RTT，客户端 1-RTT 即可发应用数据
 *              （对比 TLCP 标准握手 3-RTT，节省 2 个 RTT）
 *       RESUME 会话恢复 1-RTT，全程 0 次 SM2 运算
 *       PSK    预共享密钥，0-RTT 早期数据（重放风险声明见 docs/03 §7）
 *   - ServerHello‖Certificate‖ServerKeyExchange‖CCS‖Finished 合并单数据报
 * 握手记录在 CCS 前为明文（与 TLCP 语义一致），关键参数由 SKE 签名覆盖；
 * transcript = 全部握手消息（不含 CCS/Finished）的 SM3 流式摘要。
 * 一个 UDP 数据报可含多记录；接收按记录流式解保护，遇 CCS 即切换 rx 密钥。
 */
#include <string.h>
#include "dtlcp_internal.h"

static void save_session(dtlcp_ctx *c);   /* 前向声明（定义见文末） */
static int  suite_known(uint16_t s);      /* 前向声明（定义见文末「公共入口」） */

/* ================= 编码辅助 ================= */
static void p_u8(uint8_t **p, uint8_t v) { *(*p)++ = v; }
static void p_u16(uint8_t **p, uint16_t v)
{
    *(*p)++ = (uint8_t)(v >> 8); *(*p)++ = (uint8_t)v;
}
static void p_u24(uint8_t **p, uint32_t v)
{
    *(*p)++ = (uint8_t)(v >> 16); *(*p)++ = (uint8_t)(v >> 8); *(*p)++ = (uint8_t)v;
}
static void p_bytes(uint8_t **p, const uint8_t *b, size_t n)
{
    memcpy(*p, b, n); *p += n;
}

static uint8_t g_u8(const uint8_t **p) { return *(*p)++; }
static uint16_t g_u16(const uint8_t **p)
{
    uint16_t v = (uint16_t)((*p)[0] << 8 | (*p)[1]); *p += 2; return v;
}
static uint32_t g_u24(const uint8_t **p)
{
    uint32_t v = ((uint32_t)(*p)[0] << 16) | ((uint32_t)(*p)[1] << 8) | (*p)[2];
    *p += 3; return v;
}
static const uint8_t *g_bytes(const uint8_t **p, size_t n)
{
    const uint8_t *s = *p; *p += n; return s;
}

/* ================= 随机源 =================
 * 安全规则：配置了熵源（生产固件）时，熵源失败必须 fail-closed（返回 E_NORNG），
 * 绝不回退到“由时间/计数器播种的确定性 DRBG”；未配置熵源只允许测试/演示，
 * 且 cfg_check 已要求调用方显式设置 allow_deterministic_rng=1。 */
static int hs_random(dtlcp_ctx *c, uint8_t *out, size_t len, const char *tag)
{
    smc_drbg dr;
    uint8_t seed[48];
    size_t i, tl;
    if (c->entropy_fn) {
        if (c->entropy_fn(c->entropy_user, out, len) == 0) return DTLCP_OK;
        return DTLCP_E_NORNG;
    }
    tl = strlen(tag);
    memset(seed, 0, sizeof seed);
    for (i = 0; i < tl && i < 16; i++) seed[i] = (uint8_t)tag[i];
    memcpy(seed + 16, &c->now_us, 8);
    memcpy(seed + 24, &c->stats.hs_messages, 4);
    memcpy(seed + 28, &c->stats.bytes_sent, 8);
    seed[36] = (uint8_t)c->role;
    smc_drbg_seed(&dr, seed, 37);
    smc_drbg_bytes(&dr, out, len);
    return DTLCP_OK;
}

/* ================= transcript ================= */
static void hs_absorb(dtlcp_ctx *c, const uint8_t *msg, size_t len)
{
    sm3_update(&c->hs_hash, msg, len);
    c->stats.hs_messages++;
}

static void hs_digest(const dtlcp_ctx *c, uint8_t out[SMC_SM3_DIGEST])
{
    sm3_ctx tmp = c->hs_hash;
    sm3_final(&tmp, out);
}

/* ================= 发送路径 ================= */
static int io_send(dtlcp_ctx *c, const uint8_t *buf, size_t len)
{
    int rc = c->io.send(c->io.user, buf, len);
    if (rc == 0) c->stats.bytes_sent += len;
    return rc;
}

static int rec_send(dtlcp_ctx *c, dtlcp_rec *rec, uint8_t type,
                    const uint8_t *payload, size_t plen)
{
    size_t olen;
    int rc = dtlcp_rec_protect(rec, type, payload, plen, c->txbuf, &olen, c->now_us);
    if (rc != DTLCP_OK) return rc;
    return io_send(c, c->txbuf, olen);
}

/* 握手消息：type(1)‖len(3)‖body → 入 transcript → 单记录发送 */
static int hs_send(dtlcp_ctx *c, dtlcp_rec *rec, uint8_t hs_type,
                   const uint8_t *body, size_t blen)
{
    uint8_t *msg = c->rxbuf; /* 发送阶段复用接收缓冲；rec_send 输出到 txbuf */
    uint8_t *p = msg;
    if (blen + 4 > DTLCP_RECBUF) return DTLCP_E_PARAM;
    p_u8(&p, hs_type);
    p_u24(&p, (uint32_t)blen);
    p_bytes(&p, body, blen);
    hs_absorb(c, msg, (size_t)(p - msg));
    return rec_send(c, rec, DTLCP_RT_HANDSHAKE, msg, (size_t)(p - msg));
}

/* 打包握手消息（不入 transcript，调用方统一 absorb） */
static size_t hs_pack(uint8_t *out, uint8_t hs_type, const uint8_t *body, size_t blen)
{
    uint8_t *p = out;
    p_u8(&p, hs_type);
    p_u24(&p, (uint32_t)blen);
    p_bytes(&p, body, blen);
    return (size_t)(p - out);
}

static int rec_append(dtlcp_ctx *c, dtlcp_rec *rec, uint8_t type,
                      const uint8_t *payload, size_t plen, uint8_t *buf, size_t *off)
{
    size_t olen;
    /* 保护到独立暂存区再拼接：payload 可能指向 buf，直接原地保护会重叠破坏明文 */
    int rc = dtlcp_rec_protect(rec, type, payload, plen, c->txbuf, &olen, c->now_us);
    if (rc != DTLCP_OK) return rc;
    if (*off + olen > DTLCP_RECBUF) return DTLCP_E_PARAM;
    memcpy(buf + *off, c->txbuf, olen);
    *off += olen;
    return DTLCP_OK;
}

/* ================= 接收路径（数据报 → 流式多记录） ================= */
static int dg_recv(dtlcp_ctx *c, dtlcp_datagram *dg)
{
    int got;
    dg->buf = c->rxbuf;
    got = c->io.recv(c->io.user, dg->buf, DTLCP_RECBUF);
    if (got <= 0) return DTLCP_E_IO;
    dg->len = (size_t)got;
    dg->off = 0;
    c->stats.bytes_recv += dg->len;
    return DTLCP_OK;
}

/* 取下一个记录（CCS 记录解出后调用 install_rx 由状态机负责） */
static int dg_next(dtlcp_ctx *c, dtlcp_datagram *dg,
                   uint8_t *type, uint8_t *payload, size_t cap, size_t *plen)
{
    size_t consumed;
    int rc;
    if (dg->off >= dg->len) return DTLCP_E_FORMAT;   /* 无更多记录 */
    rc = dtlcp_rec_unprotect(&c->rx, dg->buf + dg->off, dg->len - dg->off,
                             payload, cap, plen, type, &consumed);
    if (rc != DTLCP_OK) return rc;
    dg->off += consumed;
    return DTLCP_OK;
}

/* ================= 消息标志 ================= */
#define HF_RESUME   0x01
#define HF_PSK      0x02
#define HF_KEYSHARE 0x04
#define HF_EARLY    0x08

static size_t build_client_hello(dtlcp_ctx *c, uint8_t *out)
{
    uint8_t *p = out;
    uint8_t flags = 0;
    if (c->mode == DTLCP_MODE_RESUME) flags |= HF_RESUME;
    if (c->mode == DTLCP_MODE_PSK)    flags |= HF_PSK;
    if (c->mode == DTLCP_MODE_FULL)   flags |= HF_KEYSHARE;
    if (c->mode == DTLCP_MODE_PSK && c->use_early) flags |= HF_EARLY;

    p_u16(&p, DTLCP_VERSION);
    p_bytes(&p, c->cr, 32);
    p_u8(&p, c->sid_len);
    p_bytes(&p, c->session_id, c->sid_len);
    p_u8(&p, flags);
    p_u8(&p, (uint8_t)c->identity_len);
    p_bytes(&p, (const uint8_t *)c->identity, c->identity_len);
    p_u8(&p, 1);
    p_u16(&p, c->suite);
    if (flags & HF_KEYSHARE) p_bytes(&p, c->eph_pub, SMC_SM2_PUBKEY);
    return (size_t)(p - out);
}

static int parse_client_hello(dtlcp_ctx *c, const uint8_t *in, size_t len)
{
    const uint8_t *p = in, *end = in + len;
    uint8_t flags, nsuite;
    if (len < 35) return DTLCP_E_FORMAT; /* version + random + sid_len */
    if (g_u16(&p) != DTLCP_VERSION) return DTLCP_E_FORMAT;
    memcpy(c->cr, g_bytes(&p, 32), 32);
    c->sid_len = g_u8(&p);
    if (c->sid_len > DTLCP_SID_MAX || p + c->sid_len + 3 > end) return DTLCP_E_FORMAT;
    memcpy(c->session_id, g_bytes(&p, c->sid_len), c->sid_len);
    flags = g_u8(&p);
    {
        uint8_t il = g_u8(&p);
        if (il > DTLCP_ID_MAX || p + il + 1 > end) return DTLCP_E_FORMAT;
        memcpy(c->identity, g_bytes(&p, il), il);
        c->identity_len = il;
    }
    nsuite = g_u8(&p);
    if (nsuite == 0 || p + 2u * nsuite > end) return DTLCP_E_FORMAT;
    c->suite = g_u16(&p);
    while (--nsuite) (void)g_u16(&p);
    /* 服务端白名单：未知套件、或与本端配置不符一律拒绝（防套件降级/混淆） */
    if (!suite_known(c->suite) || c->suite != c->suite_cfg) return DTLCP_E_NOSUPPORT;
    if (flags & HF_KEYSHARE) {
        if (p + SMC_SM2_PUBKEY > end) return DTLCP_E_FORMAT;
        memcpy(c->peer_eph_pub, g_bytes(&p, SMC_SM2_PUBKEY), SMC_SM2_PUBKEY);
    }
    if (p != end) return DTLCP_E_FORMAT;
    c->mode = (flags & HF_PSK) ? DTLCP_MODE_PSK :
              (flags & HF_RESUME) ? DTLCP_MODE_RESUME : DTLCP_MODE_FULL;
    c->use_early = (flags & HF_EARLY) ? 1 : 0;
    return DTLCP_OK;
}

static size_t build_server_hello(dtlcp_ctx *c, uint8_t *out)
{
    uint8_t *p = out;
    p_u16(&p, DTLCP_VERSION);
    p_bytes(&p, c->sr, 32);
    p_u8(&p, c->sid_len);
    p_bytes(&p, c->session_id, c->sid_len);
    p_u16(&p, c->suite);
    p_u8(&p, (uint8_t)(c->mode == DTLCP_MODE_RESUME ? HF_RESUME :
                       c->mode == DTLCP_MODE_PSK ? HF_PSK : HF_KEYSHARE));
    if (c->mode == DTLCP_MODE_FULL) p_bytes(&p, c->eph_pub, SMC_SM2_PUBKEY);
    return (size_t)(p - out);
}

static int parse_server_hello(dtlcp_ctx *c, const uint8_t *in, size_t len)
{
    const uint8_t *p = in, *end = in + len;
    uint8_t flags, expected;
    if (len < 38) return DTLCP_E_FORMAT; /* version + random + sid_len + suite + flags */
    if (g_u16(&p) != DTLCP_VERSION) return DTLCP_E_FORMAT;
    memcpy(c->sr, g_bytes(&p, 32), 32);
    c->sid_len = g_u8(&p);
    if (c->sid_len > DTLCP_SID_MAX || p + c->sid_len + 3 > end) return DTLCP_E_FORMAT;
    memcpy(c->session_id, g_bytes(&p, c->sid_len), c->sid_len);
    {
        uint16_t offered = c->suite;
        uint16_t chosen = g_u16(&p);
        if (chosen != offered) return DTLCP_E_NOSUPPORT;   /* 拒绝降级 */
        c->suite = chosen;
    }
    flags = g_u8(&p);
    expected = (uint8_t)(c->mode == DTLCP_MODE_FULL ? HF_KEYSHARE :
                         c->mode == DTLCP_MODE_RESUME ? HF_RESUME : HF_PSK);
    if (flags != expected) return DTLCP_E_NOSUPPORT;
    if (c->mode == DTLCP_MODE_FULL) {
        if (p + SMC_SM2_PUBKEY > end) return DTLCP_E_FORMAT;
        memcpy(c->peer_eph_pub, g_bytes(&p, SMC_SM2_PUBKEY), SMC_SM2_PUBKEY);
    }
    return p == end ? DTLCP_OK : DTLCP_E_FORMAT;
}

/* SKE 签名摘要：e = SM3(ZA_signer ‖ "DTLCP-SKE" ‖ cr ‖ sr ‖ c_eph ‖ s_eph) */
static void ske_hash(const dtlcp_ctx *c,
                     const uint8_t signer_pub[SMC_SM2_PUBKEY],
                     const char *identity, size_t idlen,
                     uint8_t e[SMC_SM3_DIGEST])
{
    static const char tag[9] = {'D','T','L','C','P','-','S','K','E'};
    uint8_t za[SMC_SM3_DIGEST];
    sm3_ctx s;
    sm2_compute_za(za, identity, idlen, signer_pub);
    sm3_init(&s);
    sm3_update(&s, za, 32);
    sm3_update(&s, (const uint8_t *)tag, 9);
    sm3_update(&s, c->cr, 32);
    sm3_update(&s, c->sr, 32);
    sm3_update(&s, c->role == DTLCP_ROLE_CLIENT ? c->eph_pub : c->peer_eph_pub, SMC_SM2_PUBKEY);
    sm3_update(&s, c->role == DTLCP_ROLE_CLIENT ? c->peer_eph_pub : c->eph_pub, SMC_SM2_PUBKEY);
    sm3_final(&s, e);
}

/* CertVerify 摘要：e = SM3(ZA_client ‖ "DTLCP-CV" ‖ hs_hash) */
static void cv_hash(const dtlcp_ctx *c,
                    const uint8_t signer_pub[SMC_SM2_PUBKEY],
                    const char *identity, size_t idlen,
                    uint8_t e[SMC_SM3_DIGEST])
{
    static const char tag[8] = {'D','T','L','C','P','-','C','V'};
    uint8_t za[SMC_SM3_DIGEST], hv[SMC_SM3_DIGEST];
    sm3_ctx s;
    sm2_compute_za(za, identity, idlen, signer_pub);
    hs_digest(c, hv);
    sm3_init(&s);
    sm3_update(&s, za, 32);
    sm3_update(&s, (const uint8_t *)tag, 8);
    sm3_update(&s, hv, 32);
    sm3_final(&s, e);
}

/* ================= 密钥安装 ================= */
static void install_tx(dtlcp_ctx *c)
{
    if (c->role == DTLCP_ROLE_CLIENT)
        dtlcp_rec_init_tx(&c->tx, c->suite, c->keys.client_mac, c->keys.client_key, c->keys.client_iv);
    else
        dtlcp_rec_init_tx(&c->tx, c->suite, c->keys.server_mac, c->keys.server_key, c->keys.server_iv);
}

static void install_rx(dtlcp_ctx *c)
{
    if (c->role == DTLCP_ROLE_CLIENT)
        dtlcp_rec_init_rx(&c->rx, c->suite, c->keys.server_mac, c->keys.server_key, c->keys.server_iv);
    else
        dtlcp_rec_init_rx(&c->rx, c->suite, c->keys.client_mac, c->keys.client_key, c->keys.client_iv);
}

static int eph_keygen(dtlcp_ctx *c)
{
    sm2_fe d;
    sm2_pt q;
    int rc = hs_random(c, c->eph_priv, 32, "dtlcp-eph");
    if (rc != DTLCP_OK) return rc;
    sm2_fe_from_bytes(&d, c->eph_priv);
    sm2_fn_mod(&d, &d);
    sm2_fe_to_bytes(c->eph_priv, &d);
    sm2_scalarmult_base(&q, &d);
    sm2_encode_pubkey(c->eph_pub, &q);
    c->stats.sm2_ops++;
    return DTLCP_OK;
}

/* ================= FINISHED ================= */
static int send_finished(dtlcp_ctx *c)
{
    uint8_t hv[SMC_SM3_DIGEST], vd[DTLCP_VERIFY_LEN];
    hs_digest(c, hv);
    dtlcp_finished_verify(&c->keys, c->role == DTLCP_ROLE_CLIENT, hv, vd);
    return hs_send(c, &c->tx, DTLCP_HS_FINISHED, vd, sizeof vd);
}

static int check_finished(dtlcp_ctx *c, const uint8_t *body, size_t blen)
{
    uint8_t hv[SMC_SM3_DIGEST], vd[DTLCP_VERIFY_LEN], diff = 0;
    int i;
    if (blen != DTLCP_VERIFY_LEN) return DTLCP_E_FORMAT;
    hs_digest(c, hv);
    dtlcp_finished_verify(&c->keys, c->role == DTLCP_ROLE_SERVER, hv, vd);
    for (i = 0; i < DTLCP_VERIFY_LEN; i++) diff |= (uint8_t)(vd[i] ^ body[i]);
    return diff ? DTLCP_E_VERIFY : DTLCP_OK;
}

/* ================= FULL：客户端 ================= */
static int client_full(dtlcp_ctx *c)
{
    uint8_t body[DTLCP_HS_BODY_MAX], shared[32];
    uint8_t *payload = c->txbuf;
    size_t blen, plen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = hs_random(c, c->cr, 32, "client-random");
    if (rc != DTLCP_OK) return rc;
    rc = eph_keygen(c);
    if (rc != DTLCP_OK) return rc;

    blen = build_client_hello(c, body);
    rc = hs_send(c, &c->tx, DTLCP_HS_CLIENT_HELLO, body, blen);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    /* SH */
        rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_SERVER_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_server_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);
    {   /* SH 即可派生密钥（key_share 已就位） */
        if (sm2_ecdh(shared, c->eph_priv, c->peer_eph_pub) != 0) return DTLCP_E_CRYPTO;
        c->stats.sm2_ops++;
        dtlcp_derive_keys(shared, 32, c->cr, c->sr, &c->keys);
    }

    /* CERT */
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_CERTIFICATE) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = dtlcp_cert_parse(&c->peer_cert, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
        rc = dtlcp_cert_check(&c->peer_cert, &c->ca_cert);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);

    /* SKE */
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        uint8_t e[SMC_SM3_DIGEST];
        const uint8_t *sig;
        if (g_u8(&p) != DTLCP_HS_SERVER_KEY_EX) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        if (g_u8(&p) != 0x01) return DTLCP_E_NOSUPPORT;
        sig = g_bytes(&p, SMC_SM2_SIG);
        ske_hash(c, c->peer_cert.pubkey,
                 (const char *)c->peer_cert.subject, c->peer_cert.subject_len, e);
        if (sm2_verify(sig, e, c->peer_cert.pubkey) != 0) return DTLCP_E_VERIFY;
        c->stats.sm2_ops += 2;
    }
    hs_absorb(c, payload, plen);

    /* CCS → 切换 rx */
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_CCS) return DTLCP_E_STATE;
    install_rx(c);

    /* FIN */
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_FINISHED) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = check_finished(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
        hs_absorb(c, payload, plen);   /* 链式 transcript：对端 Finished 计入本端摘要 */
    }

    /* flight3：[Cert+CV] + CCS + FIN */
    if (c->verify_peer) {
        int clen = dtlcp_cert_encode(&c->cert, body, sizeof body);
        if (clen < 0) return clen;
        rc = hs_send(c, &c->tx, DTLCP_HS_CERTIFICATE, body, (size_t)clen);
        if (rc != DTLCP_OK) return rc;
        {
            uint8_t e[SMC_SM3_DIGEST], sv[SMC_SM2_SIG], rnd[SMC_SM2_BYTES];
            cv_hash(c, c->cert.pubkey, (const char *)c->cert.subject, c->cert.subject_len, e);
            rc = hs_random(c, rnd, sizeof rnd, "sign-rnd");
            if (rc != DTLCP_OK) return rc;
            if (sm2_sign(sv, e, c->privkey, rnd) != 0) return DTLCP_E_CRYPTO;
            c->stats.sm2_ops++;
            body[0] = 0x01;
            memcpy(body + 1, sv, SMC_SM2_SIG);
            rc = hs_send(c, &c->tx, 63 /* cert_verify */, body, 1 + SMC_SM2_SIG);
            if (rc != DTLCP_OK) return rc;
        }
    }
    {
        uint8_t ccs = 0x01;
        rc = rec_send(c, &c->tx, DTLCP_RT_CCS, &ccs, 1);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    rc = send_finished(c);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;
    save_session(c);
    c->established = 1;
    return DTLCP_OK;
}

/* ================= FULL：服务端 ================= */
static int server_full(dtlcp_ctx *c)
{
    uint8_t body[DTLCP_HS_BODY_MAX], shared[32];
    uint8_t *payload = c->txbuf, *packet = c->rxbuf;
    size_t blen, plen, off = 0, mlen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_CLIENT_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_client_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);

    rc = hs_random(c, c->sr, 32, "server-random");
    if (rc != DTLCP_OK) return rc;
    rc = hs_random(c, c->session_id, 8, "server-sid");
    if (rc != DTLCP_OK) return rc;
    c->sid_len = 8;
    rc = eph_keygen(c);
    if (rc != DTLCP_OK) return rc;
    if (sm2_ecdh(shared, c->eph_priv, c->peer_eph_pub) != 0) return DTLCP_E_CRYPTO;
    c->stats.sm2_ops++;
    dtlcp_derive_keys(shared, 32, c->cr, c->sr, &c->keys);

    /* SH */
    blen = build_server_hello(c, body);
    mlen = hs_pack(packet + off, DTLCP_HS_SERVER_HELLO, body, blen);
    hs_absorb(c, packet + off, mlen);
    rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
    if (rc != DTLCP_OK) return rc;

    /* CERT */
    {
        int clen = dtlcp_cert_encode(&c->cert, body, sizeof body);
        if (clen < 0) return clen;
        mlen = hs_pack(packet + off, DTLCP_HS_CERTIFICATE, body, (size_t)clen);
        hs_absorb(c, packet + off, mlen);
        rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }

    /* SKE（签名绑定双方 key_share） */
    {
        uint8_t e[SMC_SM3_DIGEST], sv[SMC_SM2_SIG], rnd[SMC_SM2_BYTES];
        ske_hash(c, c->cert.pubkey, (const char *)c->cert.subject, c->cert.subject_len, e);
        rc = hs_random(c, rnd, sizeof rnd, "sign-rnd");
        if (rc != DTLCP_OK) return rc;
        if (sm2_sign(sv, e, c->privkey, rnd) != 0) return DTLCP_E_CRYPTO;
        c->stats.sm2_ops++;
        body[0] = 0x01;
        memcpy(body + 1, sv, SMC_SM2_SIG);
        mlen = hs_pack(packet + off, DTLCP_HS_SERVER_KEY_EX, body, 1 + SMC_SM2_SIG);
        hs_absorb(c, packet + off, mlen);
        rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }

    /* CCS + FIN（切换 tx 密钥） */
    {
        uint8_t ccs = 0x01;
        rc = rec_append(c, &c->tx, DTLCP_RT_CCS, &ccs, 1, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    {
        uint8_t hv[SMC_SM3_DIGEST], vd[DTLCP_VERIFY_LEN];
        hs_digest(c, hv);
        dtlcp_finished_verify(&c->keys, 0, hv, vd);
        mlen = hs_pack(packet + off, DTLCP_HS_FINISHED, vd, sizeof vd);
        hs_absorb(c, packet + off, mlen);   /* 自身 Finished 计入 transcript（链式） */
        rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    rc = io_send(c, packet, off);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    /* flight3：[CERT + CV] + CCS + FIN [+ data] */
    {
        int saw_ccs = 0, saw_cert = 0, saw_cv = 0;
        for (;;) {
            rc = dg_recv(c, &dg);
            if (rc != DTLCP_OK) return rc;
            c->stats.hs_flights++;
            while (dg.off < dg.len) {
                rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
                if (rc != DTLCP_OK) return rc;
                if (type == DTLCP_RT_CCS) {
                    install_rx(c);
                    saw_ccs = 1;
                } else if (type == DTLCP_RT_HANDSHAKE) {
                    const uint8_t *p = payload;
                    uint8_t hst = g_u8(&p);
                    (void)g_u24(&p);
                    if (hst == DTLCP_HS_CERTIFICATE) {
                        rc = dtlcp_cert_parse(&c->peer_cert, p, plen - 4);
                        if (rc != DTLCP_OK) return rc;
                        rc = dtlcp_cert_check(&c->peer_cert, &c->ca_cert);
                        if (rc != DTLCP_OK) return rc;
                        /* 身份绑定：ClientHello 声明的 identity 必须与客户端证书
                           subject 一致；否则“基于 ZA 的身份绑定”只绑定了证书自身，
                           攻击者可拿自己的证书声明受害者身份。 */
                        if (c->identity_len == 0 ||
                            c->peer_cert.subject_len != c->identity_len ||
                            memcmp(c->peer_cert.subject, c->identity, c->identity_len) != 0)
                            return DTLCP_E_VERIFY;
                        saw_cert = 1;
                        hs_absorb(c, payload, plen);
                    } else if (hst == 63) {   /* cert_verify */
                        uint8_t e[SMC_SM3_DIGEST];
                        if (g_u8(&p) != 0x01) return DTLCP_E_NOSUPPORT;
                        cv_hash(c, c->peer_cert.pubkey,
                                (const char *)c->peer_cert.subject, c->peer_cert.subject_len, e);
                        if (sm2_verify(g_bytes(&p, SMC_SM2_SIG), e, c->peer_cert.pubkey) != 0)
                            return DTLCP_E_VERIFY;
                        saw_cv = 1;
                        c->stats.sm2_ops += 2;
                        hs_absorb(c, payload, plen);
                    } else if (hst == DTLCP_HS_FINISHED) {
                        if (c->verify_peer && !(saw_cert && saw_cv)) return DTLCP_E_VERIFY;
                        rc = check_finished(c, p, plen - 4);
                        if (rc != DTLCP_OK) return rc;
                        if (saw_ccs) { save_session(c); c->established = 1; return DTLCP_OK; }
                    }
                } else if (type == DTLCP_RT_APPDATA && saw_ccs) {
                    if (plen <= sizeof c->pending) {
                        memcpy(c->pending, payload, plen);
                        c->pending_len = plen;
                    }
                }
            }
        }
    }
}

/* ================= RESUME ================= */
static int client_resume(dtlcp_ctx *c)
{
    uint8_t body[DTLCP_HS_BODY_MAX];
    uint8_t *payload = c->txbuf;
    size_t blen, plen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = hs_random(c, c->cr, 32, "client-random");
    if (rc != DTLCP_OK) return rc;
    memcpy(c->session_id, c->session.session_id, c->session.sid_len);
    c->sid_len = c->session.sid_len;

    blen = build_client_hello(c, body);
    rc = hs_send(c, &c->tx, DTLCP_HS_CLIENT_HELLO, body, blen);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_SERVER_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_server_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);
    dtlcp_expand_keys(c->session.master, c->cr, c->sr, &c->keys);

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_CCS) return DTLCP_E_STATE;
    install_rx(c);

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_FINISHED) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = check_finished(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
        hs_absorb(c, payload, plen);   /* 链式 transcript：对端 Finished 计入 */
    }
    {
        uint8_t ccs = 0x01;
        rc = rec_send(c, &c->tx, DTLCP_RT_CCS, &ccs, 1);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    rc = send_finished(c);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;
    save_session(c);
    c->established = 1;
    return DTLCP_OK;
}

static int server_resume(dtlcp_ctx *c, const dtlcp_session *hit)
{
    uint8_t body[DTLCP_HS_BODY_MAX];
    uint8_t *payload = c->txbuf, *packet = c->rxbuf;
    size_t blen, plen, off = 0, mlen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_CLIENT_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_client_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);

    if (!hit || !hit->valid) return DTLCP_E_STATE;
    if (hit->sid_len != c->sid_len ||
        memcmp(hit->session_id, c->session_id, c->sid_len) != 0) return DTLCP_E_VERIFY;
    rc = hs_random(c, c->sr, 32, "server-random");
    if (rc != DTLCP_OK) return rc;
    dtlcp_expand_keys(hit->master, c->cr, c->sr, &c->keys);

    blen = build_server_hello(c, body);
    mlen = hs_pack(packet + off, DTLCP_HS_SERVER_HELLO, body, blen);
    hs_absorb(c, packet + off, mlen);
    rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
    if (rc != DTLCP_OK) return rc;

    {
        uint8_t ccs = 0x01;
        rc = rec_append(c, &c->tx, DTLCP_RT_CCS, &ccs, 1, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    {
        uint8_t hv[SMC_SM3_DIGEST], vd[DTLCP_VERIFY_LEN];
        hs_digest(c, hv);
        dtlcp_finished_verify(&c->keys, 0, hv, vd);
        mlen = hs_pack(packet + off, DTLCP_HS_FINISHED, vd, sizeof vd);
        hs_absorb(c, packet + off, mlen);   /* 自身 Finished 计入 transcript（链式） */
        rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    rc = io_send(c, packet, off);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    /* flight3 可能跨多个数据报（CCS 与 FIN 分开发出），持续接收至建立 */
    {
        int saw_ccs = 0;   /* 跨数据报保持：CCS 只出现一次但 FIN 可能后到 */
        for (;;) {
        rc = dg_recv(c, &dg);
        if (rc != DTLCP_OK) return rc;
        c->stats.hs_flights++;
        while (dg.off < dg.len) {
            rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
            if (rc != DTLCP_OK) return rc;
            if (type == DTLCP_RT_CCS) {
                install_rx(c);
                saw_ccs = 1;
            } else if (type == DTLCP_RT_HANDSHAKE) {
                const uint8_t *p = payload;
                uint8_t hst = g_u8(&p);
                (void)g_u24(&p);
                if (hst == DTLCP_HS_FINISHED) {
                    if (!saw_ccs) return DTLCP_E_STATE;
                    rc = check_finished(c, p, plen - 4);
                    if (rc != DTLCP_OK) return rc;
                    save_session(c);
                    c->established = 1;
                    return DTLCP_OK;
                }
            } else if (type == DTLCP_RT_APPDATA && saw_ccs) {
                /* 仅接受 CCS 之后的应用数据：此前记录仍是零密钥，可被伪造注入 */
                if (plen <= sizeof c->pending) {
                    memcpy(c->pending, payload, plen);
                    c->pending_len = plen;
                }
            }
        }
        if (c->established) return DTLCP_OK;
        }
    }
}

/* ================= PSK（含 0-RTT 早期数据） ================= */
static void early_keys(const dtlcp_ctx *c, uint8_t mac[32], uint8_t key[16], uint8_t iv[8])
{
    uint8_t block[56];
    dtlcp_prf(c->psk, c->psk_len, "dtlcp early data", c->cr, 32, block, sizeof block);
    memcpy(mac, block, 32);
    memcpy(key, block + 32, 16);
    memcpy(iv, block + 48, 8);
}

static int client_psk(dtlcp_ctx *c, const uint8_t *early, size_t early_len)
{
    uint8_t body[DTLCP_HS_BODY_MAX];
    uint8_t *payload = c->txbuf, *packet = c->rxbuf;
    size_t blen, plen, off = 0, mlen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = hs_random(c, c->cr, 32, "client-random");
    if (rc != DTLCP_OK) return rc;
    c->use_early = (early && early_len) ? 1 : 0;

    blen = build_client_hello(c, body);
    mlen = hs_pack(packet, DTLCP_HS_CLIENT_HELLO, body, blen);
    hs_absorb(c, packet, mlen);
    rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet, mlen, packet, &off);
    if (rc != DTLCP_OK) return rc;

    if (c->use_early) {
        uint8_t emac[32], ekey[16], eiv[8];
        early_keys(c, emac, ekey, eiv);
        /* CH 已保护完成，主 tx 可临时切为 early key；最终 install_tx 会覆盖。 */
        dtlcp_rec_init_tx(&c->tx, c->suite, emac, ekey, eiv);
        rc = rec_append(c, &c->tx, DTLCP_RT_APPDATA, early, early_len, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    rc = io_send(c, packet, off);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_SERVER_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_server_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);
    dtlcp_derive_keys(c->psk, c->psk_len, c->cr, c->sr, &c->keys);

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_CCS) return DTLCP_E_STATE;
    install_rx(c);

    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_FINISHED) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = check_finished(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
        hs_absorb(c, payload, plen);   /* 链式 transcript：对端 Finished 计入 */
    }
    {
        uint8_t ccs = 0x01;
        rc = rec_send(c, &c->tx, DTLCP_RT_CCS, &ccs, 1);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    rc = send_finished(c);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;
    c->established = 1;
    return DTLCP_OK;
}

static int server_psk(dtlcp_ctx *c)
{
    uint8_t body[DTLCP_HS_BODY_MAX];
    uint8_t *payload = c->txbuf, *packet = c->rxbuf;
    size_t blen, plen, off = 0, mlen;
    uint8_t type;
    dtlcp_datagram dg;
    int rc;

    rc = dg_recv(c, &dg);
    if (rc != DTLCP_OK) return rc;

    /* flight1 可能含：CH 记录 + 0-RTT 早期数据记录 */
    rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
    if (rc != DTLCP_OK || type != DTLCP_RT_HANDSHAKE || plen < 4) return DTLCP_E_FORMAT;
    {
        const uint8_t *p = payload;
        if (g_u8(&p) != DTLCP_HS_CLIENT_HELLO) return DTLCP_E_FORMAT;
        (void)g_u24(&p);
        rc = parse_client_hello(c, p, plen - 4);
        if (rc != DTLCP_OK) return rc;
    }
    hs_absorb(c, payload, plen);

    if (c->use_early && dg.off < dg.len) {
        uint8_t emac[32], ekey[16], eiv[8];
        early_keys(c, emac, ekey, eiv);
        /* CH 已解保护完成，主 rx 可临时切为 early key；最终 install_rx 会覆盖。 */
        dtlcp_rec_init_rx(&c->rx, c->suite, emac, ekey, eiv);
        {   /* 0-RTT 早期数据：换用 early 密钥解 */
            size_t consumed;
            rc = dtlcp_rec_unprotect(&c->rx, dg.buf + dg.off, dg.len - dg.off,
                                     c->pending, sizeof c->pending, &c->pending_len,
                                     &type, &consumed);
            if (rc != DTLCP_OK) return rc;
            dg.off += consumed;
        }
        /* 防重放：应用提供钩子即强制执行；命中重放只丢弃早期数据并计数，
           握手继续（避免重放报文变成对服务端的拒绝服务）。 */
        if (c->early_replay_check &&
            c->early_replay_check(c->early_replay_user, c->cr) != 0) {
            c->stats.replays_dropped++;
            c->pending_len = 0;
        }
    }

    rc = hs_random(c, c->sr, 32, "server-random");
    if (rc != DTLCP_OK) return rc;
    dtlcp_derive_keys(c->psk, c->psk_len, c->cr, c->sr, &c->keys);

    blen = build_server_hello(c, body);
    mlen = hs_pack(packet + off, DTLCP_HS_SERVER_HELLO, body, blen);
    hs_absorb(c, packet + off, mlen);
    rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
    if (rc != DTLCP_OK) return rc;

    {
        uint8_t ccs = 0x01;
        rc = rec_append(c, &c->tx, DTLCP_RT_CCS, &ccs, 1, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    install_tx(c);            /* CCS 之后启用新发送密钥 */
    {
        uint8_t hv[SMC_SM3_DIGEST], vd[DTLCP_VERIFY_LEN];
        hs_digest(c, hv);
        dtlcp_finished_verify(&c->keys, 0, hv, vd);
        mlen = hs_pack(packet + off, DTLCP_HS_FINISHED, vd, sizeof vd);
        hs_absorb(c, packet + off, mlen);   /* 自身 Finished 计入 transcript（链式） */
        rc = rec_append(c, &c->tx, DTLCP_RT_HANDSHAKE, packet + off, mlen, packet, &off);
        if (rc != DTLCP_OK) return rc;
    }
    rc = io_send(c, packet, off);
    if (rc != DTLCP_OK) return rc;
    c->stats.hs_flights++;

    /* flight3 可能跨多个数据报（CCS 与 FIN 分开发出），持续接收至建立 */
    {
        int saw_ccs = 0;   /* 跨数据报保持：CCS 只出现一次但 FIN 可能后到 */
        for (;;) {
        rc = dg_recv(c, &dg);
        if (rc != DTLCP_OK) return rc;
        c->stats.hs_flights++;
        while (dg.off < dg.len) {
            rc = dg_next(c, &dg, &type, payload, DTLCP_RECBUF, &plen);
            if (rc != DTLCP_OK) return rc;
            if (type == DTLCP_RT_CCS) {
                install_rx(c);
                saw_ccs = 1;
            } else if (type == DTLCP_RT_HANDSHAKE) {
                const uint8_t *p = payload;
                uint8_t hst = g_u8(&p);
                (void)g_u24(&p);
                if (hst == DTLCP_HS_FINISHED) {
                    if (!saw_ccs) return DTLCP_E_STATE;
                    rc = check_finished(c, p, plen - 4);
                    if (rc != DTLCP_OK) return rc;
                    save_session(c);
                    c->established = 1;
                    return DTLCP_OK;
                }
            } else if (type == DTLCP_RT_APPDATA && saw_ccs) {
                /* 仅接受 CCS 之后的应用数据：此前记录仍是零密钥，可被伪造注入 */
                if (plen <= sizeof c->pending) {
                    memcpy(c->pending, payload, plen);
                    c->pending_len = plen;
                }
            }
        }
        if (c->established) return DTLCP_OK;
        }
    }
}

/* ================= 会话保存（供 RESUME） ================= */
static void save_session(dtlcp_ctx *c)
{
    memcpy(c->session.master, c->keys.master, 48);
    memcpy(c->session.session_id, c->session_id, c->sid_len);
    c->session.sid_len = c->sid_len;
    c->session.suite = c->suite;
    c->session.valid = 1;
}

/* ================= 公共入口 ================= */
void dtlcp_init(dtlcp_ctx *ctx)
{
    uint8_t zero[32];
    memset(ctx, 0, sizeof *ctx);
    memset(zero, 0, sizeof zero);
    dtlcp_rec_init_tx(&ctx->tx, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, zero, zero, zero);
    dtlcp_rec_init_rx(&ctx->rx, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, zero, zero, zero);
    sm3_init(&ctx->hs_hash);
}

/* 已知套件白名单（未知/自创套件一律拒绝，避免把未知值当作 EtM 处理） */
static int suite_known(uint16_t s)
{
    return s == DTLCP_SUITE_ECDHE_SM4_CBC_SM3 ||
           s == DTLCP_SUITE_L_SM2_SM4_CTR_SM3 ||
           s == DTLCP_SUITE_L_PSK_SM4_CTR_SM3;
}

/* 配置合法性：套件已知且与模式匹配；无熵源时默认拒绝（fail-closed） */
static int cfg_check(const dtlcp_ctx *c, const dtlcp_cfg *cfg)
{
    if (!suite_known(c->suite)) return DTLCP_E_NOSUPPORT;
    if (cfg->mode == DTLCP_MODE_PSK) {
        if (c->suite != DTLCP_SUITE_L_PSK_SM4_CTR_SM3) return DTLCP_E_NOSUPPORT;
        /* 空 PSK（len=0）等价于“人尽皆知的密钥”，必须拒绝而不是静默使用 */
        if (c->psk_len == 0) return DTLCP_E_PARAM;
    } else if (c->suite == DTLCP_SUITE_L_PSK_SM4_CTR_SM3) {
        return DTLCP_E_NOSUPPORT;
    }
    if (!c->entropy_fn && !cfg->allow_deterministic_rng) return DTLCP_E_NORNG;
    return DTLCP_OK;
}

static void cfg_copy(dtlcp_ctx *c, const dtlcp_cfg *cfg, uint64_t now_us)
{
    c->role = cfg->role;
    c->mode = cfg->mode;
    c->suite = cfg->suite ? cfg->suite : DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    c->suite_cfg = c->suite;
    c->now_us = now_us;
    if (cfg->identity) {
        size_t n = strlen(cfg->identity);
        if (n > DTLCP_ID_MAX) n = DTLCP_ID_MAX;
        memcpy(c->identity, cfg->identity, n);
        c->identity_len = n;
    } else {
        memcpy(c->identity, "1234567812345678", 16);
        c->identity_len = 16;
    }
    c->cert = cfg->cert;
    c->ca_cert = cfg->ca_cert;
    memcpy(c->privkey, cfg->privkey, SMC_SM2_BYTES);
    c->verify_peer = cfg->verify_peer;
    c->entropy_fn = cfg->entropy;
    c->entropy_user = cfg->entropy_user;
    c->early_replay_check = cfg->early_replay_check;
    c->early_replay_user = cfg->early_replay_user;
    if (cfg->psk && cfg->psk_len) {
        size_t n = cfg->psk_len > DTLCP_PSK_MAX ? DTLCP_PSK_MAX : cfg->psk_len;
        memcpy(c->psk, cfg->psk, n);
        c->psk_len = n;
    }
}

int dtlcp_connect(dtlcp_ctx *ctx, const dtlcp_cfg *cfg, dtlcp_io *io, uint64_t now_us)
{
    int rc;
    dtlcp_init(ctx);
    cfg_copy(ctx, cfg, now_us);
    ctx->io = *io;
    rc = cfg_check(ctx, cfg);
    if (rc != DTLCP_OK) return rc;
    switch (cfg->mode) {
    case DTLCP_MODE_FULL:
        return client_full(ctx);
    case DTLCP_MODE_RESUME:
        if (!cfg->resume || !cfg->resume->valid) return DTLCP_E_PARAM;
        ctx->session = *cfg->resume;
        return client_resume(ctx);
    case DTLCP_MODE_PSK:
        return client_psk(ctx, cfg->early_data, cfg->early_len);
    default:
        return DTLCP_E_PARAM;
    }
}

int dtlcp_accept(dtlcp_ctx *ctx, const dtlcp_cfg *cfg, dtlcp_io *io, uint64_t now_us)
{
    int rc;
    dtlcp_init(ctx);
    cfg_copy(ctx, cfg, now_us);
    ctx->io = *io;
    rc = cfg_check(ctx, cfg);
    if (rc != DTLCP_OK) return rc;
    switch (cfg->mode) {
    case DTLCP_MODE_FULL:
        return server_full(ctx);
    case DTLCP_MODE_RESUME:
        return server_resume(ctx, cfg->resume);
    case DTLCP_MODE_PSK:
        return server_psk(ctx);
    default:
        return DTLCP_E_PARAM;
    }
}

int dtlcp_send(dtlcp_ctx *ctx, const uint8_t *data, size_t len)
{
    if (!ctx->established) return DTLCP_E_STATE;
    if (len > DTLCP_RECBUF - 77) return DTLCP_E_PARAM;   /* 记录缓冲硬上限 */
    return rec_send(ctx, &ctx->tx, DTLCP_RT_APPDATA, data, len);
}

int dtlcp_recv(dtlcp_ctx *ctx, uint8_t *buf, size_t cap)
{
    dtlcp_datagram dg;
    int rc;
    if (!ctx->established) return DTLCP_E_STATE;
    if (ctx->pending_len) {
        size_t n = ctx->pending_len < cap ? ctx->pending_len : cap;
        memcpy(buf, ctx->pending, n);
        ctx->pending_len = 0;
        ctx->stats.app_recv += n;
        return (int)n;
    }
    rc = dg_recv(ctx, &dg);
    if (rc != DTLCP_OK) return rc;
    while (dg.off < dg.len) {
        uint8_t type;
        uint8_t *payload = ctx->txbuf;
        size_t plen;
        rc = dg_next(ctx, &dg, &type, payload, DTLCP_RECBUF, &plen);
        if (rc != DTLCP_OK) return rc;
        if (type == DTLCP_RT_APPDATA) {
            size_t n = plen < cap ? plen : cap;
            memcpy(buf, payload, n);
            ctx->stats.app_recv += n;
            return (int)n;
        }
    }
    return DTLCP_E_IO;
}

int dtlcp_is_established(const dtlcp_ctx *ctx) { return ctx->established; }
void dtlcp_get_stats(const dtlcp_ctx *ctx, dtlcp_stats *st) { *st = ctx->stats; }
const dtlcp_session *dtlcp_get_session(const dtlcp_ctx *ctx) { return &ctx->session; }
