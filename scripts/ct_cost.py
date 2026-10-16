#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ct_cost.py — 交错测量 CT=0 / CT=1 两个构建的标量乘耗时，报告"常量时间的性能代价"

用法（由 scripts/ct_cost.sh 调用）：
    python scripts/ct_cost.py <bench_sct_opt> <bench_sct_ct> [轮数]

口径（关键，见文末"口径说明"）：交替运行两侧；**判据取同一输入集上的均值**。
变时间路径的耗时随输入变化，其最小值来自最省的那个标量，与常量时间路径的最小值
不可比——跨输入比最小值会把 CT 代价虚报近一倍。
"""
import re
import subprocess
import sys

# 指标② 行：| 路径 | 最小 | 中位 | 最大 | 均值 | CV | 极差/均值 |
ROW2 = re.compile(
    r"^\|\s*([^|]*?)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*(\d+)\s*\|\s*([\d.]+)\s*\|\s*([\d.]+)\s*\|",
    re.M)
# 指标① 行：| 路径 | a / b / c | a / b / c | 判定 |
ROW1 = re.compile(
    r"^\|\s*([^|]*?)\s*\|\s*(\d+)\s*/\s*(\d+)\s*/\s*(\d+)\s*\|\s*(\d+)\s*/\s*(\d+)\s*/\s*(\d+)\s*\|", re.M)


def run(path):
    out = subprocess.run([path], capture_output=True, text=True, timeout=900,
                         encoding="utf-8", errors="replace").stdout
    ops, timing = {}, {}
    for m in ROW1.finditer(out):
        ops.setdefault(m.group(1), (int(m.group(2)), int(m.group(3)),
                                    int(m.group(5)), int(m.group(6))))
    for m in ROW2.finditer(out):
        timing.setdefault(m.group(1), (int(m.group(2)), int(m.group(4)), int(m.group(5))))
    return ops, timing          # timing: (最小, 最大, 均值)


def path_key(name):
    if "基点" in name:
        return "基点"
    if "通用" in name:
        return "通用"
    return None


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    if len(sys.argv) < 3:
        print("用法: python scripts/ct_cost.py <bench_sct_opt> <bench_sct_ct> [轮数]")
        return 2
    p0, p1 = sys.argv[1], sys.argv[2]
    rounds = int(sys.argv[3]) if len(sys.argv) > 3 else 5

    best = [{}, {}]
    ops = [{}, {}]
    for r in range(rounds):
        for i, p in enumerate((p0, p1)):
            try:
                o, t = run(p)
            except Exception as e:                                  # noqa: BLE001
                print("运行 %s 失败：%s" % (p, e))
                return 2
            if not ops[i]:
                for name, v in o.items():
                    k = path_key(name)
                    if k:
                        ops[i][k] = v
            for name, v in t.items():
                k = path_key(name)
                if not k:
                    continue
                if k in best[i]:
                    best[i][k] = tuple(min(a, b) for a, b in zip(best[i][k], v))
                else:
                    best[i][k] = v
        print("  第 %d/%d 轮完成" % (r + 1, rounds), file=sys.stderr)

    L = []
    L.append("# 常量时间的性能代价（CT=0 vs CT=1，交错测量）\n")
    L.append("> 生成方式：`bash scripts/ct_cost.sh`（%d 轮交替运行两侧，各取多轮最小值）" % rounds)
    L.append("> 比值 = **CT=1 ÷ CT=0**，1.00 表示常量时间不带来额外开销\n")

    L.append("## 运算次数（确定性指标，与机器无关）\n")
    L.append("| 标量乘路径 | 构建 | 点加 最小/最大/极差 | 倍点 最小/最大/极差 |")
    L.append("|---|---|---|---|")
    for i, lbl in enumerate(("CT=0（变时间）", "CT=1（常量时间）")):
        for k in ("通用", "基点"):
            if k in ops[i]:
                a0, a1, d0, d1 = ops[i][k]
                L.append("| %s | %s | %d / %d / %d | %d / %d / %d |"
                         % (k, lbl, a0, a1, a1 - a0, d0, d1, d1 - d0))
    L.append("")

    L.append("## 耗时与代价（cycles）\n")
    L.append("**判据用「同一输入集上的均值」**，不取最小值——原因见文末口径说明。\n")
    L.append("| 标量乘路径 | CT=0 均值 | CT=1 均值 | **CT 代价（均值，判据）** | CT=0 最坏 | CT=1 最坏 |")
    L.append("|---|---|---|---|---|---|")
    for k in ("基点", "通用"):
        if k in best[0] and k in best[1]:
            _, x0, v0 = best[0][k]
            _, x1, v1 = best[1][k]
            L.append("| %s | %s | %s | **%.3f×** | %s | %s |"
                     % (k, "{:,}".format(v0), "{:,}".format(v1), v1 / v0,
                        "{:,}".format(x0), "{:,}".format(x1)))
    L.append("")

    if "基点" in best[0] and "基点" in best[1]:
        v0 = best[0]["基点"][2]
        v1 = best[1]["基点"][2]
        L.append("## 结论\n")
        L.append("- **基点路径（签名/密钥生成的 k·G）**：常量时间的代价约 **%.0f%%**（均值口径）。"
                 % ((v1 / v0 - 1) * 100))
        L.append("  该路径在 CT 构建下改为「静态仿射 comb 表 + 全表扫描 cmov」：")
        L.append("  运算次数由 257 倍点 / 78 点加降到 **51 倍点 / 51 点加**且极差为 0；")
        L.append("  扫描的廉价字操作（每窗口 32 项 × 2 域的 cmov）远低于它省下的 206 次倍点，")
        L.append("  因此**用预计算表与常量时间并不冲突**——这是本项优化要说明的核心事实。")
        if "通用" in best[0] and "通用" in best[1]:
            g0 = best[0]["通用"][2]
            g1 = best[1]["通用"][2]
            L.append("- **通用路径（变基点 tP，验签/ECDH）**：代价约 **%.0f%%**（均值口径）。"
                     "该路径无预计算表可用，只能退回全表扫描固定窗口。"
                     % ((g1 / g0 - 1) * 100))
        L.append("- 对照：改造前（CT 构建**完全放弃**预计算表）基点路径代价约 **2.8×**，见 docs/04。")
        L.append("")
        L.append("## 口径说明（务必与数字同时引用）\n")
        L.append("1. **判据取均值，不取最小值。** 变时间路径的耗时随输入变化：其最小值来自")
        L.append("   输入集中最省的那个标量（稀疏值只触发 2 次点加），而常量时间路径对任何输入")
        L.append("   都做满 51 次点加。跨输入比较最小值，等于拿「最快的标量」比「每个标量」，")
        L.append("   会把 CT 代价虚报为约 1.7×（本脚本早期版本即犯此错，已修正）。")
        L.append("2. 输入集包含 2 个对抗性极端值（n−1、稀疏），变时间路径无法规避它们；")
        L.append("   而 CT 路径本就不利用输入特征——故该口径对 CT 略偏保守（略高估其代价）。")
        L.append("3. 交错运行 + 各取多轮最小值以过滤主机调度与频率漂移；")
        L.append("   <10% 的差值必须交错测量才可信（顺序快照曾被漂移掩盖）。")
        L.append("4. 运算次数为确定性指标，与主机无关——它是常量时间性质的**主要证据**；")
        L.append("   时间数据仅用于给出代价量级。")

    print("\n".join(L))
    return 0


if __name__ == "__main__":
    sys.exit(main())
