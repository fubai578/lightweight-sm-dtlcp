/* Minimal W5500 UDP transport for DTLCP: one socket, 2 KiB TX/RX, polling I/O. */
#include <string.h>
#include "stm32f1xx_min.h"
#include "w5500_udp.h"

#define W5_COMMON       0u
#define W5_SREG(n)      (uint8_t)(1u + 4u * (n))
#define W5_TXBUF(n)     (uint8_t)(2u + 4u * (n))
#define W5_RXBUF(n)     (uint8_t)(3u + 4u * (n))
#define W5_WRITE        0x04u

#define W5_MR           0x0000u
#define W5_GAR          0x0001u
#define W5_SUBR         0x0005u
#define W5_SHAR         0x0009u
#define W5_SIPR         0x000Fu
#define W5_PHYCFGR      0x002Eu
#define W5_VERSIONR     0x0039u

#define SN_MR           0x0000u
#define SN_CR           0x0001u
#define SN_IR           0x0002u
#define SN_SR           0x0003u
#define SN_PORT         0x0004u
#define SN_DIPR         0x000Cu
#define SN_DPORT        0x0010u
#define SN_RXBUF_SIZE   0x001Eu
#define SN_TXBUF_SIZE   0x001Fu
#define SN_TX_FSR       0x0020u
#define SN_TX_WR        0x0024u
#define SN_RX_RSR       0x0026u
#define SN_RX_RD        0x0028u

#define SN_MR_UDP       0x02u
#define SN_CR_OPEN      0x01u
#define SN_CR_CLOSE     0x10u
#define SN_CR_SEND      0x20u
#define SN_CR_RECV      0x40u
#define SN_IR_SENDOK    0x10u
#define SN_IR_TIMEOUT   0x08u
#define SOCK_UDP        0x22u
#define W5_SPIN_MAX     250000u
#define W5_RECV_SPIN_MAX 50000000u
#define W5_BUF_SIZE     2048u

static unsigned g_cs_pin = 10;

static uint8_t spi_xfer(uint8_t v)
{
    while ((SPI1->SR & SPI_SR_TXE) == 0) { }
    SPI1->DR = v;
    while ((SPI1->SR & SPI_SR_RXNE) == 0) { }
    return (uint8_t)SPI1->DR;
}

static void cs_low(void)
{
    if (g_cs_pin == 4) GPIOA->BRR = (1u << 4);
    else GPIOB->BRR = (1u << 10);
}
static void cs_high(void)
{
    while (SPI1->SR & SPI_SR_BSY) { }
    if (g_cs_pin == 4) GPIOA->BSRR = (1u << 4);
    else GPIOB->BSRR = (1u << 10);
}

static void w5_begin(uint16_t addr, uint8_t block, int write)
{
    cs_low();
    (void)spi_xfer((uint8_t)(addr >> 8));
    (void)spi_xfer((uint8_t)addr);
    (void)spi_xfer((uint8_t)((block << 3) | (write ? W5_WRITE : 0u)));
}

static uint8_t w5_read8(uint8_t block, uint16_t addr)
{
    uint8_t v;
    w5_begin(addr, block, 0);
    v = spi_xfer(0);
    cs_high();
    return v;
}

static void w5_write8(uint8_t block, uint16_t addr, uint8_t v)
{
    w5_begin(addr, block, 1);
    (void)spi_xfer(v);
    cs_high();
}

static void w5_read(uint8_t block, uint16_t addr, uint8_t *buf, size_t len)
{
    w5_begin(addr, block, 0);
    while (len--) *buf++ = spi_xfer(0);
    cs_high();
}

static void w5_write(uint8_t block, uint16_t addr, const uint8_t *buf, size_t len)
{
    w5_begin(addr, block, 1);
    while (len--) (void)spi_xfer(*buf++);
    cs_high();
}

static uint16_t w5_read16(uint8_t block, uint16_t addr)
{
    uint8_t b[2];
    w5_read(block, addr, b, 2);
    return (uint16_t)((uint16_t)b[0] << 8 | b[1]);
}

static void w5_write16(uint8_t block, uint16_t addr, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    w5_write(block, addr, b, 2);
}

static uint16_t w5_read16_stable(uint8_t block, uint16_t addr)
{
    uint16_t a, b;
    do { a = w5_read16(block, addr); b = w5_read16(block, addr); } while (a != b);
    return a;
}

static int w5_cmd(uint8_t sn, uint8_t cmd)
{
    uint32_t guard = W5_SPIN_MAX;
    w5_write8(W5_SREG(sn), SN_CR, cmd);
    while (w5_read8(W5_SREG(sn), SN_CR) != 0) if (--guard == 0) return -1;
    return 0;
}

static int w5_open_udp(w5500_udp *u)
{
    uint8_t sn = u->socket_no;
    (void)w5_cmd(sn, SN_CR_CLOSE);
    w5_write8(W5_SREG(sn), SN_MR, SN_MR_UDP);
    w5_write16(W5_SREG(sn), SN_PORT, u->local_port);
    if (w5_cmd(sn, SN_CR_OPEN) != 0) return -1;
    return w5_read8(W5_SREG(sn), SN_SR) == SOCK_UDP ? 0 : -1;
}

static void spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN | RCC_APB2ENR_IOPAEN |
                    RCC_APB2ENR_IOPBEN | RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    /* PA5 SCK/PA7 MOSI=AF-PP, PA6 MISO=pull-up input; PA4 is alternate CS.
       Pull-up makes an absent/undriven MISO read 0xFF instead of an ambiguous 0x00. */
    GPIOA->CRL = (GPIOA->CRL & ~((0xFu << 16) | (0xFu << 20) |
                                  (0xFu << 24) | (0xFu << 28))) |
                 (0x3u << 16) | (0xBu << 20) | (0x8u << 24) | (0xBu << 28);
    GPIOA->BSRR = (1u << 6);
    /* PB10 is the preferred software CS, push-pull 50MHz. */
    GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 8)) | (0x3u << 8);
    GPIOA->BSRR = (1u << 4);
    GPIOB->BSRR = (1u << 10);
    /* SPI mode 0, master, software NSS, PCLK/4 = 2 MHz at default HSI 8 MHz. */
    SPI1->CR1 = SPI_CR1_MSTR | (1u << SPI_CR1_BR_SHIFT) |
                SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_SPE;
}

int w5500_udp_init(w5500_udp *u)
{
    uint8_t sn, zero = 0, two = 2;
    uint32_t guard = W5_SPIN_MAX;
    if (!u || u->socket_no > 7) return -1;
    spi_init();
    g_cs_pin = 10;
    if (w5_read8(W5_COMMON, W5_VERSIONR) != 0x04u) {
        g_cs_pin = 4;
        if (w5_read8(W5_COMMON, W5_VERSIONR) != 0x04u) {
            g_cs_pin = 10;
            return -2;
        }
    }
    w5_write8(W5_COMMON, W5_MR, 0x80u);
    while (w5_read8(W5_COMMON, W5_MR) & 0x80u) if (--guard == 0) return -2;
    if (w5_read8(W5_COMMON, W5_VERSIONR) != 0x04u) return -2;

    w5_write(W5_COMMON, W5_GAR, u->gateway, 4);
    w5_write(W5_COMMON, W5_SUBR, u->subnet, 4);
    w5_write(W5_COMMON, W5_SHAR, u->mac, 6);
    w5_write(W5_COMMON, W5_SIPR, u->ip, 4);
    for (sn = 0; sn < 8; sn++) {
        w5_write(W5_SREG(sn), SN_RXBUF_SIZE, sn == u->socket_no ? &two : &zero, 1);
        w5_write(W5_SREG(sn), SN_TXBUF_SIZE, sn == u->socket_no ? &two : &zero, 1);
    }
    return w5_open_udp(u) == 0 ? 0 : -3;
}

int w5500_udp_link_up(void)
{
    return (w5_read8(W5_COMMON, W5_PHYCFGR) & 1u) != 0;
}

void w5500_diag_regs(uint8_t out[3])
{
    if (!out) return;
    spi_init();
    g_cs_pin = 10;
    if (w5_read8(W5_COMMON, W5_VERSIONR) != 0x04u) {
        g_cs_pin = 4;
        if (w5_read8(W5_COMMON, W5_VERSIONR) != 0x04u) g_cs_pin = 10;
    }
    out[0] = w5_read8(W5_COMMON, W5_MR);
    out[1] = w5_read8(W5_COMMON, W5_VERSIONR);
    out[2] = w5_read8(W5_COMMON, W5_PHYCFGR);
}

unsigned w5500_cs_pin(void) { return g_cs_pin; }

int w5500_udp_send_cb(void *user, const uint8_t *buf, size_t len)
{
    w5500_udp *u = (w5500_udp *)user;
    uint8_t sn, ir;
    uint16_t ptr;
    uint32_t guard = W5_SPIN_MAX;
    if (!u || !buf || len == 0 || len > W5_BUF_SIZE || u->peer_port == 0) return -1;
    sn = u->socket_no;
    while (w5_read16_stable(W5_SREG(sn), SN_TX_FSR) < len) if (--guard == 0) return -2;
    w5_write(W5_SREG(sn), SN_DIPR, u->peer_ip, 4);
    w5_write16(W5_SREG(sn), SN_DPORT, u->peer_port);
    ptr = w5_read16(W5_SREG(sn), SN_TX_WR);
    w5_write(W5_TXBUF(sn), ptr, buf, len);
    w5_write16(W5_SREG(sn), SN_TX_WR, (uint16_t)(ptr + len));
    w5_write8(W5_SREG(sn), SN_IR, SN_IR_SENDOK | SN_IR_TIMEOUT);
    if (w5_cmd(sn, SN_CR_SEND) != 0) return -3;
    guard = W5_SPIN_MAX;
    do {
        ir = w5_read8(W5_SREG(sn), SN_IR);
        if (ir & SN_IR_TIMEOUT) { w5_write8(W5_SREG(sn), SN_IR, SN_IR_TIMEOUT); return -4; }
        if (ir & SN_IR_SENDOK) { w5_write8(W5_SREG(sn), SN_IR, SN_IR_SENDOK); return 0; }
    } while (--guard);
    return -5;
}

int w5500_udp_recv_cb(void *user, uint8_t *buf, size_t cap)
{
    w5500_udp *u = (w5500_udp *)user;
    uint8_t head[8], sn;
    uint16_t ptr, len, available;
    uint32_t guard = W5_RECV_SPIN_MAX;
    if (!u || !buf) return -1;
    sn = u->socket_no;
    do {
        available = w5_read16_stable(W5_SREG(sn), SN_RX_RSR);
        if (available >= 8) break;
        if (w5_read8(W5_SREG(sn), SN_SR) != SOCK_UDP) return -2;
    } while (--guard);
    if (available < 8) return 0;
    ptr = w5_read16(W5_SREG(sn), SN_RX_RD);
    w5_read(W5_RXBUF(sn), ptr, head, sizeof head);
    len = (uint16_t)((uint16_t)head[6] << 8 | head[7]);
    if (len > W5_BUF_SIZE) {
        (void)w5_open_udp(u); /* 丢弃不可能装入 2 KiB RX buffer 的畸形帧 */
        return -4;
    }
    guard = W5_RECV_SPIN_MAX;
    while (available < (uint16_t)(8u + len)) {
        if (--guard == 0) return 0;
        available = w5_read16_stable(W5_SREG(sn), SN_RX_RSR);
    }
    memcpy(u->peer_ip, head, 4);
    u->peer_port = (uint16_t)((uint16_t)head[4] << 8 | head[5]);
    if (len <= cap) w5_read(W5_RXBUF(sn), (uint16_t)(ptr + 8u), buf, len);
    w5_write16(W5_SREG(sn), SN_RX_RD, (uint16_t)(ptr + 8u + len));
    if (w5_cmd(sn, SN_CR_RECV) != 0) return -3;
    return len <= cap ? (int)len : -5;
}
