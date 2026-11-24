#!/usr/bin/env python3
"""Reproduce paired old/new MCU SM3 checks or a same-image F103 benchmark.

--board explicitly flashes the attached F103. Restore its application afterwards.
Both kernels use identical flags. The baseline comes from a pinned Git revision.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "e0c782a"
NAMES = ("sm3", "sm3_init", "sm3_update", "sm3_final", "sm3_compress",
         "hmac_sm3", "hmac_sm3_init", "hmac_sm3_update", "hmac_sm3_final", "smc_sm3_kdf")
OPS = ("sm3_0B", "sm3_28B", "sm3_55B", "sm3_56B", "sm3_64B", "sm3_65B",
       "sm3_256B", "sm3_1KB", "sm3_4KB", "hmac_sm3_256B")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", required=True, help="GCC executable")
    parser.add_argument("--baseline-ref", default=BASELINE)
    parser.add_argument("--board", action="store_true", help="flash and benchmark the attached F103")
    parser.add_argument("--output", type=Path, default=ROOT/"bin/sm3-ab/reproduced")
    args = parser.parse_args()
    cc = Path(shutil.which(args.cc) or args.cc).resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline = subprocess.check_output(["git", "show", args.baseline_ref+":crypto/src/sm3.c"], cwd=ROOT)
    old_source = output/"sm3_before.c"
    old_source.write_bytes(baseline)
    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-fstack-usage", "-DSMC_SM3_KERNEL=4",
             "-ffunction-sections", "-fdata-sections", "-I"+str(ROOT/"crypto/include")]
    if args.board:
        flags += ["-mcpu=cortex-m3", "-mthumb", "-ffreestanding", "-DSM3_AB_BOARD"]
    rename = ["-D"+n+"=before_"+n for n in NAMES]
    units = [("before", old_source, rename), ("after", ROOT/"crypto/src/sm3.c", []),
             ("hmac_before", ROOT/"crypto/src/hmac_sm3.c", rename),
             ("hmac_after", ROOT/"crypto/src/hmac_sm3.c", []),
             ("main", ROOT/"apps/bench/bench_sm3_ab.c", [])]
    if args.board:
        units.append(("startup", ROOT/"embedded/cortex-m3/startup_stm32f103c8.c", []))
    env = os.environ.copy()
    env["PATH"] = str(cc.parent)+os.pathsep+env.get("PATH", "")

    def run(command, capture=False):
        return subprocess.run([str(c) for c in command], cwd=ROOT, env=env, check=True,
                              capture_output=capture, text=True, encoding="utf-8", errors="replace")

    objects = []
    for name, source, extra in units:
        obj = output/(name+".o")
        run([cc]+flags+extra+["-c", source, "-o", obj])
        objects.append(obj)
    executable = output/("ab.elf" if args.board else "ab.exe" if os.name=="nt" else "ab")
    link = [cc]+objects
    if args.board:
        link += ["-mcpu=cortex-m3", "-mthumb", "-specs=nano.specs", "-nostartfiles",
                 "-Wl,--gc-sections", "-T"+str(ROOT/"embedded/cortex-m3/stm32f103c8.ld")]
    run(link+["-o", executable])
    if not args.board:
        result = run([executable], capture=True).stdout
        (output/"correctness.log").write_text(result, encoding="utf-8")
        print(result.strip())
        return

    from board_bench import find_symbol
    from pyocd.core.helpers import ConnectHelper
    from pyocd.flash.file_programmer import FileProgrammer
    address = find_symbol(executable, "sm3_ab_results")
    session = ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc", frequency=100000,
              options={"connect_mode":"under-reset", "chip_erase":"chip"})
    if session is None:
        raise RuntimeError("No probe found")
    with session:
        target = session.board.target
        FileProgrammer(session).program(str(executable))
        target.reset()
        target.resume()
        deadline = time.monotonic()+90
        while time.monotonic()<deadline:
            time.sleep(.5)
            target.halt()
            values = target.read_memory_block32(address, 26)
            target.resume()
            if values[0]==0x534d3341 and values[1]==2:
                break
        else:
            raise RuntimeError("Board A/B timeout")
    if values[4] != len(OPS) or values[5] != 7 or any(v == 0 for v in values[6:26]):
        raise RuntimeError("Invalid board result layout or stopped cycle counter")
    size = cc.with_name(cc.name.replace("gcc", "size"))
    sizes = run([size, output/"before.o", output/"after.o", executable], capture=True).stdout
    (output/"size.log").write_text(sizes, encoding="utf-8")
    frames = {}
    for variant in ("before", "after"):
        for line in (output/(variant+".su")).read_text(encoding="utf-8", errors="replace").splitlines():
            if "sm3_compress\t" in line:
                frames[variant] = int(line.split("\t")[1])
    result = dict(baseline_ref=args.baseline_ref, compiler=run([cc, "--version"], capture=True).stdout.splitlines()[0],
                  hardware="STM32F103C8T6 Cortex-M3, HSI 8MHz", flags=[f for f in flags if not f.startswith("-I")],
                  rounds=values[5], repeats_per_sample=8, method="alternating old/new order; min of seven batches",
                  failures=values[2], checks=values[3], compress_stack_frame_bytes=frames,
                  before_source_sha256=hashlib.sha256(baseline).hexdigest(),
                  after_source_sha256=hashlib.sha256((ROOT/"crypto/src/sm3.c").read_bytes()).hexdigest(),
                  firmware_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(), operations=[])
    for i, name in enumerate(OPS):
        old, new = values[6+i], values[16+i]
        result["operations"].append(dict(operation=name, before_cycles=old, after_cycles=new,
                                         speedup=old/new, cycle_reduction_pct=100*(old-new)/old))
    (output/"board.json").write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print("Board A/B: %d checks, %d failures" % (values[3], values[2]))
    for row in result["operations"]:
        print("%s: %d -> %d cycles (%.3fx)" % (row["operation"], row["before_cycles"], row["after_cycles"], row["speedup"]))
    if values[2]:
        raise RuntimeError("Board correctness failed")


if __name__=="__main__":
    main()
