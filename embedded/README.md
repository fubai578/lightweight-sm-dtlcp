# embedded/ — 嵌入式移植层

本目录提供把算法库与 DTLCP 接入 MCU 固件所需的全部材料。

## 目录

| 路径 | 内容 |
|---|---|
| `port/smc_port.h` | 平台钩子接口：TRNG 熵源 / 周期计数 / 看门狗 / 延时 |
| `port/smc_port_stm32.c` | STM32F4/L4 参考实现（DWT 周期计数 + 硬件 RNG） |
| `cortex-m4/` | 裸机工程骨架：Makefile、main.c（KAT 自检 + 微基准）、链接脚本占位 |

## 快速移植（5 步）

1. **实现 `smc_port.h` 的 5 个钩子**（对照 `smc_port_stm32.c` 改时钟与 TRNG）。
2. **选择内核**：编译宏决定实现档位（`crypto/include/smc/smc_config.h`）：
   - 性能优先：`SMC_SM3_KERNEL=2`、`SMC_SM4_KERNEL=2`（T-table 4KB RAM）、`SMC_SM2_KERNEL=2`
     （默认含**基点 comb 预计算表**：＋3096 B Flash、RAM 增量 0；不需要可 `SMC_SM2_FIXED_TABLE=0`）
   - 内存优先：`SMC_SM3_KERNEL=3`（栈 <100B）、`SMC_SM4_KERNEL=3`（仅 256B S-box）、
     `SMC_SM2_ENC=0`、`SMC_SM2_FIXED_TABLE=0`
   - 抗侧信道优先：`SMC_SM2_CT=1`（comb 表按设计自动禁用；运算次数与访存恒定，代价约 2.8×）
3. **加入源文件**：`crypto/src/*.c`（算法库零 libc 依赖，**含自动生成的 `sm2_base_table.h`**）
   + 需要传输时 `dtlcp/src/*.c`（仅依赖 string.h）。
4. **接入随机数**：`smc_rng.entropy = 你的 TRNG 回调`；签名 nonce / IV / 密钥生成都走此接口。
5. **构建**：`cd embedded/cortex-m4 && make`（需 arm-none-eabi-gcc），或把源码拖入 CubeMX/Keil/IAR 工程。

## 内存预算（x86-64 对象实测，MCU 略小，见 bin/size_report.md）

| 配置 | Flash(text) | 静态 RAM | 备注 |
|---|---|---|---|
| 全功能（含 comb 表） | ~26.0KB | 4.1KB | 含 SM4 T-table 4KB ＋ comb 表 2KB |
| 关 comb 表 | ~22.9KB | 4.1KB | 省 3096 B Flash |
| 裁 SM2 加密 | ~24.2KB | 4.1KB | DTLCP 最小配置 |
| 最小（SMALL/TINY + 裁 ENC/CBC + 关 comb） | ~20.4KB | **0** | 满足 <32KB Flash 约束 |
| 常量时间标量乘 | ~20.8KB | 4.1KB | `SMC_SM2_CT=1`（wNAF/交织代码不编译） |

### 栈预算（**按调用路径算，不要按单函数帧**）

`-fstack-usage` 只给出**单函数自身帧**，而任务栈需要的是最长调用链之和。用
`python scripts/stack_depth.py "obj/size/*_full.su"` 计算：

| 指标 | 默认配置 | 常量时间构建 |
|---|---|---|
| 最大单函数帧 | `scalarmult_add_naf` 1792 B | `scalarmult_add_naf` 1792 B |
| **最大调用路径（下界）** | **3280 B** | **3440 B** |
| **建议任务栈（1.5× 余量）** | **≥5 KB** | **≥6 KB** |

最长路径：`scalarmult_add_naf → build_odd_table → sm2_jac_add → sm2_fp_mul → fe_mod_once`。

> 早期文档曾按"`fe_pow_mod` 688 B"预留栈——该结论**不成立**（那只是单个函数帧，且现已非最大热点）。
> 该数值随编译器版本变化，请在自己的工具链上重跑脚本后再定栈。
> DTLCP 完整协议栈上下文约 7KB（含 3×1600B 记录缓冲，可在 `dtlcp_internal.h` 调小）。

## RTOS 集成注意

- 算法函数无全局状态（SM4 T-table 懒初始化除外，建议启动时调用一次 `sm4_encrypt_block` 预热）；
  **基点 comb 表是 `static const` 只读常量，无初始化过程、无并发问题**；
- `dtlcp_ctx` 每连接一个实例，勿跨任务共享；收发需与传输层回调线程安全对接；
- 长运算（SM2 现约 0.26–2.6M 周期）期间记得喂狗：钩子 `smc_port_watchdog_feed` 可挂在标量乘循环（需要时开启）。

## 板上验证

`cortex-m4/main.c` 上电后运行 SM3/SM4 标准向量自检与微基准，经 UART 打印
`cycles/op`。将结果与 `docs/04` 中 host 数据对照即可得到目标板实测性能；
功耗实测可用开发板电流探头（如 Nordic PPK2）替换能量模型参数。
