# Makefile — 便捷入口（Linux / macOS / 装有 make 的环境）
#
# 说明：所有构建逻辑统一收敛在 scripts/build.sh（单一事实来源），
#       本文件仅做转发；Windows 无 make 时请直接使用 scripts/*.sh。
#
# 常用目标：
#   make            等价 make test（构建 + 全量测试）
#   make build      构建全部产物到 bin/
#   make test       KAT + 差分指纹 + 协议端到端 + 攻击负向
#   make demo       FULL / RESUME / PSK 三模式 UDP 演示
#   make bench      性能基准 → bin/bench_results.md
#   make size       Flash/RAM/栈 + 裁剪矩阵 → bin/size_report.md
#   make power      F103 板端功耗（实测 DWT 周期数 → 实测 µJ）→ bin/power_results.md
#   make check-data 核对实测数据与《作品设计报告》一致（含功耗、内存、协议、指纹）
#   make thirdparty 第三方基线对照（下载 GmSSL，交错测量 → bin/thirdparty_compare.md）
#   make sct        侧信道执行路径相关性实验（CT=0/1 对比）
#   make m4         Cortex-M4 交叉编译（需 arm-none-eabi-gcc）
#   make clean      清理产物

SHELL := /bin/bash
CROSS ?= arm-none-eabi-

.PHONY: all build test demo bench size power check-data thirdparty sct m4 clean help

all: test

help:
	@sed -n 's/^#   //p' Makefile | head -14

build:
	bash scripts/build.sh

test:
	bash scripts/run_tests.sh

demo:
	bash scripts/demo_run.sh

bench:
	bash scripts/benchmark.sh

size:
	bash scripts/size_report.sh

power:
	python3 scripts/power_report.py

check-data:
	python3 scripts/check_report_data.py

thirdparty:
	bash scripts/thirdparty_bench.sh

sct: build
	@echo "======== SMC_SM2_CT=0（变时间）========"
	./bin/bench_sct_opt$(shell uname -s | grep -qiE 'mingw|msys|cygwin' && echo .exe)
	@echo ""
	@echo "======== SMC_SM2_CT=1（常量时间）========"
	./bin/bench_sct_ct$(shell uname -s | grep -qiE 'mingw|msys|cygwin' && echo .exe)

m4:
	$(MAKE) -C embedded/cortex-m4 CROSS=$(CROSS)

clean:
	rm -rf bin obj
	@echo "已清理 bin/ 与 obj/"
