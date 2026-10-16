#!/usr/bin/env python3
"""
capture_board.py — 抓 STM32F103 板的串口自检输出（T1 用）

为什么需要这个脚本，而不是直接用串口助手：
  ① **DTR 自动复位**：多数 CP2102 转接板把 DTR 接到了 MCU 的 NRST，
     开串口时拉低 DTR 就能复位，**保证能抓到上电自检的完整输出**——
     固件跑完自检就停在 for(;;) 空循环，不复位可能整个窗口都错过。
  ② **等到 summary.fail 立刻收**（自检末尾那行），不用干等固定秒数。
  ③ **解出 RESULT 行**：直接给出可回填docs/09 §8 表格的键值对。

用法：
    python capture_board.py                 # 默认 COM3 / 115200，抓 30s
    python capture_board.py COM5            # 指定端口
    python capture_board.py COM3 9600 60# 指定端口/波特率/超时（秒）
    python capture_board.py --out board.log    # 同时存盘（推荐，证据要留档）

注意（2026-10-14 实测踩坑）：
  * CP2102 的 **TX→PA10、RX→PA9**，两根线必须交叉。接反时烧录正常但串口全程安静。
  * 端口号可能随插拔变化（COM3/COM5…），用设备管理器或
    `python -c "import serial.tools.list_ports as l;print([p.device for p in l.comports()])"`
    确认。**Windows 10/11 会给新设备分配新 COM 号，这是正常的。**
  * 需要 pip install pyserial（或 pyocd 已带 pyserial）。
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("需要 pyserial：pip install pyserial", file=sys.stderr)
    sys.exit(1)


def parse_args():
    ap = argparse.ArgumentParser(
        description="抓取 STM32F103 板串口自检输出",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("port", nargs="?", default="COM3", help="串口号（默认 COM3）")
    ap.add_argument("baud", nargs="?", type=int, default=115200, help="波特率（默认 115200）")
    ap.add_argument("timeout", nargs="?", type=float, default=30.0,
                    help="抓取时长上限，秒（默认 30）")
    ap.add_argument("--out", metavar="FILE", help="把原始串口输出写入该文件")
    ap.add_argument("--no-reset", action="store_true",
                    help="不拉 DTR（板子正在跑时用；默认会复位以便抓全窗口）")
    return ap.parse_args()


def main():
    a = parse_args()

    p = serial.Serial()
    p.port = a.port
    p.baudrate = a.baud
    p.timeout = 0.3
    try:
        # 先置 DTR/RTS 再 open：多数 CP2102 板 DTR 接 NRST，可触发复位。
        # 但串口句柄未打开时部分平台会抛异常，故 try/except 包裹。
        try:
            p.dtr = False
            p.rts = False
        except Exception:
            pass
        p.open()
    except Exception as e:
        print("打开 %s 失败：%s" % (a.port, e), file=sys.stderr)
        print("  · 确认设备在：python -c \"import serial.tools.list_ports as l;"
              "print([x.device for x in l.comports()])\"", file=sys.stderr)
        print("  · 确认 TX/RX 是否交叉（TX→PA10、RX→PA9）", file=sys.stderr)
        return 2

    try:
        if not a.no_reset:
            time.sleep(0.3)
            p.reset_input_buffer()
    except Exception:
        pass

    buf = bytearray()
    deadline = time.time() + a.timeout
    while time.time() < deadline:
        try:
            d = p.read(4096)
        except Exception:
            continue
        if not d:
            continue
        buf.extend(d)
        # 自检末尾会打印这行，收到就可以收工，不必等满超时
        if b"summary.fail" in bytes(buf):
            break

    try:
        p.close()
    except Exception:
        pass

    text = bytes(buf).decode("utf-8", "replace")

    if a.out:
        try:
            with open(a.out, "wb") as fh:
                fh.write(bytes(buf))
            print("[saved] %s (%d bytes)" % (a.out, len(buf)))
        except OSError as e:
            print("[warn] 存盘失败：%s" % e, file=sys.stderr)

    if not buf:
        print("(30s 内无数据 —— 优先查 TX/RX 是否交叉，其次查波特率)")
        return 1

    print(text)
    print("-" * 60)

    # 抽出 RESULT 行，便于直接回填 docs/09 §8 表格
    results = [ln.split("=", 1) for ln in text.splitlines()
               if ln.startswith("RESULT ")]
    if results:
        print("解析到的 RESULT 行（共 %d 条）：" % len(results))
        width = max(len(k) for k, _ in results)
        for k, v in results:
            print("  %-*s = %s" % (width, k.strip(), v.strip()))
    else:
        print("未找到 RESULT 行 —— 固件可能没跑到自检，或串口接线有问题。")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
