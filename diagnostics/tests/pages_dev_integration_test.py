"""Real local Pages/Miniflare integration for the diagnostics Function.

Run this bounded test through the checkout wrapper. It creates a synthetic
Pages project under ``work/diagnostics``, applies the real D1 migration with
the pinned Wrangler, starts ``pages dev`` over local HTTPS, and retains a
hash-only receipt. No hosted endpoint or real report is used.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import ssl
import subprocess
import tempfile
import time
import unittest
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "diagnostics"
WORK_ROOT = ROOT / "work" / "diagnostics"
WRANGLER = Path(os.environ.get("WRANGLER_BIN", ROOT / ".tools" / "wrangler" / "node_modules" / ".bin" / "wrangler"))
WRANGLER_VERSION = json.loads((ROOT / "dependencies.lock.json").read_text())["deployment_tools"]["wrangler"]["version"]
SOURCE_COMMIT = "0123456789abcdef0123456789abcdef01234567"
RUNTIME_HASH = "0123456789abcdef"
ADMIN_TOKEN = "local-admin-token-0123456789"
RELEASES = json.dumps({
    "staging": [{"source_commit": SOURCE_COMMIT, "runtime_hash": RUNTIME_HASH, "build_profile": "player"}],
    "production": [{"source_commit": SOURCE_COMMIT, "runtime_hash": RUNTIME_HASH, "build_profile": "player"}],
}, separators=(",", ":"))
HISTORY_COLUMNS = [
    "timestamp", "source_frame", "scene", "stage", "fighter0", "fighter1",
    "fighter2", "fighter3", "debt_ticks", "update_ms", "draw_ms", "total_ms",
    "preparation_ms", "queued_delta", "created_delta", "texture_upload_bytes",
    "source_steps", "source_draws", "running", "interval_ms",
]


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def report(session_id: str, incident_id: str = "incident-1", *, env: str = "staging", origin: str | None = None, value: float = 1.0) -> dict:
    return {
        "schema": "melee-web-diagnostics", "version": 1,
        "session_id": session_id, "incident_id": incident_id,
        "identity": {"source_commit": SOURCE_COMMIT, "runtime_hash": RUNTIME_HASH, "build_profile": "player"},
        "environment": {"env": env, "origin": origin or f"https://{'staging.webmelee.gg' if env == 'staging' else 'www.webmelee.gg'}"},
        "client": {"browser_family": "chrome", "browser_major": 140, "platform": "linux"},
        "capabilities": {
            "native": {"available": True, "observed": True, "reason": None},
            "audio": {"available": False, "observed": False, "reason": "unavailable"},
            "longtask": {"available": True, "observed": False, "reason": "supported_no_events"},
        },
        "incident": {
            "reason": "simulation_debt", "reason_code": 1, "value": value, "threshold": 2.0,
            "source_frame": 1, "scene": "match", "scene_code": 7,
            "clock_owner": "simulation", "clock_owner_code": 1,
        },
        "history": {
            "columns": HISTORY_COLUMNS,
            "rows": [[1, 1, 7, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 16]],
            "evicted": False, "evicted_count": 0, "truncated": False,
        },
        "events": {"pre": [], "post": []},
        "flags": {"incomplete": False, "persistence_failure": False},
    }


class PagesDevDiagnosticsIntegration(unittest.TestCase):
    maxDiff = None

    def setUp(self) -> None:
        if not WRANGLER.is_file():
            raise unittest.SkipTest("Set WRANGLER_BIN to the installed locked Wrangler executable")
        self.assertEqual(self._wrangler_version(), WRANGLER_VERSION)
        WORK_ROOT.mkdir(parents=True, exist_ok=True)
        self.fixture = Path(tempfile.mkdtemp(prefix="pages-dev-integration-", dir=WORK_ROOT))
        self.server: subprocess.Popen[str] | None = None
        self.purge_server: subprocess.Popen[str] | None = None
        self.port = self._free_port()
        self._make_fixture()
        self._run_wrangler("d1", "migrations", "apply", "diag-local", "--local", "--persist-to", "state")

    def tearDown(self) -> None:
        self._stop_server()
        receipt = {
            "schema": "melee-web-diagnostics-local-pages-receipt-v1",
            "result": "executed",
            "wrangler": str(WRANGLER),
            "wrangler_version": self._wrangler_version(),
            "compatibility_date": "2026-09-18",
            "fixture": str(self.fixture),
            "source_hashes": {
                name: sha256(SOURCE / name)
                for name in ("schema.mjs", "worker.mjs", "purge-worker.mjs", "purge-config.mjs", "purge-wrangler.jsonc", "pages-function-adapter.mjs", "pages-function-catchall-adapter.mjs", "_routes.json")
            },
            "migration_sha256": sha256(SOURCE / "migrations" / "0001_diagnostics.sql"),
            "config_sha256": sha256(self.fixture / "wrangler.jsonc"),
            "synthetic_identity": {"source_commit": SOURCE_COMMIT, "runtime_hash": RUNTIME_HASH, "build_profile": "player"},
        }
        (self.fixture / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")

    @staticmethod
    def _free_port() -> int:
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            return int(sock.getsockname()[1])

    def _wrangler_version(self) -> str:
        result = subprocess.run([str(WRANGLER), "--version"], capture_output=True, text=True, check=False, timeout=20)
        return result.stdout.strip().splitlines()[-1] if result.stdout.strip() else "unknown"

    def _make_fixture(self) -> None:
        public = self.fixture / "public"
        api = self.fixture / "functions" / "api"
        (api / "diagnostics").mkdir(parents=True)
        public.mkdir(parents=True)
        shutil.copy2(SOURCE / "pages-function-adapter.mjs", api / "diagnostics.js")
        shutil.copy2(SOURCE / "pages-function-catchall-adapter.mjs", api / "diagnostics" / "[[report]].js")
        for name in ("worker.mjs", "schema.mjs"):
            shutil.copy2(SOURCE / name, api / name)
        for name in ("worker.mjs", "schema.mjs", "purge-worker.mjs", "purge-config.mjs"):
            shutil.copy2(SOURCE / name, self.fixture / name)
        shutil.copy2(SOURCE / "_routes.json", public / "_routes.json")
        (public / "index.html").write_text("STATIC-OK")
        (public / "blocked.txt").write_text("STATIC-BLOCKED-MARKER")
        migration_dir = self.fixture / "migrations"
        migration_dir.mkdir()
        shutil.copy2(SOURCE / "migrations" / "0001_diagnostics.sql", migration_dir / "0001_diagnostics.sql")
        (self.fixture / "wrangler.jsonc").write_text(json.dumps({
            "name": "diag-local",
            "compatibility_date": "2026-09-18",
            "pages_build_output_dir": "./public",
            "d1_databases": [{
                "binding": "DIAGNOSTICS_DB", "database_name": "diag-local",
                "database_id": "00000000-0000-0000-0000-000000000001",
                "migrations_dir": "./migrations",
            }],
        }, indent=2) + "\n")
        (self.fixture / "purge-wrangler.jsonc").write_text(json.dumps({
            "name": "diag-local-purge",
            "main": "./purge-worker.mjs",
            "compatibility_date": "2026-09-18",
            "triggers": {"crons": ["*/30 * * * *"]},
            "d1_databases": [{
                "binding": "DIAGNOSTICS_DB", "database_name": "diag-local",
                "database_id": "00000000-0000-0000-0000-000000000001",
                "migrations_dir": "./migrations",
            }],
        }, indent=2) + "\n")

    def _run_wrangler(self, *args: str) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [str(WRANGLER), "--cwd", str(self.fixture), *args],
            capture_output=True, text=True, check=False, timeout=45,
        )
        if result.returncode != 0:
            self.fail(f"Wrangler failed ({result.returncode}): {result.stdout[-2000:]}{result.stderr[-2000:]}")
        return result

    def _start_server(self, *, rate_limit: int = 60, daily_report_cap: int = 1000) -> None:
        bindings = [
            "--binding", "DIAGNOSTICS_ADMIN_TOKEN=" + ADMIN_TOKEN,
            "--binding", "DIAGNOSTICS_ALLOWED_RELEASES=" + RELEASES,
            "--binding", f"DIAGNOSTICS_RATE_LIMIT={rate_limit}",
            "--binding", f"DIAGNOSTICS_DAILY_REPORT_CAP={daily_report_cap}",
            "--binding", "DIAGNOSTICS_DAILY_BYTE_CAP=16777216",
        ]
        self.server = subprocess.Popen(
            [str(WRANGLER), "--cwd", str(self.fixture), "pages", "dev", "public",
             "--port", str(self.port), "--local-protocol", "https", "--persist-to", "state",
             *bindings, "--log-level", "error", "--show-interactive-dev-session", "false"],
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, start_new_session=True,
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                status, _, _ = self._request("/")
                if status == 200:
                    return
            except Exception:
                time.sleep(0.2)
        self.fail("Wrangler Pages dev did not become ready")

    def _stop_server(self) -> None:
        for name in ("server", "purge_server"):
            process = getattr(self, name)
            if process is None:
                continue
            if process.poll() is None:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                    process.wait(timeout=5)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    process.kill()
                    process.wait(timeout=5)
            setattr(self, name, None)

    def _start_purge_server(self) -> None:
        purge_port = self._free_port()
        self.purge_server = subprocess.Popen(
            [str(WRANGLER), "--cwd", str(self.fixture), "dev", "purge-worker.mjs",
             "--config", "purge-wrangler.jsonc", "--local", "--test-scheduled",
             "--port", str(purge_port), "--persist-to", "state", "--log-level", "error",
             "--show-interactive-dev-session", "false"],
            stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, start_new_session=True,
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{purge_port}/__scheduled", timeout=1) as response:
                    self.assertEqual(response.status, 200)
                    self.assertEqual(response.read(), b"Ran scheduled event")
                    return
            except Exception:
                time.sleep(0.2)
        self.fail("Wrangler scheduled Worker did not become ready")

    def _request(self, path: str, method: str = "GET", body: bytes | None = None, *, host: str = "staging.webmelee.gg", origin: str | None = None, token: str | None = None, content_type: str | None = None) -> tuple[int, dict[str, str], bytes]:
        headers = {"Host": host}
        if origin is not None:
            headers["Origin"] = origin
        if token is not None:
            headers["Authorization"] = "Bearer " + token
        if content_type is not None:
            headers["Content-Type"] = content_type
        request = urllib.request.Request(
            f"https://127.0.0.1:{self.port}{path}", method=method, data=body, headers=headers,
        )
        context = ssl._create_unverified_context()
        try:
            with urllib.request.urlopen(request, context=context, timeout=8) as response:
                return response.status, {key.lower(): value for key, value in response.headers.items()}, response.read()
        except urllib.error.HTTPError as error:
            return error.code, {key.lower(): value for key, value in error.headers.items()}, error.read()

    def _post(self, value: dict, *, host: str = "staging.webmelee.gg", origin: str | None = None) -> tuple[int, dict[str, str], dict]:
        origin = origin or f"https://{host}"
        status, headers, body = self._request(
            "/api/diagnostics", "POST", json.dumps(value, separators=(",", ":")).encode(),
            host=host, origin=origin, content_type="application/json",
        )
        try:
            decoded = json.loads(body)
        except json.JSONDecodeError:
            decoded = {"raw": body.decode("utf-8", "replace")}
        return status, headers, decoded

    def test_static_fallback_and_public_route_isolation(self) -> None:
        self._start_server()
        status, _, body = self._request("/")
        self.assertEqual((status, body), (200, b"STATIC-OK"))
        status, _, body = self._request("/blocked.txt")
        self.assertEqual((status, body), (200, b"STATIC-BLOCKED-MARKER"))
        private_status, _, private_body = self._request("/api/private-route")
        self.assertEqual((private_status, private_body), (200, b"STATIC-OK"))
        self.assertEqual(self._request("/api/diagnostics/private-route")[0], 404)
        self.assertEqual(self._request("/api/diagnostics")[0], 401)

    def test_dedup_conflict_admin_catchall_and_headers(self) -> None:
        self._start_server()
        first, headers, accepted = self._post(report("session-localdedup"))
        self.assertEqual(first, 201)
        self.assertEqual(headers["cache-control"], "no-store")
        self.assertEqual(headers["x-content-type-options"], "nosniff")
        report_id = accepted["report_id"]
        self.assertTrue(re.fullmatch(r"[a-f0-9]{64}", report_id))
        self.assertEqual(self._post(report("session-localdedup"))[2]["duplicate"], True)
        self.assertEqual(self._post(report("session-localdedup", value=2.0))[0], 409)
        status, _, body = self._request("/api/diagnostics", token=ADMIN_TOKEN)
        self.assertEqual(status, 200)
        self.assertEqual(json.loads(body)["reports"][0]["report_id"], report_id)
        self.assertEqual(self._request(f"/api/diagnostics/{report_id}", token=ADMIN_TOKEN)[0], 200)
        self.assertEqual(self._request(f"/api/diagnostics/{report_id}", method="DELETE", token=ADMIN_TOKEN)[0], 200)
        self.assertEqual(self._request(f"/api/diagnostics/{report_id}", token=ADMIN_TOKEN)[0], 404)
        self.assertEqual(self._request("/api/diagnostics?limit=1")[0], 401)

    def test_limits_origin_host_and_staging_production_separation(self) -> None:
        self._start_server()
        value = report("session-limits")
        self.assertEqual(self._request("/api/diagnostics", "POST", b"{", host="staging.webmelee.gg", origin="https://staging.webmelee.gg", content_type="application/json")[0], 400)
        self.assertEqual(self._request("/api/diagnostics", "POST", b"x" * 65537, host="staging.webmelee.gg", origin="https://staging.webmelee.gg", content_type="application/json")[0], 413)
        self.assertEqual(self._post(value, origin="https://webmelee.gg")[0], 403)
        self.assertEqual(self._post(value, host="staging.webmelee.pages.dev")[0], 404)
        production = report("session-production", env="production", origin="https://www.webmelee.gg")
        self.assertEqual(self._post(production, host="www.webmelee.gg")[0], 201)
        self.assertEqual(self._post(value, host="www.webmelee.gg", origin="https://www.webmelee.gg")[0], 403)
        status, _, body = self._request("/api/diagnostics?environment=production&limit=10", token=ADMIN_TOKEN)
        self.assertEqual(status, 200)
        self.assertEqual(len(json.loads(body)["reports"]), 1)

    def test_rate_and_daily_caps_are_enforced_locally(self) -> None:
        self._start_server(rate_limit=3, daily_report_cap=1)
        self.assertEqual(self._post(report("session-cap1"))[0], 201)
        self.assertEqual(self._post(report("session-cap1"))[2]["duplicate"], True)
        self.assertEqual(self._post(report("session-cap2"))[2]["error"], "daily_cap")
        self.assertEqual(self._post(report("session-cap3"))[2]["error"], "rate_limited")

    def test_body_timeout_is_enforced_by_local_worker(self) -> None:
        self._start_server()
        context = ssl._create_unverified_context()
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=4)
        tls = context.wrap_socket(raw, server_hostname="localhost")
        tls.settimeout(8)
        try:
            tls.sendall(
                b"POST /api/diagnostics HTTP/1.1\r\nHost: staging.webmelee.gg\r\n"
                b"Origin: https://staging.webmelee.gg\r\nContent-Type: application/json\r\n"
                b"Content-Length: 2\r\nConnection: close\r\n\r\n{"
            )
            time.sleep(5.5)
            try:
                tls.sendall(b"}")
            except OSError:
                pass
            chunks = []
            while True:
                chunk = tls.recv(4096)
                if not chunk:
                    break
                chunks.append(chunk)
            response = b"".join(chunks)
        finally:
            tls.close()
        self.assertIn(b" 408 ", response)
        self.assertIn(b'"body_timeout"', response)

    def test_scheduled_purge_worker_runs_against_local_d1(self) -> None:
        self._start_server()
        status, _, accepted = self._post(report("session-expired"))
        self.assertEqual(status, 201)
        report_id = accepted["report_id"]
        self._run_wrangler(
            "d1", "execute", "diag-local", "--local", "--persist-to", "state",
            "--command", f"UPDATE diagnostic_reports SET expires_at = 0 WHERE report_id = '{report_id}'",
        )
        self._start_purge_server()
        self.assertEqual(self._request(f"/api/diagnostics/{report_id}", token=ADMIN_TOKEN)[0], 404)


if __name__ == "__main__":
    unittest.main()
