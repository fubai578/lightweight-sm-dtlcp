#ifndef W5500_UDP_H
#define W5500_UDP_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t mac[6], ip[4], subnet[4], gateway[4];
    uint8_t peer_ip[4];
    uint16_t local_port, peer_port;
    uint8_t socket_no;
} w5500_udp;

/* 0=成功；-2 表示 SPI 未读到 W5500 VERSIONR=0x04。 */
int w5500_udp_init(w5500_udp *u);
int w5500_udp_link_up(void);
void w5500_diag_regs(uint8_t out_mr_version_phy[3]);
unsigned w5500_cs_pin(void);
int w5500_udp_send_cb(void *user, const uint8_t *buf, size_t len);
int w5500_udp_recv_cb(void *user, uint8_t *buf, size_t cap);

#endif
