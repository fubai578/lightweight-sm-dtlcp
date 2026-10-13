#!/bin/bash
# ct_cost.sh — 常量时间（SMC_SM2_CT）的性能代价：交错测量 CT=0 与 CT=1 两个构建
#
# 为什么必须交错：CT 与非 CT 的差值在"基点路径"上只有约 5%（<10%），
# 顺序运行会被主机频率漂移掩盖（本仓库已记录过 fe_mul 顺序快照出现离群值的教训）。
# 本脚本交替运行两个二进制、各取多轮最小值，报告比值。
#
# 用法：bash scripts/ct_cost.sh [轮数，默认 5]
# 产物：bin/ct_cost.md
set -e
cd "$(dirname "$0")/.."
ROUNDS=${1:-5}
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac

CT0=bin/bench_sct_opt$EXE
CT1=bin/bench_sct_ct$EXE
if [ ! -f "$CT0" ] || [ ! -f "$CT1" ]; then
  echo "缺少 $CT0 / $CT1，请先运行 bash scripts/build.sh"
  exit 1
fi

python scripts/ct_cost.py "$CT0" "$CT1" "$ROUNDS" > bin/ct_cost.md
cat bin/ct_cost.md
echo ""
echo "结果已写入 bin/ct_cost.md"
