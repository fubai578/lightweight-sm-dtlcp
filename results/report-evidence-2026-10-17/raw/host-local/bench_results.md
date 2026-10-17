# 算法基准（实测：x86-64 host, gcc -O2, 2026-10-16）

> REF=教科书基线内核；OPT=指令级优化内核（wNAF/折叠约减/T-table）
> notab=与 OPT 同内核但关闭 SM2 基点 comb 预计算表（同轮 A/B：comb 净收益）
> TINY=最小内存内核

> 口径：RDTSC 计数受 CPU 频率状态影响，**跨运行绝对值不可比**，横向对比请用同一次输出内的比值。

## [ref]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 6325 | 3.17 | 315681 |
| hash 4KB | 2000 | 24354 | 12.20 | 81968 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 211 | 0.11 | 9479572 |
| ECB decrypt block | 20000 | 208 | 0.10 | 9603380 |
| CBC encrypt 1KB | 2000 | 15771 | 7.90 | 126574 |
| CTR xor 1KB | 2000 | 15501 | 7.76 | 128785 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 3136 | 1.57 | 636517 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 1547326 | 775.12 | 1290 |
| sign | 32 | 1704883 | 854.09 | 1171 |
| verify | 32 | 2984754 | 1495.18 | 669 |
| ecdh | 32 | 1535691 | 769.29 | 1300 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 276 | 0.14 | 7238247 |
| fe_sqr | 20000 | 294 | 0.15 | 6788405 |
| fe_inv | 2000 | 107830 | 54.02 | 18513 |

## [opt]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 5858 | 2.93 | 340745 |
| hash 4KB | 2000 | 22504 | 11.27 | 88706 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 136 | 0.07 | 14726456 |
| ECB decrypt block | 20000 | 134 | 0.07 | 14938751 |
| CBC encrypt 1KB | 2000 | 10063 | 5.04 | 198371 |
| CTR xor 1KB | 2000 | 9942 | 4.98 | 200779 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 2902 | 1.45 | 687994 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 173824 | 87.08 | 11484 |
| sign | 32 | 274313 | 137.42 | 7277 |
| verify | 32 | 534103 | 267.55 | 3738 |
| ecdh | 32 | 423478 | 212.13 | 4714 |
| 对端表构建（一次性，单点） | 8 | 346170 | 173.41 | 5767 |
| verify（对端表，复用） | 32 | 290926 | 145.73 | 6862 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 94 | 0.05 | 21301523 |
| fe_sqr | 20000 | 94 | 0.05 | 21287919 |
| fe_inv | 2000 | 31118 | 15.59 | 64150 |

## [mcu]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 5702 | 2.86 | 350073 |
| hash 4KB | 2000 | 21697 | 10.87 | 92005 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 137 | 0.07 | 14584701 |
| ECB decrypt block | 20000 | 133 | 0.07 | 15039856 |
| CBC encrypt 1KB | 2000 | 10077 | 5.05 | 198090 |
| CTR xor 1KB | 2000 | 9946 | 4.98 | 200700 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 2797 | 1.40 | 713598 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 172164 | 86.24 | 11595 |
| sign | 32 | 275614 | 138.07 | 7243 |
| verify | 32 | 538249 | 269.63 | 3709 |
| ecdh | 32 | 429182 | 214.99 | 4651 |
| 对端表构建（一次性，单点） | 8 | 349685 | 175.16 | 5709 |
| verify（对端表，复用） | 32 | 293233 | 146.89 | 6808 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 95 | 0.05 | 21081480 |
| fe_sqr | 20000 | 94 | 0.05 | 21301523 |
| fe_inv | 2000 | 31326 | 15.69 | 63725 |

## [notab]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 5703 | 2.86 | 350030 |
| hash 4KB | 2000 | 21352 | 10.70 | 93490 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 134 | 0.07 | 14863258 |
| ECB decrypt block | 20000 | 133 | 0.07 | 15013888 |
| CBC encrypt 1KB | 2000 | 10064 | 5.04 | 198360 |
| CTR xor 1KB | 2000 | 9940 | 4.98 | 200833 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 2748 | 1.38 | 726480 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 446831 | 223.83 | 4468 |
| sign | 32 | 567080 | 284.08 | 3520 |
| verify | 32 | 536000 | 268.50 | 3724 |
| ecdh | 32 | 429375 | 215.09 | 4649 |
| 对端表构建（一次性，单点） | 8 | 347285 | 173.96 | 5748 |
| verify（对端表，复用） | 32 | 564632 | 282.85 | 3535 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 94 | 0.05 | 21204411 |
| fe_sqr | 20000 | 94 | 0.05 | 21202163 |
| fe_inv | 2000 | 31177 | 15.62 | 64030 |

## [tiny]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 5062 | 2.54 | 394376 |
| hash 4KB | 2000 | 19155 | 9.60 | 104215 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 210 | 0.10 | 9527893 |
| ECB decrypt block | 20000 | 223 | 0.11 | 8949747 |
| CBC encrypt 1KB | 2000 | 14660 | 7.34 | 136174 |
| CTR xor 1KB | 2000 | 14624 | 7.33 | 136504 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 2544 | 1.27 | 784683 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 170472 | 85.39 | 11710 |
| sign | 32 | 273818 | 137.17 | 7290 |
| verify | 32 | 534413 | 267.71 | 3735 |
| ecdh | 32 | 424325 | 212.56 | 4705 |
| 对端表构建（一次性，单点） | 8 | 346812 | 173.72 | 5756 |
| verify（对端表，复用） | 32 | 290987 | 145.77 | 6860 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 96 | 0.05 | 20865936 |
| fe_sqr | 20000 | 96 | 0.05 | 20792182 |
| fe_inv | 2000 | 31129 | 15.59 | 64129 |

## 功耗估算（能量模型）
#### 内核 [ref] 每操作能量

| operation | cycles/op | energy/op (µJ) |
|---|---|---|
| hash 1KB | 6325 | 1.897 |
| hash 4KB | 24354 | 7.306 |
| ECB encrypt block | 211 | 0.063 |
| ECB decrypt block | 208 | 0.062 |
| CBC encrypt 1KB | 15771 | 4.731 |
| CTR xor 1KB | 15501 | 4.650 |
| HMAC-SM3 (256B) | 3136 | 0.941 |
| keygen | 1547326 | 464.198 |
| sign | 1704883 | 511.465 |
| verify | 2984754 | 895.426 |
| ecdh | 1535691 | 460.707 |
| fe_mul | 276 | 0.083 |
| fe_sqr | 294 | 0.088 |
| fe_inv | 107830 | 32.349 |

#### 内核 [opt] 每操作能量

| operation | cycles/op | energy/op (µJ) |
|---|---|---|
| hash 1KB | 5858 | 1.757 |
| hash 4KB | 22504 | 6.751 |
| ECB encrypt block | 136 | 0.041 |
| ECB decrypt block | 134 | 0.040 |
| CBC encrypt 1KB | 10063 | 3.019 |
| CTR xor 1KB | 9942 | 2.983 |
| HMAC-SM3 (256B) | 2902 | 0.871 |
| keygen | 173824 | 52.147 |
| sign | 274313 | 82.294 |
| verify | 534103 | 160.231 |
| ecdh | 423478 | 127.043 |
| 对端表构建（一次性，单点） | 346170 | 103.851 |
| verify（对端表，复用） | 290926 | 87.278 |
| fe_mul | 94 | 0.028 |
| fe_sqr | 94 | 0.028 |
| fe_inv | 31118 | 9.335 |

#### 内核 [mcu] 每操作能量

| operation | cycles/op | energy/op (µJ) |
|---|---|---|
| hash 1KB | 5702 | 1.711 |
| hash 4KB | 21697 | 6.509 |
| ECB encrypt block | 137 | 0.041 |
| ECB decrypt block | 133 | 0.040 |
| CBC encrypt 1KB | 10077 | 3.023 |
| CTR xor 1KB | 9946 | 2.984 |
| HMAC-SM3 (256B) | 2797 | 0.839 |
| keygen | 172164 | 51.649 |
| sign | 275614 | 82.684 |
| verify | 538249 | 161.475 |
| ecdh | 429182 | 128.755 |
| 对端表构建（一次性，单点） | 349685 | 104.906 |
| verify（对端表，复用） | 293233 | 87.970 |
| fe_mul | 95 | 0.029 |
| fe_sqr | 94 | 0.028 |
| fe_inv | 31326 | 9.398 |

#### 内核 [notab] 每操作能量

| operation | cycles/op | energy/op (µJ) |
|---|---|---|
| hash 1KB | 5703 | 1.711 |
| hash 4KB | 21352 | 6.406 |
| ECB encrypt block | 134 | 0.040 |
| ECB decrypt block | 133 | 0.040 |
| CBC encrypt 1KB | 10064 | 3.019 |
| CTR xor 1KB | 9940 | 2.982 |
| HMAC-SM3 (256B) | 2748 | 0.824 |
| keygen | 446831 | 134.049 |
| sign | 567080 | 170.124 |
| verify | 536000 | 160.800 |
| ecdh | 429375 | 128.812 |
| 对端表构建（一次性，单点） | 347285 | 104.186 |
| verify（对端表，复用） | 564632 | 169.390 |
| fe_mul | 94 | 0.028 |
| fe_sqr | 94 | 0.028 |
| fe_inv | 31177 | 9.353 |

#### 内核 [tiny] 每操作能量

| operation | cycles/op | energy/op (µJ) |
|---|---|---|
| hash 1KB | 5062 | 1.519 |
| hash 4KB | 19155 | 5.747 |
| ECB encrypt block | 210 | 0.063 |
| ECB decrypt block | 223 | 0.067 |
| CBC encrypt 1KB | 14660 | 4.398 |
| CTR xor 1KB | 14624 | 4.387 |
| HMAC-SM3 (256B) | 2544 | 0.763 |
| keygen | 170472 | 51.142 |
| sign | 273818 | 82.145 |
| verify | 534413 | 160.324 |
| ecdh | 424325 | 127.297 |
| 对端表构建（一次性，单点） | 346812 | 104.044 |
| verify（对端表，复用） | 290987 | 87.296 |
| fe_mul | 96 | 0.029 |
| fe_sqr | 96 | 0.029 |
| fe_inv | 31129 | 9.339 |

> 模型参数：V_core=3.0V, I_dyn=100µA/MHz, f=26MHz → 7.80mW, 0.30nJ/cycle
> 注：本表为能量模型估算（非实测功耗）；将 V/I 参数替换为板级实测值即得实测结果。

#### 场景估算：低功耗传感终端（5 分钟 1 条遥测，日 288 条 + 1 次握手）

| 模式 | 握手能耗 (µJ) | 日通信总能耗 (µJ) | 占 2×AA 电池(2000mAh@3V=21600J) |
|---|---|---|---|
| FULL  (2×keygen+sign+verify+ecdh) | 534 | 6294 | 0.00003% |
| RESUME (0×SM2，会话恢复) | 60 | 5820 | 0.00003% |
| PSK (0×SM2，0-RTT 早期数据) | 60 | 5820 | 0.00003% |

> 同口径 REF → OPT：FULL 握手能耗 2856 µJ → 534 µJ（**5.35× 降低**）。
> FULL 按 [opt] 节实测 cycles 折算（2×keygen+sign+verify+ecdh ＋60µJ 固定开销）；
> RESUME/PSK 为 0 次 SM2；单条遥测按 SM4-CTR(48B)+HMAC-SM3+记录封装实测量级折算（20µJ）。
> 日能耗对比：RESUME/PSK 相对 FULL 省去 SM2 握手开销，残余为遥测与 KDF 固定开销。
