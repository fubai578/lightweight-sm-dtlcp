# 实验环境与可复现入口

## F103 板测

- MCU：STM32F103C8T6，Cortex-M3。
- 时钟：HSI 8 MHz，未启用 PLL。
- 编译器：GNU Arm Embedded Toolchain 14.2.1，`-O2`。
- 周期计数：DWT CYCCNT；每项多轮取最小值。
- 网络：W5500 SPI，以太网直连 PC，UDP 4433。
- 调试与读取：ST-Link V2 / SWD；串口适配器为 CP2102。

复现入口：

```bash
bash embedded/cortex-m3/build.sh flash ref
bash embedded/cortex-m3/build.sh flash mcu
python scripts/board_bench.py --help
python scripts/compare_board_bench.py --help
bash scripts/check_m3_dtlcp.sh
```

## 主机与 CI

- 本机快照：x86-64，MinGW GCC 5.3，`-O2`，RDTSC。
- CI 快照：GitHub Actions Linux gcc，提交 `142da366dad81c649604a7b0fab3d2dff81c57a2`。
- CI 运行：<https://github.com/fubai578/lightweight-sm-dtlcp/actions/runs/38939385334>。
- GmSSL：3.1.1，双方均使用纯 C 路径，未编入 GmSSL 的高性能汇编路径。

复现入口：

```bash
bash scripts/run_tests.sh
bash scripts/benchmark.sh
bash scripts/ct_cost.sh 5
bash scripts/thirdparty_bench.sh
bash scripts/gen_fixed_base.sh --check
bash scripts/check_embedded.sh
```

主机 RDTSC 绝对值不可跨运行比较。每个加速比必须在同一快照内部计算。
