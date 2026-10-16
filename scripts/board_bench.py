#!/usr/bin/env python3
"""Flash an F103 benchmark ELF and read its completed result block over SWD."""

import argparse
import json
import struct
import sys
import time
from pathlib import Path

try:
    from elftools.elf.elffile import ELFFile
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
except ImportError as exc:
    print("需要 pyocd 和 pyelftools：python -m pip install pyocd pyelftools", file=sys.stderr)
    raise SystemExit(2) from exc


MAGIC = 0x534D4331
VERSION = 1
STATE_DONE = 2
OPS = (
    "sm3_1KB",
    "sm3_4KB",
    "hmac_sm3_256B",
    "sm4_ecb_block",
    "sm4_cbc_1KB",
    "sm4_ctr_1KB",
    "sm2_keygen",
    "sm2_sign",
    "sm2_verify",
    "sm2_ecdh",
)
RESULT_FORMAT = "<8I2i10Q"
RESULT_SIZE = struct.calcsize(RESULT_FORMAT)


def find_symbol(path, name):
    with path.open("rb") as stream:
        elf = ELFFile(stream)
        symtab = elf.get_section_by_name(".symtab")
        if symtab is None:
            raise RuntimeError("ELF 不含符号表")
        matches = symtab.get_symbol_by_name(name)
        if not matches:
            raise RuntimeError("ELF 中找不到符号 %s" % name)
        return int(matches[0]["st_value"])


def decode(raw):
    values = struct.unpack(RESULT_FORMAT, raw)
    magic, version, state, sm3, sm4, sm2, ct, failures, entropy_rc, entropy_pct = values[:10]
    return {
        "magic": "0x%08x" % magic,
        "version": version,
        "state": state,
        "kernels": {"sm3": sm3, "sm4": sm4, "sm2": sm2, "ct": ct},
        "failures": failures,
        "entropy": {"rc": entropy_rc, "ones_pct": entropy_pct},
        "cycles": dict(zip(OPS, values[10:])),
        "_magic_value": magic,
    }


def print_markdown(result):
    kernels = result["kernels"]
    print("kernels: SM3=%d SM4=%d SM2=%d CT=%d" %
          (kernels["sm3"], kernels["sm4"], kernels["sm2"], kernels["ct"]))
    print("failures: %d; entropy: rc=%d, ones=%d%%" %
          (result["failures"], result["entropy"]["rc"], result["entropy"]["ones_pct"]))
    print("\n| operation | cycles/op |")
    print("|---|---:|")
    for name in OPS:
        print("| %s | %d |" % (name, result["cycles"][name]))


def main():
    parser = argparse.ArgumentParser(description="烧录并读取 F103 板上基准结果")
    parser.add_argument("elf", type=Path)
    parser.add_argument("--target", default="stm32f103rc")
    parser.add_argument("--frequency", type=int, default=100_000)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--no-flash", action="store_true", help="只读取当前板上结果")
    parser.add_argument("--json", type=Path, help="同时保存 JSON")
    args = parser.parse_args()

    if not args.elf.is_file():
        parser.error("ELF 不存在：%s" % args.elf)
    address = find_symbol(args.elf, "smc_board_results")

    session = ConnectHelper.session_with_chosen_probe(
        target_override=args.target,
        frequency=args.frequency,
        options={"connect_mode": "under-reset", "chip_erase": "chip"},
    )
    if session is None:
        print("未找到调试器", file=sys.stderr)
        return 2

    with session:
        target = session.board.target
        if not args.no_flash:
            FileProgrammer(session).program(str(args.elf))
            target.reset()
        target.resume()

        deadline = time.monotonic() + args.timeout
        result = None
        while time.monotonic() < deadline:
            target.halt()
            raw = bytes(target.read_memory_block8(address, RESULT_SIZE))
            target.resume()
            result = decode(raw)
            if result["_magic_value"] == MAGIC and result["state"] == STATE_DONE:
                break
            time.sleep(0.5)
        else:
            state = None if result is None else result["state"]
            print("等待板测完成超时（state=%s）" % state, file=sys.stderr)
            return 1

    if result["version"] != VERSION:
        print("结果版本不匹配：%s" % result["version"], file=sys.stderr)
        return 1
    del result["_magic_value"]
    print_markdown(result)
    if args.json:
        args.json.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print("\n已保存：%s" % args.json)
    return 0 if result["failures"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
