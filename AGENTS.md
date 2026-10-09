# AGENTS.md — 项目长期记忆

> 本文件是本项目的长期记忆与协作者指南。**任何后续会话都应先读本文件。**

## 一、竞赛选题（不可变背景）

- **题目**：资源受限物联网终端轻量级国密算法优化与 DTLCP 国密改造实现（**A1-33**）
- **类型**：软件设计 / 密码竞赛（复赛选题）
- **背景**：物联网终端是安全防护薄弱环节；低成本 MCU 算力/内存/存储有限，直接跑标准国密算法时延与资源开销大；基于 UDP 的 DTLCP 协议原生支持国密套件，但完整握手与标准算法开销大。
- **要求**：
  1. 针对物联网 MCU、低功耗传感终端算力/存储/传输瓶颈，完成 **SM2/SM3/SM4 算法轻量化裁剪与指令级优化**，降低内存占用与运算耗时；
  2. 基于标准 DTLCP 协议完成**国密改造，替换原生密码套件**，实现 UDP 低时延物联网加密传输（精简握手流程）；
  3. 完成**算法实测**，对比优化前后算力开销、功耗指标，提供**可移植嵌入式 C 实现代码**与**固件安全改造规范**。

## 二、技术决策（已锁定，变更需记录）

| 决策点 | 结论 |
|---|---|
| 参照协议 | TLCP（GB/T 38636-2020）的 UDP 语义适配（DTLS 风格：epoch+序列号防重放、数据报记录层）。DTLCP 无公开全文标准，实现其**兼容子集**，差异点在 docs/02 列表说明 |
| A/B 对比策略 | 每个算法内核提供 REF（教科书基线）与 OPT（指令级优化）两套内核，同一套 API，构建两个二进制对比；差分测试保证二者输出一致 |
| SM3 内核 | REF（教科书消息扩展）/ FAST（展开压缩函数）/ TINY（滚动 16 字调度，栈占用极小） |
| SM4 内核 | REF（字节 S-box）/ FAST（T-table 4KB +8 轮展开）/ SMALL（256B S-box 无表展开） |
| SM2 内核 | 域算术：REF=试商长除法约减，OPT=Solinas 折叠约减（8×8 展开乘法）；标量乘：REF=二进制倍点法，OPT=**基点 comb 预计算表（k·G 专用）+ wNAF(w=4)（变基点）+ 验签交织双标量乘**，另提供常量时间全表扫描选项（SMC_SM2_CT，两条路径执行路径恒定 78 add/257 dbl，见 bench_sct） |
| SM2 基点表 | `SMC_SM2_FIXED_TABLE`（OPT 且非 CT 时默认开）：t=5 → 32 项/2048B **只读**表（占 Flash，RAM 增量 0）＋代码 1048B；keygen/sign 在该项上再快 2.68×/2.47×。**CT 构建自动禁用**（秘密相关查表与常量时间目标冲突）。表由源码生成，禁止手工编辑 |
| SM2 裁剪 | SIG（默认开）/ ENC（默认关）/ KE 仅保留 ECDH（DTLCP 用），SM2 加密、SM2KEP 可裁剪；config.h 生成裁剪尺寸对比表 |
| DTLCP 握手模式 | FULL（ECDHE+SM2 签名认证，2 航班 1.5-RTT）、RESUME（会话恢复 1-RTT）、PSK（预共享密钥，可 0-RTT 早期数据） |
| 记录层保护 | 标准套件 ECDHE_SM4_CBC_SM3（MAC-then-encrypt）与轻量套件 *_SM4_CTR_SM3（Encrypt-then-MAC），均 HMAC-SM3 |
| 密钥派生 | PRF = TLS 风格 P_hash(HMAC-SM3)；master_secret = PRF(PMS,"master secret",cr‖sr) |
| 交叉验证 | OpenSSL 3.2 CLI（本机有）验证 SM3/SM4/SM2；标准向量由调研给出；REF vs OPT 差分测试 |
| 评测口径 | host(x86-64) 实测 cycles/ns；Flash/RAM 用 `size`；**栈深必须用 `scripts/stack_depth.py` 算最大调用路径**（`-fstack-usage` 的单函数帧会严重低估）；功耗用能量模型（明确标注为估算） |
| 单项优化隔离 | 新增 `notab` 构建（同 opt 内核、仅 `SMC_SM2_FIXED_TABLE=0`）→ 单项收益可在同一份基准输出内相除，不受跨会话频率状态影响 |
| 构建 | 无 make → `scripts/build.sh`（bash + gcc）；Cortex-M 提供 arm-none-eabi Makefile（本机无交叉编译器，交付源码+构建脚本+移植说明） |

## 三、目录结构

```
crypto/     SM2/SM3/SM4/HMAC/DRBG 可移植嵌入式 C 实现（无动态内存、无 libc 依赖）
            含自动生成的 crypto/src/sm2_base_table.h（基点 comb 表，禁止手工编辑）
dtlcp/      DTLCP 记录层 + 握手 + 密钥调度 + UDP 传输抽象
apps/       bench 基准（bench_crypto / bench_sct / bench_gmssl）/ demo sensor+gateway / gen 基点表生成器
embedded/   平台移植层(port) + Cortex-M4 裸机工程骨架
tests/      KAT + 差分 + 协议仿真测试
docs/       01 路径规划 / 02 系统设计 / 03 固件安全规范 / 04 测试与性能对比 / 05 硬件采购 / 06 作品设计报告
scripts/    build.sh run_tests.sh demo_run.sh benchmark.sh size_report.sh power_model.py
            gen_fixed_base.sh（表生成/校验）stack_depth.py（最大调用路径栈深）
            thirdparty_bench.sh + compare_bench.py（GmSSL 对照，交错测量）
```

## 四、进度日志（最近一轮：2026-10-09）

- [x] 选题接收、环境检查（gcc 8.1 / python 3.13 / openssl 3.2.4；无 make、无 arm-gcc）
- [x] 实现路径规划文档（docs/01）
- [x] 算法库：SM3/SM4/SM2 三内核 + HMAC/KDF/DRBG；KAT+OpenSSL 交叉+差分全绿
- [x] DTLCP 协议栈：记录层(长度定界/EtM/MtE/防重放) + 握手 FULL 1.5-RTT/RESUME/PSK 0-RTT
- [x] Demo（UDP sensor/gateway 三场景一键脚本）+ bench + size/power 报告
- [x] 嵌入式移植层 + Cortex-M4 工程骨架
- [x] 文档 01/02/03/04 + README
- [x] 全量测试 run_tests.sh 全绿（5 内核变体指纹一致 80c111c8…）
- [x] 评审修复完成：derive_iv 栈溢出(32→16 截断)、CBC 解密核分离(FAST 复用加密核需逆序轮密钥)、
      记录长度字段定界、cap/len 上限、CCS 后才接受应用数据(防零密钥注入)、
      verify_peer 强制、链式 transcript、套件降级拒绝、熵源接口
- [x] 最终状态：算法 5 变体 + DTLCP 36 项测试全绿，demo 三场景可用
- [x] 竞争力优化轮：wNAF+交织双标量乘（verify 1.93×/sign 1.19×）、侧信道执行路径实验（CT 恒定 78/257）、
      Makefile + GitHub Actions CI（双平台断言）、跨平台脚本修复（$EXE 探测）、基准稳健化（5 轮取最小）
- [x] 硬件采购清单（docs/05）：精简预算版（核心 ¥60–120）。核实结论：**只有 F407 档便宜板带硬件 RNG**
      （F401CCU6 / F103C8T6 均无 RNG，不能用于 TRNG 演示）；「适配 <32KB Flash」用链接脚本
      （FLASH LENGTH=32K）链接证明即可，无需买小 Flash 板；功耗用 INA226（25µA/LSB，差值法）替代 PPK2；
      板级还需补 CMSIS 头 + GCC `_write()` 重定向（仓库现为 ARMCC 的 `__io_putchar`）
- [x] 采购结论自主复核：ST 官方页确认 F407VG 有 "True random number generator"、F401CC 特性表无 RNG；
      CMSIS 免费来源已定位（GitHub `STMicroelectronics/cmsis-device-f4` → `Include/stm32f4xx.h`，含 stm32f407xx.h）；
      立创天空星 SKU（item.szlcsc.com/44440459）显示停产；**淘宝商品页无法自动验证（返回空壳）**，
      故 docs/05 §8 只给搜索入口（淘宝/1688/立创）+ 已抓取验证的替代店铺页，不写未经验证的商品直链
- [x] **自主优化轮（2026-10-09）——基点 comb 预计算表 + 报告口径修正**：
      ① `apps/gen/gen_fixed_base.c` 生成 t=5/32 项基点 comb 表（`B_i=2^(i·d)G` 用反复倍点独立算出，
      Jacobian 链 + 1 次 Montgomery 批量求逆转仿射，生成时自检"曲线上 + 16 个随机标量与 REF 一致"）；
      `scalarmult_base` 走 comb（51 倍点+≤51 混合加，原 257 倍点）→ **keygen 3.64× / sign 3.24×**
      （OPT/REF 同轮），comb 净收益 **2.68×/2.47×**（notab A/B）；FULL 握手 6.53→4.00ms（1.63×）
      ② 新增 `notab` 构建变体 → 差分指纹扩到 **6 变体**（仍为 80c111c8…40eb5）
      ③ `bench_sct` 扩展为**通用+基点两条路径**（否则漏掉签名主路径）：CT=0 为 5–58 与 2–51，CT=1 均恒定 78/257
      ④ CI 增加"基点表与生成器一致性"断言；SCT 断言改为两条路径各计
      ⑤ **修正报告口径错误**：栈深排序字段用错导致"最大栈深 688B(fe_pow_mod)"结论错误——
      实为 `scalarmult_add_naf` 1792B，最大调用路径 **3280B（CT 3440B）** → 任务栈建议从 ≥2KB 改为 ≥5/6KB；
      `power_model.py` 解析取错列（iters 当 cycles）+ FULL 握手按"5×sign"折算 → 改为按真实构成
      （2×keygen+sign+verify+ecdh），FULL 握手能耗 REF 5556µJ → OPT 2284µJ（2.43×）
      ⑥ docs/04 重写为**单一权威快照**（此前叠了三代不同主机状态的数据）
- [x] **第三方基线对照轮（2026-10-09）**：引入 **GmSSL 3.1.1** 作外部锚点
      （`scripts/thirdparty_bench.sh` 自动下载+编译子集+**交错测量**，`bin/thirdparty_compare.md`）。
      实测：SM4 **1.03–1.12×**（打平/略胜）、SM2 基点乘 **1.59×**、验签 1.04×；
      **落后项如实记录**：SM3 0.87×、变基点乘 **0.64×**。
      **根因已定位**：域乘法 564 vs GmSSL 255 cycles（**慢 2.2×**）、域平方 0.49×、域求逆 0.50×
      ——变基点两侧倍点次数几乎相同（各约 256），差异只能来自域运算。
      ⇒ **第一优先优化项从"标量乘算法"改为"域运算改造"**（改一次性 Solinas 归约或 Montgomery 域）。
      另核实：**mbedTLS 主线 3.6.2 不含 SM2/SM3/SM4**（只有第三方 PSA 驱动），不能作国密基线
      ——原先"用 mbedTLS 对照"的建议前提是错的。
      同时发现：本库 OPT 的折叠约减在有硬件除法的 x86-64 上**略慢于 REF 试商长除法**
      （fe_mul 562 vs 533），"MCU 无除法时更优"降级为**待板上验证的假设**，不再作为结论引用
- [x] **报告口径与提交物（2026-10-09）**：docs/04 重写为单一权威快照并新增 §4 第三方对照；
      新增 **`docs/06-作品设计报告.md`**（按主办方模板 5 章，**匿名评审稿**）；
      新增 `scripts/stack_depth.py`（最大调用路径栈深，替换错误的单函数帧结论）；
      `power_model.py` 修正取列错误 + 按 FULL 握手真实构成折算
- [ ] **待硬件到位**：板上 cycles 实测（验证 SM3 展开在 MCU 的收益）、INA226 实测替换 power_model.py 参数、
      Cortex-M4 交叉编译 size/栈 与 32KB 链接约束证据
- [ ] **待做（第一优先）**：**域运算改造**——把 `fe_reduce_fold` 的"多轮折叠 + 试商收尾 + 两次条件减"
      改为**一次性硬编码 Solinas 归约**（GmSSL 形态：`r[i] = s[i] + s[8..15]` 组合，无循环无试商），
      预期 fe_mul 562 → 255–300 cycles，收益覆盖全部 SM2 操作（变基点 0.64×→有望 >1×）。
      次选：Montgomery 域；或二进制扩展欧几里得求逆（约 170 轮，栈 688→350 B，需为 CT 保留 Fermat 双实现）

## 五、关键调试经验（后续修改代码必读）

1. **T-table 旋转方向**：SM4 te1=ROTL(te0,24)、te3=ROTL(te0,8)（S[x]<<16 = ROTL(S[x]<<24,24)）
2. **fold 约减**：折叠后必须清零高位槽（acc[j]=0）+ 迭代到高位全零（严禁提前退出截断）
3. **窗口指数乘**：每 4-bit 窗口 4 次平方（×16），1 次是错的
4. **混合加法**：jac_add_mixed 的 Z3=(Z1+H)²−Z1Z1−HH 要赋给 z3
5. **记录层**：多记录拼接必须有显式长度字段（EtM 无长度无法定界）；seqb 由 epoch(2)+seq(6) 重建
6. **CCS 时序**：CCS 记录用旧密钥保护，其后第一条（Finished）才切新密钥
7. **SM2 加密**：C=C1‖C3‖C2 头长 97 字节（65+32），不是 96
8. **MinGW**：printf 需 -D__USE_MINGW_ANSI_STDIO=1（%zu/%llu）；stderr 在本机被吞，调试用 stdout
9. **CBC 解密**：FAST 内核 decrypt_block 复用加密核（要求轮密钥逆序）→ 记录层必须单独 setkey_dec
10. **derive_iv**：HMAC-SM3 输出 32 字节，IV 缓冲必须 32 字节接收后截断 16
11. **多数据报状态**：CCS 之类一次性标志必须跨数据报保持（放 for(;;) 外层）
12. **transcript 链式**：双方各自的 Finished 都要 absorb（发送后/校验后），保持对称
13. **wNAF**：signed digit 需保证中间值非负才能逻辑右移；表用 Jacobian 链建（免求逆）比批量求逆更省
14. **基准口径**：RDTSC 受 CPU 频率影响，跨运行绝对值不可比——用同一份输出的 REF/OPT 比值；每项 5 轮取最小。
    **会话内比值稳定（±5%），跨会话仍有系统偏差**（同一份 SM4 代码曾量到 1.48× 与 1.95×），
    故新增 `notab` 变体做单项 A/B 隔离；报告只认同一份输出内的比值
15. **comb 表生成器的三个坑**：
    ① 生成器对象**不能放 `obj/` 根目录**——`build.sh` 用 `obj/*_<suf>.o` 通配链接，
       文件名以 `_ref.o` 结尾会与之冲突（multiple definition）；改放 `obj/gen/` 子目录
    ② 占位项 `T[0]` 是**有效曲线点（G）**，靠坐标判零无法识别"K_j=0 需跳过"，
       必须由**索引 `K_j == 0`** 判断；且首项需特判（`K_(d-1)==0` 时累加器置无穷远，不能取 T[0]）
    ③ `sm2_jac_add_mixed` 的 `r==p` 别名安全，但本仓库沿用 wNAF 处的写法（临时变量再赋值）更稳
16. **栈深必须按调用路径算**：`-fstack-usage` 给的是单函数自身帧。`sort -t: -k3` 排的是非数字列
    （`.su` 格式为 `file:line:col:name<TAB>bytes<TAB>qualifier`，字节在第 2 个 TAB 字段），
    以此选"top5"会得出错误的最大栈深。用 `scripts/stack_depth.py`（.su + objdump 调用图）算最长加权路径
17. **报告自动化脚本要防"列错位"**：`power_model.py` 曾把基准表的第 2 列（iters）当 cycles，
    能量低估约 5 个数量级且被写进报告；基准行格式是 `| op | iters | cycles/op | us/op | ops/s |`，cycles 在第 3 列
18. **接第三方库做基准的坑（GmSSL 3.1.1，已解决，见 thirdparty_bench.sh）**：
    ① 无 cmake/make 时手工编译子集：`sm3/sm3_hmac/sm4_enc/sm4_setkey/sm4_common/sm4_modes/
       sm2_alg/sm2_lib/asn1/debug/hex` + 平台 RNG；`sm4_modes.c` 会引用 `ghash`（GCM 路径），
       基准不用 GCM，给桩即可，**别**去编 `gcm.c`（会连锁拉入 aes/block_cipher）
    ② **RNG 必须换平台实现**：默认 `rand.c` 读 `/dev/urandom`，Windows 上 `sm2_do_sign` 会静默返回 -1
       → 基准测到的是"失败返回"的耗时（曾误得 sign=316k cycles 的假数据）；改用 `rand_win.c`
    ③ `sm4_cbc_encrypt` 第 4 参数是**块数**，而 `sm4_ctr_encrypt` 同参数位是**字节数**——
       混用会越界写 16KB 直接段错误
    ④ 两侧二进制**不要放同一 obj 目录**：本项目 `build.sh` 用 `obj/*_<suf>.o` 通配链接，混放会 multiple definition
    ⑤ 交错测量（两侧交替跑、各取多轮最小值）才可信：跨进程的先后顺序会掺入 CPU 频率漂移
19. **对外部基线的公平性**：GmSSL 的 `sm2_do_sign` 每次签名向**系统 CSPRNG** 取 k
    （Windows 上约 455k cycles / 137µs），本库用 HMAC-SM3 派生 nonce。直接比 sign 会高估本库
    （1.97× vs 扣除后 1.52×）——熵源开销必须单列成行，让读者可扣除
