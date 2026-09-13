"""Focused host-only checks for the read-only CPU register diagnostic."""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "retail_cpu_registers", ROOT / "tools/retail_cpu_registers.py")
REG = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = REG
SPEC.loader.exec_module(REG)


class CpuRegisterTests(unittest.TestCase):
    def test_generated_probe_window_uses_gdb_enabled_property(self):
        # GDB has an enabled property, not enable()/disable() methods. Execute
        # the generated hooks so a compile-only test cannot miss that API error.
        class Breakpoint:
            __slots__ = ("enabled", "silent")

            def __init__(self, *args, **kwargs):
                self.enabled = True

        class Arm:
            def invoke(self, args, from_tty):
                return None

        env = {"json": json, "Path": Path, "os": SimpleNamespace(environ={}),
               "gdb": SimpleNamespace(Breakpoint=Breakpoint,
                                      BP_HARDWARE_BREAKPOINT=1),
               "word": lambda address: 0x12345678,
               "created": lambda: None, "entered": lambda: False,
               "Arm": Arm, "active": True, "ready": True,
               "source_finished": False, "frame_index": 0, "breakpoints": []}

        def advance():
            env["frame_index"] += 1
            return False

        env["scheduler_return"] = advance
        source = REG.render_instrumentation(
            probes=[REG.Probe("point", 0x80000000, 0x12345678)],
            start_tick=2, end_tick=3, stack_bytes=64,
            backchain_depth=8, helper_sha256="a" * 64)
        exec(source, env)
        rows = []
        env["_cpu_register_emit"] = rows.append
        Arm().invoke("", False)
        probe = env["_CPU_REGISTER_PROBE_OBJECTS"][0]
        self.assertFalse(probe.enabled)
        env["entered"]()
        env["scheduler_return"]()  # tick 0 completed; next tick is 1
        self.assertFalse(probe.enabled)
        env["scheduler_return"]()  # tick 1 completed; window begins at 2
        self.assertTrue(probe.enabled)
        env["scheduler_return"]()
        self.assertTrue(probe.enabled)
        saved = env["_cpu_register_original_scheduler"]
        env["_cpu_register_original_scheduler"] = lambda: False
        env["scheduler_return"]()  # duplicated prior scheduler stop
        self.assertTrue(probe.enabled)
        self.assertEqual(len(rows), 1)
        env["_cpu_register_original_scheduler"] = saved
        env["scheduler_return"]()  # tick 3 completed; disable immediately
        self.assertFalse(probe.enabled)
        self.assertEqual(rows[-1], {"record": "end",
                                   "status": "diagnostic_window_complete",
                                   "frames": 4, "window_end_tick": 3})

        # The original SDK ends its main stack with -1, not a readable frame.
        # Preserve that backchain word without attempting a sentinel read.
        reads = []

        def memory(address, size):
            reads.append((address, size))
            if address != 0x80001000:
                raise RuntimeError("unexpected memory read")
            return bytes.fromhex("ffffffff") + bytes(size - 4)

        import struct
        env["struct"] = struct
        env["_cpu_register_read"] = memory
        stack = env["_cpu_register_stack"]({"r1": 0x80001000})
        self.assertEqual(reads, [(0x80001000, 64)])
        self.assertEqual(stack["frames"][0]["backchain"], 0xffffffff)
        self.assertEqual(REG.diagnostic_errors([stack]), [])
        broken = env["_cpu_register_stack"]({"r1": 0x1234})
        self.assertTrue(REG.diagnostic_errors([broken]))

    def test_probe_document_binds_word_pins_and_call_phases(self):
        probes = REG.validate_probes({
            "schema": REG.SCHEMA, "version": REG.VERSION,
            "probes": [
                {"label": "entry", "address": "0x8000a000",
                 "expected_word": "0x7c0802a6", "phase": "before", "call": "foo"},
                {"label": "return", "address": 0x8000a004,
                 "expected_word": 0x4e800020, "phase": "after", "call": "foo"},
            ]})
        self.assertEqual(probes[0].record()["address"], "0x8000a000")
        self.assertEqual(probes[1].record()["expected_word"], "0x4e800020")

    def test_probe_document_rejects_unsafe_or_ambiguous_definitions(self):
        cases = [
            ({"label": "bad", "address": 0x80000002, "expected_word": 0}, "aligned"),
            ({"label": "bad", "address": 0x81800000, "expected_word": 0}, "bounded"),
            ({"label": "bad", "address": 0x80000000, "expected_word": 0,
              "phase": "before"}, "requires call"),
            ({"label": "bad!", "address": 0x80000000, "expected_word": 0}, "label"),
        ]
        for value, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(REG.CpuRegisterError, message):
                    REG.validate_probe(value)
        with self.assertRaisesRegex(REG.CpuRegisterError, "unique"):
            REG.validate_probes([
                {"label": "x", "address": 0x80000000, "expected_word": 0},
                {"label": "x", "address": 0x80000004, "expected_word": 0},
            ])

    def test_instrumented_collector_preserves_base_prefix_and_compiles(self):
        base = (ROOT / "tools/reference_replay_capture.py").read_bytes()
        helper_hash = hashlib.sha256(
            (ROOT / "tools/retail_cpu_registers.py").read_bytes()).hexdigest()
        instrumented = REG.instrument_collector(
            base, probes=[REG.Probe("point", 0x80000000, 0x12345678)],
            start_tick=2494, end_tick=2496, stack_bytes=64,
            backchain_depth=8, helper_sha256=helper_hash)
        self.assertTrue(instrumented.startswith(base[:-len(b"Arm()\n")]))
        self.assertEqual(instrumented.count(b"\nArm()\n"), 1)
        self.assertIn(b"_CPU_REGISTER_END = 2496", instrumented)
        compile(instrumented.decode("utf-8"), "generated-reference-collector.py", "exec")

    def test_diagnostic_loader_requires_explicit_diagnostic_end(self):
        probe = REG.Probe("point", 0x80000000, 0x12345678)
        fighter = {
            "slot": 0, "fighter_pointer": 0x80001000, "actor_pointer": 0x80002000,
            "actor_user_data": 0x80001000, "register_matches": {"r3": 0x80001000},
            "fighter": {"kind": 0, "player_id": 0, "motion": 0, "animation": 0,
                         "facing_bits": "00000000", "position_bits": ["00000000"] * 3,
                         "velocity_bits": ["00000000"] * 3,
                         "knockback_velocity_bits": ["00000000"] * 3, "ground_air": 0},
            "cpu": {"pointer": 0x80002000, "kind": 4, "level": 1, "state": 0,
                    "default_state": 0, "secondary_state": 0, "buttons": 0,
                    "sticks": [0] * 4, "triggers": [0, 0],
                    "raw_hex": "00" * 0x57c},
            "knockback": {"percent_bits": "00000000", "kb_applied_bits": "00000000",
                           "angle": 0, "magnitude_bits": "00000000", "time_since_hit": 0,
                           "raw_hex": "00" * 0x88},
            "flags": {"x2210_2230_hex": "00" * 0x20, "x221a": 0, "x221a_b3": 0},
        }
        registers = {"gpr": {"r%d" % index: 0 for index in range(32)},
                     "fpr": {"f%d" % index: {"bytes_hex": "0000000000000000", "length": 8,
                                                "type": "float"} for index in range(7)},
                     "pc": 0, "lr": 0, "ctr": 0, "cr": 0}
        rows = [
            {"record": "header", "schema": REG.SCHEMA, "version": REG.VERSION,
             "status": "diagnostic_only", "window": {"start_tick": 4, "end_tick": 6},
             "probes": [probe.record()], "extra_memory": [], "helper_sha256": "a" * 64,
             "input_plan_sha256": "b" * 64},
            {"record": "probe", "sequence": 0, "tick": 4, "probe": probe.record(),
             "registers": registers, "stack": {"pointer": 0, "frames": []},
             "fighters": [fighter], "extra_memory": []},
            {"record": "end", "status": "diagnostic_window_complete",
             "frames": 7, "window_end_tick": 6},
        ]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "registers.jsonl"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            loaded, digest = REG.load_diagnostic(path, probes=[probe], start_tick=4, end_tick=6)
            self.assertEqual(len(loaded), 3)
            self.assertEqual(len(digest), 64)
            rows[-1]["status"] = "captured"
            path.write_text("".join(json.dumps(row) + "\n" for row in rows), encoding="utf-8")
            with self.assertRaisesRegex(REG.CpuRegisterError, "bounded end"):
                REG.load_diagnostic(path, probes=[probe], start_tick=4, end_tick=6)

    def test_diagnostic_loader_rejects_missing_raw_register_or_fighter_state(self):
        probe = REG.Probe("point", 0x80000000, 0)
        registers = {"gpr": {"r%d" % index: 0 for index in range(32)},
                     "fpr": {"f%d" % index: {"bytes_hex": "0000000000000000", "length": 8,
                                                "type": "float"} for index in range(7)},
                     "pc": 0, "lr": 0, "ctr": 0, "cr": 0}
        header = {"record": "header", "schema": REG.SCHEMA, "version": REG.VERSION,
                  "status": "diagnostic_only", "window": {"start_tick": 0, "end_tick": 0},
                  "probes": [probe.record()], "extra_memory": [],
                  "helper_sha256": "a" * 64, "input_plan_sha256": "b" * 64}
        base = {"record": "probe", "sequence": 0, "tick": 0,
                "probe": probe.record(), "registers": registers,
                "stack": {"pointer": 0, "frames": []}, "fighters": []}
        end = {"record": "end", "status": "diagnostic_window_complete",
               "frames": 1, "window_end_tick": 0}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "registers.jsonl"
            for field in (("gpr", "r0"), ("fpr", "f0")):
                broken = json.loads(json.dumps(base))
                broken["registers"][field[0]].pop(field[1])
                path.write_text("".join(json.dumps(row) + "\n" for row in (header, broken, end)),
                                encoding="utf-8")
                with self.subTest(field=field):
                    with self.assertRaisesRegex(REG.CpuRegisterError, "raw"):
                        REG.load_diagnostic(path, probes=[probe], start_tick=0, end_tick=0)
            path.write_text("".join(json.dumps(row) + "\n" for row in (header, base, end)),
                            encoding="utf-8")
            with self.assertRaisesRegex(REG.CpuRegisterError, "fighter state"):
                REG.load_diagnostic(path, probes=[probe], start_tick=0, end_tick=0)

    def test_explicit_read_errors_are_never_accepted_as_complete(self):
        rows = [{"record": "header"}, {"record": "probe_error", "error": "FPR unavailable"}]
        self.assertTrue(REG.diagnostic_errors(rows))


if __name__ == "__main__":
    unittest.main()
