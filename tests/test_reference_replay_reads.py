"""Focused tests for grouped read-only reference collector memory reads."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]


def _load_collector(name):
    class Command:
        def __init__(self, *args, **kwargs):
            pass

    fake_gdb = SimpleNamespace(Command=Command, Breakpoint=Command,
                               COMMAND_USER=0)
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "tools" / "reference_replay_capture.py")
    assert spec is not None and spec.loader is not None
    collector = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"gdb": fake_gdb}):
        spec.loader.exec_module(collector)
    return collector


def _reader(segments):
    calls = []

    def read(address, size):
        calls.append((address, size))
        for base, data in segments:
            if base <= address and address + size <= base + len(data):
                start = address - base
                return data[start:start + size]
        raise AssertionError(f"unmapped fake read {address:#x}+{size:#x}")

    return read, calls


class ReferenceReplayGroupedReadTests(unittest.TestCase):
    def test_fighter_state_preserves_values_with_grouped_ranges(self):
        collector = _load_collector("grouped_fighter_state")
        address = 0x80010000
        head = bytes((index * 5 + 3) & 0xFF for index in range(0x100))
        input_anim = bytes((index * 7 + 11) & 0xFF for index in range(0x280))
        damage_shield = bytes((index * 13 + 17) & 0xFF for index in range(0x16C))
        stock_address = 0x80453080 + 2 * 0xE90 + 0x8E
        reader, calls = _reader([
            (address, head),
            (address + 0x620, input_anim),
            (address + 0x1830, damage_shield),
            (stock_address, b"\xfe"),
        ])
        collector.memory = reader

        result = collector.fighter_state(2, address)

        self.assertEqual(result["kind"], int.from_bytes(head[4:8], "big"))
        self.assertEqual(result["motion"], int.from_bytes(head[0x10:0x14], "big"))
        self.assertEqual(result["position_bits"],
                         [head[index:index + 4].hex() for index in range(0xB0, 0xBC, 4)])
        self.assertEqual(result["animation_frame_bits"], input_anim[0x274:0x278].hex())
        self.assertEqual(result["animation_speed_bits"], input_anim[0x27C:0x280].hex())
        self.assertEqual(result["damage_bits"], damage_shield[:4].hex())
        self.assertEqual(result["shield_bits"], damage_shield[0x168:0x16C].hex())
        self.assertEqual(result["stocks"], -2)
        self.assertEqual(result["input_hex"], input_anim[:0x6C].hex())
        self.assertEqual(calls, [
            (address, 0x100),
            (address + 0x620, 0x280),
            (address + 0x1830, 0x16C),
            (stock_address, 1),
        ])
        self.assertTrue(all(size <= 0x1000 for _, size in calls))

    def test_pad_state_uses_one_contiguous_bounded_read(self):
        collector = _load_collector("grouped_pad_state")
        address = 0x804C1F84
        combined = bytes((index * 9 + 5) & 0xFF for index in range(0x358))
        reader, calls = _reader([(address, combined)])
        collector.memory = reader

        result = collector.pad_state()

        expected = bytearray(combined[:10] + combined[12:0x20])
        for base in (0x804C1FAC, 0x804C20BC, 0x804C21CC):
            offset = base - address
            raw = combined[offset:offset + 0x110]
            for index in range(0, 0x110, 68):
                expected.extend(raw[index:index + 66])
        self.assertEqual(result, bytes(expected).hex())
        self.assertEqual(calls, [(address, 0x358)])
        self.assertLessEqual(calls[0][1], 0x1000)

    def test_timer_audit_uses_one_range_without_changing_fields(self):
        collector = _load_collector("grouped_timer_audit")
        address = 0x8046B6A0
        timer = bytearray(0x30)
        timer[0] = 3
        timer[8] = 2
        struct.pack_into(">I", timer, 0x24, 0x12345678)
        struct.pack_into(">I", timer, 0x28, 0x01020304)
        struct.pack_into(">H", timer, 0x2C, 0x0506)
        reader, calls = _reader([(address, bytes(timer))])
        collector.memory = reader
        collector.UNTIL_MATCH_END = False
        with tempfile.TemporaryDirectory(prefix="grouped-timer-audit-") as directory:
            collector.ROOT = Path(directory)
            collector.timer_audit({"record": "match_enter",
                                   "start_melee_hex": (bytes([2]) + bytes(0x137)).hex()})
            collector.timer_audit({"record": "frame", "index": 7})
            rows = [json.loads(line) for line in
                    (collector.ROOT / "timer-audit.jsonl").read_text().splitlines()]

        self.assertEqual(rows[1], {
            "record": "frame",
            "index": 7,
            "match_frame": 0x12345678,
            "seconds": 0x01020304,
            "subframe": 0x0506,
            "outcome": 2,
            "end_state": 3,
        })
        self.assertEqual(calls, [(address, 0x30)])
        self.assertLessEqual(calls[0][1], 0x1000)


if __name__ == "__main__":
    unittest.main()
