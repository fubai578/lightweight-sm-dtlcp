#!/bin/bash
# embedded/cortex-m4/build.sh — Cortex-M4 构建（**不依赖 make**，与本仓库主工程一致）
#
# 用法：
#   bash build.sh              # 默认：STM32F407 全量 Flash（1MB）构建
#   bash build.sh flash ref      # 用 REF（教科书基线）内核构建 —— 板上做内核 A/B 对照
#   bash build.sh flash opt ct   # OPT 内核 + 常量时间（SMC_SM2_CT=1）
#   bash build.sh flash opt loop        # 连续基准循环：功耗测量用（占空比≈100%）
#                                 #   可选 loop_sm4 / loop_verify / loop_ecdh
#   bash build.sh 32k          # 用 FLASH LENGTH=32K 的链接脚本构建 —— **证明"适配 <32KB Flash"**
#   bash build.sh sim          # 宿主仿真构建（用本机 gcc 在 PC 上跑板级代码，无需 ARM 工具链）
#   bash build.sh clean
#
# 需要：arm-none-eabi-gcc（sim 除外）。安装见 docs/08 §1。
# 产物：smc_m4.elf / smc_m4.bin（32k 模式为 smc_m4_32k.*）+ size 表 + .su 栈深文件
set -e
cd "$(dirname "$0")"
ROOT=../..
MODE=${1:-flash}
KERNEL=${2:-opt}
EXTRA=${3:-}

# 内核选择：让"优化在 MCU 上到底有没有收益"能在同一块板子上做 A/B 对照
case "$KERNEL" in
  ref)  KDEF="-DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1" ;;
  opt)  KDEF="-DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2" ;;
  tiny) KDEF="-DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2" ;;
  *) echo "第二参数（内核）只支持 ref|opt|tiny"; exit 2 ;;
esac

case "$EXTRA" in
  "")          XDEF="" ;;
  ct)          XDEF="-DSMC_SM2_CT=1" ;;
  loop)        XDEF="-DSMC_BENCH_LOOP=1" ;;
  loop_sm4)    XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_SM4=1" ;;
  loop_verify) XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_VERIFY=1" ;;
  loop_ecdh)   XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_ECDH=1" ;;
  *) echo "第三参数只支持 ct|loop|loop_sm4|loop_verify|loop_ecdh"; exit 2 ;;
esac

CRYPTO="$ROOT/crypto/src/sm3.c $ROOT/crypto/src/sm4.c $ROOT/crypto/src/sm2_arith.c \
        $ROOT/crypto/src/sm2.c $ROOT/crypto/src/hmac_sm3.c $ROOT/crypto/src/smc_rng.c"
PORT=../port/smc_port_stm32.c
SELF=board_selftest.c
INC="-I. -I$ROOT/crypto/include -I$ROOT/crypto/src -I../port"

clean() { rm -f *.o *.su *.elf *.bin *.map smc_sim 2>/dev/null || true; }

if [ "$MODE" = "clean" ]; then clean; echo "已清理"; exit 0; fi

# ---------------------------------------------------------------- 宿主仿真
if [ "$MODE" = "sim" ]; then
  CC=${CC:-gcc}
  CFLAGS="-std=c99 -O2 -Wall -Wextra -Werror -D__USE_MINGW_ANSI_STDIO=1 -DSMC_PORT_HOST_SIM \
          -DSMC_CPU_HZ=16000000u $INC"
  echo "[sim] 用 $CC 在宿主上编译板级代码（验证编译/KAT/流程/打印）"
  $CC $CFLAGS host_sim_main.c $SELF $PORT $CRYPTO -o smc_sim -lpthread
  echo "[sim] 运行："
  ./smc_sim
  echo "[sim] 退出码 $?（0 = 全部自检通过且无失败项）"
  exit 0
fi

# ---------------------------------------------------------------- 交叉编译
CROSS=${CROSS:-arm-none-eabi-}
CC=${CROSS}gcc
OBJCOPY=${CROSS}objcopy
SIZE=${CROSS}size
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "错误：找不到 $CC。"
  echo "  Windows: winget install --id Arm.GnuArmEmbeddedToolchain -e   （或从 ARM 官网下载后加入 PATH）"
  echo "  Debian/Ubuntu: sudo apt install gcc-arm-none-eabi"
  echo "  或者先跑宿主仿真：bash embedded/cortex-m4/build.sh sim"
  exit 1
fi


MCU="-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard"
# 注：只打印整数 → 不需要浮点 printf；-ffreestanding 保证不误用宿主头
CFLAGS="$MCU -O2 -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections \
        -ffreestanding -fstack-usage -DSTM32F407xx $KDEF $XDEF $INC"

case "$MODE" in
  32k)  LD=stm32f407_32k.ld; OUT=smc_m4_32k_${KERNEL}${EXTRA:+_$EXTRA} ;;
  flash) LD=stm32f407.ld;    OUT=smc_m4_${KERNEL}${EXTRA:+_$EXTRA} ;;
  *) echo "用法：bash build.sh [flash|32k|sim|clean]"; exit 2 ;;
esac

echo "[1/4] 编译（$MODE，内核=$KERNEL${EXTRA:+，扩展=$EXTRA}，链接脚本 $LD）"
SRCS="main.c startup_stm32f407.c system_stm32f4xx_stub.c $SELF $PORT $CRYPTO"
for f in $SRCS; do
  o=$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')
  $CC $CFLAGS -c "$f" -o "$o"
done

echo "[2/4] 链接"
$CC *.o $MCU -T$LD -nostartfiles -Wl,--gc-sections -Wl,-Map=$OUT.map -o $OUT.elf

echo "[3/4] 尺寸（对照 docs/04 的 Flash 预算）"
$SIZE $OUT.elf
if [ "$MODE" = "32k" ]; then
  echo "  ↑ 若此处链接成功，即证明 **代码可装进 32KB Flash**（<32KB 目标的证据）"
fi

echo "[4/4] 生成 .bin（可用 ST-LINK/CubeProgrammer 或 OpenOCD 烧录）"
$OBJCOPY -O binary $OUT.elf $OUT.bin
ls -l $OUT.bin

echo
echo "栈深热点（单函数自身帧，top5）——完整调用路径请用："
echo "  python ../../scripts/stack_depth.py \"*.su\" --objs \"*.o\""
cat *.su 2>/dev/null | sort -t$'\t' -k2 -rn | head -5 | \
  awk -F'\t' '{ split($1,a,":"); printf "  %-28s %s B\n", a[length(a)], $2 }'
echo
echo "完成：$OUT.elf / $OUT.bin"
