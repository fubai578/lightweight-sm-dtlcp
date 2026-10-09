#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
power_model.py — 能量/功耗估算模型（参数化，标注为模型估算）

  E(op) = cycles/op × E_cycle
  E_cycle = V_core × I_dyn(µA/MHz) / 1000 / 1000   [单位 nJ/cycle]

默认参数取典型低功耗 Cortex-M4（STM32L4 级）：V=3.0V, I_dyn=100µA/MHz, f=26MHz。
替换为实测板级参数即可得到实测功耗（接口预留）。

解析 bin/bench_results.md，**按内核变体分节**输出每操作能量；
场景估算统一取 `[opt]` 节（默认构建），并按 DTLCP FULL 握手的**真实运算构成**折算：
  2×keygen（双方临时密钥）+ 1×sign + 1×verify + 1×ecdh
（与 dtlcp_hs.c 的 eph_keygen×2 / send_signature / check_signature / shared_secret 对应；
 早期版本按"5×sign"折算会系统性偏差，已修正）。
"""
import re
import sys

V_CORE = 3.0          # V
I_UA_PER_MHZ = 100.0  # µA/MHz（动态电流系数）
F_MHZ = 26.0          # 目标 MCU 主频（仅用于折算平均电流/功率）

E_CYCLE_NJ = V_CORE * I_UA_PER_MHZ / 1000.0    # nJ/cycle = 0.30
P_MW = V_CORE * I_UA_PER_MHZ * F_MHZ / 1000.0  # mW

# 握手固定开销（KDF/HMAC-SM3/编解码，非 SM2 部分）µJ，取自记录层实测微秒量级折算
HANDSHAKE_FIXED_UJ = 60.0
# 单条遥测能耗 µJ：SM4-CTR(48B)+HMAC-SM3+记录封装实测量级
TELEMETRY_UJ = 20.0

ROW = re.compile(r"^\| ([^|]+?) \| (\d+) \| (\d+) \| ([\d.]+) \|", re.M)
SEC = re.compile(r"^## \[([^\]]+)\]", re.M)


def parse(path):
    """返回 {节名: {操作名: cycles}}，保持文件顺序。

    基准行格式：| operation | iters | cycles/op | us/op | ops/s |
    故 cycles 取第 3 列（第 2 列是迭代次数，早期版本误取会导致能量低估约 5 个数量级）。
    """
    text = open(path, encoding="utf-8").read()
    secs = {}
    marks = [(m.group(1), m.start()) for m in SEC.finditer(text)]
    for i, (name, pos) in enumerate(marks):
        end = marks[i + 1][1] if i + 1 < len(marks) else len(text)
        d = {}
        for m in ROW.finditer(text[pos:end]):
            op, cyc = m.group(1).strip(), int(m.group(3))
            d.setdefault(op, cyc)
        secs[name] = d
    return secs


def e_uj(cycles):
    return cycles * E_CYCLE_NJ / 1000.0


def main():
    path = "bin/bench_results.md"
    try:
        secs = parse(path)
    except OSError:
        print("（找不到 %s，先运行 scripts/benchmark.sh）" % path)
        return
    if not secs:
        print("（%s 中未解析到内核节，先运行 scripts/benchmark.sh）" % path)
        return

    for name, d in secs.items():
        if not d:
            continue
        print("#### 内核 [%s] 每操作能量\n" % name)
        print("| operation | cycles/op | energy/op (µJ) |")
        print("|---|---|---|")
        for op, cyc in d.items():
            print("| %s | %d | %.3f |" % (op, cyc, e_uj(cyc)))
        print("")

    print("> 模型参数：V_core=%.1fV, I_dyn=%.0fµA/MHz, f=%.0fMHz → %.2fmW, %.2fnJ/cycle"
          % (V_CORE, I_UA_PER_MHZ, F_MHZ, P_MW, E_CYCLE_NJ))
    print("> 注：本表为能量模型估算（非实测功耗）；将 V/I 参数替换为板级实测值即得实测结果。")

    base = secs.get("opt") or next(iter(secs.values()))
    need = ("keygen", "sign", "verify", "ecdh")
    if not all(k in base for k in need):
        print("> 场景估算跳过：[opt] 节缺少 %s 之一。"
              % "/".join(k for k in need if k not in base))
        return

    def full_handshake_uj(sec):
        """FULL 握手：2×keygen + sign + verify + ecdh（见 dtlcp_hs.c）"""
        return (2 * e_uj(sec["keygen"]) + e_uj(sec["sign"])
                + e_uj(sec["verify"]) + e_uj(sec["ecdh"]) + HANDSHAKE_FIXED_UJ)

    full_hs = full_handshake_uj(base)
    hs = {
        "FULL  (2×keygen+sign+verify+ecdh)": full_hs,
        "RESUME (0×SM2，会话恢复)":            HANDSHAKE_FIXED_UJ,
        "PSK (0×SM2，0-RTT 早期数据)":         HANDSHAKE_FIXED_UJ,
    }
    print()
    print("#### 场景估算：低功耗传感终端（5 分钟 1 条遥测，日 288 条 + 1 次握手）")
    print()
    print("| 模式 | 握手能耗 (µJ) | 日通信总能耗 (µJ) | 占 2×AA 电池(2000mAh@3V=21600J) |")
    print("|---|---|---|---|")
    day_tel = 288 * TELEMETRY_UJ
    for label, hs_uj in hs.items():
        day = hs_uj + day_tel
        print("| %s | %.0f | %.0f | %.5f%% |" % (label, hs_uj, day, day / 1e6 / 21600.0 * 100.0))
    print()
    ref = secs.get("ref")
    if ref and all(k in ref for k in need):
        ref_hs = full_handshake_uj(ref)
        print("> 同口径 REF → OPT：FULL 握手能耗 %.0f µJ → %.0f µJ（**%.2f× 降低**）。"
              % (ref_hs, full_hs, ref_hs / full_hs))
    print("> FULL 按 [%s] 节实测 cycles 折算（2×keygen+sign+verify+ecdh ＋%.0fµJ 固定开销）；"
          % ("opt" if base is secs.get("opt") else "首个可用节", HANDSHAKE_FIXED_UJ))
    print("> RESUME/PSK 为 0 次 SM2；单条遥测按 SM4-CTR(48B)+HMAC-SM3+记录封装实测量级折算（%.0fµJ）。"
          % TELEMETRY_UJ)
    print("> 日能耗对比：RESUME/PSK 相对 FULL 省去 SM2 握手开销，残余为遥测与 KDF 固定开销。")


if __name__ == "__main__":
    main()
