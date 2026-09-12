"""Durability and matrix tests for the bounded hitch evidence ledger."""

from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import hitch_capture as hitch_module  # noqa: E402
from hitch_capture import (  # noqa: E402
    HitchCaptureError,
    begin_attempt,
    create_plan,
    finish_attempt,
    load_plan,
    status,
)


class HitchCaptureTests(unittest.TestCase):
    def fixture(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        files = {}
        for name, text in (("runtime.wasm", "wasm"), ("runtime.js", "js"),
                           ("profile.json", "profile"), ("a822.mwrc", "a822 recipe"),
                           ("dca.mwrc", "dca recipe"), ("old-red.json", "old red")):
            path = root / name
            path.write_text(text, encoding="utf-8")
            files[name] = path
        spec = {
            "development_allowlist": ["a822", "dca"],
            "targets": [{"id": "a822", "label": "Fox/Marth DreamLand3892", "frames": 2},
                         {"id": "dca", "label": "Marth/Falco YS5588", "frames": 3}],
            "modes": ["unprofiled", "profiler"],
            "caches": ["cold", "warm"],
            "repetitions": 2,
            "identities": {
                "build_artifacts": [files["runtime.wasm"], files["runtime.js"]],
                "profile": files["profile.json"],
                "development_recipes": {"a822": files["a822.mwrc"], "dca": files["dca.mwrc"]},
                "legacy_red_reports": [files["old-red.json"]],
            },
            "attempts_dir": "attempts",
            "timeout_ms": 1234,
        }
        plan_path = root / "plan.json"
        plan = create_plan(spec, plan_path)
        return root, plan_path, plan, files

    def tearDown(self):
        if hasattr(self, "temporary"):
            self.temporary.cleanup()

    def report(self, root, *, native=0, browser=0, hard=0, passed=True):
        path = root / f"report-{native}-{browser}-{hard}-{passed}.json"
        path.write_text(json.dumps({
            "pass": passed,
            "metrics": {
                "nativeCallbacksOverBudget": native,
                "browserCallbackGaps": browser,
                "nativeCallbacksOver33ms": hard,
            },
        }), encoding="utf-8")
        return path

    def valid_browser_report(self, root, plan, files):
        path = root / "valid-browser-report.json"
        recipe_hash = hashlib.sha256(files["a822.mwrc"].read_bytes()).hexdigest()
        path.write_text(json.dumps({
            "schema": "melee-web-browser-retail-replay", "version": 1,
            "recipe_sha256": recipe_hash, "frames": 2, "mode": "performance",
            "complete": True, "pass": True, "failures": [], "gold_admitted": False,
            "pixels": "not_compared", "performance": "measured",
            "instrumented_timing_resumes": 0,
            "metrics": {
                "sourceFrames": 2, "browserCallbacks": 2, "nativeCallbacks": 2,
                "worstBrowserCallbackMs": 20, "worstNativeCallbackMs": 8,
                "nativeCallbacksOverBudget": 0, "browserCallbackGaps": 0, "browserLongTasks": 0,
                "browserLongTaskWorstMs": 0,
                "nativeCallbacksOver33ms": 0, "livePipelinesQueued": 0,
                "livePipelinesCreated": 0, "preparationPauses": 0,
                "audioUnderrunFrames": 0, "audioOverflowFrames": 0,
                "wasmHeapGrowthBytes": 0, "liveTextureUploadBytes": 0,
                "focusLost": False,
            },
            "preparation": {"total_ms": 1},
            "cache": {"cleared_on_startup": True, "driver_cache": "uncontrolled",
                      "state": "cleared", "bytes": 0},
            "user_agent": "test", "resolution": [640, 480], "device_pixel_ratio": 1,
            "diagnostic_capture": {
                "schema": "melee-web-diagnostic-hitch-capture", "version": 1,
                "enabled": True, "valid": True, "invalid": False,
                "overflowed": False, "overflow_count": 0, "cap": 128,
                "context_cap": 8, "event_count": 0, "observer_count": 0,
                "events": [], "observers": [],
            },
        }), encoding="utf-8")
        return path

    def test_plan_freezes_expected_development_matrix_and_explicit_allowlist(self):
        root, plan_path, plan, _ = self.fixture()
        self.assertEqual(len(plan["slots"]), 12)
        self.assertEqual([slot["mode"] for slot in plan["slots"][:8]],
                         ["unprofiled"] * 8)
        self.assertTrue(all(slot["target_id"] in {"a822", "dca"} for slot in plan["slots"]))
        self.assertEqual(plan["development_allowlist"], ["a822", "dca"])
        self.assertEqual(load_plan(plan_path)[0]["plan_id"], plan["plan_id"])
        with self.assertRaises(HitchCaptureError):
            create_plan({**copy.deepcopy({
                "development_allowlist": ["a822"], "targets": ["a822", "holdout"],
                "identities": {"build_artifacts": [], "profile": None,
                                "development_recipes": {}, "legacy_red_reports": []},
            })}, root / "bad.json")

    def test_attempt_is_exclusive_and_failed_completion_consumes_slot(self):
        root, plan_path, plan, files = self.fixture()
        started = begin_attempt(plan_path, plan["slots"][0]["slot_id"])
        self.assertTrue((Path(started["attempt_dir"]) / "start.json").is_file())
        report = self.report(root, native=2, browser=1, hard=1, passed=False)
        finished = finish_attempt(plan_path, plan["slots"][0]["slot_id"],
                                  report_path=report, attachments=[files["old-red.json"]],
                                  reason="retained red")
        self.assertTrue(finished["consumed"])
        with self.assertRaises(HitchCaptureError):
            begin_attempt(plan_path, plan["slots"][0]["slot_id"])
        current = status(plan_path)
        self.assertEqual(current["counts"]["failed_reports"], 1)
        self.assertEqual(current["legacy_unresolved_reds"], 1)
        self.assertEqual(current["performance_aggregate"]["native_over_1000_60"]["known_count"], 2)
        self.assertEqual(current["performance_aggregate"]["browser_over_1000_30"]["known_count"], 1)
        self.assertEqual(current["performance_aggregate"]["native_hard_over_33ms"]["known_count"], 1)
        self.assertEqual(current["causal_classification"], "NOT_CLASSIFIED")

    def test_fully_valid_unprofiled_report_is_retained_as_valid_but_legacy_red_keeps_admission_closed(self):
        root, plan_path, plan, files = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        report = self.valid_browser_report(root, plan, files)
        finish = finish_attempt(plan_path, slot, report_path=report)
        self.assertTrue(finish["validation"]["valid"])
        self.assertFalse(finish["acceptance_evidence"])
        current = status(plan_path)
        self.assertFalse(current["acceptance_evidence"])
        self.assertEqual(current["counts"]["failed_reports"], 0)

    def test_missing_deadlines_are_unknown_and_not_zero(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        report = root / "missing.json"
        report.write_text(json.dumps({"metrics": {"sourceFrames": 10}}), encoding="utf-8")
        finish_attempt(plan_path, slot, report_path=report)
        current = status(plan_path)
        for key in ("native_over_1000_60", "browser_over_1000_30", "native_hard_over_33ms"):
            metric = current["performance_aggregate"][key]
            self.assertIsNone(metric["count"])
            self.assertGreater(metric["unknown"], 0)
            self.assertEqual(metric["state"], "UNKNOWN")

    def test_foreign_or_incomplete_reports_are_retained_without_acceptance(self):
        root, plan_path, plan, files = self.fixture()
        for index, defect in enumerate(('recipe', 'frames', 'mode', 'cache', 'disabled', 'overflow')):
            slot = plan['slots'][index]
            report_path = self.valid_browser_report(root, plan, files)
            report = json.loads(report_path.read_text())
            report['recipe_sha256'] = plan['identities']['development_recipes'][slot['target_id']]['sha256']
            report['frames'] = next(t['frames'] for t in plan['targets'] if t['id'] == slot['target_id'])
            report['metrics']['sourceFrames'] = report['frames']
            cold = slot['cache'] == 'cold'
            report['cache'].update(cleared_on_startup=cold, state='cleared' if cold else 'ready', bytes=0 if cold else 100)
            if defect == 'recipe': report['recipe_sha256'] = '0' * 64
            if defect == 'frames': report['frames'] += 1
            if defect == 'mode': report['mode'] = 'state_capture'
            if defect == 'cache': report['cache']['cleared_on_startup'] = not cold
            if defect == 'disabled': report['diagnostic_capture']['enabled'] = False
            if defect == 'overflow': report['diagnostic_capture']['overflowed'] = True
            report_path.write_text(json.dumps(report))
            begin_attempt(plan_path, slot['slot_id'])
            finished = finish_attempt(plan_path, slot['slot_id'], report_path=report_path)
            with self.subTest(defect=defect):
                self.assertFalse(finished['validation']['valid'])
                self.assertFalse(finished['acceptance_evidence'])
                saved = Path(finished['attempt_dir']) / finished['report']['path']
                self.assertEqual(saved.read_bytes(), report_path.read_bytes())
        self.assertEqual(status(plan_path)['counts']['failed_reports'], 6)

    def test_profiled_measurements_do_not_enter_unprofiled_aggregate(self):
        root, plan_path, plan, _ = self.fixture()
        for slot in plan['slots']:
            begin_attempt(plan_path, slot['slot_id'])
            report_path = self.report(root, native=1 if slot['mode'] == 'unprofiled' else 5, passed=False)
            finish_attempt(plan_path, slot['slot_id'], report_path=report_path)
        current = status(plan_path)
        self.assertEqual(current['performance_aggregate']['native_over_1000_60']['known_count'], 8)
        self.assertEqual(current['performance_by_mode']['profiler']['native_over_1000_60']['known_count'], 20)
        self.assertFalse(current['acceptance_evidence'])

    def test_capped_event_samples_do_not_replace_authoritative_counters(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        report = root / "events.json"
        report.write_text(json.dumps({
            "schema": "melee-web-browser-retail-replay", "version": 1,
            "recipe_sha256": "wrong", "frames": 1, "mode": "performance",
            "complete": True, "pass": True, "failures": [],
            "gold_admitted": False, "pixels": "not_compared", "performance": "measured",
            "metrics": {
                "nativeCallbacks": 10, "browserCallbacks": 10,
                "nativeCallbacksOverBudget": 0, "browserCallbackGaps": 0,
                "nativeCallbacksOver33ms": 0, "worstNativeCallbackMs": 20,
                "worstBrowserCallbackMs": 20,
            },
            "diagnostic_capture": {
                "schema": "melee-web-diagnostic-hitch-capture", "version": 1,
                "enabled": True, "valid": True, "invalid": False,
                "overflowed": False, "overflow_count": 0, "events": [
                    {"id": "native-1", "kind": "native_deadline", "duration_ms": 20}
                ], "observers": [], "event_count": 1, "observer_count": 0,
                "cap": 128,
            },
        }), encoding="utf-8")
        finish_attempt(plan_path, slot, report_path=report)
        current = status(plan_path)
        metric = current["performance_aggregate"]["native_over_1000_60"]
        self.assertEqual(metric["known_count"], 0)
        self.assertEqual(metric["count"], 0)
        self.assertEqual(current["attempts"][0]["finish"]["performance_summary"][
            "native_over_1000_60"]["event_crosscheck"], "mismatch")

    def test_deadline_thresholds_are_fixed(self):
        root, _, plan, files = self.fixture()
        spec = {
            "development_allowlist": ["a822"], "targets": ["a822"],
            "thresholds": {"browser_gap_ms": 33.0},
            "identities": {"build_artifacts": [files["runtime.js"]],
                            "profile": files["profile.json"],
                            "development_recipes": {"a822": files["a822.mwrc"]},
                            "legacy_red_reports": []},
        }
        with self.assertRaisesRegex(HitchCaptureError, "fixed"):
            create_plan(spec, root / "bad-thresholds.json")

    def test_load_rejects_tampered_frozen_constraints(self):
        root, plan_path, plan, _ = self.fixture()
        original = plan_path.read_bytes()
        mutations = (
            ("threshold", lambda value: value["thresholds"].update(native_target_ms=1.0), "fixed"),
            ("mode", lambda value: value["slots"][0].update(mode="unfrozen"), "unfrozen mode"),
            ("cache", lambda value: value["slots"][0].update(cache="unfrozen"), "unfrozen cache"),
            ("ordinal", lambda value: value["slots"][0].update(ordinal=1), "ordinal is not sequential"),
            ("timeout", lambda value: value["slots"][0].update(timeout_ms=1), "timeout is not the frozen bound"),
        )
        for name, mutate, message in mutations:
            with self.subTest(name=name):
                value = json.loads(original)
                mutate(value)
                plan_path.write_text(json.dumps(value), encoding="utf-8")
                with self.assertRaisesRegex(HitchCaptureError, message):
                    status(plan_path)
                plan_path.write_bytes(original)
                self.assertEqual(status(plan_path)["planned"], len(plan["slots"]))

    def test_identity_change_blocks_reads_and_previous_red_is_not_erased(self):
        root, plan_path, plan, files = self.fixture()
        files["runtime.js"].write_text("changed", encoding="utf-8")
        with self.assertRaisesRegex(HitchCaptureError, "changed"):
            status(plan_path)
        files["runtime.js"].write_text("js", encoding="utf-8")
        begin_attempt(plan_path, plan["slots"][0]["slot_id"])
        report = self.report(root)
        finish_attempt(plan_path, plan["slots"][0]["slot_id"], report_path=report)
        files["old-red.json"].write_text("new red", encoding="utf-8")
        with self.assertRaisesRegex(HitchCaptureError, "changed"):
            status(plan_path)

    def test_crash_timeout_partial_evidence_consumes_slot(self):
        root, plan_path, plan, files = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        partial = root / "partial.json"
        partial.write_text(json.dumps({"partial": True, "diagnostic_capture": []}), encoding="utf-8")
        finish_attempt(plan_path, slot, status="timeout", report_path=partial,
                       attachments=[{"path": files["old-red.json"], "name": "legacy"}],
                       reason="runner deadline")
        current = status(plan_path)
        item = next(entry for entry in current["attempts"] if entry["slot_id"] == slot)
        self.assertEqual(item["state"], "timeout")
        self.assertTrue(item["consumed"])
        self.assertEqual(current["counts"]["timeout"], 1)

    def test_atomic_copy_failure_keeps_staging_and_retry_publishes_without_overwrite(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        report = self.report(root, passed=False)

        original_link = hitch_module.os.link

        def fail_report_publication(source, destination):
            if Path(destination).name.startswith("report-"):
                raise OSError("injected link failure")
            return original_link(source, destination)

        with patch.object(hitch_module.os, "link", side_effect=fail_report_publication):
            with self.assertRaisesRegex(HitchCaptureError, "publish attachment"):
                finish_attempt(plan_path, slot, report_path=report)

        attempt_dir = Path(status(plan_path)["attempts"][0]["attempt_dir"])
        staged = list((attempt_dir / "evidence").glob(".*.staging-*"))
        self.assertEqual(len(staged), 1)
        self.assertEqual(staged[0].read_bytes(), report.read_bytes())
        open_entry = status(plan_path)["attempts"][0]
        self.assertEqual(open_entry["state"], "open")
        self.assertEqual(len(open_entry["evidence_errors"]), 1)

        finished = finish_attempt(plan_path, slot, report_path=report)
        self.assertTrue(finished["consumed"])
        saved = attempt_dir / finished["report"]["path"]
        self.assertEqual(saved.read_bytes(), report.read_bytes())
        self.assertTrue(staged[0].is_file())
        final_entry = status(plan_path)["attempts"][0]
        self.assertEqual(len(final_entry["evidence_errors"]), 1)

    def test_start_record_write_failure_preserves_stage_and_explicit_close_is_recoverable(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        original_fsync = hitch_module.os.fsync
        calls = 0

        def fail_first_fsync(descriptor):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError("injected start fsync failure")
            return original_fsync(descriptor)

        with patch.object(hitch_module.os, "fsync", side_effect=fail_first_fsync):
            with self.assertRaisesRegex(HitchCaptureError, "write evidence"):
                begin_attempt(plan_path, slot)

        current = status(plan_path)
        entry = current["attempts"][0]
        self.assertEqual(entry["state"], "reserved_unstarted")
        attempt_dir = Path(entry["attempt_dir"])
        self.assertEqual(len(list(attempt_dir.glob(".start.json.staging-*"))), 1)
        self.assertNotIn("start", entry)
        with self.assertRaisesRegex(HitchCaptureError, "only interrupted"):
            finish_attempt(plan_path, slot, status="aborted")
        finish_attempt(plan_path, slot, status="interrupted", reason="start fsync failed")
        self.assertEqual(status(plan_path)["attempts"][0]["state"], "interrupted")

    def test_finish_record_publication_failure_preserves_stage_and_retry_reuses_evidence(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        begin_attempt(plan_path, slot)
        report = self.report(root, passed=False)
        original_link = hitch_module.os.link

        def fail_finish_publication(source, destination):
            if Path(destination).name == "finish.json":
                raise OSError("injected finish link failure")
            return original_link(source, destination)

        with patch.object(hitch_module.os, "link", side_effect=fail_finish_publication):
            with self.assertRaisesRegex(HitchCaptureError, "publish evidence"):
                finish_attempt(plan_path, slot, report_path=report)

        open_entry = status(plan_path)["attempts"][0]
        self.assertEqual(open_entry["state"], "open")
        attempt_dir = Path(open_entry["attempt_dir"])
        staged_finish = list(attempt_dir.glob(".finish.json.staging-*"))
        self.assertEqual(len(staged_finish), 1)
        saved_report = attempt_dir / "evidence" / ("report-" + report.name)
        self.assertEqual(saved_report.read_bytes(), report.read_bytes())

        finished = finish_attempt(plan_path, slot, report_path=report)
        self.assertTrue(finished["consumed"])
        self.assertTrue(staged_finish[0].is_file())
        self.assertEqual(status(plan_path)["attempts"][0]["state"], "completed")

    def test_start_publication_failure_leaves_reserved_directory_for_explicit_interruption(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        original_write = hitch_module._write_exclusive

        def fail_start(path, payload):
            if path.name == "start.json":
                raise HitchCaptureError("injected start publication failure")
            return original_write(path, payload)

        with patch.object(hitch_module, "_write_exclusive", side_effect=fail_start):
            with self.assertRaisesRegex(HitchCaptureError, "start publication"):
                begin_attempt(plan_path, slot)

        current = status(plan_path)
        entry = current["attempts"][0]
        self.assertEqual(entry["state"], "reserved_unstarted")
        self.assertEqual(current["counts"]["reserved"], 1)
        self.assertNotIn("start", entry)
        with self.assertRaisesRegex(HitchCaptureError, "already has an attempt"):
            begin_attempt(plan_path, slot)

        finished = finish_attempt(plan_path, slot, status="interrupted",
                                  reason="start publication failed")
        self.assertTrue(finished["consumed"])
        self.assertTrue(finished["reservation_only"])
        self.assertIsNone(finished["attempt_id"])
        closed = status(plan_path)["attempts"][0]
        self.assertEqual(closed["state"], "interrupted")
        self.assertNotIn("start", closed)
        with self.assertRaisesRegex(HitchCaptureError, "already has an attempt"):
            begin_attempt(plan_path, slot)

    def test_oversize_and_unreadable_report_failures_are_sealed_as_evidence(self):
        root, plan_path, plan, _ = self.fixture()
        slot = plan["slots"][0]["slot_id"]
        oversized = root / "oversized.bin"
        oversized.write_bytes(b"x" * 33)
        begin_attempt(plan_path, slot)
        with patch.object(hitch_module, "MAX_ATTACHMENT_BYTES", 32):
            with self.assertRaisesRegex(HitchCaptureError, "exceeds 32 byte limit"):
                finish_attempt(plan_path, slot, status="aborted",
                               attachments=[oversized])
        open_entry = status(plan_path)["attempts"][0]
        self.assertEqual(len(open_entry["evidence_errors"]), 1)
        self.assertIn("exceeds 32 byte limit", open_entry["evidence_errors"][0]["error"])

        unreadable = root / "unreadable.json"
        json_limit = plan_path.stat().st_size + 1
        unreadable.write_bytes(b"{" + b"x" * (json_limit + 100))
        with patch.object(hitch_module, "MAX_JSON_BYTES", json_limit):
            finished = finish_attempt(plan_path, slot, status="aborted",
                                      report_path=unreadable)
        self.assertTrue(finished["consumed"])
        closed = status(plan_path)["attempts"][0]
        self.assertEqual(len(closed["evidence_errors"]), 2)
        self.assertTrue(any("exceeds" in item["error"]
                            for item in closed["evidence_errors"]))

    def test_open_or_out_of_order_slot_is_not_skippable_and_record_seal_is_verified(self):
        root, plan_path, plan, _ = self.fixture()
        first, second = plan["slots"][0]["slot_id"], plan["slots"][1]["slot_id"]
        begin_attempt(plan_path, first)
        with self.assertRaisesRegex(HitchCaptureError, "open"):
            begin_attempt(plan_path, second)
        finish_attempt(plan_path, first, status="interrupted", reason="operator stopped")
        begin_attempt(plan_path, second)
        start_path = Path(status(plan_path)["attempts"][1]["attempt_dir"]) / "start.json"
        start_path.write_text(start_path.read_text(encoding="utf-8").replace("slot-0002", "tampered"),
                              encoding="utf-8")
        with self.assertRaisesRegex(HitchCaptureError, "modified"):
            status(plan_path)

    def test_cli_emits_json_only_and_rejects_duplicate_start(self):
        root, plan_path, plan, _ = self.fixture()
        cli = ROOT / "scripts" / "hitch_capture.py"
        slot = plan["slots"][0]["slot_id"]
        result = subprocess.run([sys.executable, str(cli), "start", "--plan", str(plan_path),
                                 "--slot", slot], capture_output=True, text=True, check=True)
        value = json.loads(result.stdout)
        self.assertTrue(value["ok"])
        duplicate = subprocess.run([sys.executable, str(cli), "start", "--plan", str(plan_path),
                                     "--slot", slot], capture_output=True, text=True)
        self.assertEqual(duplicate.returncode, 2)
        self.assertEqual(duplicate.stdout, "")


if __name__ == "__main__":
    unittest.main()
