#!/bin/bash
# build.sh — 一键构建（Windows Git Bash / Linux）
# 产物：bin/test_{ref,opt,tiny}  bin/bench_{ref,opt}  bin/dtlcp_{sensor,gateway}  bin/test_dtlcp
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)

CC=${CC:-gcc}
CFLAGS="-std=c99 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1 -Icrypto/include -Icrypto/src -Idtlcp/src -Idtlcp/include"
LDFLAGS=""
OUT=bin
OBJ=obj
mkdir -p $OUT $OBJ

CRYPTO_SRC="crypto/src/sm3.c crypto/src/sm4.c crypto/src/sm2_arith.c crypto/src/sm2.c crypto/src/hmac_sm3.c crypto/src/smc_rng.c"
DTLCP_SRC="dtlcp/src/dtlcp_crypto.c dtlcp/src/dtlcp_record.c dtlcp/src/dtlcp_hs.c dtlcp/src/dtlcp_cert.c"

build_crypto_objs() { # $1=后缀 $2..=内核宏
  local suf=$1; shift
  local src
  for src in $CRYPTO_SRC; do
    local name=$(basename $src .c)
    $CC $CFLAGS "$@" -c $src -o $OBJ/${name}_${suf}.o
  done
}

link_test() { # $1=后缀 $2..=宏
  local suf=$1; shift
  $CC $CFLAGS "$@" tests/test_all.c $OBJ/*_${suf}.o -o $OUT/test_${suf}$EXE $LDFLAGS
}

echo "[1/4] 构算算法库三档内核 + 测试 ..."
build_crypto_objs ref  -DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1
build_crypto_objs opt  -DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2
build_crypto_objs tiny -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2
build_crypto_objs ct   -DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2 -DSMC_SM2_CT=1
build_crypto_objs lite -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0
link_test ref  -DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1
link_test opt  -DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2
link_test tiny -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2
link_test ct   -DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2 -DSMC_SM2_CT=1
link_test lite -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0
echo "      -> $OUT/test_{ref,opt,tiny,ct,lite}"

if [ -f tests/test_dtlcp.c ]; then
  echo "[2/4] 构建 DTLCP 协议测试 ..."
  $CC $CFLAGS -DSMC_SM2_ENC=0 tests/test_dtlcp.c $DTLCP_SRC $CRYPTO_SRC -o $OUT/test_dtlcp$EXE $LDFLAGS -lpthread
else
  echo "[2/4] DTLCP 测试未就绪，跳过"
fi

if [ -f apps/bench/bench_crypto.c ]; then
  echo "[3/4] 构建基准（ref/opt 双内核）..."
  $CC $CFLAGS apps/bench/bench_crypto.c $OBJ/*_ref.o  -o $OUT/bench_ref$EXE $LDFLAGS -lpthread
  $CC $CFLAGS apps/bench/bench_crypto.c $OBJ/*_opt.o  -o $OUT/bench_opt$EXE $LDFLAGS -lpthread
  $CC $CFLAGS apps/bench/bench_crypto.c $OBJ/*_tiny.o -o $OUT/bench_tiny$EXE $LDFLAGS -lpthread
else
  echo "[3/4] 基准未就绪，跳过"
fi

if [ -f apps/demo/dtlcp_sensor.c ]; then
  echo "[4/4] 构建 demo（sensor/gateway）..."
  SOCKLIB=""
  case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SOCKLIB="-lws2_32";; esac
  $CC $CFLAGS -DSMC_SM2_ENC=0 apps/demo/dtlcp_sensor.c apps/demo/udp_io.c $DTLCP_SRC $OBJ/*_opt.o -o $OUT/dtlcp_sensor$EXE $LDFLAGS $SOCKLIB
  $CC $CFLAGS -DSMC_SM2_ENC=0 apps/demo/dtlcp_gateway.c apps/demo/udp_io.c $DTLCP_SRC $OBJ/*_opt.o -o $OUT/dtlcp_gateway$EXE $LDFLAGS $SOCKLIB
else
  echo "[4/4] demo 未就绪，跳过"
fi

echo "构建完成。"
