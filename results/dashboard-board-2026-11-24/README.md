# 真机网页观测闭环（2026-11-24）

真实链路：STM32F103C8T6 + W5500 → UDP/DTLCP FULL → 本机 C 网关 → 网页。
网关收到 12 条板端在线状态上报，成功发送 12 条 ACK；板端 SWD 结果 state=3、application_success=1。

本轮仅上报 `TELEMETRY:F103:status=online`，没有虚构温湿度、主机模拟终端或历史性能数据。
`full-board-events.json` 为实际事件与汇总；`environment.json` 包含固件摘要和板端结果。

注意口径：握手耗时含网关启动后等待开发板复位与初始化的时间，不作为算法或板端性能基准。
消息序号是网关成功接收计数。ACK 事件只表示网关发送成功；独立 SWD 完成态进一步确认板端收到回执。
网页不从文件自动载入此快照。原始 Flash 备份未纳入支撑数据，保留于本地 bin 目录。

复现：先运行 `python apps/dashboard/server.py`，然后 `python scripts/run_board_dashboard.py`。
该复现命令会复位已连接的板子，不重新烧录；需要相同固件与网络配置。
