# 当前实验结果与报告口径

## 1. 正确性与持续集成

- 7 个算法变体 `ref/opt/mcu/tiny/ct/lite/notab` 均为 0 失败。
- 7 个变体差分指纹均为
  `64a5385d17ad44be141970496e5b70dca47226fd387485cc4e2ae3bdf5b49eaf`。
- 默认 `DTLCP_RECBUF=1600` 与 MCU `DTLCP_RECBUF=1024` 两档均为 42 项、0 失败。
- Linux CI 通过基点表一致性、板级预检、性能基准、4096 标量恒定性门禁和 CT 代价测试；
  Windows CI 通过构建与全量测试。

证据：`logs/01-ci-current-test-summary.log` 和上述 CI 链接。

## 2. F103 真机算法性能

平台：STM32F103C8T6，Cortex-M3，HSI 8 MHz，GNU Arm 14.2.1 `-O2`，DWT CYCCNT。

| 操作 | REF cycles | MCU cycles | REF/MCU |
|---|---:|---:|---:|
| SM3 1 KB | 90,167 | 76,482 | 1.18x |
| SM3 4 KB | 343,177 | 290,852 | 1.18x |
| HMAC-SM3 256 B | 44,584 | 38,144 | 1.17x |
| SM4 ECB block | 1,398 | 897 | 1.56x |
| SM4 CBC 1 KB | 106,095 | 71,799 | 1.48x |
| SM4 CTR 1 KB | 102,070 | 68,986 | 1.48x |
| SM2 keygen | 18,510,336 | 3,403,883 | 5.44x |
| SM2 sign | 19,156,461 | 4,710,811 | 4.07x |
| SM2 verify | 37,180,413 | 11,484,901 | 3.24x |
| SM2 ECDH | 19,275,698 | 8,980,823 | 2.15x |

证据：`raw/board/*.json`、`tables/board_cycles.csv` 和 `tables/f103_speedup.csv`。

## 3. F103 与 W5500 协议实测

| 模式 | 握手时延 | 线上字节 | SM2 次数 | 应用 RTT | 早期数据 |
|---|---:|---:|---:|---:|---|
| FULL | 2274.97 ms | 1198 B | 5 | 2876.90 ms | 否 |
| RESUME | 58.04 ms | 428 B | 0 | 27.33 ms | 否 |
| PSK | 74.77 ms | 412 B | 0 | 29.98 ms | 否 |
| PSK 0-RTT | 92.65 ms | 472 B | 0 | 9.46 ms | 是 |

四种模式均完成加密遥测 ACK。0-RTT 表示早期数据随首包发送，不表示握手确认耗时为零。

证据：`logs/03-w5500-hardware.log` 和 `tables/w5500_dtlcp_modes.csv`。

## 4. 资源预算

- `sizeof(dtlcp_ctx)`：5248 B。
- 算法与协议静态 RAM：4104 B。
- 最长跨模块栈路径静态下界：4872 B。
- 三项合计：14224 B；相对 20 KiB 余 6256 B。
- F103 32 KiB 约束测量固件：text 29720 B，链接通过。
- W5500 FULL client/server：text 26116/26556 B，data 8 B，bss 10344 B。

证据：`logs/04-memory-gate.log`、`logs/05-firmware-sizes.log` 和
`tables/memory_budget.csv`。

## 5. 常量时间

确定性结论：CT 构建在 4096 个标量上满足：

- 通用路径：78 次点加、257 次倍点、3484 次域乘，三者极差均为 0。
- 基点路径：51 次点加、51 次倍点、1347 次域乘，三者极差均为 0。

耗时比例会随主机、编译器和运行状态变化。本机快照为基点 1.140x、通用 1.099x；
最新 CI Linux 快照为基点 1.080x、通用 1.143x。报告正文推荐概括为
“两条路径代价约 1.1 倍”，精确表格必须选择并完整引用其中一个快照。

证据：`raw/host-local/ct_cost.md`、`raw/ci-linux/ct_cost.md` 和
`tables/ct_cost_snapshots.csv`。

## 6. 第三方基线

本机交错测量中，本库 OPT 相对 GmSSL 3.1.1：SM2 基点乘 5.00x、变基点乘 2.04x、
验签 3.29x、域乘 1.95x；SM3 为 0.89-0.90x。签名 API 的随机数来源不同，
签名比值必须连同熵源开销说明一起引用。

证据：`raw/host-local/thirdparty_compare.md` 和 `tables/thirdparty_comparison.csv`。

## 7. 功耗

本目录不收录功耗模型估算数据。报告如引用功耗，必须标注为“模型估算”，
不得作为实测值或跨方案比较的依据；板级电流实测完成后再补充证据。
