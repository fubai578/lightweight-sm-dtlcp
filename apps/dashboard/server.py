#!/usr/bin/env python3
"""Local dashboard shell. Device integration is intentionally disabled."""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from urllib.parse import urlsplit

STATIC = Path(__file__).resolve().parent / "static"


class Experiment:
    """Empty observation contract. Gateway integration is intentionally deferred."""
    def snapshot(self, after=0):
        return dict(run_id=None, cursor=0, status="idle", config={}, metrics={},
                    handshake=None, error="", resume_available=False, events=[])

    def start(self, data):
        raise RuntimeError("当前为平台准备阶段，尚未启用网关接入；连接设置可保存，页面保持空态。")

    def stop(self):
        pass


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
