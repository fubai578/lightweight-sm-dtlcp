# 国密传输实验台

本地演示平台，当前阶段不连接真机、不启动网关、不生成或加载演示数据。统计显示 `—`，遥测与记录区保持空态。

## 启动

在项目根目录运行 `python apps/dashboard/server.py`，浏览器打开 <http://127.0.0.1:8765>。
也可运行 `powershell -ExecutionPolicy Bypass -File scripts/start_dashboard.ps1`。
Python 3.8+，仅使用标准库，无 npm 或 CDN 依赖。`--port 8766` 可修改网页端口。Ctrl+C 停止服务。

## 当前功能

- 开发板终端 → 国密网关 → 实时观测的布局与空态。
- FULL / RESUME / PSK / PSK 0-RTT 模式选择，IPv4、UDP 端口、条数设置。
- 设置仅保存在页面内存，刷新重置，保存不触发连接。
- 专注展示、全屏、事件筛选与搜索、暂停与恢复、录制指南。
- 本机服务在线/离线状态，桌面与移动端适配。
- 预留真实握手、遥测、ACK、字节统计与事件导出展示逻辑。

“启动网关”目前明确提示尚未启用接入，不执行 C 程序或网络连接。无记录时禁用导出。
`/api/state` 与 `/api/export` 始终返回空态，`/api/start` 返回 HTTP 409，`/api/stop` 为空操作。

## 后续接入约定

开发板作为终端、电脑作为网关，由 C 网关完成解密与验证后输出公开业务事件，本机服务转发给页面。
页面不读网关内存，不展示私钥、PSK 或会话密钥。当前不修改密码算法、协议实现与终端固件。

预留事件：ready（网关就绪）、handshake（握手完成）、wire（UDP 字节数）、telemetry（解保护后的业务内容）、
ack_sent（网关发送成功，不代表终端收到）、error / finished（失败或完成）。
时间显示上海时区，为本机事件接收时间；握手耗时不作为算法微基准。

服务仅绑定 `127.0.0.1`，检查 Host / Origin，不允许访问任意本地文件。

## 检查

```powershell
python -m unittest discover -s apps/dashboard -p "test_*.py"
node --check apps/dashboard/static/app.js
```
