"""Focused tests for aggregate CI partition evidence."""

import hashlib
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0, str(ROOT / "scripts"))
import ci_aggregate
import ci_verify


class CiAggregateTests(unittest.TestCase):
    commit = "expected-commit"

    def _write_reports(self, *, selected=None, deferred=None, discovered_ids=None, overrides=None):
        temporary = tempfile.TemporaryDirectory(prefix="melee ci aggregate ")
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        overrides = overrides or {}
        selected = selected or {group: [] for group in ci_aggregate.UNIT_GROUPS}
        if deferred is None:
            deferred = {group: [] for group in ci_aggregate.UNIT_GROUPS}
            for owner, modules in ci_verify.REHOMED_TEST_MODULES.items():
                for test_id in ci_verify.REQUIRED_TESTS[owner]:
                    if test_id.split(".", 1)[0] not in modules:
                        continue
                    group = ci_verify.UNIT_GROUPS[
                        ci_verify.unit_shard(test_id.split(".", 1)[0])
                    ]
                    deferred[group].append(test_id)
        discovered_ids = discovered_ids if discovered_ids is not None else sorted(
            set().union(*(set(ids) for ids in (*selected.values(), *deferred.values())))
        )
        discovered_hash = hashlib.sha256(
            "\n".join(sorted(discovered_ids)).encode("utf-8")
        ).hexdigest()
        for group, targets in ci_aggregate.GROUPS.items():
            targets = list(targets) + list(ci_verify.LINKED_BUILD_TARGETS.get(group, ()))
            if group in ci_aggregate.UNIT_GROUPS:
                ids = list(selected[group])
                deferred_ids = list(deferred[group])
                report = {
                    "status": "passed",
                    "group": group,
                    "commit": self.commit,
                    "targets": list(targets),
                    "discovered": {"count": len(discovered_ids), "sha256": discovered_hash},
                    "selected": {
                        "count": len(ids),
                        "sha256": hashlib.sha256("\n".join(sorted(ids)).encode("utf-8")).hexdigest(),
                        "ids": ids,
                    },
                    "deferred": {
                        "count": len(deferred_ids),
                        "sha256": hashlib.sha256(
                            "\n".join(sorted(deferred_ids)).encode("utf-8")
                        ).hexdigest(),
                        "ids": deferred_ids,
                    },
                }
            else:
                rehomed_modules = set(ci_verify.REHOMED_TEST_MODULES.get(group, ()))
                linked_ids = [
                    test_id for test_id in ci_verify.REQUIRED_TESTS.get(group, ())
                    if test_id.split(".", 1)[0] in rehomed_modules
                ]
                report = {
                    "status": "passed",
                    "group": group,
                    "commit": self.commit,
                    "targets": list(targets),
                }
                if group in ci_verify.REHOMED_TEST_MODULES:
                    linked_ids.extend(ci_verify.REQUIRED_TESTS[group])
                    linked_ids = sorted(set(linked_ids))
                    report["selected"] = {
                        "count": len(linked_ids),
                        "sha256": hashlib.sha256(
                            "\n".join(linked_ids).encode("utf-8")
                        ).hexdigest(),
                        "ids": linked_ids,
                    }
                    report["tests"] = [
                        {"test": test_id, "status": "passed"}
                        for test_id in ci_verify.REQUIRED_TESTS[group]
                    ]
            report.update(overrides.get(group, {}))
            (directory / f"{group}.json").write_text(json.dumps(report), encoding="utf-8")
        return directory

    def test_good_reports_pass_and_return_summary(self):
        unit0 = self._id_for_shard(0, "unit_case_zero")
        unit1 = self._id_for_shard(1, "unit_case_one")
        reports = self._write_reports(
            selected={"unit-0": [unit0], "unit-1": [unit1]},
        )
        summary = ci_aggregate.validate_reports(reports, self.commit)
        self.assertEqual(summary["status"], "passed")
        self.assertEqual(summary["discovered"]["count"], 8)

    @staticmethod
    def _id_for_shard(shard, suffix):
        index = 0
        while True:
            module = f"test_{suffix}_{index}"
            if ci_verify.unit_shard(module) == shard:
                return f"{module}.Case.test_value"
            index += 1

    def test_empty_discovery_is_rejected(self):
        reports = self._write_reports(
            deferred={"unit-0": [], "unit-1": []}, discovered_ids=[],
        )
        with self.assertRaisesRegex(ValueError, "discovered inventory count"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_different_discovery_with_same_count_is_rejected(self):
        unit0 = self._id_for_shard(0, "discovery_zero")
        unit1 = self._id_for_shard(1, "discovery_one")
        reports = self._write_reports(
            selected={"unit-0": [unit0], "unit-1": [unit1]},
        )
        path = reports / "unit-1.json"
        report = json.loads(path.read_text())
        report["discovered"]["sha256"] = hashlib.sha256(
            b"test_a.A.test_one\ntest_c.C.test_three"
        ).hexdigest()
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "discovered inventories do not match"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_missing_selected_case_is_rejected(self):
        reports = self._write_reports(
            selected={"unit-0": [self._id_for_shard(0, "missing_zero")], "unit-1": []},
            discovered_ids=[self._id_for_shard(0, "missing_zero"),
                            self._id_for_shard(1, "missing_one")],
        )
        with self.assertRaisesRegex(ValueError, "union count"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_overlapping_selected_case_is_rejected(self):
        case = self._id_for_shard(0, "overlap")
        reports = self._write_reports(
            selected={"unit-0": [case], "unit-1": [case]},
        )
        with self.assertRaisesRegex(ValueError, "overlap"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_rehomed_module_must_be_selected_by_its_owner(self):
        reports = self._write_reports()
        module = "test_gameplay_collision"
        shard = ci_verify.UNIT_GROUPS[ci_verify.unit_shard(module)]
        path = reports / f"{shard}.json"
        report = json.loads(path.read_text())
        report["deferred"]["ids"] = [
            test_id for test_id in report["deferred"]["ids"]
            if test_id.split(".", 1)[0] != module
        ]
        ids = sorted(report["deferred"]["ids"])
        report["deferred"]["count"] = len(ids)
        report["deferred"]["sha256"] = ci_aggregate._canonical_hash(ids)
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "not selected exactly once"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_rehomed_required_case_must_pass_in_owner(self):
        reports = self._write_reports()
        path = reports / "gameplay.json"
        report = json.loads(path.read_text())
        required_id = next(
            test_id for test_id in ci_verify.REQUIRED_TESTS["gameplay"]
            if test_id.startswith("test_gameplay_collision.")
        )
        for outcome in report["tests"]:
            if outcome["test"] == required_id:
                outcome["status"] = "skipped"
        path.write_text(json.dumps(report))
        with self.assertRaisesRegex(ValueError, "did not pass required tests"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_stale_commit_is_rejected(self):
        reports = self._write_reports()
        with self.assertRaisesRegex(ValueError, "stale commit"):
            ci_aggregate.validate_reports(reports, "different-commit")

    def test_failed_build_group_is_rejected(self):
        reports = self._write_reports(overrides={"runtime": {"status": "failed"}})
        with self.assertRaisesRegex(ValueError, "did not pass"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_missing_partition_report_is_rejected(self):
        reports = self._write_reports()
        (reports / "unit-1.json").unlink()
        with self.assertRaisesRegex(ValueError, "missing partition report"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_target_list_mismatch_is_rejected(self):
        reports = self._write_reports(overrides={"runtime": {"targets": ["wrong"]}})
        with self.assertRaisesRegex(ValueError, "target list"):
            ci_aggregate.validate_reports(reports, self.commit)


if __name__ == "__main__":
    unittest.main()
