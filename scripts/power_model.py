#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
power_model.py — 能量/功耗估算模型（参数化，标注为模型估算）

  E(op) = cycles/op × E_cycle
  E_cycle = V_core × I_dyn(µA/MHz) / 1000 / 1000   [单位 nJ/cycle]

默认参数取典型低功耗 Cortex-M4（STM32L4 级）：V=3.0V, I_dyn=100µA/MHz, f=26MHz。
替换为实测板级参数即可得到实测功耗（接口预留）。
解析 bin/bench_results.md 的基准表，输出每操作能量与"终端日能耗"场景估算。
"""
import re
import sys

V_CORE = 3.0          # V
I_UA_PER_MHZ = 100.0  # µA/MHz（动态电流系数）
F_MHZ = 26.0          # 目标 MCU 主频（仅用于折算平均电流/功率）

E_CYCLE_NJ = V_CORE * I_UA_PER_MHZ / 1000.0   # nJ/cycle = 0.30
P_MW = V_CORE * I_UA_PER_MHZ * F_MHZ / 1000.0 # mW

def main():
    path = "bin/bench_results.md"
    try:
        text = open(path, encoding="utf-8").read()
    except OSError:
        print("（找不到 %s，先运行 scripts/benchmark.sh）" % path)
        return

    print("| operation | cycles/op | energy/op (µJ) @3.0V/100µA/MHz |")
    print("|---|---|---|")
    pat = re.compile(r"^\| ([^|]+?) \| (\d+) \| ([\d.]+) \| ([\d.]+) \|", re.M)
    rows = []
    for m in pat.finditer(text):
        name, iters, cyc, us = m.group(1).strip(), m.group(2), float(m.group(3)), m.group(4)
        e_uj = float(cyc) * E_CYCLE_NJ / 1000.0
        rows.append((name, cyc, e_uj))
        print("| %s | %s | %.3f |" % (name, cyc, e_uj))

    print()
    print("> 模型参数：V_core=%.1fV, I_dyn=%.0fµA/MHz, f=%.0fMHz → %.2fmW, %.2fnJ/cycle"
          % (V_CORE, I_UA_PER_MHZ, F_MHZ, P_MW, E_CYCLE_NJ))
    print("> 注：本表为能量模型估算（非实测功耗）；将 V/I 参数替换为板级实测值即得实测结果。")

    # 场景估算：传感终端每日 288 条遥测（5 分钟一次）+ 1 次握手
    print()
    print("#### 场景估算：低功耗传感终端（5 分钟 1 条遥测，日 288 条 + 1 次握手）")
    print("| 模式 | 握手能耗 (mJ) | 日通信总能耗 (mJ) | 占 2×AA 电池(2000mAh@3V=21600J) |")
    print("|---|---|---|---|")
    scen = [("FULL  (每会话 5×SM2+KDF)", 5), ("RESUME (0×SM2)", 0), ("PSK (0×SM2, 0-RTT)", 0)]
    sm2_e = None
    for name, cyc, e in rows:
        if name == "sign":
            sm2_e = e
    if sm2_e is None:
        sm2_e = 200.0
    for label, sm2ops in scen:
        hs_mj = (sm2ops * sm2_e + 60.0) / 1000.0          # 握手 ≈ SM2 运算 + 60µJ 固定开销
        day_mj = hs_mj + 288 * 0.02                       # 每条遥测 ≈ 20µJ（SM4+SM3+记录）
        pct = day_mj / 1000.0 / 21600.0 * 100.0
        print("| %s | %.2f | %.2f | %.4f%% |" % (label, hs_mj, day_mj, pct))
    print("> 注：遥测单条能耗按 SM4-CTR(48B)+HMAC-SM3+记录封装实测量级折算，模型估算。")

if __name__ == "__main__":
    main()
