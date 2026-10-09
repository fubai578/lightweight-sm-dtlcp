#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
compare_bench.py — 交错测量本库与第三方基线，生成对照报告

为什么要交错：RDTSC 计数受 CPU 频率状态影响，**跨运行绝对值不可比**（见 docs/04 §1.2）。
两个二进制若先后单独跑，比值会掺入频率漂移。本脚本在每一轮里交替运行两侧，
每侧每操作取多轮最小值——最小值过滤调度/频率噪声，交错保证两侧经历相同的主机状态。

用法（由 scripts/thirdparty_bench.sh 调用）：
    python scripts/compare_bench.py --out bin/thirdparty_compare.md \
        --ours opt:bin/bench_opt_tp --ours ref:bin/bench_ref_tp \
        --gmssl bin/bench_gmssl --passes 3

输出：markdown 对照表（加速比 = GmSSL cycles ÷ 本库 cycles，>1 表示本库更快）
"""
import argparse
import re
import subprocess
import sys

ROW = re.compile(r"^\| ([^|]+?) \| (\d+) \| (\d+) \|", re.M)   # | op | iters | cycles | ...
SEC = re.compile(r"^#{3,4} (.+)$", re.M)


def run_bench(path):
    """运行一个基准，返回 {操作名: 最小 cycles}。同名操作跨节取最小（如 keygen 在 SM2 节）。"""
    try:
        out = subprocess.run([path], capture_output=True, text=True, timeout=600).stdout
    except (OSError, subprocess.TimeoutExpired) as e:
        print("运行 %s 失败：%s" % (path, e), file=sys.stderr)
        return None
    ops = {}
    for m in ROW.finditer(out):
        op, cyc = m.group(1).strip(), int(m.group(3))
        if op not in ops or cyc < ops[op]:
            ops[op] = cyc
    return ops


def merge_min(dst, src):
    for k, v in src.items():
        if k not in dst or v < dst[k]:
            dst[k] = v


# 对照口径：GmSSL 操作名 → 本库操作名（None 表示不参与对照，仅列出）
PAIRS = [
    ("SM3 哈希", [
        ("hash 1KB", "hash 1KB", "SM3 1KB"),
        ("hash 4KB", "hash 4KB", "SM3 4KB"),
        ("HMAC-SM3 (256B)", "HMAC-SM3 (256B)", "HMAC-SM3 256B"),
    ]),
    ("SM4 分组密码", [
        ("ECB encrypt block", "ECB encrypt block", "SM4 ECB 单块加密"),
        ("CBC encrypt 1KB", "CBC encrypt 1KB", "SM4-CBC 1KB"),
        ("CTR xor 1KB", "CTR xor 1KB", "SM4-CTR 1KB"),
    ]),
    ("SM2 椭圆曲线（本库 opt 含 comb 基点表）", [
        ("keygen (k·G 基点乘)", "keygen", "基点乘 k·G"),
        ("scalarmult (k·P 变基点)", "ecdh", "变基点乘 k·P（本库取 ecdh）"),
        ("sign", "sign", "签名（含 nonce 生成）"),
        ("verify", "verify", "验签"),
        ("ecdh (k·P 变基点)", None, "ECDH（GmSSL 侧，同变基点乘）"),
    ]),
    ("域算术 Fp（根因定位：标量乘差异来自算法还是域运算）", [
        ("fe_mul", "fe_mul", "域乘法"),
        ("fe_sqr", "fe_sqr", "域平方"),
        ("fe_inv", "fe_inv", "域求逆"),
    ]),
]

# 需要单独说明的行（不参与"更快/更慢"判定）
NOTES = {
    "rand_bytes(32) 熵源系统调用（参考）":
        "GmSSL 的 do_sign 每次签名都向系统 CSPRNG 取 k（rand_win 为系统调用）；"
        "本库用 HMAC-SM3 派生 nonce（无系统调用）。该行用于扣除该项影响。",
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--ours", action="append", default=[])
    ap.add_argument("--gmssl", required=True)
    ap.add_argument("--passes", type=int, default=3)
    a = ap.parse_args()

    ours_labels = [s.split(":", 1)[0] for s in a.ours]
    ours_paths = [s.split(":", 1)[1] for s in a.ours]

    best = {lbl: {} for lbl in ours_labels}
    gm = {}
    for p in range(a.passes):
        for lbl, path in zip(ours_labels, ours_paths):
            r = run_bench(path)
            if r is None:
                return 2
            merge_min(best[lbl], r)
        r = run_bench(a.gmssl)
        if r is None:
            return 2
        merge_min(gm, r)
        print("  第 %d/%d 轮完成" % (p + 1, a.passes))

    lines = []
    lines.append("# 第三方基线对照报告（GmSSL %s）\n" % "3.1.1")
    lines.append("> 生成方式：`bash scripts/thirdparty_bench.sh`（可复现）。")
    lines.append("> 测量方式：%d 轮**交错**运行两侧二进制，每侧每操作取最小值。" % a.passes)
    lines.append("> 加速比 = **GmSSL cycles ÷ 本库 cycles**，>1 表示本库更快（<1 表示本库更慢）。\n")
    lines.append("## 为什么选 GmSSL 作为基线\n")
    lines.append("- 本仓库原有的全部加速比都是「本库 REF vs 本库 OPT」——**自己比自己**，对评审不可比。")
    lines.append("- GmSSL 是国内事实上的国密参考实现，且同为**可移植 C**（非 OpenSSL 的手写汇编），")
    lines.append("  面向 MCU 的移植性可比；用它作外部锚点比用 OpenSSL 公平。")
    lines.append("- **注意**：mbedTLS 主线（3.6.x）**不含** SM2/SM3/SM4，不能作为国密基线（已核实）。\n")

    for sec, items in PAIRS:
        lines.append("## %s\n" % sec)
        cols = ["操作"] + ["GmSSL cycles"] + ["本库 %s cycles" % l for l in ours_labels] \
               + ["本库 opt / GmSSL"]
        lines.append("| " + " | ".join(cols) + " |")
        lines.append("|" + "---|" * len(cols))
        for gm_op, our_op, label in items:
            if gm_op not in gm:
                continue
            row = [label, "{:,}".format(gm[gm_op])]
            for lbl in ours_labels:
                c = best[lbl].get(our_op) if our_op else None
                row.append("{:,}".format(c) if c else "—")
            if our_op and best["opt"].get(our_op):
                r = gm[gm_op] / best["opt"][our_op]
                row.append("**%.2f×** %s" % (r, "（本库快）" if r > 1.05 else
                                             ("（本库慢）" if r < 0.95 else "（持平）")))
            else:
                row.append("—")
            lines.append("| " + " | ".join(row) + " |")
        lines.append("")

    lines.append("## 单列说明项\n")
    for op, note in NOTES.items():
        if op in gm:
            lines.append("- **%s = %s cycles**（%.1f µs）：%s"
                         % (op, "{:,}".format(gm[op]), gm[op] * 0.3 / 1000.0, note))
    lines.append("")

    lines.append("## 口径与限制（务必与数字同时引用）\n")
    lines.append("1. **API 包装层次不完全相同**：本库 `sign` 由 HMAC-SM3 派生 nonce 且入参是")
    lines.append("   预哈希的 e；GmSSL `sm2_do_sign` 每次向系统 CSPRNG 取 k。两者都算「含 nonce 生成」，")
    lines.append("   但 GmSSL 的熵源是系统调用（上表已单列其开销），扣除后可比性更好。")
    lines.append("2. **本库 `ecdh` 与 GmSSL `sm2_point_mul` 同为变基点 k·P**；GmSSL 另有 `sm2_do_ecdh`")
    lines.append("   （含密钥结构处理），数值与 `sm2_point_mul` 相近，故以变基点标量乘为准。")
    lines.append("3. **未测**：密钥/证书编解码、ASN.1、PEM、TLS 记录层——本项目不对标这些功能。")
    lines.append("4. 两侧均为 `-O2` 单线程 x86-64；GmSSL 的高性能汇编路径（如 `sm3_x8_avx2.c`、")
    lines.append("   `sm4_aesni_avx.c`）**未编入**（保持纯 C 可比口径）。")
    lines.append("5. 跨会话的主机频率状态仍会影响绝对值，故比值取自交错测量的最小值；")
    lines.append("   若两侧比值与本文档差异超过 ±15%，请重跑本脚本确认。")

    text = "\n".join(lines) + "\n"
    with open(a.out, "w", encoding="utf-8") as fh:
        fh.write(text)
    print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
