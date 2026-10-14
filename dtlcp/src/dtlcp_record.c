/*
 * dtlcp_record.c — DTLCP 记录层：数据报保护/解保护 + 防重放
 *
 * 记录布局（单记录 = 单 UDP 数据报，或一个数据报内多记录拼接）：
 *   type(1) | ver(2) | epoch(2) | seq(6) | len(2) | [iv(16)] | payload | [mac(32)]
 *   - CBC 套件（MAC-then-encrypt）：iv 随记录携带，payload = SM4-CBC(plain‖mac‖pad)
 *   - CTR 套件（Encrypt-then-MAC） ：无 iv 段，payload = SM4-CTR(plain)，mac 尾随
 * 防重放：epoch + 48bit 序列号 + 64 位滑动窗口（MAC 验证通过后才推进窗口）
 *
 * CTR 计数器块布局（唯一性依据，改动前请读 crypto/include/smc/sm4.h 的 sm4_ctr_xor_n）：
 *   iv_prefix(8) | 记录序号 seq(6) | 块计数(2)      ← 只有块计数自增
 *   记录序号必须留在自增域之外：否则记录 s 的第 n 块与记录 s+n 的第 1 块会共用
 *   密钥流（two-time pad，可由已知明文直接恢复其余记录）。
 */
#include <string.h>
#include "dtlcp_internal.h"

static void put_be16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static uint16_t get_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void put_seq(uint8_t out[8], uint16_t epoch, uint64_t seq)
{
    out[0] = (uint8_t)(epoch >> 8);
    out[1] = (uint8_t)epoch;
    out[2] = (uint8_t)(seq >> 40);
    out[3] = (uint8_t)(seq >> 32);
    out[4] = (uint8_t)(seq >> 24);
    out[5] = (uint8_t)(seq >> 16);
    out[6] = (uint8_t)(seq >> 8);
    out[7] = (uint8_t)seq;
}

static uint64_t get_seq(const uint8_t in[8])
{
    return ((uint64_t)in[2] << 40) | ((uint64_t)in[3] << 32) | ((uint64_t)in[4] << 24) |
           ((uint64_t)in[5] << 16) | ((uint64_t)in[6] << 8)  | (uint64_t)in[7];
}

/* MAC 输入：seq(8)‖type(1)‖ver(2)‖len(2)‖data */
static void mac_compute(const uint8_t mac_key[32], const uint8_t seqb[8],
                        uint8_t type, uint16_t len, const uint8_t *data,
                        uint8_t out[DTLCP_MAC_LEN])
{
    hmac_sm3_ctx c;
    uint8_t hdr[5];
    hdr[0] = type;
    put_be16(hdr + 1, DTLCP_VERSION);
    put_be16(hdr + 3, len);
    hmac_sm3_init(&c, mac_key, 32);
    hmac_sm3_update(&c, seqb, 8);
    hmac_sm3_update(&c, hdr, 5);
    hmac_sm3_update(&c, data, len);
    hmac_sm3_final(&c, out);
}

static int mac_verify(const uint8_t mac_key[32], const uint8_t seqb[8],
                      uint8_t type, uint16_t len, const uint8_t *data,
                      const uint8_t mac[DTLCP_MAC_LEN])
{
    uint8_t calc[DTLCP_MAC_LEN], diff = 0;
    int i;
    mac_compute(mac_key, seqb, type, len, data, calc);
    for (i = 0; i < DTLCP_MAC_LEN; i++) diff |= (uint8_t)(calc[i] ^ mac[i]);
    return diff ? -1 : 0;
}

/* 防重放窗口推进（仅在 MAC 验证通过后调用） */
static int replay_update(dtlcp_rec *r, uint64_t seq)
{
    if (!r->have_max) {
        r->have_max = 1; r->max_seq = seq; r->window = 1;
        return 0;
    }
    if (seq > r->max_seq) {
        uint64_t sh = seq - r->max_seq;
        r->window = (sh >= 64) ? 1 : ((r->window << sh) | 1);
        r->max_seq = seq;
        return 0;
    }
    {
        uint64_t delta = r->max_seq - seq;
        if (delta >= 64) return DTLCP_E_REPLAY;
        if (r->window & ((uint64_t)1 << delta)) return DTLCP_E_REPLAY;
        r->window |= (uint64_t)1 << delta;
    }
    return 0;
}

static void derive_iv(const uint8_t prefix[8], const uint8_t seqb[8], uint8_t iv[16])
{
    /* 确定性唯一 IV：HMAC(prefix, seqb) 截断 16 字节（唯一性由序列号保证；
       生产固件建议 TRNG 随机 IV，docs/03 §5） */
    uint8_t h[SMC_SM3_DIGEST];
    hmac_sm3(prefix, 8, seqb, 8, h);
    memcpy(iv, h, 16);
}

/* CTR 计数器块 = iv_prefix(8) ‖ 记录序号 seq(6) ‖ 块计数(2)。
   只有末 2 字节（块计数）参与自增：块数上限 65536 × 16B ≫ 单记录长度。 */
static void derive_ctr(const dtlcp_rec *r, const uint8_t seqb[8], uint8_t ctr[16])
{
    memcpy(ctr, r->iv_prefix, 8);
    memcpy(ctr + 8, seqb + 2, 6);      /* 48 位序号（epoch 恒为 0，见 unprotect 校验） */
    ctr[14] = 0;
    ctr[15] = 0;
}

static void rec_init(dtlcp_rec *r, uint16_t suite, const uint8_t mac_key[32],
                     const uint8_t key[16], const uint8_t iv_prefix[8])
{
    memset(r, 0, sizeof *r);
    r->suite = suite;
    r->etm = (suite != DTLCP_SUITE_ECDHE_SM4_CBC_SM3);
    memcpy(r->mac_key, mac_key, 32);
    sm4_setkey_enc(&r->cipher, key);
    if (!r->etm) sm4_setkey_dec(&r->cipher_dec, key);   /* CBC 解密需逆序轮密钥 */
    memcpy(r->iv_prefix, iv_prefix, 8);
}

void dtlcp_rec_init_tx(dtlcp_rec *r, uint16_t suite, const uint8_t mac_key[32],
                       const uint8_t key[16], const uint8_t iv_prefix[8])
{
    rec_init(r, suite, mac_key, key, iv_prefix);
}

void dtlcp_rec_init_rx(dtlcp_rec *r, uint16_t suite, const uint8_t mac_key[32],
                       const uint8_t key[16], const uint8_t iv_prefix[8])
{
    rec_init(r, suite, mac_key, key, iv_prefix);
}

int dtlcp_rec_protect(dtlcp_rec *r, uint8_t type,
                      const uint8_t *in, size_t len,
                      uint8_t *out, size_t *olen, uint64_t now_us)
{
    uint8_t seqb[8];
    size_t o = DTLCP_HDR_LEN;

    (void)now_us;
    /* 记录总长 = 头13 + [IV16] + payload + [MAC32] + pad ≤ 16 */
    if (len > DTLCP_RECBUF - 77) return DTLCP_E_PARAM;
    put_seq(seqb, 0, r->seq);

    out[0] = type;
    put_be16(out + 1, DTLCP_VERSION);
    put_be16(out + 3, 0);                        /* epoch */
    memcpy(out + 5, seqb + 2, 6);

    if (r->etm) {
        uint8_t ctr[16];
        put_be16(out + 11, (uint16_t)len);       /* payload_len */
        derive_ctr(r, seqb, ctr);
        sm4_ctr_xor_n(&r->cipher, ctr, 2, in, out + o, len);   /* 只自增块计数 */
        mac_compute(r->mac_key, seqb, type, (uint16_t)len, out + o, out + o + len);
        o += len + DTLCP_MAC_LEN;
    } else {
        uint8_t iv[16];
        size_t plen;
        uint8_t pad;
        derive_iv(r->iv_prefix, seqb, iv);
        memcpy(out + o, iv, 16);
        o += 16;
        memcpy(out + o, in, len);
        mac_compute(r->mac_key, seqb, type, (uint16_t)len, in, out + o + len);
        plen = len + DTLCP_MAC_LEN;
        pad = (uint8_t)(SMC_SM4_BLOCK - (plen % SMC_SM4_BLOCK));
        if (pad == 0) pad = SMC_SM4_BLOCK;
        memset(out + o + plen, pad, pad);
        plen += pad;
        sm4_cbc_encrypt(&r->cipher, iv, out + o, out + o, plen);
        put_be16(out + 11, (uint16_t)plen);      /* payload_len = 密文长 */
        o += plen;
    }
    r->seq++;
    *olen = o;
    return DTLCP_OK;
}

int dtlcp_rec_unprotect(dtlcp_rec *r, const uint8_t *in, size_t len,
                        uint8_t *out, size_t cap, size_t *olen,
                        uint8_t *type, size_t *consumed)
{
    uint8_t seqb[8];
    uint64_t seq;
    size_t o;

    if (len < DTLCP_HDR_LEN) return DTLCP_E_FORMAT;
    *type = in[0];
    if (get_be16(in + 1) != DTLCP_VERSION) return DTLCP_E_FORMAT;
    if (get_be16(in + 3) != 0) return DTLCP_E_FORMAT;   /* 当前实现仅 epoch 0 */
    seqb[0] = in[3]; seqb[1] = in[4];                   /* epoch(2) */
    memcpy(seqb + 2, in + 5, 6);                        /* seq(6) */
    seq = get_seq(seqb);

    o = DTLCP_HDR_LEN;
    {
        uint16_t declared = get_be16(in + 11);
        if (r->etm) {
            if (len < (size_t)declared + o + DTLCP_MAC_LEN) return DTLCP_E_FORMAT;
        } else {
            /* CBC 记录 = 头(13) + IV(16) + 密文(declared)。
               必须把 IV 计入下限，否则密文可声明得比实际数据长，
               解密会读到调用方给定长度之外（越界读）。 */
            if (len < (size_t)declared + o + DTLCP_IV_LEN) return DTLCP_E_FORMAT;
        }
    }
    if (r->etm) {
        size_t ctlen = get_be16(in + 11);
        if (ctlen > cap) return DTLCP_E_PARAM;
        if (mac_verify(r->mac_key, seqb, *type, (uint16_t)ctlen, in + o,
                       in + o + ctlen) != 0) return DTLCP_E_VERIFY;
        {
            int rr = replay_update(r, seq);
            uint8_t ctr[16];
            if (rr != 0) return rr;
            derive_ctr(r, seqb, ctr);
            sm4_ctr_xor_n(&r->cipher, ctr, 2, in + o, out, ctlen);   /* 只自增块计数 */
            *olen = ctlen;
        }
        *consumed = o + ctlen + DTLCP_MAC_LEN;
        return DTLCP_OK;
    } else {
        uint8_t iv[16];
        size_t ctlen, plen;
        uint8_t pad;
        if (len < o + 16) return DTLCP_E_FORMAT;
        memcpy(iv, in + o, 16);
        o += 16;
        ctlen = get_be16(in + 11);
        if (ctlen == 0 || (ctlen % SMC_SM4_BLOCK) != 0 || ctlen > DTLCP_RECBUF)
            return DTLCP_E_FORMAT;
        if (ctlen > cap) return DTLCP_E_PARAM;   /* 解密目标容量校验 */
        sm4_cbc_decrypt(&r->cipher_dec, iv, in + o, out, ctlen);
        pad = out[ctlen - 1];
        {
            /* padding 全字节校验；错误统一为 DTLCP_E_VERIFY，避免 padding oracle 区分 */
            uint8_t bad = 0;
            size_t k;
            if (pad == 0 || pad > SMC_SM4_BLOCK || pad > ctlen) bad = 1;
            for (k = 0; k < SMC_SM4_BLOCK && k < ctlen; k++)
                if (k < pad && out[ctlen - 1 - k] != pad) bad = 1;
            if (bad) return DTLCP_E_VERIFY;
        }
        plen = ctlen - pad;
        if (plen < DTLCP_MAC_LEN) return DTLCP_E_VERIFY;
        plen -= DTLCP_MAC_LEN;
        if (mac_verify(r->mac_key, seqb, *type, (uint16_t)plen, out,
                       out + plen) != 0) return DTLCP_E_VERIFY;
        {
            int rr = replay_update(r, seq);
            if (rr != 0) return rr;
        }
        *olen = plen;
        *consumed = o + ctlen;      /* 记录定界：头+IV+密文 */
    }
    return DTLCP_OK;
}
