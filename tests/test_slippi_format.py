"""Slippi Game Start ingestion and evidence classification."""

import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))
from slippi_format import RAW_PREFIX, SlippiFormatError, decode_header, read_header
from index_slippi import build_manifest, publish


def replay_fixture(version=(3, 17, 0, 0), fixes=(0, 0), *, pal=False,
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
    payloads = bytes((0x35, 4, 0x36, game_start_size >> 8,
                      game_start_size & 0xFF))
    raw = payloads + event
    return RAW_PREFIX + struct.pack(">I", len(raw)) + raw + b"}"


class SlippiFormatTests(unittest.TestCase):
    def test_decodes_vanilla_physical_input_candidate(self):
        header = decode_header(replay_fixture())
        record = header.record()
        self.assertEqual(record["slippi_version"], "3.17.0")
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


if __name__ == "__main__":
    unittest.main()
