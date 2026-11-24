#!/usr/bin/env python3
"""Flash alternating dynamic/preloaded identities and record real F103 runs.

Requires dashboard server, Git Bash and ARM GCC on PATH. Overwrites board Flash.
Finishes with the preloaded application installed; no private data is exported.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bash",default="bash")
    parser.add_argument("--rounds",type=int,default=3)
    parser.add_argument("--output",type=Path,default=ROOT/"bin/startup-ab")
    args=parser.parse_args()
    if not 1<=args.rounds<=10:
        parser.error("rounds must be 1..10")
    out=args.output.resolve()
    out.mkdir(parents=True,exist_ok=True)
    target=ROOT/"embedded/cortex-m3/smc_m3_w5500_dtlcp.elf"

    def run(command,env=None):
        subprocess.run([str(x) for x in command],cwd=ROOT,env=env,check=True)

    for variant in ("before","after"):
        env=os.environ.copy()
        env["BOARD_IDENTITY_FLAGS"]="-DBOARD_DYNAMIC_IDENTITY=1" if variant=="before" else ""
        run([args.bash,"embedded/cortex-m3/build.sh","w5500_dtlcp"],env)
        shutil.copy2(target,out/(variant+".elf"))
    for turn in range(args.rounds):
        for variant in ("before","after"):
            shutil.copy2(out/(variant+".elf"),target)
            run([sys.executable,"-m","pyocd","flash","-M","under-reset","-f","100k",
                 "-t","stm32f103rc","-e","chip",target])
            run([sys.executable,"scripts/run_board_dashboard.py","--output",out/(variant+"-"+str(turn)+".json")])
    print("Finished with preloaded identity firmware; raw public results:",out)


if __name__=="__main__":
    main()
