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
| SM2 内核 | 域算术：REF=移位减模约减，OPT=Montgomery CIOS（32 位 limb×8）；标量乘：REF=二进制倍点法，OPT=4bit 固定窗口混合加，另提供常量时间全表扫描选项（SMC_SM2_CT） |
| SM2 裁剪 | SIG（默认开）/ ENC（默认关）/ KE 仅保留 ECDH（DTLCP 用），SM2 加密、SM2KEP 可裁剪；config.h 生成裁剪尺寸对比表 |
| DTLCP 握手模式 | FULL（ECDHE+SM2 签名认证，2 航班 1.5-RTT）、RESUME（会话恢复 1-RTT）、PSK（预共享密钥，可 0-RTT 早期数据） |
| 记录层保护 | 标准套件 ECDHE_SM4_CBC_SM3（MAC-then-encrypt）与轻量套件 *_SM4_CTR_SM3（Encrypt-then-MAC），均 HMAC-SM3 |
| 密钥派生 | PRF = TLS 风格 P_hash(HMAC-SM3)；master_secret = PRF(PMS,"master secret",cr‖sr) |
| 交叉验证 | OpenSSL 3.2 CLI（本机有）验证 SM3/SM4/SM2；标准向量由调研给出；REF vs OPT 差分测试 |
| 评测口径 | host(x86-64) 实测 cycles/ns；Flash/RAM 用 `size` + `-fstack-usage`；功耗用能量模型（明确标注为估算） |
| 构建 | 无 make → `scripts/build.sh`（bash + gcc）；Cortex-M 提供 arm-none-eabi Makefile（本机无交叉编译器，交付源码+构建脚本+移植说明） |

## 三、目录结构

```
crypto/     SM2/SM3/SM4/HMAC/DRBG 可移植嵌入式 C 实现（无动态内存、无 libc 依赖）
dtlcp/      DTLCP 记录层 + 握手 + 密钥调度 + UDP 传输抽象
apps/       bench 基准 / demo sensor+gateway / 测试入口
embedded/   平台移植层(port) + Cortex-M4 裸机工程骨架
tests/      KAT + 差分 + 协议仿真测试
docs/       01 实现路径规划 / 02 系统设计 / 03 固件安全改造规范 / 04 测试与性能对比报告
scripts/    build.sh / run_tests.sh / demo_run.sh / benchmark.sh / size_report.sh
```

## 四、进度日志（2026-09-25）

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
