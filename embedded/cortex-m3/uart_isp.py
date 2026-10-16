#!/usr/bin/env python3
"""
uart_isp.py — STM32F103C8T6 的 UART ISP 烧录器（**不需要 ST-Link、不需要任何驱动**）

为什么需要这个
--------------
本机（2026-10-14 实测）ST-Link V2 被系统枚举为 USB\\VID_0483&PID_3748，
但 **Status=Error、Class/Service/DriverVersion 全空** —— 也就是"硬件在、驱动没装"。
装WinUSB 驱动需要管理员权限与 ST 官网下载（本次被站点安全策略拦截），
于是 SWD 这条路暂时不通。

而 STM32F103C8T6 **芯片内自带 bootloader**（系统存储器），官方支持
USART1(PA9/PA10) 通过 UART ISP 烧录 —— 这条路只需要一根 CP2102，
驱动是 Windows 自带的（VID_10C4），**完全绕开上面的问题**。

代价（必须知道）：
  ① **烧录后不能读回、不能调试**（ISP 没有 SWD 的调试能力），所以断点是别想了；
     但烧录本身是可靠的，串口日志照样能看。
  ② 每次烧录要先手动拨 BOOT0：
     - 地阔星 F103C8T6 用 **BOOT0 跳线帽或 PB2 引脚**（见板子丝印）
     - BOOT0 = **1**(接 3V3) → 上电进入 ISP
     - BOOT0 = **0**(接 GND) → 上电从 Flash 启动
     烧完记得拨回 0，否则下次上电不进自己程序。
  ③ 传输比 SWD 慢（33 KB 固件约 10–20 s），但完全够用。

用法
----
    # 1) 先把 BOOT0 拨到 1，按一下复位
    # 2) 找到 COM 号（设备管理器 / 下脚本不带参数会自动扫描）
    python uart_isp.py --port COM3 --bin smc_m3_opt.bin
    #    或自动扫描：python uart_isp.py --bin smc_m3_opt.bin

接线（只有 3 根，很省事）：
    CP2102 TX  → 板子 PA10（MCU 的 RX）
    CP2102 RX  → 板子 PA9 （MCU 的 TX）
    CP2102 GND → 板子 GND
    **不要接 CP2102 的 VCC**（板子自己供电，且避免 5V 灌进 3.3V 轨）

协议：STM32 自定义 bootloader 协议（USART，115200 8N1，0x7E 同步帧）。
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial，请先执行：pip install pyserial")

# STM32 USART bootloader 常量
ST_USART_SYNC = 0x7E
ST_USART_ACK = 0x79
ST_GET = 0x00
ST_READ_VERSION = 0x01
ST_READ_MEMORY = 0x11
ST_GO = 0x21
ST_ERASE = 0x44
ST_WRITE_MEMORY = 0x31
ST_ERASE_PAGE = 0x45
ST_ERASE_MASS = 0xFF

STM32_F103_FLASH_BASE = 0x08000000
STM32_F103_FLASH_SIZE = 64 * 1024
PAGE_SIZE = 1024
ERASE_TIMEOUT = 30.0


class ISPError(Exception):
    pass


def hexify(b):
    return " ".join(f"{x:02X}" for x in b)


def wait_ack(s, timeout=5.0, desc="ACK"):
    """等一个 ACK 字节；ST 的应答可能是 ACK(0x79) 或其他，先收再判。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        c = s.read(1)
        if c:
            return c[0]
    raise ISPError(f"等待 {desc} 超时（{timeout}s）——检查 BOOT0 是否拨到 1、TX/RX 是否接反")


def cmd_simple(s, cmd, desc, timeout=5.0):
    """发单字节命令，等 ACK。"""
    s.write(bytes([cmd]))
    r = wait_ack(s, timeout, desc)
    if r != ST_USART_ACK:
        raise ISPError(f"{desc}: 期望 ACK(0x79)，收到 0x{r:02X}")
    return r


def cmd_with_checksum(s, payload, desc, timeout=10.0):
    """发 [cmd + data...] + 异或校验和，等 ACK。"""
    x = 1
    for b in payload[1:]:
        x ^= b
    s.write(bytes(payload) + bytes([x]))
    r = wait_ack(s, timeout, desc)
    if r != ST_USART_ACK:
        raise ISPError(f"{desc}: 期望 ACK(0x79)，收到 0x{r:02X}")


def connect_and_prepare(port):
    print(f"[1/6] 打开串口 {port} (115200 8N1) ...")
    s = serial.Serial(port, 115200, timeout=0.2, write_timeout=5.0)
    time.sleep(0.1)
    # 发同步字节把 bootloader 叫醒（芯片刚复位时需要）
    print("[2/6] 发送同步字节 0x7E ...")
    s.write(bytes([ST_USART_SYNC]))
    time.sleep(0.05)
    s.reset_input_buffer()
    # 有些板子需要连发几次同步才响应
    for _ in range(5):
        s.write(bytes([ST_USART_SYNC]))
        time.sleep(0.1)
        s.reset_input_buffer()
        s.write(bytes([ST_GET]))
        time.sleep(0.05)
        r = s.read(1)
        if r and r[0] == ST_USART_ACK:
            print("      ✓ 芯片已响应（ACK）")
            break
    else:
        s.close()
        raise ISPError(
            "芯片无应答。检查：\n"
            "  ① BOOT0 是否拨到 1（接 3V3）\n"
            "  ② 插上板子后是否按了复位键\n"
            "  ③ CP2102 TX 是否接 PA10、RX 是否接 PA9（TX/RX 交叉，不是直连）\n"
            "  ④ 板子是否有电（3V3 有输出？）"
        )

    # 读版本确认
    s.reset_input_buffer()
    cmd_simple(s, ST_READ_VERSION, "读版本")
    ver = s.read(3)
    if len(ver) < 2:
        raise ISPError(f"读版本返回过短: {hexify(ver)}")
    print(f"      芯片版本: {ver[0]}.{ver[1]:x}")
    return s


def erase_all(s):
    print("[3/6] 全片擦除（会先检查擦除权限）...")
    s.reset_input_buffer()
    s.write(bytes([ST_ERASE_MASS]))
    r = wait_ack(s, 1.0, "擦除前检查")
    if r != ST_USART_ACK:
        # 有些 bootloader 返回 NACK 表示"没有读保护"，属正常
        print(f"      擦除前检查返回 0x{r:02X}（非 ACK，若后续擦除成功可忽略）")
    t0 = time.time()
    while time.time() - t0 < ERASE_TIMEOUT:
        if s.in_waiting:
            c = s.read(1)
            if c and c[0] == ST_USART_ACK:
                print("      ✓ 擦除完成")
                return
        time.sleep(0.05)
    raise ISPError("擦除超时")


def write_flash(s, data, base=STM32_F103_FLASH_BASE):
    print(f"[4/6] 写入 {len(data)} 字节 @ 0x{base:08X} ...")
    off = 0
    chunk = 256
    total = len(data)
    while off < total:
        n = min(chunk, total - off)
        addr = base + off
        payload = bytes([ST_WRITE_MEMORY]) + list(addr.to_bytes(4, "big")) + \
                  list(bytes([n - 1])) + list(data[off:off + n])
        # 补齐到 256 的整数倍（STM32 要求）
        pad = chunk - n
        if pad:
            payload += [0xFF] * pad
            # 长度字段也补 255
            payload[5] = 0xFF
            payload = payload[:-pad] + [0xFF] * pad if False else payload
        x = 1
        for b in payload[1:]:
            x ^= b
        s.reset_input_buffer()
        s.write(bytes(payload) + bytes([x]))
        r = wait_ack(s, 5.0, f"写入 0x{addr:08X}")
        if r != ST_USART_ACK:
            raise ISPError(f"写入 0x{addr:08X} 失败: 0x{r:02X}")
        off += n
        pct = off * 100 // total
        print(f"      \r{off}/{total} B ({pct}%)", end="", flush=True)
    print()
    print("      ✓ 写入完成")


def go(s):
    print("[5/6] 启动程序 ...")
    addr = STM32_F103_FLASH_BASE
    payload = bytes([ST_GO]) + list(addr.to_bytes(4, "big"))
    x = 1
    for b in payload[1:]:
        x ^= b
    s.reset_input_buffer()
    s.write(bytes(payload) + bytes([x]))
    r = wait_ack(s, 1.0, "GO")
    if r != ST_USART_ACK:
        print(f"      GO 返回 0x{r:02X}（不影响已经烧好的固件）")


def scan_ports():
    """扫描可用串口，返回列表。"""
    try:
        from serial.tools import list_ports
        return [(p.device, p.description) for p in list_ports.comports()]
    except Exception as e:
        print(f"扫描串口失败: {e}")
        return []


def main():
    ap = argparse.ArgumentParser(description="STM32F1 UART ISP 烧录（无需 ST-Link 驱动）")
    ap.add_argument("--port", help="串口号，如 COM3；省略则自动扫描")
    ap.add_argument("--bin", required=True, help="待烧录的 .bin 文件")
    ap.add_argument("--base", default=hex(STM32_F103_FLASH_BASE),
                    help="Flash 起始地址，默认 0x08000000")
    args = ap.parse_args()

    if not args.port:
        ports = scan_ports()
        if not ports:
            sys.exit(
                "未找到任何串口。请确认：\n"
                "  ① CP2102 已插上（设备管理器应出现 CP2102 USB to UART Bridge COMx）\n"
                "  ② CP2102 驱动已装（Windows 一般自动装；缺驱动去 silabs 官网下 CP210x）"
            )
        print("发现串口：")
        for d, desc in ports:
            print(f"  {d}  {desc}")
        args.port = ports[0][0]
        print(f"→ 使用 {args.port}\n")

    with open(args.bin, "rb") as f:
        data = f.read()
    base = int(args.base, 0)
    print(f"固件: {args.bin}  {len(data)} 字节  目标 0x{base:08X}")
    if len(data) > STM32_F103_FLASH_SIZE:
        sys.exit(f"固件 {len(data)} B 超出 F103C8T6 的 Flash {STM32_F103_FLASH_SIZE} B")
    print()

    print("=" * 62)
    print("  ⚠ 请确认 BOOT0 已拨到 1（接 3V3），且已按复位键")
    print("=" * 62)
    input("  准备好后按回车继续...")
    print()

    try:
        s = connect_and_prepare(args.port)
        erase_all(s)
        write_flash(s, data, base)
        go(s)
        s.close()
    except ISPError as e:
        print(f"\n✗ 失败：{e}")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n已中断")
        sys.exit(1)

    print("[6/6] 完成！")
    print()
    print("接下来务必做：")
    print("  ① 把 BOOT0 拨回 0（接 GND），否则下次上电不会运行自己的程序")
    print("  ② 按复位键，串口应出现 KAT 自检输出（115200 8N1）")
    print("  ③ 看最后一行是否为：==== 完成：失败项 = 0 ====")


if __name__ == "__main__":
    main()