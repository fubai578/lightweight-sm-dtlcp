/*
 * smc_config.h — 编译期裁剪与内核选择配置
 *
 * 可移植嵌入式 C：C99，无动态内存，无 libc 依赖（算法层）。
 * 通过 -D 覆盖默认值实现"轻量化裁剪"：
 *   SMC_SM2_ENC=0        裁掉 SM2 公钥加密/解密（DTLCP 不需要）
 *   SMC_SM2_SIGN=0       裁掉 SM2 签名/验签（纯 PSK 部署可裁）
 *   SMC_SM2_ECDH=0       裁掉 ECDH（纯 PSK 部署可裁）
 *   SMC_SM4_MODE_CBC=0   裁掉 SM4-CBC
 *   SMC_SM4_MODE_CTR=0   裁掉 SM4-CTR
 *   SMC_SM3_KERNEL       1=REF 2=FAST(默认) 3=TINY(最小栈) 4=MCU(低访存)
 *   SMC_SM4_KERNEL       1=REF 2=FAST(默认) 3=SMALL(最小常量表)
 *   SMC_SM2_KERNEL       1=REF 2=OPT(默认)   SMC_SM2_CT=1 常量时间标量乘
 *   SMC_SM2_FIXED_TABLE  1=启用基点 comb 预计算表（OPT 且非 CT 时默认 1；省 Flash 可置 0）
 */
#ifndef SMC_CONFIG_H
#define SMC_CONFIG_H

/* ---- 功能裁剪 ---- */
#ifndef SMC_SM2_ENC
#define SMC_SM2_ENC 1
#endif
#ifndef SMC_SM2_SIGN
#define SMC_SM2_SIGN 1
#endif
#ifndef SMC_SM2_ECDH
#define SMC_SM2_ECDH 1
#endif
#ifndef SMC_SM4_MODE_CBC
#define SMC_SM4_MODE_CBC 1
#endif
#ifndef SMC_SM4_MODE_CTR
#define SMC_SM4_MODE_CTR 1
#endif
#ifndef SMC_HMAC_SM3
#define SMC_HMAC_SM3 1
#endif
#ifndef SMC_DRBG
#define SMC_DRBG 1
#endif

/* ---- 内核选择 ---- */
#define SMC_KERNEL_REF   1
#define SMC_KERNEL_FAST  2   /* SM4: T-table 展开 */
#define SMC_KERNEL_TINY  3   /* SM3: 最小栈 */
#define SMC_KERNEL_SMALL 3   /* SM4: 最小常量表 */
#define SMC_KERNEL_MCU   4   /* SM3: W[16] 滚动调度 + 16 轮展开，面向顺序执行 MCU */
#define SMC_KERNEL_OPT   2

#ifndef SMC_SM3_KERNEL
#define SMC_SM3_KERNEL SMC_KERNEL_FAST
#endif
#ifndef SMC_SM4_KERNEL
#define SMC_SM4_KERNEL SMC_KERNEL_FAST
#endif
#ifndef SMC_SM2_KERNEL
#define SMC_SM2_KERNEL SMC_KERNEL_OPT
#endif

/* SM2 标量乘常量时间变体（抗时序/SPA，性能约 2x 代价） */
#ifndef SMC_SM2_CT
#define SMC_SM2_CT 0
#endif

/* SM2 基点 comb 预计算表（k·G 专用）：
 *   仅 OPT 内核生效。**CT 与非 CT 都可启用**，区别只在"怎么选表项"：
 *     非 CT：以秘密位直接索引取表项（最快，属"秘密相关访存"）
 *     CT   ：对 32 项表做**全表扫描 + cmov** 选择（访存模式与运算次数均恒定）
 *   收益：k·G 由约 257 次倍点降为 51 次倍点（+≤51 次混合加）。
 *   代价：只读常量表 2048 B（计入 Flash/text，静态 RAM 增量 0）+ comb 代码约 1 KB。
 *   注意：CT 路径的全表扫描代价是每窗口 32 项 × 2 域的 cmov（廉价字操作），
 *         远低于它省下的 206 次倍点——实测 CT 的性能代价接近 0（见 docs/04 §3.6），
 *         这一条推翻了"常量时间必然显著变慢"的常见取舍。
 *   表由 scripts/gen_fixed_base.sh 生成 + 校验；置 0 可省 3 KB Flash（退化为 wNAF）。 */
#ifndef SMC_SM2_FIXED_TABLE
#if SMC_SM2_KERNEL == SMC_KERNEL_OPT
#define SMC_SM2_FIXED_TABLE 1
#else
#define SMC_SM2_FIXED_TABLE 0
#endif
#endif

/* SM2 验签的**对端公钥预计算表**（变基点 → 固定基点）：
 *   核心事实：验签的全部输入（签名 r/s、摘要、公钥）都是**公开数据**，
 *   t=(r+s) mod n 与 P 均不保密 ⇒ 对其建表并查表**没有侧信道顾虑**，
 *   与签名路径（k、d 保密，必须常量时间）形成明确的不对称。
 *   收益：R=sG+tP 的两条腿都变成固定基点乘（见 docs/04 §3.7）。
 *   代价：每对端 4096 B 调用方缓冲（构建后有效数据 2048 B，其余为构建期暂存），
 *         仅网关/验签侧需要；单次 sm2_verify 仍保留（不需要预计算时用）。
 *   依赖 SMC_SM2_SIGN（验签功能）与 SMC_SM2_KERNEL=2。 */
#ifndef SMC_SM2_PEER_TABLE
#if SMC_SM2_SIGN && SMC_SM2_KERNEL == SMC_KERNEL_OPT
#define SMC_SM2_PEER_TABLE 1
#else
#define SMC_SM2_PEER_TABLE 0
#endif
#endif
/* 表占用：4·TENT 个域元素（32 B 各）= 4096 B，其中**构建后有效数据为前 2·TENT 个
 * （2048 B，仿射表项）且地址连续**——因此构建完成后可只保留前
 * SMC_SM2_PEER_TABLE_ACTIVE_BYTES 字节（memcpy 出来即可），把每对端的缓存成本减半。
 * 仅 sm2_peer_table_build 需要完整的 4096 B（其余为构建期暂存）。 */
#define SMC_SM2_PEER_TABLE_BYTES        (4u * SMC_SM2_TENT * 32u)
#define SMC_SM2_PEER_TABLE_ACTIVE_BYTES (2u * SMC_SM2_TENT * 32u)

/* ---- comb 表参数（尺寸的唯一来源；表**内容**由 scripts/gen_fixed_base.sh 生成）----
 * 生成器会校验自己的 GEN_TBITS 与本处一致，不一致直接 #error，避免"表与代码参数错配"。 */
#define SMC_SM2_TBITS 5                                       /* 表索引位宽 t */
#define SMC_SM2_DBLK  ((256 + SMC_SM2_TBITS - 1) / SMC_SM2_TBITS)  /* 平衡量个数 d = 52 */
#define SMC_SM2_TENT  (1u << SMC_SM2_TBITS)                   /* 表项数 2^t = 32 */

/* ---- 尺寸常量 ---- */
#define SMC_SM3_DIGEST 32
#define SMC_SM3_BLOCK  64
#define SMC_SM4_BLOCK  16
#define SMC_SM4_KEY    16
#define SMC_SM2_BYTES  32
#define SMC_SM2_SIG    64   /* r||s */
#define SMC_SM2_PUBKEY 65   /* 0x04||x||y */

#endif /* SMC_CONFIG_H */
