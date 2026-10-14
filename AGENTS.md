# AGENTS.md — 项目长期记忆

> 本文件是本项目的长期记忆与协作者指南。**任何后续会话都应先读本文件。**

## 〇、硬性工作流约定（用户明确要求，不可省略）

1. **每轮操作做完后必须提交并推送到 GitHub**——不是可选项。收尾动作固定为：
   ```bash
   cd "D:/learning/密码"
   bash scripts/run_tests.sh          # 先保绿：6 变体指纹逐位一致 + DTLCP 全绿
   git add -A && git commit -m "<类型>: <做了什么>"
   git push origin main
   ```
   远端 `origin = https://github.com/fubai578/lightweight-sm-dtlcp.git`（私有），
   凭据由 Windows 凭据管理器提供（`credential.helper=manager`），直接 `git push` 即可，无需 PAT。
2. **提交前先过匿名性检查**：仓库会作为支撑材料，**不得**出现单位/姓名/昵称/邮箱。
   文本用 grep 扫；**二进制文档（pptx/docx/pdf）要另外查文档属性**——
   `docProps/core.xml` 的 `dc:creator` / `cp:lastModifiedBy` 会被 WPS/PowerPoint 写入系统用户名
   （本项目实际出现过：框架图 pptx 的 lastModifiedBy = 昵称）。生成脚本已自动清空，
   但**每次在 Office 里另存后都要复查**。
3. **打包支撑材料时必须排除的文件**（匿名硬门槛）：`复赛通知附件/`（名单与签字页）、
   `.git/`（提交者身份）、**本文件 `AGENTS.md`（含 GitHub 账号）**。报告正文（docs/06）
   已扫描无泄漏；二进制文档要查文档属性（见上一条）。
4. **覆盖任何"不是本轮产出"的文件前先确认它是否被手工编辑过**（本项目踩过：
   重跑 `build_framework_pptx.py` 覆盖了手工调整过的框架图 → 从 WPS 备份中心恢复；
   备份路径 `%APPDATA%\kingsoft\office6\backup\<原名>.<hash>.<时间戳>.pptx`）。

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
| SM2 内核 | 域算术：REF=试商长除法约减，OPT=Solinas 折叠约减（8×8 展开乘法）；标量乘：REF=二进制倍点法，OPT=**基点 comb 预计算表（k·G 专用）+ wNAF(w=4)（变基点）+ 验签交织双标量乘**，另提供常量时间全表扫描选项（SMC_SM2_CT：点运算**无条件算完 + 掩码选择**，运算次数与域乘次数对 4096 标量恒定，见 bench_sct） |
| SM2 基点表 | `SMC_SM2_FIXED_TABLE`（OPT 时默认开，**CT 构建也开**）：t=5 → 32 项/2048B **只读**表（占 Flash，RAM 增量 0）＋代码约 2.9KB；keygen/sign 在该项上再快 2.64×/2.34×。表由源码生成，禁止手工编辑 |
| CT × 预计算表 | **二者不再互斥**：CT 构建对同一张表做**全表扫描 + cmov**，恒做加法后用掩码决定是否采纳；基点路径 257dbl/78add → **51/51 + 1347 域乘，4096 标量极差 0**，性能代价 **2.8× → 1.13×**（通用 1.18×）。判据必须用**同一输入集上的均值**（比最小值会虚报成 1.7×），且必须同时看**域乘次数**（只数点加/倍点调用次数会漏掉「提前返回跳过的工作量」，见经验 24） |
| 记录层 CTR 计数器 | 计数器块 = `iv_prefix(8)‖序号(6)‖块计数(2)`，**只有块计数自增**（`derive_ctr` + `sm4_ctr_xor_n`）；序号落在自增域内会造成跨记录密钥流复用（two-time pad）。测试逐块断言 C⊕P 唯一 |
| API 防误用 | 套件白名单（未知套件/模式不匹配 → `E_NOSUPPORT`）；服务端校验客户端提议==本端配置；**熵源 fail-closed**：`entropy=NULL` 且未显式 `allow_deterministic_rng` → `E_NORNG` |
| 对端公钥表 | `SMC_SM2_PEER_TABLE`（默认开）：**验签输入全为公开数据 ⇒ 无需常量时间**，故为对端公钥建 comb 表把 `tP` 变成固定基点乘 → verify **1.84×**；建表≈0.56 次验签（第 2 次起净赚）；调用方提供缓冲（构建 4KB / 缓存 2KB），与公钥绑定、错表 fail-closed |
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
docs/       01 路径规划 / 02 系统设计 / 03 固件安全规范 / 04 测试与性能对比 / 05 硬件采购 / 06 作品设计报告 / 07 分工与推进 / 08 硬件测试操作指南
scripts/    build.sh run_tests.sh demo_run.sh benchmark.sh size_report.sh power_model.py
            gen_fixed_base.sh（表生成/校验）stack_depth.py（最大调用路径栈深）
            thirdparty_bench.sh + compare_bench.py（GmSSL 对照，交错测量）
            ct_cost.sh + ct_cost.py（常量时间代价，交错测量）
            check_embedded.sh（板级预检：宿主仿真 + 语法 + 32K 链接证明）
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
- [x] **硬件到货（2026-10-13）**：现状剖析 + 推进计划写入 **docs/07**（任务卡 T0–T7：占板任务串行、不占板任务并行；
      唯一硬规则 = 谁提交谁保证 `run_tests.sh` 全绿）。
      基线复核：run_tests.sh 全绿（6 变体指纹 80c111c8… + DTLCP 36 项）。**三个硬约束**：
      ① 本机无 `arm-none-eabi-gcc` / `openocd` / `pyocd` / `make`（M0 阻塞项，须提供不依赖 make 的 build.sh）；
      ② `DTLCP_RECBUF=1600` → `dtlcp_hs.c` 握手函数单帧 `payload[1600]+packet[1600]+body[600]` ≈ **3.8KB 栈**
         （叠加记录上下文 3×1600B ≈ 4.7KB）→ 20KB RAM 上须先实测栈深，再定「降 RECBUF」或「握手复用 ctx 缓冲」；
      ③ 报告口径：docs/06 写「Cortex-M4 骨架」而实测将发生在 M3 → 决策点（建议补 F407 核心板 ¥25–60，
         顺带覆盖 TRNG 条款）
- [x] **官方复赛通知入库（2026-10-13）**：从竞赛平台 `chinacodes.com.cn` 下齐 —— 通知正文（`news/findNewsByTitle.do?id=256`）、
      官方附件包（POST `news/appendixdown.do`，字段 `title`/`appendix`/`appendix_store_name`；**服务器套了一层 zip**，
      内层才是 附件一=团队名单 PDF、附件二/三模板）、盖章版总通知 PDF、附件一 HTML（id=255，465 队）。
      **⏰ 提交窗口 2026-10-19（周一）9:00 — 10-22（周四）18:00**，队长账号提交，支撑材料 ≤4 GB，
      命名 `编号-2026密码竞赛复赛作品设计报告 / …相关支撑材料 / …审批及签字页`；本队编号 **CACR2026XTP27G**
      （已填入 docs/06 封面）。**匿名是硬门槛**：报告与支撑材料不得出现单位/领队/指导/队员信息，违者取消资格
      ——仓库已扫描无泄漏，`复赛通知附件/` 已在 .gitignore，**打包支撑材料时必须排除**（含名单与签字页）。
      docs/07 已据此改为**倒排期**：实测窗口 10-13→10-17，10-18 定稿缓冲；必保 T0/T1/T2/T3/T7，
      T4（W5500 真机）尽力，T5/T6 可缓
- [x] **创新性轮（2026-10-13）——两项密码学层面新结论**：
      ① **常量时间不必放弃预计算表**：CT 分支改为对静态 comb 表做全表扫描 + cmov，
      基点路径运算次数 257dbl/78add → **51/51**（4096 标量极差 0），性能代价 **2.8× → 1.06×**
      （`scripts/ct_cost.sh` 交错 5 轮、均值口径；通用路径 1.08×）
      ⚠️ **该轮的 1.06× 判据有缺陷**（只数点运算调用次数，看不到"提前返回跳过的域乘"），
      真实代价见 2026-10-14 安全修复轮 ③：**1.13× / 1.18×**
      ② **验签变基点腿降级为固定基点**：依据"验签输入全为公开数据 ⇒ 无需常量时间"的不对称性，
      实现 `sm2_peer_table_build`/`sm2_verify_with_table` → verify **1.84×**，建表≈0.56 次验签，
      每对端缓存 2KB；新增 12 项专项测试（含错表 fail-closed、紧凑表一致、表项在曲线上）
      ③ 配套工程：comb 参数上提到 `smc_config.h`（生成器改为只做一致性校验，防参数错配）；
      `bench_sct --sweep N` 大样本恒定性门禁（CI 以退出码断言）；`build.sh` 变体宏集中为唯一来源
      ④ 修正三处自身缺陷：sweep 漏初始化基点（曾扫成 k·(0,0)）、跨输入比最小值的口径陷阱、
      `sm2_keygen(rng=NULL)` 固定种子导致 ECDH 测试在自比
- [x] **安全修复轮（自审 2026-10-14）——三处密码学缺陷，均先探针复现再修**：
      ① **[严重] CTR 跨记录密钥流复用**：计数器块把 48 位记录序号放进了自增域
      （`sm4_ctr_xor` 自增低 8 字节），于是记录 s 的第 n 块与记录 s+n 的第 1 块共用密钥流
      → 探针用已知明文逐字节恢复出相邻记录（two-time pad）。修为 `iv_prefix(8)‖序号(6)‖块计数(2)`
      + 新增 `sm4_ctr_xor_n(inc_bytes)`（只自增块计数）；`test_dtlcp.c` 新增「密钥流块在会话内唯一」
      （已在旧代码上确认会 FAIL）。
      ② **[中] CBC 记录长度校验漏了 16 字节 IV**：`len < declared+13` 应为 `+13+16`，
      少 16 字节的输入会被接受并读到声明长度之外（越界读）。已修 + 新增截断负向用例。
      ③ **[中] CT 构建并非恒定工作量**：点运算对「无穷远」提前返回，被跳过的域乘仍被计数器
      计成一笔点加 → 实测域乘次数 156–3432（通用，k=1 最省）、327–1347（基点）随秘密变化，
      而计数钩子报「恒定」。修为**无条件算完 + 掩码选择**，并把「空操作占位点」坐标改为异于
      无穷远表示（否则「累加器仍为无穷远 + 本窗口为空」会触发 h=0 退化分支）；
      **代价回到真实值：基点 1.06× → 1.13×、通用 1.08× → 1.18×**（旧数字是用错判据量的）。
      ④ 配套：新增**域乘次数**门禁（`SMC_OP_MUL` 钩子 + `bench_sct --sweep` 断言极差 0，
      边界值扩到 n−1/稀疏/高位全1/**k=1**/k=2）；新增配置防误用（套件白名单 `E_NOSUPPORT`、
      模式↔套件匹配、**熵源 fail-closed**）；DTLCP 测试 36 → **42 项**；体积/栈深数字同步
      （full 30,080 B、trim-min 21,000 B、CT 栈 3,936 B）。
      **残留（如实披露）**：`fe_reduce_fold` 的试商次数仍随操作数值在 3–6 间变化
      （折叠轮数对全尺寸输入恒为 5），按完整标量乘折算残余工作量差异 2.4–3.6%；
      彻底消除需固定步数约减（= 域运算改造，与「缩小对 GmSSL 差距」是同一件事）。

- [ ] **待做（硬件线）**：M0 工具链+DAPLink 烧录链路 → M1 size/栈深/32KB 链接证据 → M2 INA226 功耗实测
      → M3 W5500 + DTLCP 真机三模式 → M4 文档定稿；**域运算改造（第一优先）与硬件无关，可并行开工**
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
19. **"提前 return"的分支要检查初始化**：`bench_sct --sweep` 曾在 `GPT` 初始化之前 return，
    导致扫描的是 `k·(0,0)`（非曲线点，退化分支频繁触发）。**症状识别**：同一函数在两次运行中
    "各自恒定但数值不同"（257 vs 263），优先怀疑输入/状态而非算法本身。
20. **跨输入比最小值是陷阱**：变时间路径的耗时随输入变化，最小值来自最省的标量；
    常量时间路径对任何输入都做满。用最小值算 CT 代价得 1.72× 的假结论（真值 1.13×）。
    判据要用**同一输入集上的均值**；最小值只用于同一输入集内过滤噪声。
21. **`build.sh` 通配链接的前提**：`obj/*_<suf>.o` 要求同变体所有 `.o` 用**同一组宏**编译，
    否则 `.c` 的 `#if` 与所链接对象不一致——轻则打印内核名不符，重则链接期缺符号
    （`SMC_SM2_PEER_TABLE` 即因此暴露该缺陷；已把变体宏集中为 `M_REF/M_OPT/...` 变量）。
22. **`sm2_keygen(d,pub,NULL)` 是确定性的**（固定 DRBG 种子）：连续两次调用返回**同一密钥对**。
    测试要"两个不同参与方"时必须 `trand(d,32) + sm2_pub_from_priv(pub,d)`——否则 ECDH 一致性
    测试等于自比，"错表 fail-closed"测试也会因"错表其实是对表"而失效（本项目两处都踩过）。
23. **对外部基线的公平性**：GmSSL 的 `sm2_do_sign` 每次签名向**系统 CSPRNG** 取 k
    （Windows 上约 455k cycles / 137µs），本库用 HMAC-SM3 派生 nonce。直接比 sign 会高估本库
    （1.97× vs 扣除后 1.52×）——熵源开销必须单列成行，让读者可扣除
24. **"恒定"必须用工作量而不是调用次数来证明**：`SMC_OP_ADD()` 这类钩子加在函数入口，
    函数内部若对"操作数/结果为无穷远"提前返回，被跳过的域乘**仍会被计成一笔点加**——
    计数恒定、工作量却随秘密变化（实测 CT 构建下域乘 156–3432，k=1 最省）。
    判据要同时看**域乘次数**（`sm2_op_mul`）。修法是"无条件算完 + cmov 选择结果"，
    不要以"提前返回很快所以无所谓"放过它。
25. **占位点的坐标不能等于无穷远的规范表示**：CT 路径给"本窗口为空"用的占位点若取 (1,1,1)
    （与 `sm2_jac_set_inf` 相同），则"累加器仍是无穷远 + 本窗口为空"时两点重合，
    触发 `h = 0` 的 `P = ±Q` 退化分支 → 工作量又随"前导空窗口个数"变化（k=1 差 21×）。
    占位点取 (2,2,1) 即让该分支不可达（结果本来就被掩码丢弃）。
26. **流密码（CTR）的"唯一性域"必须整体落在自增域之外**：本项目把 48 位记录序号放在计数器块
    的低 64 位，而 `sm4_ctr_xor` 又自增这 8 字节 → 记录 s 的第 n 块与记录 s+n 的第 1 块共用
    密钥流（two-time pad，已知明文可逐字节恢复相邻记录）。正确布局
    `iv_prefix(8)‖seq(6)‖块计数(2)`，只自增块计数。**测试要逐块比对 C⊕P 的唯一性**——
    "能正确往返"永远发现不了这个（自洽 ≠ 安全）。
27. **长度校验要把"不可见"的段算进去**：CBC 记录 = 头(13)+IV(16)+密文(len)，
    只按 `头+密文` 校验会漏 16 字节 → 输入少 16 字节仍被接受并越界读。
    凡是记录里"不体现在明文长度上"的段（IV / MAC / padding）都要单独核对长度下限。
28. **API 防误用优先于文档约定**：`entropy=NULL` 静默退化为确定性密钥、未知套件被当作 EtM
    处理——这类"默认不安全"必须做成 fail-closed（拒绝握手 + 显式开关），
    否则文档写"生产必须接 TRNG"也拦不住一次忘传参数。
