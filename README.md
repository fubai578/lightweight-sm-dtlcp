# 资源受限物联网终端轻量级国密算法优化与 DTLCP 国密改造实现

> 竞赛选题编号：A1-33 ｜ 类型：软件设计（密码竞赛）
> 一句话：面向低成本 MCU / 低功耗传感终端的国密软件栈——SM2/SM3/SM4 轻量化裁剪与指令级优化，
> 加上 UDP 之上的 DTLCP 国密传输协议（握手精简至 0 ~ 1.5-RTT），附全套实测对比与固件安全改造规范。

---

## 1. 选题要求 → 交付对照

| 赛题要求 | 本仓库交付 | 实测结论（数据来源） |
|---|---|---|
| ① SM2/SM3/SM4 轻量化裁剪与指令级优化 | 三算法各提供 **REF（教科书基线）/ OPT（指令级优化）/ TINY·SMALL（最小内存）三内核**，编译期宏切换、同一 API；`smc_config.h` 功能裁剪矩阵 | **SM2 verify 1.93× / sign 1.19× / ECDH 1.25×**（wNAF+交织双标量乘）；SM4 **1.35–1.48×**（T-table）；裁剪后 Flash **20.8K→18.2K**、T-table RAM **4K→0**（同一份基准输出的 REF/OPT 比值，见 `bin/bench_results.md`） |
| ② 基于标准 DTLCP 完成国密改造、替换原生密码套件、UDP 低时延传输 | 记录层（SM4-CTR EtM / SM4-CBC MtE + epoch/序列号防重放）+ 握手精简（FULL / RESUME / PSK）+ HMAC-SM3 密钥调度 + 紧凑 SM2 证书 | FULL **1.5-RTT**（对比 TLCP 标准 3-RTT）、RESUME **1-RTT / 0 次 SM2**、PSK **0-RTT 早期数据**；握手时延 **13.5ms → 1.0ms** |
| ③ 算法实测（算力/功耗）、可移植嵌入式 C、固件安全改造规范 | 基准/内存/功耗模型三类报告 + 零 libc 依赖嵌入式 C + 9 章固件安全规范 | 5 内核变体测试全绿且**差分指纹逐位一致**；KAT 与 OpenSSL 3.2 交叉验证（`docs/04`） |
| ✦ 侧信道防护实证（加分项） | `SMC_SM2_CT` 常量时间标量乘 + **执行路径相关性实验**（`apps/bench/bench_sct.c`） | 操作计数（确定性指标）：变时间构建点加次数随 nonce 在 **5–58** 间变化；常量时间构建**恒定为 78**（极差 0）→ 执行路径与秘密无关 |

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
bash scripts/build.sh        # ① 构建：算法库 5 变体 + 全部测试 + demo + bench
bash scripts/run_tests.sh    # ② 正确性：KAT / 差分指纹 / 协议端到端 / 攻击负向
bash scripts/demo_run.sh     # ③ 演示：UDP 传感终端 ↔ 网关，FULL/RESUME/PSK 三场景
bash scripts/benchmark.sh    # ④ 性能：REF vs OPT 对比 + 功耗估算 → bin/bench_results.md
bash scripts/size_report.sh  # ⑤ 内存：Flash/RAM/栈 + 4 种裁剪配置 → bin/size_report.md
python scripts/power_model.py  # ⑥ 功耗：能量模型表 + 终端日能耗场景估算

bash scripts/build.sh && ./bin/bench_sct_opt$EXE && ./bin/bench_sct_ct$EXE   # ⑦ 侧信道执行路径实验
```

> 也可用 Makefile（Linux/macOS/装有 make 的环境）：`make`（构建+测试）/ `make demo` / `make bench` / `make sct` / `make m4`。
> 仓库内置 **GitHub Actions CI**（Ubuntu + Windows 双平台：构建 → 全量测试断言 → 差分指纹一致性断言 → 侧信道实验断言 → 报告 artifact）。

**预期结果**：

- `build.sh` 输出 `构建完成`，产物在 `bin/`：`test_{ref,opt,tiny,ct,lite}`、`test_dtlcp`、`bench_{ref,opt,tiny}`、`dtlcp_{sensor,gateway}`；
- `run_tests.sh` 输出 5 个变体 `失败 0`、5 行相同指纹 `80c111c8…40eb5`、DTLCP `36 项，失败 0`；
- `demo_run.sh` 输出三场景握手统计（FULL ≈11–13ms/5 次 SM2 → RESUME ≈1.0ms/0 次 SM2 → PSK 0-RTT 回显），末尾附模式对比摘要。

---

## 4. 目录结构与文件清单

```
crypto/         算法库（零 libc 依赖、零动态内存，可直接编入裸机固件）
  include/smc/smc_config.h   编译期裁剪开关 + 内核选择（本文件是"裁剪"总入口）
  include/smc/sm3.h  sm4.h  sm2.h  hmac_sm3.h  smc_rng.h
  src/sm3.c                  SM3 三内核：REF / FAST（8 轮展开）/ TINY（16 字环形调度，栈 ~64B）
  src/sm4.c                  SM4 三内核：REF / FAST（T-table 4KB）/ SMALL（仅 256B S-box）；CBC/CTR
  src/sm2_arith.c            SM2 域/标量/点算术：REF=试商长除法，OPT=Solinas 折叠约减+4bit 窗口标量乘
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
  demo/dtlcp_sensor.c        传感终端（客户端）：连接、发遥测、收 ACK、统计
  demo/dtlcp_gateway.c       网关（服务端）：接受连接、回执、会话缓存
  demo/udp_io.{h,c}          UDP 传输（WinSock/POSIX 双后端）
  demo/demo_common.h         演示 PKI（确定性密钥）与统计打印
  bench/bench_crypto.c       微基准：cycles(RDTSC)/µs，多轮取最小，输出 markdown 表
  bench/bench_sct.c          侧信道实验：操作计数（确定性）+ 交错采样时间

tests/
  test_all.c                 算法 KAT + SM2 数学性质 + 往返 + 负向 + 差分指纹（--fp）
  test_dtlcp.c               证书链/三模式端到端（含 CBC 标准套件）/记录层攻击负向（36 项）

embedded/       嵌入式移植
  port/smc_port.h            平台钩子：TRNG / 周期计数 / 看门狗 / 延时
  port/smc_port_stm32.c      STM32F4/L4 参考实现（DWT + 硬件 RNG）
  cortex-m4/                 Makefile + main.c(KAT+基准) + 启动文件 + 链接脚本
  README.md                  5 步移植指南与内存预算

Makefile        便捷入口（转发到 scripts/*.sh；Windows 无 make 时直接用脚本）
.github/workflows/ci.yml  GitHub Actions：Ubuntu + Windows 双平台构建与断言测试
scripts/        构建与测评（无 make，全 bash）
  build.sh run_tests.sh demo_run.sh benchmark.sh size_report.sh power_model.py

docs/           文档（建议阅读顺序：01 → 02 → 04 → 03）
  01-实现路径规划.md          技术路线、里程碑、风险对策
  02-系统设计说明.md          优化机理、协议设计、**与 TLCP/DTLS 标准差异表**、安全性质与限制
  03-固件安全改造规范.md      9 章规范（安全启动/密钥/TRNG/侧信道/DTLCP 使用/OTA/验收清单）
  04-测试与性能对比报告.md    测试方法与全部实测数据（算力/内存/协议开销/功耗估算）

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
| SM2 | 试商长除法约减 + 二进制标量乘 | **Solinas 折叠约减 + wNAF(w=4) 标量乘 + 验签交织双标量乘**（`SMC_SM2_CT=1` 切换常量时间全表扫描） | 配合 `SMC_SM2_ENC=0` 等裁剪开关 |

功能裁剪（`smc_config.h`，`-D` 覆盖）：`SMC_SM2_ENC`（公钥加密）、`SMC_SM2_SIGN`、`SMC_SM2_ECDH`、
`SMC_SM4_MODE_CBC/CTR`、`SMC_DRBG`。裁剪组合的 Flash/RAM 差值见 `bin/size_report.md`。

### 5.2 DTLCP 三模式（握手精简）

| 模式 | 流程 | RTT | SM2 运算 | 握手线上字节 | 握手时延 |
|---|---|---|---|---|---|
| FULL | CH → SH‖Cert‖SKE‖CCS‖Fin → CCS‖Fin | 1.5-RTT | 5 次（签名+验签+ECDH） | ≈1198B | ≈11–13.5ms |
| RESUME | CH(sid) → SH‖CCS‖Fin → CCS‖Fin | 1-RTT | 0 | ≈428B | ≈1.0ms |
| PSK | CH(identity)[+early] → SH‖CCS‖Fin | 0-RTT 数据 | 0 | ≈474B | ≈1.0ms |

密码套件：`ECDHE_SM4_CBC_SM3`（标准兼容，MAC-then-encrypt）/ `L_SM2_SM4_CTR_SM3`（默认轻量，EtM）/
`L_PSK_SM4_CTR_SM3`（PSK + 0-RTT）。协议细节与差异清单见 `docs/02`。

---

## 6. 正确性是怎么保证的（四层）

1. **标准 KAT 与 OpenSSL 交叉**：SM3（"abc"/"abcd"×4/1000×'q' 等）、SM4 标准向量、HMAC-SM3 的期望值
   均由本机 OpenSSL 3.2.4 生成后固化进测试；SM2 用曲线方程、n·G=∞、(n−1)G=−G 等数学性质自检。
2. **REF/OPT 差分**：5 个内核变体（ref/opt/tiny/ct/lite）对同一输入集的输出做 SM3 摘要——
   必须逐位一致（当前 `80c111c874afde0179907d7fff4ef92c411a34c505882fa08e0bfff0d9640eb5`）。
3. **协议端到端**：三模式 UDP 会话 + CBC 标准套件 + 0-RTT 早期数据 + 加密回显（`tests/test_dtlcp.c`）。
4. **攻击负向**：重放、密文篡改、截断、伪造 MAC、窗口内乱序重复、证书篡改、非法曲线点——全部必须被拒绝；
5. **侧信道执行路径**：确定性操作计数证明变时间构建的加法次数随 nonce 变化（5–58），常量时间构建恒定（78）——CI 中对两个断言自动校验；
6. **云端复现**：GitHub Actions 在 Ubuntu 与 Windows 两个平台分别构建并跑全量测试，差分指纹种类数断言为 1。

复现：`bash scripts/run_tests.sh`（全绿即通过）。

---

## 7. 嵌入式接入（5 步）

1. 实现 `embedded/port/smc_port.h` 的 5 个钩子（TRNG / 周期计数 / 看门狗 / 延时 / 初始化）；
2. 在 `smc_config.h` 选内核档位（性能优先→FAST；<32KB Flash 的 MCU→TINY+SMALL+裁 ENC）；
3. 将 `crypto/src/*.c`（必须）与 `dtlcp/src/*.c`（需要通信时）加入工程；
4. 把 TRNG 回调接到 `smc_rng.entropy` / `dtlcp_cfg.entropy`（**生产必须**，否则签名 nonce 走确定性测试路径）；
5. `cd embedded/cortex-m4 && make`（需 arm-none-eabi-gcc），或拖入 CubeMX/Keil/IAR 工程。

内存预算、RTOS 集成注意与板上验证方法见 `embedded/README.md`。

---

## 8. 文档索引

| 文档 | 回答什么问题 |
|---|---|
| `docs/01-实现路径规划.md` | 为什么这样设计？技术路线、里程碑、风险与对策 |
| `docs/02-系统设计说明.md` | 优化机理（折叠约减/窗口标量乘/T-table）、协议设计、**与标准差异表**、安全性质与已知限制 |
| `docs/03-固件安全改造规范.md` | 固件要改什么？安全启动、密钥/TRNG、算法使用、侧信道、DTLCP 使用、OTA、验收清单（"应/宜/可/不得"分级） |
| `docs/04-测试与性能对比报告.md` | 实测数据汇总：正确性、算力、内存、协议开销、功耗估算、优化空间 |
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
可在编译时任意组合；`scripts/build.sh` 中已有 ref/opt/tiny/ct/lite 五组示例，加一组照抄即可。

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
- 常量时间选项（`SMC_SM2_CT`）覆盖标量乘表访问模式，点运算退化分支仍存在时序面（缓解见 `docs/03 §6`）。

---

## 11. 一键复现命令汇总

```bash
git clone <repo> && cd <repo>     # 或直接进入项目目录
bash scripts/build.sh             # 构建
bash scripts/run_tests.sh         # 正确性（应全绿 + 指纹一致）
bash scripts/demo_run.sh          # 三模式 UDP 演示
bash scripts/benchmark.sh         # 性能对比（bin/bench_results.md）
bash scripts/size_report.sh       # 内存与裁剪（bin/size_report.md）
python scripts/power_model.py     # 功耗估算
```
