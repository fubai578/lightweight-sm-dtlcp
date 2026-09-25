/*
 * dtlcp_cert.c — 紧凑 SM2 证书（X.509 映射约束见 docs/02 §4.4）
 *
 * 编码 = tbs ‖ sig(64)；tbs = ver ‖ serial ‖ subject ‖ issuer ‖ pubkey(65) ‖ sig_alg
 * 签名 e = SM3(ZA_issuer ‖ tbs)（ZA 按 GB/T 32918.2 绑定签发者身份）
 */
#include <string.h>
#include "dtlcp_internal.h"

static size_t cert_tbs(const dtlcp_cert *c, uint8_t *out)
{
    uint8_t *p = out;
    *p++ = c->version;
    *p++ = c->serial_len;
    memcpy(p, c->serial, c->serial_len);  p += c->serial_len;
    *p++ = c->subject_len;
    memcpy(p, c->subject, c->subject_len); p += c->subject_len;
    *p++ = c->issuer_len;
    memcpy(p, c->issuer, c->issuer_len);  p += c->issuer_len;
    memcpy(p, c->pubkey, SMC_SM2_PUBKEY); p += SMC_SM2_PUBKEY;
    *p++ = c->sig_alg;
    return (size_t)(p - out);
}

int dtlcp_cert_issue(dtlcp_cert *cert, const char *subject, const uint8_t serial,
                     const uint8_t subject_pub[SMC_SM2_PUBKEY],
                     const char *issuer, const uint8_t issuer_priv[SMC_SM2_BYTES])
{
    uint8_t tbs[256], e[SMC_SM3_DIGEST], za[SMC_SM3_DIGEST];
    uint8_t issuer_pub[SMC_SM2_PUBKEY];
    size_t tbslen;
    size_t slen = strlen(subject), ilen = strlen(issuer);

    if (slen > DTLCP_ID_MAX || ilen > DTLCP_ID_MAX) return DTLCP_E_PARAM;
    memset(cert, 0, sizeof *cert);
    cert->version = 1;
    cert->serial_len = 1;
    cert->serial[0] = serial;
    cert->subject_len = (uint8_t)slen;
    memcpy(cert->subject, subject, slen);
    cert->issuer_len = (uint8_t)ilen;
    memcpy(cert->issuer, issuer, ilen);
    memcpy(cert->pubkey, subject_pub, SMC_SM2_PUBKEY);
    cert->sig_alg = 0x01;

    if (sm2_pub_from_priv(issuer_pub, issuer_priv) != 0) return DTLCP_E_CRYPTO;
    sm2_compute_za(za, issuer, ilen, issuer_pub);
    tbslen = cert_tbs(cert, tbs);
    {
        sm3_ctx c;
        sm3_init(&c);
        sm3_update(&c, za, SMC_SM3_DIGEST);
        sm3_update(&c, tbs, tbslen);
        sm3_final(&c, e);
    }
    return sm2_sign(cert->sig, e, issuer_priv, NULL) == 0 ? DTLCP_OK : DTLCP_E_CRYPTO;
}

int dtlcp_cert_check(const dtlcp_cert *cert, const dtlcp_cert *issuer_cert)
{
    uint8_t tbs[256], e[SMC_SM3_DIGEST], za[SMC_SM3_DIGEST];
    size_t tbslen;

    if (cert->sig_alg != 0x01) return DTLCP_E_NOSUPPORT;
    if (memcmp(cert->issuer, issuer_cert->subject, cert->issuer_len) != 0 ||
        cert->issuer_len != issuer_cert->subject_len) return DTLCP_E_VERIFY;

    sm2_compute_za(za, (const char *)issuer_cert->subject, issuer_cert->subject_len,
                   issuer_cert->pubkey);
    tbslen = cert_tbs(cert, tbs);
    {
        sm3_ctx c;
        sm3_init(&c);
        sm3_update(&c, za, SMC_SM3_DIGEST);
        sm3_update(&c, tbs, tbslen);
        sm3_final(&c, e);
    }
    return sm2_verify(cert->sig, e, issuer_cert->pubkey) == 0 ? DTLCP_OK : DTLCP_E_VERIFY;
}

int dtlcp_cert_encode(const dtlcp_cert *cert, uint8_t *out, size_t cap)
{
    uint8_t *p = out;
    size_t tbslen = cert_tbs(cert, out);
    if (tbslen + SMC_SM2_SIG > cap) return DTLCP_E_PARAM;
    p += tbslen;
    memcpy(p, cert->sig, SMC_SM2_SIG);
    return (int)(tbslen + SMC_SM2_SIG);
}

int dtlcp_cert_parse(dtlcp_cert *cert, const uint8_t *in, size_t len)
{
    const uint8_t *p = in, *end = in + len;
    memset(cert, 0, sizeof *cert);
    if (len < 1 + 1 + 1 + 1 + SMC_SM2_PUBKEY + 1 + SMC_SM2_SIG) return DTLCP_E_FORMAT;

    cert->version = *p++;
    cert->serial_len = *p++;
    if (cert->serial_len > sizeof cert->serial || p + cert->serial_len > end) return DTLCP_E_FORMAT;
    memcpy(cert->serial, p, cert->serial_len); p += cert->serial_len;

    if (p >= end) return DTLCP_E_FORMAT;
    cert->subject_len = *p++;
    if (cert->subject_len > sizeof cert->subject || p + cert->subject_len > end) return DTLCP_E_FORMAT;
    memcpy(cert->subject, p, cert->subject_len); p += cert->subject_len;

    if (p >= end) return DTLCP_E_FORMAT;
    cert->issuer_len = *p++;
    if (cert->issuer_len > sizeof cert->issuer || p + cert->issuer_len > end) return DTLCP_E_FORMAT;
    memcpy(cert->issuer, p, cert->issuer_len); p += cert->issuer_len;

    if (p + SMC_SM2_PUBKEY + 1 + SMC_SM2_SIG > end) return DTLCP_E_FORMAT;
    memcpy(cert->pubkey, p, SMC_SM2_PUBKEY); p += SMC_SM2_PUBKEY;
    cert->sig_alg = *p++;
    memcpy(cert->sig, p, SMC_SM2_SIG);
    return DTLCP_OK;
}
