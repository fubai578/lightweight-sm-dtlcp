/*
 * udp_io.c — UDP 传输实现（WinSock / POSIX）
 */
#include <stdio.h>
#include <string.h>
#include "udp_io.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

int udp_io_sys_init(void)
{
#ifdef _WIN32
    static int done = 0;
    if (!done) {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
        done = 1;
    }
#endif
    return 0;
}

int udp_io_bind(udp_io_ctx *c, uint16_t port)
{
    struct sockaddr_in a;
    memset(c, 0, sizeof *c);
    c->timeout_ms = 10000;
    c->s = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (c->s == INVALID_SOCKET) return -1;
#else
    if (c->s < 0) return -1;
#endif
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(port);
    if (bind(c->s, (struct sockaddr *)&a, sizeof a) != 0) return -2;
    return 0;
}

int udp_io_connect_peer(udp_io_ctx *c, const char *host, uint16_t port)
{
    memset(c, 0, sizeof *c);
    c->timeout_ms = 10000;
    c->s = socket(AF_INET, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (c->s == INVALID_SOCKET) return -1;
#else
    if (c->s < 0) return -1;
#endif
    memset(&c->peer, 0, sizeof c->peer);
    c->peer.sin_family = AF_INET;
    c->peer.sin_port = htons(port);
    c->peer.sin_addr.s_addr = inet_addr(host);
    c->has_peer = 1;
    return 0;
}

int udp_io_set_peer(udp_io_ctx *c, const char *host, uint16_t port)
{
    memset(&c->peer, 0, sizeof c->peer);
    c->peer.sin_family = AF_INET;
    c->peer.sin_port = htons(port);
    c->peer.sin_addr.s_addr = inet_addr(host);
    if (c->peer.sin_addr.s_addr == INADDR_NONE) return -1;
    c->has_peer = 1;
    return 0;
}

void udp_io_set_timeout(udp_io_ctx *c, int ms)
{
    c->timeout_ms = ms;
#ifdef _WIN32
    setsockopt(c->s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&ms, sizeof ms);
#else
    {
        struct timeval tv;
        tv.tv_sec = ms / 1000;
        tv.tv_usec = (ms % 1000) * 1000;
        setsockopt(c->s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
#endif
}

void udp_io_close(udp_io_ctx *c)
{
#ifdef _WIN32
    closesocket(c->s);
#else
    close(c->s);
#endif
}

int udp_send_cb(void *user, const uint8_t *buf, size_t len)
{
    udp_io_ctx *c = (udp_io_ctx *)user;
    int n;
    if (!c->has_peer) return -1;
    n = (int)sendto(c->s, (const char *)buf, (int)len, 0,
                    (struct sockaddr *)&c->peer, sizeof c->peer);
    return (n == (int)len) ? 0 : -1;
}

int udp_recv_cb(void *user, uint8_t *buf, size_t cap)
{
    udp_io_ctx *c = (udp_io_ctx *)user;
    struct sockaddr_in from;
    int flen = sizeof from;
    int n = (int)recvfrom(c->s, (char *)buf, (int)cap, 0,
                          (struct sockaddr *)&from, &flen);
    if (n <= 0) return 0;   /* 超时/错误 → 0（dtlcp 视为无数据） */
    if (!c->has_peer) {
        c->peer = from;
        c->has_peer = 1;
    }
    return n;
}

uint64_t udp_now_us(void)
{
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    {
        uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
        return t / 10;   /* 100ns → µs，纪元偏移不影响差值 */
    }
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000 + (uint64_t)tv.tv_usec;
#endif
}
