import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"),
                str(ROOT / "tests")]

import reference_capture_semantics as semantics  # noqa: E402
import reference_observer_stream as observer_stream  # noqa: E402
import whole_session_replay as replay  # noqa: E402
from test_reference_capture_semantics import (  # noqa: E402
    _decoded_observer_rows, _whole_pad_state, _whole_setup)


def _raw_pad_snapshot() -> bytes:
    semantic = bytes.fromhex(_whole_pad_state())
    raw = bytearray(0x358)
    raw[:10] = semantic[:10]
    raw[12:32] = semantic[10:30]
    cursor = 30
    for offset in (0x28, 0x138, 0x248):
        for bank_offset in range(offset, offset + 0x110, 68):
            raw[bank_offset:bank_offset + 66] = semantic[cursor:cursor + 66]
            cursor += 66
    return bytes(raw)


def _whole_session_cpu_setup(match_index: int) -> bytes:
    """Four ordinary CPU9 players with the selected per-match roster."""
    lineups = replay.V9_MILESTONE_ROSTER
    raw = bytearray.fromhex(_whole_setup()["start_melee_hex"])
    raw[0] = (raw[0] & 0x1F) | 0x20  # accepted four-Mario MatchKind
    raw[2] |= 0x80  # stock rules
    raw[0x0B] = 0xFF  # accepted item-frequency off setting
    raw[0x0E:0x10] = (0x20).to_bytes(2, "big")  # Final Destination
    raw[0x20:0x28] = bytes.fromhex("ffffffffffffffff")
    raw[0x30:0x34] = bytes.fromhex("3f800000")
    raw[0x34:0x38] = bytes.fromhex("3f800000")
    for slot in range(6):
        base = 0x60 + slot * 0x24
        if slot < 4:
            raw[base:base + 5] = bytes((lineups[match_index][slot], 1, 4, slot, 0))
            raw[base + 14] = 4
            raw[base + 15] = 9
            for offset in (0x18, 0x1C, 0x20):
                raw[base + offset:base + offset + 4] = bytes.fromhex("3f800000")
        else:
            raw[base + 1] = 3
    return bytes(raw)


def _pad_consume(match_index: int, value: int, source_tick: int) -> dict:
    address = 0x80500000
    slot = b"".join(bytes([value + port]) * 11 + b"\0" for port in range(4))
    registers = [0] * 32
    registers[25] = address
    slices = [
        {"name": "pad_queue", "address": 0x804C1F78, "size": 0xC,
         "hex": (b"" + b"\0" * 7 + address.to_bytes(4, "big")).hex()},
        {"name": "pad_slot", "address": address, "size": 0x30, "hex": slot.hex()},
    ]
    return {
        "seq": 0,
        "source_tick": source_tick,
        "draw_ordinal": 0,
        "event": "boundary",
        "payload": {
            "pc": 0x80377584,
            "gprs": registers,
            "boundary": "pad_consume",
            "boundary_kind": 2,
            "whole_boundary_kind": 2,
            "whole_session": True,
            "match_index": match_index,
            "slices": slices,
        },
    }


def _candidate(capture_id: str = "capture-a", sequence_id: str = "sequence-a",
               *, v9_milestone: bool = False):
    rows = copy.deepcopy(_decoded_observer_rows())
    # Keep the fixture's published masks consistent with its typed SaveData,
    # as the v8 reader checks both source ranges at the transport boundary.
    for row in rows:
        payload = row.get("payload", {})
        if payload.get("boundary") != "css_enter":
            continue
        for item in payload.get("slices", []):
            if item.get("name") == "profile_save_data":
                raw = bytearray.fromhex(item["hex"])
                raw[0:4] = bytes.fromhex("07ff01c0")
                item["hex"] = raw.hex()
    rows.append({"seq": 0, "source_tick": 0, "draw_ordinal": 0, "event": "end",
                 "payload": {"status": "completed", "natural": True}})
    handshake = rows[0]["payload"]
    handshake.update({
        "dolphin_commit": replay.EXPECTED_DOLPHIN_COMMIT,
        "dol_sha1": replay.EXPECTED_DOL_SHA1,
        "dol_sha256": replay.EXPECTED_DOL_SHA256,
        "cpu": "JITARM64",
        "writes_guest_memory": False,
        "capture_id": capture_id,
        "sequence_id": sequence_id,
    })
    start = rows[1]["payload"]
    start.update({"source_revision": "GALE01r2", "boundaries": "typed-existing-retail-harness"})
    for announcement in (handshake, start):
        announcement.update({"capture_id": capture_id, "sequence_id": sequence_id,
                             "match_count": 3, "whole_session": True})

    raw_pad = _raw_pad_snapshot().hex()
    setup = _whole_setup()["start_melee_hex"]
    for row in rows:
        payload = row.get("payload", {})
        boundary = payload.get("boundary")
        if boundary == "css_enter" and payload.get("match_index") == 0:
            payload["slices"].extend([
                {"name": "pad_snapshot", "address": 0x804C1F84, "size": 0x358,
                 "hex": raw_pad},
                {"name": "rng_pointer", "address": 0x804D5F94, "size": 4,
                 "hex": "80510000"},
                {"name": "rng_value", "address": 0x80510000, "size": 4,
                 "hex": "12345678"},
            ])
        if boundary == "entry":
            setup_hex = (_whole_session_cpu_setup(payload.get("match_index", 0)).hex()
                         if v9_milestone else setup)
            payload["gprs"][3] = 0x80520000
            payload["slices"].append(
                {"name": "match_setup", "address": 0x80520000, "size": 0x138,
                 "hex": setup_hex})

    augmented = []
    value = 0x10
    for row in rows:
        augmented.append(row)
        payload = row.get("payload", {})
        boundary = payload.get("boundary")
        match = payload.get("match_index", 0)
        add = (
            boundary == "css_enter" and match == 0
            or boundary in {"sss_enter", "entry", "results_enter"}
            or boundary == "return_css" and match < 2
        )
        if add:
            augmented.append(_pad_consume(
                match + 1 if boundary == "return_css" else match,
                value, row["source_tick"]))
            value += 1
    final_tick = max(row["source_tick"] for row in augmented[:-1]) + 1
    augmented[-1]["source_tick"] = final_tick
    for seq, row in enumerate(augmented):
        row["seq"] = seq
        row["timestamp_ns"] = seq + 10
    return augmented


def _write_raw(path: Path, rows: list[dict]) -> None:
    tags = {name: tag for tag, name in observer_stream.SLICE_NAMES.items()}
    codes = {name: code for code, name in observer_stream.EVENT_NAMES.items()}
    output = bytearray()
    for row in rows:
        event = row["event"]
        payload = row["payload"]
        if event != "boundary":
            raw = json.dumps(payload, separators=(",", ":")).encode()
            pc = 0
        else:
            slices = payload.get("slices", [])
            prefix = observer_stream.BOUNDARY.pack(
                payload.get("boundary_kind", 0), 1, 0, 32, len(slices), 0)
            gprs = struct.pack("<32I", *payload["gprs"])
            descriptor_size = observer_stream.SLICE.size * len(slices)
            offset = observer_stream.BOUNDARY.size + 32 * 4 + descriptor_size
            descriptors = bytearray()
            data = bytearray()
            for item in slices:
                raw_slice = bytes.fromhex(item["hex"])
                descriptors += observer_stream.SLICE.pack(
                    tags[item["name"]], item.get("flags", 0), item["address"],
                    len(raw_slice), offset)
                data += raw_slice
                offset += len(raw_slice)
            metadata = observer_stream.WHOLE_METADATA.pack(
                payload["match_index"], payload["boundary_kind"], 0)
            raw = prefix + gprs + descriptors + data + metadata
            pc = payload.get("pc", 0)
        header = observer_stream.HEADER.pack(
            observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION, codes[event],
            row["seq"], row["timestamp_ns"], pc, row["source_tick"],
            row["draw_ordinal"], len(raw), zlib.crc32(raw) & 0xFFFFFFFF)
        output += header + raw
    path.write_bytes(output)


class _RepeatedFrames:
    """Bounded lazy sequence for exercising the v8 transport ceiling."""

    def __init__(self, frame: dict, count: int):
        self.frame = frame
        self.count = count

    def __len__(self):
        return self.count

    def __iter__(self):
        for _ in range(self.count):
            yield self.frame


class WholeSessionReplayTests(unittest.TestCase):
    def test_normalizes_source_consumed_inputs_and_encodes_v8_context(self):
        capture = replay.capture_from_records(_candidate())
        self.assertEqual(len(capture["frames"]), 12)
        self.assertEqual(capture["frames"][0]["scene"], "css")
        self.assertEqual(capture["frames"][1]["scene"], "sss")
        self.assertEqual(capture["frames"][2]["scene"], "match")
        self.assertEqual(capture["frames"][3]["scene"], "results")
        self.assertEqual(capture["first_css"]["rng"], 0x12345678)
        self.assertEqual(capture["first_css"]["pad_state_hex"], _whole_pad_state())
        payload, transport = replay.encode_v8(capture)
        magic, version, seed, frame_count, characters, stages = replay.HEADER.unpack_from(payload)
        self.assertEqual((magic, version, seed, frame_count), (b"MWRC", 8, 0x12345678, 12))
        self.assertEqual((characters, stages), (0x07FF, 0x01C0))
        context_offset = replay.HEADER.size
        context_version, context_flags, context_size = replay.CONTEXT_HEADER.unpack_from(
            payload, context_offset)
        self.assertEqual((context_version, context_flags, context_size),
                         (2, 0, replay.CONTEXT_BYTES))
        self.assertEqual(payload[context_offset + replay.CONTEXT_HEADER.size:
                                context_offset + replay.CONTEXT_HEADER.size + 0x18].hex(),
                         capture["first_css"]["game_rules_hex"])
        css_offset = (context_offset + replay.CONTEXT_HEADER.size +
                      replay.semantics.GAME_RULES_SIZE + replay.semantics.SAVE_DATA_SIZE)
        self.assertEqual(payload[css_offset:css_offset + replay.CSS_DATA_SIZE].hex(),
                         capture["first_css"]["css_data_hex"])
        span_offset = (replay.HEADER.size + replay.CONTEXT_HEADER.size +
                       replay.CONTEXT_BYTES + replay.GAME_INFO_SIZE +
                       replay.PAD_STATE_BYTES + 12 * 44)
        span_count = struct.unpack_from(">H", payload, span_offset)[0]
        self.assertEqual(span_count, len(capture["spans"]))
        self.assertEqual(len(payload), span_offset + 2 + span_count * replay.SPAN.size)
        self.assertEqual(transport["frame_count"], 12)

    def test_provisional_v7_is_rejected_at_producer_boundary(self):
        capture = replay.capture_from_records(_candidate())
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    "v7 cannot carry first-CSS source context"):
            replay.encode_v7(capture)

    def test_partial_first_css_context_is_rejected(self):
        rows = _candidate()
        first = next(row for row in rows
                     if row["payload"].get("boundary") == "css_enter")
        first["payload"]["slices"] = [item for item in first["payload"]["slices"]
                                        if item["name"] != "menu_css_context"]
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    "typed CSSData context"):
            replay.capture_from_records(rows)

    def test_four_player_setup_is_admitted_with_typed_css_context(self):
        rows = _candidate()
        for row in rows:
            if row.get("payload", {}).get("boundary") != "entry":
                continue
            item = next(item for item in row["payload"]["slices"]
                        if item["name"] == "match_setup")
            raw = bytearray.fromhex(item["hex"])
            for port in range(4):
                base = 0x60 + port * 0x24
                raw[base:base + 5] = bytes([8, 1, 4, port, 0])
                raw[base + 14:base + 16] = bytes([4, 9])
            item["hex"] = raw.hex()
        capture = replay.capture_from_records(rows)
        payload, transport = replay.encode_v8(capture)
        offset = replay.HEADER.size + replay.CONTEXT_HEADER.size + replay.CONTEXT_BYTES
        encoded = payload[offset:offset + replay.GAME_INFO_SIZE]
        decoded = replay._decode_setup(encoded.hex())
        self.assertEqual(len(decoded["players"]), 4)
        self.assertEqual([p["port"] for p in decoded["players"]], [1, 2, 3, 4])
        self.assertTrue(all(p["player_type"] == 1 and p["cpu_level"] == 9
                            for p in decoded["players"]))
        self.assertEqual(encoded.hex(), capture["setup_hex"])
        self.assertEqual(transport["version"], 8)

    def test_v8_accepts_three_legacy_budgets_with_bounded_transport_size(self):
        capture = replay.capture_from_records(_candidate())
        first = capture["frames"][0]
        count = replay.V8_MAX_FRAMES
        capture["frames"] = _RepeatedFrames(first, count)
        capture["spans"] = [{"scene": replay.SCENES["css"], "first_frame": 0,
                              "last_frame": count - 2},
                             {"scene": replay.SCENES["results"],
                              "first_frame": count - 1, "last_frame": count - 1}]
        payload, transport = replay.encode_v8(capture)
        self.assertEqual(transport["frame_count"], count)
        self.assertEqual(len(payload), 4775538)
        self.assertEqual(replay.LEGACY_MAX_FRAMES, 36000)

    def test_v8_rejects_frames_above_its_bounded_cap(self):
        capture = replay.capture_from_records(_candidate())
        capture["frames"] = _RepeatedFrames(capture["frames"][0],
                                             replay.V8_MAX_FRAMES + 1)
        with self.assertRaisesRegex(replay.WholeSessionReplayError, "108000"):
            replay.encode_v8(capture)

    def test_v8_rejects_unreachable_initial_and_final_owners(self):
        for scene in ("sss", "match", "results", "prize"):
            with self.subTest(initial=scene):
                capture = replay.capture_from_records(_candidate())
                capture["spans"][0]["scene"] = replay.SCENES[scene]
                with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                            "must start in CSS"):
                    replay.encode_v8(capture)
        for scene in ("css", "sss", "match"):
            with self.subTest(final=scene):
                capture = replay.capture_from_records(_candidate())
                capture["spans"][-1]["scene"] = replay.SCENES[scene]
                with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                            "must end in Results or Prize"):
                    replay.encode_v8(capture)

    def test_v8_rejects_span_index_outside_input_timeline(self):
        capture = replay.capture_from_records(_candidate())
        capture["spans"][0]["last_frame"] = len(capture["frames"])
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    "span last_frame"):
            replay.encode_v8(capture)

    def test_rejects_missing_source_consumption(self):
        rows = _candidate()
        rows = [row for row in rows if row["payload"].get("boundary") != "pad_consume"]
        for seq, row in enumerate(rows):
            row["seq"] = seq
        with self.assertRaisesRegex(replay.WholeSessionReplayError, "no source-consumed"):
            replay.capture_from_records(rows)

    def test_v9_accepts_and_retains_changed_setup_between_matches(self):
        rows = _candidate(v9_milestone=True)
        capture = replay.capture_from_records(rows)
        self.assertNotEqual(capture["setup_hexes"][0], capture["setup_hexes"][2])
        payload, _ = replay.encode_v9(capture)
        setup_cursor = replay.HEADER.size + replay.CONTEXT_HEADER.size + replay.CONTEXT_BYTES
        count, reserved = struct.unpack_from(">HH", payload, setup_cursor)
        self.assertEqual((count, reserved), (3, 0))
        setup_cursor += 4 + 2 * replay.GAME_INFO_SIZE
        third = replay._decode_setup(
            payload[setup_cursor:setup_cursor + replay.GAME_INFO_SIZE].hex())
        self.assertEqual(tuple(player["character_kind"] for player in third["players"]),
                         replay.V9_MILESTONE_ROSTER[2])

    def test_rejects_pad_queue_slot_mismatch(self):
        rows = _candidate()
        for row in rows:
            if row["payload"].get("boundary") == "pad_consume":
                row["payload"]["gprs"][25] += 0x30
                break
        with self.assertRaisesRegex(replay.WholeSessionReplayError, "source queue slot"):
            replay.capture_from_records(rows)

    def test_allows_source_step_reset_across_scene_owner_boundaries(self):
        rows = _candidate()
        pads = [row for row in rows
                if row["payload"].get("boundary") == "pad_consume"]
        self.assertGreaterEqual(len(pads), 2)
        pads[1]["source_tick"] = pads[0]["source_tick"]
        capture = replay.capture_from_records(rows)
        self.assertEqual(capture["frames"][0]["source_tick"],
                         capture["frames"][1]["source_tick"])

    def test_rejects_two_consumes_in_one_scene_instance(self):
        rows = _candidate()
        first = next(index for index, row in enumerate(rows)
                     if row["payload"].get("boundary") == "pad_consume")
        rows.insert(first + 1, copy.deepcopy(rows[first]))
        for seq, row in enumerate(rows):
            row["seq"] = seq
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    r"multiple PAD consumes.*css scene instance"):
            replay.capture_from_records(rows)

    def test_pad_snapshot_helper_keeps_source_layout(self):
        self.assertEqual(semantics.pad_snapshot_bytes(_raw_pad_snapshot()), _whole_pad_state())

    def test_path_projection_preserves_transport_and_rejects_corrupt_diagnostics(self):
        rows = _candidate()
        draw = next(row for row in rows
                    if row.get("payload", {}).get("boundary") == "draw_return")
        draw["payload"]["slices"].append({
            "name": "fighter_head", "address": 0x80580000,
            "size": 0x10000, "hex": "a5" * 0x10000,
        })
        expected = replay.capture_from_records(rows)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "large-diagnostic.mwro"
            _write_raw(path, rows)
            projected = list(replay._project_replay_records(observer_stream.iter_records(path)))
            self.assertEqual(len(projected), len(rows))
            projected_draw = next(row for row in projected
                                  if row.get("payload", {}).get("boundary") == "draw_return")
            self.assertNotIn("slices", projected_draw["payload"])
            self.assertNotIn("gprs", projected_draw["payload"])
            self.assertEqual(projected_draw["seq"], draw["seq"])
            actual = replay.capture_from_path(path)
            for field in ("identity", "lifecycle", "first_css", "setup_hex", "frames", "spans"):
                self.assertEqual(actual[field], expected[field], field)
            # A bad CRC in precisely the discarded diagnostic data must fail
            # before projection; input-only scope is not a malformed-row waiver.
            raw = bytearray(path.read_bytes())
            offset = raw.index(bytes.fromhex("a5" * 64))
            raw[offset] ^= 1
            path.write_bytes(raw)
            with self.assertRaisesRegex(replay.WholeSessionReplayError, "CRC|checksum"):
                replay.capture_from_path(path)

    def test_export_pair_reads_raw_mwro_and_writes_sidecar(self):
        rows_a = _candidate(v9_milestone=True)
        rows_b = _candidate("capture-b", "sequence-a", v9_milestone=True)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "a.mwro"
            second = root / "b.mwro"
            output = root / "candidate.mwrc"
            sidecar = root / "candidate.json"
            _write_raw(first, rows_a)
            _write_raw(second, rows_b)
            result = replay.export_pair(first, second, output, sidecar)
            self.assertTrue(output.is_file())
            self.assertTrue(sidecar.is_file())
            self.assertEqual(json.loads(sidecar.read_text())["claims"]
                             ["source_consumed_pad_repeatability"], "pass")
        self.assertEqual(result["transport"]["version"], 9)
        self.assertEqual(result["claims"]["runtime_initial_css_context"],
                         replay.RUNTIME_CONTEXT_STATUS)

    def test_export_pair_keeps_v8_for_identical_match_setups(self):
        rows_a = _candidate()
        rows_b = _candidate("capture-b", "sequence-a")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "a.mwro"
            second = root / "b.mwro"
            output = root / "candidate.mwrc"
            _write_raw(first, rows_a)
            _write_raw(second, rows_b)
            result = replay.export_pair(first, second, output)
            self.assertEqual(result["transport"]["version"], 8)
            self.assertEqual(replay.HEADER.unpack_from(output.read_bytes())[1], 8)

    def test_v9_preserves_each_match_setup_and_v8_rejects_changes(self):
        rows = _candidate(v9_milestone=True)
        capture = replay.capture_from_records(rows)
        expected = [_whole_session_cpu_setup(index).hex() for index in range(3)]
        self.assertEqual(capture["setup_hexes"], expected)
        self.assertEqual([tuple(player["character_kind"] for player in setup["players"])
                          for setup in capture["declared_setups"]],
                         list(replay.V9_MILESTONE_ROSTER))
        payload, transport = replay.encode_v9(capture)
        self.assertEqual(transport["version"], 9)
        self.assertEqual(replay.HEADER.unpack_from(payload)[1], 9)

        setup_cursor = replay.HEADER.size + replay.CONTEXT_HEADER.size + replay.CONTEXT_BYTES
        setup_count, reserved = struct.unpack_from(">HH", payload, setup_cursor)
        self.assertEqual((setup_count, reserved), (3, 0))
        setup_cursor += 4
        actual = [payload[setup_cursor + index * replay.GAME_INFO_SIZE:
                           setup_cursor + (index + 1) * replay.GAME_INFO_SIZE].hex()
                  for index in range(setup_count)]
        self.assertEqual(actual, expected)
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    "v8 requires identical StartMeleeData"):
            replay.encode_v8(capture)

    def test_v9_rejects_wrong_roster_cpu_level_rules_or_setup_count(self):
        capture = replay.capture_from_records(_candidate(v9_milestone=True))
        for mutate, message in (
            (lambda setup: setup.__setitem__(0x60, 1), "character lineup"),
            (lambda setup: setup.__setitem__(0x60 + 0x24 + 15, 8), "CPU9 profile"),
            (lambda setup: setup.__setitem__(0x0E, 0x21), "Final Destination"),
        ):
            with self.subTest(message=message):
                changed = dict(capture)
                changed["setup_hexes"] = list(capture["setup_hexes"])
                raw = bytearray.fromhex(changed["setup_hexes"][0])
                mutate(raw)
                changed["setup_hexes"][0] = raw.hex()
                with self.assertRaisesRegex(replay.WholeSessionReplayError, message):
                    replay.encode_v9(changed)
        changed = dict(capture)
        changed["setup_hexes"] = []
        for setup_hex in capture["setup_hexes"]:
            raw = bytearray.fromhex(setup_hex)
            raw[0x0B] = 0
            changed["setup_hexes"].append(raw.hex())
        with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                    "accepted four-Mario stock-match profile"):
            replay.encode_v9(changed)
        changed = dict(capture)
        changed["setup_hexes"] = capture["setup_hexes"][:2]
        with self.assertRaisesRegex(replay.WholeSessionReplayError, "exactly three"):
            replay.encode_v9(changed)

    def test_independent_pair_rejects_match_setup_change(self):
        rows_a = _candidate(v9_milestone=True)
        rows_b = _candidate("capture-b", "sequence-a", v9_milestone=True)
        entry = next(row for row in rows_b if row.get("event") == "boundary" and
                     row.get("payload", {}).get("boundary") == "entry" and
                     row["payload"]["match_index"] == 1)
        setup = next(value for value in entry["payload"]["slices"]
                     if value["name"] == "match_setup")
        raw = bytearray.fromhex(setup["hex"])
        raw[0x60 + 3] = 1
        setup["hex"] = raw.hex()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first, second = root / "a.mwro", root / "b.mwro"
            _write_raw(first, rows_a)
            _write_raw(second, rows_b)
            with self.assertRaisesRegex(replay.WholeSessionReplayError,
                                        "not source-consumed repeatable"):
                replay.export_pair(first, second, root / "candidate.mwrc")

    def test_export_single_marks_repeatability_as_unevaluated(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "single.mwro"
            output = root / "single.mwrc"
            sidecar = root / "single.json"
            _write_raw(source, _candidate(v9_milestone=True))
            result = replay.export_single(source, output, sidecar)
            self.assertEqual(result["claims"]["source_consumed_workload"], "pass")
            self.assertEqual(result["claims"]["source_consumed_pad_repeatability"],
                             "not_evaluated")
            self.assertEqual(result["claims"]["runtime_initial_css_context"],
                             replay.RUNTIME_CONTEXT_STATUS)
            self.assertEqual(json.loads(sidecar.read_text())["claims"]
                             ["independent_execution_identity"], "not_evaluated")

    def test_export_single_keeps_v8_for_identical_match_setups(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "single.mwro"
            output = root / "single.mwrc"
            _write_raw(source, _candidate())
            result = replay.export_single(source, output)
            self.assertEqual(result["transport"]["version"], 8)
            self.assertEqual(replay.HEADER.unpack_from(output.read_bytes())[1], 8)


if __name__ == "__main__":
    unittest.main()
