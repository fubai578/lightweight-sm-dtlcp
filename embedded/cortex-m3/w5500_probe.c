#include "smc_port.h"
#include "w5500_udp.h"

static void puts_uart(const char *s)
{
    while (*s) smc_port_putc(*s++);
    smc_port_putc('\n');
}

/* SWD result: magic/state/raw regs/init/link/cs/rx_count/tx_count/last_len. */
volatile uint32_t w5500_probe_results[11];

int main(void)
{
    w5500_udp net = {
        {0x02, 0x53, 0x4d, 0x43, 0x00, 0x01},
        {169, 254, 33, 64}, {255, 255, 0, 0}, {0, 0, 0, 0},
        {169, 254, 33, 63}, 4433, 4433, 0
    };
    uint8_t raw[3];
    uint8_t packet[256];
    int rc;
    smc_port_init();
    w5500_probe_results[0] = 0x57353530u; /* "W550" */
    w5500_probe_results[1] = 1;
    w5500_diag_regs(raw);
    w5500_probe_results[2] = raw[0];
    w5500_probe_results[3] = raw[1];
    w5500_probe_results[4] = raw[2];
    w5500_probe_results[7] = w5500_cs_pin();
    puts_uart("W5500_PROBE_BEGIN");
    rc = w5500_udp_init(&net);
    w5500_probe_results[5] = (uint32_t)rc;
    if (rc != 0) {
        w5500_probe_results[1] = 3;
        puts_uart("W5500_VERSION_OR_SPI_FAIL");
        for (;;) { }
    }
    w5500_probe_results[6] = (uint32_t)w5500_udp_link_up();
    w5500_probe_results[1] = 2;
    puts_uart("W5500_VERSION_OK");
    puts_uart(w5500_udp_link_up() ? "W5500_LINK_UP" : "W5500_LINK_DOWN");
    puts_uart("W5500_UDP_OPEN_OK");
    for (;;) {
        int n;
        w5500_probe_results[6] = (uint32_t)w5500_udp_link_up();
        n = w5500_udp_recv_cb(&net, packet, sizeof packet);
        if (n > 0) {
            w5500_probe_results[8]++;
            w5500_probe_results[10] = (uint32_t)n;
            if (w5500_udp_send_cb(&net, packet, (size_t)n) == 0)
                w5500_probe_results[9]++;
        }
    }
}

/* Bare-metal newlib stubs are not needed: this probe does not call stdio. */
