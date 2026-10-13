#!/bin/bash
# check_embedded.sh — 板级代码的上板前检查（无需 ARM 工具链也能跑大部分）
#
# 做三件事：
#   ① 宿主仿真：用本机 gcc 编译并**执行**同一份板级代码（KAT、测量循环、打印、TRNG 自检）
#      —— 上板前就能发现编译错误、KAT 写错、格式串问题、流程漏初始化；
#   ② 语法检查：对全部板级源文件做 -fsyntax-only，覆盖仿真未编译到的分支；
#   ③ 交叉构建（可选）：若 PATH 里有 arm-none-eabi-gcc，则做真实交叉编译 + size，
#      并用 32K 链接脚本证明"适配 <32KB Flash"。
#
# 用法：bash scripts/check_embedded.sh          （三步都尝试，缺工具链时跳过第③步）
# 退出码：0 = 通过；非 0 = 有问题（CI 以此作为门禁）
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
CM4=embedded/cortex-m4
INC="-I$CM4 -I$ROOT/crypto/include -I$ROOT/crypto/src -I$ROOT/embedded/port"

echo "==== ① 宿主仿真：编译并执行板级代码 ===="
( cd $CM4 && bash build.sh sim > /tmp/sim.log 2>&1 ) || {
  echo "失败：宿主仿真未通过，日志尾部："; tail -25 /tmp/sim.log; exit 1
}
# 断言：KAT 全过 + 无失败项 + 10 项基准都有 RESULT 行
fails=$(grep -c "FAIL" /tmp/sim.log || true)
results=$(grep -c "^RESULT bench\." /tmp/sim.log || true)
summary=$(grep -o "RESULT summary.fail=[0-9]*" /tmp/sim.log | tail -1)
echo "  KAT/自检 FAIL 数：$fails（应 0）"
echo "  基准 RESULT 行数：$results（应 10）"
echo "  $summary（应 fail=0）"
[ "$fails" -eq 0 ] || { echo "失败：仿真中出现 FAIL"; exit 1; }
[ "$results" -eq 10 ] || { echo "失败：基准项数不符（板级代码可能漏测）"; exit 1; }
echo "$summary" | grep -q "fail=0" || { echo "失败：自检汇总非 0"; exit 1; }

echo ""
echo "==== ② 语法检查（-fsyntax-only，覆盖仿真未编译分支）===="
CC=${CC:-gcc}
SYN="-std=c99 -fsyntax-only -Wall -Wextra -DSTM32F407xx $INC"
for f in $CM4/main.c $CM4/board_selftest.c $CM4/startup_stm32f407.c \
         $CM4/system_stm32f4xx_stub.c $CM4/host_sim_main.c embedded/port/smc_port_stm32.c; do
  $CC $SYN $f && echo "  ok  $f"
done

echo ""
echo "==== ③ 交叉编译 + 32K 链接证明（需要 arm-none-eabi-gcc）===="
if command -v arm-none-eabi-gcc >/dev/null 2>&1; then
  ( cd $CM4 && bash build.sh flash | tail -20 )
  ( cd $CM4 && bash build.sh 32k | tail -12 )
  echo "  已产出 smc_m4.elf/.bin 与 smc_m4_32k.elf/.bin"
else
  echo "  跳过：未安装 arm-none-eabi-gcc（本机/CI 常见）。"
  echo "  安装方法见 docs/08 §1；装好后重跑本脚本即可完成真实尺寸与 32K 适配证明。"
fi

echo ""
echo "板级检查完成。"
