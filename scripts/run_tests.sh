#!/bin/bash
# run_tests.sh — 全量正确性测试（KAT + 差分 + 协议 + 负向）
set -eo pipefail   # 管道中的失败必须让脚本失败（否则 test | tail -1 会吞掉退出码）
cd "$(dirname "$0")/.."
# 平台产物后缀（MinGW 需 .exe，Linux/macOS 无后缀）
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac

bash scripts/build.sh >/dev/null

echo "==== 算法库测试（7 内核变体）===="
for v in ref opt mcu tiny ct lite notab; do
  printf "%-6s: " $v
  ./bin/test_$v$EXE | tail -1
done

echo ""
echo "==== 差分指纹（7 变体必须逐位一致）===="
for v in ref opt mcu tiny ct lite notab; do
  printf "%-6s: " $v
  ./bin/test_$v$EXE --fp
done

echo ""
echo "==== DTLCP 协议测试 ===="
./bin/test_dtlcp$EXE | tail -1
printf "MCU RECBUF=1024: "
./bin/test_dtlcp_mcu$EXE | tail -1

echo ""
echo "全部测试执行完毕。"
