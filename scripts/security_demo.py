#!/usr/bin/env python3
"""Run a real F103 session and request local protocol rejection tests.

Requires running dashboard + ST-Link + current network firmware. Does not flash.
Tamper/replay are gateway-local injections of a real received record, not wire attacks.
"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
from urllib.request import Request,urlopen


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port",type=int,default=8765)
    parser.add_argument("--output",type=Path,default=Path("bin/security-demo.json"))
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    base="http://127.0.0.1:%d"%args.port

    def state():
        with urlopen(base+"/api/state",timeout=5) as response:
            return json.load(response)

    previous=state()["run_id"]
    board_output=args.output.with_name(args.output.stem+"-board.json").resolve()
    process=subprocess.Popen([sys.executable,str(root/"scripts/run_board_dashboard.py"),"--port",str(args.port),
                              "--output",str(board_output)],cwd=root)
    deadline=time.monotonic()+120
    try:
        def wait_for(predicate):
            while time.monotonic()<deadline:
                result=state()
                if result["run_id"]!=previous and predicate(result):return result
                if process.poll() is not None:raise RuntimeError("Board run exited before test finished")
                time.sleep(.05)
            raise RuntimeError("Security demo timed out")

        wait_for(lambda r:r["status"]=="connected" and r["security_available"])
        for action in ("tamper","replay"):
            request=Request(base+"/api/security-test",data=json.dumps({"test":action}).encode(),
                            headers={"Content-Type":"application/json"})
            with urlopen(request,timeout=5) as response:json.load(response)
            result=wait_for(lambda r:any(e["test"]==action for e in r["security_results"]))
            event=next(e for e in result["security_results"] if e["test"]==action)
            if not event["passed"]:raise RuntimeError("Protocol did not reject as expected")
        process.wait(timeout=max(1,deadline-time.monotonic()))
        if process.returncode:raise RuntimeError("Real board run failed")
        result=state()
        board=json.loads(board_output.read_text(encoding="utf-8"))
        last_test=max(e["id"] for e in result["security_results"])
        later=[e["message_id"] for e in result["events"] if e["type"]=="telemetry" and e["id"]>last_test]
        result["normal_messages_after_tests"]=later
        result["board_ack_confirmed"]=board["board_ack_confirmed"]
        result["board_swd_result"]=board["board_swd_result"]
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        if result["status"]!="complete" or not later or not board["board_ack_confirmed"]:
            raise RuntimeError("Normal communication did not continue after rejection tests")
        print("PASS: actual rc=-5/-6, receive state unchanged, later real telemetry+ACK completed.")
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


if __name__=="__main__":
    main()
