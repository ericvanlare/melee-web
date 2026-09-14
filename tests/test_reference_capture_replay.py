import hashlib
import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
import zlib
from copy import deepcopy
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools")]

import reference_capture_replay as REPLAY  # noqa: E402
from reference_capture_replay import ReplayError, derive_replay  # noqa: E402
from reference_observer_stream import BOUNDARY, HEADER, MAGIC_U32, SCHEMA_VERSION  # noqa: E402
from reference_session_bundle import RAW_OBSERVER_NAME, ReferenceSessionBundle  # noqa: E402
from cpu_observation_validation import load_observation  # noqa: E402
from retail_replay_validation import EXPECTED_PROVENANCE  # noqa: E402


ENVIRONMENT = {
    "disc": {"dol_sha1": EXPECTED_PROVENANCE["dol_sha1"],
              "dol_sha256": "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"},
    "dolphin": {"source_revision": EXPECTED_PROVENANCE["dolphin_commit"], "binary_sha256": "aa" * 32,
                 "observer_sha256": "bb" * 32},
}


def observer_frame(event_code: int, seq: int, payload: bytes, *, source_tick: int = 0,
                   draw_ordinal: int = 0) -> bytes:
    header = HEADER.pack(MAGIC_U32, SCHEMA_VERSION, event_code, seq, 0, 0,
                         source_tick, draw_ordinal, len(payload), zlib.crc32(payload) & 0xffffffff)
    return header + payload


def valid_observer_stream(record_count: int) -> bytes:
    handshake = json.dumps({
        "schema": "melee-web-passive-dolphin-observer", "version": 1,
        "dolphin_commit": ENVIRONMENT["dolphin"]["source_revision"],
        "dol_sha1": ENVIRONMENT["disc"]["dol_sha1"],
        "dol_sha256": ENVIRONMENT["disc"]["dol_sha256"], "cpu": "JITARM64",
        "writes_guest_memory": False, "ring_capacity": 1024,
    }, separators=(",", ":")).encode()
    start = json.dumps({"status": "recording", "source_revision": "GALE01r2",
                        "boundaries": "typed-existing-retail-harness"},
                       separators=(",", ":")).encode()
    end = json.dumps({"status": "completed", "natural": True},
                     separators=(",", ":")).encode()
    boundary = BOUNDARY.pack(1, 0, 0, 32, 0, 0) + bytes(32 * 4)
    rows = [observer_frame(1, 0, handshake), observer_frame(2, 1, start)]
    rows.extend(observer_frame(3, seq, boundary, source_tick=seq, draw_ordinal=seq)
                for seq in range(2, record_count - 1))
    rows.append(observer_frame(6, record_count - 1, end,
                               source_tick=record_count - 1,
                               draw_ordinal=record_count - 1))
    return b"".join(rows)


def pad_state() -> str:
    config = bytes.fromhex("0000002d00000008001e0000000000007f0000ff0000ff007fffff000000")
    return (config + bytes(12 * 66)).hex()


def fighter(slot: int) -> dict:
    return {
        "slot": slot, "kind": 0, "motion": 14, "animation": 1,
        "facing_bits": "3f800000", "position_bits": ["00000000", "3f800000", "00000000"],
        "velocity_bits": ["00000000", "00000000", "00000000"],
        "knockback_bits": ["00000000", "00000000", "00000000"],
        "ground_air": 0, "animation_frame_bits": "00000000", "animation_speed_bits": "3f800000",
        "damage_bits": "00000000", "shield_bits": "00000000", "stocks": 4,
        "input_hex": "00" * 0x6C,
    }


def state(index: int) -> dict:
    return {
        "rng": 0x100 + index, "scene_frame": index, "match_frame": index,
        "pad_state_hex": pad_state(), "fighters": [fighter(0), fighter(1)],
    }


def setup_hex() -> str:
    setup = bytearray(0x138)
    setup[0x61] = 0
    setup[0x61 + 0x24] = 1
    for slot in range(2, 6):
        setup[0x61 + slot * 0x24] = 3
    return setup.hex()


def cpu_snapshot(index: int) -> dict:
    cpu = {
        "kind": 4, "level": 1, "state": 1, "default_state": 1,
        "secondary_state": 10, "target_slot": -1, "buttons": 0,
        "sticks": [0, 0, 0, 0], "triggers": [0, 0],
        "command_duration": index, "command_cursor": -1,
        "command_bytes": "", "defend_queue": [], "attack_queue": [],
    }
    subject = {
        "state": 0, "timer": 0, "on_ledge": 0, "force_inactive": 0,
        "was_framed": 0, "position_bits": ["00000000"] * 3,
        "bone_bits": ["00000000"] * 3,
    }
    hud = {
        "present": True, "damage": 0, "old_damage": 0,
        "last_attack_damage": 0, "shake_frames": 0, "explode": 0,
        "randomize_velocity": 0, "force_shake": 0, "hide_digits": 0,
        "animation_status": 0,
    }
    magnifier = {"offscreen": 0, "ignore_offscreen": 0, "edge": 0}
    player = lambda slot, value: {
        "slot": slot, "cpu": value, "subject": deepcopy(subject),
        "magnifier": deepcopy(magnifier), "hud": deepcopy(hud),
    }
    return {
        "match": {"frame": index, "seconds": 60, "subframe": 0,
                  "outcome": 0, "end_state": 0},
        "camera": {"position_bits": ["00000000"] * 3,
                   "interest_bits": ["00000000"] * 3, "projection": 1,
                   "fov_bits": "3f800000", "near_bits": "3f800000",
                   "far_bits": "3f800000"},
        "players": [player(0, None), player(1, deepcopy(cpu))],
    }


def semantic_records(frame_count: int = 2, *, nonneutral_cpu: bool = False) -> list[dict]:
    cpu_pad = "20" * 11 if nonneutral_cpu else "00" * 11
    entry = {"record": "match_enter", "rng": 0x100,
             "start_melee_hex": setup_hex(), "pad_state_hex": pad_state()}
    initial = {"record": "match_enter_complete", **state(0)}
    rows = [
        {"seq": 0, "source_tick": 0, "draw_ordinal": 0, "event": "handshake",
         "payload": {"schema": "melee-web-passive-dolphin-observer", "version": 1,
                     "dolphin_commit": ENVIRONMENT["dolphin"]["source_revision"],
                     "dol_sha1": ENVIRONMENT["disc"]["dol_sha1"],
                     "dol_sha256": ENVIRONMENT["disc"]["dol_sha256"], "cpu": "JITARM64",
                     "writes_guest_memory": False, "ring_capacity": 1024}},
        {"seq": 1, "source_tick": 0, "draw_ordinal": 0, "event": "start",
         "payload": {"status": "recording", "source_revision": "GALE01r2",
                     "boundaries": "typed-existing-retail-harness"}},
        {"seq": 2, "source_tick": 0, "draw_ordinal": 0, "event": "match_enter",
         "payload": {"retail": entry}},
        {"seq": 3, "source_tick": 0, "draw_ordinal": 0, "event": "match_initial", "phase": "entry",
         "payload": {"retail": initial, "cpu": cpu_snapshot(0)}},
    ]
    for index in range(frame_count):
        rows.append({
            "seq": len(rows), "source_tick": index, "draw_ordinal": index,
            "event": "pad_consume", "phase": "input",
            "payload": {"ports": ["10" * 11, cpu_pad, "00" * 10 + "ff", "00" * 10 + "ff"]},
        })
        rows.append({
            "seq": len(rows), "source_tick": index, "draw_ordinal": index,
            "event": "source_tick", "phase": "gameplay",
            "payload": {"retail": {"record": "frame", "index": index,
                                     "consumed_inputs": [["10" * 11, cpu_pad,
                                                          "00" * 10 + "ff", "00" * 10 + "ff"]],
                                     **state(index)},
                        "cpu": cpu_snapshot(index)},
        })
        rows.append({
            "seq": len(rows), "source_tick": index, "draw_ordinal": index,
            "event": "draw_return", "phase": "draw",
            "payload": {
                "draw": {"record": "draw", "index": index, "source_index": index,
                          "before": state(index), "after": state(index)},
                "cpu": cpu_snapshot(index),
            },
        })
    next_seq = len(rows)
    rows.extend([
        {"seq": next_seq, "source_tick": frame_count, "draw_ordinal": frame_count,
         "event": "result_return", "phase": "ending_result",
         "payload": {"result": {"outcome": 1, "winners": [0]}}},
        {"seq": next_seq + 1, "source_tick": frame_count, "draw_ordinal": frame_count,
         "event": "scene_reset",
         "payload": {"remaining_fighter_slots": [], "released_fighter_slots": [0, 1],
                     "result": {"outcome": 1, "winners": [0]},
                     "source_ticks": frame_count}},
    ])
    return rows


class ReferenceCaptureReplayTests(unittest.TestCase):
    def _bundle(self, root: Path, *, frame_count: int = 2,
                nonneutral_cpu: bool = False,
                observer_record_count: int | None = None) -> tuple[Path, list[dict]]:
        provenance = dict(EXPECTED_PROVENANCE)
        provenance.update({"cpu": "JITARM64", "dolphin_binary_sha256": "aa" * 32})
        records = semantic_records(frame_count, nonneutral_cpu=nonneutral_cpu)
        bundle = ReferenceSessionBundle.begin(
            root, "session-replay", "GALE01", "run-replay",
            sequence_start=0,
            metadata={"provenance": provenance, "observer_identity": "bb" * 32,
                      "environment": ENVIRONMENT},
            raw_observer=valid_observer_stream(observer_record_count or len(records) + 1),
        )
        for record in records:
            bundle.append(record)
        # The lifecycle validator requires an explicit observer-end phase.
        final = {"seq": len(records), "event": "observer_end", "phase": "observer_end",
                 "payload": {"status": "completed", "natural": True}}
        path = bundle.complete(observer_end=final, semantic_validator=lambda *_: None)
        return path, records + [final]

    @staticmethod
    def _derive(bundle, output, stored):
        class FakeSemanticSession:
            def __init__(self):
                self.index = 0

            def consume(self, _record):
                value = deepcopy(stored[self.index])
                self.index += 1
                return value

            def completion(self):
                return {"complete": True, "source_ticks": 2, "source_draws": 2,
                        "pad_polls": 2, "result": {"outcome": 1, "winners": [0]}}

        with mock.patch.object(REPLAY, "SemanticSession", FakeSemanticSession):
            return derive_replay(bundle, output)

    def test_single_capture_derives_mwrc_and_keeps_cpu_out_of_input(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "inbox"
            bundle, stored = self._bundle(root)
            output = Path(directory) / "derived"
            report = self._derive(bundle, output, stored)
            destination = Path(report["path"])
            self.assertTrue((destination / "capture.mwrc").is_file())
            self.assertTrue((destination / "candidate.jsonl").is_file())
            self.assertTrue((destination / "cpu-sidecar.jsonl").is_file())
            self.assertTrue((destination / "draw-audit.jsonl").is_file())
            self.assertGreater(len((destination / "cpu-sidecar.jsonl").read_text().splitlines()), 0)
            self.assertGreater(len((destination / "draw-audit.jsonl").read_text().splitlines()), 0)
            sidecar = load_observation(destination / "cpu-sidecar.jsonl")
            self.assertEqual(sidecar.header["schema"], "melee-web-cpu-observation")
            self.assertEqual(sidecar.header["frames_requested"], 2)
            self.assertEqual(len(sidecar.frames), 2)
            self.assertEqual(len(sidecar.draws), 2)
            self.assertEqual(sidecar.end["result"], {"outcome": 1, "winners": [0]})
            self.assertEqual(sidecar.end["remaining_fighter_slots"], [])
            sidecar_rows = [json.loads(line) for line in
                            (destination / "cpu-sidecar.jsonl").read_text().splitlines()]
            self.assertEqual(sidecar_rows[0]["record"], "header")
            self.assertNotIn("seq", sidecar_rows[0])
            self.assertNotIn("cpu", sidecar_rows[0])
            self.assertEqual(sidecar_rows[-1]["record"], "end")
            mwrc = (destination / "capture.mwrc").read_bytes()
            # Version 3 enables the runtime's CPU observation companion.
            self.assertEqual(int.from_bytes(mwrc[4:8], "big"), 3)
            candidate_header = json.loads((destination / "candidate.jsonl").read_text().splitlines()[0])
            self.assertEqual(candidate_header["active_player_count"], 2)
            first_frame = 16 + 0x138 + 822
            self.assertEqual(mwrc[first_frame + 11:first_frame + 22], bytes(11))
            manifest = json.loads((destination / "derived-manifest.json").read_text())
            self.assertEqual(manifest["claims"]["reference_repeatability"], "not_established")
            self.assertEqual(manifest["transport"]["cpu_generated_decisions"], "excluded from replay input")
            self.assertEqual(manifest["source"]["manifest_sha256"],
                             json.loads((bundle / "manifest.json").read_text())["manifest_sha256"])
            # The raw accepted bundle and observer bytes were not rewritten.
            self.assertEqual((bundle / RAW_OBSERVER_NAME).read_bytes(),
                             valid_observer_stream(len(stored)))

    def test_missing_observer_stream_is_rejected_before_derivation(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, _ = self._bundle(Path(directory) / "inbox")
            (bundle / RAW_OBSERVER_NAME).unlink()
            with self.assertRaisesRegex(ReplayError, "observer"):
                derive_replay(bundle, Path(directory) / "derived")

    def test_malformed_observer_stream_is_rejected_by_strict_decoder(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "observer.bin"
            path.write_bytes(b"not an MWRO stream")
            with self.assertRaisesRegex(ReplayError, "malformed"):
                REPLAY._read_observer_records(path, {"environment": ENVIRONMENT})

    def test_cpu_output_bytes_are_rejected_from_replay_input(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, stored = self._bundle(Path(directory) / "inbox", nonneutral_cpu=True)
            with self.assertRaisesRegex(ReplayError, "non-neutral"):
                self._derive(bundle, Path(directory) / "derived", stored)

    def test_stored_semantics_must_match_authoritative_decode(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, stored = self._bundle(Path(directory) / "inbox")
            header = json.loads((bundle / "header.json").read_text())
            altered = deepcopy(stored)
            altered[2]["event"] = "tampered"

            class FakeSemanticSession:
                def __init__(self):
                    self.index = 0

                def consume(self, _record):
                    value = deepcopy(stored[self.index])
                    self.index += 1
                    return value

                def completion(self):
                    return {"complete": True}

            with mock.patch.object(REPLAY, "SemanticSession", FakeSemanticSession), \
                 self.assertRaisesRegex(ReplayError, "differs from observer"):
                REPLAY._validate_authoritative_semantics(
                    header, bundle / RAW_OBSERVER_NAME, altered)

    def test_ingest_does_not_move_bundle_before_authoritative_completion(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, _ = self._bundle(Path(directory) / "inbox", observer_record_count=3)
            with self.assertRaisesRegex(ReplayError, "incomplete"):
                REPLAY.ingest_and_derive(bundle, Path(directory) / "derived")
            self.assertTrue(bundle.exists())
            self.assertEqual(list((bundle.parent.parent / "ingested").iterdir()), [])

    def test_derived_destination_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            bundle, stored = self._bundle(Path(directory) / "inbox")
            output = Path(directory) / "derived"
            self._derive(bundle, output, stored)
            with self.assertRaisesRegex(ReplayError, "already exists"):
                self._derive(bundle, output, stored)


if __name__ == "__main__":
    unittest.main()
