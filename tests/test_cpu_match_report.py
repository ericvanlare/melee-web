"""Focused acceptance tests for complete CPU-match evidence joins."""
from __future__ import annotations

from contextlib import contextmanager
from copy import deepcopy
from dataclasses import dataclass
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import types
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))

import cpu_observation_validation  # noqa: E402
import cpu_reference_scenarios  # noqa: E402
import replay_coverage  # noqa: E402
import retail_match_completion  # noqa: E402
import retail_setup_validation  # noqa: E402


def _load_checker():
    spec = importlib.util.spec_from_file_location(
        "check_cpu_match_under_test", ROOT / "scripts" / "check_cpu_match.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


CHECKER = _load_checker()


@dataclass
class FakeCapture:
    path: Path
    sha256: str
    capture_id: str
    plan_sha256: str
    frames: list[dict]

    @property
    def header(self):
        return {
            "provenance": {"input_plan_sha256": self.plan_sha256},
            "capture_id": self.capture_id,
        }

    @property
    def match_enter(self):
        return {"rng": 1234, "start_melee_hex": "00" * 0x138}


def _setup(result_winner=1):
    return {
        "stage": 32,
        "match_kind": 1,
        "is_stock": True,
        "is_teams": False,
        "timer_enabled": True,
        "timer_counts_up": False,
        "time_limit_seconds": 60,
        "item_frequency": -1,
        "item_mask_hex": "ffffffffffffffff",
        "disable_pausing": False,
        "damage_ratio_bits": "3f800000",
        "game_speed_bits": "3f800000",
        "players": [
            {"port": 1, "character_kind": 8, "costume": 0, "stocks": 2,
             "rumble_enabled": False, "player_type": 0},
            {"port": 2, "character_kind": 2, "costume": 1, "stocks": 2,
             "rumble_enabled": False, "player_type": 1, "cpu_kind": 4,
             "cpu_level": 1},
        ],
        "result_winner": result_winner,
    }


def _scenario():
    return cpu_reference_scenarios.get_scenario(
        "mario-human-vs-fox-cpu1-final-destination")


def _write_manifest(root: Path):
    root.mkdir(parents=True, exist_ok=True)
    scenario_value = _scenario()
    plan_sha = hashlib.sha256(
        cpu_reference_scenarios.canonical(scenario_value)).hexdigest()
    plan = root / "input-plan.json"
    plan.write_text("plan\n")
    recipe = root / "recipe.mwrc"
    recipe.write_bytes(b"RECIPE")
    disc_identity = {
        "disc_image_sha256": "d" * 64,
        "disc_image_bytes": 145997824,
        "disc_dol_sha1": "e" * 40,
    }
    scenario = root / "scenario.json"
    scenario.write_text(json.dumps(scenario_value) + "\n")
    references = []
    for label, digest in (("a", "a" * 64), ("b", "b" * 64)):
        trace = root / f"reference-{label}.jsonl"
        trace.write_text("placeholder\n")
        run = root / f"run-{label}"
        (run / "evidence").mkdir(parents=True)
        (run / "run-metadata.json").write_text(json.dumps({
            "status": "captured", "output_sha256": digest,
            "capture_id": f"capture-{label}",
            "identity": disc_identity,
        }) + "\n")
        (run / "evidence" / "draw-audit.jsonl").write_text("draw\n")
        (run / "evidence" / "cpu-observation.jsonl").write_text("observation\n")
        (run / "evidence" / "scene-teardown.json").write_text(json.dumps({
            "schema": "melee-web-retail-scene-teardown", "version": 1,
            "phase": "HSD_GObj_80391304_return", "address": "8039157c",
            "source_ticks": 2, "source_draws": 2,
            "released_fighter_slots": [0, 1], "entity_list_count": 64,
            "entity_heads_hex": "0" * 512, "routing_hex": "0" * 12,
            "result": {"outcome": 2, "winners": [1]},
        }) + "\n")
        references.append({"trace": trace.name, "run_directory": run.name})
    native = root / "native"
    native.mkdir()
    (native / "trace.jsonl").write_text("native\n")
    (native / "observation.jsonl").write_text("native observation\n")
    (native / "stderr.log").write_text("native log\n")
    (native / "hitlag-audit.log").write_text("hitlag audit\n")
    (native / "matrix-audit.jsonl").write_text("matrix audit\n")
    native_build = root / 'native-build'
    native_build.mkdir()
    artifacts = {}
    for name in ('gameplay_retail_trace.js', 'gameplay_retail_trace.wasm'):
        (native_build / name).write_bytes(name.encode())
        artifacts[name] = CHECKER.sha(native_build / name)
    (root / 'menu-assets').mkdir()
    (root / 'game-assets').mkdir()
    (root / 'game-assets' / 'owned.dat').write_bytes(b'owned asset')
    (root / 'node').write_bytes(b'synthetic node identity')
    runtime_inputs = CHECKER.runtime_inputs(root / 'node', root / 'menu-assets', root / 'game-assets')
    recipe_hash = CHECKER.sha(recipe)
    config_path = native / 'capture-config.json'
    config_path.write_text(json.dumps({'schema': 'melee-web-cpu-native-capture', 'version': 1,
        'require_match_complete': True, 'artifacts': artifacts, 'recipe_sha256': recipe_hash, 'runtime_inputs': runtime_inputs}))
    (native / 'capture-result.json').write_text(json.dumps({
        'schema': 'melee-web-cpu-native-capture-result', 'version': 1,
        'status': 'completed', 'returncode': 0, 'full_completion': True, 'artifact_change': False,
        'capture_config_sha256': CHECKER.sha(config_path), 'recipe_sha256_before': recipe_hash,
        'recipe_sha256_after': recipe_hash, 'artifacts_before': artifacts, 'artifacts_after': artifacts,
        'runtime_inputs_before': runtime_inputs, 'runtime_inputs_after': runtime_inputs,
        'outputs': {'trace.jsonl': CHECKER.sha(native / 'trace.jsonl'),
                    'cpu-observation.jsonl': CHECKER.sha(native / 'observation.jsonl'),
                    'stderr.log': CHECKER.sha(native / 'stderr.log'),
                    'hitlag-audit.log': CHECKER.sha(native / 'hitlag-audit.log'),
                    'matrix-audit.jsonl': CHECKER.sha(native / 'matrix-audit.jsonl')}}))
    browser = root / "browser"
    browser.mkdir()
    (browser / "capture-config.json").write_text(json.dumps({
        **disc_identity,
        "recipe_sha256": hashlib.sha256(recipe.read_bytes()).hexdigest(),
    }) + "\n")
    (browser / "retail-browser-report.json").write_text(json.dumps({
        "source_match": {"complete": True, "outcome": 2, "winner": 1},
        "metrics": {"sourceFrames": 2, "sourceSteps": 2, "sourceDraws": 2},
        "preparation": {"source_draws": 0},
        "instrumented_timing_resumes": 0, "user_agent": "test",
    }) + "\n")
    (browser / "browser-errors.json").write_text('{"errors": [], "requests": []}\n')
    (browser / "preparation-observation.jsonl").write_text('')
    build = root / "build"
    build.mkdir()
    for name in CHECKER.BUILD_ARTIFACTS:
        (build / name).write_bytes(name.encode("utf-8"))
        (native_build / name).write_bytes(name.encode("utf-8"))
    served = {
        name: hashlib.sha256((build / name).read_bytes()).hexdigest()
        for name in CHECKER.BUILD_ARTIFACTS
    }
    (browser / "served-artifacts.json").write_text(json.dumps(served) + "\n")
    (browser / "served-artifacts-after.json").write_text(json.dumps(served) + "\n")
    manifest = root / "manifest.json"
    manifest.write_text(json.dumps({
        "schema": "melee-web-cpu-match-run", "version": 1,
        "scenario_id": scenario_value["scenario_id"], "scenario": scenario.name,
        "input_plan": plan.name, "recipe": recipe.name,
        "reference_a": references[0], "reference_b": references[1],
        "native": {"trace": "native/trace.jsonl",
                    "observation": "native/observation.jsonl", "build_directory": "native-build",
                    "menu_assets": "menu-assets", "game_assets": "game-assets", "node_executable": "node"},
        "browser": browser.name, "build_directory": build.name,
    }) + "\n")
    return manifest, plan_sha, references


def _fake_captures(root: Path, plan_sha: str):
    return {
        (root / "reference-a.jsonl").resolve(): FakeCapture(
            root / "reference-a.jsonl", "a" * 64, "capture-a", plan_sha, [{}, {}]),
        (root / "reference-b.jsonl").resolve(): FakeCapture(
            root / "reference-b.jsonl", "b" * 64, "capture-b", plan_sha, [{}, {}]),
    }


@contextmanager
def _checker_fakes(captures, *, winner_b=1, winners_b=None,
                   native_status="declared_state_match"):
    setup = _setup()
    published_winners_b = [winner_b] if winners_b is None else list(winners_b)
    observation = types.SimpleNamespace(
        sha256="o" * 64,
        draws=[{}, {}],
        end={"result": {"outcome": 2, "winners": [1]},
              "remaining_fighter_slots": []},
    )

    def load_capture(path, **_):
        value = captures[Path(path).resolve()]
        if value.capture_id == "capture-b" and winner_b != 1:
            value = FakeCapture(value.path, value.sha256, value.capture_id,
                                value.plan_sha256, value.frames)
        return value

    def load_observation(_path, _capture=None):
        if published_winners_b == [1] or "run-b" not in str(_path):
            return observation
        return types.SimpleNamespace(
            sha256="q" * 64,
            draws=[{}, {}],
            end={"result": {"outcome": 2, "winners": published_winners_b},
                  "remaining_fighter_slots": []},
        )

    def decode(_raw):
        value = json.loads(json.dumps(setup))
        if winner_b != 1:
            value["result_winner"] = winner_b
        return value

    def compare_observations(*args):
        if len(args) == 2:
            if published_winners_b != [1]:
                return {"reference_repeatability": "fail",
                        "status": "reference_divergence",
                        "first_divergence": {
                            "phase": "end", "tick": 2,
                            "field": "end.result.winners",
                            "expected": {"outcome": 2, "winners": [1]},
                            "actual": {"outcome": 2,
                                       "winners": published_winners_b}}}
            return {"reference_repeatability": "pass", "status": "repeatable",
                    "first_divergence": None}
        return {"status": "declared_state_match"}

    def compare_port(*_args):
        return {"status": native_status if "native" in str(_args[-1])
                else "declared_state_match"}

    def fake_analyze(*_args, **_kwargs):
        return {"frames": 2, "cpu_match": {
            "player_count": 2, "roles": ["human", "cpu"],
            "cpu_difficulties": [5], "target_changes": 1,
            "stock_losses": 1, "respawns": 1,
        }}

    patches = [
        mock.patch.object(CHECKER, "load_capture", load_capture),
        mock.patch.object(
            CHECKER, "load_plan",
            lambda _path: ({"version": 3,
                             "source_sha256": next(iter(captures.values())).plan_sha256},
                            next(iter(captures.values())).plan_sha256)),
        mock.patch.object(CHECKER, "verify_capture", lambda *_args: None),
        mock.patch.object(CHECKER, "_decode_setup", decode),
        mock.patch.object(CHECKER, "load_observation", load_observation),
        mock.patch.object(CHECKER, "load_match_completion",
                          lambda *_args, **_kwargs: {"match_result": 2}),
        mock.patch.object(CHECKER, "load_draw_audit", lambda *_args: {"status": "validated"}),
        mock.patch.object(CHECKER, "compare_retail",
                          lambda *_args: {"repeatable": True, "status": "repeatable"}),
        mock.patch.object(CHECKER, "compare_observations", compare_observations),
        mock.patch.object(CHECKER, "compare_port", compare_port),
        mock.patch.object(CHECKER, "read_jsonl", lambda _path: []),
        mock.patch.object(CHECKER, "_first_difference", lambda *_args: None),
        mock.patch.object(CHECKER, "encode_mwrc", lambda *_args: (b"RECIPE", None)),
        mock.patch.object(CHECKER, "validate_report", lambda *_args: None),
        mock.patch.object(CHECKER, "analyze_cpu_capture", fake_analyze),
    ]
    for patch in patches:
        patch.start()
    try:
        yield
    finally:
        for patch in reversed(patches):
            patch.stop()


class CpuMatchReportTests(unittest.TestCase):
    def test_native_receipt_is_bound_to_declared_build_node_and_assets(self):
        for changed in ('native-build/gameplay_retail_trace.wasm',
                        'native-build/gameplay_menu_browser.wasm',
                        'node', 'game-assets/owned.dat'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                manifest, plan_sha, _ = _write_manifest(root)
                with _checker_fakes(_fake_captures(root, plan_sha)):
                    self.assertTrue(CHECKER.check_manifest(manifest)['native_agrees'])
                    (root / changed).write_bytes(b'different declared input')
                    result = CHECKER.check_manifest(manifest)
                self.assertFalse(result['native_agrees'])
                self.assertEqual(result['checks']['native_capture']['status'], 'invalid_or_incomplete')

    def test_native_output_change_cannot_reuse_success_receipt(self):
        with tempfile.TemporaryDirectory(prefix="cpu-native-receipt-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            with _checker_fakes(_fake_captures(root, plan_sha)):
                before = CHECKER.check_manifest(manifest)
                self.assertTrue(before['native_agrees'])
                (root / 'native/trace.jsonl').write_text('changed after capture\n')
                after = CHECKER.check_manifest(manifest)
            self.assertFalse(after['native_agrees'])
            self.assertIn('receipt', after['checks']['native_capture']['error'])

    def test_native_diagnostic_output_change_cannot_reuse_success_receipt(self):
        with tempfile.TemporaryDirectory(prefix="cpu-native-diagnostic-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            with _checker_fakes(_fake_captures(root, plan_sha)):
                before = CHECKER.check_manifest(manifest)
                self.assertTrue(before['native_agrees'])
                (root / 'native/hitlag-audit.log').write_text('changed diagnostic\n')
                after = CHECKER.check_manifest(manifest)
        self.assertFalse(after['native_agrees'])
        self.assertIn('receipt', after['checks']['native_capture']['error'])

    def test_extra_source_preparation_draw_prevents_full_agreement(self):
        with tempfile.TemporaryDirectory(prefix="cpu-preparation-phase-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            browser_report = root / 'browser' / 'retail-browser-report.json'
            report = json.loads(browser_report.read_text())
            report['preparation']['source_draws'] = 1
            browser_report.write_text(json.dumps(report) + '\n')
            with _checker_fakes(_fake_captures(root, plan_sha)), mock.patch.object(
                    CHECKER, 'load_preparation_observations', return_value=[{'index': 0}]):
                result = CHECKER.check_manifest(manifest)
            self.assertTrue(result['reference_accepted'])
            self.assertFalse(result['browser_agrees'])
            phase = result['checks']['browser_source_phases']
            self.assertEqual(phase['status'], 'additional_source_draws')
            self.assertEqual(phase['all_observed_source_draws'], 3)

    def test_missing_browser_source_counters_prevent_agreement(self):
        with tempfile.TemporaryDirectory(prefix="cpu-source-cadence-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            browser_report = root / 'browser' / 'retail-browser-report.json'
            report = json.loads(browser_report.read_text())
            del report['metrics']['sourceSteps']
            del report['metrics']['sourceDraws']
            browser_report.write_text(json.dumps(report) + '\n')
            with _checker_fakes(_fake_captures(root, plan_sha)):
                result = CHECKER.check_manifest(manifest)
        self.assertTrue(result['reference_accepted'])
        self.assertFalse(result['browser_agrees'])
        self.assertIn('source timeline count',
                      result['checks']['browser_completion']['error'])

    def test_extra_browser_http_artifact_prevents_agreement(self):
        with tempfile.TemporaryDirectory(prefix="cpu-browser-artifact-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            served_path = root / 'browser' / 'served-artifacts.json'
            served = json.loads(served_path.read_text())
            served['unexpected-runtime.js'] = '0' * 64
            served_path.write_text(json.dumps(served) + '\n')
            after_path = root / 'browser' / 'served-artifacts-after.json'
            after_path.write_text(json.dumps(served) + '\n')
            with _checker_fakes(_fake_captures(root, plan_sha)):
                result = CHECKER.check_manifest(manifest)
        self.assertTrue(result['reference_accepted'])
        self.assertFalse(result['browser_agrees'])
        self.assertIn('inventory', result['checks']['browser_build']['error'])

    def test_preparation_report_count_must_match_observed_rows(self):
        with tempfile.TemporaryDirectory(prefix="cpu-preparation-count-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            browser_report = root / 'browser' / 'retail-browser-report.json'
            report = json.loads(browser_report.read_text())
            report['preparation']['source_draws'] = 1
            browser_report.write_text(json.dumps(report) + '\n')
            with _checker_fakes(_fake_captures(root, plan_sha)):
                result = CHECKER.check_manifest(manifest)
        self.assertTrue(result['reference_accepted'])
        self.assertFalse(result['browser_agrees'])
        self.assertIn('preparation report',
                      result['checks']['browser_source_phases']['error'])

    def test_missing_scene_teardown_prevents_reference_acceptance(self):
        with tempfile.TemporaryDirectory(prefix="cpu-scene-teardown-") as directory:
            root = Path(directory)
            manifest, plan_sha, references = _write_manifest(root)
            (root / references[0]['run_directory'] / 'evidence' /
             'scene-teardown.json').unlink()
            with _checker_fakes(_fake_captures(root, plan_sha)):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result['reference_accepted'])
        self.assertEqual(result['checks']['reference_a']['status'],
                         'invalid_or_incomplete')
        self.assertIn('scene-teardown',
                      result['checks']['reference_a']['error'])

    def test_noncanonical_scenario_cannot_reuse_corpus_id(self):
        with tempfile.TemporaryDirectory(prefix="cpu-scenario-identity-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            scenario_path = root / 'scenario.json'
            scenario = json.loads(scenario_path.read_text())
            scenario['players'][1]['cpu_level'] = 2
            scenario_path.write_text(json.dumps(scenario) + '\n')
            with self.assertRaisesRegex(ValueError, 'canonical CPU corpus'):
                CHECKER.check_manifest(manifest)

    def test_complete_pair_preserves_distinct_native_and_browser_statuses(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures, native_status="declared_state_divergence"):
                result = CHECKER.check_manifest(manifest)
        self.assertTrue(result["reference_accepted"])
        self.assertFalse(result["native_agrees"])
        self.assertTrue(result["browser_agrees"])
        self.assertEqual(result["status"], "browser_matches_native_red")
        self.assertEqual(result["checks"]["coverage"]["cpu_match"]["roles"],
                         ["human", "cpu"])

    def test_reference_metadata_mismatch_is_retained_as_nonacceptance(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, references = _write_manifest(root)
            bad_metadata = root / references[1]["run_directory"] / "run-metadata.json"
            value = json.loads(bad_metadata.read_text())
            value["output_sha256"] = "f" * 64
            bad_metadata.write_text(json.dumps(value) + "\n")
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result["reference_accepted"])
        self.assertEqual(result["status"], "reference_not_accepted")
        self.assertEqual(result["checks"]["reference_b"]["status"],
                         "invalid_or_incomplete")
        self.assertIn("metadata", result["checks"]["reference_b"]["error"])

    def test_reference_pair_keeps_first_divergence(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures), mock.patch.object(
                    CHECKER, "compare_retail",
                    return_value={"repeatable": False, "status": "diverged",
                                  "first_divergence": {"record": "frame",
                                                        "frame": 17,
                                                        "field": "rng"}}):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result["reference_accepted"])
        self.assertEqual(result["checks"]["reference_repeatability"]["first_divergence"],
                         {"record": "frame", "frame": 17, "field": "rng"})

    def test_different_published_winners_cannot_form_an_accepted_pair(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures, winner_b=0):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result["reference_accepted"])
        self.assertEqual(result["status"], "reference_not_accepted")

    def test_mismatched_disc_identity_is_retained_as_nonacceptance(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, references = _write_manifest(root)
            bad_metadata = root / references[1]["run_directory"] / "run-metadata.json"
            value = json.loads(bad_metadata.read_text())
            value["identity"]["disc_image_sha256"] = "f" * 64
            bad_metadata.write_text(json.dumps(value) + "\n")
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result["reference_accepted"])
        self.assertEqual(result["status"], "reference_not_accepted")
        self.assertEqual(result["checks"]["disc_identity"]["status"],
                         "invalid_or_incomplete")
        self.assertIn("disc identities differ", result["checks"]["disc_identity"]["error"])

    def test_tied_published_winners_are_incomplete_reference_evidence(self):
        with tempfile.TemporaryDirectory(prefix="cpu-match-report-") as directory:
            root = Path(directory)
            manifest, plan_sha, _ = _write_manifest(root)
            captures = _fake_captures(root, plan_sha)
            with _checker_fakes(captures, winners_b=[0, 1]):
                result = CHECKER.check_manifest(manifest)
        self.assertFalse(result["reference_accepted"])
        self.assertFalse(result["natural_match_complete"])
        self.assertEqual(result["status"], "reference_not_accepted")


class CpuCoverageAggregationTests(unittest.TestCase):
    def test_analyze_cpu_capture_retains_roles_and_per_player_coverage(self):
        class FakeCapture:
            sha256 = "c" * 64
            match_enter = {"start_melee_hex": "00" * 0x138}
            frames = [{}, {}]

        def sample(cpu):
            return {"cpu": cpu}

        cpu0 = {
            "state": 1, "target_slot": -1, "command_bytes": "0180017f",
            "buttons": 0, "sticks": [0, 0, 0, 0], "triggers": [0, 0],
            "attack_queue": [3, 3], "defend_queue": [],
        }
        cpu1 = {
            "state": 2, "target_slot": 0, "command_bytes": "0180017f",
            "buttons": 1, "sticks": [0, 0, 0, 0], "triggers": [0, 0],
            "attack_queue": [4], "defend_queue": [8],
        }
        subject = {"state": 0, "timer": 0, "on_ledge": 0,
                   "force_inactive": 0, "was_framed": 1,
                   "position_bits": ["00000000"] * 3,
                   "bone_bits": ["00000000"] * 3}
        player0 = {"magnifier": {"offscreen": 0}, "subject": subject}
        player1 = {"magnifier": {"offscreen": 0}, "subject": subject}
        audit = types.SimpleNamespace(
            sha256="d" * 64,
            header={"source_drawing": True},
            frames=[{"match": {"frame": 0}, "players": [sample(None), sample(cpu0)]},
                    {"match": {"frame": 1}, "players": [sample(None), sample(cpu1)]}],
            initial={"players": [player0, player1], "camera": "a"},
            draws=[{"source_index": 0, "players": [player0, player1], "camera": "b"}],
            end={"result": {"outcome": 2, "winners": [0]},
                 "remaining_fighter_slots": []},
        )
        setup = {
            "players": [{"player_type": 0, "cpu_level": 0},
                        {"player_type": 1, "cpu_level": 7}],
        }
        base = {
            "players": [
                {"slot": 0, "kind": 0,
                 "motions": [{"id": 0, "name": "ftCo_MS_Wait",
                              "scope": "common", "family": "common:movement",
                              "ticks": 1, "entries": 1}],
                 "stock_losses": [{"tick": 1}], "respawns": [{"tick": 2}]},
                {"slot": 1, "kind": 1,
                 "motions": [{"id": 14, "name": "ftFx_MS_AttackN",
                              "scope": "fighter_specific", "family": "common:attack",
                              "ticks": 2, "entries": 2}],
                 "stock_losses": [{"tick": 1}], "respawns": []},
            ],
            "frames": 2,
            "scope": "coverage",
        }
        fake_capture = FakeCapture()
        completion_module = sys.modules["retail_match_completion"]
        setup_module = sys.modules["retail_setup_validation"]
        observation_module = sys.modules["cpu_observation_validation"]
        with mock.patch.object(replay_coverage, "analyze_capture", return_value=base), \
                mock.patch.object(replay_coverage, "load_capture", return_value=fake_capture), \
                mock.patch.object(setup_module, "_decode_setup", return_value=setup), \
                mock.patch.object(observation_module, "load_observation", return_value=audit), \
                mock.patch.object(completion_module, "load_match_completion",
                                  return_value={"match_result": 2}):
            result = deepcopy(replay_coverage.analyze_cpu_capture(
                "capture.jsonl", input_plan="plan.json", observation="observation.jsonl",
                completion="completion.json"))
            audit.header["source_drawing"] = False
            audit.draws = []
            base["features"] = []
            headless = deepcopy(replay_coverage.analyze_cpu_capture(
                "capture.jsonl", input_plan="plan.json", observation="observation.jsonl",
                completion="completion.json"))

        human, cpu = result["players"]
        self.assertEqual(human["role"], "human")
        self.assertNotIn("cpu", human)
        self.assertEqual(cpu["role"], "cpu")
        self.assertEqual(cpu["cpu"]["difficulty"], 7)
        self.assertEqual(cpu["cpu"]["decision_state_ticks"], {"1": 1, "2": 1})
        self.assertEqual(cpu["cpu"]["target_changes"],
                         [{"tick": 1, "from": -1, "to": 0}])
        self.assertEqual(cpu["cpu"]["initial_target_slot"], -1)
        self.assertEqual(cpu["cpu"]["target_ticks"], {"-1": 1, "0": 1})
        self.assertEqual(cpu["cpu"]["attack_queue"], {
            "observed_ids": [3, 4], "tick_counts": {"3": 1, "4": 1},
            "transitions": [{"tick": 1, "from": [3, 3], "to": [4]}],
        })
        self.assertEqual(cpu["cpu"]["defend_queue"], {
            "observed_ids": [8], "tick_counts": {"8": 1},
            "transitions": [{"tick": 1, "from": [], "to": [8]}],
        })
        self.assertEqual(cpu["cpu"]["generated_active_input_ticks"], 1)
        self.assertEqual(cpu["attack_motion_entries"], 2)
        self.assertEqual(cpu["cpu"]["source_motion_inventory"], [{
            "id": 14, "name": "ftFx_MS_AttackN", "scope": "fighter_specific",
            "family": "common:attack", "ticks": 2, "entries": 2,
        }])
        self.assertEqual(len(human["stock_losses"]), 1)
        self.assertEqual(len(human["respawns"]), 1)
        self.assertEqual(result["cpu_match"]["cpu_difficulties"], [7])
        self.assertEqual(result["cpu_match"]["roles"], ["human", "cpu"])
        self.assertEqual(result["cpu_match"]["drawing"], {
            "available": True, "phase": "source_draws", "source_draws": 1,
        })
        self.assertEqual(result["cpu_match"]["camera_transform_changes"], 1)
        self.assertIn("cpu-level:1:7", result["features"])
        self.assertIn("cpu-target:1:0", result["features"])
        self.assertIn("cpu-queue:1:attack:3", result["features"])
        self.assertIn("cpu-family:1:common:attack", result["features"])
        self.assertIn("camera:transform-change", result["features"])
        self.assertIn("ending:result:2", result["features"])

        headless_cpu = headless["players"][1]
        self.assertEqual(headless["cpu_match"]["drawing"], {
            "available": False, "phase": "simulation_frames_only", "source_draws": 0,
        })
        self.assertIsNone(headless["cpu_match"]["camera_transform_changes"])
        self.assertIsNone(headless_cpu["magnifier_events"])
        self.assertIsNone(headless_cpu["offscreen_draws"])
        self.assertIsNone(headless_cpu["camera_subject_events"])
        self.assertIn("drawing:headless", headless["features"])
        self.assertNotIn("camera:transform-change", headless["features"])


class CpuObservationTerminalTests(unittest.TestCase):
    @staticmethod
    def _snapshot(slot, *, cpu_value=None):
        subject = {"state": 0, "timer": 0, "on_ledge": 0,
                   "force_inactive": 0, "was_framed": 0,
                   "position_bits": ["00000000"] * 3,
                   "bone_bits": ["00000000"] * 3}
        return {
            "slot": slot,
            "cpu": cpu_value,
            "subject": subject,
            "magnifier": {"offscreen": 0, "ignore_offscreen": 0, "edge": 0},
            "hud": {"present": True, "damage": 0, "old_damage": 0,
                    "last_attack_damage": 0, "shake_frames": 0,
                    "explode": 0, "randomize_velocity": 0,
                    "force_shake": 0, "hide_digits": 0,
                    "animation_status": 0},
        }

    def test_loader_accepts_and_preserves_published_terminal_result(self):
        setup = bytearray(0x138)
        setup[0x61] = 0
        setup[0x61 + 0x24] = 1
        for slot in range(2, 6):
            setup[0x61 + slot * 0x24] = 3
        cpu_value = {
            "kind": 4, "level": 5, "state": 0, "default_state": 0,
            "secondary_state": 0, "target_slot": -1, "buttons": 0,
            "sticks": [0, 0, 0, 0], "triggers": [0, 0],
            "command_duration": 0, "command_cursor": -1,
            "command_bytes": "", "defend_queue": [], "attack_queue": [],
        }
        snapshots = [self._snapshot(0), self._snapshot(1, cpu_value=cpu_value)]
        common = {"match": {"frame": 0, "seconds": 0, "subframe": 0,
                             "outcome": 0, "end_state": 0},
                  "camera": {"position_bits": ["00000000"] * 3,
                             "interest_bits": ["00000000"] * 3,
                             "projection": 1, "fov_bits": "3f800000",
                             "near_bits": "3f800000", "far_bits": "3f800000"},
                  "players": snapshots}
        rows = [
            {"record": "header", "schema": "melee-web-cpu-observation",
             "version": 1, "frames_requested": 1, "source_drawing": True,
             "setup_hex": setup.hex()},
            dict({"record": "initial"}, **common),
            dict({"record": "frame", "index": 0}, **common),
            dict({"record": "draw", "index": 0, "source_index": 0}, **common),
            {"record": "end", "frames": 1, "draws": 1,
             "remaining_fighter_slots": [],
             "result": {"outcome": 2, "winners": [1]}, "status": "captured"},
        ]
        with tempfile.TemporaryDirectory(prefix="cpu-observation-") as directory:
            path = Path(directory) / "observation.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows))
            observation = cpu_observation_validation.load_observation(path)
        self.assertEqual(observation.end["result"], {"outcome": 2, "winners": [1]})


if __name__ == "__main__":
    unittest.main()
