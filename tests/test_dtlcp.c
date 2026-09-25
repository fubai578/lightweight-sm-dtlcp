/*
 * test_dtlcp.c — DTLCP 协议测试
 *   1) 证书链签发/验证/篡改
 *   2) 内存数据报管道 + 服务端线程：FULL(双向认证) / RESUME / PSK(0-RTT) 端到端
 *   3) 记录层负向：重放 / 篡改 / 截断 / 伪造 MAC / 乱序窗口
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#include "dtlcp_internal.h"

static int g_total, g_fail;
static void ok(int cond, const char *name)
{
    g_total++;
    if (!cond) { g_fail++; printf("  [FAIL] %s\n", name); }
    else       printf("  [ ok ] %s\n", name);
}

/* ================= 内存数据报管道（线程安全，带超时） ================= */
#define QCAP 32
#define QMSG 1700
typedef struct {
    uint8_t q[2][QCAP][QMSG];
    size_t  qlen[2][QCAP];
    int     head[2], tail[2], count[2];
    pthread_mutex_t mu;
    pthread_cond_t  cv;
} pipe_t;

typedef struct { pipe_t *p; int dir; } pipe_end_t;

static void pipe_init(pipe_t *p)
{
    memset(p, 0, sizeof *p);
    pthread_mutex_init(&p->mu, NULL);
    pthread_cond_init(&p->cv, NULL);
}

static int pipe_send(void *u, const uint8_t *buf, size_t len)
{
    pipe_end_t *e = (pipe_end_t *)u;
    pipe_t *p = e->p;
    if (len > QMSG) return -1;
    pthread_mutex_lock(&p->mu);
    if (p->count[e->dir] >= QCAP) { pthread_mutex_unlock(&p->mu); return -1; }
    memcpy(p->q[e->dir][p->tail[e->dir]], buf, len);
    p->qlen[e->dir][p->tail[e->dir]] = len;
    p->tail[e->dir] = (p->tail[e->dir] + 1) % QCAP;
    p->count[e->dir]++;
    pthread_cond_broadcast(&p->cv);
    pthread_mutex_unlock(&p->mu);
    return 0;
}

static int pipe_recv(void *u, uint8_t *buf, size_t cap)
{
    pipe_end_t *e = (pipe_end_t *)u;
    pipe_t *p = e->p;
    int src = 1 - e->dir;
    size_t n;
    int waits = 0;

    pthread_mutex_lock(&p->mu);
    while (p->count[src] == 0) {
        struct timeval tv;
        struct timespec ts;
        gettimeofday(&tv, NULL);
        ts.tv_sec = tv.tv_sec;
        ts.tv_nsec = (long)tv.tv_usec * 1000 + 50 * 1000 * 1000;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        pthread_cond_timedwait(&p->cv, &p->mu, &ts);
        if (p->count[src] == 0 && ++waits > 40) {
            pthread_mutex_unlock(&p->mu);
            return 0;   /* ~2s 超时 */
        }
    }
    n = p->qlen[src][p->head[src]];
    if (n > cap) n = cap;
    memcpy(buf, p->q[src][p->head[src]], n);
    p->head[src] = (p->head[src] + 1) % QCAP;
    p->count[src]--;
    pthread_mutex_unlock(&p->mu);
    return (int)n;
}

/* ================= PKI ================= */
typedef struct {
    uint8_t ca_priv[32], ca_pub[65];
    uint8_t srv_priv[32], srv_pub[65];
    uint8_t cli_priv[32], cli_pub[65];
    dtlcp_cert ca_cert, srv_cert, cli_cert;
} pki_t;

static void pki_setup(pki_t *p)
{
    sm2_keygen(p->ca_priv, p->ca_pub, NULL);
    dtlcp_cert_issue(&p->ca_cert, "DTLCP-ROOT-CA", 1, p->ca_pub, "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->srv_priv, p->srv_pub, NULL);
    dtlcp_cert_issue(&p->srv_cert, "sensor-gw-01", 2, p->srv_pub, "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->cli_priv, p->cli_pub, NULL);
    dtlcp_cert_issue(&p->cli_cert, "sensor-node-01", 3, p->cli_pub, "DTLCP-ROOT-CA", p->ca_priv);
}

/* ================= 服务端线程 ================= */
typedef struct {
    dtlcp_cfg     cfg;
    pipe_end_t    end;
    uint64_t      now;
    int           rc;
    int           echo_count;
    dtlcp_ctx     ctx;
    dtlcp_session session_out;
} server_job_t;

static void *server_thread(void *arg)
{
    server_job_t *j = (server_job_t *)arg;
    dtlcp_io io;
    io.send = pipe_send;
    io.recv = pipe_recv;
    io.user = &j->end;
    j->rc = dtlcp_accept(&j->ctx, &j->cfg, &io, j->now);
    if (j->rc == 0 && j->echo_count) {
        int i;
        for (i = 0; i < j->echo_count; i++) {
            uint8_t buf[600];
            int n = dtlcp_recv(&j->ctx, buf, sizeof buf);
            if (n > 0) {
                if (dtlcp_send(&j->ctx, buf, (size_t)n) != 0) { j->rc = -100; break; }
            } else if (j->ctx.pending_len == 0) {
                j->rc = -101;
                break;
            }
        }
    }
    if (j->rc == 0) j->session_out = *dtlcp_get_session(&j->ctx);
    return NULL;
}

/* ================= 端到端通用驱动 ================= */
typedef struct {
    int mode;
    int suite;
    int verify_peer;
    const uint8_t *psk; size_t psk_len;
    const uint8_t *early; size_t early_len;
    const dtlcp_session *cli_sess;
    const dtlcp_session *srv_sess;
    const char *msg;
    int check_early;
} mode_cfg_t;

static void run_e2e(const char *name, const pki_t *pki, const mode_cfg_t *mc)
{
    pipe_t pipe;
    server_job_t job;
    pthread_t th;
    dtlcp_ctx cctx;
    dtlcp_cfg ccfg;
    dtlcp_io cio;
    pipe_end_t cend = { &pipe, 0 };
    uint8_t rx[600];
    int n, rc;
    size_t mlen = strlen(mc->msg);

    pipe_init(&pipe);
    memset(&job, 0, sizeof job);
    job.end.p = &pipe;
    job.end.dir = 1;
    job.now = 1000;
    job.echo_count = mc->check_early ? 2 : 1;

    memset(&job.cfg, 0, sizeof job.cfg);
    job.cfg.role = DTLCP_ROLE_SERVER;
    job.cfg.mode = mc->mode;
    job.cfg.suite = (uint16_t)mc->suite;
    job.cfg.identity = "sensor-gw-01";
    job.cfg.cert = pki->srv_cert;
    memcpy(job.cfg.privkey, pki->srv_priv, 32);
    job.cfg.ca_cert = pki->ca_cert;
    job.cfg.verify_peer = mc->verify_peer;
    job.cfg.psk = mc->psk;
    job.cfg.psk_len = mc->psk_len;
    job.cfg.resume = (dtlcp_session *)mc->srv_sess;

    memset(&ccfg, 0, sizeof ccfg);
    ccfg.role = DTLCP_ROLE_CLIENT;
    ccfg.mode = mc->mode;
    ccfg.suite = (uint16_t)mc->suite;
    ccfg.identity = "sensor-node-01";
    ccfg.cert = pki->cli_cert;
    memcpy(ccfg.privkey, pki->cli_priv, 32);
    ccfg.ca_cert = pki->ca_cert;
    ccfg.verify_peer = mc->verify_peer;
    ccfg.psk = mc->psk;
    ccfg.psk_len = mc->psk_len;
    ccfg.early_data = mc->early;
    ccfg.early_len = mc->early_len;
    ccfg.resume = (dtlcp_session *)mc->cli_sess;

    pthread_create(&th, NULL, server_thread, &job);

    cio.send = pipe_send;
    cio.recv = pipe_recv;
    cio.user = &cend;
    rc = dtlcp_connect(&cctx, &ccfg, &cio, 2000);
    if (rc != 0) printf("  [dbg] connect rc=%d\n", rc);
    ok(rc == 0, name);

    if (rc == 0) {
        rc = dtlcp_send(&cctx, (const uint8_t *)mc->msg, (unsigned long long)mlen);
        ok(rc == 0, "  客户端发送应用数据");
        n = dtlcp_recv(&cctx, rx, sizeof rx);
        if (mc->check_early) {
            ok(n == (int)mc->early_len && memcmp(rx, mc->early, mc->early_len) == 0,
               "  0-RTT 早期数据回显（免握手时延）");
            n = dtlcp_recv(&cctx, rx, sizeof rx);
        }
        ok(n == (int)mlen && memcmp(rx, mc->msg, (unsigned long long)mlen) == 0, "  加密回显一致");
    }
    pthread_join(th, NULL);
    if (job.rc != 0) printf("  [dbg] server rc=%d\n", job.rc);
    ok(job.rc == 0, "  服务端握手/回显成功");
    if (mc->check_early) {
        ok(job.rc == 0, "  服务端完整收发（含早期数据消费）");
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    static pki_t pki;
    static dtlcp_session cli_sess, srv_sess;
    static uint8_t psk[32];
    mode_cfg_t mc;
    int i;

    printf("==== DTLCP 协议测试 ====\n");
    pki_setup(&pki);
    for (i = 0; i < 32; i++) psk[i] = (uint8_t)(0x5A + i);

    printf("证书:\n");
    ok(dtlcp_cert_check(&pki.srv_cert, &pki.ca_cert) == 0, "服务端证书链验证");
    ok(dtlcp_cert_check(&pki.cli_cert, &pki.ca_cert) == 0, "客户端证书链验证");
    {
        dtlcp_cert bad = pki.srv_cert;
        bad.pubkey[10] ^= 0x01;
        ok(dtlcp_cert_check(&bad, &pki.ca_cert) != 0, "证书公钥篡改被拒绝");
    }

    printf("端到端:\n");
    memset(&mc, 0, sizeof mc);
    mc.mode = DTLCP_MODE_FULL;
    mc.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    mc.verify_peer = 1;
    mc.msg = "hello-dtlcp-full";
    run_e2e("FULL 握手（双向认证 + 加密回显）", &pki, &mc);

    /* FULL 再跑一次拿双方会话（供 RESUME） */
    {
        pipe_t pipe;
        server_job_t job;
        pthread_t th;
        dtlcp_ctx cctx;
        dtlcp_cfg ccfg;
        dtlcp_io cio;
        pipe_end_t cend = { &pipe, 0 };
        uint8_t rx[600];
        int rc;

        pipe_init(&pipe);
        memset(&job, 0, sizeof job);
        job.end.p = &pipe; job.end.dir = 1;
        job.now = 3000; job.echo_count = 1;
        job.cfg.role = DTLCP_ROLE_SERVER;
        job.cfg.mode = DTLCP_MODE_FULL;
        job.cfg.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
        job.cfg.identity = "sensor-gw-01";
        job.cfg.cert = pki.srv_cert;
        memcpy(job.cfg.privkey, pki.srv_priv, 32);
        job.cfg.ca_cert = pki.ca_cert;
        pthread_create(&th, NULL, server_thread, &job);

        memset(&ccfg, 0, sizeof ccfg);
        ccfg.role = DTLCP_ROLE_CLIENT;
        ccfg.mode = DTLCP_MODE_FULL;
        ccfg.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
        ccfg.identity = "sensor-node-01";
        ccfg.cert = pki.cli_cert;
        memcpy(ccfg.privkey, pki.cli_priv, 32);
        ccfg.ca_cert = pki.ca_cert;
        cio.send = pipe_send; cio.recv = pipe_recv; cio.user = &cend;
        rc = dtlcp_connect(&cctx, &ccfg, &cio, 3100);
        if (rc == 0) {
            dtlcp_send(&cctx, (const uint8_t *)"sess", 4);
            (void)dtlcp_recv(&cctx, rx, sizeof rx);
            cli_sess = *dtlcp_get_session(&cctx);
        }
        pthread_join(th, NULL);
        srv_sess = job.session_out;
        ok(rc == 0 && cli_sess.valid && srv_sess.valid, "FULL 会话缓存建立");
        ok(memcmp(cli_sess.master, srv_sess.master, 48) == 0, "两端 master_secret 一致");
    }

    memset(&mc, 0, sizeof mc);
    mc.mode = DTLCP_MODE_FULL;
    mc.suite = DTLCP_SUITE_ECDHE_SM4_CBC_SM3;
    mc.verify_peer = 1;
    mc.msg = "hello-dtlcp-cbc";
    run_e2e("FULL + 标准套件 ECDHE_SM4_CBC_SM3(MtE)", &pki, &mc);

    memset(&mc, 0, sizeof mc);
    mc.mode = DTLCP_MODE_RESUME;
    mc.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    mc.cli_sess = &cli_sess;
    mc.srv_sess = &srv_sess;
    mc.msg = "hello-dtlcp-resume";
    run_e2e("RESUME 会话恢复（0 次 SM2 运算）", &pki, &mc);

    memset(&mc, 0, sizeof mc);
    mc.mode = DTLCP_MODE_PSK;
    mc.suite = DTLCP_SUITE_L_PSK_SM4_CTR_SM3;
    mc.psk = psk; mc.psk_len = 32;
    mc.early = (const uint8_t *)"early-telemetry"; mc.early_len = 16;
    mc.msg = "hello-dtlcp-psk";
    mc.check_early = 1;
    run_e2e("PSK 握手 + 0-RTT 早期数据", &pki, &mc);

    printf("记录层负向:\n");
    {
        uint8_t mac[32], key[16], ivp[8], plain[64], rec[512], out[512];
        size_t olen, plen, consumed;
        uint8_t type;
        dtlcp_rec tx, rxr;
        int rc, i;

        for (i = 0; i < 32; i++) mac[i] = (uint8_t)i;
        for (i = 0; i < 16; i++) key[i] = (uint8_t)(0x10 + i);
        for (i = 0; i < 8; i++)  ivp[i] = (uint8_t)(0x20 + i);
        for (i = 0; i < 64; i++) plain[i] = (uint8_t)(0x30 + i);

        dtlcp_rec_init_tx(&tx, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, mac, key, ivp);
        dtlcp_rec_init_rx(&rxr, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, mac, key, ivp);

        rc = dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 64, rec, &olen, 0);
        ok(rc == DTLCP_OK, "记录保护");
        rc = dtlcp_rec_unprotect(&rxr, rec, olen, out, sizeof out, &plen, &type, &consumed);
        ok(rc == DTLCP_OK && plen == 64 && memcmp(out, plain, 64) == 0, "记录解保护还原");
        rc = dtlcp_rec_unprotect(&rxr, rec, olen, out, sizeof out, &plen, &type, &consumed);
        ok(rc == DTLCP_E_REPLAY, "重放记录被拒绝");

        dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 64, rec, &olen, 0);
        rec[15] ^= 0x01;
        rc = dtlcp_rec_unprotect(&rxr, rec, olen, out, sizeof out, &plen, &type, &consumed);
        ok(rc == DTLCP_E_VERIFY, "密文篡改被拒绝");

        dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 64, rec, &olen, 0);
        rc = dtlcp_rec_unprotect(&rxr, rec, olen / 2, out, sizeof out, &plen, &type, &consumed);
        ok(rc != DTLCP_OK, "截断记录被拒绝");

        {
            uint8_t mac2[32];
            dtlcp_rec rx2;
            for (i = 0; i < 32; i++) mac2[i] = (uint8_t)(i ^ 0x55);
            dtlcp_rec_init_rx(&rx2, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, mac2, key, ivp);
            dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 64, rec, &olen, 0);
            rc = dtlcp_rec_unprotect(&rx2, rec, olen, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_E_VERIFY, "伪造 MAC 被拒绝");
        }

        {
            uint8_t r0[512], r1[512];
            size_t l0, l1;
            dtlcp_rec_init_tx(&tx, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, mac, key, ivp);
            dtlcp_rec_init_rx(&rxr, DTLCP_SUITE_L_SM2_SM4_CTR_SM3, mac, key, ivp);
            dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 32, r0, &l0, 0);
            dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 32, r1, &l1, 0);
            rc = dtlcp_rec_unprotect(&rxr, r1, l1, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_OK, "乱序记录（新序号）接受");
            rc = dtlcp_rec_unprotect(&rxr, r0, l0, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_OK, "乱序记录（旧序号）接受");
            rc = dtlcp_rec_unprotect(&rxr, r0, l0, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_E_REPLAY, "窗口内重复记录拒绝");
        }

        {
            /* CBC 多记录拼接定界：两条记录一个缓冲，逐条解 */
            uint8_t joined[1024];
            size_t off = 0, c1, co;
            dtlcp_rec_init_tx(&tx, DTLCP_SUITE_ECDHE_SM4_CBC_SM3, mac, key, ivp);
            dtlcp_rec_init_rx(&rxr, DTLCP_SUITE_ECDHE_SM4_CBC_SM3, mac, key, ivp);
            dtlcp_rec_protect(&tx, DTLCP_RT_HANDSHAKE, plain, 48, joined, &c1, 0);
            dtlcp_rec_protect(&tx, DTLCP_RT_CCS, (const uint8_t*)"", 1, joined + c1, &olen, 0);
            off = c1 + olen;
            rc = dtlcp_rec_unprotect(&rxr, joined, off, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_OK && consumed == c1 && type == DTLCP_RT_HANDSHAKE && plen == 48,
               "CBC 拼接记录1 定界解出");
            rc = dtlcp_rec_unprotect(&rxr, joined + consumed, off - consumed, out, sizeof out,
                                     &plen, &type, &co);
            ok(rc == DTLCP_OK && type == DTLCP_RT_CCS && plen == 1, "CBC 拼接记录2 定界解出");

            dtlcp_rec_init_tx(&tx, DTLCP_SUITE_ECDHE_SM4_CBC_SM3, mac, key, ivp);
            dtlcp_rec_init_rx(&rxr, DTLCP_SUITE_ECDHE_SM4_CBC_SM3, mac, key, ivp);
            dtlcp_rec_protect(&tx, DTLCP_RT_APPDATA, plain, 48, rec, &olen, 0);
            printf("[dbg] rec olen=%zu hdr=%02x%02x%02x lenfld=%02x%02x iv=%02x%02x%02x%02x; ",
                   olen, rec[0], rec[1], rec[2], rec[11], rec[12], rec[13], rec[14], rec[15], rec[16]);
            rc = dtlcp_rec_unprotect(&rxr, rec, olen, out, sizeof out, &plen, &type, &consumed);
            ok(rc == DTLCP_OK && plen == 48 && memcmp(out, plain, 48) == 0,
               "CBC 套件（MAC-then-encrypt）往返");
            rec[20] ^= 0x80;
            rc = dtlcp_rec_unprotect(&rxr, rec, olen, out, sizeof out, &plen, &type, &consumed);
            ok(rc != DTLCP_OK, "CBC 套件篡改被拒绝");
        }
    }

    printf("==== 结果：%d 项，失败 %d ====\n", g_total, g_fail);
    return g_fail ? 1 : 0;
}
