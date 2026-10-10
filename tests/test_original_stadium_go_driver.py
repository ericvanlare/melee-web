from __future__ import annotations

import copy
import importlib.util
import sys
from pathlib import Path
import unittest

HERE = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(HERE / "tools"), str(HERE / "scripts"),
                str(HERE / "reference-capture" / "dolphin")]


from authored_sd_reference_plan import canonical  # noqa: E402
import importlib.util as _importlib_util  # noqa: E402
_menu_spec = _importlib_util.spec_from_file_location("candidate_sd_original_menu_plan",
                                                       HERE / "tools" / "sd_original_menu_plan.py")
_menu = _importlib_util.module_from_spec(_menu_spec)
assert _menu_spec.loader is not None
_menu_spec.loader.exec_module(_menu)
route_pads = _menu.route_pads
sheik_transform_prefix_packet = _menu.sheik_transform_prefix_packet
stadium_go_prefix_packet = _menu.stadium_go_prefix_packet
validate_packet = _menu.validate_packet
from reference_versus_sequence_capture import raw_pad  # noqa: E402
from stadium_go_prefix import EXPECTED_SETUP_RECEIPT_SHA256  # noqa: E402
from capture_sd_reference_prefix import (drive_stadium_sss, StadiumGoPrefixReceiver,
    check_stadium_capture_bounds, STADIUM_CAPTURE_CAPS, StadiumIntentController)
from retail_input_plan import NEUTRAL_PAD, DISCONNECTED_PAD
from reference_observer_stream import SLICE_NAMES
from unittest.mock import patch
import tempfile
import time  # noqa: E402


class _Controller:
    def __init__(self):
        self.current = (None, None)
        self.actions = []

    def set_both(self, p1, p2, *, action):
        self.current = (p1, p2)
        self.actions.append((action, p1, p2))


class _Receiver:
    def __init__(self, policy):
        self.menus = {"sss": policy}
        self.stage = {"index": 0, "kind": 4, "stable_polls": 2}
        self.menu_polls = 0
        self.menu_consumed = 0
        self.consumed_samples = []
        self.target_neutral_release_sequence = None
        self.target_neutral_polls = 0
        self.confirm_sequence = None
        self.confirm_release_sequence = None
        self.ended = False


class OriginalStadiumGoDriverTests(unittest.TestCase):
    def test_stadium_packet_is_fixed_bounded_and_distinct_from_sheik(self):
        packet = stadium_go_prefix_packet()
        self.assertEqual(packet["version"], 11)
        self.assertEqual(packet["scope"], "stadium_go_prefix")
        self.assertEqual(packet["setup_receipt_sha256"], EXPECTED_SETUP_RECEIPT_SHA256)
        self.assertNotIn("authored_recipe_sha256", packet)
        self.assertEqual((packet["sss"]["target_index"], packet["sss"]["target_kind"]), (18, 3))
        self.assertEqual(sum(p["max_source_polls"] for p in packet["sss"]["pulses"]),
                         packet["sss"]["max_movement_source_polls"])
        self.assertLessEqual(packet["sss"]["max_movement_source_polls"], 60)
        self.assertEqual(validate_packet(copy.deepcopy(packet)), packet)
        allowed = route_pads(packet)
        for pulse in packet["sss"]["pulses"]:
            self.assertIn((raw_pad(x=pulse["x"], y=pulse["y"]), "00" * 11), allowed)
        self.assertIn((raw_pad(buttons=["A"]), "00" * 11), allowed)
        self.assertIn(("00" * 11, "00" * 11), allowed)

    def test_packet_mutations_are_refused_and_sheik_declaration_is_unchanged(self):
        bad_target = stadium_go_prefix_packet()
        bad_target["sss"]["target_kind"] = 32
        with self.assertRaises(ValueError):
            validate_packet(bad_target)
        bad_pulse = stadium_go_prefix_packet()
        bad_pulse["sss"]["pulses"][0]["x"] = 35
        with self.assertRaises(ValueError):
            validate_packet(bad_pulse)

        import hashlib
        self.assertEqual(hashlib.sha256(canonical(sheik_transform_prefix_packet())).hexdigest(),
                         "c0fbdad35462f1d769d79fc0e068bc7abb80cc6b39faf825502ed4c1ccbd066c")

    def test_cardinal_driver_waits_for_consumed_pulse_neutral_stability_and_confirm_release(self):
        packet = stadium_go_prefix_packet()
        receiver, controller = _Receiver(packet["sss"]), _Controller()
        sequence = 0
        pulse_polls = 0

        def wait_source(predicate, label, max_polls):
            nonlocal pulse_polls
            nonlocal sequence
            start = receiver.menu_polls
            while not predicate():
                if receiver.ended or receiver.menu_polls - start >= max_polls:
                    raise AssertionError(f"bounded synthetic wait failed: {label}")
                receiver.menu_polls += 1
                sequence += 1
                p1, p2 = controller.current
                if p1 is not None:
                    pair = [p1, p2]
                    if not any(sample["ports"][:2] == pair
                               for sample in receiver.consumed_samples[-1:]):
                        receiver.menu_consumed += 1
                        receiver.consumed_samples.append({"sequence": sequence, "ports": pair})
                if label.startswith("SSS-cardinal-") and label.endswith(":source-poll"):
                    pulse_polls += 1
                    if pulse_polls >= 2:
                        receiver.stage = {"index": 18, "kind": 3, "stable_polls": 2}
                if (receiver.target_neutral_release_sequence is not None and
                        receiver.stage["index"] == 18 and
                        sequence > receiver.target_neutral_release_sequence and
                        controller.current == ("00" * 11, "00" * 11)):
                    receiver.target_neutral_polls += 1
                if p1 == raw_pad(buttons=["A"]):
                    receiver.confirm_sequence = sequence
                elif (receiver.confirm_sequence is not None and receiver.confirm_release_sequence is None and
                      pair == ["00" * 11, "00" * 11]):
                    receiver.confirm_release_sequence = sequence

        drive_stadium_sss(receiver, packet, controller, lambda: None, wait_source)
        labels = [item[0] for item in controller.actions]
        self.assertEqual(labels[0], "SSS-Stadium-neutral-entry")
        self.assertIn("SSS-cardinal-0", labels)
        self.assertNotIn("SSS-cardinal-1", labels)
        self.assertLess(labels.index("SSS-cardinal-0:neutral"), labels.index("SSS-Stadium-confirm"))
        self.assertLess(labels.index("SSS-Stadium-confirm"), labels.index("SSS-Stadium-confirm:neutral-release"))
        self.assertEqual(receiver.stage, {"index": 18, "kind": 3, "stable_polls": 2})
        self.assertEqual(receiver.confirm_sequence, receiver.confirm_release_sequence - 1)

    def test_actual_receiver_reads_authored_sss_and_consumed_confirm_release(self):
        receiver = StadiumGoPrefixReceiver(stadium_go_prefix_packet())
        receiver.css_live_owner_sequence = 1
        def item(tag, raw, address):
            return dict(name=SLICE_NAMES[tag], tag=tag, flags=0, size=len(raw),
                        hex=raw.hex(), address=address)
        def poll(seq, index=18, kind=3):
            receiver._pad_poll(dict(seq=seq, payload=dict(slices=[
                item(40, bytes([9]), 0x80400000),
                item(17, bytes([2,0,0,0,0,0]), 0x80479D30),
                item(41, bytes([index]), 0x804D6CAE),
                item(42, bytes([kind]), 0x803F06D0 + index*0x1C + 0xB)])))
        def consume(seq, pad):
            queue = bytes([1,0,0,0,0,0,0,0]) + (0x804C3000).to_bytes(4, "big")
            ports = [pad, NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
            slot = b"".join(bytes.fromhex(p) + b"\0" for p in ports)
            gprs = [0]*32; gprs[25] = 0x804C3000
            receiver._pad_consume_stadium(dict(seq=seq, payload=dict(gprs=gprs, slices=[
                item(2, queue, 0x804C1F78), item(3, slot, 0x804C3000)])))
        poll(2, 0, 4)  # valid non-Stadium navigation remains accepted
        self.assertEqual(receiver.stage["kind"], 4)
        consume(3, NEUTRAL_PAD)
        receiver.target_neutral_release_sequence = 3
        poll(4); poll(5)
        self.assertEqual(receiver.target_neutral_polls, 2)
        consume(6, raw_pad(buttons=["A"]))
        consume(7, NEUTRAL_PAD)
        self.assertEqual((receiver.confirm_sequence, receiver.confirm_release_sequence), (6,7))
        poll(8, 0, 4)
        self.assertEqual(receiver.target_neutral_polls, 0)
        with self.assertRaises(ValueError):
            consume(9, raw_pad(x=1))  # outside declared input alphabet

    def test_stadium_stream_caps_and_continuous_record_deadline(self):
        with tempfile.TemporaryDirectory() as scratch:
            raw, native = Path(scratch)/"mwro", Path(scratch)/"mwri"
            deadline = time.monotonic()+60
            check_stadium_capture_bounds(raw, native, deadline, 0)
            with self.assertRaises(ValueError):
                check_stadium_capture_bounds(raw, native, time.monotonic()-1, 0)
            with self.assertRaises(ValueError):
                check_stadium_capture_bounds(raw, native, deadline, STADIUM_CAPTURE_CAPS["observer_records"]+1)
            for path,key in ((raw,"observer_bytes"),(native,"input_bytes")):
                with path.open("wb") as f: f.truncate(STADIUM_CAPTURE_CAPS[key]+1)
                with self.assertRaises(ValueError):
                    check_stadium_capture_bounds(raw, native, deadline, 0)
                path.unlink()

    def test_stadium_intention_caps_refuse_before_pipe_write(self):
        controller = object.__new__(StadiumIntentController)
        with tempfile.TemporaryDirectory() as scratch:
            controller.log = Path(scratch)/"intentions"
            controller.intent_records = STADIUM_CAPTURE_CAPS["intent_records"]
            with patch("reference_versus_sequence_capture.DualPipeController.write") as write:
                with self.assertRaises(ValueError):
                    controller.write(0, NEUTRAL_PAD, action="neutral")
                write.assert_not_called()
                controller.intent_records = 0
                with controller.log.open("wb") as f: f.truncate(STADIUM_CAPTURE_CAPS["intent_bytes"])
                with self.assertRaises(ValueError):
                    controller.write(0, NEUTRAL_PAD, action="neutral")
                write.assert_not_called()


if __name__ == "__main__":
    unittest.main()
