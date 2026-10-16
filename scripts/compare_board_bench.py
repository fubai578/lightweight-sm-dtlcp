#!/usr/bin/env python3
"""Compare board_bench.py JSON files and emit a Markdown REF-relative table."""

import argparse
import json
from pathlib import Path


OPS = (
    "sm3_1KB", "sm3_4KB", "hmac_sm3_256B", "sm4_ecb_block",
    "sm4_cbc_1KB", "sm4_ctr_1KB", "sm2_keygen", "sm2_sign",
    "sm2_verify", "sm2_ecdh",
)


def main():
    parser = argparse.ArgumentParser(description="比较多档 F103 板测 JSON")
    parser.add_argument("ref", type=Path)
    parser.add_argument("variants", nargs="+", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    inputs = [args.ref] + args.variants
    data = [json.loads(path.read_text(encoding="utf-8")) for path in inputs]
    names = [path.stem.removeprefix("board_").upper() for path in inputs]
    ref = data[0]["cycles"]

    lines = []
    lines.append("| operation | " + " | ".join(names) +
                 " | " + " | ".join(name + "/REF" for name in names[1:]) + " |")
    lines.append("|---|" + "---:|" * (len(names) * 2 - 1))
    for op in OPS:
        values = [item["cycles"][op] for item in data]
        ratios = [ref[op] / value if value else 0.0 for value in values[1:]]
        lines.append("| %s | %s | %s |" % (
            op,
            " | ".join("%d" % value for value in values),
            " | ".join("%.2fx" % ratio for ratio in ratios),
        ))
    output = "\n".join(lines) + "\n"
    print(output, end="")
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
