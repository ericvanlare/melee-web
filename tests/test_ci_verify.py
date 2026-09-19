"""Focused tests for CI partition inventory and failure reporting."""

import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_verify


class CiVerifyTests(unittest.TestCase):
    def test_inventory_rejects_an_omitted_target(self):
        groups = dict(ci_verify.GROUPS)
        groups["fighter"] = ()
        with patch.object(ci_verify, "GROUPS", groups):
            with self.assertRaisesRegex(ValueError, "cover every all/fighter target"):
                ci_verify.check_inventory()

    def test_inventory_rejects_a_duplicate_target(self):
        groups = dict(ci_verify.GROUPS)
        groups["duplicate"] = groups["runtime"]
        with patch.object(ci_verify, "GROUPS", groups):
            with self.assertRaisesRegex(ValueError, "cover every all/fighter target"):
                ci_verify.check_inventory()

    def test_failure_subtest_error_and_skip_are_recorded_and_not_green(self):
        class MixedTests(unittest.TestCase):
            def test_failure(self):
                self.fail("expected failure")

            def test_subtest_error(self):
                with self.subTest(case="missing-build"):
                    raise RuntimeError("linked consumer could not start")

            def test_skip(self):
                self.skipTest("build output unavailable")

        suite = unittest.defaultTestLoader.loadTestsFromTestCase(MixedTests)
        result = unittest.TextTestRunner(
            stream=io.StringIO(), verbosity=0, resultclass=ci_verify.RecordingResult,
        ).run(suite)
        self.assertFalse(result.wasSuccessful())
        self.assertIn("failed", {outcome["status"] for outcome in result.outcomes})
        self.assertIn("skipped", {outcome["status"] for outcome in result.outcomes})
        self.assertTrue(any("missing-build" in outcome["test"] for outcome in result.outcomes))

    def _run_group_with_command_failure(self, *, fail_on_build):
        temporary = tempfile.TemporaryDirectory(prefix="melee ci verify ")
        root = Path(temporary.name)

        def run(command, **kwargs):
            is_build = "--build" in command
            if is_build == fail_on_build:
                raise subprocess.CalledProcessError(1, command)

        with patch.object(ci_verify, "ROOT", root), \
                patch.object(ci_verify, "check_inventory"), \
                patch.object(ci_verify.platform, "platform", return_value="test-platform"), \
                patch.object(ci_verify.subprocess, "check_output", return_value="test-sha\n"), \
                patch.object(ci_verify.subprocess, "run", side_effect=run):
            with self.assertRaises(subprocess.CalledProcessError):
                ci_verify.run_group("runtime", 2)

        report_path = root / "work/ci/runtime.json"
        self.assertTrue(report_path.is_file())
        report = json.loads(report_path.read_text(encoding="utf-8"))
        self.assertEqual(report["status"], "failed")
        self.assertIn("configure", [phase["name"] for phase in report["phases"]])
        if fail_on_build:
            self.assertIn("build", [phase["name"] for phase in report["phases"]])
        else:
            self.assertNotIn("build", [phase["name"] for phase in report["phases"]])
        temporary.cleanup()

    def test_configure_failure_propagates_and_retains_failed_report(self):
        self._run_group_with_command_failure(fail_on_build=False)

    def test_build_failure_propagates_and_retains_failed_report(self):
        self._run_group_with_command_failure(fail_on_build=True)

    def test_linked_consumer_skip_cannot_be_accepted(self):
        class SkippedConsumer(unittest.TestCase):
            def test_consumer(self):
                self.skipTest("build output unavailable")

        suite = unittest.defaultTestLoader.loadTestsFromTestCase(SkippedConsumer)
        expected_id = suite._tests[0].id()
        with patch.object(ci_verify, "LINKED_TESTS", {"gameplay": (expected_id,)}), \
                patch.object(ci_verify.unittest.TestLoader, "loadTestsFromNames", return_value=suite):
            report = {}
            with self.assertRaisesRegex(RuntimeError, "CI test partition failed|Required linked tests did not pass"):
                ci_verify.run_tests("gameplay", report)
        self.assertEqual(report["tests_run"], 1)
        self.assertEqual(report["tests"][0]["status"], "skipped")


if __name__ == "__main__":
    unittest.main()
