#!/bin/bash
# benchmark.sh — REF vs OPT 基准对比 + 功耗估算
set -e
cd "$(dirname "$0")/.."
bash scripts/build.sh >/dev/null

OUT=bin/bench_results.md
{
echo "# 算法基准（实测：x86-64 host, gcc -O2, $(date +%Y-%m-%d)）"
echo ""
echo "> REF=教科书基线内核；OPT=指令级优化内核；TINY/SMALL=最小内存内核"
echo ""
} > $OUT

for v in ref opt tiny; do
  case $v in
    ref)  B=bin/bench_ref.exe;;
    opt)  B=bin/bench_opt.exe;;
    tiny) B=bin/bench_tiny.exe;;
  esac
  if [ ! -f $B ]; then continue; fi
  echo "## [$v]" >> $OUT
  ./$B | sed -n '/^####/,$p' >> $OUT
  echo "" >> $OUT
done

echo "## 功耗估算（能量模型）" >> $OUT
python scripts/power_model.py >> $OUT 2>/dev/null || echo "（power_model.py 未运行）" >> $OUT

echo "结果已写入 $OUT"
