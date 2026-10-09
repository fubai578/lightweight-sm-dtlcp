#!/bin/bash
# thirdparty_bench.sh — 第三方基线对照（GmSSL 3.1.1，纯 C 国密参考实现）
#
# 为什么需要：本仓库原有的加速比全部是"本库 REF vs 本库 OPT"（自己比自己），
# 对外部无可比性。GmSSL 是国内事实上的国密参考实现，且同为可移植 C（非 OpenSSL 那种手写汇编），
# 是更公平的锚点。
#
# 用法：
#   bash scripts/thirdparty_bench.sh              # 下载(缓存) + 编译 + 交错测量 + 出报告
#   GMSSL_DIR=/path/to/GmSSL-3.1.1 bash scripts/thirdparty_bench.sh   # 用本地已解压源码
#
# 产物：bin/thirdparty_compare.md
# 依赖：curl（下载）、gcc；无 cmake/make 也可（只手工编译所需子集）。
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
CC=${CC:-gcc}
GMSSL_VER=3.1.1
CACHE=${GMSSL_CACHE:-obj/thirdparty}
SRC=${GMSSL_DIR:-$CACHE/GmSSL-$GMSSL_VER}
OUT=bin/thirdparty_compare.md

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) EXE=".exe"; SOCKLIB="-lws2_32" ;;
  *)                    EXE="";     SOCKLIB="" ;;
esac

mkdir -p $CACHE bin obj/thirdparty/gmssl obj/thirdparty/ours

# ---------- 1. 获取源码 ----------
if [ ! -d "$SRC" ]; then
  echo "[1/4] 下载 GmSSL $GMSSL_VER ..."
  TARBALL=$CACHE/gmssl-$GMSSL_VER.tar.gz
  if [ ! -f "$TARBALL" ]; then
    curl -sL --retry 3 --retry-delay 2 -o "$TARBALL" \
      "https://codeload.github.com/guanzhi/GmSSL/tar.gz/refs/tags/v$GMSSL_VER"
  fi
  tar xzf "$TARBALL" -C $CACHE
else
  echo "[1/4] 使用已有源码：$SRC"
fi
if [ ! -d "$SRC/src" ]; then
  echo "错误：$SRC 下找不到 GmSSL 源码（可用 GMSSL_DIR=... 指定解压目录）"
  exit 1
fi

# ---------- 2. 编译 GmSSL 所需子集 ----------
# 只编译基准用到的原语，避开 cmake 与 GCM/AES/PEM 等无关依赖：
#   sm3(+hmac) / sm4(enc,setkey,common,modes) / sm2(alg,lib) / asn1,debug,hex(错误与格式化) / rand_win
# 注：rand.c 读 /dev/urandom，Windows 上必须改用 rand_win.c（否则 do_sign 取随机数失败）
echo "[2/4] 编译 GmSSL 子集 ..."
GINC="-I$SRC/include -Isrc"
GCF="-std=c99 -O2 -w -D__USE_MINGW_ANSI_STDIO=1 $GINC"
GSRC="sm3 sm3_hmac sm4_enc sm4_setkey sm4_common sm4_modes sm2_alg sm2_lib asn1 debug hex"
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) GSRC="$GSRC rand_win" ;;
  *)                    GSRC="$GSRC rand" ;;
esac
for f in $GSRC; do
  $CC $GCF -c "$SRC/src/$f.c" -o obj/thirdparty/gmssl/$f.o
done
# ghash 桩：sm4_modes.c 引用它（GCM 路径），基准不调用 GCM
cat > obj/thirdparty/gmssl/stub.c <<'EOF'
#include <stddef.h>
#include <stdint.h>
void ghash(const uint8_t h[16], const uint8_t *aad, size_t aadlen,
           const uint8_t *c, size_t clen, uint8_t out[16])
{
    (void)h; (void)aad; (void)aadlen; (void)c; (void)clen;
    if (out) { int i; for (i = 0; i < 16; i++) out[i] = 0; }
}
EOF
$CC $GCF -c obj/thirdparty/gmssl/stub.c -o obj/thirdparty/gmssl/stub.o

# ---------- 3. 构建两侧基准 ----------
echo "[3/4] 构建基准（本库 opt/ref + GmSSL）..."
CFLAGS="-std=c99 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1 -Icrypto/include -Icrypto/src -Idtlcp/src -Idtlcp/include"
CRYPTO_SRC="crypto/src/sm3.c crypto/src/sm4.c crypto/src/sm2_arith.c crypto/src/sm2.c crypto/src/hmac_sm3.c crypto/src/smc_rng.c"
for v in ref opt; do
  case $v in
    ref) MACROS="-DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1" ;;
    opt) MACROS="-DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2" ;;
  esac
  for s in $CRYPTO_SRC; do
    n=$(basename $s .c)
    $CC $CFLAGS $MACROS -c $s -o obj/thirdparty/ours/${n}_$v.o
  done
  $CC $CFLAGS $MACROS apps/bench/bench_crypto.c obj/thirdparty/ours/*_$v.o \
      -o bin/bench_${v}_tp$EXE -lpthread
done
$CC $GCF apps/bench/bench_gmssl.c obj/thirdparty/gmssl/*.o -o bin/bench_gmssl$EXE -lpthread $SOCKLIB

# ---------- 4. 交错测量 + 出报告 ----------
echo "[4/4] 交错测量并生成报告 ..."
python scripts/compare_bench.py --out "$OUT" \
  --ours "opt:bin/bench_opt_tp$EXE" --ours "ref:bin/bench_ref_tp$EXE" \
  --gmssl "bin/bench_gmssl$EXE" --passes 3

echo "结果已写入 $OUT"
