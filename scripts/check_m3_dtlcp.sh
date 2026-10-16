#!/bin/bash
# F103 DTLCP RAM gate: real struct size + linked static RAM + longest stack path.
set -eo pipefail
cd "$(dirname "$0")/.."

CROSS=${CROSS:-arm-none-eabi-}
CC=${CROSS}gcc
NM=${CROSS}nm
SIZE=${CROSS}size
OBJDUMP=${CROSS}objdump
OUT=obj/m3_dtlcp_budget

command -v "$CC" >/dev/null 2>&1 || { echo "找不到 $CC"; exit 2; }
mkdir -p "$OUT"

FLAGS="-mcpu=cortex-m3 -mthumb -O2 -std=c99 -Wall -Wextra \
 -ffunction-sections -fdata-sections -ffreestanding -fstack-usage \
 -DSTM32F103xB -DDTLCP_RECBUF=1024 \
 -DSMC_SM3_KERNEL=4 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0 \
 -Icrypto/include -Icrypto/src -Idtlcp/include -Idtlcp/src -Iembedded/cortex-m3"

SRCS="crypto/src/sm3.c crypto/src/sm4.c crypto/src/sm2_arith.c crypto/src/sm2.c \
 crypto/src/hmac_sm3.c crypto/src/smc_rng.c \
 dtlcp/src/dtlcp_crypto.c dtlcp/src/dtlcp_record.c dtlcp/src/dtlcp_cert.c dtlcp/src/dtlcp_hs.c \
 embedded/cortex-m3/w5500_udp.c"
OBJS=""
for src in $SRCS; do
  obj="$OUT/$(echo "$src" | sed 's|/|_|g; s|\.c$|.o|')"
  "$CC" $FLAGS -c "$src" -o "$obj"
  OBJS="$OBJS $obj"
done
"$CC" $FLAGS -Os -c apps/bench/embedded_size_probe.c -o "$OUT/size_probe.o"
"$CC" -mcpu=cortex-m3 -mthumb -nostdlib -Wl,-r $OBJS -o "$OUT/combined.o"

ctx_hex=$($NM -S "$OUT/size_probe.o" | awk '$4=="size_probe_dtlcp_ctx" {print $2}')
ctx=$((16#$ctx_hex))
static_ram=$($SIZE $OBJS | awk 'NR>2 && $6!="(TOTALS)" {d+=$2; b+=$3} END {print d+b}')
stack_line=$(python scripts/stack_depth.py "$OUT/*.su" --objs "$OUT/combined.o" \
  --objdump "$OBJDUMP" --tsv)
stack=$(printf '%s\n' "$stack_line" | awk -F'\t' '{print $3}')
path=$(printf '%s\n' "$stack_line" | awk -F'\t' '{print $4}')
total=$((ctx + static_ram + stack))
headroom=$((20480 - total))

echo "DTLCP_RECBUF=1024 | ctx=${ctx} B | static=${static_ram} B | stack=${stack} B"
echo "RAM lower bound=${total} B / 20480 B | headroom=${headroom} B"
echo "longest path: $path"

if [ "$headroom" -lt 0 ]; then
  echo "失败：F103 20 KB RAM 预算溢出"
  exit 1
fi
