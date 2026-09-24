from __future__ import annotations

import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib


sys.path.insert(0, str(Path(__file__).parents[1] / "reference-capture" / "dolphin"))
import reference_observer_stream as stream


def frame(event: int, sequence: int, payload: bytes, *, pc: int = 0,
          source_tick: int = 0, draw_ordinal: int = 0) -> bytes:
    header = stream.HEADER.pack(
        stream.MAGIC_U32, stream.SCHEMA_VERSION, event, sequence, sequence + 10,
        pc, source_tick, draw_ordinal, len(payload), zlib.crc32(payload) & 0xffffffff)
    return header + payload


def boundary_payload() -> bytes:
    # One PAD queue slice and the complete 32-register boundary operand array.
    raw = b"queuebytes"
    prefix = stream.BOUNDARY.pack(1, 0, 0x80300000, 32, 1, 0)
    gprs = struct.pack("<32I", *range(32))
    descriptor_offset = stream.BOUNDARY.size + 32 * 4 + stream.SLICE.size
    descriptor = stream.SLICE.pack(2, 0, 0x804C1F78, len(raw), descriptor_offset)
    return prefix + gprs + descriptor + raw


def whole_boundary_payload(kind: int = 13, *, match_index: int = 0,
                           flags: int = stream.WHOLE_SESSION_FLAG,
                           metadata_kind: int | None = None,
                           metadata_reserved: int = 0,
                           audio_owner_epoch: int | None = None) -> bytes:
    raw = b"css-state"
    prefix = stream.BOUNDARY.pack(kind, flags, 0x80300000, 32, 1, 0)
    gprs = struct.pack("<32I", *range(32))
    descriptor_offset = stream.BOUNDARY.size + 32 * 4 + stream.SLICE.size
    descriptor = stream.SLICE.pack(31, 0, 0x804D6CC0, len(raw), descriptor_offset)
    if audio_owner_epoch is None:
        metadata = stream.WHOLE_METADATA.pack(
            match_index, kind if metadata_kind is None else metadata_kind, metadata_reserved)
    else:
        metadata = stream.WHOLE_AUDIO_METADATA.pack(
            match_index, kind if metadata_kind is None else metadata_kind,
            audio_owner_epoch, metadata_reserved)
    return prefix + gprs + descriptor + raw + metadata


def prize_profile_boundary_payload() -> bytes:
    values = (b"\x07\xff", b"\x01\xc0")
    prefix = stream.BOUNDARY.pack(26, stream.WHOLE_SESSION_FLAG, 0x801BFCFC,
                                  32, len(values), 0)
    gprs = struct.pack("<32I", *range(32))
    descriptor_offset = stream.BOUNDARY.size + 32 * 4 + stream.SLICE.size * len(values)
    descriptors = b""
    offset = descriptor_offset
    for tag, address, value in ((36, 0x8045C538, values[0]),
                                 (37, 0x8045C53A, values[1])):
        descriptors += stream.SLICE.pack(tag, 0, address, len(value), offset)
        offset += len(value)
    metadata = stream.WHOLE_AUDIO_METADATA.pack(0, 26, 3, 0)
    return prefix + gprs + descriptors + b"".join(values) + metadata


PROFILE_ROOT = 0x804D0000
PROFILE_GAME_RULES_OFFSET = 0x1850
PROFILE_GAME_RULES_SIZE = 0x18
PROFILE_SAVE_DATA_OFFSET = 0x1868
PROFILE_SAVE_DATA_SIZE = 0x55E8


def css_profile_context_boundary_payload() -> bytes:
    # A CSS entry publishes the authored rules and save block: the asserted
    # 0x18-byte GameRules and the 0x55E8-byte save block that carries the
    # unlock masks, counters, persistent fighter records and name banks.
    values = [
        (38, 0, PROFILE_ROOT + PROFILE_GAME_RULES_OFFSET,
         bytes((index + 1) & 0xFF for index in range(PROFILE_GAME_RULES_SIZE))),
        (39, 0, PROFILE_ROOT + PROFILE_SAVE_DATA_OFFSET,
         bytes((index * 7 + 3) & 0xFF for index in range(PROFILE_SAVE_DATA_SIZE))),
    ]
    prefix = stream.BOUNDARY.pack(13, stream.WHOLE_SESSION_FLAG, 0x8026688C,
                                  32, len(values), 0)
    gprs = struct.pack("<32I", *range(32))
    descriptor_offset = stream.BOUNDARY.size + 32 * 4 + stream.SLICE.size * len(values)
    descriptors = b""
    offset = descriptor_offset
    for tag, flags, address, value in values:
        descriptors += stream.SLICE.pack(tag, flags, address, len(value), offset)
        offset += len(value)
    metadata = stream.WHOLE_AUDIO_METADATA.pack(0, 13, 3, 0)
    return prefix + gprs + descriptors + b"".join(item[3] for item in values) + metadata


def startup_prize_exit_boundary_payload() -> bytes:
    raw = b"startup-prize-exit"
    prefix = stream.BOUNDARY.pack(30, stream.WHOLE_SESSION_FLAG, 0x801BFF7C,
                                  32, 1, 0)
    gprs = struct.pack("<32I", *range(32))
    descriptor_offset = stream.BOUNDARY.size + 32 * 4 + stream.SLICE.size
    descriptor = stream.SLICE.pack(36, 0, 0x8045C538, len(raw), descriptor_offset)
    metadata = stream.WHOLE_AUDIO_METADATA.pack(0, 30, 3, 0)
    return prefix + gprs + descriptor + raw + metadata


class ObserverStreamTests(unittest.TestCase):
    def test_decodes_strict_boundary_and_named_slices(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.mwro"
            path.write_bytes(
                frame(1, 0, b'{"schema":"test"}')
                + frame(2, 1, b'{"status":"recording"}')
                + frame(3, 2, boundary_payload(), pc=0x8034DD8C,
                       source_tick=17, draw_ordinal=4)
                + frame(6, 3, b'{"status":"completed","natural":true}')
            )
            records = list(stream.iter_records(path))
        self.assertEqual([record["event"] for record in records],
                         ["handshake", "start", "boundary", "end"])
        boundary = records[2]
        self.assertEqual(boundary["source_tick"], 17)
        self.assertEqual(boundary["payload"]["boundary"], "pad_poll")
        self.assertEqual(boundary["payload"]["gprs"][7], 7)
        self.assertEqual(boundary["payload"]["slices"][0]["name"], "pad_queue")
        self.assertEqual(boundary["payload"]["slices"][0]["address"], 0x804C1F78)
        self.assertEqual(boundary["payload"]["slices"][0]["hex"], b"queuebytes".hex())

    def test_rejects_sequence_gap_and_trailing_payload(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "gap.mwro"
            path.write_bytes(frame(1, 0, b"{}") + frame(2, 2, b"{}"))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))

            path.write_bytes(frame(1, 0, b"{}\x00"))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))

    def test_status_requires_explicit_completion_shape(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "status.json"
            path.write_text(json.dumps({
                "state": "completed", "event_count": 4, "last_seq": 3,
                "source_tick": 120, "draw_ordinal": 120,
                "completed": True, "invalid": False, "error": None,
            }))
            self.assertTrue(stream.read_status(path)["completed"])
            value = json.loads(path.read_text())
            value["completed"] = False
            path.write_text(json.dumps(value))
            with self.assertRaises(stream.ObserverStreamError):
                stream.read_status(path)

    def test_decodes_opt_in_whole_session_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "whole.mwro"
            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, whole_boundary_payload(match_index=2),
                       pc=0x8026688C))
            records = list(stream.iter_records(path))
        payload = records[-1]["payload"]
        self.assertTrue(payload["whole_session"])
        self.assertEqual(payload["match_index"], 2)
        self.assertEqual(payload["whole_boundary_kind"], 13)

    def test_decodes_extended_audio_owner_epoch_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "whole-audio.mwro"
            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, whole_boundary_payload(match_index=1,
                                                       audio_owner_epoch=7),
                       pc=0x8026688C))
            records = list(stream.iter_records(path))
        self.assertEqual(records[-1]["payload"]["audio_owner_epoch"], 7)

    def test_decodes_prize_boundary_and_typed_profile_masks(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "prize-profile.mwro"
            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, prize_profile_boundary_payload(), pc=0x801BFCFC))
            records = list(stream.iter_records(path))
        payload = records[-1]["payload"]
        self.assertEqual(payload["boundary"], "prize_mode_enter")
        self.assertEqual([item["name"] for item in payload["slices"]],
                         ["profile_characters", "profile_stages"])
        self.assertEqual([item["size"] for item in payload["slices"]], [2, 2])

    def test_decodes_startup_prize_mode_exit_with_pinned_boundary_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "startup-prize-exit.mwro"
            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, startup_prize_exit_boundary_payload(), pc=0x801BFF7C))
            records = list(stream.iter_records(path))
        payload = records[-1]["payload"]
        self.assertEqual(payload["boundary"], "startup_prize_mode_exit")
        self.assertEqual(payload["pc"], 0x801BFF7C)

    def test_decodes_css_typed_profile_context_slices(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "css-profile-context.mwro"
            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, css_profile_context_boundary_payload(), pc=0x8026688C))
            records = list(stream.iter_records(path))
        payload = records[-1]["payload"]
        self.assertEqual(payload["boundary"], "css_enter")
        self.assertEqual([item["name"] for item in payload["slices"]],
                         ["profile_game_rules", "profile_save_data"])
        self.assertEqual([item["size"] for item in payload["slices"]],
                         [PROFILE_GAME_RULES_SIZE, PROFILE_SAVE_DATA_SIZE])
        self.assertEqual([item["address"] for item in payload["slices"]],
                         [PROFILE_ROOT + PROFILE_GAME_RULES_OFFSET,
                          PROFILE_ROOT + PROFILE_SAVE_DATA_OFFSET])
        self.assertEqual(payload["slices"][0]["hex"],
                         bytes((index + 1) & 0xFF
                               for index in range(PROFILE_GAME_RULES_SIZE)).hex())

    def test_rejects_invalid_whole_metadata_or_missing_opt_in(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "whole-invalid.mwro"
            path.write_bytes(frame(3, 0, whole_boundary_payload(), pc=0x8026688C))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))

            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, whole_boundary_payload(flags=3),
                       pc=0x8026688C))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))

            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, whole_boundary_payload(metadata_kind=14),
                       pc=0x8026688C))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))

            path.write_bytes(
                frame(1, 0, b'{"whole_session":true,"match_count":3}')
                + frame(2, 1, b'{"whole_session":true,"match_count":3}')
                + frame(3, 2, whole_boundary_payload(metadata_reserved=1),
                       pc=0x8026688C))
            with self.assertRaises(stream.ObserverStreamError):
                list(stream.iter_records(path))


if __name__ == "__main__":
    unittest.main()
