/*
 * dtlcp_gateway.c — 物联网网关（DTLCP 服务端 demo）
 * 用法：dtlcp_gateway --mode full|resume|psk --port 9000 --msgs 3
 *                    [--save-session FILE] [--load-session FILE]
 */
#include <stdlib.h>
#include "demo_common.h"
#ifdef _WIN32
#include <windows.h>
#endif

static int events_enabled;
static unsigned message_number;
static uint64_t first_packet_us;
static uint8_t captured_record[DTLCP_RECBUF];
static size_t captured_length;
static const char *security_control;
static unsigned security_request;
static void state_event(const char *,const char *,int);
static void security_poll(dtlcp_ctx *ctx)
{
    FILE *f;
    unsigned request;
    char action[16];
    uint8_t input[DTLCP_RECBUF],output[DTLCP_RECBUF],type=0;
    size_t olen=0,consumed=0;
    dtlcp_rec before;
    int rc,expected,unchanged;
    if(!security_control || !captured_length) return;
    f=fopen(security_control,"r");
    if(!f) return;
    if(fscanf(f,"%u %15s",&request,action)!=2) {fclose(f);return;}
    fclose(f);
    if(request<=security_request) return;
    security_request=request;
    if(strcmp(action,"tamper") && strcmp(action,"replay")) return;
    memcpy(input,captured_record,captured_length);
    if(!strcmp(action,"tamper")) input[captured_length-1]^=1; /* MAC bit */
    expected=!strcmp(action,"tamper")?DTLCP_E_VERIFY:DTLCP_E_REPLAY;
    before=ctx->rx;
    rc=dtlcp_rec_unprotect(&ctx->rx,input,captured_length,output,sizeof output,&olen,&type,&consumed);
    unchanged=memcmp(&before,&ctx->rx,sizeof before)==0;
    if(events_enabled) printf("@dtlcp {\"type\":\"security_result\",\"role\":\"gateway\",\"request_id\":%u,\"test\":\"%s\",\"code\":%d,\"expected_code\":%d,\"passed\":%s,\"state_unchanged\":%s,\"bytes\":%lu,\"text\":\"local captured-record injection; protocol return code\"}\n",request,action,rc,expected,(rc==expected&&unchanged)?"true":"false",unchanged?"true":"false",(unsigned long)captured_length);
    if(rc!=expected || !unchanged) {
        state_event("error","security test failed; stopping gateway",rc);
        exit(2);
    }
}

static void event(const char *type, const uint8_t *data, size_t len, int code)
{
    size_t j;
    if (!events_enabled) return;
    printf("@dtlcp {\"type\":\"%s\",\"role\":\"gateway\",\"code\":%d,\"message_id\":%u,\"bytes\":%lu,\"text\":\"",type,code,message_number,(unsigned long)len);
    for(j=0;j<len;j++) {
        unsigned c=data[j];
        if(c=='"' || c=='\\') {putchar('\\');putchar(c);}
        else if(c<32 || c>=127) printf("\\u%04x",c);
        else putchar(c);
    }
    puts("\"}");
}
static void state_event(const char *type,const char *text,int code)
{event(type,(const uint8_t *)text,strlen(text),code);}
static int observed_send(void *user,const uint8_t *buf,size_t len)
{
    int rc=udp_send_cb(user,buf,len);
    if(events_enabled && rc==0) printf("@dtlcp {\"type\":\"wire\",\"role\":\"gateway\",\"direction\":\"tx\",\"bytes\":%lu}\n",(unsigned long)len);
    return rc;
}
static int observed_recv(void *user,uint8_t *buf,size_t cap)
{
    int n=udp_recv_cb(user,buf,cap);
    if(n>DTLCP_HDR_LEN+DTLCP_MAC_LEN && buf[0]==DTLCP_RT_APPDATA && (size_t)n<=sizeof captured_record) {
        memcpy(captured_record,buf,(size_t)n); captured_length=(size_t)n;
    }
    if(n>0 && !first_packet_us) first_packet_us=udp_now_us();
    if(events_enabled && n>0) printf("@dtlcp {\"type\":\"wire\",\"role\":\"gateway\",\"direction\":\"rx\",\"bytes\":%d}\n",n);
    return n;
}

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
    const char *mode_str = "full", *save = NULL, *load = NULL, *peer = NULL;
    int mode = DTLCP_MODE_FULL, port = 9000, msgs = 3, i, rc;
    uint64_t t0, t1;
    (void)t1;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--security-control") && i+1<argc) {security_control=argv[++i];continue;}
        if (!strcmp(argv[i], "--events")) { events_enabled = 1; continue; }
        if (!strcmp(argv[i], "--mode") && i + 1 < argc) mode_str = argv[++i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--msgs") && i + 1 < argc) msgs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save-session") && i + 1 < argc) save = argv[++i];
        else if (!strcmp(argv[i], "--load-session") && i + 1 < argc) load = argv[++i];
        else if (!strcmp(argv[i], "--peer") && i + 1 < argc) peer = argv[++i];
    }
    if (!strcmp(mode_str, "full")) mode = DTLCP_MODE_FULL;
    else if (!strcmp(mode_str, "resume")) mode = DTLCP_MODE_RESUME;
    else if (!strcmp(mode_str, "psk")) mode = DTLCP_MODE_PSK;
    else { fprintf(stderr, "unknown mode %s\n", mode_str); return 1; }

    setvbuf(stdout, NULL, _IONBF, 0);
    udp_io_sys_init();
    demo_pki_setup(&pki);
    demo_psk(psk);
    memset(&sess, 0, sizeof sess);
    if (mode == DTLCP_MODE_RESUME && load && load_session(load, &sess) != 0) {
        printf("[gateway] 无可用会话缓存：%s\n", load);
        return 1;
    }

    if (udp_io_bind(&udp, (uint16_t)port) != 0) {
        printf("[gateway] bind %d 失败\n", port);
        return 1;
    }
    udp_io_set_timeout(&udp, peer ? 60000 : 10000);
    if (peer) {
        static const uint8_t prime[] = "DTLCP-PRIME";
        if (udp_io_set_peer(&udp, peer, (uint16_t)port) != 0 ||
            udp_send_cb(&udp, prime, sizeof prime - 1u) != 0) {
            printf("[gateway] prime %s:%d 失败\n", peer, port);
            return 1;
        }
        printf("[gateway] firewall prime -> %s:%d；现在复位板端\n", peer, port);
    }
    io.send = observed_send;
    io.recv = observed_recv;
    io.user = &udp;

    memset(&cfg, 0, sizeof cfg);
    cfg.role = DTLCP_ROLE_SERVER;
    cfg.mode = mode;
    cfg.suite = (mode == DTLCP_MODE_PSK) ? DTLCP_SUITE_L_PSK_SM4_CTR_SM3
                                         : DTLCP_SUITE_L_SM2_SM4_CTR_SM3;
    cfg.identity = "sensor-gw-01";
    cfg.cert = pki.gw_cert;
    memcpy(cfg.privkey, pki.gw_priv, 32);
    cfg.ca_cert = pki.ca_cert;
    cfg.verify_peer = 1;
    cfg.allow_deterministic_rng = 1;   /* 演示程序无 TRNG：显式开启确定性 DRBG */
    cfg.psk = psk;
    cfg.psk_len = 32;
    cfg.resume = (mode == DTLCP_MODE_RESUME) ? &sess : NULL;

    printf("[gateway] DTLCP 服务就绪 mode=%s port=%d\n", mode_name(mode), port);
    state_event("ready", "gateway listening", 0);
    t0 = udp_now_us();
    rc = dtlcp_accept(&ctx, &cfg, &io, t0);
    t1 = udp_now_us();
    if (rc != DTLCP_OK) {
        state_event("error", "handshake failed", rc);
        printf("[gateway] 握手失败 rc=%d\n", rc);
        return 1;
    }
    dtlcp_get_stats(&ctx, &st);
    print_stats("GW", mode, &st, t1-t0);
    if(events_enabled) printf("@dtlcp {\"type\":\"handshake\",\"role\":\"gateway\",\"elapsed_us\":%llu,\"active_elapsed_us\":%llu,\"sm2_ops\":%d,\"flights\":%d,\"wire_sent\":%llu,\"wire_recv\":%llu,\"peer\":\"%s\"}\n",(unsigned long long)(t1-t0),(unsigned long long)(first_packet_us?t1-first_packet_us:0),st.sm2_ops,st.hs_flights,(unsigned long long)st.bytes_sent,(unsigned long long)st.bytes_recv,inet_ntoa(udp.peer.sin_addr));
    if (save) {
        const dtlcp_session *s = dtlcp_get_session(&ctx);
        if (save_session(save, s) == 0)
            printf("[gateway] 会话已缓存 → %s (sid_len=%llu)\n", save, (unsigned long long)s->sid_len);
    }

    for (i = 0; i < msgs; i++) {
        uint8_t buf[512], ack[64];
        int n = dtlcp_recv(&ctx, buf, sizeof buf - 1);
        int alen;
        if (n <= 0) { printf("[gateway] recv 失败\n"); return 1; }
        buf[n] = 0;
        message_number++;
        event("telemetry", buf, (size_t)n, 0);
        alen = snprintf((char *)ack, sizeof ack, "ACK:%.32s",
                        (const char *)buf + (n > 10 ? 10 : 0));
        if (dtlcp_send(&ctx, ack, (size_t)alen) != 0) {
            printf("[gateway] send 失败\n");
            return 1;
        }
        event("ack_sent",ack,(size_t)alen,0);
        security_poll(&ctx);
    }
    state_event("complete","gateway completed",0);
    dtlcp_get_stats(&ctx, &st);
    printf("[gateway] 完成 %d 条遥测回执 | 应用收=%lluB 发=%lluB\n", msgs, st.app_recv, (unsigned long long)st.app_sent);
    /* Keep only the actual session receive state for explicit local negative
     * tests. No replayed record reaches the application or emits an ACK. */
    if(security_control) {
        uint64_t deadline=udp_now_us()+60000000ull;
        while(udp_now_us()<deadline) {
            security_poll(&ctx);
#ifdef _WIN32
            Sleep(50);
#else
            usleep(50000);
#endif
        }
    }
    udp_io_close(&udp);
    return 0;
}
