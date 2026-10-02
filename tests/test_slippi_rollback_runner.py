# SPDX-License-Identifier: MIT
"""Sensitivity and retained-failure checks for the desktop rollback runner."""
from dataclasses import replace
import copy
import json
import struct
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference-capture/slippi"))
from run_rollback import (RollbackRun, read_diagnostic_log, verify_log_order, verify_profile,
                          ARTIFACT_IDENTITY_FIELDS, artifact_identities,
                          validate_baseline_game,
                          verify_prediction_rollbacks, verify_recording_clock, verify_transport,
                          verify_speculative_corrections)
from test_compare_rollback import timeline
from test_slippi_format import timeline_fixture, _pre_frame, _post_frame
from tools.slippi_format import SlippiFrame
from slippi_format import RAW_PREFIX, _raw_stream, decode_timeline
from tools.slippi_rollback_diagnostic import SCHEMA


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


class RollbackRunnerEvidenceTests(unittest.TestCase):
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
        data = timeline_fixture()
        capture = decode_timeline(data)
        loads = [{"state_scene_frame": 1, "end_scene_frame": 2, "load_event_sequence": 20}]
        report = verify_speculative_corrections(data, capture, loads, remote_port=1)
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
            with self.assertRaisesRegex(ValueError, "wrong-input/state revision"):
                verify_speculative_corrections(changed, decode_timeline(changed), loads, remote_port=1)
        with self.assertRaisesRegex(ValueError, "wrong-input/state revision"):
            verify_speculative_corrections(data, capture,
                                          [dict(loads[0], state_scene_frame=0, end_scene_frame=1)],
                                          remote_port=1)
        with self.assertRaisesRegex(ValueError, "validated finalized timeline"):
            verify_speculative_corrections(data, replace(capture, duplicate_updates=99), loads, remote_port=1)

    def test_changed_baseline_client_is_refused_before_profile_or_process_creation(self):
        run = RollbackRun.__new__(RollbackRun)
        run.evidence = {key: "unchanged" for key in ARTIFACT_IDENTITY_FIELDS}
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
        rows += [{"event": "recording_frame_start", "scene_frame": scene} for scene in (98, 99, 100)]
        rows += [{"event": "savestate_load", "frame": 98, "odb": odb},
                 {"event": "savestate_load_complete", "frame": 98}]
        rows += [{"event": "recording_frame_start", "scene_frame": scene} for scene in (98, 99, 100)]
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
