"""One-sided original-versus-port diagnostics remain outside gold admission."""

from copy import deepcopy
import hashlib
import json
from pathlib import Path
import stat
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests"))

from reference_capture_comparison import compare, status_exit_code  # noqa: E402
from test_port_replay_validation import fixture as port_fixture  # noqa: E402
from test_retail_replay_validation import candidate  # noqa: E402


PAD_STATE_HEX = (
    "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000"
    + "00" * (12 * 66)
)


def as_v3(rows):
    """Promote the compact fixture to the current multiplayer candidate schema."""
    result = deepcopy(rows)
    result[0]["version"] = 3
    result[0]["active_player_count"] = 2
    if "provenance" in result[0]:
        result[0]["provenance"]["cpu"] = "JITARM64"
    result[1] = {"record": "match_enter", "rng": result[1]["rng"],
                 "start_melee_hex": result[1]["start_melee_hex"],
                 "pad_state_hex": PAD_STATE_HEX}
    for row in result[2:-1]:
        row["pad_state_hex"] = PAD_STATE_HEX
    return result


def write_jsonl(path, rows):
    path.write_text("".join(json.dumps(row, separators=(",", ":")) + "\n" for row in rows),
                    encoding="utf-8")


def derived_fixture(root, rows, cpu_rows=None):
    derived = root / "derived"
    derived.mkdir()
    candidate_path = derived / "candidate.jsonl"
    write_jsonl(candidate_path, rows)
    if cpu_rows is not None:
        write_jsonl(derived / "cpu-sidecar.jsonl", cpu_rows)
    payload = b"synthetic mwrc only"
    (derived / "capture.mwrc").write_bytes(payload)
    source_hash = "a" * 64
    artifacts = []
    for path in sorted(derived.iterdir()):
        artifacts.append({"name": path.name, "bytes": path.stat().st_size,
                          "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                          "source_manifest_sha256": source_hash})
    manifest = {"schema": "webmelee-reference-capture-diagnostic-replay-v1", "version": 1,
                "session_id": "synthetic-session", "source": {
                    "state": "ingested", "manifest_sha256": source_hash,
                    "observer_raw_sha256": "b" * 64,
                    "raw_files_retained": ["header.json", "records.jsonl", "observer.bin", "manifest.json"]},
                "artifacts": artifacts,
                "transport": {"mwrc_sha256": hashlib.sha256(payload).hexdigest(),
                               "input_sha256": "c" * 64, "frames": len(rows) - 4,
                               "cpu_generated_decisions": "excluded from replay input", "human_input_ports": [0]},
                "claims": {"single_capture_diagnostic": True,
                           "reference_repeatability": "not_established",
                           "port_equivalence": "not_claimed",
                           "performance_acceptance": "not_claimed", "gold_admission": "not_claimed"}}
    unsigned = json.dumps(manifest, sort_keys=True, separators=(",", ":")).encode()
    manifest["manifest_sha256"] = hashlib.sha256(unsigned).hexdigest()
    (derived / "derived-manifest.json").write_text(json.dumps(manifest, sort_keys=True) + "\n",
                                                     encoding="utf-8")
    return derived


class ReferenceCaptureComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="reference-comparison-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.reference_rows = as_v3(candidate())
        _, self.port_rows = port_fixture()
        self.port_rows = as_v3(self.port_rows)
        self.derived = derived_fixture(self.root, self.reference_rows)
        self.trace = self.root / "trace.jsonl"
        write_jsonl(self.trace, self.port_rows)

    def test_one_original_against_port_reports_hashes_without_repeatability_or_gold(self):
        report = compare(self.derived, self.trace, trace_kind="native")
        self.assertEqual(report["status"], "matched")
        self.assertEqual(report["trace"]["kind"], "native")
        self.assertEqual(report["replay"]["status"], "declared_state_match")
        self.assertEqual(report["coverage"]["frames_compared"], 3)
        self.assertEqual(len(report["derived"]["candidate_sha256"]), 64)
        self.assertEqual(report["claims"]["reference_repeatability"], "not_established")
        self.assertEqual(report["claims"]["gold_admission"], "not_claimed")
        self.assertFalse(report["gold_admitted"])
        self.assertEqual(report["missing_coverage"], ["cpu_observation"])
        self.assertEqual(status_exit_code(report["status"]), 0)

    def test_first_port_difference_and_group_coverage_are_preserved(self):
        changed = deepcopy(self.port_rows)
        changed[4]["supplied_inputs"][0] = "01" + "00" * 10
        changed[4]["fighters"][1]["motion"] = 44
        write_jsonl(self.trace, changed)
        report = compare(self.derived, self.trace)
        self.assertEqual(report["status"], "diverged")
        self.assertEqual(report["replay"]["first_divergence"]["frame"], 1)
        self.assertEqual(report["replay"]["checks"]["inputs"], "diverged")
        self.assertEqual(report["replay"]["checks"]["fighters"], "diverged")
        self.assertEqual(report["trace"]["frames"], 3)
        self.assertEqual(status_exit_code(report["status"]), 1)

    def test_incomplete_trace_uses_single_reference_prefix_diagnostic(self):
        prefix = deepcopy(self.port_rows[:-1])
        prefix[4]["supplied_inputs"][0] = "01" + "00" * 10
        prefix[5]["fighters"][1]["motion"] = 44
        write_jsonl(self.trace, prefix)
        report = compare(self.derived, self.trace)
        self.assertEqual(report["status"], "incomplete_prefix")
        self.assertFalse(report["replay"]["complete"])
        self.assertEqual(report["replay"]["first_divergence"]["frame"], 1)
        self.assertEqual(report["replay"]["checks"]["inputs"], "diverged")
        self.assertEqual(report["replay"]["checks"]["fighters"], "diverged")
        self.assertEqual(report["missing_coverage"], ["port_completion", "cpu_observation"])
        self.assertEqual(report["claims"]["reference_repeatability"], "not_established")
        self.assertNotIn("reference_repeatability", report["replay"])
        self.assertNotIn("reference_a", report["replay"].get("capture_hashes", {}))
        self.assertEqual(status_exit_code(report["status"]), 2)

    def test_matching_incomplete_trace_remains_incomplete(self):
        write_jsonl(self.trace, self.port_rows[:-1])
        report = compare(self.derived, self.trace)
        self.assertEqual(report["status"], "incomplete_prefix")
        self.assertIsNone(report["replay"]["first_divergence"])
        self.assertEqual(report["replay"]["observed_frames"], 3)
        self.assertEqual(report["trace"]["frames"], 3)
        self.assertEqual(report["missing_coverage"], ["port_completion", "cpu_observation"])

    def bound_cpu_fixture(self):
        from test_cpu_observation import CpuObservationTests
        rows = CpuObservationTests().capture_timeline(3)
        for core in (self.reference_rows, self.port_rows):
            core[1]["start_melee_hex"] = rows[0]["setup_hex"]
        self.port_rows[0]["rendering"] = "source_draws"
        rows[1]["match"]["frame"] = self.reference_rows[2]["match_frame"]
        for row in rows[2:-1]:
            index = row["source_index"] if row["record"] == "draw" else row["index"]
            row["match"]["frame"] = self.reference_rows[3 + index]["match_frame"]
        shutil.rmtree(self.derived)
        self.derived = derived_fixture(self.root, self.reference_rows, rows)
        write_jsonl(self.trace, self.port_rows)
        target = self.root / "target-cpu.jsonl"
        write_jsonl(target, rows)
        return self.derived / "cpu-sidecar.jsonl", target, rows

    def test_derived_binding_and_cpu_sidecar_are_checked(self):
        native_cpu, target_cpu, _ = self.bound_cpu_fixture()
        report = compare(self.derived, self.trace, reference_cpu=native_cpu,
                         trace_cpu=target_cpu)
        self.assertEqual(report["status"], "matched")
        self.assertEqual(report["cpu"]["status"], "matched")
        self.assertEqual(report["cpu"]["coverage"]["cpu"]["status"], "matched")
        manifest = self.derived / "derived-manifest.json"
        value = json.loads(manifest.read_text())
        value["source"]["manifest_sha256"] = "d" * 64
        manifest.write_text(json.dumps(value), encoding="utf-8")
        report = compare(self.derived, self.trace)
        self.assertEqual(report["status"], "invalid")
        self.assertIn("binding hash mismatch", report["error"])

    def test_cpu_difference_reports_earliest_field_and_domain(self):
        native_cpu, target_cpu, rows = self.bound_cpu_fixture()
        rows[1]["players"][1]["cpu"]["buttons"] ^= 1
        write_jsonl(target_cpu, rows)
        report = compare(self.derived, self.trace, reference_cpu=native_cpu,
                         trace_cpu=target_cpu)
        self.assertEqual(report["status"], "diverged")
        self.assertEqual(report["cpu"]["first_divergence"]["phase"], "initial")
        self.assertEqual(report["cpu"]["coverage"]["cpu"]["first_divergence"]["phase"], "initial")

    def test_identical_unrelated_cpu_inputs_cannot_claim_coverage(self):
        from test_cpu_observation import CpuObservationTests
        native_cpu, target_cpu, _ = self.bound_cpu_fixture()
        unrelated = CpuObservationTests().capture()[0]
        external = self.root / "unrelated.jsonl"
        write_jsonl(external, unrelated)
        report = compare(self.derived, self.trace, reference_cpu=external, trace_cpu=external)
        self.assertEqual(report["cpu"]["status"], "invalid_input")
        self.assertIn("artifact binding", report["cpu"]["error"])
        self.assertEqual(report["missing_coverage"], ["cpu_observation"])

    def test_cpu_sidecars_are_bound_to_setup_and_core_timeline(self):
        for mutation, message in (("setup", "setup differs"), ("initial", "initial clock differs"),
                                  ("frame", "clock differs at tick"), ("count", "frame count differs")):
            for side in ("reference", "target"):
                with self.subTest(mutation=mutation, side=side):
                    native_cpu, target_cpu, rows = self.bound_cpu_fixture()
                    changed = deepcopy(rows)
                    if mutation == "setup":
                        changed[0]["setup_hex"] = "01" + changed[0]["setup_hex"][2:]
                    elif mutation == "initial":
                        changed[1]["match"]["frame"] += 1
                    elif mutation == "frame":
                        changed[2]["match"]["frame"] += 1
                    else:
                        changed = changed[:-3] + [changed[-1]]
                        changed[0]["frames_requested"] = 2
                        changed[-1]["frames"] = changed[-1]["draws"] = 2
                    if side == "reference":
                        shutil.rmtree(self.derived)
                        self.derived = derived_fixture(self.root, self.reference_rows, changed)
                    else:
                        write_jsonl(target_cpu, changed)
                    report = compare(self.derived, self.trace, reference_cpu=native_cpu, trace_cpu=target_cpu)
                    self.assertEqual(report["status"], "invalid_input")
                    self.assertIn(message, report["cpu"]["error"])

    def test_cpu_drawing_declaration_must_match_core_trace(self):
        native_cpu, target_cpu, rows = self.bound_cpu_fixture()
        self.port_rows[0]["rendering"] = "excluded"
        write_jsonl(self.trace, self.port_rows)
        report = compare(self.derived, self.trace, reference_cpu=native_cpu, trace_cpu=target_cpu)
        self.assertEqual(report["status"], "invalid_input")
        self.assertIn("drawing declaration", report["cpu"]["error"])

    def test_invalid_cpu_sidecar_is_explicit_without_discarding_replay_result(self):
        reference_cpu = self.root / "reference-cpu.jsonl"
        trace_cpu = self.root / "trace-cpu.jsonl"
        reference_cpu.write_text("not a CPU observation\n", encoding="utf-8")
        trace_cpu.write_text("not a CPU observation\n", encoding="utf-8")
        changed = deepcopy(self.port_rows)
        changed[4]["fighters"][1]["motion"] = 44
        write_jsonl(self.trace, changed)
        report = compare(self.derived, self.trace, reference_cpu=reference_cpu,
                         trace_cpu=trace_cpu)
        self.assertEqual(report["status"], "invalid_input")
        self.assertEqual(report["replay"]["status"], "diverged")
        self.assertEqual(report["replay"]["first_divergence"]["frame"], 1)
        self.assertEqual(report["cpu"]["status"], "invalid_input")
        self.assertEqual(report["missing_coverage"], ["cpu_observation"])
        self.assertEqual(status_exit_code(report["status"]), 2)

    def test_cli_report_output_is_exclusive_and_mode_restricted(self):
        output = self.root / "comparison.json"
        command = [sys.executable, str(ROOT / "scripts/compare_reference_capture.py"),
                   "--derived", str(self.derived), "--trace", str(self.trace),
                   "--output", str(output)]
        first = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o600)
        original = output.read_bytes()
        second = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(second.returncode, 2)
        self.assertEqual(output.read_bytes(), original)

    def test_cli_report_cannot_enter_raw_or_derived_input_even_through_alias(self):
        raw = self.root / "raw"
        raw.mkdir()
        (raw / "manifest.json").write_text("synthetic immutable marker")
        for source in (self.derived, raw):
            alias = self.root / (source.name + "-output-alias")
            alias.symlink_to(source, target_is_directory=True)
            before = {str(p.relative_to(source)): p.read_bytes() for p in source.rglob("*") if p.is_file()}
            for output in (source / "report.json", source / "nested/report.json", alias / "report.json"):
                command = [sys.executable, str(ROOT / "scripts/compare_reference_capture.py"),
                           "--derived", str(self.derived), "--trace", str(self.trace),
                           "--bundle", str(raw), "--output", str(output)]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 2)
                self.assertIn("outside immutable", result.stderr)
                self.assertEqual({str(p.relative_to(source)): p.read_bytes() for p in source.rglob("*") if p.is_file()}, before)
                self.assertFalse((source / "nested").exists())

    def test_derived_symlink_root_is_rejected_before_resolution(self):
        alias = self.root / "derived-alias"
        alias.symlink_to(self.derived, target_is_directory=True)
        report = compare(alias, self.trace)
        self.assertEqual(report["status"], "invalid")
        self.assertIn("symlink", report["error"])


if __name__ == "__main__":
    unittest.main()
