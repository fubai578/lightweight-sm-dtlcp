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
# 说明：反汇编输出可能有前导空格（不同 objdump 版本/Windows 版行为不一致），
#   故先 strip() 再匹配，且不强制 ^ 锚定。
RE_HEADER = re.compile(r"^[0-9a-f]+\s+<([^>]+)>:$")
# 覆盖两类目标：
#   x86   : call / callq  0x1234 <func+0x8>
#   ARM   : bl 0 <func>   （Thumb：bl 后跟偏移再跟符号名）
RE_CALL = re.compile(r"\b(?:callq?|bl|blx)\s+(?:[0-9a-fx]*\s*)?<([^>]+)>")
# 可重定位 ARM .o 中，bl 的反汇编目标常显示为当前函数/0；下一行 relocation
# 才给出真实 callee，例如 `R_ARM_THM_CALL sm2_verify`。
RE_RELOC_CALL = re.compile(
    r"\bR_(?:ARM|AARCH64)_(?:[A-Z0-9_]*CALL|[A-Z0-9_]*JUMP\d*)\s+([^\s]+)"
)


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


def strip_sym(s):
    """去掉 objdump 符号上的偏移后缀：'foo+0x8' → 'foo'。
    ARM 的 Thumb 指令与 x86 一样会打印 `func+0x8`，不剥离就匹配不上 .su 里的纯函数名。"""
    return s.split("+")[0].split("-0x")[0].strip()


def parse_calls(objs, objdump="objdump"):
    """提取目标文件内的 call 边。

    objdump 可通过 --objdump 指定（如 arm-none-eabi-objdump）。
    原因：交叉编译 ARM 固件时，宿主 PATH 里往往只有 x86 的 objdump，
    它读不了 ARM ELF；硬编码 "objdump" 会静默返回空 → 调用图全空 →
    栈深被低估为 0（本项目踩过，教训记录见项目内部笔记）。
    """
    edges = defaultdict(set)
    for o in objs:
        try:
            dis = subprocess.run([objdump, "-dr", o], capture_output=True,
                                 text=True, errors="replace").stdout
        except OSError:
            return None
        if not dis.strip():
            return None                        # 空输出＝工具不对/读不了该格式，视为失败
        cur = None
        pending = None
        for line in dis.splitlines():
            reloc = RE_RELOC_CALL.search(line)
            if reloc and cur:
                caller = pending[0] if pending else cur
                edges[caller].add(strip_sym(reloc.group(1)))
                pending = None
                continue
            if pending:
                caller, callee = pending
                edges[caller].add(callee)
                pending = None
            m = RE_HEADER.match(line.strip())
            if m:
                cur = strip_sym(m.group(1))
                continue
            if cur:
                c = RE_CALL.search(line)
                if c:
                    pending = (cur, strip_sym(c.group(1)))
        if pending:
            edges[pending[0]].add(pending[1])
    return edges


def max_path(edges):
    """返回 (最长路径列表, 深度)。含递归保护：路径上重复出现的节点不再累加其子树。

    ★ 修复一个原有的缺陷（ARM 交叉编译时才暴露）：
      旧实现在**递归返回后**才 guard.discard(n)，但 memo 缓存的是
      "在 guard 集合非空时算出的中间结果"。一旦某个函数先被一条深路径访问过，
      它被剪枝后的 (深度, 路径) 就被缓存下来；后续所有查询复用这个残缺值，
      最终最长路径退化成单个节点（本项目在 ARM 上实测：1712 B 而非真实值）。
      正确做法：memo 只缓存**不在 guard 中**的完整结果；guard 命中时
      返回 (0,[]) 且**不写入 memo**。
    """
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
    objdump = "objdump"
    i = 1
    while i < len(sys.argv):
        a = sys.argv[i]
        if a == "--objs":
            i += 1
            objs.extend(glob.glob(sys.argv[i]))
        elif a == "--objdump":
            i += 1
            objdump = sys.argv[i]
        elif a == "--top":
            i += 1
            top = int(sys.argv[i])
        elif a == "--tsv":
            tsv = True
        else:
            su.extend(glob.glob(a))
        i += 1

    if not su:
        print("用法：python scripts/stack_depth.py <*.su> [--objs <*.o>] "
              "[--objdump <objdump>] [--top N] [--tsv]")
        return 2
    if not objs:
        objs = sorted(set(os.path.splitext(p)[0] + ".o" for p in su))
        objs = [o for o in objs if os.path.exists(o)]

    parse_su(su)
    edges = parse_calls(objs, objdump)
    if edges is None:
        print("需要 objdump（binutils）才能计算调用路径栈深。")
        print("  交叉编译场景请显式指定：--objdump arm-none-eabi-objdump")
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
