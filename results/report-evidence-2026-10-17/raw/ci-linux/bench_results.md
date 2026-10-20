# 算法基准（实测：x86-64 host, gcc -O2, 2026-10-16）

> REF=教科书基线内核；OPT=指令级优化内核（wNAF/折叠约减/T-table）
> notab=与 OPT 同内核但关闭 SM2 基点 comb 预计算表（同轮 A/B：comb 净收益）
> TINY=最小内存内核

> 口径：RDTSC 计数受 CPU 频率状态影响，**跨运行绝对值不可比**，横向对比请用同一次输出内的比值。

## [ref]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 14528 | 5.60 | 178691 |
| hash 4KB | 2000 | 55824 | 21.50 | 46505 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 372 | 0.14 | 6981665 |
| ECB decrypt block | 20000 | 371 | 0.14 | 6991420 |
| CBC encrypt 1KB | 2000 | 26305 | 10.13 | 98693 |
| CTR xor 1KB | 2000 | 25365 | 9.77 | 102348 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 6965 | 2.68 | 372743 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 1850023 | 712.62 | 1403 |
| sign | 32 | 2038515 | 785.23 | 1274 |
| verify | 32 | 3571088 | 1375.56 | 727 |
| ecdh | 32 | 1792305 | 690.39 | 1448 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 314 | 0.12 | 8259858 |
| fe_sqr | 20000 | 335 | 0.13 | 7737914 |
| fe_inv | 2000 | 126975 | 48.91 | 20445 |

## [opt]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 7505 | 2.89 | 345895 |
| hash 4KB | 2000 | 28765 | 11.08 | 90249 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 215 | 0.08 | 12064080 |
| ECB decrypt block | 20000 | 223 | 0.09 | 11654312 |
| CBC encrypt 1KB | 2000 | 16301 | 6.28 | 159259 |
| CTR xor 1KB | 2000 | 16215 | 6.25 | 160101 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 3675 | 1.42 | 706468 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 252844 | 97.40 | 10267 |
| sign | 32 | 367954 | 141.74 | 7055 |
| verify | 32 | 803644 | 309.56 | 3230 |
| ecdh | 32 | 641996 | 247.30 | 4044 |
| 对端表构建（一次性，单点） | 8 | 527706 | 203.27 | 4919 |
| verify（对端表，复用） | 32 | 445924 | 171.77 | 5822 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 129 | 0.05 | 20127548 |
| fe_sqr | 20000 | 129 | 0.05 | 20098584 |
| fe_inv | 2000 | 49049 | 18.89 | 52928 |

## [mcu]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 14757 | 5.68 | 175919 |
| hash 4KB | 2000 | 56113 | 21.61 | 46265 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 219 | 0.08 | 11838438 |
| ECB decrypt block | 20000 | 214 | 0.08 | 12104808 |
| CBC encrypt 1KB | 2000 | 16408 | 6.32 | 158219 |
| CTR xor 1KB | 2000 | 16301 | 6.28 | 159260 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 6936 | 2.67 | 374294 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 273519 | 105.36 | 9491 |
| sign | 32 | 378849 | 145.93 | 6852 |
| verify | 32 | 808149 | 311.30 | 3212 |
| ecdh | 32 | 645189 | 248.53 | 4024 |
| 对端表构建（一次性，单点） | 8 | 522402 | 201.23 | 4969 |
| verify（对端表，复用） | 32 | 437820 | 168.65 | 5929 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 127 | 0.05 | 20384160 |
| fe_sqr | 20000 | 128 | 0.05 | 20350911 |
| fe_inv | 2000 | 48073 | 18.52 | 54003 |

## [notab]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 8397 | 3.23 | 309183 |
| hash 4KB | 2000 | 32184 | 12.40 | 80664 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 223 | 0.09 | 11642880 |
| ECB decrypt block | 20000 | 225 | 0.09 | 11521560 |
| CBC encrypt 1KB | 2000 | 16512 | 6.36 | 157223 |
| CTR xor 1KB | 2000 | 16455 | 6.34 | 157773 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 4184 | 1.61 | 620522 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 692628 | 266.80 | 3748 |
| sign | 32 | 814540 | 313.76 | 3187 |
| verify | 32 | 830104 | 319.75 | 3127 |
| ecdh | 32 | 661760 | 254.91 | 3923 |
| 对端表构建（一次性，单点） | 8 | 540394 | 208.16 | 4804 |
| verify（对端表，复用） | 32 | 875518 | 337.25 | 2965 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 130 | 0.05 | 20009284 |
| fe_sqr | 20000 | 130 | 0.05 | 20029744 |
| fe_inv | 2000 | 49467 | 19.05 | 52481 |

## [tiny]
#### SM3 哈希
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| hash 1KB | 2000 | 7482 | 2.88 | 346989 |
| hash 4KB | 2000 | 30171 | 11.62 | 86044 |

#### SM4 分组密码
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| ECB encrypt block | 20000 | 349 | 0.13 | 7437759 |
| ECB decrypt block | 20000 | 349 | 0.13 | 7434991 |
| CBC encrypt 1KB | 2000 | 24099 | 9.28 | 107727 |
| CTR xor 1KB | 2000 | 24267 | 9.35 | 106980 |

#### HMAC-SM3
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| HMAC-SM3 (256B) | 4000 | 3708 | 1.43 | 700188 |

#### SM2 椭圆曲线
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| keygen | 32 | 260444 | 100.32 | 9968 |
| sign | 32 | 379063 | 146.01 | 6849 |
| verify | 32 | 815415 | 314.09 | 3184 |
| ecdh | 32 | 646447 | 249.01 | 4016 |
| 对端表构建（一次性，单点） | 8 | 525925 | 202.58 | 4936 |
| verify（对端表，复用） | 32 | 444209 | 171.11 | 5844 |

#### 域算术 Fp（根因定位用，非公共 API）
| operation | iters | cycles/op | us/op | ops/s |
|---|---|---|---|---|
| fe_mul | 20000 | 129 | 0.05 | 20155703 |
| fe_sqr | 20000 | 129 | 0.05 | 20145328 |
| fe_inv | 2000 | 48917 | 18.84 | 53072 |
