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
    def test_unit_shards_cover_discovered_suite_without_overlap(self):
        loader = unittest.TestLoader()
        discovered = loader.discover(str(ROOT / "tests"))
        all_ids = {test.id() for test in ci_verify._test_cases(discovered)}
        selected = {
            group: ci_verify._filter_suite(discovered, ci_verify._shard_index(group))
            for group in ci_verify.UNIT_GROUPS
        }
        shard_ids = {
            group: {test.id() for test in ci_verify._test_cases(suite)}
            for group, suite in selected.items()
        }
        self.assertEqual(set().union(*shard_ids.values()), all_ids)
        self.assertEqual(shard_ids["unit-0"] & shard_ids["unit-1"], set())

    def test_unit_shards_keep_each_module_cohort_together(self):
        loader = unittest.TestLoader()
        discovered = loader.discover(str(ROOT / "tests"))
        all_cases = list(ci_verify._test_cases(discovered))
        by_module = {}
        for test in all_cases:
            by_module.setdefault(ci_verify._test_module(test), set()).add(test.id())
        selected = {
            group: {test.id() for test in ci_verify._test_cases(
                ci_verify._filter_suite(discovered, ci_verify._shard_index(group))
            )}
            for group in ci_verify.UNIT_GROUPS
        }
        for module, ids in by_module.items():
            owners = {group for group, shard_ids in selected.items() if ids & shard_ids}
            self.assertEqual(owners, {ci_verify.UNIT_GROUPS[ci_verify.unit_shard(module)]})

    def test_unit_shards_include_new_test_module_without_dropping_cases(self):
        class NewModuleTests(unittest.TestCase):
            __module__ = "test_new_ci_module"

            def test_first(self):
                pass

            def test_second(self):
                pass

        suite = unittest.TestSuite([NewModuleTests("test_first"), NewModuleTests("test_second")])
        all_ids = {test.id() for test in ci_verify._test_cases(suite)}
        shard_ids = {
            group: {
                test.id() for test in ci_verify._test_cases(
                    ci_verify._filter_suite(suite, ci_verify._shard_index(group))
                )
            }
            for group in ci_verify.UNIT_GROUPS
        }
        self.assertEqual(set().union(*shard_ids.values()), all_ids)
        self.assertEqual(sum(bool(ids) for ids in shard_ids.values()), 1)
        owner = ci_verify.UNIT_GROUPS[ci_verify.unit_shard("test_new_ci_module")]
        self.assertEqual(shard_ids[owner], all_ids)

    def test_unit_run_reports_discovered_and_selected_inventory(self):
        class InventoryTests(unittest.TestCase):
            __module__ = "test_inventory_module"

            def test_first(self):
                pass

            def test_second(self):
                pass

        discovered = unittest.TestSuite([
            InventoryTests("test_first"), InventoryTests("test_second"),
        ])
        report = {}
        with patch.object(ci_verify.unittest.TestLoader, "discover", return_value=discovered):
            ci_verify.run_tests("unit-0", report)
        self.assertEqual(report["discovered"]["count"], 2)
        expected = 2 if ci_verify.unit_shard("test_inventory_module") == 0 else 0
        self.assertEqual(report["selected"]["count"], expected)
        self.assertIn("sha256", report["discovered"])
        self.assertIn("sha256", report["selected"])

    def test_inventory_requires_both_unit_shards(self):
        groups = dict(ci_verify.GROUPS)
        del groups["unit-1"]
        with patch.object(ci_verify, "GROUPS", groups):
            with self.assertRaisesRegex(ValueError, "both unit-0 and unit-1"):
                ci_verify.check_inventory()

    def test_inventory_rejects_an_omitted_target(self):
        groups = dict(ci_verify.GROUPS)
        groups["build"] = groups["build"][:-1]
        with patch.object(ci_verify, "GROUPS", groups):
            with self.assertRaisesRegex(ValueError, "cover every all/fighter target"):
                ci_verify.check_inventory()

    def test_inventory_rejects_a_duplicate_target(self):
        groups = dict(ci_verify.GROUPS)
        groups["duplicate"] = groups["build"]
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
                ci_verify.run_group("build", 3)

        report_path = root / "work/ci/build.json"
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
        with patch.object(ci_verify, "LINKED_TESTS", {"build": (expected_id,)}), \
                patch.object(ci_verify.unittest.TestLoader, "loadTestsFromNames", return_value=suite):
            report = {}
            with self.assertRaisesRegex(RuntimeError, "CI test partition failed|Required linked tests did not pass"):
                ci_verify.run_tests("build", report)
        self.assertEqual(report["tests_run"], 1)
        self.assertEqual(report["tests"][0]["status"], "skipped")


if __name__ == "__main__":
    unittest.main()
