#include <string.h>
#include "smc_port.h"
#include "w5500_udp.h"
#include "dtlcp_internal.h"

/* ⚠ 演示 PKI 安全边界（B7）
 * 本文件的证书/私钥由 sm2_keygen(..., NULL) 的固定种子派生：所有设备共用同一套
 * CA/网关/传感器密钥，仅用于“可复现演示”，**禁止用于生产**。
 * 生产形态：每设备独立烧录密钥与证书（或使用安全元件），并在构建时定义
 * SMC_PRODUCTION_BUILD —— 该宏会让本文件编译失败，防止固定种子 PKI 混入产品。
 */
#if defined(SMC_PRODUCTION_BUILD)
#error "固定种子演示 PKI 不得用于生产构建（生产须每设备独立烧录密钥/证书）"
#endif
#define DTLCP_DEMO_PKI_FIXED_SEED 1

#if defined(BOARD_DYNAMIC_IDENTITY)
typedef struct {
    uint8_t ca_priv[32], ca_pub[65];
    uint8_t gw_priv[32], gw_pub[65];
    uint8_t sn_priv[32], sn_pub[65];
    dtlcp_cert ca_cert, gw_cert, sn_cert;
} board_pki;

static board_pki g_pki;
#else
#include "demo_node_identity.h"
#endif
static dtlcp_ctx g_ctx;
volatile uint32_t w5500_dtlcp_results[10];
/* SWD demo mailbox: 1 requests RESUME without reset; reset clears the cache. */
volatile uint32_t w5500_dtlcp_command;
static dtlcp_session saved_session;

static void puts_uart(const char *s)
{
    while (*s) smc_port_putc(*s++);
    smc_port_putc('\n');
}

#if defined(BOARD_DYNAMIC_IDENTITY)
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

#endif

/* TIM2 at 1 kHz, measured from after smc_port_init; reset is not power-on. */
#define START_REG(a) (*(volatile uint32_t *)(a))
volatile uint32_t w5500_startup_results[7];
static uint32_t startup_ms(void) { return START_REG(0x40000024u) & 0xffffu; }
static void startup_timer(void)
{
    START_REG(0x4002101cu) |= 1u;
    START_REG(0x40000000u) = 0;
    START_REG(0x40000028u) = 7999;
    START_REG(0x4000002cu) = 65535;
    START_REG(0x40000014u) = 1;
    START_REG(0x40000024u) = 0;
    START_REG(0x40000000u) = 1;
}

static int board_entropy(void *user, uint8_t *buf, size_t len)
{
    (void)user;
    return smc_port_rng_bytes(buf, len);
}

int main(void)
{
    static const uint8_t telemetry[] = "TELEMETRY:F103:status=online";
    w5500_udp net = {
        {0x02, 0x53, 0x4d, 0x43, 0x00, 0x01},
        {169, 254, 33, 64}, {255, 255, 0, 0}, {0, 0, 0, 0},
        {169, 254, 33, 63}, 4433, 4433, 0
    };
    dtlcp_cfg cfg;
    dtlcp_io io;
    dtlcp_stats stats;
    uint8_t reply[128];
    int rc, n, index;

    smc_port_init();
    startup_timer();
    w5500_startup_results[0] = 0x53544131u;
#if defined(BOARD_DYNAMIC_IDENTITY)
    w5500_startup_results[1] = 0;
#else
    w5500_startup_results[1] = 1;
#endif
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

    w5500_startup_results[2] = startup_ms();
#if defined(BOARD_DYNAMIC_IDENTITY)
    pki_setup(&g_pki);
#endif
    w5500_startup_results[3] = startup_ms();
    puts_uart("DTLCP_PKI_OK");
    memset(&cfg, 0, sizeof cfg);
    cfg.role = DTLCP_ROLE_CLIENT;
    cfg.mode = DTLCP_MODE_FULL;
    cfg.suite = DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    cfg.identity = "sensor-node-01";
#if defined(BOARD_DYNAMIC_IDENTITY)
    cfg.cert = g_pki.sn_cert;
    memcpy(cfg.privkey, g_pki.sn_priv, 32);
    cfg.ca_cert = g_pki.ca_cert;
#else
    cfg.cert = demo_node_cert;
    memcpy(cfg.privkey, demo_node_private, 32);
    cfg.ca_cert = demo_node_ca;
#endif
    cfg.verify_peer = 1;
    cfg.entropy = board_entropy;
    io.send = w5500_udp_send_cb;
    io.recv = w5500_udp_recv_cb;
    io.user = &net;

connect_session:
    w5500_dtlcp_results[8] = 0;
    w5500_dtlcp_results[1] = 2;
    if (cfg.mode == DTLCP_MODE_FULL) w5500_startup_results[4] = startup_ms();
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
    if (cfg.mode == DTLCP_MODE_FULL) w5500_startup_results[5] = startup_ms();
    saved_session = *dtlcp_get_session(&g_ctx);
    puts_uart("DTLCP_HANDSHAKE_OK");

    w5500_dtlcp_results[6] = (uint32_t)stats.sm2_ops;
    for (index=0; index<12; index++) {
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
    if (index == 0 && cfg.mode == DTLCP_MODE_FULL) w5500_startup_results[6] = startup_ms();
    /* Presentation pacing only; independent of debugger-controlled DWT. */
    { volatile uint32_t pause; for (pause=0; pause<800000u; pause++) { } }
    }
    w5500_dtlcp_results[1] = 3;
    w5500_dtlcp_results[8] = 1;
    puts_uart("DTLCP_APP_OK");
    for (;;) {
        smc_port_watchdog_feed();
        if (w5500_dtlcp_command == 1) {
            w5500_dtlcp_command = 0;
            if (!saved_session.valid) continue;
            /* Clear stale UDP records (including gateway firewall priming)
             * before creating the next record-layer context. */
            if (w5500_udp_init(&net) != 0) {
                w5500_dtlcp_results[1] = 0xE1u;
                continue;
            }
            cfg.mode = DTLCP_MODE_RESUME;
            cfg.resume = &saved_session;
            goto connect_session;
        }
    }
}
