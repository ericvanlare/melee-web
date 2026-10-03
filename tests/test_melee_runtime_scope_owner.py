import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


class ScopedRuntimeOwnerTests(unittest.TestCase):
    def run_mode(self, mode, expected):
        from check_gameplay import node_runtime
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/melee_runtime_scope_owner_test.mjs"), mode],
            cwd=ROOT, capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(expected, result.stdout)

    def test_initial_scene_scope_restart_and_file_lifetime(self):
        self.run_mode("--lifecycle", "initial commit")

    def test_source_payload_backpressure_and_complete_native_copy_order(self):
        self.run_mode("--stream-bound", "bounded source payload")

    def test_source_payload_byte_budget_before_file_count_limit(self):
        self.run_mode("--stream-byte-bound", "bounded source payload")

    def test_duplicate_concurrent_request_fails_closed(self):
        self.run_mode("--duplicate", "duplicate concurrent request")

    def test_commit_failure_aborts_without_prepare(self):
        self.run_mode("--commit-fail", "commit failure aborts")

    def test_read_failure_aborts_and_retries(self):
        self.run_mode("--read-fail", "read failure preserves")

    def test_late_read_failure_aborts_staged_payload_and_retries(self):
        self.run_mode("--read-mid-fail", "read-mid failure preserves")

    def test_put_failure_aborts_and_retries(self):
        self.run_mode("--put-fail", "put failure preserves")

    def test_late_open_after_stop_closes_late_session(self):
        self.run_mode("--late-open-stop", "late open after stop")

    def test_late_open_after_destroy_closes_late_session(self):
        self.run_mode("--late-open-destroy", "late open after destroy")

    def test_destroy_closes_file_and_audio_after_native_unload_failure(self):
        self.run_mode("--destroy-unload-fail", "failed native unload")

    def test_source_transition_error_is_visible_and_eject_recovers(self):
        self.run_mode("--source-transition-fail", "unsupported source transition is surfaced")


if __name__ == "__main__":
    unittest.main()
