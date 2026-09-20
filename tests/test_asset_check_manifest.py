"""Mocked contract tests for the checks manifest frontend."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("check_assets", ROOT / "scripts/check_assets.py")
assert SPEC is not None and SPEC.loader is not None
CHECK_ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CHECK_ASSETS)


class AssetCheckManifestTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="melee asset manifest ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "ordinary.dat").write_bytes(b"ordinary")
        (self.root / "stage.usd").write_bytes(b"stage")

    def write_manifest(self, value):
        path = self.root / "checks.json"
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_mixed_rows_resolve_paths_and_preserve_each_selection(self):
        manifest = self.write_manifest({
            "checks": [
                {"path": "ordinary.dat", "symbol": "fixture_joint"},
                {"path": "stage.usd", "stage_entry": 3, "opaque": True},
                {"path": "ordinary.dat", "stage_entry": 0, "opaque": False},
            ]
        })

        checks = CHECK_ASSETS.load_manifest(manifest)

        resolved_root = self.root.resolve()
        self.assertEqual(checks, [
            {"path": resolved_root / "ordinary.dat", "symbol": "fixture_joint"},
            {"path": resolved_root / "stage.usd", "stage_entry": 3, "opaque": True},
            {"path": resolved_root / "ordinary.dat", "stage_entry": 0, "opaque": False},
        ])

    def test_invalid_manifests_are_rejected_before_any_check_can_run(self):
        invalid_values = [
            {},
            {"checks": []},
            {"checks": [{"path": "ordinary.dat", "extra": True}]},
            {"checks": [{"symbol": "fixture_joint"}]},
            {"checks": [{"path": "ordinary.dat", "symbol": None}]},
            {"checks": [{"path": "ordinary.dat", "stage_entry": None}]},
            {"checks": [{"path": "ordinary.dat", "stage_entry": True}]},
            {"checks": [{"path": "ordinary.dat", "stage_entry": 0, "opaque": "yes"}]},
            {"checks": [{"path": "ordinary.dat", "opaque": True}]},
        ]
        for value in invalid_values:
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    CHECK_ASSETS.load_manifest(self.write_manifest(value))

    def test_absolute_path_is_preserved_and_resolved(self):
        absolute = (self.root / "ordinary.dat").resolve()
        manifest = self.write_manifest({"checks": [{"path": str(absolute)}]})

        self.assertEqual(CHECK_ASSETS.load_manifest(manifest), [{"path": absolute}])

    def test_manifest_rows_compile_once_and_preserve_parser_failures(self):
        checks = [
            {"path": self.root / "ordinary.dat", "symbol": "fixture_joint"},
            {"path": self.root / "stage.usd", "stage_entry": 3, "opaque": True},
        ]
        records = [
            {"root": "fixture_joint", "status": "rejected"},
            {"root": "stage", "status": "accepted"},
        ]
        calls = []

        def fake_run(command, **kwargs):
            calls.append((command, kwargs))
            if len(calls) == 1:
                return subprocess.CompletedProcess(command, 0, b"", b"")
            record = records[len(calls) - 2]
            returncode = 1 if len(calls) == 2 else 0
            return subprocess.CompletedProcess(command, returncode,
                                               (json.dumps(record) + "\n").encode(), b"")

        output = io.StringIO()
        with mock.patch.object(CHECK_ASSETS.shutil, "which", return_value="c++"), \
             mock.patch.object(CHECK_ASSETS.subprocess, "run", side_effect=fake_run), \
             contextlib.redirect_stdout(output):
            status = CHECK_ASSETS.run_checks(checks)

        self.assertEqual(status, 1)
        self.assertEqual(len(calls), 3)
        self.assertEqual(calls[0][0][0], "c++")
        self.assertEqual(calls[1][0][1:], [str(self.root / "ordinary.dat"),
                                            "--symbol", "fixture_joint"])
        self.assertEqual(calls[2][0][1:], [str(self.root / "stage.usd"),
                                            "--stage-entry", "3", "--opaque"])
        self.assertTrue(all("shell" not in kwargs for _, kwargs in calls))
        emitted = [json.loads(line) for line in output.getvalue().splitlines()]
        self.assertEqual([record["file"] for record in emitted],
                         [str(self.root / "ordinary.dat"), str(self.root / "stage.usd")])
        self.assertEqual([record["status"] for record in emitted], ["rejected", "accepted"])
        self.assertEqual(emitted[0]["selection"], {"symbol": "fixture_joint"})
        self.assertEqual(emitted[1]["selection"], {"stage_entry": 3, "opaque": True})

    def test_missing_input_is_actionable_without_compiling(self):
        checks = [{"path": self.root / "missing.dat"}]
        with mock.patch.object(CHECK_ASSETS.shutil, "which") as which, \
             mock.patch.object(CHECK_ASSETS.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "missing or not a regular file"):
                CHECK_ASSETS.run_checks(checks)
        which.assert_not_called()
        run.assert_not_called()


if __name__ == "__main__":
    unittest.main()
