#!/bin/bash
# benchmark.sh — REF vs OPT 基准对比 + 功耗估算
set -e
cd "$(dirname "$0")/.."
# 平台产物后缀（MinGW 需 .exe，Linux/macOS 无后缀）
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac

bash scripts/build.sh >/dev/null

OUT=bin/bench_results.md
{
echo "# 算法基准（实测：x86-64 host, gcc -O2, $(date +%Y-%m-%d)）"
echo ""
echo "> REF=教科书基线内核；OPT=指令级优化内核（wNAF/折叠约减/T-table）"
echo "> notab=与 OPT 同内核但关闭 SM2 基点 comb 预计算表（同轮 A/B：comb 净收益）"
echo "> TINY=最小内存内核"
echo ""
echo "> 口径：RDTSC 计数受 CPU 频率状态影响，**跨运行绝对值不可比**，横向对比请用同一次输出内的比值。"
echo ""
} > $OUT

for v in ref opt mcu notab tiny; do
  B=bin/bench_${v}$EXE
  if [ ! -f $B ]; then continue; fi
  echo "## [$v]" >> $OUT
  ./$B | sed -n '/^####/,$p' >> $OUT
  echo "" >> $OUT
done

echo "## 功耗估算（能量模型）" >> $OUT
python scripts/power_model.py >> $OUT 2>/dev/null || echo "（power_model.py 未运行）" >> $OUT

echo "结果已写入 $OUT"
