#include <string.h>
#include "smc_port.h"
#include "w5500_udp.h"
#include "dtlcp_internal.h"

#ifndef BOARD_DTLCP_MODE
#define BOARD_DTLCP_MODE DTLCP_MODE_FULL
#endif

typedef struct {
    uint8_t ca_priv[32], ca_pub[65], gw_priv[32], gw_pub[65], sn_priv[32], sn_pub[65];
    dtlcp_cert ca_cert, gw_cert, sn_cert;
} board_pki;

static dtlcp_ctx g_ctx;
static board_pki g_pki;
volatile uint32_t w5500_dtlcp_server_results[10];

static void puts_uart(const char *s)
{
    while (*s) smc_port_putc(*s++);
    smc_port_putc('\n');
}

static int board_entropy(void *user, uint8_t *buf, size_t len)
{
    (void)user;
    return smc_port_rng_bytes(buf, len);
}

static void pki_setup(board_pki *p)
{
    sm2_keygen(p->ca_priv, p->ca_pub, NULL);
    dtlcp_cert_issue(&p->ca_cert, "DTLCP-ROOT-CA", 1, p->ca_pub,
                     "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->gw_priv, p->gw_pub, NULL);
    dtlcp_cert_issue(&p->gw_cert, "sensor-gw-01", 2, p->gw_pub,
                     "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->sn_priv, p->sn_pub, NULL);
    dtlcp_cert_issue(&p->sn_cert, "sensor-node-01", 3, p->sn_pub,
                     "DTLCP-ROOT-CA", p->ca_priv);
}

static void demo_psk(uint8_t psk[32])
{
    int i;
    for (i = 0; i < 32; i++) psk[i] = (uint8_t)(0x5A + i);
}

int main(void)
{
    w5500_udp net = {
        {0x02, 0x53, 0x4d, 0x43, 0x00, 0x01},
        {169, 254, 33, 64}, {255, 255, 0, 0}, {0, 0, 0, 0},
        {0, 0, 0, 0}, 4433, 0, 0
    };
    dtlcp_cfg cfg;
    dtlcp_io io;
    dtlcp_stats stats;
    uint8_t msg[256];
    uint8_t psk[32];
    static const uint8_t ack[] = "ACK:F103-W5500-DTLCP";
    int rc, n;

    smc_port_init();
    memset((void *)w5500_dtlcp_server_results, 0, sizeof w5500_dtlcp_server_results);
    w5500_dtlcp_server_results[0] = 0x44545331u;
    w5500_dtlcp_server_results[1] = 1;
    rc = w5500_udp_init(&net);
    w5500_dtlcp_server_results[2] = (uint32_t)rc;
    if (rc != 0 || !w5500_udp_link_up()) {
        w5500_dtlcp_server_results[1] = 0xE1u;
        for (;;) { }
    }
    puts_uart("DTLCP_SERVER_NET_OK");
    pki_setup(&g_pki);
    demo_psk(psk);
    puts_uart("DTLCP_SERVER_READY");

    memset(&cfg, 0, sizeof cfg);
    cfg.role = DTLCP_ROLE_SERVER;
    cfg.mode = BOARD_DTLCP_MODE;
    cfg.suite = BOARD_DTLCP_MODE == DTLCP_MODE_PSK ? DTLCP_SUITE_L_PSK_SM4_CTR_SM3 :
                                                    DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    cfg.identity = "sensor-gw-01";
    cfg.cert = g_pki.gw_cert;
    memcpy(cfg.privkey, g_pki.gw_priv, 32);
    cfg.ca_cert = g_pki.ca_cert;
    cfg.verify_peer = 1;
    cfg.entropy = board_entropy;
    cfg.psk = psk;
    cfg.psk_len = sizeof psk;
    io.send = w5500_udp_send_cb;
    io.recv = w5500_udp_recv_cb;
    io.user = &net;

    w5500_dtlcp_server_results[1] = 2;
    rc = dtlcp_accept(&g_ctx, &cfg, &io, smc_port_cycles());
    w5500_dtlcp_server_results[3] = (uint32_t)rc;
    dtlcp_get_stats(&g_ctx, &stats);
    w5500_dtlcp_server_results[4] = stats.bytes_sent;
    w5500_dtlcp_server_results[5] = stats.bytes_recv;
    w5500_dtlcp_server_results[6] = (uint32_t)stats.hs_flights;
    if (rc != DTLCP_OK) {
        w5500_dtlcp_server_results[1] = 0xE2u;
        for (;;) { }
    }
    puts_uart("DTLCP_SERVER_HANDSHAKE_OK");
    for (int i = 0, count = g_ctx.pending_len ? 2 : 1; i < count; i++) {
        n = dtlcp_recv(&g_ctx, msg, sizeof msg);
        w5500_dtlcp_server_results[7] = (uint32_t)n;
        if (n <= 0 || dtlcp_send(&g_ctx, ack, sizeof ack - 1u) != DTLCP_OK) {
            w5500_dtlcp_server_results[1] = 0xE3u;
            for (;;) { }
        }
        w5500_dtlcp_server_results[9]++;
    }
    w5500_dtlcp_server_results[1] = 3;
    w5500_dtlcp_server_results[8] = 1;
    puts_uart("DTLCP_SERVER_APP_OK");
#if defined(BOARD_DTLCP_RESUME_SEQUENCE)
    {
        dtlcp_session saved = *dtlcp_get_session(&g_ctx);
        cfg.mode = DTLCP_MODE_RESUME;
        cfg.resume = &saved;
        w5500_dtlcp_server_results[1] = 4;
        rc = dtlcp_accept(&g_ctx, &cfg, &io, smc_port_cycles());
        w5500_dtlcp_server_results[3] = (uint32_t)rc;
        if (rc != DTLCP_OK) {
            w5500_dtlcp_server_results[1] = 0xE4u;
            for (;;) { }
        }
        n = dtlcp_recv(&g_ctx, msg, sizeof msg);
        if (n <= 0 || dtlcp_send(&g_ctx, ack, sizeof ack - 1u) != DTLCP_OK) {
            w5500_dtlcp_server_results[1] = 0xE5u;
            for (;;) { }
        }
        dtlcp_get_stats(&g_ctx, &stats);
        w5500_dtlcp_server_results[4] = stats.bytes_sent;
        w5500_dtlcp_server_results[5] = stats.bytes_recv;
        w5500_dtlcp_server_results[6] = (uint32_t)stats.sm2_ops;
        w5500_dtlcp_server_results[1] = 5;
        puts_uart("DTLCP_SERVER_RESUME_OK");
    }
#endif
    for (;;) { smc_port_watchdog_feed(); }
}
