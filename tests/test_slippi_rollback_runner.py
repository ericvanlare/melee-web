# SPDX-License-Identifier: MIT
"""Sensitivity and retained-failure checks for the desktop rollback runner."""
from dataclasses import replace
import copy
import hashlib
import json
import os
import struct
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference-capture/slippi"))
from run_rollback import (RollbackRun, read_diagnostic_log, verify_log_order, verify_profile,
                          ARTIFACT_IDENTITY_FIELDS, artifact_identities,
                          _expected_downstream_patch_identities,
                          validate_baseline_game,
                          validate_baseline_receipts,
                          validate_service_lineage,
                          verify_prediction_rollbacks, verify_recording_clock, verify_transport,
                          verify_speculative_corrections)
import run_local as run_local_module
from test_compare_rollback import timeline
from test_slippi_format import timeline_fixture, _pre_frame, _post_frame
from tools.slippi_format import SlippiFrame
from slippi_format import RAW_PREFIX, _raw_stream, decode_timeline
from tools.slippi_rollback_diagnostic import PROFILE_NAME, SCHEMA


def fixture():
    base = timeline()
    sample = base.frames[0].inputs[0]
    rows = [{"event": "input_profile_assignment", "game_sequence": 0,
             "role": 1, "local_player_port": 1}]
    generated = {source: bytes(8) for source in range(1, 201)}
    for first, last, pad in ((90, 139, bytes.fromhex("00007f0000000000")),
                            (150, 200, bytes.fromhex("0000810000000000"))):
        for source in range(first, last + 1):
            generated[source] = pad
    for source, pad in generated.items():
        rows.append({"event": "input_profile", "observer_context": "exi", "game_sequence": 0,
                     "source_frame": source, "consumed_frame": source + 2, "delay": 2,
                     "role": 1, "generated_pad_hex": pad.hex(),
                     "pad_after_hex": pad.hex() + "01020304", "preserved_tail_hex": "01020304"})
    frames = []
    for consumed in range(0, 203):
        pad = generated.get(consumed - 2, bytes(8))
        x = pad[2] if pad[2] < 128 else pad[2] - 256
        inputs = (replace(sample, frame=consumed - 123, raw_stick=(x, 0)),)
        frames.append(SlippiFrame(consumed - 123, 1, consumed, inputs, ()))
    return replace(base, frames=tuple(frames)), rows


def recording_frame_starts(*, target_occurrences=1):
    values = [(-123, 0)] + [(-122, 1)] * target_occurrences
    values.append((-121, 2))
    return [{"frame": frame, "rng": (0xB0000000 + frame) & 0xFFFFFFFF,
             "scene_frame": scene, "event_sequence": index * 10,
             "occurrence": index}
            for index, (frame, scene) in enumerate(values)]


def repeated_revision_fixture(*, finalized_occurrences):
    """Make two native rewind occurrences for the same recorded scene.

    Each Frame Start owns the PRE/POST revisions following it.  The first
    occurrence may intentionally have only the speculative pair; the old
    global revision scan would incorrectly borrow the later occurrence's final
    pair for it.
    """
    base = timeline_fixture()
    capture = decode_timeline(base)
    raw = _raw_stream(base)
    table_end = 1 + raw[1]
    events = []
    cursor = table_end
    while cursor < len(raw):
        command = raw[cursor]
        size = capture.header.event_payload_sizes[command]
        events.append(raw[cursor:cursor + size + 1])
        cursor += size + 1
    prefix = []
    suffix = []
    groups = []
    current = None
    for event in events:
        if event[0] == 0x3A:
            if current is not None:
                groups.append(current)
            current = [event]
        elif event[0] == 0x39:
            if current is not None:
                groups.append(current)
                current = None
            suffix.append(event)
        elif current is None:
            prefix.append(event)
        else:
            current.append(event)
    if current is not None:
        groups.append(current)
    target_group = next(group for group in groups
                        if struct.unpack_from(">i", group[0], 1)[0] == -122)

    def target_occurrence(include_final):
        if include_final:
            remote = (_pre_frame(-122, 0), _post_frame(-122, 0, action=14))
        else:
            remote = (_pre_frame(-122, 0, buttons=0x200),
                      _post_frame(-122, 0, action=15))
        value = [target_group[0], *remote, _pre_frame(-122, 1),
                 _post_frame(-122, 1)]
        value.append(target_group[-1])
        return value

    replacement = []
    for group in groups:
        frame = struct.unpack_from(">i", group[0], 1)[0]
        if frame == -122:
            for occurrence in range(4):
                replacement.extend(target_occurrence(occurrence in finalized_occurrences))
        else:
            replacement.extend(group)
    new_raw = raw[:table_end] + b"".join(prefix + replacement + suffix)
    return RAW_PREFIX + struct.pack(">I", len(new_raw)) + new_raw + b"ignored metadata"


def repeated_loads():
    return [
        {"state_scene_frame": 1, "end_scene_frame": 2, "load_event_sequence": 15,
         "load_complete_event_sequence": 16,
         "recording_frame_start_occurrences": [
             {"frame": -122, "rng": (0xB0000000 - 122) & 0xFFFFFFFF,
              "scene_frame": 1, "event_sequence": 20, "occurrence": 2,
              "pre_rewind_occurrence": 1}]},
        {"state_scene_frame": 1, "end_scene_frame": 2, "load_event_sequence": 35,
         "load_complete_event_sequence": 36,
         "recording_frame_start_occurrences": [
             {"frame": -122, "rng": (0xB0000000 - 122) & 0xFFFFFFFF,
              "scene_frame": 1, "event_sequence": 40, "occurrence": 4,
              "pre_rewind_occurrence": 3}]},
    ]


class RollbackRunnerEvidenceTests(unittest.TestCase):
    def _write_baseline_receipt_fixture(self, root):
        service = {
            "service_public_ref": "codex/local-api-framing-pr",
            "service_public_commit": "0" * 40,
            "service_public_parent_commit": "1" * 40,
            "service_public_path_diff_sha256": "2" * 64,
            "service_base_server_cpp_sha256": "3" * 64,
            "service_integration_test_sha256": "4" * 64,
            "service_framing_lineage_sha256": "a" * 64,
            "service_binary_sha256": "b" * 64,
            "service_build_cache_sha256": "c" * 64,
            "service_source_inventory_sha256": "d" * 64,
            "service_source_sha256": "d" * 64,
            "service_source_patch_sha256": "e" * 64,
            "service_worktree_head": "f" * 40,
        }
        identities = {key: ("1" * 64 if key != "service_framing_lineage_sha256"
                            else service["service_framing_lineage_sha256"])
                      for key in ARTIFACT_IDENTITY_FIELDS}
        identities["downstream_patch_sha256"] = _expected_downstream_patch_identities()
        identities["matchmaker_sha256"] = service["service_binary_sha256"]
        for cycle in (1, 2):
            cycle_root = root / f"cycle-{cycle:02d}"
            cycle_root.mkdir()
            for name, role in (("p1", 1), ("p2", 2)):
                (cycle_root / f"{name}.slp").write_bytes(f"cycle{cycle}-{name}".encode())
                (cycle_root / f"{name}-rollback-config.json").write_text(json.dumps({
                    "schema": SCHEMA, "enabled": True, "stage_id": 32,
                    "rng_offset": 0x1234, "overlay": None,
                    "transport": {"action": "none", "frame": None,
                                   "release_frame": None},
                    "input_profile": {"name": PROFILE_NAME, "role": role},
                }) + "\n")
            for game in (1, 2):
                replays = {name: {"file": f"{name}.slp",
                                  "sha256": hashlib.sha256(
                                      (cycle_root / f"{name}.slp").read_bytes()).hexdigest()}
                           for name in ("p1", "p2")}
                comparison = {"result": "passed", "game": game, "scenario": "none",
                              "peer_comparison": {"result": "passed",
                                                  "compared_frames": 1342,
                                                  "first_divergence": None},
                              "replays": replays}
                (cycle_root / f"game-{game:02d}-comparison.json").write_text(
                    json.dumps(comparison) + "\n")
            evidence = {"result": "passed", "service_framing": service, **identities}
            (cycle_root / "evidence.json").write_text(json.dumps(evidence) + "\n")
        rollback = {"result": "passed", "scenario": "none",
                    "fresh_profile_repeatability_verified": True,
                    "rollback_correctness_claimed": False,
                    "cycles": [{"cycle": cycle, "result": "passed",
                                "evidence": f"cycle-{cycle:02d}/evidence.json"}
                               for cycle in (1, 2)]}
        (root / "rollback-run.json").write_text(json.dumps(rollback) + "\n")
        return service

    def test_baseline_admission_binds_both_cycles_comparisons_and_service(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            binding = validate_baseline_receipts(root, service)
            self.assertEqual(binding["artifact_identity"]["service_framing_lineage_sha256"],
                             service["service_framing_lineage_sha256"])
            changed = json.loads((root / "cycle-02/evidence.json").read_text())
            changed["matchmaker_sha256"] = "9" * 64
            (root / "cycle-02/evidence.json").write_text(json.dumps(changed) + "\n")
            with self.assertRaisesRegex(ValueError, "service binary"):
                validate_baseline_receipts(root, service)

    def test_actual_retained_job_e04_cycle_identity_map_admits(self):
        evidence_path = os.environ.get("MELEE_E04_CYCLE_EVIDENCE")
        if not evidence_path:
            self.skipTest("MELEE_E04_CYCLE_EVIDENCE is not set")
        path = Path(evidence_path).expanduser().resolve(strict=True)
        evidence = json.loads(path.read_text(encoding="utf-8"))
        identities = artifact_identities(evidence)
        self.assertEqual(identities["downstream_patch_sha256"],
                         _expected_downstream_patch_identities())

    def test_downstream_patch_identity_rejects_path_hash_and_type_mutations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            evidence = json.loads((root / "cycle-01/evidence.json").read_text())
            expected = dict(evidence["downstream_patch_sha256"])
            cases = (
                (dict(expected, **{"patches/unlocked.patch": "1" * 64}),
                 "path set differs"),
                ({key: value for key, value in list(expected.items())[:-1]},
                 "path set differs"),
                (dict(expected, **{next(iter(expected)): "0" * 64}),
                 "differs from patch bytes"),
                (dict(expected, **{next(iter(expected)): True}),
                 "is not a lowercase SHA-256 identity"),
                ("1" * 64, "is not a patch identity map"),
            )
            for value, message in cases:
                with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                    artifact_identities(dict(evidence, downstream_patch_sha256=value))
            binding = validate_baseline_receipts(root, service)
            self.assertEqual(binding["artifact_identity"]["downstream_patch_sha256"], expected)
            changed = json.loads((root / "cycle-02/evidence.json").read_text())
            changed["downstream_patch_sha256"] = dict(expected)
            changed["downstream_patch_sha256"][next(iter(expected))] = "0" * 64
            (root / "cycle-02/evidence.json").write_text(json.dumps(changed) + "\n")
            with self.assertRaisesRegex(ValueError, "differs from patch bytes"):
                validate_baseline_receipts(root, service)

    def test_service_lineage_requires_public_ref_and_all_source_identities(self):
        value = {
            "schema": "melee-web-service-lineage-v2",
            "build_profile": {"matchmaker_release_build_verified": True},
            "service_base_server_cpp_sha256": "c952d06734050285d99e52f620f86b23849439f605ed72faff818520ac55c15a",
            "service_binary_sha256": "2" * 64,
            "service_build_cache_sha256": "3" * 64,
            "service_source_inventory_sha256": "4" * 64,
            "service_integration_test_sha256": "37f3b2ca979e4012f4a425ce069a91a8a374d299ac87367a1c0891449c58169c",
            "service_public_commit": "cdc4b899ee85861cce7f2cebfc9d756a30326ab6",
            "service_public_parent_commit": "473e97c41633043562bc5015e1add630d23b70b3",
            "service_public_path_diff_sha256": "cd2fe85202f0315b0e8c216e05447cd9fdfba6b5fb6044f903daeb579eeeb856",
            "service_public_ref": "codex/local-api-framing-pr",
            "service_source_patch_sha256": "cd2fe85202f0315b0e8c216e05447cd9fdfba6b5fb6044f903daeb579eeeb856",
            "service_source_sha256": "78b680a16ff717e61cbb561281240a9a205a105ac4053634441fb88941fce719",
            "service_worktree_head": "b50684772ba88c795c09bd03ada3ee7adab0f204",
        }
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "service-lineage.json"
            path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")
            identity = validate_service_lineage(path)
            self.assertEqual(identity["service_public_commit"], value["service_public_commit"])
            self.assertEqual(identity["service_binary_sha256"], value["service_binary_sha256"])
            self.assertEqual(identity["service_build_cache_sha256"],
                             value["service_build_cache_sha256"])
            changed = dict(value, service_public_parent_commit="0" * 40)
            path.write_text(json.dumps(changed, indent=2, sort_keys=True) + "\n")
            with self.assertRaises(ValueError):
                validate_service_lineage(path)

    def test_service_lineage_generator_binds_fresh_outputs_and_refuses_tamper(self):
        lock = json.loads((ROOT / "reference-capture/slippi/client.lock.json").read_text())
        prerequisite = lock["service_prerequisite"]
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_patch = root / "service-paths.patch"
            cache = root / "CMakeCache.txt"
            client = root / "client"
            service = root / "service"
            output = root / "lineage.json"
            for path in (source_patch, cache, client, service):
                path.write_bytes(path.name.encode())
            expected_hashes = {
                source_patch: prerequisite["public_path_diff_sha256"],
                cache: "1" * 64,
                service: "2" * 64,
                run_local_module.HERE / "local_matchmaker/server.cpp": prerequisite["source_sha256"],
                run_local_module.HERE / "local_matchmaker/integration_test.cpp":
                    prerequisite["integration_test_sha256"],
            }

            def fake_sha(path):
                path = Path(path)
                return expected_hashes.get(path, hashlib.sha256(path.read_bytes()).hexdigest())

            def invoke(destination=output):
                with patch.object(run_local_module, "_verify_pinned_client_source", return_value={}), \
                     patch.object(run_local_module, "_verify_build_profile", return_value={
                         "matchmaker_release_build_verified": True}), \
                     patch.object(run_local_module, "_sha256", side_effect=fake_sha), \
                     patch.object(run_local_module.subprocess, "run", return_value=SimpleNamespace(
                         returncode=0, stdout="f" * 40 + "\n")):
                    return run_local_module.generate_service_lineage(
                        lock, dolphin_source=root, enet_source=root,
                        dolphin_build=root, matchmaker_build=root,
                        client_binary=client, service_binary=service,
                        source_patch=source_patch, output=destination)

            receipt = invoke()
            self.assertEqual(receipt["service_binary_sha256"], "2" * 64)
            self.assertEqual(receipt["service_build_cache_sha256"], "1" * 64)
            self.assertEqual(receipt["service_source_patch_sha256"],
                             prerequisite["public_path_diff_sha256"])
            with self.assertRaises(FileExistsError):
                invoke()

            original = expected_hashes[source_patch]
            expected_hashes[source_patch] = "3" * 64
            with self.assertRaisesRegex(RuntimeError, "reviewed public"):
                invoke(root / "tampered.json")
            expected_hashes[source_patch] = original
            with patch.object(run_local_module, "_verify_pinned_client_source", return_value={}), \
                 patch.object(run_local_module, "_verify_build_profile",
                              side_effect=RuntimeError("stale configured service output")):
                with self.assertRaisesRegex(RuntimeError, "stale configured"):
                    run_local_module.generate_service_lineage(
                        lock, dolphin_source=root, enet_source=root,
                        dolphin_build=root, matchmaker_build=root,
                        client_binary=client, service_binary=service,
                        source_patch=source_patch)

    def test_baseline_admission_rejects_stale_cycle_or_comparison(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            changed = json.loads((root / "cycle-02/game-02-comparison.json").read_text())
            changed["peer_comparison"]["result"] = "failed"
            (root / "cycle-02/game-02-comparison.json").write_text(json.dumps(changed) + "\n")
            with self.assertRaisesRegex(ValueError, "passed no-impairment"):
                validate_baseline_receipts(root, service)

    def test_baseline_admission_rejects_tampered_service_lineage_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            changed = json.loads((root / "cycle-02/evidence.json").read_text())
            changed["service_framing"] = dict(service, service_source_sha256="9" * 64)
            (root / "cycle-02/evidence.json").write_text(json.dumps(changed) + "\n")
            with self.assertRaisesRegex(ValueError, "source/build identity"):
                validate_baseline_receipts(root, service)

    def test_baseline_admission_rejects_boolean_cycle_and_config_fields(self):
        for relative, mutate in (
                ("rollback-run.json", lambda value: value["cycles"].__setitem__(1, dict(
                    value["cycles"][1], cycle=True))),
                ("cycle-02/p2-rollback-config.json", lambda value: value["input_profile"].__setitem__(
                    "role", True))):
            with self.subTest(relative=relative):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    service = self._write_baseline_receipt_fixture(root)
                    path = root / relative
                    changed = json.loads(path.read_text())
                    mutate(changed)
                    path.write_text(json.dumps(changed) + "\n")
                    with self.assertRaises(ValueError):
                        validate_baseline_receipts(root, service)

    def test_baseline_admission_rejects_wrong_cycle_replay_and_profile_config(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            cycle_two = root / "cycle-02"
            replay = cycle_two / "p1.slp"
            # A symlink to cycle 01 must not satisfy the cycle-02 binding.
            replay.unlink()
            replay.symlink_to(root / "cycle-01/p1.slp")
            with self.assertRaisesRegex(ValueError, "escaped its cycle"):
                validate_baseline_receipts(root, service)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            service = self._write_baseline_receipt_fixture(root)
            config_path = root / "cycle-02/p2-rollback-config.json"
            config = json.loads(config_path.read_text())
            config["input_profile"]["name"] = "wrong-profile"
            config_path.write_text(json.dumps(config) + "\n")
            with self.assertRaisesRegex(ValueError, "pinned none profile"):
                validate_baseline_receipts(root, service)

            config["input_profile"]["name"] = PROFILE_NAME
            config["transport"]["frame"] = 98
            config_path.write_text(json.dumps(config) + "\n")
            with self.assertRaisesRegex(ValueError, "pinned none profile"):
                validate_baseline_receipts(root, service)
    def test_failed_incomplete_or_wrong_baseline_game_is_refused(self):
        receipt = {"result": "passed", "scenario": "none", "game": 1,
                   "peer_comparison": {"result": "passed"}}
        validate_baseline_game(receipt, 1)
        for key, value in (("result", "failed"), ("result", "incomplete"),
                           ("scenario", "hold"), ("game", 2),
                           ("peer_comparison", {"result": "failed"}),
                           ("peer_comparison", {})):
            with self.subTest(key=key, value=value):
                with self.assertRaisesRegex(ValueError, "passed no-impairment peer comparison"):
                    validate_baseline_game(dict(receipt, **{key: value}), 1)

    def test_actual_wrong_input_and_post_state_must_be_corrected_to_finalized_pair(self):
        data = repeated_revision_fixture(finalized_occurrences={3})
        capture = decode_timeline(data)
        loads = [{"state_scene_frame": 1, "end_scene_frame": 2, "load_event_sequence": 35,
                  "load_complete_event_sequence": 36,
                  "recording_frame_start_occurrences": [
                      {"frame": -122, "rng": (0xB0000000 - 122) & 0xFFFFFFFF,
                       "scene_frame": 1, "event_sequence": 40,
                       "occurrence": 4, "pre_rewind_occurrence": 3}]}]
        starts = recording_frame_starts(target_occurrences=4)
        report = verify_speculative_corrections(
            data, capture, loads, remote_port=1, recording_frame_starts=starts)
        correction = report["observed_corrections"][0]
        self.assertEqual(correction["recorded_frame"], -122)
        self.assertEqual(correction["input_difference"]["field"], "physical_input.buttons")
        self.assertEqual(correction["post_state_difference"]["field"], "post_state.action_state")
        raw = _raw_stream(data)
        changes = (
            raw.replace(_pre_frame(-122, 0), _pre_frame(-122, 0, buttons=0x200)),
            raw.replace(_post_frame(-122, 0), _post_frame(-122, 0, action=15)),
            raw.replace(_pre_frame(-122, 0, buttons=0x200) + _post_frame(-122, 0, action=15), b""),
        )
        for changed_raw in changes:
            changed = RAW_PREFIX + struct.pack(">I", len(changed_raw)) + changed_raw
            with self.assertRaises(ValueError):
                verify_speculative_corrections(
                    changed, decode_timeline(changed), loads, remote_port=1,
                    recording_frame_starts=starts)
        with self.assertRaises(ValueError):
            verify_speculative_corrections(
                data, capture,
                [dict(loads[0], state_scene_frame=0, end_scene_frame=1)],
                remote_port=1, recording_frame_starts=starts)
        with self.assertRaisesRegex(ValueError, "validated finalized timeline"):
            verify_speculative_corrections(
                data, replace(capture, duplicate_updates=99), loads, remote_port=1,
                recording_frame_starts=starts)

    def test_repeated_load_corrections_are_bound_to_their_frame_start_occurrence(self):
        starts = recording_frame_starts(target_occurrences=4)
        loads = repeated_loads()
        cross_satisfied = repeated_revision_fixture(finalized_occurrences={3})
        with self.assertRaisesRegex(ValueError, "wrong-input/state revision"):
            verify_speculative_corrections(
                cross_satisfied, decode_timeline(cross_satisfied), loads, remote_port=1,
                recording_frame_starts=starts)

        valid = repeated_revision_fixture(finalized_occurrences={1, 3})
        report = verify_speculative_corrections(
            valid, decode_timeline(valid), loads, remote_port=1,
            recording_frame_starts=starts)
        self.assertEqual(report["correction_count"], 2)
        self.assertEqual(
            [row["recording_frame_start_occurrence"] for row in report["observed_corrections"]],
            [2, 4])

    def test_repeated_load_pairing_gap_and_occurrence_reuse_are_refused(self):
        starts = recording_frame_starts(target_occurrences=4)
        valid = repeated_revision_fixture(finalized_occurrences={1, 3})
        loads = repeated_loads()
        swapped = copy.deepcopy(loads)
        swapped[0]["load_event_sequence"] = 30
        swapped[0]["load_complete_event_sequence"] = 31
        with self.assertRaisesRegex(ValueError, "recording occurrence is outside its load interval"):
            verify_speculative_corrections(
                valid, decode_timeline(valid), swapped, remote_port=1,
                recording_frame_starts=starts)
        gap = repeated_revision_fixture(finalized_occurrences={1, 3})
        raw = _raw_stream(gap)
        # Removing the final POST from occurrence 2 leaves no complete final
        # pair in that occurrence; an older occurrence must not repair it.
        final_post = _post_frame(-122, 0, action=14)
        raw = raw.replace(final_post, b"", 1)
        gap = RAW_PREFIX + struct.pack(">I", len(raw)) + raw
        with self.assertRaises(ValueError):
            verify_speculative_corrections(
                gap, decode_timeline(gap), loads, remote_port=1,
                recording_frame_starts=starts)

    def test_changed_baseline_client_is_refused_before_profile_or_process_creation(self):
        run = RollbackRun.__new__(RollbackRun)
        run.evidence = {key: "1" * 64 for key in ARTIFACT_IDENTITY_FIELDS}
        run.evidence["downstream_patch_sha256"] = _expected_downstream_patch_identities()
        run.baseline_identities = artifact_identities(run.evidence)
        run.evidence["client_binary_sha256"] = "changed native client"
        with patch("run_local.PairRun._make_profiles") as make_profiles:
            with self.assertRaisesRegex(ValueError, "baseline_artifact_identity.client_binary_sha256"):
                run._make_profiles(None)
            make_profiles.assert_not_called()

    def test_prediction_flags_load_completion_and_source_resimulation_are_required(self):
        odb = {"frame": 102, "savestate_frame": 98, "stable_savestate_frame": 98,
               "savestate_is_predicting": 1, "rollback_active": 1,
               "rollback_should_load_state": 1, "stable_rollback_active": 1,
               "stable_rollback_should_load_state": 1,
               "rollback_end_frame": 101, "stable_rollback_end_frame": 101}
        rows = [{"event": "recording_game_start"},
                {"event": "savestate_capture", "frame": 98},
                {"event": "savestate_capture_complete", "frame": 98, "odb": odb}]
        rows += [{"event": "recording_frame_start", "frame": scene - 123,
                  "rng": (0xB0000000 + scene - 123) & 0xFFFFFFFF,
                  "scene_frame": scene} for scene in (98, 99, 100)]
        rows += [{"event": "savestate_load", "frame": 98, "odb": odb},
                 {"event": "savestate_load_complete", "frame": 98}]
        rows += [{"event": "recording_frame_start", "frame": scene - 123,
                  "rng": (0xB0000000 + scene - 123) & 0xFFFFFFFF,
                  "scene_frame": scene} for scene in (98, 99, 100)]
        rows += [{"event": "online_inputs", "source_frame": 102,
                  "odb": {key: 0 for key in ("rollback_active", "rollback_should_load_state",
                                            "stable_rollback_active", "stable_rollback_should_load_state")}}]
        for row in rows:
            if row["event"] in ("savestate_capture_complete", "savestate_load"):
                row["preservation"] = {"odb_address": 1, "odb_size": 3311, "rxb_address": 2,
                                       "rxb_size": 297, "sscb_address": 3, "sscb_size": 158}
        rows = [dict(row, observer_context="exi", event_sequence=index) for index, row in enumerate(rows)]
        report = verify_prediction_rollbacks(rows, game_number=1)
        self.assertEqual(report["depths"], [3])
        self.assertEqual(report["prediction_error_loads"][0]["repeated_scene_frames"], [98, 99, 100])
        self.assertEqual(
            [(value["pre_rewind_occurrence"], value["occurrence"])
             for value in report["prediction_error_loads"][0]["recording_frame_start_occurrences"]],
            [(0, 3), (1, 4), (2, 5)])
        for index, key, value in ((2, "savestate_is_predicting", 0),
                                  (6, "stable_rollback_should_load_state", 0),
                                  (6, "stable_rollback_end_frame", 106)):
            changed = copy.deepcopy(rows)
            changed[index]["odb"][key] = value
            with self.assertRaises(ValueError):
                verify_prediction_rollbacks(changed, game_number=1)
        for changed in (rows[:7] + rows[8:], rows[:8] + rows[9:], rows[:-1]):
            with self.assertRaises(ValueError):
                verify_prediction_rollbacks(changed, game_number=1)
        changed = copy.deepcopy(rows)
        changed[6]["preservation"]["sscb_address"] = 99
        with self.assertRaises(ValueError):
            verify_prediction_rollbacks(changed, game_number=1)

    def test_prediction_loads_cannot_borrow_a_later_same_scene_callback(self):
        odb = {"frame": 102, "savestate_frame": 98, "stable_savestate_frame": 98,
               "savestate_is_predicting": 1, "rollback_active": 1,
               "rollback_should_load_state": 1, "stable_rollback_active": 1,
               "stable_rollback_should_load_state": 1,
               "rollback_end_frame": 99, "stable_rollback_end_frame": 99}
        preservation = {"odb_address": 1, "odb_size": 3311, "rxb_address": 2,
                        "rxb_size": 297, "sscb_address": 3, "sscb_size": 158}

        def frame_start():
            return {"event": "recording_frame_start", "frame": -25,
                    "rng": (0xB0000000 - 25) & 0xFFFFFFFF, "scene_frame": 98}

        def make_rows(*, include_first_post):
            rows = [
                {"event": "recording_game_start"},
                {"event": "savestate_capture", "frame": 98},
                {"event": "savestate_capture_complete", "frame": 98,
                 "odb": odb, "preservation": preservation},
                frame_start(),
                {"event": "savestate_load", "frame": 98, "odb": odb,
                 "preservation": preservation},
                {"event": "savestate_load_complete", "frame": 98},
            ]
            if include_first_post:
                rows.append(frame_start())
            rows.extend([
                {"event": "online_inputs", "source_frame": 100,
                 "odb": {key: 0 for key in ("rollback_active", "rollback_should_load_state",
                                              "stable_rollback_active", "stable_rollback_should_load_state")}},
                {"event": "savestate_capture", "frame": 98},
                {"event": "savestate_capture_complete", "frame": 98,
                 "odb": odb, "preservation": preservation},
                {"event": "savestate_load", "frame": 98, "odb": odb,
                 "preservation": preservation},
                {"event": "savestate_load_complete", "frame": 98},
                frame_start(),
                {"event": "online_inputs", "source_frame": 101,
                 "odb": {key: 0 for key in ("rollback_active", "rollback_should_load_state",
                                              "stable_rollback_active", "stable_rollback_should_load_state")}},
            ])
            return [dict(row, observer_context="exi", event_sequence=index)
                    for index, row in enumerate(rows)]

        valid = verify_prediction_rollbacks(make_rows(include_first_post=True), game_number=1)
        self.assertEqual(valid["observed_load_count"], 2)
        self.assertEqual(
            [load["recording_frame_start_occurrences"][0]["occurrence"]
             for load in valid["prediction_error_loads"]], [1, 2])
        with self.assertRaisesRegex(ValueError, "ordered recording callback interval"):
            verify_prediction_rollbacks(make_rows(include_first_post=False), game_number=1)

    def test_malformed_or_truncated_diagnostic_rows_are_not_skipped(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "diagnostic.jsonl"
            path.write_text('{"event":"start"}\n')
            self.assertEqual(read_diagnostic_log(path), [{"event": "start"}])
            for data in (b'', b'{"event":"start"}', b'{}\nbad row\n{}\n', b'[]\n', b'\xff\n'):
                path.write_bytes(data)
                with self.assertRaises((ValueError, UnicodeDecodeError)):
                    read_diagnostic_log(path)

    def test_fault_release_must_match_original_packet_order_and_schedule(self):
        held = [{"event": "pad_transport", "observer_context": "transport", "game_sequence": 0,
                 "action": "hold_begin" if index == 0 else "hold", "packet_frame": frame,
                 "payload_hash": f"{frame:016x}", "begin_count": 1 if index == 0 else 0,
                 "held_count": index + 1, "history_count": 3,
                 "history_first_frame": frame - 2, "history_last_frame": frame}
                for index, frame in enumerate(range(98, 104))]
        release = {"event": "pad_transport", "observer_context": "transport", "game_sequence": 0,
                   "action": "hold_release", "release_packet_frame": 104,
                   "release_payload_hash": "0123456789abcdef", "release_count": 6,
                   "held_frames": list(range(98, 104)),
                   "held_payload_hashes": [row["payload_hash"] for row in held],
                   "history_count": 7, "history_first_frame": 98, "history_last_frame": 104}
        rows = held + [release]
        self.assertTrue(verify_transport(rows, game_sequence=0, scenario="hold", role=2)
                        ["released_original_packets_in_order"])
        reversed_hashes = list(reversed(release["held_payload_hashes"]))
        for changed in (rows[1:], rows[:-1],
                        held + [dict(release, held_payload_hashes=reversed_hashes)],
                        held + [dict(release, release_packet_frame=105)],
                        [dict(held[0], held_count=True)] + rows[1:],
                        held[:-1] + [dict(release, release_count=True)],
                        [dict(held[0], history_count=99)] + rows[1:]):
            with self.assertRaises(ValueError):
                verify_transport(changed, game_sequence=0, scenario="hold", role=2)
        for index, key in ((0, "payload_hash"), (-1, "release_payload_hash")):
            for value in (None, "", "not-hex", 123):
                changed = copy.deepcopy(rows)
                changed[index][key] = value
                with self.assertRaisesRegex(ValueError, "native FNV-1a64 identity"):
                    verify_transport(changed, game_sequence=0, scenario="hold", role=2)
            changed = copy.deepcopy(rows)
            del changed[index][key]
            with self.assertRaisesRegex(ValueError, "native FNV-1a64 identity"):
                verify_transport(changed, game_sequence=0, scenario="hold", role=2)
        with self.assertRaises(ValueError):
            verify_transport(rows, game_sequence=0, scenario="none", role=2)
        with self.assertRaises(ValueError):
            verify_transport(rows, game_sequence=0, scenario="hold", role=1)

    def test_role2_duplicate_requires_one_frame98_event_and_exact_packet_bytes(self):
        duplicate = {
            "event": "pad_transport", "observer_context": "transport", "game_sequence": 0,
            "action": "duplicate", "role": 2, "packet_frame": 98,
            "payload_hash": "0123456789abcdef", "duplicate_payload_hash": "0123456789abcdef",
            "duplicate_count": 1, "history_count": 3,
            "history_first_frame": 96, "history_last_frame": 98,
        }
        report = verify_transport([duplicate], game_sequence=0,
                                  scenario="duplicate", role=2)
        self.assertTrue(report["exact_payload_and_history_preserved"])
        cases = [
            ([], "missing duplicate"),
            ([duplicate, dict(duplicate)], "extra duplicate"),
            ([dict(duplicate, action="drop")], "wrong action"),
            ([dict(duplicate, packet_frame=97)], "wrong frame"),
            ([dict(duplicate, role=1)], "wrong role"),
            ([dict(duplicate, duplicate_count=2)], "wrong count"),
            ([dict(duplicate, duplicate_count=True)], "boolean count"),
            ([dict(duplicate, duplicate_payload_hash="fedcba9876543210")], "changed copy"),
            ([dict(duplicate, history_last_frame=99)], "changed history"),
        ]
        for rows, label in cases:
            with self.subTest(label=label), self.assertRaises(ValueError):
                verify_transport(rows, game_sequence=0, scenario="duplicate", role=2)
        with self.assertRaises(ValueError):
            verify_transport([duplicate], game_sequence=0, scenario="duplicate", role=1)

        receiver = [
            {"event": "pad_transport_receive", "observer_context": "transport",
             "game_sequence": 0, "packet_frame": 98, "packet_player_port": 1,
             "receiver_port": 1, "payload_hash": duplicate["payload_hash"],
             "inputs_to_copy": 1},
            {"event": "pad_transport_receive", "observer_context": "transport",
             "game_sequence": 0, "packet_frame": 98, "packet_player_port": 1,
             "receiver_port": 1, "payload_hash": duplicate["payload_hash"],
             "inputs_to_copy": 0},
        ]
        self.assertEqual(verify_transport(receiver, game_sequence=0,
                                          scenario="duplicate", role=1)
                         ["receiver"]["receiver_event_count"], 2)
        for changed in (
                [dict(receiver[0], inputs_to_copy=True), receiver[1]],
                [dict(receiver[0], inputs_to_copy=0), receiver[1]],
                [dict(receiver[0], receiver_port=2), receiver[1]],
                [receiver[0], dict(receiver[1], payload_hash="fedcba9876543210")],
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                verify_transport(changed, game_sequence=0, scenario="duplicate", role=1)

    def test_jitter_and_reorder_require_authored_native_fields(self):
        jitter = [
            {"event": "pad_transport", "observer_context": "transport", "game_sequence": 0,
             "action": "jitter_hold", "packet_frame": 98, "payload_hash": "0123456789abcdef",
             "held_count": 1, "history_count": 1, "history_first_frame": 98,
             "history_last_frame": 98},
            {"event": "pad_transport", "observer_context": "transport", "game_sequence": 0,
             "action": "jitter_release", "release_packet_frame": 99, "dispatch_frame": 99,
             "release_payload_hash": "fedcba9876543210", "release_count": 1,
             "held_frames": [98], "held_payload_hashes": ["0123456789abcdef"],
             "dispatch_payload_hashes": [], "history_count": 1,
             "history_first_frame": 99, "history_last_frame": 99},
        ]
        report = verify_transport(jitter, game_sequence=0, scenario="jitter", role=2)
        self.assertTrue(report["native_history_and_identities_observed"])
        reorder = [
            dict(jitter[0], action="reorder_hold"),
            dict(jitter[1], action="reorder_dispatch",
                 dispatch_payload_hashes=["fedcba9876543210", "0123456789abcdef"]),
        ]
        report = verify_transport(reorder, game_sequence=0, scenario="reorder", role=2)
        self.assertEqual(report["fault_events"], 2)
        for changed in ([dict(jitter[0], action="jitter_hold", history_count=True), jitter[1]],
                        [jitter[0], dict(jitter[1], release_payload_hash="not-hex")],
                        [dict(reorder[0], action="reorder_hold", history_count=True), reorder[1]]):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                verify_transport(changed, game_sequence=0,
                                 scenario="reorder" if changed[0]["action"] == "reorder_hold" else "jitter",
                                 role=2)

    def test_recording_delimiter_and_final_rng_clock_are_checked(self):
        capture, _ = fixture()
        rows = [{"event": "recording_game_start", "observer_context": "exi"}]
        rows += [{"event": "recording_frame_start", "observer_context": "exi",
                  "frame": frame.number, "rng": frame.start_random_seed,
                  "scene_frame": frame.scene_frame_counter} for frame in capture.frames]
        report = verify_recording_clock(capture, rows, game_number=1)
        self.assertEqual(report["finalized_frames_compared"], 203)
        preserved = {"odb_address": 1, "odb_size": 3311, "rxb_address": 2, "rxb_size": 297}
        fresh = [rows[0], {"event": "savestate_capture", "observer_context": "exi",
                           "preservation": preserved},
                 dict(rows[1], odb={}, rxb={}, preservation=preserved), *rows[2:]]
        self.assertEqual(verify_recording_clock(capture, fresh, game_number=1)
                         ["finalized_frames_compared"], 203)
        # A wrong speculative RNG is acceptable only when the final revision
        # agrees; this check by itself makes no rollback correctness claim.
        repeated = dict(rows[-1], rng=rows[-1]["rng"] ^ 1)
        revised = rows[:-1] + [repeated, rows[-1]]
        self.assertEqual(verify_recording_clock(capture, revised, game_number=1)
                         ["repeated_frame_starts"], 1)
        for changed in (rows[1:], rows[:-1], rows + [repeated],
                        rows[:1] + [dict(rows[1], odb={})] + rows[2:],
                        fresh[:2] + [dict(fresh[2], preservation=dict(preserved, odb_address=99))] + fresh[3:],
                        rows[:-1] + [dict(rows[-1], scene_frame=999)]):
            with self.assertRaises(ValueError):
                verify_recording_clock(capture, changed, game_number=1)
        # The rematch delimiter, rather than the online counter reset, owns
        # startup observations that happen before online frame 1.
        rematch = rows + rows
        self.assertEqual(verify_recording_clock(capture, rematch, game_number=2)
                         ["game_info_boundary"], 2)

    def test_delayed_input_edge_and_startup_are_checked_at_recorded_frame(self):
        capture, rows = fixture()
        report = verify_profile(capture, rows, game_sequence=0, role=1)
        self.assertEqual(report["compared_startup_frames"], 92)
        frames = list(capture.frames)
        index = 92  # Source 90 + delay 2 -> SLP -31.
        frames[index] = replace(frames[index], inputs=(replace(frames[index].inputs[0],
                                                             raw_stick=(0, 0)),))
        with self.assertRaisesRegex(ValueError, '"recorded_frame": -31'):
            verify_profile(replace(capture, frames=tuple(frames)), rows, game_sequence=0, role=1)
        frames = list(capture.frames)
        frames[0] = replace(frames[0], inputs=(replace(frames[0].inputs[0], raw_stick=(-127, 0)),))
        with self.assertRaisesRegex(ValueError, '"recorded_frame": -123'):
            verify_profile(replace(capture, frames=tuple(frames)), rows, game_sequence=0, role=1)

    def test_missing_generated_prefix_and_duplicate_assignment_are_refused(self):
        capture, rows = fixture()
        for changed in (rows[:1] + rows[2:], rows + rows[:1]):
            with self.assertRaises(ValueError):
                verify_profile(capture, changed, game_sequence=0, role=1)

    def test_context_order_and_schema_are_required(self):
        rows = [{"diagnostic_schema": SCHEMA, "observer_context": "exi",
                 "game_sequence": game, "event_sequence": index}
                for index, game in enumerate((-1, 0, 0, 1))]
        self.assertEqual(verify_log_order(rows), {"exi": 4, "transport": 0})
        for key, value in (("event_sequence", 0), ("observer_context", "unknown"),
                           ("diagnostic_schema", "unknown"), ("game_sequence", 2)):
            changed = [dict(row) for row in rows]
            changed[2][key] = value
            with self.assertRaises(ValueError):
                verify_log_order(changed)

    def test_preflight_failure_retains_evidence_without_starting_processes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            disc = root / "owned-test-input"
            disc.write_bytes(b"not a game disc")
            run = RollbackRun(root=root, disc=disc, cycle=1, timeouts={},
                              client_binary=root / "missing-client", rollback_diagnostic="none")
            with self.assertRaises(FileNotFoundError):
                run.run()
            report = json.loads((run.work / "evidence.json").read_text())
            self.assertEqual(report["result"], "failed-preparation")
            self.assertIn("FileNotFoundError", report["failure"])
            self.assertEqual(run.children, {})
            self.assertNotIn("ports_released", report)


if __name__ == "__main__":
    unittest.main()
