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
   - 内存优先：`SMC_SM3_KERNEL=3`（栈 <100B）、`SMC_SM4_KERNEL=3`（仅 256B S-box）、`SMC_SM2_ENC=0`
3. **加入源文件**：`crypto/src/*.c`（算法库零 libc 依赖）+ 需要传输时 `dtlcp/src/*.c`（仅依赖 string.h）。
4. **接入随机数**：`smc_rng.entropy = 你的 TRNG 回调`；签名 nonce / IV / 密钥生成都走此接口。
5. **构建**：`cd embedded/cortex-m4 && make`（需 arm-none-eabi-gcc），或把源码拖入 CubeMX/Keil/IAR 工程。

## 内存预算（x86-64 对象实测，MCU 略小，见 bin/size_report.md）

| 配置 | Flash(text) | 静态 RAM | 备注 |
|---|---|---|---|
| 全功能 | ~20.8KB | 4.1KB | 含 SM4 T-table |
| 裁 SM2 加密 | ~19.1KB | 4.1KB | DTLCP 最小配置 |
| 最小（SMALL/TINY 内核） | ~18.2KB | 0 | 适合 <32KB Flash MCU |

栈深：SM2 求逆窗口表路径约 688B（`fe_pow_mod`），建议任务栈 ≥2KB。
DTLCP 完整协议栈上下文约 7KB（含 3×1600B 记录缓冲，可在 `dtlcp_internal.h` 调小）。

## RTOS 集成注意

- 算法函数无全局状态（SM4 T-table 懒初始化除外，建议启动时调用一次 `sm4_encrypt_block` 预热）；
- `dtlcp_ctx` 每连接一个实例，勿跨任务共享；收发需与传输层回调线程安全对接；
- 长运算（SM2 约 1-5M 周期）期间记得喂狗：钩子 `smc_port_watchdog_feed` 可挂在标量乘循环（需要时开启）。

## 板上验证

`cortex-m4/main.c` 上电后运行 SM3/SM4 标准向量自检与微基准，经 UART 打印
`cycles/op`。将结果与 `docs/04` 中 host 数据对照即可得到目标板实测性能；
功耗实测可用开发板电流探头（如 Nordic PPK2）替换能量模型参数。
