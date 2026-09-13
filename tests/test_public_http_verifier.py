"""Real-HTTP regression tests for the public shell deployment verifier."""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import unittest
import urllib.parse

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from verify_public_http import verify  # noqa: E402


class _StaticHTTP:
    """Small controllable HTTP origin used instead of a mocked urllib layer."""

    def __init__(self, files: dict[str, bytes], *, canonical_redirects: bool = False,
                 overrides: dict[str, tuple[int, bytes]] | None = None,
                 missing_headers: set[str] | None = None):
        self.files = files
        self.canonical_redirects = canonical_redirects
        self.overrides = overrides or {}
        self.missing_headers = missing_headers or set()
        owner = self

        class Handler(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"

            def do_GET(self):  # noqa: N802 - BaseHTTPRequestHandler API
                owner._get(self)

            def log_message(self, _format, *_args):
                return

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    @property
    def url(self) -> str:
        return f"http://127.0.0.1:{self.server.server_port}"

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

    def _get(self, request: BaseHTTPRequestHandler) -> None:
        path = urllib.parse.urlsplit(request.path).path
        if self.canonical_redirects and path.endswith(".html") and path in self.files:
            destination = "/" if path == "/index.html" else path.removesuffix(".html")
            request.send_response(301)
            request.send_header("Location", destination)
            request.end_headers()
            return
        if path in self.overrides:
            status, body = self.overrides[path]
        elif path in self.files:
            status, body = 200, self.files[path]
        else:
            request.send_response(404)
            request.end_headers()
            return
        request.send_response(status)
        if status == 200:
            if path.endswith(".css"):
                request.send_header("Content-Type", "text/css")
                request.send_header("Cache-Control", "public, max-age=31536000, immutable")
            elif path.endswith(".js"):
                request.send_header("Content-Type", "application/javascript")
                request.send_header("Cache-Control", "public, max-age=31536000, immutable")
            elif path.endswith(".html") or path in {"/", "/terms", "/privacy", "/copyright", "/notices"}:
                request.send_header("Content-Type", "text/html")
            if path not in self.missing_headers:
                request.send_header("Content-Security-Policy", "default-src 'none'; connect-src 'none'")
                request.send_header("X-Content-Type-Options", "nosniff")
                request.send_header("Referrer-Policy", "no-referrer")
                request.send_header("X-Frame-Options", "DENY")
                request.send_header("Permissions-Policy", "fullscreen=(self)")
                request.send_header("X-Robots-Tag", "noindex, nofollow, noarchive")
        request.end_headers()
        request.wfile.write(body)


def _fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    files = {
        "/index.html": b"<html><body>Gameplay is not available</body></html>",
        "/terms.html": b"<html><h1>Terms of Use</h1></html>",
        "/privacy.html": b"<html><h1>Privacy Notice</h1></html>",
        "/copyright.html": b"<html><h1>Copyright & contact</h1></html>",
        "/notices.html": b"<html><h1>Third-party notices</h1></html>",
        "/robots.txt": b"User-agent: *\nDisallow: /\n",
    }
    css = b"body { color: black; }"
    js = b"document.documentElement.dataset.ready = 'yes';"
    css_name = f"/assets/site.{hashlib.sha256(css).hexdigest()[:16]}.css"
    js_name = f"/assets/site.{hashlib.sha256(js).hexdigest()[:16]}.js"
    files[css_name] = css
    files[js_name] = js
    aliases = {
        "/": "/index.html",
        "/terms": "/terms.html",
        "/privacy": "/privacy.html",
        "/copyright": "/copyright.html",
        "/notices": "/notices.html",
    }
    for alias, source in aliases.items():
        files[alias] = files[source]
    records = [
        {"path": path.lstrip("/"), "size": len(body), "sha256": hashlib.sha256(body).hexdigest()}
        for path, body in sorted(files.items()) if path not in aliases
    ]
    return files, {"files": records, "index_production": False}


class PublicHTTPVerifierTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="melee-public-http-")
        self.manifest_path = Path(self.temp.name) / "manifest.json"
        self.files, manifest = _fixture()
        self.manifest_path.write_text(json.dumps(manifest) + "\n", encoding="utf-8")
        self.servers: list[_StaticHTTP] = []

    def tearDown(self) -> None:
        for server in reversed(self.servers):
            server.close()
        self.temp.cleanup()

    def server(self, **kwargs) -> _StaticHTTP:
        server = _StaticHTTP(self.files, **kwargs)
        self.servers.append(server)
        return server

    def test_valid_canonical_html_redirects_pass(self):
        origin = self.server(canonical_redirects=True)
        result = verify(origin.url, self.manifest_path)
        self.assertEqual(result["result"], "pass")
        self.assertEqual([item["canonical_path"] for item in result["aliases"]],
                         ["/", "/terms", "/privacy", "/copyright", "/notices"])

    def test_cross_origin_matching_bytes_are_rejected(self):
        attacker = self.server()
        origin = self.server()
        origin.redirects = {"/terms.html": f"{attacker.url}/terms"}

        # The redirect is installed after construction so it can reference the
        # second real HTTP origin's ephemeral port.
        original_get = origin._get

        def redirected_get(request):
            path = urllib.parse.urlsplit(request.path).path
            if path in origin.redirects:
                request.send_response(302)
                request.send_header("Location", origin.redirects[path])
                request.end_headers()
                return
            original_get(request)

        origin._get = redirected_get
        with self.assertRaisesRegex(ValueError, "escaped the candidate origin"):
            verify(origin.url, self.manifest_path)

    def test_off_origin_redirect_on_missing_route_is_rejected(self):
        attacker = self.server()
        origin = self.server()
        origin.redirects = {"/runtime.html": f"{attacker.url}/runtime.html"}
        original_get = origin._get

        def redirected_get(request):
            path = urllib.parse.urlsplit(request.path).path
            if path in origin.redirects:
                request.send_response(302)
                request.send_header("Location", origin.redirects[path])
                request.end_headers()
                return
            original_get(request)

        origin._get = redirected_get
        with self.assertRaisesRegex(ValueError, "escaped the candidate origin"):
            verify(origin.url, self.manifest_path)

    def test_legal_alias_spa_fallback_is_rejected(self):
        origin = self.server(overrides={"/privacy": (200, self.files["/index.html"])})
        with self.assertRaisesRegex(ValueError, r"byte identity/status failed: /privacy"):
            verify(origin.url, self.manifest_path)

    def test_missing_legal_alias_headers_are_rejected(self):
        origin = self.server(missing_headers={"/copyright"})
        with self.assertRaisesRegex(ValueError, r"/copyright: missing CSP"):
            verify(origin.url, self.manifest_path)

    def test_optimized_python_still_rejects_missing_alias_headers(self):
        origin = self.server(missing_headers={"/copyright"})
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(ROOT / "scripts")
        probe = subprocess.run(
            [sys.executable, "-O", "-c",
             "import sys; from verify_public_http import verify; verify(sys.argv[1], sys.argv[2])",
             origin.url, str(self.manifest_path)],
            env=environment, capture_output=True, text=True,
        )
        self.assertNotEqual(probe.returncode, 0, probe.stdout + probe.stderr)
        self.assertIn('/copyright: missing CSP', probe.stderr)


if __name__ == "__main__":
    unittest.main()
