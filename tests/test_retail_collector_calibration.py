"""Focused gates for calibrating a changed retail collector."""

from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import hashlib


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


VALIDATION = _load("retail_validation_for_calibration_tests",
                   ROOT / "tools" / "retail_replay_validation.py")
CALIBRATION = _load("retail_calibration_for_tests",
                    ROOT / "tools" / "retail_collector_calibration.py")

_capture_counter = 0


def _new_capture_id():
    global _capture_counter
    value = "00000000000040008000000000000000"
    value = value[:-4] + f"{_capture_counter:04x}"
    _capture_counter += 1
    return value


def _fighter(slot):
    return {
        "slot": slot, "kind": 0, "motion": 14, "animation": 1,
        "facing_bits": "3f800000",
        "position_bits": ["00000000", "3f800000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "ground_air": 0, "animation_frame_bits": "00000000",
        "animation_speed_bits": "3f800000", "damage_bits": "00000000",
        "shield_bits": "00000000", "stocks": 4, "input_hex": "00" * 0x6C,
    }


def _state(scene, match, rng):
    return {"rng": rng, "scene_frame": scene, "match_frame": match,
            "fighters": [_fighter(0), _fighter(1)]}


def candidate(frame_count=3, capture_id=None, *, collector="dd" * 32,
             cpu="Interpreter64"):
    if capture_id is None:
        capture_id = _new_capture_id()
    provenance = dict(VALIDATION.EXPECTED_PROVENANCE)
    provenance["cpu"] = cpu
    if cpu == "JITARM64":
        provenance["experimental_reference_backend"] = True
    rows = [{
        "record": "header", "schema": VALIDATION.SCHEMA,
        "version": VALIDATION.VERSION, "phase": VALIDATION.PHASE,
        "input_phase": VALIDATION.INPUT_PHASE,
        "initial_phase": VALIDATION.INITIAL_PHASE,
        "game_revision": VALIDATION.GAME_REVISION,
        "frames_requested": frame_count,
        "provenance": provenance, "collector_sha256": collector,
        "writes_game_state": False, "capture_id": capture_id,
    }, {
        "record": "match_enter", "rng": 0x12345678,
        "start_melee_hex": "01" * 0x138,
        "pad_lib_hex": "02" * 0x20,
        "pad_master_hex": "03" * 0x110,
        "pad_game_hex": "04" * 0x110,
    }, {"record": "match_enter_complete", **_state(999, 0, 0x12345678)}]
    for index in range(frame_count):
        rows.append({
            "record": "frame", "index": index,
            "consumed_inputs": [["10" * 11, "20" * 11,
                                  "30" * 11, "40" * 11]],
            **_state(index, 0 if index < 2 else 1, 0x12345679 + index),
        })
    rows.append({"record": "end", "frames": frame_count, "status": "captured"})
    return rows


def _write(path, rows):
    path.write_text("\n".join(json.dumps(row, sort_keys=True,
                                         separators=(",", ":"))
                               for row in rows) + "\n", encoding="utf-8")


class RetailCollectorCalibrationTests(unittest.TestCase):
    def test_dequeued_input_phase_can_be_calibrated_against_entry_queue_pair(self):
        rows = candidate(collector='ee' * 32, capture_id=_new_capture_id())
        rows[0]['input_phase'] = VALIDATION.DEQUEUED_INPUT_PHASE
        with tempfile.TemporaryDirectory() as directory:
            first, second, path = self._paths(directory, candidate_rows=rows)
            report = CALIBRATION.calibrate(first, second, path)
            self.assertEqual(report['status'], 'collector_calibrated')

    def test_cli_cannot_overwrite_input_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            original = first.read_bytes()
            with self.assertRaises(SystemExit) as caught:
                CALIBRATION.cli_main(['--reference-a', str(first), '--reference-b', str(second),
                                      '--candidate', str(candidate_path), '--output', str(first)])
            self.assertEqual(caught.exception.code, 2)
            self.assertEqual(first.read_bytes(), original)

    def test_hashes_bind_validated_bytes_and_late_difference_ignores_hex_case(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            with mock.patch.object(CALIBRATION, '_read_hash', return_value='00' * 32):
                report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report['capture_hashes']['candidate'],
                             hashlib.sha256(candidate_path.read_bytes()).hexdigest())
            diff = CALIBRATION._difference({'a': 'aa', 'b': 1}, {'a': 'AA', 'b': 2}, 'test')
            self.assertEqual(diff['field'], 'test.b')

    def test_reference_pair_optional_provenance_must_match(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            rows = [json.loads(v) for v in second.read_text().splitlines()]
            rows[0]['provenance']['setup'] = 'different menu checkpoint'
            second.write_text('\n'.join(json.dumps(v) for v in rows) + '\n')
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report['status'], 'invalid_calibration')
            self.assertEqual(report['failed_phase'], 'reference_provenance')

    def _paths(self, directory, *, reference_cpu="Interpreter64", reference_b=None,
               candidate_rows=None, candidate_cpu="Interpreter64"):
        directory = Path(directory)
        reference_a = directory / "reference-a.jsonl"
        reference_b_path = directory / "reference-b.jsonl"
        candidate_path = directory / "candidate.jsonl"
        _write(reference_a, candidate(capture_id=_new_capture_id(), cpu=reference_cpu))
        _write(reference_b_path, reference_b or candidate(capture_id=_new_capture_id(),
                                                          cpu=reference_cpu))
        _write(candidate_path, candidate_rows or candidate(collector="ee" * 32,
                                                           capture_id=_new_capture_id(),
                                                           cpu=candidate_cpu))
        return reference_a, reference_b_path, candidate_path

    def test_changed_reference_pair_is_rejected_before_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            changed = candidate(capture_id=_new_capture_id())
            changed[5]["fighters"][1]["motion"] = 99
            first, second, candidate_path = self._paths(directory, reference_b=changed)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "reference_repeatability")
            self.assertEqual(report["reference_repeatability"], "diverged")

    def test_invalid_reference_is_rejected_before_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            second.write_text("not json\n", encoding="utf-8")
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "reference_validation")
            self.assertEqual(report["invalid_capture"]["capture"], "reference_b")

    def test_reused_capture_id_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            rows = json.loads(first.read_text().splitlines()[0])
            candidate_rows = candidate(capture_id=rows["capture_id"], collector="ee" * 32)
            _write(candidate_path, candidate_rows)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "candidate_identity")

    def test_success_allows_different_collector_and_reports_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "collector_calibrated")
            self.assertTrue(report["calibrated"])
            self.assertFalse(report["gold_admitted"])
            self.assertTrue(report["collector_identity_change"]["candidate_differs"])
            self.assertTrue(report["collector_identity_change"]["difference_permitted"])
            self.assertEqual(set(report["capture_hashes"]),
                             {"reference_a", "reference_b", "candidate"})

    def test_changed_setup_state_input_frame_and_end_report_first_divergence(self):
        mutations = (
            ("setup", lambda rows: rows[1].__setitem__("start_melee_hex", "02" * 0x138),
             "match_enter"),
            ("state", lambda rows: rows[2].__setitem__("match_frame", 1),
             "match_enter_complete"),
            ("input", lambda rows: rows[3]["consumed_inputs"][0].__setitem__(2, "31" * 11),
             "frame"),
            ("frame", lambda rows: rows[4]["fighters"][0].__setitem__("motion", 99),
             "frame"),
        )
        for name, mutate, record in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                candidate_rows = candidate(collector="ee" * 32,
                                           capture_id=_new_capture_id())
                mutate(candidate_rows)
                first, second, candidate_path = self._paths(directory,
                                                             candidate_rows=candidate_rows)
                report = CALIBRATION.calibrate(first, second, candidate_path)
                self.assertEqual(report["status"], "diverged")
                self.assertEqual(report["first_divergence"]["record"], record)

    def test_changed_end_is_rejected_by_candidate_schema(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate_rows = candidate(collector="ee" * 32,
                                       capture_id=_new_capture_id())
            candidate_rows[-1]["status"] = "captured-but-changed"
            first, second, candidate_path = self._paths(directory,
                                                         candidate_rows=candidate_rows)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "candidate_validation")

    def test_changed_phase_is_invalid_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate_rows = candidate(collector="ee" * 32,
                                       capture_id=_new_capture_id())
            candidate_rows[0]["phase"] = "wrong-phase"
            first, second, candidate_path = self._paths(directory,
                                                         candidate_rows=candidate_rows)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "candidate_validation")

    def test_explicit_jit_candidate_cpu_is_reported_as_intentional(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate_rows = candidate(collector="ee" * 32,
                                       capture_id=_new_capture_id(), cpu="JITARM64")
            first, second, candidate_path = self._paths(directory,
                                                         candidate_rows=candidate_rows)
            report = CALIBRATION.calibrate(first, second, candidate_path,
                                           candidate_cpu="JITARM64")
            self.assertEqual(report["status"], "collector_calibrated")
            self.assertTrue(report["cpu_identity_change"]["changed"])
            self.assertTrue(report["cpu_identity_change"]["difference_permitted"])
            self.assertEqual(report["reference_cpu_requested"], "Interpreter64")
            self.assertEqual(report["candidate_cpu_requested"], "JITARM64")
            self.assertEqual(report["cpu_identity_change"]["changed_fields"], ["cpu"])
            self.assertEqual(report["checks"]["cpu_identity"], "intentional_change")

    def test_explicit_jit_reference_pair_allows_interpreter_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(
                directory, reference_cpu="JITARM64", candidate_cpu="Interpreter64")
            report = CALIBRATION.calibrate(
                first, second, candidate_path,
                reference_cpu="JITARM64", candidate_cpu="Interpreter64")
            self.assertEqual(report["status"], "collector_calibrated")
            self.assertTrue(report["calibrated"])
            self.assertEqual(report["reference_cpu_requested"], "JITARM64")
            self.assertEqual(report["candidate_cpu_requested"], "Interpreter64")
            self.assertEqual(report["cpu_identity_change"]["reference"], "JITARM64")
            self.assertEqual(report["cpu_identity_change"]["candidate"], "Interpreter64")
            self.assertEqual(report["cpu_identity_change"]["changed_fields"], ["cpu"])
            self.assertEqual(report["checks"]["cpu_identity"], "intentional_change")

    def test_default_reference_profile_rejects_jit_pair(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(
                directory, reference_cpu="JITARM64", candidate_cpu="Interpreter64")
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "reference_validation")
            self.assertEqual(report["invalid_capture"]["capture"], "reference_a")
            self.assertEqual(report["reference_cpu_requested"], "Interpreter64")

    def test_unknown_profiles_are_rejected_without_auto_detection(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(directory)
            reference_report = CALIBRATION.calibrate(
                first, second, candidate_path, reference_cpu="auto")
            self.assertEqual(reference_report["status"], "invalid_calibration")
            self.assertEqual(reference_report["failed_phase"], "reference_cpu")

            candidate_report = CALIBRATION.calibrate(
                first, second, candidate_path, candidate_cpu="auto")
            self.assertEqual(candidate_report["status"], "invalid_calibration")
            self.assertEqual(candidate_report["failed_phase"], "candidate_cpu")

    def test_mixed_reference_pair_is_rejected_for_explicit_jit_profile(self):
        with tempfile.TemporaryDirectory() as directory:
            mixed_reference = candidate(capture_id=_new_capture_id(), cpu="Interpreter64")
            first, second, candidate_path = self._paths(
                directory, reference_cpu="JITARM64", reference_b=mixed_reference)
            report = CALIBRATION.calibrate(
                first, second, candidate_path, reference_cpu="JITARM64")
            self.assertEqual(report["status"], "invalid_calibration")
            self.assertEqual(report["failed_phase"], "reference_validation")
            self.assertEqual(report["invalid_capture"]["capture"], "reference_b")

    def test_experimental_marker_is_rejected_on_interpreter_side(self):
        with tempfile.TemporaryDirectory() as directory:
            candidate_rows = candidate(capture_id=_new_capture_id(), collector="ee" * 32)
            candidate_rows[0]["provenance"]["experimental_reference_backend"] = True
            first, second, candidate_path = self._paths(
                directory, candidate_rows=candidate_rows)
            report = CALIBRATION.calibrate(first, second, candidate_path)
            self.assertEqual(report["status"], "diverged")
            self.assertEqual(
                report["first_divergence"]["field"],
                "header.provenance.experimental_reference_backend",
            )

    def test_cli_accepts_explicit_reference_cpu(self):
        with tempfile.TemporaryDirectory() as directory:
            first, second, candidate_path = self._paths(
                directory, reference_cpu="JITARM64", candidate_cpu="Interpreter64")
            output = Path(directory) / "calibration.json"
            with mock.patch("builtins.print"):
                exit_code = CALIBRATION.cli_main([
                    "--reference-a", str(first), "--reference-b", str(second),
                    "--candidate", str(candidate_path),
                    "--reference-cpu", "JITARM64",
                    "--candidate-cpu", "Interpreter64",
                    "--output", str(output),
                ])
            self.assertEqual(exit_code, 0)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertEqual(report["status"], "collector_calibrated")
            self.assertEqual(report["reference_cpu_requested"], "JITARM64")


if __name__ == "__main__":
    unittest.main()
