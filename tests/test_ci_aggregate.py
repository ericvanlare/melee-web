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


class CiAggregateTests(unittest.TestCase):
    commit = "expected-commit"

    def _write_reports(self, *, selected=None, discovered_ids=None, overrides=None):
        temporary = tempfile.TemporaryDirectory(prefix="melee ci aggregate ")
        self.addCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        overrides = overrides or {}
        selected = selected or {group: [] for group in ci_aggregate.UNIT_GROUPS}
        discovered_ids = discovered_ids if discovered_ids is not None else sorted(
            set().union(*(set(ids) for ids in selected.values()))
        )
        discovered_hash = hashlib.sha256(
            "\n".join(sorted(discovered_ids)).encode("utf-8")
        ).hexdigest()
        for group, targets in ci_aggregate.GROUPS.items():
            if group in ci_aggregate.UNIT_GROUPS:
                ids = list(selected[group])
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
                }
            else:
                report = {
                    "status": "passed",
                    "group": group,
                    "commit": self.commit,
                    "targets": list(targets),
                }
            report.update(overrides.get(group, {}))
            (directory / f"{group}.json").write_text(json.dumps(report), encoding="utf-8")
        return directory

    def test_good_reports_pass_and_return_summary(self):
        reports = self._write_reports(
            selected={"unit-0": ["test_a.A.test_one"], "unit-1": ["test_b.B.test_two"]},
        )
        summary = ci_aggregate.validate_reports(reports, self.commit)
        self.assertEqual(summary["status"], "passed")
        self.assertEqual(summary["discovered"]["count"], 2)

    def test_empty_discovery_is_rejected(self):
        reports = self._write_reports()
        with self.assertRaisesRegex(ValueError, "discovered inventory count"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_missing_selected_case_is_rejected(self):
        reports = self._write_reports(
            selected={"unit-0": ["test_a.A.test_one"], "unit-1": []},
            discovered_ids=["test_a.A.test_one", "test_b.B.test_two"],
        )
        with self.assertRaisesRegex(ValueError, "union count"):
            ci_aggregate.validate_reports(reports, self.commit)

    def test_overlapping_selected_case_is_rejected(self):
        reports = self._write_reports(
            selected={"unit-0": ["test_a.A.test_one"], "unit-1": ["test_a.A.test_one"]},
        )
        with self.assertRaisesRegex(ValueError, "overlap"):
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
