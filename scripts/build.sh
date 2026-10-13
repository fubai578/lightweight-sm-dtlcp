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

# 各变体的编译宏：**唯一来源**。对象、测试与基准都必须用同一组宏编译——
# 否则 .c 里的 #if 分支（如 SMC_SM2_PEER_TABLE）会与所链接的对象不一致，
# 轻则打印的内核名不符，重则链接期缺符号（曾因此构建失败）。
M_REF="-DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1"
M_OPT="-DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2"
M_TINY="-DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2"
M_CT="$M_OPT -DSMC_SM2_CT=1"
M_LITE="$M_TINY -DSMC_SM2_ENC=0"
M_OPC="$M_OPT -DSMC_SM2_OPCOUNT=1"
M_CTC="$M_CT -DSMC_SM2_OPCOUNT=1"
M_NOTAB="$M_OPT -DSMC_SM2_FIXED_TABLE=0"

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
build_crypto_objs ref  $M_REF
build_crypto_objs opt  $M_OPT
build_crypto_objs tiny $M_TINY
build_crypto_objs ct   $M_CT
build_crypto_objs lite $M_LITE
build_crypto_objs opc  $M_OPC
build_crypto_objs ctc  $M_CTC
# notab：关闭基点 comb 预计算表——用于「comb 收益 / 3KB Flash 代价」的同轮 A/B 对照，
#        同时把这一裁剪开关也纳入差分指纹验证
build_crypto_objs notab $M_NOTAB
link_test ref   $M_REF
link_test opt   $M_OPT
link_test tiny  $M_TINY
link_test ct    $M_CT
link_test lite  $M_LITE
link_test notab $M_NOTAB
echo "      -> $OUT/test_{ref,opt,tiny,ct,lite,notab}"

if [ -f tests/test_dtlcp.c ]; then
  echo "[2/4] 构建 DTLCP 协议测试 ..."
  $CC $CFLAGS -DSMC_SM2_ENC=0 tests/test_dtlcp.c $DTLCP_SRC $CRYPTO_SRC -o $OUT/test_dtlcp$EXE $LDFLAGS -lpthread
else
  echo "[2/4] DTLCP 测试未就绪，跳过"
fi

if [ -f apps/bench/bench_crypto.c ]; then
  echo "[3/4] 构建基准（ref/opt 双内核）..."
  $CC $CFLAGS $M_REF   apps/bench/bench_crypto.c $OBJ/*_ref.o   -o $OUT/bench_ref$EXE $LDFLAGS -lpthread
  $CC $CFLAGS $M_OPT   apps/bench/bench_crypto.c $OBJ/*_opt.o   -o $OUT/bench_opt$EXE $LDFLAGS -lpthread
  $CC $CFLAGS $M_TINY  apps/bench/bench_crypto.c $OBJ/*_tiny.o  -o $OUT/bench_tiny$EXE $LDFLAGS -lpthread
  # notab：与 opt 同内核、仅关闭基点 comb 表 → comb 净收益的同轮 A/B
  $CC $CFLAGS $M_NOTAB apps/bench/bench_crypto.c $OBJ/*_notab.o -o $OUT/bench_notab$EXE $LDFLAGS -lpthread
  if [ -f apps/bench/bench_sct.c ]; then
    $CC $CFLAGS $M_OPC apps/bench/bench_sct.c $OBJ/*_opc.o -o $OUT/bench_sct_opt$EXE $LDFLAGS -lpthread
    $CC $CFLAGS $M_CTC apps/bench/bench_sct.c $OBJ/*_ctc.o -o $OUT/bench_sct_ct$EXE $LDFLAGS -lpthread
  fi
else
  echo "[3/4] 基准未就绪，跳过"
fi

if [ -f apps/demo/dtlcp_sensor.c ]; then
  echo "[4/4] 构建 demo（sensor/gateway）..."
  SOCKLIB=""
  case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) SOCKLIB="-lws2_32";; esac
  $CC $CFLAGS $M_OPT apps/demo/dtlcp_sensor.c apps/demo/udp_io.c $DTLCP_SRC $OBJ/*_opt.o -o $OUT/dtlcp_sensor$EXE $LDFLAGS $SOCKLIB
  $CC $CFLAGS $M_OPT apps/demo/dtlcp_gateway.c apps/demo/udp_io.c $DTLCP_SRC $OBJ/*_opt.o -o $OUT/dtlcp_gateway$EXE $LDFLAGS $SOCKLIB
else
  echo "[4/4] demo 未就绪，跳过"
fi

echo "构建完成。"
