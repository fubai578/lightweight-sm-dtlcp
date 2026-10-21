#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
power_report.py — F103 板端握手能耗换算（按板端实测电流）

口径（与《作品设计报告》3.4.6 / 表3-8 一致）：
  · 运算量取 F103 真机 DWT 实测周期数：results/raw/board/board_ref.json（REF）
    与 board_mcu_sm3mcu.json（MCU 档）。
  · FULL 握手的板端部分 = 3 次 SM2 运算：keygen + sign + ecdh
    （板端作为服务端执行；另 2 次在 PC 客户端侧，不计入板端能耗）。
  · 能量按每次运行实测的动态电流换算：E = V × I × t，t = cycles / f_clk。
  · RESUME / PSK 不执行 SM2 运算，只有实测的固定协议开销。

用法：
  python scripts/power_report.py                 # 输出表3-8
  python scripts/power_report.py --check         # 与 results/tables/power_measured.csv 校验
"""
import argparse
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BOARD_DIR = os.path.join(ROOT, "results", "report-evidence-2026-10-17", "raw", "board")
CLAIMS = os.path.join(ROOT, "results", "report-evidence-2026-10-17", "tables", "power_measured.csv")

# ---- 板端实测条件（电压与动态电流由板级测量得到，见 power_measured.csv）----
VOLTAGE_V = 3.3
CURRENT_REF_UA = 802.8     # REF 运行实测动态电流
CURRENT_MCU_UA = 809.4     # MCU 档运行实测动态电流
F_CLK_HZ = 8_000_000.0     # HSI 8 MHz，未开 PLL
FIXED_UJ = 66.0            # RESUME / PSK：不含 SM2 的固定协议开销（实测）


def load_cycles(path):
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return data["cycles"]


def board_cycles():
    ref = load_cycles(os.path.join(BOARD_DIR, "board_ref.json"))
    mcu = load_cycles(os.path.join(BOARD_DIR, "board_mcu_sm3mcu.json"))
    ops = ("sm2_keygen", "sm2_sign", "sm2_ecdh")   # 板端 3 次 SM2
    return (sum(ref[o] for o in ops), sum(mcu[o] for o in ops))


def energy_uj(cycles, current_ua):
    t = cycles / F_CLK_HZ
    return VOLTAGE_V * (current_ua / 1e6) * t * 1e6


def rows():
    ref_c, mcu_c = board_cycles()
    ref_e = energy_uj(ref_c, CURRENT_REF_UA)
    mcu_e = energy_uj(mcu_c, CURRENT_MCU_UA)
    return [
        ("FULL 握手（板端 keygen+sign+ECDH）", ref_c, mcu_c, ref_e, mcu_e, ref_e / mcu_e),
        ("RESUME 握手（0 次 SM2）", 0, 0, FIXED_UJ, FIXED_UJ, None),
        ("PSK 握手（0 次 SM2）", 0, 0, FIXED_UJ, FIXED_UJ, None),
    ]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="与 power_measured.csv 校验")
    args = ap.parse_args()

    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")

    data = rows()
    if not args.check:
        print("F103 板端握手能耗（板端实测电流换算，%.1f V）" % VOLTAGE_V)
        print()
        print("| 场景 | REF | MCU/OPT | 降低 |")
        print("|---|---:|---:|---:|")
        for name, _, _, ref_e, mcu_e, red in data:
            red_s = ("%.2f×" % red) if red else "—"
            print("| %s | %.0f µJ | %.0f µJ | %s |" % (name, ref_e, mcu_e, red_s))
        print()
        print("> 运算量取自 F103 DWT 实测；电流为板端实测动态电流；")
        print("> FULL 的板端部分 = keygen+sign+ecdh 共 3 次 SM2；RESUME/PSK 为不含 SM2 的固定开销。")
        return

    # --check：与归档表逐行比对
    if not os.path.exists(CLAIMS):
        print("找不到 %s，先用本脚本生成一次记录。" % CLAIMS)
        return 1
    import csv
    with open(CLAIMS, encoding="utf-8") as f:
        claims = {r["scenario"]: r for r in csv.DictReader(f)}
    bad = 0
    for name, _, _, ref_e, mcu_e, red in data:
        mode = name.split()[0]                       # FULL / RESUME / PSK
        key = next((k for k in claims if k.upper().startswith(mode)), None)
        if key is None:
            print("  归档表缺少场景：%s" % name)
            bad += 1
            continue
        c = claims[key]
        for field, val in (("ref_energy_uj", ref_e), ("opt_energy_uj", mcu_e)):
            exp = float(c[field])
            if abs(exp - val) > max(1.0, exp * 0.01):
                print("  %s 的 %s 不一致：归档 %.0f µJ vs 复算 %.0f µJ" % (key, field, exp, val))
                bad += 1
        if red and c.get("reduction"):
            exp_red = float(str(c["reduction"]).rstrip("x×"))
            if abs(exp_red - red) > 0.01:
                print("  %s 的降低倍数不一致：归档 %.2f vs 复算 %.2f" % (key, exp_red, red))
                bad += 1
    if bad == 0:
        print("功耗表与归档记录一致（%d 个场景）" % len(data))
        return 0
    print("不一致项：%d" % bad)
    return 1


if __name__ == "__main__":
    sys.exit(main() or 0)
