# 内存占用报告（gcc -O2, x86-64 host 对象文件）

> text≈Flash 占用，data+bss≈RAM 静态占用。

> **栈深口径**：先用 -fstack-usage 取逐函数**自身**静态帧，再用 objdump 提取调用图求
> **最长加权调用路径**（scripts/stack_depth.py）。权重不含返回地址/寄存器保存等 ABI 开销，
> 函数指针间接调用不在图中，故为**下界**——任务栈须在此之上留安全余量。

## 配置：full

> 默认全功能（SM3/SM4=FAST，SM2=OPT 含基点 comb 表）

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 19864 | 0 | 0 |
| sm2 | 4460 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **29824** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：no-comb

> 同 full，关闭基点 comb 表（-DSMC_SM2_FIXED_TABLE=0）：量化 comb 的 Flash 代价

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 16908 | 0 | 0 |
| sm2 | 4460 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **26868** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：trim-noenc

> 裁 SM2 公钥加密/解密（DTLCP 最小配置）

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 19864 | 0 | 0 |
| sm2 | 2820 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **28184** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：trim-min

> 最小 RAM/Flash：SMALL+TINY 内核 + 裁 ENC/CBC + 关 comb 表

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1780 | 0 | 0 |
| sm4 | 1260 | 0 | 0 |
| sm2_arith | 13588 | 0 | 0 |
| sm2 | 2388 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **20624** | **0** | **0** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    fe_pow_mod	704 B
    sm2_jac_add	640 B
    sm2_jac_add_mixed	560 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：ct-nocomb

> 常量时间 + **关** comb 表（改造前的 CT 形态）：量化"用表与 CT 结合"省下的代价

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 14828 | 0 | 0 |
| sm2 | 4460 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **24788** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    sm2_scalarmult_jac	2000 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
    sm2_encrypt	672 B
```

**最大调用路径栈深（下界）：3392 B**

```
    sm2_scalarmult_add
      → sm2_scalarmult_jac
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3392 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：peer-off

> 关对端公钥预计算表（SMC_SM2_PEER_TABLE=0）：量化该特性的 Flash 代价

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 16544 | 0 | 0 |
| sm2 | 4028 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **26072** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    fe_pow_mod	704 B
    sm2_encrypt	672 B
    sm3_compress	656 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：tiny-stack

> 全功能 + SM3 TINY 滚动调度核（最小栈）

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1780 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 19864 | 0 | 0 |
| sm2 | 4460 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **29872** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    scalarmult_add_naf	1776 B
    scalarmult_naf	1104 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
```

**最大调用路径栈深（下界）：3216 B**

```
    scalarmult_add_naf
      → build_odd_table
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3216 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）

## 配置：ct-const

> 常量时间标量乘（SMC_SM2_CT=1；comb 表按设计自动禁用）

| 模块 | text(B) | data(B) | bss(B) |
|---|---|---|---|
| sm3 | 1732 | 0 | 0 |
| sm4 | 2160 | 0 | 4128 |
| sm2_arith | 18024 | 0 | 0 |
| sm2 | 4460 | 0 | 0 |
| hmac_sm3 | 836 | 0 | 0 |
| smc_rng | 772 | 0 | 0 |
| **合计** | **27984** | **0** | **4128** |

单函数最大帧 top5（自身帧）：

```
    sm2_scalarmult_jac	2000 B
    sm2_peer_table_build	1088 B
    fe_pow_mod	704 B
    sm2_verify_with_table	688 B
    sm2_encrypt	672 B
```

**最大调用路径栈深（下界）：3392 B**

```
    sm2_scalarmult_add
      → sm2_scalarmult_jac
      → sm2_jac_add
      → sm2_fp_mul
```

栈预算建议：按 1.5× 余量向上取整 ≥ **5 KB**（下界 3392 B；余量覆盖 ABI 开销、中断嵌套与编译器差异）
