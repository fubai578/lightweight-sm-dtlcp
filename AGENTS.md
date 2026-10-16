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
   （本项目实际出现过：框架图 pptx 的 lastModifiedBy = 昵称；飞书导出的报告 docx 的
   creator = `YYT`、lastModifiedBy = `tiger`、`Company` = `微软中国`）。
   **统一用 `python scripts/sanitize_office_meta.py --check <文件>` 检查、去掉 `--check` 即清理**；
   凡在 Office/WPS 里另存过就要复跑一次（模板 `附件二` 本身就带 `YYT`/`SeanXiao`，会继承到成品）。
3. **打包支撑材料时必须排除的文件**（匿名硬门槛）：`复赛通知附件/`（名单与签字页）、
   `.git/`（提交者身份）、**本文件 `AGENTS.md`（含 GitHub 账号）**。报告正文（docs/06）
   已扫描无泄漏；二进制文档要查文档属性（见上一条）。
4. **覆盖任何"不是本轮产出"的文件前先确认它是否被手工编辑过**（本项目踩过：
   重跑 `build_framework_pptx.py` 覆盖了手工调整过的框架图 → 从 WPS 备份中心恢复；
   备份路径 `%APPDATA%\kingsoft\office6\backup\<原名>.<hash>.<时间戳>.pptx`）。
5. **报告数字必须与仓库证据同源**：报告（飞书正式稿 / `docs/06`）里的每个比值都得能在
   `bin/bench_results.md`、`bin/ct_cost.md`、`bin/size_report.md` 里找到出处，且**只认同一份
   输出内的比值**。发现不一致时先重跑基准，再同步文档——不要凭记忆改数字。

## 〇之二、飞书文档通道（团队报告在飞书上，用 CLI 读，不走 MCP）

- 可用工具：workbuddy 装的官方命令行连接器 `@larksuite/cli`（`lark-cli` v1.0.97），路径
  `C:\Users\HaoLing\.workbuddy\binaries\node\cli-connector-packages\node_modules\@larksuite\cli\bin\lark-cli.exe`
  （应用 `cli_aa1befb20af85cbc`，配置在 `~/.lark-cli/`，app secret 存 keychain）。
  **ZCode 侧的 feishu MCP（lark-mcp）是另一套授权，用不上时可以不管。**
- 读文档三步：
  1. `lark-cli wiki +node-get --node-token <wiki_token>` → 拿 `obj_token` / `obj_type`
  2. 文件类（`obj_type=file`，如 .docx）：`lark-cli drive +download --wiki-token <wiki_token> --output <本地路径>`
     云文档类：`lark-cli docs +fetch ...`
  3. 读正文：`python -c "zipfile + re"` 解 `word/document.xml`（注意 `<w:t[^>]*>` 会误吞
     `<w:tblPr>`，正则要用 `<w:t(?:\s[^>]*)?>`）
- 身份过期时（`lark-cli auth status` 显示 user `missing`）：`lark-cli auth login --no-wait --json --recommend`
  拿授权链接 → 用户在浏览器/飞书扫码完成 → `lark-cli auth login --device-code <code>` 收尾。
  bot 身份即使 ready 也读不了**未授权给应用**的 wiki（错误码 131006），别拿它当兜底。
- 团队报告节点：wiki token `GYs8wAwS8i5bWBkteMmcs9IknMg`（标题「作品设计报告.docx」，
  比 `docs/06` 更长的正式稿，361 段/4 图）。**它是团队在飞书上编辑的正式稿，
  不要用 CLI 直接覆盖写入**；改动建议以"待替换清单"形式给对方。

## 〇之三、写作工具：academic-humanizer skill（2026-10-16 装入）

- 来源 `github.com/AIScientists-Dev/academic-humanizer`（MIT，v0.3.3），已装到用户级
  `~/.zcode/skills/academic-humanizer/`（10 个文件全部按仓库元数据哈希校验通过）。
- 用途：`docs/06` 与飞书正式稿的**润色**——修掉通用 AI 腔（模板化开场、"值得注意"式强调、
  堆砌形容词/连接词、超长从句）与**核对论断是否有数据支撑**（每个比值都要落到
  `bin/*.md` 出处，见硬性约定第 5 条）；不用于规避 AI 检测，也**不许改动任何数字/引用**。
- 本地适配：原 frontmatter 缺 ZCode 认的 `when_to_use`，已补中英双语触发词（其余内容保持原样，
  便于日后直接对上游 diff）。**注意**：该 skill 的 AI 腔清单是英文中心（delve/em-dash 等），
  中文写作的对应清单（"随着…的快速发展""综上所述"）只在触发词里列了名字，未展开成规则。

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
            sanitize_office_meta.py（清/查 docx·pptx·xlsx 的作者与单位字段——匿名硬门槛）
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
      ② 当时 `DTLCP_RECBUF=1600` 且握手局部数组重复，20KB RAM 风险尚未裁决；
         该问题已于 2026-10-16 通过 RECBUF=1024、ctx 缓冲复用和自动 RAM 门禁解除；
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

- [x] **F103C8T6 硬件线 T0/T2 落地（2026-10-13 夜）—— 交叉编译与 32KB 适配证明已完成**：
      硬件确认：**W5500 + 立创地阔星 STM32F103C8T6 + ST-Link V2 + CP2102**（无 INA226、无 F407）。
      ① **装好 GNU Arm 14.2.Rel1**（`winget install Arm.GnuArmEmbeddedToolchain`；
         路径含空格且带版本号 `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\14.2 rel1\bin`）
         → **M0 阻塞项解除**
      ② **新增 `embedded/cortex-m3/` 完整工程**：`stm32f1xx_min.h`（F1 最小寄存器定义，自包含，
         **不依赖 CMSIS**）、`port/smc_port_f1.c`（F1 移植层）、`startup_stm32f103c8.c`、
         `stm32f103c8.ld` + `stm32f103c8_32k.ld`、`build.sh`（不依赖 make）；
         `board_selftest.c`/`main.c` 由 M4 版复制并改熵源措辞
      ③ **熵源（F103 无 RNG 的处理）**：`smc_port_f1.c` 用 **ADC1_IN0(PA0) 噪声 + LFSR 白化**，
         自检四关（无超时/无重复/比特∈[35%,65%]/8窗口唯一）。**如实标注边界**：
         非 TRNG、熵质量远低于硬件 TRNG，生产必须外接 TRNG；自检项措辞用「熵源自检」而**非**
         「TRNG 健康自检」（后者是虚假陈述，评审追问会很难答）
      ④ **★ 六档尺寸实测（链接器值，非估算，可直接回填 docs/04 §5）**：
         `ref` 27696B/9576B ｜ `opt` **33520B/13680B** ｜ `opt ct` 33024B ｜ `tiny` 33152B/9576B
         ｜ `min` 30112B ｜ **`32k min` 29552B/9576B ← 链接成功，"适配<32KB Flash"已证明**
      ⑤ **★ 反直觉但重要的实测结论：REF 比 OPT 更小（27.7KB vs 33.5KB）**
         —— comb 表 2048B 占 Flash + SM4 T-table 4128B 占 bss。**"优化"=用 Flash/RAM 换时间**，
         报告必须如实说明，否则评审会问"为什么优化后代码变大"
      ⑥ **M3 上 DWT/CYCCNT 可用** → 板上 cycles 实测**不受换 M3 影响**（原报告口径担心解除）
      ⑦ `scripts/stack_depth.py` 加 `--objdump` 参数 + ARM Thumb `bl` 识别 + memo 污染修复；
         M3 栈深实测峰值 **`scalarmult_add_naf` 单帧 1712 B**（`-O2` 内联了 `sm2_fp_mul`，
         故调用路径退化为单帧；**任务栈建议 ≥3KB，勿直接套用 x86 的 3280B**）
      ⑧ 修复三个**原有缺陷**：`cortex-m4/build.sh` 的 `.._.._x.o` 隐藏文件通配坑、
         两个 `startup_*.c` 的向量表缺 `__attribute__((used))`（会链接出 0 字节 ELF）、
         `stack_depth.py` 的 memo 残缺缓存
      ⑨ 新增 **`docs/09-F103上板操作指南.md`**（接线/构建/烧录/尺寸表/熵源边界/
         W5500 接线/无 INA226 时的功耗替代方案/12 条踩坑记录/与 M4 差异总表）
      ⑩ host 侧 `run_tests.sh` 回归**仍全绿**；此阶段尚未上板的项目已在后续条目完成。
- [ ] **待做（硬件线）**：T3 功耗实测（无 INA226，见 docs/09 §9）
      → T4 W5500 + DTLCP 真机三模式（T5 RAM 门禁已解除）→ T7 文档定稿
- [x] **域运算改造完成（2026-10-16）**：`fe_reduce_solinas` 按 limb 15→8 逆序单趟折叠，
      再做两次固定 carry 回折 + 一次条件减，删除多轮 17-limb 复制和试商收尾。
      host `fe_mul` 273→94 cycles；F103 相对 REF 的 keygen/sign/verify/ECDH 达
      **5.44×/4.06×/3.24×/2.15×**（旧 ECDH 0.83×已反转）；对 GmSSL 域乘 **1.95×**、
      变基点乘 **2.04×**。新增 512 组随机域乘差分，7 变体新指纹
      `64a5385d…b49eaf` 一致；两档 DTLCP 各 42 项全绿，CT 4096 标量工作量极差 0。

- [x] **SM3 MCU 内核 + DTLCP RAM 裁决（2026-10-16）**：新增 `SMC_SM3_KERNEL=4`，
      删除 W'[64] 数组、即时计算 W[j]^W[j+4]、拆分 0–15/16–63 轮；F103 SM3
      1KB/4KB/HMAC 相对 REF **1.18×/1.18×/1.17×**，压缩栈 **608→368 B**。
      算法差分扩为 **7 变体**且指纹一致。DTLCP 发送/接收复用 ctx 的 rxbuf/txbuf，
      early tx/rx 复用主记录状态，ctx **5920→5248 B**；最长跨模块栈路径 **4872 B**，
      MCU RAM 下界 **14224/20480 B**、余量 **6256 B**。新增 `check_m3_dtlcp.sh`，
      默认与 MCU(RECBUF=1024) 两档协议均 42 项全绿。

- [x] **W5500 T4（2026-10-16 真机完成）**：依据 WIZnet 官方 ioLibrary 实现 F103 SPI1
      单 socket UDP 驱动与 `dtlcp_io` send/recv 回调；PA5/6/7 + PB10 CS，2 KiB TX/RX，
      支持服务端从 UDP 头学习对端。探针支持 PB10/PA4 双 SCS 自动探测及 SWD 原始寄存器结果。
      修正接线后 VERSIONR=0x04、PHY Link=1/100 Mbps、36 B UDP 回显一致。
      PC Public 防火墙限制下采用 PC sensor 主动连接板端 gateway，真机完成 FULL
      2274.97 ms/1198 B/5 SM2、RESUME 58.04 ms/428 B/0 SM2、PSK 74.77 ms/412 B、
      PSK 0-RTT 92.65 ms/472 B，四模式均完成加密遥测 ACK。

- [x] **F103 真机基线打通（2026-10-16）**：定位并修复 PA9/PA10 错写 `GPIOA_CRL`
      （应为 `CRH`）、RX 误配输出、USART3 APB1 时钟和 PC13 CRH 四处外设错误；诊断固件
      COM3 连续收到 `11111111`。新增 `smc_board_results` SWD 结果块与
      `scripts/board_bench.py` / `compare_board_bench.py`，不再依赖长串口日志人工抄数。
      REF/OPT/TINY/MCU 四档真机均 `failures=0`、熵源自检 rc=0（ones=51%）。板测结论：
      SM4 FAST **1.48–1.56×**；SM3 FAST **0.85×**。后续新增 SM3 MCU 内核：删除 W' 数组、
      即时异或并拆分轮函数，F103 hash/HMAC **1.17–1.18×**、栈 608→368 B；`mcu` 档改为
      SM3 MCU + SM4 FAST + SM2 OPT。域归约优化后带测量块的
      `32k min` = 29,720 B，仍满足 32 KiB 链接约束。

## 五、关键调试经验（后续修改代码必读）

1. **T-table 旋转方向**：SM4 te1=ROTL(te0,24)、te3=ROTL(te0,8)（S[x]<<16 = ROTL(S[x]<<24,24)）
2. **Solinas 约减**：高位必须按 15→8 逆序处理并立即清零；新项只落到更低 limb，才能单趟收完。
   两次 carry 回折的边界已用 200k 随机 512-bit 输入 + 65,572 个 0/全1 limb 组合验证。
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
29. **ARM 交叉编译的五个坑（F103C8T6 移植实测，2026-10-13）**：
    ① `-specs=nano.specs` **必须同时出现在链接命令**：只在 `$CFLAGS`（编译阶段）给，
       链接仍拉完整版 `libc.a` → Flash 50KB vs 33.5KB（**差 16.5KB**），32K 直接溢出。
       症状隐蔽：编译日志完全正常，只有 `size` 能看出来。
    ② `printf("%llu")` 会让 **newlib-nano 回退到完整格式化器**（含 dtoa + `__aeabi_dmul` 等
       软浮点例程，+18KB Flash）。nano 只支持 `%l`（32 位）。解法：手工拆两半打印。
    ③ **向量表必须加 `__attribute__((used))`**：`static` 且无人引用的 section 会被直接丢弃，
       再被 `--gc-sections` 连带回收所有代码 → **链接"成功"但 ELF = 0 字节**。
       F4 版原本被 `-Werror` 的 unused-variable 警告挡住才没暴露。
    ④ newlib 会引用 `_sbrk/_lseek/_fstat/_isatty/_exit/_kill/_getpid`，裸机**必须自备桩**。
       本仓库把 `_sbrk` 设为**返回失败**——因为零动态内存是赛题要求之一，
       让 malloc 立刻失败比悄悄分配 20KB RAM 里不存在的大块内存更安全。
    ⑤ Git Bash 下 `../../crypto/src/x.c` 若按原样生成 `.._.._x.o`，
       **以点开头会被当隐藏文件**，`*.o` 通配匹配不到 → 又一个 0 字节 ELF。
       修法：规范化路径前缀，且**显式收集 OBJS 列表**代替通配。
30. **`stack_depth.py` 的三个静默失效点**（都是"不报错但结果错"，极危险）：
    ① 正则只认 x86 的 `callq`，**ARM Thumb 是 `bl <sym>`** → 调用图全空、栈深退化为单帧。
    ② `objdump` 硬编码；交叉编译时宿主 PATH 里只有 x86 版，读不了 ARM ELF 且**不报错**。
       → 已加 `--objdump` 参数；且**空输出视为失败**而非"无调用边"。
    ③ `max_path` 的 `memo` 会缓存"递归保护未清空时算出的残缺值"，导致最长路径退化。
    ④ ARM 反汇编的符号带偏移后缀 `func+0x8`，不剥离就匹配不上 `.su` 里的纯函数名。
31. **`-O2` 内联对栈深口径的影响**：内联会把 `sm2_fp_mul` 等小函数帧**合并进调用者**，
    于是「最大调用路径」退化为**单个大帧**（M3 实测：`scalarmult_add_naf` 单帧 1712 B）。
    ⇒ **ARM 的栈深不能直接套用 x86 的非内联口径**（x86 为 3280 B），
    报告里必须注明这个差别，并说明"未开启 `--no-inline` 重测"这一事实。
32. **优化 ≠ 体积更小（MCU 上尤其如此）**：F103 实测 REF 内核 27 696 B < OPT 33 520 B。
    因为 comb 表 2 048 B 占 Flash、SM4 T-table 4 128 B 占 bss，REF 是纯代码实现无表。
    ⇒ 正确表述是「**用 Flash/RAM 换时间**」；选型按 flash 紧张与否分档
    （紧→`ref`/`tiny`/`min`，求性能→`opt`）。这条必须写进报告，否则会被问"为什么优化后变大"。
