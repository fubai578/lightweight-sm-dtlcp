#!/bin/bash
# run_tests.sh — 全量正确性测试（KAT + 差分 + 协议 + 负向）
set -e
cd "$(dirname "$0")/.."
# 平台产物后缀（MinGW 需 .exe，Linux/macOS 无后缀）
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac

bash scripts/build.sh >/dev/null

echo "==== 算法库测试（5 内核变体）===="
for v in ref opt tiny ct lite; do
  printf "%-6s: " $v
  ./bin/test_$v$EXE | tail -1
done

echo ""
echo "==== 差分指纹（5 变体必须逐位一致）===="
for v in ref opt tiny ct lite; do
  printf "%-6s: " $v
  ./bin/test_$v$EXE --fp
done

echo ""
echo "==== DTLCP 协议测试 ===="
./bin/test_dtlcp$EXE | tail -1

echo ""
echo "全部测试执行完毕。"
