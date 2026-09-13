"""Slippi Game Start ingestion and evidence classification."""

import io
import json
from dataclasses import replace
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
from slippi_format import (RAW_PREFIX, SlippiFormatError, decode_header,
                           decode_timeline, read_header, read_timeline)
from replay_transport import (FLAG_DERIVED_SETUP, FLAG_PHYSICAL_INPUT,
                              FRAME, GAME_INFO_SIZE, HEADER, MAGIC, PAD, PLAYER,
                              encode_transport)
from index_slippi import build_manifest, publish
from normalize_slippi import normalize


def replay_fixture(version=(3, 19, 0, 0), fixes=(0, 0), *, pal=False,
                   frozen=False, major_scene=2, stage=31):
    game_start_size = 0x2F9 if version >= (3, 17, 0, 0) else 0x1A4
    event = bytearray(1 + game_start_size)
    event[0] = 0x36
    event[1:5] = bytes(version)
    info = memoryview(event)[5:5 + 0x138]
    struct.pack_into(">H", info, 0xE, stage)
    for index, character in enumerate((2, 18)):
        offset = 0x60 + 0x24 * index
        info[offset:offset + 4] = bytes((character, 0, 4, index))
    for index in range(2, 4):
        info[0x61 + 0x24 * index] = 3
    struct.pack_into(">I", event, 0x13D, 0x12345678)
    for index, fix in enumerate(fixes):
        struct.pack_into(">II", event, 0x141 + 8 * index, fix, fix)
    if version >= (1, 5, 0, 0):
        event[0x1A1] = int(pal)
    if version >= (2, 0, 0, 0):
        event[0x1A2] = int(frozen)
    if version >= (3, 7, 0, 0):
        event[0x1A3] = 2
        event[0x1A4] = major_scene
    pre_size = 0x42 if version >= (3, 19, 0, 0) else 0x40 \
        if version >= (3, 17, 0, 0) else 0x3F
    payloads = bytes((0x35, 7, 0x36, game_start_size >> 8,
                      game_start_size & 0xFF, 0x37, pre_size >> 8,
                      pre_size & 0xFF))
    raw = payloads + event
    return RAW_PREFIX + struct.pack(">I", len(raw)) + raw + b"}"


def _pre_frame(frame, port, *, buttons=0x100, raw=(64, -32, 127, -127)):
    event = bytearray(1 + 0x42)
    event[0] = 0x37
    struct.pack_into(">iBBI", event, 1, frame, port, 0, 0xA0000000 + frame)
    struct.pack_into(">H", event, 0xB, 14)
    for offset, value in zip((0xD, 0x11, 0x15, 0x19, 0x1D, 0x21,
                              0x25, 0x29),
                             (1.25, -2.5, 1.0, 0.5, -0.25, 1.0, -1.0, 0.3)):
        struct.pack_into(">f", event, offset, value)
    struct.pack_into(">IHff", event, 0x2D, buttons, buttons & 0xFFFF,
                     0.25, 0.75)
    struct.pack_into("b", event, 0x3B, raw[0])
    struct.pack_into(">f", event, 0x3C, 12.0)
    for offset, value in zip((0x40, 0x41, 0x42), raw[1:]):
        struct.pack_into("b", event, offset, value)
    return bytes(event)


def _post_frame(frame, port, *, action=14):
    event = bytearray(1 + 0x54)
    event[0] = 0x38
    struct.pack_into(">iBBBH", event, 1, frame, port, 0, 2 if port == 0 else 8,
                     action)
    for offset, value in zip((0xA, 0xE, 0x12, 0x16, 0x1A),
                             (1.5, -2.0, 1.0, 12.0, 60.0)):
        struct.pack_into(">f", event, offset, value)
    event[0x21] = 4
    event[0x26:0x2B] = bytes((1, 2, 3, 4, 5))
    event[0x2F] = 1
    struct.pack_into(">H", event, 0x30, 7)
    event[0x32] = 1
    event[0x34] = 2
    return bytes(event)


def timeline_fixture(*, missing_port=False, fixes=(0, 0)):
    header_only = replay_fixture(fixes=fixes)
    raw_size = struct.unpack_from(">I", header_only, len(RAW_PREFIX))[0]
    header_raw = header_only[15:15 + raw_size]
    old_table_size = 1 + header_raw[1]
    game_start = header_raw[old_table_size:]
    sizes = ((0x36, len(game_start) - 1), (0x37, 0x42), (0x38, 0x54),
             (0x39, 6), (0x3A, 12), (0x3C, 8))
    table = bytearray((0x35, 1 + 3 * len(sizes)))
    for command, size in sizes:
        table += bytes((command, size >> 8, size & 0xFF))
    events = [bytes(table), game_start]
    for frame in (-123, -122, -121):
        start = bytearray(13)
        start[0] = 0x3A
        struct.pack_into(">iII", start, 1, frame, 0xB0000000 + frame,
                         frame + 123)
        events.append(bytes(start))
        for port in range(2):
            if missing_port and frame == -122 and port == 1:
                continue
            events.extend((_pre_frame(frame, port), _post_frame(frame, port)))
        if frame == -122:
            events.extend((_pre_frame(frame, 0, buttons=0x200),
                           _post_frame(frame, 0, action=15)))
        bookend = bytearray(9)
        bookend[0] = 0x3C
        finalized = frame if frame == -123 else frame - 1
        struct.pack_into(">ii", bookend, 1, frame, finalized)
        events.append(bytes(bookend))
    game_end = bytearray(7)
    game_end[0] = 0x39
    game_end[1] = 7
    game_end[2] = 0
    events.append(bytes(game_end))
    raw = b"".join(events)
    return RAW_PREFIX + struct.pack(">I", len(raw)) + raw + b"ignored metadata"


class SlippiFormatTests(unittest.TestCase):
    def test_decodes_vanilla_physical_input_candidate(self):
        header = decode_header(replay_fixture())
        record = header.record()
        self.assertEqual(record["slippi_version"], "3.19.0")
        self.assertEqual(record["stage_id"], 31)
        self.assertEqual(record["random_seed"], 0x12345678)
        self.assertEqual([p["character_id"] for p in record["players"]], [2, 18])
        self.assertTrue(record["classification"]["vanilla_profile_candidate"])
        self.assertTrue(record["classification"]["raw_pad_fields_complete"])

    def test_ucf_and_online_scene_are_explicit_rejections(self):
        record = decode_header(replay_fixture(fixes=(1, 1), major_scene=8)).record()
        reasons = record["classification"]["vanilla_rejection_reasons"]
        self.assertIn("port 1 controller fix is ucf", reasons)
        self.assertIn("port 2 controller fix is ucf", reasons)
        self.assertIn("major scene 8 is not offline VS mode", reasons)
        self.assertFalse(record["classification"]["vanilla_profile_candidate"])

    def test_legacy_version_reports_processed_input_scope(self):
        record = decode_header(replay_fixture(version=(2, 0, 1, 0), fixes=(0, 0))).record()
        self.assertEqual(record["classification"]["input_evidence"], "processed_only")
        self.assertFalse(record["classification"]["raw_pad_fields_complete"])
        self.assertIsNone(record["major_scene"])

    def test_version_label_does_not_overstate_missing_physical_fields(self):
        record = decode_header(replay_fixture(version=(3, 17, 0, 0))).record()
        self.assertFalse(record["classification"]["raw_pad_fields_complete"])

    def test_streaming_reader_rejects_zero_length_and_truncation(self):
        valid = replay_fixture()
        self.assertEqual(read_header(io.BytesIO(valid)).random_seed, 0x12345678)
        zero = bytearray(valid)
        zero[len(RAW_PREFIX):len(RAW_PREFIX) + 4] = b"\0\0\0\0"
        with self.assertRaisesRegex(SlippiFormatError, "raw length is zero"):
            read_header(io.BytesIO(zero))
        with self.assertRaisesRegex(SlippiFormatError, "truncated Game Start"):
            read_header(io.BytesIO(valid[:100]))
        bad_table = bytearray(valid)
        bad_table[16] = 0
        with self.assertRaisesRegex(SlippiFormatError, "Event Payloads size"):
            read_header(io.BytesIO(bad_table))
        short_raw = bytearray(valid)
        short_raw[len(RAW_PREFIX):len(RAW_PREFIX) + 4] = struct.pack(">I", 5)
        with self.assertRaisesRegex(SlippiFormatError, "declared raw stream"):
            read_header(io.BytesIO(short_raw))

    def test_manifest_deduplicates_and_does_not_publish_paths(self):
        with tempfile.TemporaryDirectory(prefix="slippi index ") as temporary:
            root = Path(temporary)
            (root / "Player Name - set one.slp").write_bytes(replay_fixture())
            (root / "copy.slp").write_bytes(replay_fixture())
            manifest = build_manifest([root], 2)
            self.assertEqual((manifest["files_discovered"], manifest["unique_files"],
                              manifest["duplicates"]), (2, 1, 1))
            encoded = json.dumps(manifest)
            self.assertNotIn("Player Name", encoded)
            output = root / "manifest.json"
            publish(output, manifest)
            self.assertEqual(json.loads(output.read_text()), manifest)

    def test_empty_directory_is_not_a_successful_corpus(self):
        with tempfile.TemporaryDirectory(prefix="empty slippi index ") as temporary:
            with self.assertRaisesRegex(ValueError, "no .slp files"):
                build_manifest([Path(temporary)], 1)

    def test_normalizes_finalized_physical_inputs_and_exact_float_bits(self):
        timeline = decode_timeline(timeline_fixture())
        self.assertEqual([frame.number for frame in timeline.frames], [-123, -122, -121])
        self.assertEqual(timeline.finalized_through, None)
        self.assertEqual(timeline.duplicate_updates, 2)
        corrected = timeline.frames[1].inputs[0]
        self.assertEqual(corrected.physical_buttons, 0x200)
        self.assertEqual(corrected.raw_stick, (64, -32))
        self.assertEqual(corrected.raw_cstick, (127, -127))
        self.assertEqual(corrected.processed_stick_bits, (0x3F000000, 0xBE800000))
        self.assertEqual(timeline.frames[1].expected[0].action_state, 15)
        self.assertTrue(timeline.classification()["exact_input_candidate"])

    def test_streaming_timeline_does_not_read_private_metadata(self):
        replay = timeline_fixture()
        timeline = read_timeline(io.BytesIO(replay))
        self.assertEqual(len(timeline.frames), 3)
        self.assertNotIn("metadata", json.dumps(timeline.record()))

    def test_timeline_rejects_missing_active_port(self):
        with self.assertRaisesRegex(SlippiFormatError,
                                    "frame -122 is missing port 2 input"):
            decode_timeline(timeline_fixture(missing_port=True))

    def test_normalized_record_uses_content_identity_without_source_path(self):
        with tempfile.TemporaryDirectory(prefix="private slippi ") as temporary:
            source = Path(temporary) / "Player Name - set.slp"
            source.write_bytes(timeline_fixture())
            record = normalize(source)
            encoded = json.dumps(record)
            self.assertEqual(record["schema"], "melee-web-normalized-replay")
            self.assertEqual(len(record["source_sha256"]), 64)
            self.assertEqual(record["source"]["sha256"], record["source_sha256"])
            self.assertEqual(record["source"]["settings"]["stage_id"], 31)
            self.assertEqual(record["workload"]["setup"], {
                "rules_origin": "derived", "stock_count": 4,
                "timer_enabled": False, "items_enabled": False,
                "is_teams": False, "rumble_enabled": True})
            self.assertEqual(record["workload"]["port_mapping"], [
                {"source_port": 1, "runtime_slot": 0},
                {"source_port": 2, "runtime_slot": 1}])
            self.assertNotIn("Player Name", encoded)
            self.assertNotIn(str(source), encoded)

    def test_ucf_rejection_cannot_become_accuracy_success(self):
        timeline = decode_timeline(timeline_fixture(fixes=(1, 1)))
        self.assertFalse(timeline.classification()["exact_input_candidate"])
        encoded = encode_transport(timeline, "12" * 32)
        self.assertEqual(
            HEADER.unpack_from(encoded)[2],
            FLAG_PHYSICAL_INPUT | FLAG_DERIVED_SETUP)
        with tempfile.TemporaryDirectory(prefix="ucf slippi ") as temporary:
            source = Path(temporary) / "ucf.slp"
            source.write_bytes(timeline_fixture(fixes=(1, 1)))
            record = normalize(source)
            self.assertEqual(record["workload"]["comparison"], "never")
            self.assertFalse(record["classification"]["exact_input_candidate"])

    def test_fixed_transport_is_input_only_and_labels_overrides(self):
        timeline = decode_timeline(timeline_fixture())
        digest = "12" * 32
        encoded = encode_transport(timeline, digest)
        header = HEADER.unpack_from(encoded)
        self.assertEqual(header[0], MAGIC)
        self.assertEqual(header[1], 2)
        self.assertEqual(header[2], FLAG_PHYSICAL_INPUT | FLAG_DERIVED_SETUP)
        self.assertEqual(header[8:10], (31, 31))
        expected_size = (HEADER.size + GAME_INFO_SIZE + 2 * PLAYER.size +
                         len(timeline.frames) *
                         (FRAME.size + 2 * PAD.size))
        self.assertEqual(len(encoded), expected_size)
        overridden = encode_transport(timeline, digest, characters=(2, 8), stage=32)
        overridden_header = HEADER.unpack_from(overridden)
        self.assertEqual(overridden_header[2], FLAG_PHYSICAL_INPUT | FLAG_DERIVED_SETUP)
        self.assertEqual(overridden_header[8:10], (32, 31))
        game_info_start = HEADER.size
        self.assertEqual(
            overridden[game_info_start + 0xE:game_info_start + 0x10],
            b"\x00\x1f")
        player_start = HEADER.size + GAME_INFO_SIZE
        self.assertEqual(PLAYER.unpack_from(overridden, player_start)[:3], (1, 2, 2))
        self.assertEqual(PLAYER.unpack_from(overridden, player_start + PLAYER.size)[:3],
                         (2, 18, 8))

    def test_transport_rejects_unsupported_source_port_mapping(self):
        timeline = decode_timeline(timeline_fixture())
        players = [dict(player) for player in timeline.header.players]
        players[0]["port"] = 3
        header = replace(timeline.header, players=tuple(players))
        unsupported = replace(timeline, header=header)
        with self.assertRaisesRegex(SlippiFormatError, "only supports source ports"):
            encode_transport(unsupported, "12" * 32)


if __name__ == "__main__":
    unittest.main()
