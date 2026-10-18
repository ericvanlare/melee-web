from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"),
                str(ROOT / "scripts"), str(ROOT / "tests")]

import reference_observer_stream as wire
import test_stadium_idle_3500_reference as idle
from stadium_go_prefix import (EXPECTED_SETUP_RECEIPT_SHA256, StadiumGoPrefixError,
                               validate_stadium_go_ordinal2_reference,
                               validate_stadium_go_prefix)


def go2_fixture(*, end_ordinal: int = 2, tail: int = 0):
    raw, status = idle.fixture(end_ordinal=end_ordinal, tail=tail)
    records = []
    for h, b in idle.literal_frames(raw):
        row = wire._decode_record(h, b, "GO+2 fixture")
        payload = row["payload"]
        if row["event"] in ("handshake", "start", "end"):
            payload.pop("original_idle_post_go_ticks", None)
            payload["original_post_go_target_ordinal"] = 2
            if row["event"] in ("start", "end"):
                payload["completion_boundary"] = "relative_post_go_tick_2_then_first_draw_return"
            if row["event"] == "end":
                payload["comparison_end_tick"] = 127
                payload["tail_ticks_before_draw_return"] = 1 + max(0, end_ordinal - 2) + tail
            b = json.dumps(payload, separators=(",", ":")).encode()
        records.append(idle.stored_frame(h, b, replacement_payload=b))
    data = b"".join(
        idle.stored_frame(h, b, sequence=sequence)
        for sequence, record in enumerate(records)
        for h, b in idle.literal_frames(record)
    )
    status = dict(status)
    status["original_post_go_target_ordinal"] = 2
    return data, status


def input_stream_fixture():
    header = struct.pack("<4sHHII", b"MWRI", 1, 16, 40, 0)

    def record(sequence, tick, port, *, kind=1, connected=1):
        fields = bytes((0, 0, 0, 0, 0, 0, 0, 0, 0, connected))
        prefix = struct.pack("<QQIH10BI", sequence, tick, port, 0, *fields, kind)
        return prefix + struct.pack("<I", zlib.crc32(prefix) & 0xFFFFFFFF)

    return header + record(0, 1, 0) + record(1, 1, 0xFFFFFFFF, kind=2, connected=0)


def remove_consume_for_tick(data: bytes, status: dict, target_tick: int) -> tuple[bytes, dict]:
    kept = []
    removed = 0
    for h, b in idle.literal_frames(data):
        row = wire._decode_record(h, b, "missing queue fixture")
        if (row["event"] == "boundary" and row["payload"].get("boundary") == "pad_consume" and
                row.get("source_tick") == target_tick):
            removed += 1
            continue
        kept.append(idle.stored_frame(h, b, sequence=len(kept)))
    if removed != 1:
        raise AssertionError(f"expected exactly one target PAD consume, removed {removed}")
    if type(status.get("event_count")) is not int or type(status.get("last_seq")) is not int:
        raise AssertionError("missing-consume fixture status lacks integer event_count/last_seq")
    if status["event_count"] != len(kept) + 1 or status["last_seq"] != len(kept):
        raise AssertionError("missing-consume fixture input status does not match its complete source")
    altered_status = dict(status)
    altered_status["event_count"] -= 1
    altered_status["last_seq"] -= 1
    return b"".join(kept), altered_status


def remove_go2_seed_slices(data: bytes) -> bytes:
    output = []
    go_tick = None
    removed = 0
    for h, b in idle.literal_frames(data):
        row = wire._decode_record(h, b, "GO+2 seed compatibility fixture")
        payload = row["payload"]
        if row["event"] == "progress" and payload.get("phase") == "go_after":
            go_tick = row["source_tick"]
        elif (go_tick is not None and row["event"] == "boundary" and
              payload.get("boundary") == "source_tick" and
              row["source_tick"] == go_tick + 2):
            slices = payload.get("slices", [])
            retained = [item for item in slices if item.get("tag") not in (19, 20)]
            removed = len(slices) - len(retained)
            rebuilt = idle.frame(6, row["source_tick"], retained)
            rebuilt_header, b = idle.literal_frames(rebuilt)[0]
            h = list(h)
            h[-2:] = rebuilt_header[-2:]
            h = tuple(h)
        output.append((h, b))
    if removed != 2:
        raise AssertionError(f"expected two GO+2 RNG slices to remove, removed {removed}")
    return b"".join(idle.stored_frame(h, b, sequence=index, replacement_payload=b)
                    for index, (h, b) in enumerate(output))


class StadiumGoOrdinal2ReferenceTests(unittest.TestCase):
    def validate(self, data, status, input_data=None, input_status=None, *,
                 include_go2_state=False):
        with tempfile.TemporaryDirectory(prefix="stadium-go-ordinal2-") as directory:
            root = Path(directory)
            observer = root / "observer.bin"
            observer.write_bytes(data)
            observer_status = root / "observer-status.json"
            observer_status.write_text(json.dumps(status))
            native = root / "inputs.mwri"
            native.write_bytes(input_stream_fixture() if input_data is None else input_data)
            native_status = root / "input-status.json"
            native_status.write_text(json.dumps(input_status or {
                "version": 1, "mode": "record", "events": 1,
                "complete": True, "invalid": False, "error": None,
            }))
            return validate_stadium_go_ordinal2_reference(
                observer, input_path=native, input_status_path=native_status,
                expected_setup_receipt_sha256=EXPECTED_SETUP_RECEIPT_SHA256,
                status_path=observer_status,
                include_go2_state=include_go2_state), observer

    def test_fresh_record_go2_retains_actual_queue_and_source_state(self):
        data, status = go2_fixture()
        result, observer = self.validate(data, status)
        self.assertEqual(result["decision"], "PASS_FRESH_RECORD_GO_ORDINAL2_REFERENCE_ONLY")
        self.assertEqual([row["ordinal"] for row in
                          result["consumed_queue_state"]["actual_consumed_queue_rows"]], [0, 1, 2])
        self.assertEqual(result["consumed_queue_state"]["source_state_snapshots_validated_at_ordinals"],
                         [0, 1, 2])
        self.assertEqual(result["input_status"]["mode"], "record")
        self.assertFalse(result["claims"]["historical_recording_equivalence"])
        with tempfile.TemporaryDirectory(prefix="stadium-go-ordinal2-legacy-reject-") as directory:
            path = Path(directory) / "observer.bin"
            path.write_bytes(data)
            status_path = Path(str(path) + ".status.json")
            status_path.write_text(json.dumps(status))
            with self.assertRaisesRegex(
                    StadiumGoPrefixError,
                    r"GO\+2 declaration differs from explicit validation scope"):
                validate_stadium_go_prefix(path, constructor_observations=True,
                                           status_path=status_path)

    def test_default_reference_does_not_decode_go2_seed_but_explicit_branch_requires_it(self):
        data, status = go2_fixture()
        without_go2_seed = remove_go2_seed_slices(data)
        legacy, _ = self.validate(without_go2_seed, status)
        self.assertEqual(legacy["version"], 1)
        self.assertNotIn("go2_declared_state", legacy["consumed_queue_state"])
        with self.assertRaisesRegex(StadiumGoPrefixError,
                                    r"GO\+2 RNG pointer: expected one tag=19 flags=0x0"):
            self.validate(without_go2_seed, status, include_go2_state=True)

    def test_explicit_go2_comparison_branch_exports_typed_fields_seed_and_literal_queue(self):
        data, status = go2_fixture()
        result, _ = self.validate(data, status, include_go2_state=True)
        self.assertEqual(result["version"], 2)
        self.assertEqual(result["decision"],
                         "PASS_FRESH_RECORD_GO_ORDINAL2_TYPED_STATE_REFERENCE_ONLY")
        go2 = result["consumed_queue_state"]["go2_declared_state"]
        self.assertEqual(go2["schema"], "melee-web-original-stadium-go2-declared-state")
        self.assertEqual(go2["typed_fields"]["label"], "GO2")
        expected_tick = result["consumed_queue_state"]["go_source_tick_C"] + 2
        self.assertEqual(go2["typed_fields"]["source_tick"], expected_tick)
        self.assertEqual(go2["seed_observation"]["source_tick"], expected_tick)
        self.assertEqual(go2["source_queue"]["source_tick"], expected_tick)
        self.assertEqual(go2["source_queue"]["queue_slot_status_hex"], [
            "0000000000000000000000", "0000000000000000000000",
            "00000000000000000000ff", "00000000000000000000ff"])
        self.assertEqual(go2["provenance"]["source_stream_sha256"],
                         result["route"]["stream_sha256"])
        self.assertEqual(go2["provenance"]["input_stream_sha256"],
                         result["input_stream"]["sha256"])

    def test_target_and_actual_consume_are_mandatory(self):
        missing_target, status = go2_fixture(end_ordinal=1)
        with self.assertRaises(StadiumGoPrefixError):
            self.validate(missing_target, status)
        complete, status = go2_fixture()
        missing_consume, missing_consume_status = remove_consume_for_tick(complete, status, 127)
        with self.assertRaisesRegex(StadiumGoPrefixError, "lacks its actual preceding PAD consume"):
            self.validate(missing_consume, missing_consume_status)

    def test_go2_requires_own_completed_record_status(self):
        data, status = go2_fixture()
        replay_status = {"version": 1, "mode": "replay", "events": 1,
                         "complete": True, "invalid": False, "error": None}
        with self.assertRaisesRegex(StadiumGoPrefixError, "mode disagrees"):
            self.validate(data, status, input_status=replay_status)
        incomplete_status = {"version": 1, "mode": "record", "events": 1,
                             "complete": False, "invalid": False, "error": None}
        with self.assertRaisesRegex(StadiumGoPrefixError, "did not complete"):
            self.validate(data, status, input_status=incomplete_status)

    def test_cli_and_runner_refuse_replay_or_non_stadium_hypotheses(self):
        from unittest.mock import patch
        from test_stadium_recorded_replay import driver

        args = []
        for name in ("dolphin", "disc", "profile", "menu-recipe", "output", "build-manifest"):
            args += ["--" + name, "unused-" + name]
        with patch.object(driver, "run", return_value={}) as run, patch("builtins.print"):
            driver.main(args + ["--stadium-go-ordinal2"])
        self.assertIs(run.call_args.kwargs["stadium_go_ordinal2"], True)

        menus = {"scope": "stadium_go_prefix", "version": 11,
                 "setup_receipt_sha256": EXPECTED_SETUP_RECEIPT_SHA256}
        with patch.object(driver, "validate_reference_build_manifest", return_value={}), \
             patch.object(driver, "menu_actions", return_value=(menus, "recipe")), \
             patch.object(driver.subprocess, "Popen", side_effect=AssertionError("must not launch")):
            with self.assertRaisesRegex(driver.SdDiagnosticError, "fresh-record Stadium route"):
                driver._run(dolphin="unused", disc="unused", profile="unused", menu_recipe="unused",
                            output="unused", build_manifest="unused", timeout=180,
                            replay_receipt={}, stadium_go_ordinal2=True)
            with self.assertRaisesRegex(driver.SdDiagnosticError, "fresh-record Stadium route"):
                driver._run(dolphin="unused", disc="unused", profile="unused",
                            menu_recipe="unused", output="unused", build_manifest="unused",
                            timeout=180, stadium_go_ordinal2=True, stadium_idle_3500=True)


if __name__ == "__main__":
    unittest.main()
