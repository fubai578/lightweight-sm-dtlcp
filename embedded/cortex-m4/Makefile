# cortex-m4/Makefile — ARM Cortex-M4 裸机构建（需 arm-none-eabi-gcc）
# 本机无交叉编译器时仅交付源码与构建脚本；在装有 GNU Arm 工具链的机器上：
#   cd embedded/cortex-m4 && make
# 产物：smc_m4.elf / smc_m4.bin（可用 OpenOCD/pyOCD 烧录）

CROSS   ?= arm-none-eabi-
CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE    := $(CROSS)size

MCU     ?= -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
DEFS    := -DSTM32F407xx -DSMC_CPU_HZ=168000000u
OPT     ?= -O2
CFLAGS  := $(MCU) $(OPT) -std=c99 -Wall -ffunction-sections -fdata-sections \
           -ffreestanding -fstack-usage \
           -I../../crypto/include -I../../crypto/src -I../port $(DEFS)
LDFLAGS := $(MCU) -Tstm32f407.ld -nostartfiles -Wl,--gc-sections -Wl,-Map=smc_m4.map

SRCS := main.c startup_stm32f407.c system_stm32f4xx_stub.c \
        ../port/smc_port_stm32.c \
        ../../crypto/src/sm3.c ../../crypto/src/sm4.c ../../crypto/src/sm2_arith.c \
        ../../crypto/src/sm2.c ../../crypto/src/hmac_sm3.c ../../crypto/src/smc_rng.c

OBJS := $(SRCS:.c=.o)

all: smc_m4.elf smc_m4.bin size

smc_m4.elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

smc_m4.bin: smc_m4.elf
	$(OBJCOPY) -O binary $< $@

size: smc_m4.elf
	@echo "--- stack usage hotspots ---"
	@cat *.su ../port/*.su ../../crypto/src/*.su 2>/dev/null | sort -t: -k3 -rn | head -8

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) *.su smc_m4.elf smc_m4.bin smc_m4.map

.PHONY: all clean size
