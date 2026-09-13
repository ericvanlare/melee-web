"""Focused tests for the original result and scene-ownership boundaries.

These tests exercise the collector callbacks with a bounded fake GDB surface.
They do not start Dolphin or write game memory.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


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


def _match_memory(*, heads=b"\0\0\0\0", result_state=3,
                  result=2, scene_request=1):
    """Return the small read-only RAM view used by terminal callbacks."""
    head_address = 0x80001000

    def memory(address, size):
        if address == 0x804CE380:
            return b"\0" * size
        if address == head_address:
            return heads[:size]
        if address == 0x80479D30:
            return b"\0" * size
        if address == 0x8046B6A0:
            return bytes([result_state]) + b"\0" * (size - 1)
        if address == 0x8046B6A8:
            return bytes([result]) + b"\0" * (size - 1)
        if address == 0x80479D64:
            return struct.pack(">I", scene_request)[:size]
        return b"\0" * size

    def word(address):
        if address == 0x804D782C:
            return head_address
        if address == 0x80479D64:
            return scene_request
        return 0

    return memory, word


class RetailTerminalCollectorTests(unittest.TestCase):
    def setUp(self):
        self.collector = _load_collector("terminal_collector")
        self.collector.active = True
        self.collector.ready = True
        self.collector.source_finished = True
        self.collector.active_player_count = 2
        self.collector.frame_index = 3
        self.collector.draw_count = 3
        self.collector.last_drawn_source_index = 2
        self.collector.exit_observation = {"index": 2}
        self.collector.cpu_observation = SimpleNamespace(result=None,
                                                         end=mock.Mock())
        self.collector.gdb.parse_and_eval = lambda expression: {
            "$r3": 0x80479D98,
            "$lr": 0x8016E9C8,
        }[expression]

    def test_exact_bound_is_ready_and_finishes_after_final_draw(self):
        memory, word = _match_memory()
        self.collector.memory = memory
        self.collector.word = word
        self.collector.LIMIT = 3
        self.collector.source_finished = False
        self.collector.machine_context = lambda: []
        self.collector.draw_before = {"sentinel": True}
        self.collector.draw_source_index = 2
        self.collector.draw_count = 2
        self.collector.observations = SimpleNamespace(
            accept=lambda *args: True)
        self.collector.cpu_observation.draw = mock.Mock()

        sample = {"scene_frame": 3}
        self.collector.state = lambda: sample
        with tempfile.TemporaryDirectory(prefix="terminal-bound-") as directory:
            self.collector.ROOT = Path(directory)
            self.collector.OUTPUT = Path(directory) / "capture.jsonl"
            returned = self.collector.draw_return()
            self.assertFalse(returned)
            self.assertTrue(self.collector.source_finished)
            self.assertEqual(self.collector.last_drawn_source_index, 2)
            rows = [json.loads(line)
                    for line in self.collector.OUTPUT.read_text().splitlines()]
            self.assertEqual(rows[-1], {"record": "end", "frames": 3,
                                        "status": "captured"})

    def test_natural_end_before_exact_bound_is_rejected(self):
        memory, word = _match_memory()
        self.collector.memory = memory
        self.collector.word = word
        self.collector.LIMIT = 3
        self.collector.source_finished = False
        self.collector.machine_context = lambda: []
        self.collector.frame_index = 2
        self.collector.last_drawn_source_index = 1
        self.collector.exit_observation = {"index": 1}
        self.collector.draw_before = {"sentinel": True}
        self.collector.draw_source_index = 1
        self.collector.draw_count = 1
        self.collector.observations = SimpleNamespace(
            accept=lambda *args: True)
        self.collector.cpu_observation.draw = mock.Mock()
        self.collector.state = lambda: {"scene_frame": 2}

        with tempfile.TemporaryDirectory(prefix="terminal-early-") as directory:
            self.collector.ROOT = Path(directory)
            with self.assertRaisesRegex(RuntimeError, "before the declared exact reference bound"):
                self.collector.draw_return()
        self.assertTrue(self.collector.active)

    def test_result_publication_reads_exact_source_result(self):
        result = bytearray(0x28)
        result[4] = 2
        result[0xD] = 2
        result[0x10:0x12] = bytes((1, 0))

        def memory(address, size):
            if address == 0x80479D98 + 0xC:
                return bytes(result[:size])
            return b"\0" * size

        self.collector.memory = memory
        self.assertFalse(self.collector.result_enter())
        self.assertEqual(self.collector.result_pointer, 0x80479D98)
        self.assertFalse(self.collector.result_return())
        self.assertEqual(self.collector.cpu_observation.result,
                         {"outcome": 2, "winners": [1, 0]})

    def test_scene_reset_rejects_missing_published_result(self):
        self.collector.memory, self.collector.word = _match_memory()
        with self.assertRaisesRegex(RuntimeError, "preceded original result publication"):
            self.collector.scene_objects_reset()
        self.collector.cpu_observation.end.assert_not_called()

    def test_scene_reset_rejects_nonempty_original_entity_heads(self):
        self.collector.cpu_observation.result = {"outcome": 2, "winners": [1]}
        self.collector.memory, self.collector.word = _match_memory(
            heads=b"\0\0\0\1")
        with self.assertRaisesRegex(RuntimeError, "nonempty entity lists"):
            self.collector.scene_objects_reset()
        self.collector.cpu_observation.end.assert_not_called()
        self.assertTrue(self.collector.active)


if __name__ == "__main__":
    unittest.main()
