"""Check report ownership on failures without impersonating a successful compiler."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_census import census, publish_report


class GameplayCensusTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="melee census report ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / "build/gameplay-census"
        self.output.mkdir(parents=True)
        self.report_path = self.output / "report.json"
        self.old_report = {"status": "complete", "compiled_count": 1, "old_run": True}
        publish_report(self.report_path, self.old_report)
        self.object_path = self.output / "objects/melee/fixture.c.o"
        self.object_path.parent.mkdir(parents=True)
        self.object_path.write_bytes(b"previous run")

    def test_invalid_link_root_rejects_before_preparation_or_output_mutation(self):
        for name in ("", "bad-name", "_unicode_\u00e9", "symbol,other"):
            with self.subTest(name=name), patch("gameplay_census.prepare_sources") as prepare:
                with self.assertRaisesRegex(ValueError, "C symbol"):
                    census(self.root, ["melee/fixture.c"], 1, name)
                prepare.assert_not_called()
                self.assertEqual(json.loads(self.report_path.read_text()), self.old_report)
                self.assertEqual(self.object_path.read_bytes(), b"previous run")

    def test_failed_atomic_publication_preserves_valid_previous_json(self):
        with patch("gameplay_census.os.replace", side_effect=OSError("publication failed")):
            with self.assertRaisesRegex(OSError, "publication failed"):
                publish_report(self.report_path, {"status": "in_progress"})
        self.assertEqual(json.loads(self.report_path.read_text()), self.old_report)
        self.assertEqual(list(self.output.glob(".report-*")), [])

    def test_timeout_and_interruption_never_retain_stale_success(self):
        # Only dependency setup is isolated. The compiler call always raises;
        # no successful object, symbol output or link is fabricated in this test.
        version = self.root / ".deps/emsdk/upstream/emscripten/emscripten-version.txt"
        version.parent.mkdir(parents=True)
        version.write_text("1.2.3")
        inputs = ("patches/melee-gameplay.patch", "patches/aurora-browser.patch",
                  "src/gameplay_compat.h", "src/hsd_probe_compat.h")
        for name in inputs:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("Authored provenance input for " + name)
        for exception, status in ((subprocess.TimeoutExpired("compiler", 120), "failed"),
                                  (KeyboardInterrupt(), "interrupted")):
            with self.subTest(status=status):
                publish_report(self.report_path, self.old_report)
                self.object_path.write_bytes(b"previous run")

                def interrupted_tool(*args, **kwargs):
                    pending = json.loads(self.report_path.read_text())
                    self.assertEqual(pending["status"], "in_progress")
                    self.assertIsNone(pending["compiled_count"])
                    self.assertNotIn("old_run", pending)
                    raise exception

                with patch("gameplay_census.read_lock", return_value={"emscripten": "1.2.3"}), \
                     patch("gameplay_census.verify_sources"), \
                     patch("gameplay_census.prepare_sources", return_value=self.root / "build/gameplay-source/src"), \
                     patch("gameplay_census.subprocess.run", side_effect=interrupted_tool):
                    with self.assertRaises(type(exception)):
                        census(self.root, ["melee/fixture.c"], 1, "fixture")
                report = json.loads(self.report_path.read_text())
                self.assertEqual(report["status"], status)
                self.assertEqual(report["failure"]["type"], type(exception).__name__)
                self.assertFalse(report["runtime_validated"])
                self.assertEqual(report["units"], [])
                self.assertEqual(report["requested_link_root"], "fixture")
                self.assertIsNone(report["compiled_count"])
                self.assertFalse(self.object_path.exists())
                self.assertNotIn(str(self.root), self.report_path.read_text())
                for name in inputs:
                    self.assertEqual(report["provenance"]["sha256"][name],
                                     hashlib.sha256((self.root / name).read_bytes()).hexdigest())
                command = report["provenance"]["compiler_command"]
                self.assertIn("-ffp-contract=off", command)
                self.assertIn("${ROOT}/src/gameplay_compat.h", command)


if __name__ == "__main__":
    unittest.main()
