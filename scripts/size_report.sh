#!/bin/bash
# size_report.sh — Flash/RAM/栈占用与裁剪尺寸对比
#  -fstack-usage 输出逐函数最大栈深；size 输出 text/data/bss
set -e
cd "$(dirname "$0")/.."
CC=${CC:-gcc}
CFLAGS="-std=c99 -O2 -D__USE_MINGW_ANSI_STDIO=1 -Icrypto/include -Icrypto/src"
mkdir -p obj/size bin

OUT=bin/size_report.md
{
echo "# 内存占用报告（gcc -O2, x86-64 host 对象文件）"
echo ""
echo "> text≈Flash 占用，data+bss≈RAM 静态占用；栈深由 -fstack-usage 给出（最大调用路径上界参考）"
} > $OUT

build_one() { # $1=标签 $2..=宏
  local tag=$1; shift
  local total_text=0 total_data=0 total_bss=0
  echo "" >> $OUT
  echo "## 配置：$tag" >> $OUT
  echo "| 模块 | text(B) | data(B) | bss(B) |" >> $OUT
  echo "|---|---|---|---|" >> $OUT
  for src in crypto/src/sm3.c crypto/src/sm4.c crypto/src/sm2_arith.c crypto/src/sm2.c \
             crypto/src/hmac_sm3.c crypto/src/smc_rng.c; do
    local name=$(basename $src .c)
    local o=obj/size/${name}_${tag}.o
    $CC $CFLAGS "$@" -fstack-usage -c $src -o $o 2>/dev/null
    local sz=$(size $o | tail -1)
    local t=$(echo $sz | awk '{print $1}')
    local d=$(echo $sz | awk '{print $2}')
    local b=$(echo $sz | awk '{print $3}')
    echo "| $name | $t | $d | $b |" >> $OUT
    total_text=$((total_text + t)); total_data=$((total_data + d)); total_bss=$((total_bss + b))
  done
  echo "| **合计** | **$total_text** | **$total_data** | **$total_bss** |" >> $OUT
  echo "" >> $OUT
  echo "栈深热点（.su, top5）：" >> $OUT
  cat obj/size/*_${tag}.su 2>/dev/null | sort -t: -k3 -rn | head -5 | \
    sed 's/^/    /' >> $OUT || true
}

build_one full
build_one trim-noenc  -DSMC_SM2_ENC=0
build_one trim-min    -DSMC_SM2_ENC=0 -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM4_MODE_CBC=0
build_one tiny-stack  -DSMC_SM3_KERNEL=3

echo "结果已写入 $OUT"
