# 资源受限物联网终端轻量级国密算法优化与 DTLCP 国密改造实现

> 竞赛选题编号：A1-33 ｜ 类型：软件设计（密码竞赛）
> 一句话：面向低成本 MCU / 低功耗传感终端的国密软件栈——SM2/SM3/SM4 轻量化裁剪与指令级优化，
> 加上 UDP 之上的 DTLCP 国密传输协议（握手精简至 0 ~ 1.5-RTT），附全套实测对比与固件安全改造规范。

---

## 1. 选题要求 → 交付对照

| 赛题要求 | 本仓库交付 | 实测结论（数据来源） |
|---|---|---|
| ① SM2/SM3/SM4 轻量化裁剪与指令级优化 | 三算法各提供 **REF（教科书基线）/ OPT（指令级优化）/ TINY·SMALL（最小内存）三内核**，编译期宏切换、同一 API；`smc_config.h` 功能裁剪矩阵 | **SM2 keygen 3.73× / sign 3.32× / verify 2.29× / ECDH 1.45×**（Solinas 折叠约减 + wNAF + 验签交织双标量乘 + **基点 comb 预计算表**）；SM4 **1.76–1.93×**（T-table）；裁剪后 Flash **26.6K→20.9K**、T-table RAM **4K→0**（同一份基准输出的 REF/OPT 比值，见 `bin/bench_results.md`） |
| ② 基于标准 DTLCP 完成国密改造、替换原生密码套件、UDP 低时延传输 | 记录层（SM4-CTR EtM / SM4-CBC MtE + epoch/序列号防重放）+ 握手精简（FULL / RESUME / PSK）+ HMAC-SM3 密钥调度 + 紧凑 SM2 证书 | FULL **1.5-RTT**（对比 TLCP 标准 3-RTT）、RESUME **1-RTT / 0 次 SM2**、PSK **0-RTT 早期数据**；FULL 握手时延 **6.53ms → 4.00ms（1.63×，comb 前后同条件 A/B，7 轮取最小）** |
| ③ 算法实测（算力/功耗）、可移植嵌入式 C、固件安全改造规范 | 基准/内存/功耗模型三类报告 + 零 libc 依赖嵌入式 C + 9 章固件安全规范 | 6 内核变体测试全绿且**差分指纹逐位一致**；KAT 与 OpenSSL 3.2 交叉验证（`docs/04`） |
| ④ **第三方基线对照**（外部锚点，非自证） | `scripts/thirdparty_bench.sh`：自动取 GmSSL 3.1.1（纯 C 国密参考实现）编译子集并**交错测量** | 对 GmSSL：SM4 **打平–1.12×**、SM2 基点乘 **1.61×**、验签 **1.03×**；**SM3 0.87×、变基点乘 0.62×**（根因：域乘法慢 2.2×，已列入第一优先优化项） |
| ✦ **常量时间不再放弃预计算表**（本轮新结论） | CT 构建以"静态仿射 comb 表 + **全表扫描 cmov**"复用同一张只读表；点运算一律**无条件算完 + 掩码选择**（不因无穷远提前返回）；`scripts/ct_cost.sh` 交错测量 | 基点路径的点加/倍点由 257 dbl+78 add 降到 **51 dbl + 51 add**，且 **4096 个标量上运算次数与域乘次数极差均为 0**；性能代价由约 **2.8× 降到 1.13×**（通用路径 1.18×） |
| ✦ **验签变基点腿降级为固定基点**（本轮新结论） | `sm2_peer_table_build` / `sm2_verify_with_table`（`SMC_SM2_PEER_TABLE`）：依据"验签输入全为公开数据 ⇒ 无需常量时间"为对端公钥建 comb 表 | verify **5.97M → 3.24M cycles（1.84×）**；建表 3.33M ≈ 0.56 次验签，**第 2 次验签起净赚**；每对端缓存 **2 KB** |
| ✦ 侧信道防护实证（加分项） | `SMC_SM2_CT` 常量时间标量乘 + **执行路径与工作量相关性实验**（`apps/bench/bench_sct.c`，覆盖通用/基点两条路径） | 确定性指标（运算次数 **+ 域乘次数**）：变时间构建点加随 nonce 在 **5–58**、**2–51** 间变化（域乘 2950–3232 / 1281–1347）；常量时间构建**基点 51/51/1347、通用 78/257/3484，两项极差均 0** → 执行路径与实际工作量都与秘密无关 |

---

## 2. 环境要求

| 项 | 要求 | 说明 |
|---|---|---|
| C 编译器 | gcc（MinGW-w64 或 Linux） | 本仓库在 `gcc 8.1.0 (MinGW-w64)` 下开发验证；**不依赖 make**，全部用 bash 脚本构建 |
| Python | 3.8+ | 功耗模型脚本 `scripts/power_model.py` |
| 线程库 | pthread（MinGW 自带 winpthreads） | 协议测试与基准计时使用 |
| OpenSSL 3.x | 可选 | 用于 KAT 期望值交叉验证；仓库内已固化期望值，跑测试不是必需 |
| ARM 工具链 | 可选 | `embedded/cortex-m4` 需 `arm-none-eabi-gcc`（本机未安装，交付源码+Makefile+移植说明） |

Windows 用户使用 **Git Bash** 执行全部命令。

---

## 3. 快速开始

```bash
bash scripts/build.sh        # ① 构建：算法库 6 变体 + 全部测试 + demo + bench
bash scripts/run_tests.sh    # ② 正确性：KAT / 差分指纹 / 协议端到端 / 攻击负向
bash scripts/demo_run.sh     # ③ 演示：UDP 传感终端 ↔ 网关，FULL/RESUME/PSK 三场景
bash scripts/benchmark.sh    # ④ 性能：REF vs OPT vs notab + 功耗估算 → bin/bench_results.md
bash scripts/size_report.sh  # ⑤ 内存：Flash/RAM/栈深 + 6 种裁剪配置 → bin/size_report.md
python scripts/power_model.py  # ⑥ 功耗：能量模型表 + 终端日能耗场景估算
bash scripts/gen_fixed_base.sh --check   # ⑦ 校验基点预计算表与生成器一致
bash scripts/thirdparty_bench.sh         # ⑧ 第三方基线对照（下载并编译 GmSSL 子集 → bin/thirdparty_compare.md）
bash scripts/ct_cost.sh                  # ⑨ 常量时间的性能代价，交错测量 → bin/ct_cost.md
./bin/bench_sct_ct$EXE --sweep 4096      # ⑩ 大样本恒定性门禁（4096 标量，退出码即断言）
bash scripts/check_embedded.sh           # ⑪ 板级代码预检（宿主仿真+语法+32K 证明，无需 ARM 工具链）

bash scripts/build.sh && ./bin/bench_sct_opt$EXE && ./bin/bench_sct_ct$EXE   # ⑨ 侧信道执行路径实验
```

> 也可用 Makefile（Linux/macOS/装有 make 的环境）：`make`（构建+测试）/ `make demo` / `make bench` / `make sct` / `make m4`。
> 仓库内置 **GitHub Actions CI**（Ubuntu + Windows 双平台：构建 → 基点表一致性 → 全量测试断言 → 差分指纹一致性断言 → 侧信道实验断言 → 报告 artifact）。

**预期结果**：

- `build.sh` 输出 `构建完成`，产物在 `bin/`：`test_{ref,opt,tiny,ct,lite,notab}`、`test_dtlcp`、`bench_{ref,opt,notab,tiny}`、`dtlcp_{sensor,gateway}`；
- `run_tests.sh` 输出 6 个变体 `失败 0`、6 行相同指纹 `80c111c8…40eb5`、DTLCP `42 项，失败 0`；
- `demo_run.sh` 输出三场景握手统计（FULL ≈4–6ms/5 次 SM2 → RESUME ≈1.0ms/0 次 SM2 → PSK 0-RTT 回显），末尾附模式对比摘要。

---

## 4. 目录结构与文件清单

```
crypto/         算法库（零 libc 依赖、零动态内存，可直接编入裸机固件）
  include/smc/smc_config.h   编译期裁剪开关 + 内核选择（本文件是"裁剪"总入口）
  include/smc/sm3.h  sm4.h  sm2.h  hmac_sm3.h  smc_rng.h
  src/sm3.c                  SM3 三内核：REF / FAST（8 轮展开）/ TINY（16 字环形调度，栈 ~64B）
  src/sm4.c                  SM4 三内核：REF / FAST（T-table 4KB）/ SMALL（仅 256B S-box）；CBC/CTR
  src/sm2_arith.c            SM2 域/标量/点算术：REF=试商长除法，OPT=Solinas 折叠约减+4bit 窗口标量乘
  src/sm2_base_table.h       **基点 comb 预计算表（自动生成，只读常量 2048B，不占 RAM）**
  src/sm2_internal.h         内部曲线类型与接口
  src/sm2.c                  ZA / 签名 / 验签 / ECDH / 公钥加密（C1‖C3‖C2，可裁剪）
  src/hmac_sm3.c  src/smc_rng.c       HMAC-SM3 / KDF / HMAC-SM3 DRBG

dtlcp/          DTLCP 协议栈（仅依赖 string.h）
  include/dtlcp/dtlcp.h      公共 API：套件/模式/配置/会话/统计
  src/dtlcp_crypto.c         密钥调度：PRF=P_hash(HMAC-SM3)、master/key_block/Finished
  src/dtlcp_record.c         记录层：EtM/MtE、显式长度定界、64 位滑动窗口防重放、确定性唯一 IV
  src/dtlcp_cert.c           紧凑 SM2 证书（签发/校验/TLV 编解码，X.509 映射见 docs/02 §4.1）
  src/dtlcp_hs.c             握手状态机：FULL 1.5-RTT / RESUME 1-RTT / PSK 0-RTT（含早期数据）
  src/dtlcp_internal.h       内部结构（记录层状态、上下文）

apps/           应用层（允许 OS/libc 依赖）
  gen/gen_fixed_base.c       基点 comb 表生成器（含自检：曲线上 + 与 REF 二进制标量乘比对）
  demo/dtlcp_sensor.c        传感终端（客户端）：连接、发遥测、收 ACK、统计
  demo/dtlcp_gateway.c       网关（服务端）：接受连接、回执、会话缓存
  demo/udp_io.{h,c}          UDP 传输（WinSock/POSIX 双后端）
  demo/demo_common.h         演示 PKI（确定性密钥）与统计打印
  bench/bench_crypto.c       微基准：cycles(RDTSC)/µs，多轮取最小，输出 markdown 表
  bench/bench_sct.c          侧信道实验：操作计数（确定性，通用/基点两条路径）+ 交错采样时间

tests/
  test_all.c                 算法 KAT + SM2 数学性质 + 往返 + 负向 + 差分指纹（--fp）
  test_dtlcp.c               证书链/三模式端到端（含 CBC 标准套件）/记录层攻击负向 + 密钥流唯一性 + 配置防误用（42 项）

embedded/       嵌入式移植
  port/smc_port.h            平台钩子：TRNG / 周期计数 / 看门狗 / 延时
  port/smc_port_stm32.c      STM32F4/L4 参考实现（DWT + 硬件 RNG）
  cortex-m4/                 Makefile + main.c(KAT+基准) + 启动文件 + 链接脚本
  README.md                  5 步移植指南与内存预算

Makefile        便捷入口（转发到 scripts/*.sh；Windows 无 make 时直接用脚本）
.github/workflows/ci.yml  GitHub Actions：Ubuntu + Windows 双平台构建与断言测试
scripts/        构建与测评（无 make，全 bash）
  build.sh run_tests.sh demo_run.sh benchmark.sh size_report.sh power_model.py
  gen_fixed_base.sh         基点表生成 / --check 校验（CI 使用）
  stack_depth.py            最大调用路径栈深（.su 帧 + objdump 调用图）

docs/           文档（建议阅读顺序：01 → 02 → 04 → 03）
  01-实现路径规划.md          技术路线、里程碑、风险对策
  02-系统设计说明.md          优化机理、协议设计、**与 TLCP/DTLS 标准差异表**、安全性质与限制
  03-固件安全改造规范.md      9 章规范（安全启动/密钥/TRNG/侧信道/DTLCP 使用/OTA/验收清单）
  04-测试与性能对比报告.md    测试方法与全部实测数据（算力/内存/协议开销/功耗估算）
  05-硬件采购清单.md          精简预算采购清单（¥60–120）+ 每件硬件的用途与测量接线
  06-作品设计报告.md          **复赛提交物**：按主办方模板撰写（匿名评审稿）
  07-分工与推进计划.md        硬件到位后的现状剖析 + 任务卡排期（占板/不占板两条队列）

AGENTS.md       项目长期记忆（选题、技术决策、调试经验——修改代码前必读）
bin/ obj/       构建产物（可随时删除后重建）
```

---

## 5. 技术要点速览

### 5.1 算法三内核（编译期切换）

| 算法 | REF（基线） | OPT（默认，最优性能） | TINY / SMALL（最小内存） |
|---|---|---|---|
| SM3 | 教科书消息扩展 | 2 路批量 + 压缩 8 轮展开 | 16 字环形滚动调度，**栈 ~64B** |
| SM4 | τ 与 L 分步 | T-table 合并 L∘τ（+4KB RAM） | 仅 256B S-box，**RAM 增量 0** |
| SM2 | 试商长除法约减 + 二进制标量乘 | **Solinas 折叠约减 + wNAF(w=4) 标量乘 + 验签交织双标量乘 + 基点 comb 预计算表**（`SMC_SM2_CT=1` 切换常量时间全表扫描） | 配合 `SMC_SM2_ENC=0`、`SMC_SM2_FIXED_TABLE=0` 等裁剪开关 |

**基点 comb 预计算表**（`SMC_SM2_FIXED_TABLE`，OPT 且非 CT 时默认开）：固定基点 k·G 由 wNAF 的
约 257 次倍点降为 51 次倍点 + ≤51 次混合加，使 **keygen 再快 2.68×、sign 再快 2.47×**；
代价是 **3096 B Flash（表 2048 B + 代码约 1 KB）、静态 RAM 增量 0**。
表由 `apps/gen/gen_fixed_base.c` 生成（生成时 1 次批量求逆转仿射，并自检与 REF 二进制标量乘一致），
`scripts/gen_fixed_base.sh --check` 校验表与生成器同步（CI 已纳入）。
**CT 构建不再禁用查表**（本轮改进）：改为对同一张表做**全表扫描 + cmov**（访存模式与运算次数恒定），
于是常量时间的性能代价从约 **2.8× 降到 1.13×**——"用预计算表"与"常量时间"其实并不冲突。
详见 `docs/04 §3.6` 与 `bash scripts/ct_cost.sh` 的实测。

**对端公钥预计算表**（`SMC_SM2_PEER_TABLE`，默认开）：验签 `R = sG + tP` 的变基点腿 `tP`
可借助"验签输入全为公开数据 ⇒ 无需常量时间"这一不对称性，也用 comb 表驱动，
使验签 **1.84×**（建表 0.56 次验签，第 2 次起净赚）。见 `docs/04 §3.7`。

功能裁剪（`smc_config.h`，`-D` 覆盖）：`SMC_SM2_ENC`（公钥加密）、`SMC_SM2_SIGN`、`SMC_SM2_ECDH`、
`SMC_SM4_MODE_CBC/CTR`、`SMC_DRBG`、`SMC_SM2_FIXED_TABLE`。裁剪组合的 Flash/RAM/栈深见 `bin/size_report.md`。

### 5.2 DTLCP 三模式（握手精简）

| 模式 | 流程 | RTT | SM2 运算 | 握手线上字节 | 握手时延（本机环回） |
|---|---|---|---|---|---|
| FULL | CH → SH‖Cert‖SKE‖CCS‖Fin → CCS‖Fin | 1.5-RTT | 5 次（2×临时密钥+签名+验签+ECDH） | ≈1198B | ≈4.0–6.0ms |
| RESUME | CH(sid) → SH‖CCS‖Fin → CCS‖Fin | 1-RTT | 0 | ≈428B | ≈1.0ms |
| PSK | CH(identity)[+early] → SH‖CCS‖Fin | 0-RTT 数据 | 0 | ≈474B | ≈1.0ms |

> FULL 的 5 次 SM2 中，2 次临时密钥生成与 1 次签名走基点 comb 快路径，验签与 ECDH 走交织/变基点路径；
> 该分布使 **comb 对握手时延的端到端贡献为 6.53ms → 4.00ms（1.63×，同条件 A/B、7 轮取最小）**。

密码套件：`ECDHE_SM4_CBC_SM3`（标准兼容，MAC-then-encrypt）/ `L_SM2_SM4_CTR_SM3`（默认轻量，EtM）/
`L_PSK_SM4_CTR_SM3`（PSK + 0-RTT）。协议细节与差异清单见 `docs/02`。

---

## 6. 正确性是怎么保证的（四层）

1. **标准 KAT 与 OpenSSL 交叉**：SM3（"abc"/"abcd"×4/1000×'q' 等）、SM4 标准向量、HMAC-SM3 的期望值
   均由本机 OpenSSL 3.2.4 生成后固化进测试；SM2 用曲线方程、n·G=∞、(n−1)G=−G 等数学性质自检。
2. **REF/OPT 差分**：6 个内核变体（ref/opt/tiny/ct/lite/notab）对同一输入集的输出做 SM3 摘要——
   必须逐位一致（当前 `80c111c874afde0179907d7fff4ef92c411a34c505882fa08e0bfff0d9640eb5`）。
   指纹输入包含 keygen/sign/ECDH/verify，因此**同时校验了 comb 预计算表与 wNAF 路径的等价性**。
3. **基点表专项校验**：生成器在生成时自检"全部表项在曲线上 + 16 个随机标量与 REF 二进制标量乘一致"；
   `scripts/gen_fixed_base.sh --check` 在 CI 中校验仓库内表与生成器同步（防止表被手工改动或过期）。
4. **协议端到端**：三模式 UDP 会话 + CBC 标准套件 + 0-RTT 早期数据 + 加密回显（`tests/test_dtlcp.c`）。
5. **攻击负向**：重放、密文篡改、截断、伪造 MAC、窗口内乱序重复、证书篡改、非法曲线点——全部必须被拒绝；
6. **侧信道执行路径**：确定性操作计数证明变时间构建的加法次数随 nonce 变化（通用路径 5–58、基点路径 2–51），
   常量时间构建两条路径的运算次数与域乘次数均恒定（基点 51/51/1347、通用 78/257/3484）
   ——CI 中对两个断言自动校验（含 `--sweep 4096` 的极差退出码门禁）；
   另有**大样本门禁** `./bin/bench_sct_ct --sweep 4096`：4096 个标量（含 3 个边界值）上极差必须为 0；
7. **对端表专项测试（12 项）**：与 `sm2_verify` 差分一致、篡改/错摘要拒绝、
   **表与公钥不匹配时 fail-closed**、非法曲线点建表被拒、表项全部在曲线上、紧凑表（2 KB）结果一致；
8. **云端复现**：GitHub Actions 在 Ubuntu 与 Windows 两个平台分别构建并跑全量测试，差分指纹种类数断言为 1。

复现：`bash scripts/run_tests.sh`（全绿即通过）。

---

## 7. 嵌入式接入（5 步）

1. 实现 `embedded/port/smc_port.h` 的 5 个钩子（TRNG / 周期计数 / 看门狗 / 延时 / 初始化）；
2. 在 `smc_config.h` 选内核档位（性能优先→FAST；<32KB Flash 的 MCU→TINY+SMALL+裁 ENC，并按需关 comb 表）；
3. 将 `crypto/src/*.c`（必须，含自动生成的 `sm2_base_table.h`）与 `dtlcp/src/*.c`（需要通信时）加入工程；
4. 把 TRNG 回调接到 `smc_rng.entropy` / `dtlcp_cfg.entropy`（**生产必须**，否则签名 nonce 走确定性测试路径）；
5. 构建（**不需要 make**）：`bash embedded/cortex-m4/build.sh sim`（无工具链也能在 PC 上跑通板级代码）
   → `bash embedded/cortex-m4/build.sh flash opt`（交叉编译）→ `bash embedded/cortex-m4/build.sh 32k opt`
   （32K 适配证明）。接线/烧录/记录表见 **`docs/08-硬件测试操作指南.md`**。

**栈预算（重要）**：`-fstack-usage` 只给单函数帧，实际需要的是调用链之和。
`scripts/stack_depth.py` 由 `.su` + `objdump` 调用图算出**最大调用路径栈深**：
默认配置 **3280 B**（路径 `scalarmult_add_naf → build_odd_table → sm2_jac_add → sm2_fp_mul`），
常量时间构建 3936 B；按 1.5× 余量建议任务栈 **≥5 KB**（CT 构建 ≥6 KB）。
注意这替代了早期文档中"栈预算以 `fe_pow_mod` 688 B 计"的**不成立**结论——那只是单个函数帧。

内存预算、RTOS 集成注意与板上验证方法见 `embedded/README.md`。

---

## 8. 文档索引

| 文档 | 回答什么问题 |
|---|---|
| `docs/01-实现路径规划.md` | 为什么这样设计？技术路线、里程碑、风险与对策 |
| `docs/02-系统设计说明.md` | 优化机理（折叠约减/窗口标量乘/T-table）、协议设计、**与标准差异表**、安全性质与已知限制 |
| `docs/03-固件安全改造规范.md` | 固件要改什么？安全启动、密钥/TRNG、算法使用、侧信道、DTLCP 使用、OTA、验收清单（"应/宜/可/不得"分级） |
| `docs/04-测试与性能对比报告.md` | 实测数据汇总：正确性、算力、内存、协议开销、功耗估算、优化空间 |
| `docs/05-硬件采购清单.md` | 硬件采购清单与实测替换说明 |
| `docs/06-作品设计报告.md` | 复赛提交物（按主办方模板 5 章组织，匿名稿） |
| `docs/08-硬件测试操作指南.md` | **上板怎么做**：接线、烧录、记录表、功耗差值法、排查表、验收清单 |
| `docs/figures/系统框架图.pptx` | 系统分层框架图（图形对象手工绘制、可在 PowerPoint 继续编辑；同名 `.png` 可直接插入报告，生成脚本 `build_framework_pptx.py` 可复现） |
| `docs/07-分工与推进计划.md` | 硬件到位后的现状剖析、任务卡 T0–T7 与占板排期 |
| `AGENTS.md` | 项目长期记忆：选题背景、已锁定技术决策、12 条关键调试经验（二次开发前必读） |

---

## 9. 常见问题（FAQ）

**Q：控制台/输出出现乱码？**
Git Bash 默认 GBK 代码页显示 UTF-8 中文所致；不影响功能。可将输出重定向到文件查看，或 `chcp 65001`。

**Q：MinGW 下 `%zu`/`%llu` 打印异常？**
构建脚本已统一加 `-D__USE_MINGW_ANSI_STDIO=1`；若自行编译请带上该宏。

**Q：调试信息看不到？**
本机某些包装命令（如 `timeout`）会吞掉 stderr——仓库内调试输出统一走 stdout，故可直接重定向排查。

**Q：想比较别的内核组合？**
`SMC_SM3_KERNEL` / `SMC_SM4_KERNEL` / `SMC_SM2_KERNEL`（1=REF、2=OPT/FAST、3=TINY/SMALL）
可在编译时任意组合；`scripts/build.sh` 中已有 ref/opt/tiny/ct/lite/notab 六组示例，加一组照抄即可。

**Q：改了 SM2 曲线参数或标量乘算法，基点预计算表怎么办？**
`bash scripts/gen_fixed_base.sh` 重新生成（生成器带自检：全部表项在曲线上、16 个随机标量与 REF
二进制标量乘一致）；`--check` 只校验不覆盖。CI 会断言仓库内的表与生成器一致，避免出现"过期表"。

**Q：常量时间（`SMC_SM2_CT=1`）到底慢多少？**
基点路径约 **1.13×**、通用路径约 **1.18×**（`bash scripts/ct_cost.sh` 交错测量，均值口径；
调整前的 1.06×/1.08× 是用「只数点运算调用次数」的判据量的，见 `docs/04 §3.6` 口径陷阱二）。
注意**不要用最小值作判据**：变时间路径的最小值来自输入集中最省的标量，跨输入比最小值会把代价
虚报成约 1.7×；同一输入集上的均值才是公平 A/B。

**Q：网关要对很多终端反复验签，怎么加速？**
用对端公钥预计算表：`sm2_peer_table_build(tbl, peer_pub)` 一次，之后 `sm2_verify_with_table(sig, dgst, tbl)`
每次约快 **1.84×**。表按公钥缓存（设备公钥长期不变），构建后只需保留 **2 KB**；
建表成本约 0.56 次普通验签，第 2 次验签起净赚。

**Q：栈要留多少？**
不要只看 `-fstack-usage` 的单函数帧（那只是自身帧）。用 `python scripts/stack_depth.py "obj/size/*_full.su"`
算最大调用路径：当前默认配置 **3280 B**，建议任务栈 ≥5 KB、常量时间构建 ≥6 KB。

**Q：没有 make / arm-none-eabi-gcc？**
主工程本来就不依赖 make；Cortex-M 工程需自行安装 GNU Arm 工具链后 `make`，或直接复制源码进 IDE 工程。

**Q：如何用真实硬件替换功耗估算？**
`scripts/power_model.py` 顶部三个参数（V / µA per MHz / f）改为板级实测值即可；
`embedded/cortex-m4/main.c` 在板上直接输出 DWT 周期数，两者结合得到实测 µJ/操作。

---

## 10. 已知限制与声明

- **功耗为能量模型估算**（参数化、非硬件实测），接口已预留板级实测替换；
- **DTLCP 是兼容子集**：TLCP（GB/T 38636）语义 + DTLS 数据报机制；无公开 DTLCP 全文标准，
  版本差异在 `docs/02 §4` 逐项列表（消息布局、RTT、cookie、证书格式等）；
- **0-RTT 风险**：早期数据无前向保密且可重放（与 TLS1.3 同），缓解见 `docs/03 §7`；
- **握手明文**（与 TLCP 一致）：关键参数由 SKE 签名覆盖，元数据可观察；
- **常量时间选项（`SMC_SM2_CT`）**覆盖标量乘表访问模式，点运算退化分支仍存在时序面（缓解见 `docs/03 §6`）；
  该构建**使用**comb 表，但以全表扫描 + cmov 选表项（访存模式与运算次数恒定），
  代价约 1.13×——即"恒定访存"与"预计算表"可以并存（`docs/04 §3.6`）；
- **默认（非 CT）构建的基点 comb 表索引依赖秘密**——与既有的 wNAF 同属"秘密相关访存"类别，
  由 `apps/bench/bench_sct.c` 的执行路径实验**显式量化并如实报告**（而非声称默认构建即抗侧信道）；
- **对端公钥表（`sm2_verify_with_table`）的安全依据是"验签输入全为公开数据"**：
  签名 (r,s)、摘要、公钥 P 及其导出的 t 均不保密，故查表无泄漏面，无需常量时间。
  但这**不适用于签名路径**（k、d 保密，必须走 CT 构建）。表与公钥绑定：
  传错表只会算出不同的 R 从而验签失败（fail-closed），不会误判通过；每对端缓存 2 KB。
- **加速比口径**：全部为**同一次基准输出内的 REF/OPT 比值**。RDTSC 绝对值受 CPU 频率状态影响，
  跨运行不可比；`notab` 变体（同内核、仅关 comb 表）用于隔离单项优化的净收益；
  与第三方（GmSSL）的比值取自**交错测量**的最小值（`scripts/thirdparty_bench.sh`）。
  **<10% 量级的小差值（如 `fe_mul` 的 REF/OPT 比较）只能用交错测量判定**——顺序快照出现过离群值。
- **对外部基线仍有落后项（如实声明）**：SM3 慢 GmSSL 0.87×；变基点标量乘慢 0.64×，
  根因已定位为域运算慢 2.0–2.2×（`docs/04 §4`），列为下一轮第一优先优化项。
  另：本库 OPT 的 Solinas 折叠约减在**有硬件除法**的 x86-64 上略慢于 REF 的试商长除法
  （`fe_mul` 交错测量 REF 533 vs OPT 561；"MCU 无硬件除法时更优"仅为待板上验证的假设）。

---

## 11. 一键复现命令汇总

```bash
git clone <repo> && cd <repo>     # 或直接进入项目目录
bash scripts/build.sh             # 构建
bash scripts/run_tests.sh         # 正确性（应全绿 + 6 行指纹一致）
bash scripts/demo_run.sh          # 三模式 UDP 演示
bash scripts/benchmark.sh         # 性能对比（bin/bench_results.md）
bash scripts/size_report.sh       # 内存与栈深（bin/size_report.md）
python scripts/power_model.py     # 功耗估算
bash scripts/gen_fixed_base.sh --check   # 基点表与生成器一致性
bash scripts/ct_cost.sh                  # 常量时间的性能代价（交错测量）
./bin/bench_sct_ct --sweep 4096          # 大样本恒定性门禁（退出码即断言）
```
