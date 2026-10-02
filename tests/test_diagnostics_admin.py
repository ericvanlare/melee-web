import contextlib
import io
import json
import os
from pathlib import Path
import stat
import sys
import tempfile
import unittest
from urllib.parse import parse_qs, urlsplit
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import diagnostics_admin as admin  # noqa: E402


TOKEN = "test-admin-token-never-printed-123456"
REPORT_ID = "a" * 64
SOURCE_COMMIT = "0123456789abcdef0123456789abcdef01234567"
RUNTIME_HASH = "0123456789abcdef"


def report():
    return {
        "schema": "melee-web-diagnostics",
        "version": 1,
        "session_id": "session-aaaaaaaaaaaaaaaa",
        "incident_id": "incident-1",
        "identity": {"source_commit": SOURCE_COMMIT, "runtime_hash": RUNTIME_HASH, "build_profile": "player"},
        "environment": {"env": "staging", "origin": "https://staging.webmelee.gg"},
        "client": {"browser_family": "chrome", "browser_major": None, "platform": "ios"},
        "capabilities": {
            "native": {"available": True, "observed": True, "reason": None},
            "audio": {"available": False, "observed": False, "reason": "unavailable"},
            "longtask": {"available": True, "observed": False, "reason": "supported_no_events"},
        },
        "incident": {
            "reason": "runtime_failure", "reason_code": 4, "value": None, "threshold": None,
            "source_frame": None, "scene": "opening_vs", "scene_code": 13,
            "clock_owner": "other", "clock_owner_code": 0,
        },
        "history": {
            "columns": [
                "timestamp", "source_frame", "scene", "stage", "fighter0", "fighter1",
                "fighter2", "fighter3", "debt_ticks", "update_ms", "draw_ms", "total_ms",
                "preparation_ms", "queued_delta", "created_delta", "texture_upload_bytes",
                "source_steps", "source_draws", "running", "interval_ms",
            ],
            "rows": [[None] * 20], "evicted": False, "evicted_count": 0, "truncated": False,
        },
        "events": {
            "pre": [{
                "type": "audio", "timestamp": 10, "callback_ms": 1, "interval_ms": 2,
                "queue_depth": 3, "underruns": 0, "overflows": 0, "clock_seconds": 0.1,
                "context_state": "running", "enabled": True, "source_frame": None,
            }],
            "post": [{"type": "longtask", "timestamp": 20, "duration_ms": 22, "source_frame": 3}],
        },
        "flags": {"incomplete": False, "persistence_failure": False},
    }


def envelope():
    return {"report_id": REPORT_ID, "received_at": 100, "expires_at": 200, "bytes": 1234, "report": report()}


class FakeResponse:
    def __init__(self, value, status=200):
        self.raw = json.dumps(value, separators=(",", ":")).encode()
        self.status = status

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def read(self, size=-1):
        if size < 0:
            result, self.raw = self.raw, b""
            return result
        result, self.raw = self.raw[:size], self.raw[size:]
        return result


class Recorder:
    def __init__(self, value):
        self.value = value
        self.requests = []

    def __call__(self, request, timeout=0):
        self.requests.append(request)
        return FakeResponse(self.value)


class DiagnosticsAdminTest(unittest.TestCase):
    def setUp(self):
        self.environment = {
            admin.API_ENV: "https://staging.webmelee.gg/api/diagnostics",
            admin.TOKEN_ENV: TOKEN,
        }

    def run_cli(self, arguments, response):
        recorder = Recorder(response)
        stdout = io.StringIO()
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, self.environment, clear=True), \
                mock.patch("urllib.request.urlopen", recorder), \
                contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            code = admin.main(arguments)
        return code, stdout.getvalue(), stderr.getvalue(), recorder

    def test_query_filters_cursor_and_bearer_header(self):
        cursor = "100." + REPORT_ID
        code, output, error, recorder = self.run_cli([
            "query", "--environment", "staging", "--source-commit", SOURCE_COMMIT,
            "--runtime-hash", RUNTIME_HASH, "--reason", "runtime_failure",
            "--build", "player", "--from", "10", "--to", "20", "--limit", "7", "--cursor", cursor,
        ], {"reports": [envelope()], "next_cursor": None})
        self.assertEqual(code, 0)
        self.assertEqual(error, "")
        self.assertNotIn(TOKEN, output)
        request = recorder.requests[0]
        self.assertEqual(request.get_header("Authorization"), "Bearer " + TOKEN)
        query = parse_qs(urlsplit(request.full_url).query)
        self.assertEqual(query["environment"], ["staging"])
        self.assertEqual(query["source_commit"], [SOURCE_COMMIT])
        self.assertEqual(query["runtime_hash"], [RUNTIME_HASH])
        self.assertEqual(query["reason"], ["runtime_failure"])
        self.assertEqual(query["build"], ["player"])
        self.assertEqual(query["from"], ["10"])
        self.assertEqual(query["to"], ["20"])
        self.assertEqual(query["limit"], ["7"])
        self.assertEqual(query["cursor"], [cursor])

    def test_get_delete_purge_and_download_are_authenticated(self):
        code, output, _, recorder = self.run_cli(["get", REPORT_ID], envelope())
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(output)["report_id"], REPORT_ID)
        self.assertEqual(recorder.requests[0].get_method(), "GET")

        code, output, _, recorder = self.run_cli(["delete", REPORT_ID], {"deleted": True})
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(output), {"deleted": True})
        self.assertEqual(recorder.requests[0].get_method(), "DELETE")

        code, output, _, recorder = self.run_cli(["purge", "--before", "200", "--limit", "9"], {"deleted": 3})
        self.assertEqual(code, 0)
        self.assertEqual(json.loads(output), {"deleted": 3})
        query = parse_qs(urlsplit(recorder.requests[0].full_url).query)
        self.assertEqual(query, {"before": ["200"], "limit": ["9"]})

        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory) / "sanitized.json"
            code, output, _, _ = self.run_cli(["download", REPORT_ID, "--output", str(output_path)], envelope())
            self.assertEqual(code, 0)
            self.assertEqual(json.loads(output), {"downloaded": True})
            self.assertEqual(json.loads(output_path.read_text())["schema"], "melee-web-diagnostics")
            self.assertEqual(stat.S_IMODE(output_path.stat().st_mode), 0o600)

    def test_secret_is_environment_only_and_response_is_revalidated(self):
        with mock.patch.dict(os.environ, {admin.API_ENV: "https://staging.webmelee.gg", admin.TOKEN_ENV: TOKEN}, clear=True):
            with self.assertRaises(SystemExit):
                admin._build_parser().parse_args(["--token", TOKEN, "query"])
        private = envelope()
        private["report"]["private"] = "must not pass"
        code, output, error, _ = self.run_cli(["get", REPORT_ID], private)
        self.assertEqual(code, 2)
        self.assertEqual(output, "")
        self.assertNotIn(TOKEN, error)
        self.assertIn("invalid_report", error)

    def test_missing_secret_does_not_open_network(self):
        recorder = Recorder(envelope())
        with mock.patch.dict(os.environ, {admin.API_ENV: "https://staging.webmelee.gg"}, clear=True), \
                mock.patch("urllib.request.urlopen", recorder):
            code = admin.main(["get", REPORT_ID])
        self.assertEqual(code, 2)
        self.assertEqual(recorder.requests, [])

    def test_response_parser_bounds_nesting_and_shape_before_display(self):
        with self.assertRaises(admin.CliError):
            admin._decode_json(("[" * 17 + "0" + "]" * 17).encode())
        with self.assertRaises(admin.CliError):
            admin._decode_json(("[" + ",".join("0" for _ in range(129)) + "]").encode())
        malformed = envelope()
        malformed["report"]["incident"]["scene_code"] = []
        with self.assertRaises(admin.CliError):
            admin._sanitize_envelope(malformed)


if __name__ == "__main__":
    unittest.main()
