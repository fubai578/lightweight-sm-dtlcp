/*
 * dtlcp_sensor.c — 低功耗传感终端（DTLCP 客户端 demo）
 * 用法：dtlcp_sensor --mode full|resume|psk --port 9000 --msgs 3
 *                    [--host 127.0.0.1] [--early TEXT]
 *                    [--save-session FILE] [--load-session FILE]
 */
#include <stdlib.h>
#include "demo_common.h"

size_t g_hs_sent, g_hs_recv;

static int load_session(const char *path, dtlcp_session *s)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(s, 1, sizeof *s, f);
    fclose(f);
    return (n == sizeof *s && s->valid) ? 0 : -1;
}

static int save_session(const char *path, const dtlcp_session *s)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fwrite(s, 1, sizeof *s, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    static demo_pki pki;
    static dtlcp_ctx ctx;
    dtlcp_cfg cfg;
    dtlcp_io io;
    udp_io_ctx udp;
    dtlcp_session sess;
    dtlcp_stats st;
    uint8_t psk[32];
    const char *mode_str = "full", *host = "127.0.0.1", *early = NULL;
    const char *save = NULL, *load = NULL;
    int mode = DTLCP_MODE_FULL, port = 9000, msgs = 3, i, rc;
    uint64_t t0, t1, rtt_sum = 0, rtt_max = 0;
    size_t wire_per_msg = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode_str = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--msgs") && i + 1 < argc) msgs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--host") && i + 1 < argc) host = argv[++i];
        else if (!strcmp(argv[i], "--early") && i + 1 < argc) early = argv[++i];
        else if (!strcmp(argv[i], "--save-session") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--load-session") && i + 1 < argc) load = argv[++i];
    }
    if (!strcmp(mode_str, "full")) mode = DTLCP_MODE_FULL;
    else if (!strcmp(mode_str, "resume")) mode = DTLCP_MODE_RESUME;
    else if (!strcmp(mode_str, "psk")) mode = DTLCP_MODE_PSK;
    else { printf("unknown mode %s\n", mode_str); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    udp_io_sys_init();
    demo_pki_setup(&pki);
    demo_psk(psk);
    memset(&sess, 0, sizeof sess);
    if (mode == DTLCP_MODE_RESUME && load && load_session(load, &sess) != 0) {
        printf("[sensor] 无会话缓存：%s（先以 FULL 模式建立会话）\n", load);
        return 1;
    }

    if (udp_io_connect_peer(&udp, host, (uint16_t)port) != 0) {
        printf("[sensor] socket 失败\n");
        return 1;
    }
    udp_io_set_timeout(&udp, 5000);
    io.send = udp_send_cb;
    io.recv = udp_recv_cb;
    io.user = &udp;

    memset(&cfg, 0, sizeof cfg);
    cfg.role = DTLCP_ROLE_CLIENT;
    cfg.mode = mode;
    cfg.suite = (mode == DTLCP_MODE_PSK) ? DTLCP_SUITE_L_PSK_SM4_CTR_SM3
                                         : DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    cfg.identity = "sensor-node-01";
    cfg.cert = pki.sn_cert;
    memcpy(cfg.privkey, pki.sn_priv, 32);
    cfg.ca_cert = pki.ca_cert;
    cfg.verify_peer = 1;
    cfg.allow_deterministic_rng = 1;   /* 演示程序无 TRNG：显式开启确定性 DRBG */
    cfg.psk = psk;
    cfg.psk_len = 32;
    cfg.early_data = (const uint8_t *)early;
    cfg.early_len = early ? strlen(early) : 0;
    cfg.resume = (mode == DTLCP_MODE_RESUME) ? &sess : NULL;

    printf("[sensor] 连接 %s:%d mode=%s ...\n", host, port, mode_name(mode));
    t0 = udp_now_us();
    rc = dtlcp_connect(&ctx, &cfg, &io, t0);
    t1 = udp_now_us();
    if (rc != DTLCP_OK) {
        printf("[sensor] 握手失败 rc=%d\n", rc);
        return 1;
    }
    dtlcp_get_stats(&ctx, &st);
    print_stats("SN", mode, &st, t1 - t0);
    {
        /* 记录握手阶段线上字节基线，供每消息开销统计 */
        extern size_t g_hs_sent, g_hs_recv;
        g_hs_sent = st.bytes_sent;
        g_hs_recv = st.bytes_recv;
    }
    printf("  握手开销：握手阶段 %d（发/收/发各一次数据报）| 握手线上 %lluB | SM2 运算 %d 次\n",
           st.hs_flights, (unsigned long long)st.bytes_sent + st.bytes_recv, st.sm2_ops);

    if (early && mode == DTLCP_MODE_PSK) {
        /* 0-RTT 早期数据回显 */
        uint8_t rx[256];
        int n = dtlcp_recv(&ctx, rx, sizeof rx - 1);
        if (n > 0) {
            rx[n] = 0;
            printf("  0-RTT 早期数据已送达并回显：%.32s\n", (const char *)rx);
        }
    }

    for (i = 0; i < msgs; i++) {
        char msg[128];
        uint8_t rx[256];
        int mlen, n;
        uint64_t r0, r1;
        mlen = snprintf(msg, sizeof msg, "TELEMETRY:%d:temp=23.%d,hum=6%d",
                        i, i % 10, i % 10);
        r0 = udp_now_us();
        if (dtlcp_send(&ctx, (const uint8_t *)msg, (size_t)mlen) != 0) {
            printf("[sensor] 发送失败\n");
            return 1;
        }
        n = dtlcp_recv(&ctx, rx, sizeof rx - 1);
        r1 = udp_now_us();
        if (n <= 0) { printf("[sensor] 应答超时\n"); return 1; }
        rx[n] = 0;
        rtt_sum += r1 - r0;
        if (r1 - r0 > rtt_max) rtt_max = r1 - r0;
        if (i == 0) printf("  样例：%s → %s\n", msg, (const char *)rx);
    }

    dtlcp_get_stats(&ctx, &st);
    {
        extern size_t g_hs_sent, g_hs_recv;
        size_t app_wire = 0;
        if (st.bytes_sent + st.bytes_recv > g_hs_sent + g_hs_recv)
            app_wire = st.bytes_sent + st.bytes_recv - g_hs_sent - g_hs_recv;
        wire_per_msg = app_wire / (size_t)(msgs ? msgs : 1);
    }
    printf("  应用 %d 条遥测：RTT 平均=%.2fms 最大=%.2fms | 每消息线上≈%lluB\n",
           msgs, rtt_sum / 1000.0 / msgs, rtt_max / 1000.0, (unsigned long long)wire_per_msg);
    if (save) {
        const dtlcp_session *s = dtlcp_get_session(&ctx);
        if (save_session(save, s) == 0)
            printf("  会话已缓存 → %s（可用于 RESUME）\n", save);
    }
    udp_io_close(&udp);
    return 0;
}
