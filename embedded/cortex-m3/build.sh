#!/bin/bash
# embedded/cortex-m3/build.sh — STM32F103C8T6 (Cortex-M3) 构建（**不依赖 make**，与本仓库主工程一致）
#
# 用法：
#   bash build.sh                # 默认：STM32F103C8T6 全量 Flash (64K) 构建
#   bash build.sh flash ref      # REF（教科书基线）内核 —— 板上做内核 A/B 对照
#   bash build.sh flash opt      # OPT（优化）内核 —— 默认
#   bash build.sh flash mcu      # F103 专用：SM3 MCU + SM4 FAST + SM2 OPT
#   bash build.sh flash opt ct   # OPT + 常量时间（SMC_SM2_CT=1）
#   bash build.sh flash opt loop # 连续基准循环：功耗测量用（占空比≈100%）
#   bash build.sh 32k opt        # FLASH LENGTH=32K 约束链接 —— **证明"适配 <32KB Flash"**
#   bash build.sh sim            # 宿主仿真（用本机 gcc 在 PC 上跑板级代码，无需 ARM 工具链）
#   bash build.sh clean
#
# 需要：arm-none-eabi-gcc（sim 除外）。
# Windows 安装：winget install --id Arm.GnuArmEmbeddedToolchain -e
# 装完后把 "<安装目录>\bin" 加入 PATH（默认 C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin）。
# 产物：smc_m3.elf / smc_m3.bin（32k 模式为 smc_m3_32k_*.+）+ size 表 + .su 栈深文件
set -e
cd "$(dirname "$0")"
ROOT=../..
MODE=${1:-flash}
KERNEL=${2:-opt}
EXTRA=${3:-}

# ================================================================ 诊断模式（先于内核校验）
# uart_probe：串口/固件存活定位固件（不含算法、不含 printf）。
#   烧它可区分「固件没跑」与「纯接线问题」——它同时输出到 PA9 与 PB10，
#   并翻转 PA5（板载 LED）。详见 uart_probe.c 头部注释。
# 用法：bash build.sh uart_probe flash
if [ "$MODE" = "uart_probe" ]; then
    CC=${CC:-arm-none-eabi-gcc}
    OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
    SIZE=${SIZE:-arm-none-eabi-size}
    echo "[诊断] 构建 uart_probe 固件（无算法/无 printf）"
    PROBE_CFLAGS="-mcpu=cortex-m3 -mthumb -O1 -std=c99 -Wall -Wextra \
          -ffunction-sections -fdata-sections -ffreestanding -fstack-usage \
          -specs=nano.specs -u _printf_float -DSTM32F103xB \
          -I. -I../port"
    "$CC" $PROBE_CFLAGS -c uart_probe.c -o uart_probe.o
    "$CC" $PROBE_CFLAGS -c startup_stm32f103c8.c -o startup_stm32f103c8.o
    "$CC" uart_probe.o startup_stm32f103c8.o -mcpu=cortex-m3 -mthumb \
          -specs=nano.specs -u _printf_float \
          -Tstm32f103c8.ld -nostartfiles -Wl,--gc-sections \
          -o smc_m3_uart_probe.elf
    "$SIZE" smc_m3_uart_probe.elf
    "$OBJCOPY" -O binary smc_m3_uart_probe.elf smc_m3_uart_probe.bin
    echo
    echo "诊断固件完成：smc_m3_uart_probe.elf / .bin"
    echo
    echo "烧录：pyocd flash -M under-reset -f 100k -t stm32f103rc -e chip smc_m3_uart_probe.elf"
    echo "判定："
    echo "  ★ 烧录前先确认 BOOT0 跳线拨到 0（若拨到 1 则进 ISP，固件不运行、"
    echo "    烧录却照样成功 —— 这是最容易被误判成「接线错」的原因）"
    echo "  · 板载 LED 闪烁（本固件同时驱动 PC13 与 PA5，两种板子都适用）"
    echo "    → 固件在跑，问题在接线（TX/RX 未交叉）"
    echo "  · LED 不闪 → 固件没跑：查 BOOT0、复位电路、Flash 是否真写入"
    echo
    echo "引脚速查（本工程默认 USART1）："
    echo "  CP2102 TX → PA10，CP2102 RX → PA9，GND 共地（两根线必须交叉）"
    echo "  板上位置：PA9/PA10 在板子左侧排针，B12 往下数第 6、7 个"
    echo
    echo "诊断完成后烧回正式固件："
    echo "  pyocd flash -M under-reset -f 100k -t stm32f103rc -e chip smc_m3_opt.elf"
    exit 0
fi

if [ "$MODE" = "w5500_probe" ]; then
    CC=${CC:-arm-none-eabi-gcc}
    OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
    SIZE=${SIZE:-arm-none-eabi-size}
    PROBE_FLAGS="-mcpu=cortex-m3 -mthumb -O2 -std=c99 -Wall -Wextra \
      -ffunction-sections -fdata-sections -ffreestanding -DSTM32F103xB \
      -I. -I../port"
    "$CC" $PROBE_FLAGS -c w5500_probe.c -o w5500_probe.o
    "$CC" $PROBE_FLAGS -c w5500_udp.c -o w5500_udp.o
    "$CC" $PROBE_FLAGS -c ../port/smc_port_f1.c -o w5500_port.o
    "$CC" $PROBE_FLAGS -c startup_stm32f103c8.c -o w5500_startup.o
    "$CC" w5500_probe.o w5500_udp.o w5500_port.o w5500_startup.o \
      -mcpu=cortex-m3 -mthumb -Tstm32f103c8.ld -nostartfiles \
      -Wl,--gc-sections -o smc_m3_w5500_probe.elf
    "$SIZE" smc_m3_w5500_probe.elf
    "$OBJCOPY" -O binary smc_m3_w5500_probe.elf smc_m3_w5500_probe.bin
    echo "完成：smc_m3_w5500_probe.elf / .bin"
    echo "烧录后串口应依次输出 VERSION_OK、LINK_UP/DOWN、UDP_OPEN_OK"
    exit 0
fi

if [ "$MODE" = "w5500_dtlcp" ] || [ "$MODE" = "w5500_dtlcp_server" ] || \
   [ "$MODE" = "w5500_dtlcp_psk" ] || [ "$MODE" = "w5500_dtlcp_resume" ]; then
    CC=${CC:-arm-none-eabi-gcc}
    OBJCOPY=${OBJCOPY:-arm-none-eabi-objcopy}
    SIZE=${SIZE:-arm-none-eabi-size}
    F="-mcpu=cortex-m3 -mthumb -O2 -std=c99 -Wall -Wextra -ffunction-sections \
      -fdata-sections -ffreestanding -fstack-usage -DSTM32F103xB -DDTLCP_RECBUF=1024 \
      -DSMC_SM3_KERNEL=4 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0 \
      -I. -I../port -I../../crypto/include -I../../crypto/src -I../../dtlcp/include -I../../dtlcp/src"
    ENTRY=w5500_dtlcp_client.c
    OUTNAME=smc_m3_w5500_dtlcp
    if [ "$MODE" = "w5500_dtlcp_server" ]; then
      ENTRY=w5500_dtlcp_server.c
      OUTNAME=smc_m3_w5500_dtlcp_server
    elif [ "$MODE" = "w5500_dtlcp_psk" ]; then
      ENTRY=w5500_dtlcp_server.c
      OUTNAME=smc_m3_w5500_dtlcp_psk
      F="$F -DBOARD_DTLCP_MODE=DTLCP_MODE_PSK"
    elif [ "$MODE" = "w5500_dtlcp_resume" ]; then
      ENTRY=w5500_dtlcp_server.c
      OUTNAME=smc_m3_w5500_dtlcp_resume
      F="$F -DBOARD_DTLCP_RESUME_SEQUENCE=1"
    fi
    SRCS="$ENTRY w5500_udp.c startup_stm32f103c8.c ../port/smc_port_f1.c \
      ../../crypto/src/sm3.c ../../crypto/src/sm4.c ../../crypto/src/sm2_arith.c \
      ../../crypto/src/sm2.c ../../crypto/src/hmac_sm3.c ../../crypto/src/smc_rng.c \
      ../../dtlcp/src/dtlcp_crypto.c ../../dtlcp/src/dtlcp_record.c \
      ../../dtlcp/src/dtlcp_cert.c ../../dtlcp/src/dtlcp_hs.c"
    OBJS=""
    for src in $SRCS; do
      obj="dtlcp_$(echo "$src" | sed 's|\.\./||g; s|/|_|g; s|\.c$|.o|')"
      "$CC" $F -c "$src" -o "$obj"
      OBJS="$OBJS $obj"
    done
    "$CC" $OBJS -mcpu=cortex-m3 -mthumb -specs=nano.specs \
      -Tstm32f103c8.ld -nostartfiles -Wl,--gc-sections -Wl,-Map=$OUTNAME.map \
      -o $OUTNAME.elf
    "$SIZE" $OUTNAME.elf
    "$OBJCOPY" -O binary $OUTNAME.elf $OUTNAME.bin
    echo "完成：$OUTNAME.elf / .bin"
    exit 0
fi

# 内核选择：让"优化在 MCU 上到底有没有收益"能在同一块板子上做 A/B 对照
case "$KERNEL" in
  ref)  KDEF="-DSMC_SM3_KERNEL=1 -DSMC_SM4_KERNEL=1 -DSMC_SM2_KERNEL=1" ;;
  opt)  KDEF="-DSMC_SM3_KERNEL=2 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2" ;;
  mcu)  KDEF="-DSMC_SM3_KERNEL=4 -DSMC_SM4_KERNEL=2 -DSMC_SM2_KERNEL=2" ;;
  tiny) KDEF="-DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2" ;;
  lite) KDEF="-DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0 -DSMC_SM4_MODE_CBC=0" ;;
  min)  KDEF="-DSMC_SM3_KERNEL=3 -DSMC_SM4_KERNEL=3 -DSMC_SM2_KERNEL=2 -DSMC_SM2_ENC=0 -DSMC_SM4_MODE_CBC=0 -DSMC_SM2_FIXED_TABLE=0 -DSMC_SM2_PEER_TABLE=0" ;;
  *) echo "第二参数（内核）只支持 ref|opt|mcu|tiny|lite|min"; exit 2 ;;
esac
# 32K 模式自动开启精简输出（UTF-8 中文 banner 实测约 1.5KB，是能否装进 32K 的关键）
if [ "$MODE" = "32k" ]; then KDEF="$KDEF -DSMC_BOARD_TINY_PRINT=1"; fi

case "$EXTRA" in
  "")          XDEF="" ;;
  ct)          XDEF="-DSMC_SM2_CT=1" ;;
  loop)        XDEF="-DSMC_BENCH_LOOP=1" ;;
  loop_sm4)    XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_SM4=1" ;;
  loop_verify) XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_VERIFY=1" ;;
  loop_ecdh)   XDEF="-DSMC_BENCH_LOOP=1 -DSMC_BENCH_LOOP_ECDH=1" ;;
  uart2)       XDEF="-DSMC_UART_PORT=2" ;;
  uart3)       XDEF="-DSMC_UART_PORT=3" ;;
  *) echo "第三参数只支持 ct|loop|loop_sm4|loop_verify|loop_ecdh|uart2|uart3"; exit 2 ;;
esac

CRYPTO="$ROOT/crypto/src/sm3.c $ROOT/crypto/src/sm4.c $ROOT/crypto/src/sm2_arith.c \
        $ROOT/crypto/src/sm2.c $ROOT/crypto/src/hmac_sm3.c $ROOT/crypto/src/smc_rng.c"
PORT=../port/smc_port_f1.c
SELF=board_selftest.c
INC="-I. -I$ROOT/crypto/include -I$ROOT/crypto/src -I../port"

# ★ F103C8T6 只有 20KB RAM，而算法库静态已占 9576 B。
#   DTLCP 上下文含 pending+txbuf+rxbuf **三份** RECBUF（1600 时=4800B），
#   且握手函数单帧栈约 3.8KB（body[600]+payload[1600]+packet[1600]）——
#   合计约 8.6KB，20KB 装不下。降到 1024 后：RAM 3072B + 单帧栈约 2.6KB，
#   省出约 3 KB。1024 仍能容纳 FULL 握手 body（实测约 600B）与典型遥测。
#   口径与依据见 dtlcp/src/dtlcp_internal.h 的 DTLCP_RECBUF 注释块。
MCU_DEFS="-DSTM32F103xB -DDTLCP_RECBUF=1024"

clean() { rm -f *.o *.su *.elf *.bin *.map smc_sim_f1 .._*.o .._*.su 2>/dev/null || true; }

if [ "$MODE" = "clean" ]; then clean; echo "已清理"; exit 0; fi

# ---------------------------------------------------------------- 宿主仿真
if [ "$MODE" = "sim" ]; then
  CC=${CC:-gcc}
  CFLAGS="-std=c99 -O2 -Wall -Wextra -D__USE_MINGW_ANSI_STDIO=1 -DSMC_PORT_HOST_SIM \
          -DSMC_CPU_HZ=8000000u $INC"
  echo "[sim] 用 $CC 在宿主上编译 F103 板级代码（验证编译/KAT/流程/打印）"
  # 注意：sim 复用 ../cortex-m4/host_sim_main.c（它只依赖 smc_port.h 接口，与芯片无关）
  $CC $CFLAGS ../cortex-m4/host_sim_main.c $SELF $PORT $CRYPTO -o smc_sim_f1
  echo "[sim] 运行："
  ./smc_sim_f1
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
  echo "  Windows: winget install --id Arm.GnuArmEmbeddedToolchain -e"
  echo "            然后把 \"C:\\Program Files (x86)\\Arm GNU Toolchain arm-none-eabi\\<ver>\\bin\" 加入 PATH"
  echo "  Debian/Ubuntu: sudo apt install gcc-arm-none-eabi"
  echo "  或者先跑宿主仿真：bash embedded/cortex-m3/build.sh sim"
  exit 1
fi

# ★ Cortex-M3：无 FPU，故去掉 -mfpu/-mfloat-abi（F4 版有，这里必须不同）
MCU="-mcpu=cortex-m3 -mthumb"
# 注：只打印整数 → 不需要浮点 printf；-ffreestanding 保证不误用宿主头
# -specs=nano.specs：newlib-nano 版 printf，**64KB Flash 器件的必需项**（full 版 printf 单独就要十几 KB）
#
# ★ -u _printf_float 的取舍（实测：不给 32K 链接装不下）：
#   newlib-nano 的 vfprintf **默认就带浮点格式化**，会拉进 __aeabi_dmul/_dmuldf3 等
#   软浮点例程 + dtoa，实测在 M3 上占约 18 KB Flash（链接报 overflowed by 18804 bytes）。
#   本工程的自检**只打印整数**（%d/%u/%llu/%02x），因此链接时用 -u _printf_float
#   把浮点格式化裁掉。代价：板级若真要 printf("%.2f") 会输出乱码——
#   但仓库内没有任何这种用法（见 board_selftest.c 全部 printf 调用）。
#   正确做法是按需开启：确实要打浮点时删掉 -u _printf_float 并接受 +18 KB Flash。
CFLAGS="$MCU -O2 -std=c99 -Wall -Wextra -ffunction-sections -fdata-sections \
        -ffreestanding -fstack-usage -specs=nano.specs -u _printf_float \
        $MCU_DEFS $KDEF $XDEF $INC"

case "$MODE" in
  32k)  LD=stm32f103c8_32k.ld; OUT=smc_m3_32k_${KERNEL}${EXTRA:+_$EXTRA} ;;
  flash) LD=stm32f103c8.ld;    OUT=smc_m3_${KERNEL}${EXTRA:+_$EXTRA} ;;
  *) echo "用法：bash build.sh [flash|32k|sim|clean] [ref|opt|mcu|tiny|lite|min] [ct|loop|loop_verify|loop_ecdh|uart2|uart3]"; exit 2 ;;
esac

echo "[1/4] 编译（$MODE，内核=$KERNEL${EXTRA:+，扩展=$EXTRA}，链接脚本 $LD）"
echo "      MCU: Cortex-M3 (无 FPU) | newlib-nano | SYSCLK 默认 HSI 8MHz"
SRCS="main.c startup_stm32f103c8.c $SELF $PORT $CRYPTO"
OBJS=""
for f in $SRCS; do
  # 目标文件名：路径分隔符与前导 "../" 全部规范化，**保证不以 '.' 或 '_' 开头**
  #（否则 ../../crypto/src/x.c 会生成 ".._.._x.o"，在 Windows/Git Bash 下被当隐藏文件，
  #  `*.o` 通配符匹配不到 → 链接出 0 字节 ELF。这类坑很难查，故显式收集 OBJS 列表。）
  o=$(echo "$f" | sed 's|\.\./||g; s|^\.\./||; s|/|_|g; s|\.c$|.o|')
  case "$o" in
    ._*|_*) echo "内部错误：目标文件名 '$o' 非法"; exit 3 ;;
  esac
  $CC $CFLAGS -c "$f" -o "$o"
  OBJS="$OBJS $o"
done

echo "[2/4] 链接"
# 用显式 OBJS 列表而非 *.o 通配（理由见上方注释）
# ★ -specs=nano.specs **必须出现在链接命令里**（不能只在编译阶段给）：
#   实测踩坑：只在 $CFLAGS 里给 specs 时，链接仍然拉的是完整版 libc.a，
#   产物 50 KB / 32KB 链接直接 overflow。显式补上后降到 22.8 KB —— 差 27 KB。
$CC $OBJS $MCU -specs=nano.specs -u _printf_float -T$LD -nostartfiles \
    -Wl,--gc-sections -Wl,-Map=$OUT.map -o $OUT.elf

echo "[3/4] 尺寸（对照 docs/04 的 Flash 预算）"
$SIZE $OUT.elf
if [ "$MODE" = "32k" ]; then
  echo "  ↑ 若此处链接成功，即证明 **代码可装进 32KB Flash**（<32KB 目标的证据）"
fi

echo "[4/4] 生成 .bin（用 ST-LINK / OpenOCD / pyOCD 烧录）"
$OBJCOPY -O binary $OUT.elf $OUT.bin
ls -l $OUT.bin

echo
echo "栈深热点（单函数自身帧，top5）——完整调用路径请用："
echo "  python ../../scripts/stack_depth.py \"*.su\" --objs \"*.o\" --objdump ${CROSS}objdump"
cat *.su 2>/dev/null | sort -t$'\t' -k2 -rn | head -5 | \
  awk -F'\t' '{ split($1,a,":"); printf "  %-28s %s B\n", a[length(a)], $2 }'
echo
echo "烧录（ST-Link V2，本机 2026-10-14 实测通过的完整参数）："
echo "  pyocd flash -M under-reset -f 100k -t stm32f103rc -e chip $OUT.elf"
echo "  openocd -f interface/stlink.cfg -f target/stf1x.cfg -c \"program $OUT.elf verify reset exit\""
echo
echo "  ★ -e chip 是**必需项**：省略它（默认 -e sector）会报"
echo "    'flash program page failure (address 0x08000000; result code 0x1)'。"
echo
echo "完成：$OUT.elf / $OUT.bin"
