"""Verify idle state, control validation and real event aggregation."""
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
        server.EXPERIMENT.session_dir.cleanup()

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

    def test_unsupported_mode_preserves_empty_state(self):
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/start", b'{"mode":"psk"}', {"Content-Type":"application/json"})
        self.assertEqual(caught.exception.code, 400)
        self.assertEqual(server.EXPERIMENT.snapshot()["events"], [])

    def test_resume_without_cache_rejected(self):
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/start", b'{"mode":"resume"}', {"Content-Type":"application/json"})
        self.assertEqual(caught.exception.code,400)
        self.assertFalse(server.EXPERIMENT.snapshot()["resume_available"])

    def test_security_rejects_invalid_action_or_missing_record(self):
        for body,code in ((b'{"test":"invalid"}',400),(b'{"test":"replay"}',409)):
            with self.assertRaises(HTTPError) as caught:
                self.request("/api/security-test",body,{"Content-Type":"application/json"})
            self.assertEqual(caught.exception.code,code)
        self.assertEqual(server.EXPERIMENT.snapshot()["security_results"],[])

    def test_security_request_is_not_a_result(self):
        from unittest.mock import Mock
        experiment=server.Experiment()
        try:
            experiment.proc=Mock()
            experiment.proc.poll.return_value=None
            experiment.status="complete"
            experiment.metrics["received"]=1
            experiment.security_test({"test":"tamper"})
            snapshot=experiment.snapshot()
            self.assertEqual(snapshot["security_results"],[])
            self.assertNotIn("tamper_rejected",snapshot["metrics"])
            self.assertIsNotNone(snapshot["security_pending"])
            with self.assertRaises(RuntimeError):experiment.security_test({"test":"replay"})
            experiment.publish(dict(type="security_result",test="tamper",request_id=1,code=-5,passed=True,state_unchanged=True))
            self.assertIsNone(experiment.snapshot()["security_pending"])
            self.assertEqual(experiment.snapshot()["metrics"]["tamper_rejected"],1)
        finally:experiment.session_dir.cleanup()

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

    def test_aggregation_uses_only_received_events(self):
        experiment = server.Experiment()
        self.assertEqual(experiment.snapshot()["metrics"], {})
        experiment.publish(dict(type="wire",direction="rx",bytes=70))
        experiment.publish(dict(type="telemetry",message_id=1,bytes=25,text="test"))
        experiment.publish(dict(type="ack_sent",message_id=1,bytes=15,text="ack"))
        snapshot=experiment.snapshot()
        experiment.session_dir.cleanup()
        self.assertEqual(snapshot["metrics"],dict(wire_rx=70,received=1,app_bytes=25,acks=1,app_sent=15))
        self.assertEqual(len(experiment.snapshot(after=2)["events"]),1)

    def test_non_json_control_request_rejected(self):
        with self.assertRaises(HTTPError) as caught:
            self.request("/api/start", b"mode=full", {"Content-Type":"application/x-www-form-urlencoded"})
        self.assertEqual(caught.exception.code, 415)


if __name__ == "__main__":
    unittest.main()
