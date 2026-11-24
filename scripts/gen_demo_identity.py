#!/usr/bin/env python3
"""Generate/check the public fixed-seed test identity. No production secrets."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc",required=True)
    parser.add_argument("--check",action="store_true")
    args=parser.parse_args()
    cc=Path(args.cc).resolve()
    env=os.environ.copy()
    env["PATH"]=str(cc.parent)+os.pathsep+env.get("PATH","")
    with tempfile.TemporaryDirectory(prefix="demo-identity-") as tmp:
        exe=Path(tmp)/("generator.exe" if os.name=="nt" else "generator")
        command=[str(cc),"-O2","-std=c99","-Icrypto/include","-Icrypto/src","-Idtlcp/include","-Idtlcp/src",
                 "apps/gen/gen_demo_identity.c","dtlcp/src/dtlcp_cert.c"]
        command += [str(p.relative_to(ROOT)) for p in (ROOT/"crypto/src").glob("*.c")]
        subprocess.run(command+["-o",str(exe)],cwd=ROOT,env=env,check=True)
        generated=subprocess.check_output([str(exe)],env=env).replace(b"\r\n",b"\n")
    target=ROOT/"embedded/cortex-m3/demo_node_identity.h"
    if args.check:
        if target.read_bytes().replace(b"\r\n",b"\n")!=generated:
            raise RuntimeError("Demo identity differs from generator")
        print("Demo identity regenerated and signatures verified: PASS")
    else:
        target.write_bytes(generated)
        print("Generated public demo identity; never use in production")


if __name__=="__main__":
    main()
