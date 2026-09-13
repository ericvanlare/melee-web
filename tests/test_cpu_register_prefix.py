"""Focused tests for the ended, non-admitting CPU-register prefix join."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "tests")]

from cpu_register_prefix import compare_prefix_paths  # noqa: E402
from test_retail_replay_validation import candidate  # noqa: E402


DIAGNOSTIC_COLLECTOR = "ee" * 32
PAD_STATE = "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000" + "00" * (12 * 66)
NEUTRAL = "00" * 11
DISCONNECTED = "00" * 10 + "ff"


def _write_jsonl(path: Path, rows) -> None:
    path.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows), encoding="utf-8")


def _as_v3(rows):
    rows = deepcopy(rows)
    rows[0].update(version=3, active_player_count=2)
    rows[1].pop("pad_lib_hex")
    rows[1].pop("pad_master_hex")
    rows[1].pop("pad_game_hex")
    rows[1]["pad_state_hex"] = PAD_STATE
    for row in rows[2:]:
        if row["record"] in ("match_enter_complete", "frame"):
            row["pad_state_hex"] = PAD_STATE
    return rows


def _plan(frame_count: int) -> dict:
    return {
        "schema": "melee-web-retail-input-plan", "version": 3,
        "policy": "dolphin-pipe-raw-v2", "source_sha256": "11" * 32,
        "first_frame": -123, "source_stage": 32, "source_characters": [0, 1],
        "active_player_count": 2, "source_player_types": [0, 1],
        "source_cpu_kinds": [None, 4], "source_cpu_levels": [None, 1],
        "source_cpu_pad_modes": [None, "neutral"], "controlled_ports": [1],
        "frames": [[NEUTRAL, NEUTRAL, DISCONNECTED, DISCONNECTED]
                   for _ in range(frame_count)],
    }


def _fixture(root: Path, frame_count: int = 4, prefix_count: int = 3):
    full_plan = _plan(frame_count)
    prefix_plan = dict(full_plan, frames=full_plan["frames"][:prefix_count])
    full_bytes = (json.dumps(full_plan, sort_keys=True, indent=2) + "\n").encode()
    prefix_bytes = (json.dumps(prefix_plan, sort_keys=True, indent=2) + "\n").encode()
    full_path, prefix_plan_path = root / "input-plan-full.json", root / "input-plan-prefix.json"
    full_path.write_bytes(full_bytes)
    prefix_plan_path.write_bytes(prefix_bytes)
    full_hash = hashlib.sha256(full_bytes).hexdigest()
    prefix_hash = hashlib.sha256(prefix_bytes).hexdigest()
    collector_dir = root / "collector"
    collector_dir.mkdir()
    collector_files = {
        "reference_replay_capture.py": b"collector\n",
        "reference_replay_boundary.py": b"boundary\n",
        "retail_input_plan.py": b"input plan\n",
        "retail_input_bootstrap.py": b"bootstrap\n",
    }
    for name, data in collector_files.items():
        (collector_dir / name).write_bytes(data)
    collector_source = collector_dir / "reference_replay_capture.py"
    collector_input_hash = hashlib.sha256(collector_source.read_bytes()).hexdigest()
    collector_bundle = hashlib.sha256(
        b"\0".join(collector_files[name] for name in collector_files)).hexdigest()
    helper_rows = [{"name": name, "sha256": hashlib.sha256(data).hexdigest(),
                    "bytes": len(data)} for name, data in list(collector_files.items())[1:]]
    gold_a, gold_b = _as_v3(candidate(frame_count)), _as_v3(candidate(frame_count))
    for rows in (gold_a, gold_b):
        rows[0]["collector_sha256"] = collector_bundle
        rows[0]["provenance"]["input_plan_sha256"] = full_hash
    prefix = deepcopy(gold_a[:3 + prefix_count])
    prefix[0].update(frames_requested=prefix_count, collector_sha256=DIAGNOSTIC_COLLECTOR)
    prefix[0]["capture_id"] = "000000000000400080000000000000aa"
    prefix[0]["provenance"]["input_plan_sha256"] = prefix_hash
    prefix.append({"record": "end", "frames": prefix_count, "status": "captured"})
    gold_a_path, gold_b_path = root / "gold-a.jsonl", root / "gold-b.jsonl"
    prefix_path, metadata_path = root / "capture-prefix.jsonl", root / "run-metadata.json"
    _write_jsonl(gold_a_path, gold_a)
    _write_jsonl(gold_b_path, gold_b)
    _write_jsonl(prefix_path, prefix)
    metadata_path.write_text(json.dumps({
        "schema": "melee-web-retail-cpu-register-diagnostic", "version": 1,
        "status": "diagnostic_only", "evidence_status": "diagnostic_only",
        "candidate_admission": "forbidden", "frames_requested": prefix_count,
        "identity": {"game_revision": "GALE01r2", "cpu": "Interpreter64",
                      "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
                      "source_revision": "b43912cc78606f96c9569f5d6229bc9d7e265ea5",
                      "dolphin_binary_sha256": "aa" * 32, "dolphin_version": "2606a",
                      "dolphin_commit": "c77bbaa0f372c3f72281602a8b087206706542cb"},
        "diagnostic": {
            "window": {"start_tick": 1, "end_tick": prefix_count - 1},
            "full_input_plan_path": str(full_path),
            "full_input_plan_sha256": full_hash,
            "executed_input_prefix_path": str(prefix_plan_path),
            "executed_input_prefix_sha256": prefix_hash,
            "collector": {"input_collector": str(collector_source),
                           "input_collector_sha256": collector_input_hash,
                           "helpers": helper_rows},
        },
        "owned": {"capture_prefix": str(prefix_path),
                  "collector_sha256": DIAGNOSTIC_COLLECTOR},
    }) + "\n", encoding="utf-8")
    return gold_a_path, gold_b_path, prefix_path, metadata_path


class CpuRegisterPrefixTests(unittest.TestCase):
    def test_cli_cannot_overwrite_an_existing_indirect_input(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            collector = root / "collector/reference_replay_capture.py"
            before = collector.read_bytes()
            result = subprocess.run(
                [sys.executable, str(ROOT / "scripts/check_cpu_register_prefix.py"),
                 *map(str, paths), "--output", str(collector)],
                capture_output=True, text=True, check=False)
            self.assertEqual(result.returncode, 2)
            self.assertIn("output already exists", result.stderr)
            self.assertEqual(collector.read_bytes(), before)

    def test_actual_metadata_shape_and_all_v3_source_fields(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            paths = _fixture(Path(directory))
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "diagnostic_prefix")
        self.assertEqual(report["full_gold_repeatability"]["status"], "repeatable")
        self.assertEqual([x["status"] for x in report["source_comparisons"]], ["exact", "exact"])
        self.assertEqual(report["prefix_frames"], 3)
        self.assertFalse(report["candidate_admitted"])
        self.assertTrue(report["prefix_is_shorter_than_gold"])
        self.assertTrue(report["metadata_differences"]["collector_sha256"]["differs"])
        self.assertIn("configuration_socket", report["metadata_differences"])

    def test_first_difference_is_exact_and_checks_raw_pad_input(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            paths = _fixture(Path(directory))
            rows = [json.loads(line) for line in paths[2].read_text().splitlines()]
            rows[4]["consumed_inputs"][0][0] = "ff" * 11
            _write_jsonl(paths[2], rows)
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "diagnostic_diverged")
        first = report["first_divergence"]
        self.assertEqual(first["frame"], 1)
        self.assertEqual(first["field"], "frame[1].consumed_inputs[0][0]")

    def test_full_pair_must_repeat_before_prefix_is_loaded(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            rows = [json.loads(line) for line in paths[1].read_text().splitlines()]
            rows[3]["rng"] += 1
            _write_jsonl(paths[1], rows)
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertEqual(report["full_gold_repeatability"]["status"], "diverged")
        self.assertNotIn("source_comparisons", report)

    def test_real_end_is_required_and_declared_count_must_match(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            rows = [json.loads(line) for line in paths[2].read_text().splitlines()][:-1]
            _write_jsonl(paths[2], rows)
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("end", report["error"])

        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            rows = [json.loads(line) for line in paths[2].read_text().splitlines()]
            rows[-1]["frames"] = 2
            _write_jsonl(paths[2], rows)
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("end.frames", report["error"])

    def test_diagnostic_window_must_end_at_the_ended_prefix(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            metadata = json.loads(paths[3].read_text())
            metadata["diagnostic"]["window"]["end_tick"] = 1
            paths[3].write_text(json.dumps(metadata) + "\n")
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("end_tick", report["error"])

    def test_plan_prefix_configuration_and_hash_bindings_are_strict(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            plan = json.loads(Path(paths[3].parent / "input-plan-prefix.json").read_text())
            plan["source_stage"] = 33
            changed = json.dumps(plan, sort_keys=True, indent=2) + "\n"
            (root / "input-plan-prefix.json").write_text(changed)
            metadata = json.loads(paths[3].read_text())
            metadata["diagnostic"]["executed_input_prefix_sha256"] = hashlib.sha256(changed.encode()).hexdigest()
            paths[3].write_text(json.dumps(metadata) + "\n")
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("input-plan configuration", report["error"])

    def test_identity_binary_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            rows = [json.loads(line) for line in paths[2].read_text().splitlines()]
            rows[0]["provenance"]["dolphin_binary_sha256"] = "bb" * 32
            _write_jsonl(paths[2], rows)
            report = compare_prefix_paths(*paths)
        self.assertEqual(report["status"], "invalid_capture")
        self.assertIn("identity", report["error"])

    def test_failed_run_requires_explicit_allow_and_retains_reason(self):
        with tempfile.TemporaryDirectory(prefix="cpu-register-prefix-") as directory:
            root = Path(directory)
            paths = _fixture(root)
            metadata = json.loads(paths[3].read_text())
            metadata.update(status="diagnostic_failed", error="retained probe errors")
            paths[3].write_text(json.dumps(metadata) + "\n")
            rejected = compare_prefix_paths(*paths)
            accepted = compare_prefix_paths(*paths, allow_failed=True)
        self.assertEqual(rejected["status"], "invalid_capture")
        self.assertIn("allow_failed", rejected["error"])
        self.assertEqual(accepted["status"], "diagnostic_failed")
        self.assertEqual(accepted["conclusion"]["reason"], "retained probe errors")
        self.assertEqual(accepted["conclusion"]["evidence_admission"], "forbidden")


if __name__ == "__main__":
    unittest.main()
