import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import reference_capture_semantics as semantics  # noqa: E402


class FakeMemory:
    def __init__(self, _slices):
        self.spans = []
        self.queue_address = 0x80500000

    def __call__(self, address, size):
        if address == 0x804C1F78 and size == 0xC:
            return bytes([1]) + bytes(7) + self.queue_address.to_bytes(4, "big")
        if address == self.queue_address and size == 48:
            return b"".join(bytes([value]) * 11 + b"\0" for value in (0x10, 0x20, 0x30, 0x40))
        return bytes(size)

    def word(self, address):
        if address == 0x804C1F80:
            return 1
        if address == 0x804C1F84:
            return self.queue_address
        return 0


class ReferenceCaptureSemanticsTests(unittest.TestCase):
    def test_unknown_boundary_and_post_teardown_observation_fail_closed(self):
        session = semantics.SemanticSession()
        row = {"seq": 0, "source_tick": 0, "draw_ordinal": 0,
               "event": "boundary", "payload": {"pc": 0x8016E9C4, "boundary": "pad_poll"}}
        with self.assertRaisesRegex(semantics.SemanticError, "boundary identity"):
            session.consume(row)
        session.complete = True
        row["payload"] = {"pc": 0x8034DD8C, "boundary": "pad_poll"}
        with self.assertRaisesRegex(semantics.SemanticError, "after completed teardown"):
            session.consume(row)

    def test_scene_change_keeps_original_transport_sequence(self):
        session = semantics.SemanticSession()
        def event(seq, routing):
            return session.consume({"seq": seq, "source_tick": 0, "draw_ordinal": 0,
                "event": "source_scene", "payload": {"slices": [
                    {"address": 0x80479D30, "hex": routing}]}})
        first = event(0, "000102030405")
        steady = event(1, "000102030405")
        changed = event(2, "010203040506")
        self.assertIsNone(first["payload"]["scene_transition"]["previous_routing_hex"])
        self.assertNotIn("scene_transition", steady["payload"])
        self.assertEqual(changed["seq"], 2)
        self.assertEqual(changed["payload"]["scene_transition"]["previous_routing_hex"], "000102030405")

    def test_brief_human_disconnect_is_detected_at_original_pad_poll(self):
        session = semantics.SemanticSession()
        session.enter = {"record": "match_enter"}
        registers = [0] * 32
        registers[31] = 0x80500030
        with mock.patch.object(semantics, "SliceMemory", FakeMemory):
            with self.assertRaisesRegex(semantics.SemanticError, "Human controller.*PAD error"):
                session.consume({"seq": 1, "source_tick": 0, "draw_ordinal": 0,
                                 "event": "pad_poll", "payload": {"gprs": registers}})

    def test_slice_memory_rejects_out_of_range_and_conflicting_overlap(self):
        with self.assertRaises(semantics.SemanticError):
            semantics.SliceMemory([{"address": "0x7fffffff", "hex": "00"}])
        with self.assertRaises(semantics.SemanticError):
            semantics.SliceMemory([
                {"address": "0x80500000", "hex": "0000"},
                {"address": "0x80500001", "hex": "ff00"},
            ])

    def test_consumed_pad_is_source_input_and_cpu_snapshot_is_output(self):
        session = semantics.SemanticSession()
        session.enter = {"record": "match_enter"}
        session.initial = {"record": "match_enter_complete"}
        session.fighters = {0: 0x80510000, 1: 0x80520000}
        with mock.patch.object(semantics, "SliceMemory", FakeMemory), \
             mock.patch.object(semantics, "state_snapshot", return_value={
                 "rng": 1, "scene_frame": 0, "match_frame": 0,
                 "pad_state_hex": "00" * 822,
                 "fighters": [],
             }), \
             mock.patch.object(session, "cpu_snapshot", return_value={"decision": "cpu-generated"}):
            registers = [0] * 32
            registers[25] = 0x80500000
            consumed = session.consume({
                "seq": 1, "source_tick": 0, "draw_ordinal": 0,
                "event": "pad_consume", "payload": {"gprs": registers},
            })
            frame = session.consume({
                "seq": 2, "source_tick": 0, "draw_ordinal": 0,
                "event": "source_tick", "payload": {"gprs": [0] * 32},
            })
        self.assertEqual(consumed["payload"]["ports"], ["10" * 11, "20" * 11, "30" * 11, "40" * 11])
        self.assertEqual(frame["payload"]["retail"]["consumed_inputs"][0][0], "10" * 11)
        self.assertEqual(frame["payload"]["cpu"]["decision"], "cpu-generated")

    def test_missing_consumed_pad_is_rejected_instead_of_reusing_a_sample(self):
        session = semantics.SemanticSession()
        session.enter = {"record": "match_enter"}
        session.initial = {"record": "match_enter_complete"}
        session.fighters = {0: 0x80510000, 1: 0x80520000}
        with mock.patch.object(semantics, "SliceMemory", FakeMemory), \
             mock.patch.object(semantics, "state_snapshot", return_value={
                 "rng": 1, "scene_frame": 0, "match_frame": 0,
                 "pad_state_hex": "00" * 822, "fighters": [],
             }):
            with self.assertRaisesRegex(semantics.SemanticError, "Lost/reordered"):
                session.consume({
                    "seq": 1, "source_tick": 0, "draw_ordinal": 0,
                    "event": "source_tick", "payload": {"gprs": [0] * 32},
                })

    def test_malformed_observer_row_is_not_silently_skipped(self):
        session = semantics.SemanticSession()
        with self.assertRaises(KeyError):
            session.consume({"seq": 1, "event": "source_tick"})


if __name__ == "__main__":
    unittest.main()
