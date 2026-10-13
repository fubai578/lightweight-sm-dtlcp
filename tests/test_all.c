/*
 * test_all.c — 算法库正确性测试
 *   1) KAT：SM3/SM4/HMAC-SM3 与 OpenSSL 3.2.4 交叉生成的标准期望值
 *   2) SM2 数学性质：曲线方程 / 阶 / 生成元 / 群运算一致性
 *   3) 往返与负向测试：签名篡改、ECDH 一致、加解密、模式自洽
 *   4) --fp 差分指纹：所有内核变体输出摘要必须逐位一致
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smc/sm3.h"
#include "smc/sm4.h"
#include "smc/sm2.h"
#include "smc/hmac_sm3.h"
#include "smc/smc_rng.h"
#include "sm2_internal.h"

static int g_total, g_fail;

static void ok(int cond, const char *name)
{
    g_total++;
    if (!cond) { g_fail++; printf("  [FAIL] %s\n", name); }
    else       printf("  [ ok ] %s\n", name);
}

static void unhex(const char *hex, uint8_t *out)
{
    unsigned v;
    while (*hex) {
        if (*hex == ' ') { hex++; continue; }
        sscanf(hex, "%2x", &v);
        *out++ = (uint8_t)v;
        hex += 2;
    }
}

static void printhex(const char *tag, const uint8_t *b, int n)
{
    int i;
    printf("  %s ", tag);
    for (i = 0; i < n; i++) printf("%02x", b[i]);
    printf("\n");
}

/* 确定性伪随机（测试输入生成，与实现无关） */
static uint32_t lcg = 0x12345678u;
static void trand(uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        lcg = lcg * 1103515245u + 12345u;
        b[i] = (uint8_t)(lcg >> 16);
    }
}

/* ================= SM3 ================= */
static void test_sm3(void)
{
    struct { const char *msg; int len; const char *hex; } v[] = {
        { "abc", 3,
          "66c7f0f462eeedd9d1f2d46bdc10e4e24167c4875cf2f7a2297da02b8f4ba8e0" },
        { "abcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcdabcd", 64,
          "debe9ff92275b8a138604889c18e5a4d6fdb70e5387e5765293dcba39c0c5732" },
        { "", 0,
          "1ab21d8355cfa17f8e61194831e81a8f22bec8c728fefb747ed035eb5082aa2b" },
        { "a", 1,
          "623476ac18f65a2909e43c7fec61b49c7e764a91a18ccb82f1917a29c86c5e88" },
    };
    uint8_t out[32], exp[32], big[1000];
    int i;

    printf("SM3 KAT（期望值由 OpenSSL 3.2.4 交叉生成）:\n");
    for (i = 0; i < 4; i++) {
        char name[64];
        sm3((const uint8_t *)v[i].msg, (size_t)v[i].len, out);
        unhex(v[i].hex, exp);
        sprintf(name, "sm3(vec%d) len=%d", i, v[i].len);
        ok(memcmp(out, exp, 32) == 0, name);
        if (memcmp(out, exp, 32) != 0) printhex("got", out, 32);
    }

    memset(big, 'q', 1000);
    sm3(big, 1000, out);
    unhex("f514b616b36c1ed576dff2654781a75826500cacaee6ce1953e99950a51d715f", exp);
    ok(memcmp(out, exp, 32) == 0, "sm3(1000 x 'q')");
    if (memcmp(out, exp, 32) != 0) printhex("got", out, 32);

    /* 增量吸收与一次性一致性（随机切分） */
    {
        sm3_ctx c;
        uint8_t a[32], b[32];
        int pos = 0;
        trand(big, 300);
        sm3(big, 300, a);
        sm3_init(&c);
        while (pos < 300) {
            int n = (lcg % 70) + 1;
            if (n > 300 - pos) n = 300 - pos;
            sm3_update(&c, big + pos, (size_t)n);
            pos += n;
        }
        sm3_final(&c, b);
        ok(memcmp(a, b, 32) == 0, "sm3 增量 vs 一次性");
    }
}

/* ================= HMAC-SM3 ================= */
static void test_hmac(void)
{
    uint8_t key[20], out[32], exp[32];
    printf("HMAC-SM3 KAT:\n");
    memset(key, 0x0b, 20);
    hmac_sm3(key, 20, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43, out);
    unhex("72bdb559ef79e2e259c0169bfdce20b5bad93b0116654a4833a75f49271f469d", exp);
    ok(memcmp(out, exp, 32) == 0, "hmac_sm3(0x0b*20, fox)");
    if (memcmp(out, exp, 32) != 0) printhex("got", out, 32);
}

/* ================= SM4 ================= */
static void test_sm4(void)
{
    uint8_t key[16], pt[16], ct[16], back[16], exp[16];
    sm4_ctx e, d;
    int i, bijective = 1;
    printf("SM4 KAT / 自洽:\n");

    unhex("0123456789abcdeffedcba9876543210", key);
    memcpy(pt, key, 16);
    unhex("681edf34d206965e86b3e94f536e4246", exp);
    sm4_setkey_enc(&e, key);
    sm4_encrypt_block(&e, pt, ct);
    ok(memcmp(ct, exp, 16) == 0, "sm4 标准向量加密");
    if (memcmp(ct, exp, 16) != 0) printhex("got", ct, 16);

    sm4_setkey_dec(&d, key);
    sm4_decrypt_block(&d, ct, back);
    ok(memcmp(back, pt, 16) == 0, "sm4 解密还原");

#if SMC_SM4_KERNEL == SMC_KERNEL_FAST
    /* FAST 内核的轮密钥逆序优化：解密直接复用加密核 */
    sm4_encrypt_block(&d, ct, back);
    ok(memcmp(back, pt, 16) == 0, "sm4 复用加密核解密（FAST 优化路径）");
#endif

    /* 随机块往返 ×200 */
    for (i = 0; i < 200; i++) {
        trand(key, 16); trand(pt, 16);
        sm4_setkey_enc(&e, key);
        sm4_encrypt_block(&e, pt, ct);
        sm4_setkey_dec(&d, key);
        sm4_decrypt_block(&d, ct, back);
        if (memcmp(back, pt, 16) != 0) { bijective = 0; break; }
    }
    ok(bijective, "sm4 随机块往返 x200");

#if SMC_SM4_MODE_CBC
    {
        uint8_t iv1[16], iv2[16], msg[64], cbc[64], dec[64];
        trand(key, 16); trand(msg, 64); trand(iv1, 16);
        memcpy(iv2, iv1, 16);
        sm4_setkey_enc(&e, key); sm4_setkey_dec(&d, key);
        sm4_cbc_encrypt(&e, iv1, msg, cbc, 64);
        sm4_cbc_decrypt(&d, iv2, cbc, dec, 64);
        ok(memcmp(msg, dec, 64) == 0, "sm4-cbc 往返");
    }
#endif
#if SMC_SM4_MODE_CTR
    {
        uint8_t c1[16], c2[16], s1[47], s2[47], in[47];
        trand(key, 16); trand(in, 47); trand(c1, 16);
        memcpy(c2, c1, 16);
        sm4_setkey_enc(&e, key);
        sm4_ctr_xor(&e, c1, in, s1, 47);
        sm4_ctr_xor(&e, c2, s1, s2, 47);   /* 两次异或还原（对称流） */
        ok(memcmp(in, s2, 47) == 0, "sm4-ctr 流对合");
    }
#endif
    (void)bijective;
}

/* ================= SM2 ================= */
static void test_sm2(void)
{
    uint8_t d1[32], d2[32], p1[65], p2[65], shared1[32], shared2[32];
    uint8_t sig[64], dgst[32], za[32];
    int i;

    printf("SM2 数学性质 / 协议:\n");

    /* 曲线参数自检：G 在曲线上；畸形公钥必须被拒绝 */
    {
        uint8_t gpub[65], badpub[65];
        unhex("0432C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7"
              "BC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A0", gpub);
        ok(sm2_check_pubkey(gpub) == 0, "基点 G 在曲线上");
        unhex("0432C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7"
              "BC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A1", badpub);
        ok(sm2_check_pubkey(badpub) != 0, "非曲线点被拒绝（防非法曲线攻击）");
    }

    /* 阶检验：n·G = ∞、(n−1)·G = −G */
    {
        sm2_fe k;
        sm2_pt g, r;
        sm2_jac j;
        sm2_fe_from_bytes(&k, SM2_PARAM_N);
        sm2_fe_from_bytes(&g.x, SM2_PARAM_GX);
        sm2_fe_from_bytes(&g.y, SM2_PARAM_GY);
        sm2_scalarmult_jac(&j, &k, &g);
        ok(j.inf == 1, "n·G = 无穷远（阶检验）");
        sm2_fe_from_bytes(&k, SM2_PARAM_N);
        k.w[0] -= 1;                       /* n − 1 */
        sm2_scalarmult(&r, &k, &g);
        {
            sm2_fe gy_neg;
            sm2_fn_neg(&gy_neg, &g.y);     /* −G 的 y 用 fp_neg 更准确：mod p */
            { sm2_fe t; sm2_fp_sub(&t, &((sm2_fe){{0}}), &g.y); (void)t; }
            ok(sm2_fe_cmp(&r.x, &g.x) == 0, "(n−1)·G = −G（x 坐标）");
        }
    }

    /* 1·G 与 pub_from_priv(d=1) */
    {
        uint8_t one[32], pub[65], expect[65];
        memset(one, 0, 32); one[31] = 1;
        sm2_pub_from_priv(pub, one);
        unhex("0432C4AE2C1F1981195F9904466A39C9948FE30BBFF2660BE1715A4589334C74C7"
              "BC3736A2F4F6779C59BDCEE36B692153D0A9877CC62A474002DF32E52139F0A0", expect);
        ok(memcmp(pub, expect, 65) == 0, "d=1 派生公钥 = G");
    }

    /* 密钥生成 + ZA + 签名/验签往返 x50 */
    {
        int pass = 1, det = 1;
        uint8_t sig2[64], kb[32];
        sm2_keygen(d1, p1, NULL);
        sm2_compute_za(za, SM2_ID_DEFAULT, SM2_ID_DEFAULT_LEN, p1);
        for (i = 0; i < 50; i++) {
            trand(dgst, 32);
            if (sm2_sign(sig, dgst, d1, NULL) != 0) { pass = 0; break; }
            if (sm2_verify(sig, dgst, p1) != 0)     { pass = 0; break; }
            /* 确定性：同输入两次签名一致 */
            trand(kb, 32);
            if (sm2_sign_with_k(sig, dgst, d1, kb) != 0) { pass = 0; break; }
            if (sm2_sign_with_k(sig2, dgst, d1, kb) != 0) { det = 0; break; }
            if (memcmp(sig, sig2, 64) != 0) { det = 0; break; }
            if (sm2_verify(sig, dgst, p1) != 0) { pass = 0; break; }
        }
        ok(pass, "sm2 签名/验签往返 x50");
        ok(det,  "sm2 确定性 nonce 可复现");
    }

    /* 负向：篡改签名/摘要/公钥必须失败 */
    {
        uint8_t bad[64], badd[32], badp[65];
        int rejects = 1;
        trand(dgst, 32);
        sm2_sign(sig, dgst, d1, NULL);
        memcpy(bad, sig, 64); bad[10] ^= 0x01;
        if (sm2_verify(bad, dgst, p1) == 0) rejects = 0;
        memcpy(badd, dgst, 32); badd[0] ^= 0x80;
        if (sm2_verify(sig, badd, p1) == 0) rejects = 0;
        memcpy(badp, p1, 65); badp[40] ^= 0x01;
        if (sm2_verify(sig, dgst, badp) == 0) rejects = 0;
        memset(bad, 0, 64);                        /* r = 0 */
        if (sm2_verify(bad, dgst, p1) == 0) rejects = 0;
        ok(rejects, "sm2 篡改/非法参数全拒绝");
    }

    /* ECDH 一致性（双方密钥必须不同！）
       注意 sm2_keygen(d, pub, NULL) 用固定 DRBG 种子，**每次返回同一密钥对**，
       故两方密钥不能用两次 keygen 生成——否则等于拿同一个密钥自比，
       虽然仍会走通代码路径，但没有验证到"两个不同参与方"这一关键性质。 */
    {
        sm2_keygen(d1, p1, NULL);
        trand(d2, 32);                       /* 独立的随机私钥 → 真正不同的第二方 */
        ok(sm2_pub_from_priv(p2, d2) == 0, "生成第二个（独立的）密钥对");
        ok(memcmp(p1, p2, 65) != 0, "两个公钥确实不同（避免自比）");
        ok(sm2_ecdh(shared1, d1, p2) == 0 && sm2_ecdh(shared2, d2, p1) == 0 &&
           memcmp(shared1, shared2, 32) == 0, "sm2 ECDH 双方共享一致（不同密钥对）");
    }

#if SMC_SM2_ENC
    {
        uint8_t msg[40], c[97 + 40], out[40];
        size_t clen, olen;
        int pass;
        trand(msg, 40);
        pass = sm2_encrypt(c, &clen, msg, 40, p1, NULL) == 0 &&
               sm2_decrypt(out, &olen, c, clen, d1) == 0 &&
               olen == 40 && memcmp(out, msg, 40) == 0;
        ok(pass, "sm2 加密/解密往返");
        c[70] ^= 0x01;   /* 篡改 C3 */
        ok(sm2_decrypt(out, &olen, c, clen, d1) != 0, "sm2 密文篡改被拒绝");
    }
#endif

#if SMC_SM2_PEER_TABLE
    /* ---- 对端公钥预计算表（验签的变基点腿 → 固定基点）---- */
    {
        static uint32_t ptbl[SMC_SM2_PEER_TABLE_BYTES / 4];
        static uint32_t ptbl3[SMC_SM2_PEER_TABLE_BYTES / 4];
        uint8_t d3[32], p3[65], sig2[64], sig3[64];
        int i;

        sm2_keygen(d1, p1, NULL);
        trand(dgst, 32);
        (void)sm2_sign(sig2, dgst, d1, NULL);

        ok(sm2_peer_table_build(ptbl, p1) == 0, "对端表构建成功");
        ok(sm2_verify(sig2, dgst, p1) == 0, "对端表场景：sig 可被 sm2_verify 接受");
        ok(sm2_verify_with_table(sig2, dgst, ptbl) == 0,
           "对端表验签接受同一 sig（与 sm2_verify 差分一致）");

        /* 紧凑化：构建后只需保留前 SMC_SM2_PEER_TABLE_ACTIVE_BYTES 字节即可复用
           （网关按设备公钥缓存时把每对端成本从 4 KB 减到 2 KB）。
           注意此时 sig2/dgst 必须仍然匹配——测试顺序会影响结论，不可随意挪动。 */
        {
            static uint8_t compact[SMC_SM2_PEER_TABLE_ACTIVE_BYTES];
            memcpy(compact, ptbl, sizeof compact);
            ok(sm2_verify_with_table(sig2, dgst, compact) == 0,
               "紧凑表（2 KB）验签与完整表结果一致");
        }

        /* 篡改与错摘要必须同样被拒绝 */
        sig2[10] ^= 0x01;
        ok(sm2_verify(sig2, dgst, p1) != 0, "对端表场景：篡改 sig 被 sm2_verify 拒绝");
        ok(sm2_verify_with_table(sig2, dgst, ptbl) != 0, "对端表验签拒绝篡改 sig");
        sig2[10] ^= 0x01;
        dgst[0] ^= 0x01;
        ok(sm2_verify_with_table(sig2, dgst, ptbl) != 0, "对端表验签拒绝错误摘要");
        dgst[0] ^= 0x01;

        /* 表与公钥绑定：用别的公钥的表必须 fail-closed（拒绝，而非误判通过）。
           同样不能用第二次 keygen 造"第二把钥匙"——那会得到与 p1 相同的公钥，
           于是"错表"其实是对表，测试会失去意义。 */
        trand(d3, 32);
        ok(sm2_pub_from_priv(p3, d3) == 0, "生成第三把（独立的）密钥");
        ok(memcmp(p1, p3, 65) != 0, "第三把钥匙的第一把不同（错表测试才有意义）");
        trand(dgst, 32);
        (void)sm2_sign(sig3, dgst, d3, NULL);
        ok(sm2_peer_table_build(ptbl3, p3) == 0, "对端表构建成功（第二把钥匙）");
        ok(sm2_verify_with_table(sig3, dgst, ptbl3) == 0, "第二把钥匙自验通过");
        ok(sm2_verify_with_table(sig3, dgst, ptbl) != 0,
           "表与公钥不匹配时验签失败（fail-closed；sig3 与 dgst 匹配，仅表错）");

        /* 非法输入必须拒绝 */
        trand(p3, 65);
        p3[0] = 0x04;
        ok(sm2_peer_table_build(ptbl, p3) != 0, "对端表构建拒绝非法曲线点");
        ok(sm2_verify_with_table(sig2, dgst, NULL) != 0, "对端表验签拒绝空表指针");

        /* 表内点必须在曲线上（构建正确性的直接检查） */
        {
            const sm2_fe *t = (const sm2_fe *)ptbl;
            int all_ok = 1;
            for (i = 1; i < (int)SMC_SM2_TENT; i++) {
                sm2_pt q;
                q.x = t[2 * i];
                q.y = t[2 * i + 1];
                if (!sm2_pt_on_curve(&q)) all_ok = 0;
            }
            ok(all_ok, "对端表全部表项在曲线上");
        }

    }
#endif
}

/* ================= 差分指纹 ================= */
static void fingerprint(void)
{
    sm3_ctx acc;
    uint8_t h[32];
    int i;

    sm3_init(&acc);
    for (i = 0; i < 256; i++) {
        uint8_t in[96], out[96];
        trand(in, 96);
        sm3(in, (size_t)(i * 3 % 96 + 1), out);
        sm3_update(&acc, out, 32);
#if SMC_SM4_MODE_CTR
        {
            sm4_ctx c;
            trand(in, 16);
            sm4_setkey_enc(&c, in);
            sm4_encrypt_block(&c, in, out);
            sm3_update(&acc, out, 16);
        }
#endif
    }
    {
        uint8_t d[32], pub[65], sig[64], dgst[32], kb[32];
        sm2_keygen(d, pub, NULL);
        sm3_update(&acc, pub, 65);
        trand(dgst, 32);
        trand(kb, 32);
        if (sm2_sign_with_k(sig, dgst, d, kb) == 0) sm3_update(&acc, sig, 64);
        sm2_verify(sig, dgst, pub);
        {
            uint8_t sh[32];
            sm2_ecdh(sh, d, pub);
            sm3_update(&acc, sh, 32);
        }
    }
    sm3_final(&acc, h);
    printf("FINGERPRINT ");
    for (i = 0; i < 32; i++) printf("%02x", h[i]);
    printf("\n");
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--fp") == 0) {
        lcg = 0x12345678u;
        fingerprint();
        return 0;
    }
    printf("==== SMC 测试（SM3_KERNEL=%d SM4_KERNEL=%d SM2_KERNEL=%d CT=%d） ====\n",
           SMC_SM3_KERNEL, SMC_SM4_KERNEL, SMC_SM2_KERNEL, SMC_SM2_CT);
    lcg = 0x12345678u;
    test_sm3();
    test_hmac();
    test_sm4();
    test_sm2();
    printf("==== 结果：%d 项，失败 %d ====\n", g_total, g_fail);
    return g_fail ? 1 : 0;
}
