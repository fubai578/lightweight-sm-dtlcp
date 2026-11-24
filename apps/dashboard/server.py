#!/usr/bin/env python3
"""Local dashboard shell. Device integration is intentionally disabled."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import subprocess
import threading
import time
from datetime import datetime, timezone
from collections import deque
import uuid
from pathlib import Path
from urllib.parse import urlsplit

STATIC = Path(__file__).resolve().parent / "static"


class Experiment:
    """Consume only actual gateway events; never generate telemetry."""
    def __init__(self):
        self.lock = threading.RLock()
        self.events = deque(maxlen=2000)
        self.seq = 0
        self.run_id = None
        self.status = "idle"
        self.config = {}
        self.metrics = {}
        self.handshake = None
        self.error = ""
        self.proc = None
        self.worker = None
        self.cancelled = False
        self.started = None
        self.ended = None

    def publish(self, event):
        with self.lock:
            self.seq += 1
            event.update(id=self.seq, timestamp=datetime.now(timezone.utc).isoformat(timespec="milliseconds"))
            self.events.append(event)
            kind = event.get("type")
            if kind == "wire":
                key = "wire_"+event["direction"]
                self.metrics[key] = self.metrics.get(key,0)+event["bytes"]
            elif kind == "ready":
                self.status = "waiting"
            elif kind == "handshake":
                self.handshake = dict(event)
                self.status = "connected"
            elif kind == "telemetry":
                self.metrics["received"] = self.metrics.get("received",0)+1
                self.metrics["app_bytes"] = self.metrics.get("app_bytes",0)+event["bytes"]
            elif kind == "ack_sent":
                self.metrics["acks"] = self.metrics.get("acks",0)+1
                self.metrics["app_sent"] = self.metrics.get("app_sent",0)+event["bytes"]
            elif kind == "error":
                self.error = event.get("text","gateway error")

    def snapshot(self, after=0):
        with self.lock:
            return dict(run_id=self.run_id,cursor=self.seq,status=self.status,
                        config=dict(self.config),metrics=dict(self.metrics),handshake=self.handshake,
                        error=self.error,resume_available=False,events=[dict(e) for e in self.events if e["id"]>after],
                        started=self.started,ended=self.ended,source="board" if self.run_id else None,
                        payload_origin="firmware_status_report" if self.run_id else None)

    def start(self, data):
        mode = data.get("mode","full")
        count = data.get("count",12)
        port = data.get("udp_port",4433)
        if mode != "full":
            raise ValueError("当前板端固件仅支持 FULL；其他模式待适配，不会自动替代。")
        if type(count) is not int or not 1<=count<=100 or type(port) is not int or not 1024<=port<=65535:
            raise ValueError("端口或条数无效")
        if data.get("peer","") not in ("","169.254.33.64"):
            raise ValueError("当前固件终端地址为 169.254.33.64")
        if port != 4433 or count != 12:
            raise ValueError("当前板端固件固定 UDP 4433、12 条遥测，请使用对应设置。")
        root = Path(__file__).resolve().parents[2]
        exe = root/"bin"/("dtlcp_gateway.exe" if os.name=="nt" else "dtlcp_gateway")
        if not exe.is_file():
            raise ValueError("请先构建带事件输出的 C 网关")
        with self.lock:
            if self.worker and self.worker.is_alive():
                raise RuntimeError("网关正在运行，请先停止")
            self.events.clear()
            self.metrics = {}
            self.handshake = None
            self.error = ""
            self.run_id = uuid.uuid4().hex[:12]
            self.config = dict(mode=mode,count=count,udp_port=port,peer="169.254.33.64")
            self.status = "starting"
            self.started = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
            self.ended = None
            self.cancelled = False
            env = os.environ.copy()
            if os.name == "nt":
                env["PATH"] = str(Path(exe).parent)+os.pathsep+env.get("PATH","")
            self.proc = subprocess.Popen([str(exe),"--events","--mode",mode,"--port",str(port),"--msgs",str(count),"--peer","169.254.33.64"],
                cwd=str(root),env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
            self.worker = threading.Thread(target=self.consume,daemon=True)
            self.worker.start()

    def consume(self):
        proc = self.proc
        timer = threading.Timer(150,proc.terminate)
        timer.daemon = True
        timer.start()
        try:
            for raw in iter(proc.stdout.readline,b""):
                line = raw.decode("utf-8",errors="replace").strip()
                if line.startswith("@dtlcp "):
                    event = json.loads(line[7:])
                    if event.get("role")=="gateway":
                        self.publish(event)
            rc = proc.wait()
            with self.lock:
                self.ended = datetime.now(timezone.utc).isoformat(timespec="milliseconds")
                if self.cancelled:
                    self.status = "stopped"
                elif rc == 0 and self.metrics.get("received")==self.config["count"] and self.metrics.get("acks")==self.config["count"]:
                    self.status = "complete"
                else:
                    self.status = "error"
                    self.publish(dict(type="error",role="platform",text=self.error or "网关退出或接收超时，请检查终端固件及网络。",code=rc))
        except Exception as exc:
            with self.lock:
                self.status = "error"
                self.error = str(exc)
            if proc.poll() is None:
                proc.terminate()
        finally:
            timer.cancel()
            proc.stdout.close()

    def stop(self):
        with self.lock:
            if self.proc and self.proc.poll() is None:
                self.cancelled = True
                self.proc.terminate()
        if self.worker:
            self.worker.join(timeout=5)


EXPERIMENT = Experiment()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def reply(self, status, body, mime="application/json; charset=utf-8"):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self'; script-src 'self'; connect-src 'self'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    def local_request(self):
        expected = "127.0.0.1:" + str(self.server.server_port)
        if self.headers.get("Host") not in (expected, "localhost:" + str(self.server.server_port)):
            self.reply(403, {"error": "仅允许本机访问"})
            return False
        origin = self.headers.get("Origin")
        if origin and origin not in ("http://" + expected, "http://localhost:" + str(self.server.server_port)):
            self.reply(403, {"error": "请求来源不匹配"})
            return False
        return True

    def do_GET(self):
        if not self.local_request():
            return
        parsed = urlsplit(self.path)
        if parsed.path == "/api/state":
            from urllib.parse import parse_qs
            try:
                after = int(parse_qs(parsed.query).get("after", ["0"])[0])
            except ValueError:
                self.reply(400, {"error": "无效游标"})
                return
            self.reply(200, EXPERIMENT.snapshot(after))
        elif parsed.path == "/api/export":
            self.reply(200, EXPERIMENT.snapshot())
        else:
            files = {"/": ("index.html", "text/html"), "/app.js": ("app.js", "text/javascript"),
                     "/style.css": ("style.css", "text/css")}
            if parsed.path not in files:
                self.reply(404, {"error": "页面不存在"})
                return
            name, mime = files[parsed.path]
            self.reply(200, (STATIC / name).read_bytes(), mime + "; charset=utf-8")

    def do_POST(self):
        if not self.local_request():
            return
        if self.headers.get_content_type() != "application/json":
            self.reply(415, {"error": "需要 JSON 请求"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= 4096:
                raise ValueError("请求长度无效")
            data = json.loads(self.rfile.read(size))
            if not isinstance(data, dict):
                raise ValueError("需要 JSON 对象")
            if self.path == "/api/start":
                EXPERIMENT.start(data)
            elif self.path == "/api/stop":
                EXPERIMENT.stop()
            else:
                self.reply(404, {"error": "接口不存在"})
                return
            self.reply(200, {"ok": True})
        except RuntimeError as exc:
            self.reply(409, {"error": str(exc)})
        except (ValueError, TypeError, AttributeError) as exc:
            self.reply(400, {"error": str(exc)})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    httpd = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print("DTLCP 演示平台：http://127.0.0.1:%d" % httpd.server_port, flush=True)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        EXPERIMENT.stop()
        httpd.server_close()


if __name__ == "__main__":
    main()
