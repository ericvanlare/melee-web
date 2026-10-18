# SPDX-License-Identifier: GPL-3.0-or-later
"""Controls for stable CI identities of the isolated profile test cohort."""

from collections import Counter
import hashlib
import importlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
import ci_aggregate
import ci_verify


PROFILE_TEST_CLASSES = (
    ("src.slippi_profile.tests.test_build_contract", "SlippiSynctestBuildContractTests"),
    ("src.slippi_profile.tests.test_profile_controls", "ProfileControlsTests"),
    ("src.slippi_profile.tests.test_offscreen_timer_block", "OffscreenTimerBlockTests"),
    ("src.slippi_profile.tests.test_deadupfall_reduction", "DeadUpFallReductionTests"),
    ("src.slippi_profile.tests.test_runner_contract", "SlippiSynctestRunnerContractTests"),
    ("src.slippi_profile.tests.test_source_composition", "SlippiSourceCompositionTests"),
    ("src.slippi_profile.tests.test_stage_render_reductions", "StageRenderReductionTests"),
    ("src.slippi_profile.tests.test_dbooc_binary32_oracle", "DboocBinary32OracleTests"),
    ("src.slippi_profile.tests.test_dbooc_scalar_helper", "DboocScalarHelperTests"),
)


def _test_cases(suite):
    for test in suite:
        if isinstance(test, unittest.TestSuite):
            yield from _test_cases(test)
        else:
            yield test


def _digest(ids):
    return hashlib.sha256("\n".join(sorted(ids)).encode("utf-8")).hexdigest()


class SlippiDiscoveryContractTests(unittest.TestCase):
    def test_forwarded_profile_tests_keep_exact_flat_ci_identity(self):
        profile_specs = PROFILE_TEST_CLASSES + (
            (__name__, "SlippiDiscoveryContractTests"),
        )
        profile_classes = [
            (module_name, class_name, getattr(importlib.import_module(module_name), class_name))
            for module_name, class_name in profile_specs
        ]

        discovered = unittest.TestLoader().discover(str(ROOT / "tests"))
        cases = list(ci_verify._test_cases(discovered))
        expected = Counter()
        for module_name, class_name, test_class in profile_classes:
            for method_name in unittest.TestLoader().getTestCaseNames(test_class):
                expected[(module_name, class_name, method_name)] += 1

        profile_cases = []
        observed = Counter()
        for case in cases:
            matches = [
                (module_name, class_name)
                for module_name, class_name, test_class in profile_classes
                if isinstance(case, test_class)
            ]
            if not matches:
                continue
            self.assertEqual(len(matches), 1, case.id())
            module_name, class_name = matches[0]
            observed[(module_name, class_name, case._testMethodName)] += 1
            test_module = ci_verify._test_module(case)
            id_module = case.id().split(".", 1)[0]
            self.assertEqual(test_module, id_module, case.id())
            self.assertEqual(test_module, "test_slippi_profile_controls_discovery")

            case_id = case.id()
            self.assertEqual(sum(other.id() == case_id for other in cases), 1)
            profile_cases.append(case)

        self.assertEqual(observed, expected)
        profile_ids = [case.id() for case in profile_cases]
        self.assertTrue(profile_ids)
        self.assertEqual(len(profile_ids), len(set(profile_ids)))

        group_ids = {}
        for group in ci_verify.UNIT_GROUPS:
            shard = ci_verify._shard_index(group)
            group_suite = ci_verify._filter_suite(discovered, shard)
            group_ids[group] = [
                case.id() for case in ci_verify._test_cases(group_suite)
                if case.id() in set(profile_ids)
            ]
        selected_union = [test_id for ids in group_ids.values() for test_id in ids]
        self.assertEqual(Counter(selected_union), Counter({test_id: 1 for test_id in profile_ids}))

        for case in profile_cases:
            module_shard = ci_verify.unit_shard(ci_verify._test_module(case))
            aggregate_shard = ci_verify.unit_shard(case.id().split(".", 1)[0])
            self.assertEqual(module_shard, aggregate_shard, case.id())
            owner_group = ci_verify.UNIT_GROUPS[module_shard]
            self.assertIn(case.id(), group_ids[owner_group])

        # Exercise the production aggregate's test-id prefix audit on this exact cohort.
        assignments = {group: list(ids) for group, ids in group_ids.items()}
        with tempfile.TemporaryDirectory(prefix="slippi-ci-discovery-") as directory:
            reports = Path(directory) / "valid"
            self._write_aggregate_reports(reports, profile_ids, assignments)
            group_names = tuple(ci_verify.UNIT_GROUPS)
            with patch.multiple(
                ci_aggregate,
                GROUPS={group: () for group in group_names},
                LINKED_BUILD_TARGETS={},
                UNIT_GROUPS=group_names,
                REHOMED_TEST_MODULES={},
                REQUIRED_TESTS={},
            ):
                result = ci_aggregate.validate_reports(reports, "discovery-control")
                self.assertEqual(result["status"], "passed")

                wrong_assignments = {group: list(ids) for group, ids in assignments.items()}
                first_id = profile_ids[0]
                correct_group = ci_verify.UNIT_GROUPS[
                    ci_verify.unit_shard(first_id.split(".", 1)[0])
                ]
                wrong_group = next(group for group in group_names if group != correct_group)
                wrong_assignments[correct_group].remove(first_id)
                wrong_assignments[wrong_group].append(first_id)
                invalid_reports = Path(directory) / "misassigned"
                self._write_aggregate_reports(invalid_reports, profile_ids, wrong_assignments)
                with self.assertRaisesRegex(ValueError, "assigned to another shard"):
                    ci_aggregate.validate_reports(invalid_reports, "discovery-control")

    @staticmethod
    def _write_aggregate_reports(directory, discovered_ids, selected_by_group):
        directory.mkdir(parents=True)
        discovered = {"count": len(discovered_ids), "sha256": _digest(discovered_ids)}
        for group, selected_ids in selected_by_group.items():
            report = {
                "status": "passed",
                "group": group,
                "commit": "discovery-control",
                "targets": [],
                "discovered": discovered,
                "selected": {
                    "count": len(selected_ids),
                    "sha256": _digest(selected_ids),
                    "ids": selected_ids,
                },
                "deferred": {"count": 0, "sha256": _digest([]), "ids": []},
            }
            (directory / f"{group}.json").write_text(
                json.dumps(report), encoding="utf-8",
            )
