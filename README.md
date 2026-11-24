# 资源受限物联网终端轻量级国密算法优化与 DTLCP 国密改造实现

> 竞赛选题编号：A1-33 ｜ 类型：软件设计（密码竞赛）
> 一句话：面向低成本 MCU / 低功耗传感终端的国密软件栈——SM2/SM3/SM4 轻量化裁剪与指令级优化，
> 加上 UDP 之上的 DTLCP 国密传输协议（握手精简至 0 ~ 1.5-RTT），附全套实测对比与固件安全改造规范。

---

2026-11-24 新增：SM3 MCU 内核采用 16 字滚动调度与轮展开，同一 F103 固件中相对上一版 MCU 的哈希加速约 1.56×、HMAC 约 1.51×，压缩函数栈帧 368→224 B；代价为增加 2536 B Flash。详见 [SM3 MCU 优化实测](docs/11-SM3-MCU优化实测.md)。下表及 docs/04 保留原归档快照口径，不与本轮 A/B 混算。

2026-11-24 启动优化：F103 演示终端默认预置只读测试身份，去掉启动时重复生成与签发。三组同板 A/B 中，平台初始化后到首 ACK 中位时间 9652→5182 ms（减少 46.3%），静态 RAM 减少 944 B；FULL 认证与 ECDHE 不变。见 [启动身份优化实测](docs/13-F103启动身份优化实测.md)。

2026-11-24 SM2 专用平方：F103 同固件 A/B 中，域平方周期减少 8.6%、域求逆减少 9.2%，公钥导出/签名/验签/ECDH 减少约 3.9%–5.9%。代价为网络客户端增加 1304 B Flash，仍通过 32 KiB 链接。详见 [SM2 专用平方实测](docs/14-SM2专用平方优化实测.md)。

## 1. 选题要求 → 交付对照

| 赛题要求 | 本仓库交付 | 实测结论（数据来源） |
|---|---|---|
| ① SM2/SM3/SM4 轻量化裁剪与指令级优化 | REF/OPT/TINY·SMALL/MCU 多档内核，编译期宏切换、同一 API；`smc_config.h` 功能裁剪矩阵 | 当前 host 快照 SM2 **8.90×/6.22×/5.59×/3.63×**，F103 真机 **5.44×/4.07×/3.24×/2.15×**；SM3 MCU 1.17–1.18×；32 KiB 精简固件链接通过 |
| ② 基于标准 DTLCP 完成国密改造、替换原生密码套件、UDP 低时延传输 | 记录层（SM4-CTR EtM / SM4-CBC MtE + epoch/序列号防重放）+ 握手精简（FULL / RESUME / PSK）+ HMAC-SM3 密钥调度 + 紧凑 SM2 证书 | FULL **1.5-RTT**（对比 TLCP 标准 3-RTT）、RESUME **1-RTT / 0 次 SM2**、PSK **0-RTT 早期数据**；F103 + W5500 真机握手 **2274.97 / 58.04 / 74.77 / 92.65 ms**（见 `docs/04` §6） |
| ③ 算法实测（算力/功耗）、可移植嵌入式 C、固件安全改造规范 | 基准/内存/功耗实测三类报告 + 零 libc 依赖嵌入式 C + 9 章固件安全规范 | 7 内核变体测试全绿且**差分指纹逐位一致**；KAT 与 OpenSSL 3.2 交叉验证（`docs/04`） |
| ④ **第三方基线对照**（外部锚点，非自证） | `scripts/thirdparty_bench.sh`：自动取 GmSSL 3.1.1（纯 C 国密参考实现）编译子集并**交错测量** | 对 GmSSL：SM4 **1.04–1.07×**、SM2 基点乘 **5.00×**、变基点乘 **2.04×**、验签 **3.29×**、域乘 **1.95×**；SM3 0.89–0.90× |
| ✦ **常量时间不再放弃预计算表**（本轮新结论） | CT 构建以"静态仿射 comb 表 + **全表扫描 cmov**"复用同一张只读表；`scripts/ct_cost.sh` 交错测量 | 基点路径的点加/倍点由 257 dbl+78 add 降到 **51 dbl + 51 add**，且点运算与域乘工作量对 **4096 个标量极差为 0**；性能代价 **1.140×**（通用路径 1.099×） |
| ✦ **验签变基点腿降级为固定基点**（本轮新结论） | `sm2_peer_table_build` / `sm2_verify_with_table`（`SMC_SM2_PEER_TABLE`）：依据"验签输入全为公开数据 ⇒ 无需常量时间"为对端公钥建 comb 表 | verify **534103 → 290926 cycles（1.84×）**；建表 346170 cycles，0.65 次验签，**第 2 次验签起净赚**；每对端缓存 **2 KB** |
| ✦ 侧信道防护实证（加分项） | `SMC_SM2_CT` 常量时间标量乘 + **执行路径与工作量相关性实验**（`apps/bench/bench_sct.c`，覆盖通用/基点两条路径） | 确定性指标（运算次数 **+ 域乘次数**）：变时间构建点加随 nonce 在 **5–58**、**2–51** 间变化（域乘 2950–3232 / 1281–1347）；常量时间构建**基点 51/51/1347、通用 78/257/3484，两项极差均 0** → 执行路径与实际工作量都与秘密无关 |

---

## 2. 环境要求

| 项 | 要求 | 说明 |
|---|---|---|
| C 编译器 | gcc（MinGW-w64 或 Linux） | 本仓库在 `gcc 8.1.0 (MinGW-w64)` 下开发验证；**不依赖 make**，全部用 bash 脚本构建 |
| Python | 3.8+ | 功耗数据脚本 `scripts/power_report.py`（实测周期数 → µJ） |
| 线程库 | pthread（MinGW 自带 winpthreads） | 协议测试与基准计时使用 |
| OpenSSL 3.x | 可选 | 用于 KAT 期望值交叉验证；仓库内已固化期望值，跑测试不是必需 |
| ARM 工具链 | 可选 | `embedded/cortex-m4` 需 `arm-none-eabi-gcc`（本机未安装，交付源码+Makefile+移植说明） |

Windows 用户使用 **Git Bash** 执行全部命令。

---

## 3. 快速开始

本地演示平台：运行 `python apps/dashboard/server.py`，打开 <http://127.0.0.1:8765>。
已接通 F103 + W5500 的 FULL 模式，显示终端真实在线状态与网关事件；没有接入时保持空态，不加载演示数据。使用说明见 `apps/dashboard/README.md`。

```bash
bash scripts/build.sh        # ① 构建：算法库 7 变体 + 全部测试 + demo + bench
bash scripts/run_tests.sh    # ② 正确性：KAT / 差分指纹 / 协议端到端 / 攻击负向
bash scripts/demo_run.sh     # ③ 演示：UDP 传感终端 ↔ 网关，FULL/RESUME/PSK 三场景
bash scripts/benchmark.sh    # ④ 性能：REF vs OPT vs notab → bin/bench_results.md
bash scripts/size_report.sh  # ⑤ 内存：Flash/RAM/栈深 + 6 种裁剪配置 → bin/size_report.md
python scripts/power_report.py  # ⑥ 功耗：F103 板端实测周期数 → 实测 µJ → bin/power_results.md
bash scripts/gen_fixed_base.sh --check   # ⑦ 校验基点预计算表与生成器一致
bash scripts/thirdparty_bench.sh         # ⑧ 第三方基线对照（用本地 GmSSL 3.1.1 源码编译 → bin/thirdparty_compare.md）
bash scripts/ct_cost.sh                  # ⑨ 常量时间的性能代价，交错测量 → bin/ct_cost.md
./bin/bench_sct_ct$EXE --sweep 4096      # ⑩ 大样本恒定性门禁（4096 标量，退出码即断言）
bash scripts/check_embedded.sh           # ⑪ 板级代码预检（宿主仿真+语法+32K 证明，无需 ARM 工具链）
python scripts/check_report_data.py       # ⑫ 核对全部实测数据与报告一致（功耗/内存/协议/指纹）

bash scripts/build.sh && ./bin/bench_sct_opt$EXE && ./bin/bench_sct_ct$EXE   # ⑨ 侧信道执行路径实验
```

> 也可用 Makefile（Linux/macOS/装有 make 的环境）：`make`（构建+测试）/ `make demo` / `make bench` / `make power` / `make check-data` / `make sct` / `make m4`。
> 仓库内置**持续集成（CI）**（`.github/workflows/ci.yml`，Ubuntu + Windows 双平台：构建 → 基点表一致性 → 全量测试断言 → 差分指纹一致性断言 → 侧信道实验断言 → 报告 artifact）。
> CI 运行记录见 `results/report-evidence-2026-10-17/logs/01-ci-current-test-summary.log`。

**预期结果**：

- `build.sh` 输出 `构建完成`，产物在 `bin/`：`test_{ref,opt,mcu,tiny,ct,lite,notab}`、
  `test_dtlcp{,_mcu}`、`bench_{ref,opt,mcu,notab,tiny}`、`dtlcp_{sensor,gateway}`；
- `run_tests.sh` 输出 7 个变体 `失败 0`、7 行相同指纹 `64a5385d…b49eaf`、两档 DTLCP 各 `42 项，失败 0`；
- `demo_run.sh` 输出三场景握手统计（FULL 10.37 ms/5 次 SM2 → RESUME 1.34 ms/0 次 SM2 → PSK 0-RTT 1.00 ms 回显），末尾附模式对比摘要。

---

## 4. 目录结构与文件清单

```
crypto/         算法库（零 libc 依赖、零动态内存，可直接编入裸机固件）
  include/smc/smc_config.h   编译期裁剪开关 + 内核选择（本文件是"裁剪"总入口）
  include/smc/sm3.h  sm4.h  sm2.h  hmac_sm3.h  smc_rng.h
  src/sm3.c                  SM3 四内核：REF / FAST / TINY / MCU（低访存，F103 实测择优）
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
  port/smc_port_stm32.c      STM32F4 参考实现（DWT + **硬件 RNG**）
  port/smc_port_f1.c         STM32F103C8T6 实现（DWT + **ADC 噪声熵源**，该芯片无 RNG）
  cortex-m4/                 F407 工程：Makefile + main.c(KAT+基准) + 启动文件 + 链接脚本(1M/128K)
  cortex-m3/                 **F103C8T6 工程（实测平台）**：build.sh(不依赖make) + 自带最小寄存器头(不依赖CMSIS)
                             + 启动文件 + 链接脚本(64K/20K 与 **32K 适配证明**)
                             + W5500 单 socket UDP 驱动/探针与 FULL/RESUME/PSK/0-RTT 真机 demo
  README.md                  移植指南与内存预算

scripts/board_bench.py       F103 自动烧录 + SWD 读取完成态/KAT/熵源/cycles（不依赖串口日志）
scripts/compare_board_bench.py  REF/OPT/TINY/MCU 板测 JSON 对比表生成器
scripts/check_m3_dtlcp.sh    F103 DTLCP 结构体/静态 RAM/跨模块最长栈路径 20KB 门禁

Makefile        便捷入口（转发到 scripts/*.sh；Windows 无 make 时直接用脚本）
scripts/        构建与测评（无 make，全 bash）
  build.sh run_tests.sh demo_run.sh benchmark.sh size_report.sh power_report.py
  gen_fixed_base.sh         基点表生成 / --check 校验（CI 使用）
  stack_depth.py            最大调用路径栈深（.su 帧 + objdump 调用图）

docs/           文档（建议阅读顺序：01 → 02 → 04 → 03）
  01-实现路径规划.md          技术路线、里程碑、风险对策
  02-系统设计说明.md          优化机理、协议设计、**与 TLCP/DTLS 标准差异表**、安全性质与限制
  03-固件安全改造规范.md      9 章规范（安全启动/密钥/TRNG/侧信道/DTLS 使用/OTA/验收清单）
  04-测试与性能对比报告.md    测试方法与全部实测数据（算力/内存/协议开销/功耗实测）
  05-硬件采购清单.md          精简预算采购清单（¥60–120）+ 每件硬件的用途与测量接线
  06-作品设计报告.md          **复赛提交物**：按主办方模板撰写（匿名评审稿）
                              ※ 报告单独提交，不随本支撑材料包分发
  07-分工与推进计划.md        硬件到位后的现状剖析 + 任务卡排期（占板/不占板两条队列）

                ※ 内部协作笔记，不随本支撑材料包分发
bin/ obj/       构建产物（可随时删除后重建）
```

---

## 5. 技术要点速览

### 5.1 算法多档内核（编译期切换）

| 算法 | REF（基线） | OPT（默认，最优性能） | TINY / SMALL（最小内存） |
|---|---|---|---|
| SM3 | 教科书消息扩展 | 2 路批量 + 压缩 8 轮展开 | 16 字环形滚动调度，**栈 ~64B** |
| SM4 | τ 与 L 分步 | T-table 合并 L∘τ（+4KB RAM） | 仅 256B S-box，**RAM 增量 0** |
| SM2 | 试商长除法约减 + 二进制标量乘 | **Solinas 折叠约减 + wNAF(w=4) 标量乘 + 验签交织双标量乘 + 基点 comb 预计算表**（`SMC_SM2_CT=1` 切换常量时间全表扫描） | 配合 `SMC_SM2_ENC=0`、`SMC_SM2_FIXED_TABLE=0` 等裁剪开关 |

**基点 comb 预计算表**（`SMC_SM2_FIXED_TABLE`，OPT 且非 CT 时默认开）：固定基点 k·G 由 wNAF 的
约 257 次倍点降为 51 次倍点 + ≤51 次混合加，使 **keygen 再快 2.57×、sign 再快 2.07×**；
代价是 **2956 B Flash（表 2048 B + 代码 908 B）、静态 RAM 增量 0**。
表由 `apps/gen/gen_fixed_base.c` 生成（生成时 1 次批量求逆转仿射，并自检与 REF 二进制标量乘一致），
`scripts/gen_fixed_base.sh --check` 校验表与生成器同步（CI 已纳入）。
**CT 构建不再禁用查表**（本轮改进）：改为对同一张表做**全表扫描 + cmov**（访存模式与运算次数恒定），
于是常量时间的性能代价从 **2.8× 降到 1.140×**——"用预计算表"与"常量时间"其实并不冲突。
详见 `docs/04 §3.6` 与 `bash scripts/ct_cost.sh` 的实测。

**对端公钥预计算表**（`SMC_SM2_PEER_TABLE`，默认开）：验签 `R = sG + tP` 的变基点腿 `tP`
可借助"验签输入全为公开数据 ⇒ 无需常量时间"这一不对称性，也用 comb 表驱动，
使验签 **1.84×**（建表 346170 cycles，0.65 次验签，第 2 次起净赚）。见 `docs/04 §9`。

功能裁剪（`smc_config.h`，`-D` 覆盖）：`SMC_SM2_ENC`（公钥加密）、`SMC_SM2_SIGN`、`SMC_SM2_ECDH`、
`SMC_SM4_MODE_CBC/CTR`、`SMC_DRBG`、`SMC_SM2_FIXED_TABLE`。裁剪组合的 Flash/RAM/栈深见 `bin/size_report.md`
（该文件与 `results/report-evidence-2026-10-17/raw/host-local/size_report.md` 同源，即报告引用口径）。

### 5.2 DTLCP 三模式（握手精简）

| 模式 | 流程 | RTT | SM2 运算 | 握手线上字节 | 握手时延（本机环回） |
|---|---|---|---|---|---|
| FULL | CH → SH‖Cert‖SKE‖CCS‖Fin → CCS‖Fin | 1.5-RTT | 5 次（2×临时密钥+签名+验签+ECDH） | 1198B | 10.37ms |
| RESUME | CH(sid) → SH‖CCS‖Fin → CCS‖Fin | 1-RTT | 0 | 428B | 1.34ms |
| PSK | CH(identity)[+early] → SH‖CCS‖Fin | 0-RTT 数据 | 0 | 474B | 1.00ms |

> FULL 的 5 次 SM2 中，2 次临时密钥生成与 1 次签名走基点 comb 快路径，验签与 ECDH 走交织/变基点路径；
> 上表为 x86-64 主机 UDP 环回值，F103 + W5500 真机数据见 `docs/04` §6。

密码套件：`ECDHE_SM4_CBC_SM3`（标准兼容，MAC-then-encrypt）/ `L_SM2_SM4_CTR_SM3`（默认轻量，EtM）/
`L_PSK_SM4_CTR_SM3`（PSK + 0-RTT）。协议细节与差异清单见 `docs/02`。

---

## 6. 正确性是怎么保证的（四层）

1. **标准 KAT 与 OpenSSL 交叉**：SM3（"abc"/"abcd"×4/1000×'q' 等）、SM4 标准向量、HMAC-SM3 的期望值
   均由本机 OpenSSL 3.2.4 生成后固化进测试；SM2 用曲线方程、n·G=∞、(n−1)G=−G 等数学性质自检。
2. **REF/OPT 差分**：7 个内核变体（ref/opt/mcu/tiny/ct/lite/notab）对同一输入集的输出做 SM3 摘要——
   必须逐位一致（当前 `64a5385d17ad44be141970496e5b70dca47226fd387485cc4e2ae3bdf5b49eaf`）。
   指纹输入包含 512 组随机域乘及 keygen/sign/ECDH/verify，因此同时校验固定结构 Solinas
   归约、comb 预计算表与 wNAF 路径的等价性。
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
8. **云端复现**：持续集成在 Ubuntu 与 Windows 两个平台分别构建并跑全量测试，差分指纹种类数断言为 1。

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
算法对象默认路径下界 **3216 B**，常量时间构建 **3392 B**；F103 DTLCP 跨模块最长路径
为 **4872 B**。部署时还需为 ABI、中断与应用调用留余量。
注意这替代了早期文档中"栈预算以 `fe_pow_mod` 688 B 计"的**不成立**结论——那只是单个函数帧。

内存预算、RTOS 集成注意与板上验证方法见 `embedded/README.md`。

---

## 8. 文档索引

| 文档 | 回答什么问题 |
|---|---|
| `docs/01-实现路径规划.md` | 为什么这样设计？技术路线、里程碑、风险与对策 |
| `docs/02-系统设计说明.md` | 优化机理（折叠约减/窗口标量乘/T-table）、协议设计、**与标准差异表**、安全性质与已知限制 |
| `docs/03-固件安全改造规范.md` | 固件要改什么？安全启动、密钥/TRNG、算法使用、侧信道、DTLCP 使用、OTA、验收清单（"应/宜/可/不得"分级） |
| `docs/04-测试与性能对比报告.md` | 实测数据汇总：正确性、算力、内存、协议开销、功耗实测、优化空间 |
| `docs/05-硬件采购清单.md` | 硬件采购清单与实测替换说明 |
| `docs/06-作品设计报告.md` | 复赛提交物（按主办方模板 5 章组织，匿名稿）※ 单独提交，不随支撑材料包分发 |
| `docs/figures/系统框架图.pptx` | 系统分层框架图（图形对象手工绘制、可在 PowerPoint 继续编辑；同名 `.png` 可直接插入报告，生成脚本 `build_framework_pptx.py` 可复现） |
| `docs/08-硬件测试操作指南.md` | **上板怎么做（F407）**：接线、烧录、记录表、功耗差值法、排查表、验收清单 |
| `docs/09-F103上板操作指南.md` | **上板怎么做（F103C8T6，实测平台）**：接线、构建烧录、**六档尺寸实测表**、熵源边界、W5500 接线、功耗测量与记录、12 条移植踩坑 |
| `docs/07-分工与推进计划.md` | **硬件到位后的现状剖析**、任务卡 T0–T7 与占板排期 |
| `results/report-evidence-2026-10-17/` | **报告修订数据入口**：F103/W5500 原始数据、CSV、CI 证据、环境与统一引用口径 |

---

## 9. 常见问题（FAQ）

**Q：控制台/输出出现乱码？**
Git Bash 默认 GBK 代码页显示 UTF-8 中文所致；不影响功能。可将输出重定向到文件查看，或 `chcp 65001`。

**Q：MinGW 下 `%zu`/`%llu` 打印异常？**
构建脚本已统一加 `-D__USE_MINGW_ANSI_STDIO=1`；若自行编译请带上该宏。

**Q：调试信息看不到？**
本机某些包装命令（如 `timeout`）会吞掉 stderr——仓库内调试输出统一走 stdout，故可直接重定向排查。

**Q：想比较别的内核组合？**
`SMC_SM3_KERNEL`（1=REF、2=FAST、3=TINY、4=MCU）/
`SMC_SM4_KERNEL` / `SMC_SM2_KERNEL`（1=REF、2=OPT/FAST、3=SMALL）
可在编译时任意组合；`scripts/build.sh` 已含 ref/opt/mcu/tiny/ct/lite/notab 七组示例。

**Q：改了 SM2 曲线参数或标量乘算法，基点预计算表怎么办？**
`bash scripts/gen_fixed_base.sh` 重新生成（生成器带自检：全部表项在曲线上、16 个随机标量与 REF
二进制标量乘一致）；`--check` 只校验不覆盖。CI 会断言仓库内的表与生成器一致，避免出现"过期表"。

**Q：常量时间（`SMC_SM2_CT=1`）到底慢多少？**
基点路径 **1.140×**、通用路径 **1.099×**（`bash scripts/ct_cost.sh` 交错测量，均值口径；
调整前的 1.06×/1.08× 是用「只数点运算调用次数」的判据量的，见 `docs/04 §3.6` 口径陷阱二）。
注意**不要用最小值作判据**：变时间路径的最小值来自输入集中最省的标量，跨输入比最小值会把代价
虚报成约 1.7×；同一输入集上的均值才是公平 A/B。

**Q：网关要对很多终端反复验签，怎么加速？**
用对端公钥预计算表：`sm2_peer_table_build(tbl, peer_pub)` 一次，之后 `sm2_verify_with_table(sig, dgst, tbl)`
每次约快 **1.84×**。表按公钥缓存（设备公钥长期不变），构建后只需保留 **2 KB**；
建表成本 346170 cycles，约 0.65 次普通验签，第 2 次验签起净赚。

**Q：栈要留多少？**
不要只看 `-fstack-usage` 的单函数帧（那只是自身帧）。用 `python scripts/stack_depth.py "obj/size/*_full.su"`
算最大调用路径：算法对象当前下界 **3216 B**（CT 3392 B）；F103 DTLCP 全链路下界 **4872 B**。

**Q：没有 make / arm-none-eabi-gcc？**
主工程本来就不依赖 make；Cortex-M 工程需自行安装 GNU Arm 工具链后 `make`，或直接复制源码进 IDE 工程。

**Q：如何复核功耗实测数据？**
`python scripts/power_report.py` 由 F103 DWT 实测周期数（`results/report-evidence-2026-10-17/tables/board_cycles.csv`）
与板端参数（3.3 V、100 µA/MHz、8 MHz、非 SM2 固定开销 66 µJ）复算 µJ；
`python scripts/check_report_data.py` 核对复算结果与报告 3.4.6 一致。

---

## 10. 已知限制与声明

- **功耗为 F103 板端实测数据**：3.3 V、100 µA/MHz，FULL 握手 18857 → 5708 µJ（3.30×）；复算与核对见 `scripts/power_report.py` 与 `scripts/check_report_data.py`；
- **DTLCP 是兼容子集**：TLCP（GB/T 38636）语义 + DTLS 数据报机制；无公开 DTLCP 全文标准，
  版本差异在 `docs/02 §4` 逐项列表（消息布局、RTT、cookie、证书格式等）；
- **0-RTT 风险**：早期数据无前向保密且可重放（与 TLS1.3 同），缓解见 `docs/03 §7`；
- **握手明文**（与 TLCP 一致）：关键参数由 SKE 签名覆盖，元数据可观察；
- **常量时间选项（`SMC_SM2_CT`）**覆盖标量乘表访问模式，点运算退化分支仍存在时序面（缓解见 `docs/03 §6`）；
  该构建**使用**comb 表，但以全表扫描 + cmov 选表项（访存模式与运算次数恒定），
  代价 1.140×——即"恒定访存"与"预计算表"可以并存（`docs/04 §9`）；
- **默认（非 CT）构建的基点 comb 表索引依赖秘密**——与既有的 wNAF 同属"秘密相关访存"类别，
  由 `apps/bench/bench_sct.c` 的执行路径实验**显式量化并如实报告**（而非声称默认构建即抗侧信道）；
- **对端公钥表（`sm2_verify_with_table`）的安全依据是"验签输入全为公开数据"**：
  签名 (r,s)、摘要、公钥 P 及其导出的 t 均不保密，故查表无泄漏面，无需常量时间。
  但这**不适用于签名路径**（k、d 保密，必须走 CT 构建）。表与公钥绑定：
  传错表只会算出不同的 R 从而验签失败（fail-closed），不会误判通过；每对端缓存 2 KB。
- **加速比口径**：全部为**同一次基准输出内的 REF/OPT 比值**。RDTSC 绝对值受 CPU 频率状态影响，
  跨运行不可比；`notab` 变体（同内核、仅关 comb 表）用于隔离单项优化的净收益；
  与第三方（GmSSL）的比值取自**交错测量**的最小值（`scripts/thirdparty_bench.sh`）。
  小差值仍须交错测量；固定结构 Solinas 归约的收益已远超噪声区间。
- **域运算瓶颈已修复**：高位逆序单趟折叠 + 两次固定 carry 回折替代多轮复制与试商收尾。
  F103 ECDH 从 REF 的 0.83×反转为 **2.15×**；x86-64 `fe_mul` 为 94 cycles，
  对 GmSSL 纯 C 基线快 **1.95×**。仍如实保留的落后项是 SM3（对 GmSSL 0.90×）。

---

## 11. 一键复现命令汇总

```bash
git clone <repo> && cd <repo>     # 或直接进入项目目录
bash scripts/build.sh             # 构建
bash scripts/run_tests.sh         # 正确性（应全绿 + 6 行指纹一致）
bash scripts/demo_run.sh          # 三模式 UDP 演示
bash scripts/benchmark.sh         # 性能对比（bin/bench_results.md）
bash scripts/size_report.sh       # 内存与栈深（bin/size_report.md）
python scripts/power_report.py    # 功耗实测数据（与报告 3.4.6 同源）
python scripts/check_report_data.py  # 核对实测数据与报告一致
bash scripts/gen_fixed_base.sh --check   # 基点表与生成器一致性
bash scripts/ct_cost.sh                  # 常量时间的性能代价（交错测量）
./bin/bench_sct_ct --sweep 4096          # 大样本恒定性门禁（退出码即断言）
```
