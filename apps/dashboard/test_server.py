"""Verify the platform stays empty and cannot initiate device connections."""
import importlib.util
import json
from pathlib import Path
import threading
import unittest
from urllib.error import HTTPError
from urllib.request import Request, urlopen

spec = importlib.util.spec_from_file_location("dashboard_server", Path(__file__).with_name("server.py"))
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class DashboardTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = server.ThreadingHTTPServer(("127.0.0.1", 0), server.Handler)
        cls.base = "http://127.0.0.1:%d" % cls.httpd.server_port
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join()

    def request(self, path, data=None, headers=None):
        return urlopen(Request(self.base + path, data=data, headers=headers or {}), timeout=3)

    def test_initial_state_and_export_are_empty(self):
        for route in ("/api/state", "/api/export"):
            with self.request(route) as response:
                data = json.load(response)
            self.assertEqual(data["status"], "idle")
            self.assertEqual(data["events"], [])
            self.assertEqual(data["metrics"], {})
            self.assertIsNone(data["handshake"])

    def test_start_disabled_and_preserves_empty_state(self):
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/start", b'{"mode":"full"}', {"Content-Type":"application/json"})
        self.assertEqual(caught.exception.code, 409)
        self.assertEqual(server.EXPERIMENT.snapshot()["events"], [])

    def test_cross_origin_and_rebound_host_rejected(self):
        for headers in ({"Origin":"https://example.invalid"}, {"Host":"example.invalid"}):
            with self.assertRaises(HTTPError) as caught:
                self.request("/api/state", headers=headers)
            self.assertEqual(caught.exception.code, 403)

    def test_no_local_file_access(self):
        for route in ("/server.py", "/../../AGENTS.md"):
            with self.assertRaises(HTTPError) as caught:
                self.request(route)
            self.assertEqual(caught.exception.code, 404)

    def test_static_and_security_policy(self):
        for route in ("/", "/app.js", "/style.css"):
            with self.request(route) as response:
                self.assertIn("frame-ancestors 'none'", response.headers["Content-Security-Policy"])
                self.assertGreater(len(response.read()), 100)

    def test_non_json_control_request_rejected(self):
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/start", b"mode=full", {"Content-Type":"application/x-www-form-urlencoded"})
        self.assertEqual(caught.exception.code, 415)


if __name__ == "__main__":
    unittest.main()
