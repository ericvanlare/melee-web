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


if __name__ == "__main__":
    unittest.main()
