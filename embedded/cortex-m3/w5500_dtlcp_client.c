#include <string.h>
#include "smc_port.h"
#include "w5500_udp.h"
#include "dtlcp_internal.h"

typedef struct {
    uint8_t ca_priv[32], ca_pub[65];
    uint8_t gw_priv[32], gw_pub[65];
    uint8_t sn_priv[32], sn_pub[65];
    dtlcp_cert ca_cert, gw_cert, sn_cert;
} board_pki;

static dtlcp_ctx g_ctx;
static board_pki g_pki;
volatile uint32_t w5500_dtlcp_results[10];

static void puts_uart(const char *s)
{
    while (*s) smc_port_putc(*s++);
    smc_port_putc('\n');
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

static int board_entropy(void *user, uint8_t *buf, size_t len)
{
    (void)user;
    return smc_port_rng_bytes(buf, len);
}

int main(void)
{
    static const uint8_t telemetry[] = "TELEMETRY:F103:temp=23.5,hum=61";
    w5500_udp net = {
        {0x02, 0x53, 0x4d, 0x43, 0x00, 0x01},
        {169, 254, 33, 64}, {255, 255, 0, 0}, {0, 0, 0, 0},
        {169, 254, 33, 63}, 4433, 4433, 0
    };
    dtlcp_cfg cfg;
    dtlcp_io io;
    dtlcp_stats stats;
    uint8_t reply[128];
    int rc, n;

    smc_port_init();
    memset((void *)w5500_dtlcp_results, 0, sizeof w5500_dtlcp_results);
    w5500_dtlcp_results[0] = 0x44544c31u; /* DTL1 */
    w5500_dtlcp_results[1] = 1;
    puts_uart("DTLCP_BOARD_BEGIN");

    rc = w5500_udp_init(&net);
    w5500_dtlcp_results[2] = (uint32_t)rc;
    if (rc != 0 || !w5500_udp_link_up()) {
        w5500_dtlcp_results[1] = 0xE1u;
        puts_uart("DTLCP_NET_FAIL");
        for (;;) { }
    }
    puts_uart("DTLCP_NET_OK");

    pki_setup(&g_pki);
    puts_uart("DTLCP_PKI_OK");
    memset(&cfg, 0, sizeof cfg);
    cfg.role = DTLCP_ROLE_CLIENT;
    cfg.mode = DTLCP_MODE_FULL;
    cfg.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    cfg.identity = "sensor-node-01";
    cfg.cert = g_pki.sn_cert;
    memcpy(cfg.privkey, g_pki.sn_priv, 32);
    cfg.ca_cert = g_pki.ca_cert;
    cfg.verify_peer = 1;
    cfg.entropy = board_entropy;
    io.send = w5500_udp_send_cb;
    io.recv = w5500_udp_recv_cb;
    io.user = &net;

    w5500_dtlcp_results[1] = 2;
    rc = dtlcp_connect(&g_ctx, &cfg, &io, smc_port_cycles());
    w5500_dtlcp_results[3] = (uint32_t)rc;
    dtlcp_get_stats(&g_ctx, &stats);
    w5500_dtlcp_results[4] = stats.bytes_sent;
    w5500_dtlcp_results[5] = stats.bytes_recv;
    w5500_dtlcp_results[6] = (uint32_t)stats.hs_flights;
    w5500_dtlcp_results[9] = (uint32_t)stats.hs_messages;
    if (rc != DTLCP_OK) {
        w5500_dtlcp_results[1] = 0xE2u;
        puts_uart("DTLCP_HANDSHAKE_FAIL");
        for (;;) { }
    }
    puts_uart("DTLCP_HANDSHAKE_OK");

    w5500_dtlcp_results[6] = (uint32_t)stats.sm2_ops;
    rc = dtlcp_send(&g_ctx, telemetry, sizeof telemetry - 1u);
    if (rc != DTLCP_OK) {
        w5500_dtlcp_results[1] = 0xE3u;
        for (;;) { }
    }
    n = dtlcp_recv(&g_ctx, reply, sizeof reply);
    w5500_dtlcp_results[7] = (uint32_t)n;
    if (n <= 0 || n < 4 || memcmp(reply, "ACK:", 4) != 0) {
        w5500_dtlcp_results[1] = 0xE4u;
        puts_uart("DTLCP_APP_FAIL");
        for (;;) { }
    }
    w5500_dtlcp_results[1] = 3;
    w5500_dtlcp_results[8] = 1;
    puts_uart("DTLCP_APP_OK");
    for (;;) { smc_port_watchdog_feed(); }
}
