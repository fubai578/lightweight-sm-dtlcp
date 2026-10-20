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
