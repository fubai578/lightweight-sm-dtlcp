/*
 * udp_io.h — UDP 数据报传输抽象（demo 用；移植时替换为 6LoWPAN/LoRa 等）
 * Windows(MinGW/WinSock) 与 POSIX 双后端。
 */
#ifndef UDP_IO_H
#define UDP_IO_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET udp_sock_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int udp_sock_t;
#endif

typedef struct {
    udp_sock_t s;
    struct sockaddr_in peer;
    int has_peer;        /* 服务端：首个对端地址（recvfrom 记忆） */
    int timeout_ms;
} udp_io_ctx;

/* 平台初始化（Windows WSAStartup） */
int  udp_io_sys_init(void);
int  udp_io_bind(udp_io_ctx *c, uint16_t port);
int  udp_io_connect_peer(udp_io_ctx *c, const char *host, uint16_t port);
void udp_io_set_timeout(udp_io_ctx *c, int ms);
void udp_io_close(udp_io_ctx *c);

/* dtlcp_io 回调签名适配 */
int  udp_send_cb(void *user, const uint8_t *buf, size_t len);
int  udp_recv_cb(void *user, uint8_t *buf, size_t cap);

/* 当前时间（µs） */
uint64_t udp_now_us(void);

#endif /* UDP_IO_H */
