"""Exercise diagnostic error/nesting boundaries with a read-only GDB double."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from retail_allocation_profile import read_symbols


class AllocationCaptureTests(unittest.TestCase):
    def load_collector(self, directory, initial_pc=0x8000522C):
        registers = {"pc": initial_pc, "r1": 0x81000000, "lr": 0x80010000,
                     "r3": 1, "r4": 64}
        for name in [*("r%d" % i for i in range(32)), "ctr", "cr"]:
            registers.setdefault(name, 0)
        words = {0x800000E4: 0x80400000, 0x80001000: 0x7C0802A6,
                 0x80001010: 0x4E800020}
        words.update({address: 0 for address in (0x80000030, 0x80000034, 0x80000028, 0x800000F4,
                                                0x80479D58, 0x804D7420)})
        breakpoints = []
        class Breakpoint:
            def __init__(self, address, kind, internal):
                self.address = address
                breakpoints.append(self)
        # Deliberately expose no write_memory, register assignment or inferior
        # call API. The observer must work entirely through reads.
        inferior = types.SimpleNamespace(read_memory=lambda address, size: struct.pack(">I", words[address])[:size])
        module = types.SimpleNamespace(Breakpoint=Breakpoint, BP_HARDWARE_BREAKPOINT=1,
            selected_inferior=lambda: inferior,
            parse_and_eval=lambda name: registers[name[1:]])
        globals_ = {name: {"address": 0x80401000 + index * 4} for index, name in enumerate(
            ("HeapArray", "NumHeaps", "ArenaStart", "ArenaEnd", "__OSCurrHeap", "current_heap", "__OSArenaLo", "__OSArenaHi"))}
        words.update({info["address"]: 0 for info in globals_.values()})
        profile = {"entry": 0x8000522C, "globals": globals_, "functions": [
            {"name": "OSAllocFromHeap", "address": 0x80001000, "entry_word": 0x7C0802A6,
             "returns": [0x80001010], "argc": 2}]}
        profile_path = directory / "profile.json"
        profile_path.write_text(json.dumps(profile))
        output = directory / "allocations.jsonl"
        namespace = {}
        with patch.dict(sys.modules, {"gdb": module}), patch.dict(os.environ, {
                "MELEE_ALLOCATION_PROFILE": str(profile_path), "MELEE_ALLOCATION_OUTPUT": str(output)}), contextlib.redirect_stdout(io.StringIO()):
            try:
                exec(compile((ROOT / "tools/reference_allocation_capture.py").read_text(),
                             "reference_allocation_capture.py", "exec"), namespace)
            except Exception:
                if "_allocation_stream" in namespace: namespace["_allocation_stream"].close()
                raise
        return namespace, registers, words, breakpoints, output

    def test_observed_result_is_separate_from_call_inputs(self):
        with tempfile.TemporaryDirectory() as temp:
            state, registers, _, probes, output = self.load_collector(Path(temp))
            self.assertFalse(probes[0].stop())
            registers["r3"] = 0x80500020
            self.assertFalse(probes[1].stop())
            state["allocation_finish"]("captured")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[1]["args"], [1, 64])
            self.assertNotIn("result", rows[1])
            self.assertEqual(rows[2]["result"], 0x80500020)
            self.assertEqual(rows[-1]["status"], "captured")

    def test_unmatched_return_retains_error_and_rejects_completion(self):
        with tempfile.TemporaryDirectory() as temp:
            state, _, _, probes, output = self.load_collector(Path(temp))
            self.assertTrue(probes[1].stop())
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                state["allocation_finish"]("captured")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[-2]["record"], "error")
            self.assertEqual(rows[-1]["status"], "incomplete")

    def test_unchanged_remote_retrap_is_retained_without_a_second_allocation(self):
        with tempfile.TemporaryDirectory() as temp:
            state, registers, _, probes, output = self.load_collector(Path(temp))
            self.assertFalse(probes[0].stop())
            self.assertFalse(probes[0].stop())
            self.assertFalse(probes[1].stop())
            state["allocation_finish"]("captured")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[2]["record"], "repeated_stop")
            self.assertEqual(rows[2]["original_sequence"], rows[1]["sequence"])
            self.assertEqual(rows[-1]["calls"], 1)

    def test_changed_non_argument_register_at_retrap_is_a_failure(self):
        with tempfile.TemporaryDirectory() as temp:
            state, registers, _, probes, output = self.load_collector(Path(temp))
            probes[0].stop()
            registers["r31"] += 1
            self.assertTrue(probes[0].stop())
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                state["allocation_finish"]("captured")
            self.assertIn("changed allocator state", output.read_text())

    def test_live_nested_call_cannot_be_certified_complete(self):
        with tempfile.TemporaryDirectory() as temp:
            state, registers, _, probes, output = self.load_collector(Path(temp))
            probes[0].stop()
            registers["r1"] -= 32
            probes[0].stop()
            with self.assertRaisesRegex(RuntimeError, "incomplete"):
                state["allocation_finish"]("captured")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertEqual(rows[2]["parent"], rows[1]["call"])
            self.assertEqual(len(rows[-1]["pending_calls"]), 2)

    def test_thread_interleaving_keeps_independent_nesting(self):
        with tempfile.TemporaryDirectory() as temp:
            state, registers, words, probes, output = self.load_collector(Path(temp))
            probes[0].stop()
            words[0x800000E4] = 0x80401000
            probes[0].stop()
            probes[1].stop()
            words[0x800000E4] = 0x80400000
            probes[1].stop()
            state["allocation_finish"]("captured")
            rows = [json.loads(line) for line in output.read_text().splitlines()]
            self.assertIsNone(rows[2]["parent"])
            self.assertEqual(rows[-1]["calls"], 2)

    def test_late_attach_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "DOL entry"):
                self.load_collector(Path(temp), initial_pc=0x80390EB4)

    def test_duplicate_required_symbol_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "symbols.txt"
            line = "OSInitAlloc = .text:0x80001000; // type:function size:0x20 scope:global\n"
            path.write_text(line + line)
            with self.assertRaisesRegex(ValueError, "ambiguous"):
                read_symbols(path)


if __name__ == "__main__":
    unittest.main()
