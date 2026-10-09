#!/bin/bash
# gen_fixed_base.sh — 生成/校验 SM2 基点 comb 预计算表（crypto/src/sm2_base_table.h）
#
# 表与生成器必须同步：本脚本把生成结果与仓库内已提交的表比对，
#   bash scripts/gen_fixed_base.sh            重新生成（覆盖）
#   bash scripts/gen_fixed_base.sh --check    仅校验（不一致则退出码 1，CI 用）
set -e
cd "$(dirname "$0")/.."
CC=${CC:-gcc}
CFLAGS="-std=c99 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1 -Icrypto/include -Icrypto/src"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe" ;;
  *)                    EXE="" ;;
esac

CHECK=0
[ "$1" = "--check" ] && CHECK=1

mkdir -p bin obj/gen
# 用 REF 内核（二进制倍点-点加）作为独立参照路径，避免生成器与 OPT 标量乘自证
# 注意：产物放入 obj/gen/ 子目录——build.sh 以 obj/*_<suf>.o 通配链接各内核对象，
#       同目录会与之冲突（multiple definition）。
$CC $CFLAGS -DSMC_SM2_KERNEL=1 -c crypto/src/sm2_arith.c -o obj/gen/sm2_arith_ref.o
$CC $CFLAGS apps/gen/gen_fixed_base.c obj/gen/sm2_arith_ref.o -o bin/gen_fixed_base$EXE

TARGET=crypto/src/sm2_base_table.h
TMP=$TARGET.tmp
./bin/gen_fixed_base$EXE > $TMP

if [ "$CHECK" = "1" ]; then
  if [ ! -f $TARGET ]; then
    echo "缺少 $TARGET（请运行 bash scripts/gen_fixed_base.sh 生成）"; rm -f $TMP; exit 1
  fi
  if diff -q $TARGET $TMP >/dev/null; then
    echo "基点预计算表与生成器一致（$TARGET）"
    rm -f $TMP
  else
    echo "基点预计算表与生成器不一致——请重新运行 bash scripts/gen_fixed_base.sh"
    diff $TARGET $TMP | head -20
    rm -f $TMP
    exit 1
  fi
else
  mv $TMP $TARGET
  echo "已写入 $TARGET"
fi
