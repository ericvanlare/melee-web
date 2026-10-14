import io
import json
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime
from capture_owner_deadline import (
    capture_browser_inventory, prepare_output_parents, read_capture_report,
    signal_pid_if_same,
)


class CaptureOwnerIdentityTests(unittest.TestCase):
    chrome = "/opt/chrome/Google Chrome"
    profile = "/tmp/capture-shared-profile"

    def row(self, pid, ppid, pgid, start, command):
        return {"pid": pid, "ppid": ppid, "pgid": pgid,
                "start_identity": start, "command": command}

    def test_inventory_excludes_same_executable_and_profile_outside_capture_lineage(self):
        root = self.row(100, 1, 100, "owner-start", "node capture-owner")
        owned = self.row(101, 100, 100, "owned-start",
                         f'{self.chrome} --user-data-dir={self.profile}')
        renderer = self.row(102, 101, 100, "renderer-start",
                            f'{self.chrome} --type=renderer --user-data-dir={self.profile}')
        foreign = self.row(200, 1, 100, "foreign-start",
                           f'{self.chrome} --user-data-dir={self.profile}')
        inventory = {}
        trace = io.StringIO()
        with patch("capture_owner_deadline.process_table",
                   return_value={row["pid"]: row for row in [root, owned, renderer, foreign]}):
            _, added = capture_browser_inventory(100, [self.chrome], inventory, trace)
        self.assertEqual({row["pid"] for row in added}, {101, 102})
        self.assertNotIn((200, "foreign-start", foreign["command"]), inventory)
        self.assertTrue(all(row["observed_while_capture_root_alive"] for row in added))

    def test_pid_reuse_or_changed_command_is_preserved(self):
        owned = {"pid": 301, "start_identity": "owned-start",
                 "command": f'{self.chrome} --user-data-dir={self.profile}'}
        reused = {"pid": 301, "ppid": 1, "pgid": 301,
                  "start_identity": "replacement-start", "command": owned["command"]}
        changed = {**owned, "command": f'{self.chrome} --user-data-dir=/tmp/other-profile'}
        with patch("capture_owner_deadline.os.kill") as kill:
            self.assertEqual(signal_pid_if_same(owned, {301: reused}, signal.SIGTERM),
                             "identity_changed_preserved")
            self.assertEqual(signal_pid_if_same(owned, {301: changed}, signal.SIGKILL),
                             "identity_changed_preserved")
            kill.assert_not_called()
            self.assertEqual(signal_pid_if_same(owned, {301: owned}, signal.SIGTERM), "signaled")
            kill.assert_called_once_with(301, signal.SIGTERM)

    def test_shared_chrome_group_signals_only_the_recorded_owned_pid(self):
        owned = {"pid": 401, "ppid": 100, "pgid": 77,
                 "start_identity": "owned-start", "command": f'{self.chrome} --type=renderer'}
        foreign = {"pid": 402, "ppid": 1, "pgid": 77,
                   "start_identity": "foreign-start", "command": f'{self.chrome} --type=renderer'}
        with patch("capture_owner_deadline.os.kill") as kill, \
             patch("capture_owner_deadline.os.killpg") as killpg:
            self.assertEqual(signal_pid_if_same(owned, {401: owned, 402: foreign}, signal.SIGTERM),
                             "signaled")
            kill.assert_called_once_with(401, signal.SIGTERM)
            killpg.assert_not_called()

    def test_cdp_only_live_pid_is_not_added_to_owned_cleanup_inventory(self):
        root = self.row(500, 1, 500, "owner-start", "node capture-owner")
        cdp_only = self.row(501, 1, 501, "cdp-start",
                            f'{self.chrome} --user-data-dir={self.profile}')
        with tempfile.TemporaryDirectory() as temporary:
            report_path = Path(temporary) / "browser-report.json"
            report_path.write_text(json.dumps({"browser_process_info": [
                {"id": 501, "type": "browser"}]}))
            report = {}
            self.assertEqual(read_capture_report(report_path, report), [501])
            inventory = {}
            with patch("capture_owner_deadline.process_table",
                       return_value={500: root, 501: cdp_only}):
                _, added = capture_browser_inventory(500, [self.chrome], inventory, io.StringIO())
            self.assertEqual(added, [])
            self.assertEqual(inventory, {})
            with patch("capture_owner_deadline.os.kill") as kill:
                for record in inventory.values():
                    signal_pid_if_same(record, {501: cdp_only}, signal.SIGTERM)
                kill.assert_not_called()


class CaptureOwnerOutputTests(unittest.TestCase):
    def test_node_runtime_owner_cleanup_controls_are_in_python_suite(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(ROOT / "tests/net_room_relay_runtime_owner_cleanup.test.mjs")],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_hosted_room_leave_cleanup_controls_are_in_python_suite(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(ROOT / "tests/a3_local_room_cleanup.test.mjs")],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_browser_report_parent_is_created_by_capture_runner(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            browser_output = root / "run" / "browser"
            browser_report = browser_output / "report.json"
            owner_report = root / "run" / "owner.json"
            process_trace = root / "run" / "processes.jsonl"
            server_log = root / "run" / "server.log"

            prepare_output_parents(
                [owner_report, browser_report, process_trace, server_log],
                browser_report,
            )

            self.assertTrue(owner_report.parent.is_dir())
            self.assertFalse(browser_output.exists())
            browser_output.mkdir()
            browser_report.write_text("{}\n")
            self.assertTrue(browser_report.is_file())


if __name__ == "__main__":
    unittest.main()
