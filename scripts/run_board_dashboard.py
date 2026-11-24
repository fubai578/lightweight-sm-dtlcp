#!/usr/bin/env python3
"""Start a real FULL experiment, reset the attached F103, and save evidence.

Requires the dashboard server and the 12-message W5500 client firmware.
Does not flash or erase the board. No telemetry is synthesized by this script.
"""
import argparse
import json
from pathlib import Path
import time
from urllib.request import Request, urlopen


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",type=int,default=8765)
    parser.add_argument("--output",type=Path,default=Path("bin/live-board-dashboard.json"))
    args = parser.parse_args()
    from pyocd.core.helpers import ConnectHelper
    from board_bench import find_symbol
    root = Path(__file__).resolve().parents[1]
    elf = root/"embedded/cortex-m3/smc_m3_w5500_dtlcp.elf"
    address = find_symbol(elf,"w5500_dtlcp_results")
    base = "http://127.0.0.1:%d" % args.port

    def post(path,data):
        request = Request(base+path,data=json.dumps(data).encode(),headers={"Content-Type":"application/json"})
        with urlopen(request,timeout=10) as response:
            return json.load(response)

    def state():
        with urlopen(base+"/api/state",timeout=5) as response:
            return json.load(response)

    # Fail before starting a gateway if no probe can be selected.
    session = ConnectHelper.session_with_chosen_probe(target_override="stm32f103rc",frequency=100000,options={"connect_mode":"attach"})
    if session is None:
        raise RuntimeError("No debug probe found")
    with session:
        post("/api/start",dict(mode="full",count=12,udp_port=4433))
        try:
            deadline = time.monotonic()+10
            while state()["status"]=="starting":
                if time.monotonic()>deadline:
                    raise RuntimeError("Gateway did not become ready")
                time.sleep(.1)
            if state()["status"]!="waiting":
                raise RuntimeError("Gateway failed before board reset")
            target = session.board.target
            target.reset()
            target.resume()
            deadline = time.monotonic()+120
            while True:
                result = state()
                print(result["status"],result["metrics"],flush=True)
                if result["status"] in ("complete","error","stopped"):
                    break
                if time.monotonic()>deadline:
                    raise RuntimeError("Board experiment timed out")
                time.sleep(1)
            # The board finishes its final pacing delay after the gateway sends ACK.
            board_deadline = time.monotonic()+10
            while True:
                target.halt()
                board = target.read_memory_block32(address,10)
                target.resume()
                if board[1]==3 or time.monotonic()>board_deadline:
                    break
                time.sleep(.5)
            result["board_swd_result"] = board
            result["board_ack_confirmed"] = board[0]==0x44544c31 and board[1]==3 and board[8]==1
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
            if result["status"]!="complete" or not result["board_ack_confirmed"]:
                raise RuntimeError("Experiment incomplete; inspect saved evidence")
            print("PASS: 12 real board messages, 12 gateway ACK sends; board completion confirmed over SWD.")
        except Exception:
            post("/api/stop",{})
            raise


if __name__=="__main__":
    main()
