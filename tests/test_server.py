"""Exercise the development server over real HTTP, without external dependencies."""

import argparse
import http.client
import importlib.util
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "serve.py"
SPEC = importlib.util.spec_from_file_location("dev_server", SCRIPT)
serve = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(serve)


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "build"
        self.root.mkdir()
        (self.root / "index.html").write_text("<h1>Melee port</h1>", encoding="utf-8")
        (self.root / "game.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00")
        self.secret = Path(self.temp.name) / "private.txt"
        self.secret.write_text("must not be served", encoding="utf-8")
        self.server = serve.create_server(self.root, port=0)
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.start()
        self.addCleanup(self.stop_server)

    def stop_server(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)
        self.assertFalse(self.thread.is_alive(), "HTTP server did not stop")

    def request(self, path, method="GET", body=None, headers=None):
        connection = http.client.HTTPConnection(*self.server.server_address, timeout=5)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            response = connection.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            connection.close()

    def assert_isolated(self, headers):
        self.assertEqual(headers["Cross-Origin-Opener-Policy"], "same-origin")
        self.assertEqual(headers["Cross-Origin-Embedder-Policy"], "require-corp")
        self.assertEqual(headers["Cache-Control"], "no-store")

    def test_serves_build_with_isolation_headers_on_loopback(self):
        self.assertEqual(self.server.server_address[0], "127.0.0.1")
        status, headers, body = self.request("/")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"<h1>Melee port</h1>")
        self.assert_isolated(headers)

    def test_wasm_mime_and_head(self):
        for method in ("GET", "HEAD"):
            with self.subTest(method=method):
                status, headers, body = self.request("/game.wasm?version=1", method)
                self.assertEqual(status, 200)
                self.assertEqual(headers["Content-type"], "application/wasm")
                self.assertEqual(headers["Content-Length"], "8")
                self.assertEqual(body, b"" if method == "HEAD" else b"\x00asm\x01\x00\x00\x00")
                self.assert_isolated(headers)

    def test_missing_file_has_isolation_and_no_cache_headers(self):
        status, headers, _ = self.request("/missing")
        self.assertEqual(status, 404)
        self.assert_isolated(headers)

    def test_rejects_literal_and_encoded_traversal(self):
        for path in ("/../private.txt", "/%2e%2e/private.txt", "/nested/%2e%2e/%2e%2e/private.txt", "/..%2fprivate.txt", "/..%5cprivate.txt"):
            with self.subTest(path=path):
                status, headers, body = self.request(path)
                self.assertEqual(status, 403)
                self.assertNotIn(b"must not be served", body)
                self.assert_isolated(headers)

    def test_rejects_file_and_directory_symlinks_outside_root(self):
        (self.root / "escape.txt").symlink_to(self.secret)
        (self.root / "escape-dir").symlink_to(self.secret.parent, target_is_directory=True)
        for path in ("/escape.txt", "/escape-dir/private.txt", "/escape-dir/"):
            with self.subTest(path=path):
                status, _, body = self.request(path)
                self.assertEqual(status, 403)
                self.assertNotIn(b"must not be served", body)

    def test_rejects_index_symlink_outside_root(self):
        (self.root / "index.html").unlink()
        (self.root / "index.html").symlink_to(self.secret)
        status, _, body = self.request("/")
        self.assertEqual(status, 403)
        self.assertNotIn(b"must not be served", body)

    def test_allows_symlink_to_build_asset(self):
        (self.root / "alias.wasm").symlink_to(self.root / "game.wasm")
        status, _, body = self.request("/alias.wasm")
        self.assertEqual(status, 200)
        self.assertEqual(body, b"\x00asm\x01\x00\x00\x00")

    def test_rejects_malformed_path(self):
        for path in ("/%00", "/%ff"):
            with self.subTest(path=path):
                status, headers, _ = self.request(path)
                self.assertEqual(status, 400)
                self.assert_isolated(headers)

    def test_evidence_is_opt_in_and_same_origin_bounded(self):
        path = "/__melee_evidence/retail-browser-report.json"
        data = b'{"complete":true}'
        origin = f"http://127.0.0.1:{self.server.server_port}"
        headers = {"Origin": origin, "Content-Type": "application/octet-stream"}
        self.assertEqual(self.request(path, "POST", data, headers)[0], 404)
        output = Path(self.temp.name) / "evidence"
        output.mkdir()
        self.server.evidence_directory = output
        for changes, expected in (({"Origin": "https://example.com"}, 403),
                                  ({"Host": "example.com"}, 403),
                                  ({"Content-Type": "text/plain"}, 415),
                                  ({"Content-Length": str(8 * 1024 * 1024 + 1)}, 413)):
            with self.subTest(changes=changes):
                self.assertEqual(self.request(path, "POST", data, {**headers, **changes})[0], expected)
        self.assertEqual(self.request("/__melee_evidence/../private.txt", "POST", data, headers)[0], 404)
        self.assertEqual(list(output.iterdir()), [])
        for _ in range(2):
            status, response_headers, body = self.request(path, "POST", data, headers)
            self.assertEqual(status, 201)
            self.assert_isolated(response_headers)
            result = json.loads(body)
            self.assertEqual(result["sha256"], hashlib.sha256(data).hexdigest())
            self.assertEqual(Path(result["path"]).read_bytes(), data)
        self.assertEqual(len(list(output.iterdir())), 1)
        self.assertEqual(self.request("/" + Path(result["path"]).name)[0], 404)
        timer_path = "/__melee_evidence/retail-timer.jsonl"
        self.assertEqual(self.request(timer_path, "POST", data,
                         {**headers, "Content-Length": str(8 * 1024 * 1024 + 1)})[0], 413)
        status, _, body = self.request(timer_path, "POST", data, headers)
        self.assertEqual(status, 201)
        timer = json.loads(body)
        self.assertEqual(Path(timer["path"]).read_bytes(), data)
        self.assertEqual(timer["sha256"], hashlib.sha256(data).hexdigest())

    def test_bounded_hitch_report_above_old_limit_is_preserved_exactly(self):
        output = Path(self.temp.name) / "hitch-evidence"
        output.mkdir()
        self.server.evidence_directory = output
        data = json.dumps({"diagnostic_capture": {"events": [
            {"id": i, "context": "x" * 2048} for i in range(64)]}}).encode()
        self.assertGreater(len(data), 65536)
        headers = {"Origin": f"http://127.0.0.1:{self.server.server_port}",
                   "Content-Type": "application/octet-stream"}
        status, _, body = self.request("/__melee_evidence/retail-browser-report.json",
                                       "POST", data, headers)
        self.assertEqual(status, 201)
        saved = json.loads(body)
        self.assertEqual(Path(saved['path']).read_bytes(), data)
        self.assertEqual(saved['sha256'], hashlib.sha256(data).hexdigest())


class ConfigurationTests(unittest.TestCase):
    def test_evidence_directory_cannot_be_served(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaisesRegex(ValueError, "outside"):
                serve.create_server(temporary, port=0, evidence_directory=Path(temporary) / "evidence")

    def test_requires_existing_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(FileNotFoundError):
                serve.create_server(Path(temporary) / "missing", port=0)
            file = Path(temporary) / "file"
            file.touch()
            with self.assertRaises(NotADirectoryError):
                serve.create_server(file, port=0)

    def test_rejects_invalid_ports(self):
        for port in (-1, 65536, 8787.0, "8787", True):
            with self.subTest(port=port), self.assertRaises(ValueError):
                serve.create_server(SCRIPT.parent, port=port)
        for value in ("0", "-1", "65536", "invalid"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                serve.cli_port(value)

    def test_cli_requires_directory(self):
        result = subprocess.run([sys.executable, str(SCRIPT)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("--directory", result.stderr)


if __name__ == "__main__":
    unittest.main()
