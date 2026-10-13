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
echo "> text≈Flash 占用，data+bss≈RAM 静态占用。"
echo ""
echo "> **栈深口径**：先用 -fstack-usage 取逐函数**自身**静态帧，再用 objdump 提取调用图求"
echo "> **最长加权调用路径**（scripts/stack_depth.py）。权重不含返回地址/寄存器保存等 ABI 开销，"
echo "> 函数指针间接调用不在图中，故为**下界**——任务栈须在此之上留安全余量。"
} > $OUT

# 打印单函数最大帧 top5（按 .su 第 2 列字节数排序；此前误用 -k3 排非数字列，已修正）
stack_top5() { # $1=标签
  cat obj/size/*_$1.su 2>/dev/null | sort -t$'\t' -k2 -rn | head -5 | \
    awk -F'\t' '{ split($1,a,":"); printf "    %s\t%s B\n", a[length(a)], $2 }'
}

build_one() { # $1=标签 $2=说明 $3..=宏
  local tag=$1; shift
  local desc=$1; shift
  local total_text=0 total_data=0 total_bss=0
  echo "" >> $OUT
  echo "## 配置：$tag" >> $OUT
  echo "" >> $OUT
  echo "> $desc" >> $OUT
  echo "" >> $OUT
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
  echo "单函数最大帧 top5（自身帧）：" >> $OUT
  echo "" >> $OUT
  echo '```' >> $OUT
  stack_top5 $tag >> $OUT
  echo '```' >> $OUT
  echo "" >> $OUT
  local tsv
  tsv=$(python "$(dirname "$0")/stack_depth.py" "obj/size/*_${tag}.su" --tsv 2>/dev/null)
  if [ -n "$tsv" ]; then
    local fname fbytes depth path
    fname=$(echo "$tsv" | awk -F'\t' '{print $1}')
    fbytes=$(echo "$tsv" | awk -F'\t' '{print $2}')
    depth=$(echo "$tsv" | awk -F'\t' '{print $3}')
    path=$(echo "$tsv" | awk -F'\t' '{print $4}')
    echo "**最大调用路径栈深（下界）：${depth} B**" >> $OUT
    echo "" >> $OUT
    echo '```' >> $OUT
    echo "$path" | sed 's/ > /\n  → /g' | sed 's/^/    /' >> $OUT
    echo '```' >> $OUT
    echo "" >> $OUT
    echo "栈预算建议：按 1.5× 余量向上取整 ≥ **$(( (depth * 3 / 2 + 1023) / 1024 )) KB**（下界 ${depth} B；余量覆盖 ABI 开销、中断嵌套与编译器差异）" >> $OUT
  fi
}

# 注意：sm2_base_table.h 为 static const 只读常量（在 sm2_arith.o 的 .rdata 中，
# 计入 text 列），RAM 增量 0——这是选择"Flash 换速度"而非"RAM 换速度"的原因。
build_one full        "默认全功能（SM3/SM4=FAST，SM2=OPT 含基点 comb 表）"
build_one no-comb     "同 full，关闭基点 comb 表（-DSMC_SM2_FIXED_TABLE=0）：量化 comb 的 Flash 代价" \
                      -DSMC_SM2_FIXED_TABLE=0
build_one trim-noenc  "裁 SM2 公钥加密/解密（DTLCP 最小配置）" -DSMC_SM2_ENC=0
build_one trim-min    "最小 RAM/Flash：SMALL+TINY 内核 + 裁 ENC/CBC + 关 comb 表" \
                      -DSMC_SM2_ENC=0 -DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM4_MODE_CBC=0 \
                      -DSMC_SM2_FIXED_TABLE=0 -DSMC_SM2_PEER_TABLE=0
build_one ct-nocomb   "常量时间 + **关** comb 表（改造前的 CT 形态）：量化\"用表与 CT 结合\"省下的代价"                       -DSMC_SM2_CT=1 -DSMC_SM2_FIXED_TABLE=0
build_one peer-off    "关对端公钥预计算表（SMC_SM2_PEER_TABLE=0）：量化该特性的 Flash 代价"                       -DSMC_SM2_PEER_TABLE=0
build_one tiny-stack  "全功能 + SM3 TINY 滚动调度核（最小栈）" -DSMC_SM3_KERNEL=3
build_one ct-const    "常量时间标量乘（SMC_SM2_CT=1；comb 表按设计自动禁用）" -DSMC_SM2_CT=1

echo "结果已写入 $OUT"
