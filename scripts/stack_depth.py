#!/usr/bin/env python3
"""
stack_depth.py — 由 -fstack-usage 与反汇编计算"最大调用路径栈深"

用途：链接期任务栈预算的核心依据。
  -fstack-usage 只给出**单函数**自身帧大小，而实际需要的栈是调用链上所有帧之和。
  本脚本用 objdump 提取同一批目标文件内的 call 边，构建调用图后求最长加权路径。

用法：
    python scripts/stack_depth.py obj/size/*_full.su [--objs obj/size/*.o] [--top 10]

口径与限制（报告中须如实标注）：
  * 权重 = 被调用函数自身的静态帧（.su），不含被调用者返回地址等 ABI 开销；
  * 通过函数指针的间接调用、以及本批 .o 之外的库调用（memcpy 等）不在图中，故为**下界**；
    实际预留应在此基础上加安全余量（中断/RTOS 上下文 + 编译器版本差异）。
"""
import glob
import os
import re
import subprocess
import sys
from collections import defaultdict

FRAME = {}
CONFLICT = {}
RE_HEADER = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
RE_CALL = re.compile(r"\bcallq?\s+[0-9a-f]*\s*<([^>]+)>")


def parse_su(paths):
    for p in paths:
        with open(p, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                parts = line.rstrip("\n").split("\t")
                if len(parts) < 2:
                    continue
                head = parts[0]
                try:
                    nbytes = int(parts[1])
                except ValueError:
                    continue
                name = head.split(":")[-1]
                if not name:
                    continue
                if name in FRAME and FRAME[name] != nbytes:
                    # 同名符号出现在不同 .o（不同配置/不同文件的 static）——记录并取较大值
                    CONFLICT.setdefault(name, set()).update({FRAME[name], nbytes})
                FRAME[name] = max(FRAME.get(name, 0), nbytes)


def parse_calls(objs):
    edges = defaultdict(set)
    for o in objs:
        try:
            dis = subprocess.run(["objdump", "-d", o], capture_output=True,
                                 text=True, errors="replace").stdout
        except OSError:
            return None
        cur = None
        for line in dis.splitlines():
            m = RE_HEADER.match(line.strip())
            if m:
                cur = m.group(1)
                continue
            if cur:
                c = RE_CALL.search(line)
                if c:
                    edges[cur].add(c.group(1))
    return edges


def max_path(edges):
    """返回 (最长路径列表, 深度)。含递归保护：路径上重复出现的节点不再累加其子树。"""
    memo = {}
    guard = set()

    def walk(n):
        if n in guard:                      # 递归调用：不再展开
            return (0, [])
        if n in memo:
            return memo[n]
        guard.add(n)
        best = (0, [])
        for c in sorted(edges.get(n, ())):
            if c not in FRAME:
                continue
            sub, path = walk(c)
            if sub > best[0]:
                best = (sub, path)
        guard.discard(n)
        res = (FRAME.get(n, 0) + best[0], [n] + best[1])
        memo[n] = res
        return res

    overall = (0, [])
    for n in FRAME:
        d, path = walk(n)
        if d > overall[0]:
            overall = (d, path)
    return overall


def main():
    args = sys.argv[1:]
    su = []
    objs = []
    top = 10
    tsv = False
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--objs":
            i += 1
            objs.extend(glob.glob(sys.argv[i]))
        elif a == "--top":
            i += 1
            top = int(sys.argv[i])
        elif a == "--tsv":
            tsv = True
        else:
            su.extend(glob.glob(a))
        i += 1

    if not su:
        print("用法：python scripts/stack_depth.py <*.su> [--objs <*.o>] [--top N] [--tsv]")
        return 2
    if not objs:
        objs = sorted(set(os.path.splitext(p)[0] + ".o" for p in su))
        objs = [o for o in objs if os.path.exists(o)]

    parse_su(su)
    edges = parse_calls(objs)
    if edges is None:
        print("需要 objdump（binutils）才能计算调用路径栈深。")
        return 2

    depth, path = max_path(edges)
    topfn = sorted(FRAME.items(), key=lambda kv: -kv[1])[:1]

    if tsv:
        # maxframe_name \t maxframe_bytes \t path_depth \t path(以 > 连接)
        print("%s\t%d\t%d\t%s" % (topfn[0][0] if topfn else "-",
                                  topfn[0][1] if topfn else 0,
                                  depth, " > ".join(path)))
        return 0

    print("### 最大调用路径栈深（.su 帧 + objdump 调用图）\n")
    print("| 指标 | 值 |")
    print("|---|---|")
    print("| 参与分析的函数数 | %d |" % len(FRAME))
    print("| 最大调用路径总栈深（下界） | **%d B** |" % depth)
    print("")
    print("最长路径（自外向内，括号为各帧字节）：")
    print("")
    print("    " + " →\n    ".join("%s(%d)" % (n, FRAME.get(n, 0)) for n in path))
    print("")

    # 单函数最大帧 top N
    print("单函数最大帧 top%d（仅自身帧，不含被调用者）：\n" % top)
    print("| 函数 | 帧 (B) |")
    print("|---|---|")
    for n, b in sorted(FRAME.items(), key=lambda kv: -kv[1])[:top]:
        print("| `%s` | %d |" % (n, b))
    if CONFLICT:
        print("")
        print("> 注意：以下同名符号在不同目标文件中帧大小不同，已取较大值：%s"
              % ", ".join(sorted(CONFLICT)))
    print("")
    print("> 口径：权重为各函数自身静态帧之和，**不含**返回地址/寄存器保存等 ABI 开销，"
          "且函数指针间接调用不在图中——因此该值为下界，任务栈应在此之上留安全余量。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
