#!/usr/bin/env python3
"""Build the actual C gateway without rebuilding unrelated benchmark tools."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="C compiler path; defaults to GCC on PATH")
    args = parser.parse_args()
    cc = args.cc or shutil.which("gcc")
    if not cc:
        parser.error("GCC not found; specify --cc")
    sources = [ROOT/"apps/demo/dtlcp_gateway.c", ROOT/"apps/demo/udp_io.c"]
    sources += sorted((ROOT/"dtlcp/src").glob("*.c"))
    sources += sorted((ROOT/"crypto/src").glob("*.c"))
    output = ROOT/"bin"/("dtlcp_gateway.exe" if os.name=="nt" else "dtlcp_gateway")
    output.parent.mkdir(exist_ok=True)
    command = [cc,"-std=c99","-O2","-Wall","-Wextra","-D__USE_MINGW_ANSI_STDIO=1"]
    command += ["-I"+str(ROOT/p) for p in ("crypto/include","crypto/src","dtlcp/src","dtlcp/include")]
    command += [str(p) for p in sources]+["-o",str(output)]
    if os.name=="nt":
        command.append("-lws2_32")
    env = os.environ.copy()
    env["PATH"] = str(Path(cc).resolve().parent)+os.pathsep+env.get("PATH","")
    subprocess.run(command,cwd=ROOT,env=env,check=True)
    print(output)


if __name__=="__main__":
    main()
