# 国密传输实验台

本地观测平台，显示实际 C 网关事件；没有数据时保持空态，不生成网页演示数据。
目前已打通 **STM32F103C8T6 + W5500 终端 → UDP/DTLCP → 电脑 C 网关 → 网页**。

## 启动与运行

1. 在项目根目录运行 `python apps/dashboard/server.py`，打开 <http://127.0.0.1:8765>。
2. 选择 FULL、UDP 4433、12 条。启动网关后复位开发板；或者执行：

   ```powershell
   python scripts/run_board_dashboard.py
   ```

   该脚本启动网关、通过 ST-Link 复位现有固件、等待 12 条真实收发并读取板端完成状态。
   不烧录、不擦除。要求 pyocd、pyelftools 和已构建的对应 ELF。
3. 页面实时显示事件。完成后可导出 JSON。重复运行时再次启动网关并复位终端。

网页服务只需 Python 标准库。服务保持启动后才可运行实验；Ctrl+C 停止。
`powershell -ExecutionPolicy Bypass -File scripts/start_dashboard.ps1` 为便捷启动入口。

## 当前固件与限制

- 支持 FULL 与 RESUME；PSK、0-RTT 尚未适配该客户端固件，选择后明确拒绝启动。
- FULL 成功后，运行 `python scripts/run_board_dashboard.py --mode resume` 恢复同一会话。
  不复位开发板：脚本通过 SWD 公开命令邮箱触发恢复，密钥缓存留在板端 RAM。
  网关缓存位于进程专属临时目录，仅作实验；不会从 HTTP 导出，正常退出删除。
  开发板复位、断电，或平台重启后必须重新 FULL。失败或停止会使平台缓存不可用。
- 恢复前重建 W5500 UDP socket，清除上一连接遗留探测包及记录，避免混入下一握手。
- 页面握手时间为首个接收数据报到握手成功；原总耗时仍在 elapsed_us 字段保留，
  不包含进首包口径的网关就绪等待、板端初始化不参与该时间比较。
- 电脑以太网 `169.254.33.63/16`，板端 `169.254.33.64/16`，UDP 4433。
- 终端固件发 12 条 `TELEMETRY:F103:status=online` 在线状态上报。
  **这是开发板真实发送的在线状态，不包含虚构的温湿度字段。**
- 网关为每次成功接收分配接收序号；它不是来自板端的业务序号。
- 网关 ACK 发送成功不代表终端收到；独立运行脚本通过 SWD 完成状态进一步确认板端收到 ACK。
  SWD 确认保存在导出证据文件，不伪装成网页网关观测事件。
- 页面保留最近 2,000 条事件，表格显示筛选后的最新 300 条；支持专注展示、全屏、搜索和暂停。
- 协议握手耗时含等待终端与初始化的时间，不是算法微基准；未采集内部各阶段耗时。
- 不加载历史 MCU 性能、算法加速比或功耗数据；无真实测量的指标继续显示 `—`。
- 已接通网关本地篡改/重放测试；首次真实记录到达后可点击对应按钮。
  正常收发结束后 C 网关额外保留 60 秒测试窗口，届时仍可测试；超时后按钮禁用。
  重新启动下一轮会关闭上一轮测试进程并继续已有恢复逻辑。
- 终端使用现有固定种子演示 PKI，仅用于实验。

## 构建

```powershell
python scripts/build_gateway.py --cc "<可用 GCC 的绝对路径>"
```

本机 Anaconda 自带 GCC 5.3 编译浮点格式化时发生内部崩溃。本轮使用已安装 Octave 的 GCC 15.2 成功构建；
运行完整 `scripts/run_tests.sh` 时将对应编译器目录置于 PATH 首位。

ARM 固件仍通过 Git Bash 的 `bash embedded/cortex-m3/build.sh w5500_dtlcp` 构建。
烧录命令为 `python -m pyocd flash -M under-reset -f 100k -t stm32f103rc -e chip embedded/cortex-m3/smc_m3_w5500_dtlcp.elf`。
烧录会覆盖 Flash，执行前应另存现有固件；本轮原固件备份位于忽略入库的 `bin/board-before-dashboard.bin`。

## 数据与接口

C 网关通过可选 `--events` 在标准输出写入 `@dtlcp ` JSON；原命令行行为保留。
Python 只消费结构化事件，不解析私钥、PSK 或会话缓存，不代替协议解密。
接口 `/api/state` 支持游标，`/api/export` 返回实际本轮记录；`/api/start` 启动网关，`/api/stop` 停止。
时间戳为本机接收事件的时间，显示上海时区。网页仅绑定本机，校验 Host / Origin。

## 检查

```powershell
python -m unittest discover -s apps/dashboard -p "test_*.py"
node --check apps/dashboard/static/app.js
```

## 预置身份与启动时间

F103 客户端默认使用 `embedded/cortex-m3/demo_node_identity.h` 中预生成的公开测试身份。
终端不再每次启动都生成 CA/网关/终端密钥并签发三份证书；只将终端证书、私钥和 CA 证书装入连接配置。
FULL 的对端验证、临时 ECDH 与握手完整性检查仍执行。固定测试私钥已公开，不能作为生产密钥。
该身份沿用原 demo seed，仍不具有每设备独立性。生产须安全配置每设备身份，CA 签发工作留在设备外。

生成一致性检查：`python scripts/gen_demo_identity.py --cc "<host GCC>" --check`。
`BOARD_IDENTITY_FLAGS=-DBOARD_DYNAMIC_IDENTITY=1` 可构建旧启动路径用于 A/B。
启动计时由 TIM2 以 1 kHz 记录，起点在 `smc_port_init` 之后，不代表完整上电延迟；
结果由运行脚本写入 `board_startup_ms`，不混入网页的网关握手时间。
字段依次为 magic、身份模式（0=动态 / 1=预置）、身份准备开始、结束、握手前、握手后、首 ACK 后。
计数器 16 bit，65.536 秒回绕；脚本附计时有效标志，无效或溢出数据不可用于性能比较。

## 安全拒绝演示

推荐自动录制流程：`python scripts/security_demo.py`。脚本运行真实 FULL，在前几条遥测之间先后发出篡改和重放测试请求，
验证实际返回码，并检查后续正常消息与 ACK 继续，以及板端最终完成态。
手动录制可在 `python scripts/run_board_dashboard.py` 运行期间或完成后 60 秒内点击网页按钮。
没有真实接收记录时、平台离线或测试待返回时，按钮禁用；请求本身不增加通过计数。

测试范围是**网关本地接收记录重验证**，不是经网线发送的外部攻击：

- 捕获开发板真实 UDP 应用记录（密文只存 C 网关内存，不输出给网页）。
- 篡改：翻转已捕获记录最后一个 MAC 字节的一位，提交到真实 `dtlcp_rec_unprotect`，要求返回 -5。
- 重放：将同一已接受记录原样再次提交到该函数，要求返回 -6。
- 两种测试都比较真实接收状态测试前后是否完全一致；返回码不符或状态被改动时测试失败，并停止网关。
- 测试不生成遥测、不发送应用 ACK、不将副本作为业务处理，线上 UDP 字节统计不增加。
- 页面计数来自 C 的 `security_result` 事件；展示拒绝原因、返回码、时间和结果。
- 控制接口 `/api/security-test` 只接受 tamper/replay，使用固定临时文件路径进行原子命令交接。
  不支持用户指定载荷、文件或命令，不经 HTTP 暴露记录密文与密钥。

若要证明外部网络攻击路径，还需另做 UDP 中继或攻击测试，不能用本演示替代互通/网络攻防报告。
