/*
 * demo_common.h — demo 共享：内置 PKI（确定性演示密钥）与遥测消息格式
 * 生产固件必须改用 TRNG 与真实证书（docs/03 §4）
 */
#ifndef DEMO_COMMON_H
#define DEMO_COMMON_H

#include <stdio.h>
#include <string.h>
#include "dtlcp_internal.h"
#include "smc/sm2.h"
#include "udp_io.h"

typedef struct {
    uint8_t ca_priv[32], ca_pub[65];
    uint8_t gw_priv[32], gw_pub[65];
    uint8_t sn_priv[32], sn_pub[65];
    dtlcp_cert ca_cert, gw_cert, sn_cert;
} demo_pki;

static void demo_pki_setup(demo_pki *p)
{
    sm2_keygen(p->ca_priv, p->ca_pub, NULL);
    dtlcp_cert_issue(&p->ca_cert, "DTLCP-ROOT-CA", 1, p->ca_pub, "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->gw_priv, p->gw_pub, NULL);
    dtlcp_cert_issue(&p->gw_cert, "sensor-gw-01", 2, p->gw_pub, "DTLCP-ROOT-CA", p->ca_priv);
    sm2_keygen(p->sn_priv, p->sn_pub, NULL);
    dtlcp_cert_issue(&p->sn_cert, "sensor-node-01", 3, p->sn_pub, "DTLCP-ROOT-CA", p->ca_priv);
}

/* 演示 PSK（32 字节固定，生产环境应安全注入） */
static void demo_psk(uint8_t psk[32])
{
    int i;
    for (i = 0; i < 32; i++) psk[i] = (uint8_t)(0x5A + i);
}

static const char *mode_name(int mode)
{
    switch (mode) {
    case DTLCP_MODE_FULL:   return "FULL";
    case DTLCP_MODE_RESUME: return "RESUME";
    case DTLCP_MODE_PSK:    return "PSK";
    default: return "?";
    }
}

static void print_stats(const char *role, int mode, const dtlcp_stats *st, uint64_t hs_us)
{
    printf("  [%s/%s] 握手 flights=%d 消息=%d 时延=%.2fms | 线上: 发=%lluB 收=%lluB | "
           "SM2=%d次 SM3=%d次 | 应用: 发=%lluB 收=%lluB\n",
           role, mode_name(mode), st->hs_flights, st->hs_messages, hs_us / 1000.0,
           st->bytes_sent, st->bytes_recv, st->sm2_ops, st->sm3_ops,
           st->app_sent, (unsigned long long)st->app_recv);
}

#endif /* DEMO_COMMON_H */
