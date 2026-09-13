"""Focused tests for the read-only PADRead publication boundary."""

import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock
from types import SimpleNamespace


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from retail_input_plan import DISCONNECTED_PAD, SCHEMA, POLICY
from retail_input_bootstrap import calibration_record


def plan(count=4):
    return {
        "schema": SCHEMA,
        "version": 1,
        "policy": POLICY,
        "source_sha256": "a" * 64,
        "first_frame": 0,
        "source_stage": 32,
        "source_characters": [8, 8],
        "frames": [["00" * 11, "00" * 11] for _ in range(count)],
    }


def load_collector(name):
    class Command:
        def __init__(self, *args, **kwargs):
            pass

    fake = SimpleNamespace(Command=Command, Breakpoint=Command, COMMAND_USER=0)
    spec = importlib.util.spec_from_file_location(
        name, ROOT / "tools" / "reference_replay_capture.py")
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, {"gdb": fake}):
        spec.loader.exec_module(module)
    return module


class PadPublicationTests(unittest.TestCase):
    def setup_collector(self, name="pad_publication"):
        collector = load_collector(name)
        collector.active = collector.ready = True
        collector.input_plan = plan()
        collector.LIMIT = len(collector.input_plan["frames"])
        collector.pad_bootstrapped = True
        collector.pad_sample_index = 0
        collector.machine_context = lambda: []
        collector.supply_input = mock.Mock()
        return collector

    def configure_pad_read(self, collector, raw, queue=b"\0" * 12,
                           caller=0x80376A28, stack=0x80002000,
                           status_end=0x80003030, scene=17):
        import struct

        addresses = {
            stack + 0x54: caller,
            0x80479D58: scene,
        }

        def parse_and_eval(expression):
            return {"$r1": stack, "$r31": status_end}[expression]

        def memory(address, size):
            if address == status_end - 0x30:
                return raw
            if address == 0x804C1F78:
                return queue
            if address in addresses:
                return struct.pack(">I", addresses[address])
            return b"\0" * size

        collector.memory = memory
        collector.word = lambda address: struct.unpack(">I", memory(address, 4))[0]
        collector.gdb.parse_and_eval = parse_and_eval

    @staticmethod
    def vector(frame):
        return b"".join(bytes.fromhex(pad) + b"\0" for pad in frame +
                         [DISCONNECTED_PAD, DISCONNECTED_PAD])

    def test_pre_restore_padread_verifies_vector_and_publishes_monotonically(self):
        collector = self.setup_collector()
        self.configure_pad_read(collector, self.vector(collector.input_plan["frames"][1]))

        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 1)
        collector.supply_input.assert_called_once_with(2)

        # A repeated GDB stop for the same PADRead is a duplicate, not a new
        # hardware sample, even though the callback is entered again.
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 1)
        collector.supply_input.assert_called_once_with(2)
        self.assertEqual(collector.observations.duplicates, {"pad_read": 1})

        queue = bytearray(b"\0" * 12)
        queue[3] = 1
        self.configure_pad_read(collector, self.vector(collector.input_plan["frames"][2]),
                                queue=bytes(queue), scene=18)
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 2)
        collector.supply_input.assert_has_calls([mock.call(2), mock.call(3)])

    def test_non_hsd_padread_caller_is_ignored_without_reading_statuses(self):
        collector = self.setup_collector("pad_publication_other_caller")
        self.configure_pad_read(collector, b"bad", caller=0x8034DA14)
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 0)
        collector.supply_input.assert_not_called()

    def test_pre_restore_padread_rejects_mismatched_vector(self):
        collector = self.setup_collector("pad_publication_mismatch")
        changed = bytearray(self.vector(collector.input_plan["frames"][1]))
        changed[0] = 1
        self.configure_pad_read(collector, bytes(changed))
        with self.assertRaisesRegex(ValueError, "Input intent mismatch"):
            collector.pad_read_before_interrupt_restore()
        collector.supply_input.assert_not_called()

    def test_pre_restore_padread_ignores_prefetch_after_declared_plan(self):
        collector = self.setup_collector("pad_publication_bounds")
        collector.LIMIT = 2
        collector.pad_sample_index = 1
        self.configure_pad_read(collector, self.vector(collector.input_plan["frames"][1]))
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.pad_sample_index, 1)
        collector.supply_input.assert_not_called()

    def test_pad_consume_only_bootstraps_queue_lookahead(self):
        collector = load_collector("pad_bootstrap")
        collector.active = collector.ready = True
        collector.input_plan = plan()
        collector.LIMIT = len(collector.input_plan["frames"])
        collector.frame_index = 0
        collector.machine_context = lambda: []
        collector.supply_input = mock.Mock()
        queue = bytearray(b"\0" * 12)
        # The post-dequeue trap sees qread already advanced and qcount already
        # decremented. Registers retain the consumed slot and old qread index.
        queue[0] = 3
        queue[1] = 1
        queue[3] = 2
        queue[8:12] = (0x80001000).to_bytes(4, "big")
        raw = self.vector(collector.input_plan["frames"][0])

        def memory(address, size):
            if address == 0x804C1F78:
                return bytes(queue)
            if address == 0x80001000:
                return raw
            return b"\0" * size

        collector.memory = memory
        collector.word = lambda address: collector.frame_index
        collector.gdb.parse_and_eval = lambda expression: {
            "$r6": 0,
            "$r25": 0x80001000,
        }[expression]
        collector.pad_consume()
        expected = [raw[i:i + 11].hex() for i in range(0, 48, 12)]
        self.assertEqual(collector.pending_inputs, [expected])
        collector.pad_consume()
        self.assertEqual(collector.observations.duplicates, {"pad": 1})
        collector.frame_index = 1
        collector.pad_consume()
        collector.supply_input.assert_called_once_with(1)

    def test_pad_consume_rejects_registers_that_are_not_the_consumed_slot(self):
        collector = load_collector("pad_post_dequeue_negative")
        collector.active = collector.ready = True
        collector.input_plan = plan()
        collector.LIMIT = len(collector.input_plan["frames"])
        collector.machine_context = lambda: []
        queue = bytearray(b"\0" * 12)
        queue[0] = 3
        queue[1] = 1
        queue[3] = 0
        queue[8:12] = (0x80001000).to_bytes(4, "big")
        collector.memory = lambda address, size: (
            bytes(queue) if address == 0x804C1F78 else b"\0" * size)
        collector.word = lambda address: 0
        collector.gdb.parse_and_eval = lambda expression: {
            "$r6": 0,
            "$r25": 0x80001030,
        }[expression]
        with self.assertRaisesRegex(RuntimeError, "does not retain the consumed slot"):
            collector.pad_consume()

    def test_calibrated_bootstrap_publishes_only_at_final_construction_read(self):
        collector = self.setup_collector("pad_calibrated_bootstrap")
        collector.ready = False
        collector.BOOTSTRAP_MODE = "calibrate"
        collector.construction_pad_reads = 0
        collector.last_construction_pad_read = None
        collector.supply_input.reset_mock()
        raw = self.vector(collector.input_plan["frames"][0])
        self.configure_pad_read(collector, raw, scene=17)
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        self.assertEqual(collector.construction_pad_reads, 1)
        self.assertEqual(collector.supply_input.call_count, 0)

        runtime = {
            "dol_sha1": "b" * 40, "dolphin_binary_sha256": "c" * 64,
            "source_revision": "revision", "cpu": "JITARM64",
            "cpu_thread": False, "cheats": False, "background_input": True,
            "fixed_rtc": 1704067200, "setup_snapshot_sha256": "d" * 64,
            "dolphin_ini_canonical_sha256": "e" * 64, "gcpad_ini_sha256": "f" * 64,
            "external_save_hashes": {"SRAM.raw": "a" * 64},
        }
        calibration = calibration_record(
            plan=collector.input_plan, plan_sha256="a" * 64, provenance=runtime,
            collector_sha256="e" * 64,
            construction_pad_reads=collector.construction_pad_reads,
            last_construction_pad_read=collector.last_construction_pad_read)
        collector.BOOTSTRAP_MODE = "apply"
        collector.bootstrap_calibration = calibration
        collector.construction_pad_reads = 0
        collector.bootstrap_applied = False
        collector.observations = type(collector.observations)()
        collector.supply_input.reset_mock()
        self.configure_pad_read(collector, raw, scene=17)
        self.assertFalse(collector.pad_read_before_interrupt_restore())
        collector.supply_input.assert_called_once_with(1)
        self.assertTrue(collector.bootstrap_applied)


if __name__ == "__main__":
    unittest.main()
