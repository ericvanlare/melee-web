#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "reference-capture/slippi"))
from transport_fault_recipes import validate_transport


def base(event, **fields):
    return {"event": event, "observer_context": "transport", "game_sequence": 0, **fields}


class TransportRecipeTests(unittest.TestCase):
    def test_jitter_requires_native_receiver_order_and_join(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99,
                 held_frames=[98], held_payload_hashes=["0123456789abcdef"],
                 dispatch_payload_hashes=[], release_payload_hash="fedcba9876543210"),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=1),
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
        ]
        report = validate_transport(sender, receiver, scenario="jitter")
        self.assertEqual(report["delivery_frames"], [98, 99])

    def test_reorder_requires_late_nonpositive_receiver_copy(self):
        sender = [
            base("pad_transport", action="reorder_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="reorder_dispatch", dispatch_frame=99,
                 release_payload_hash="fedcba9876543210",
                 history_count=1, history_first_frame=99, history_last_frame=99,
                 dispatch_payload_hashes=["fedcba9876543210", "0123456789abcdef"]),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=-1),
        ]
        report = validate_transport(sender, receiver, scenario="reorder")
        self.assertTrue(report["redundant_history_observed"])

    def test_swapped_receiver_hash_sequence_fails(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99,
                 held_frames=[98], held_payload_hashes=["0123456789abcdef"],
                 dispatch_payload_hashes=[], release_payload_hash="fedcba9876543210"),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=1),
        ]
        with self.assertRaises(ValueError):
            validate_transport(sender, receiver, scenario="jitter")

    def test_boolean_inputs_to_copy_fails(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99,
                 held_frames=[98], held_payload_hashes=["0123456789abcdef"],
                 dispatch_payload_hashes=[], release_payload_hash="fedcba9876543210"),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=True),
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
        ]
        with self.assertRaises(ValueError):
            validate_transport(sender, receiver, scenario="jitter")

    def test_sender_only_or_wrong_order_fails(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99,
                 held_frames=[98], held_payload_hashes=["0123456789abcdef"],
                 dispatch_payload_hashes=[], release_payload_hash="fedcba9876543210"),
        ]
        with self.assertRaises(ValueError):
            validate_transport(sender, [], scenario="jitter")

    def test_jitter_rejects_missing_or_nonempty_dispatch_fields(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99, held_frames=[98],
                 held_payload_hashes=["0123456789abcdef"], dispatch_payload_hashes=[],
                 release_payload_hash="fedcba9876543210"),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=1),
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
        ]
        for changed in (
            dict(sender[0], held_count=True),
            dict(sender[0], packet_frame=True),
            dict(sender[1], dispatch_payload_hashes=["0123456789abcdef"]),
            dict(sender[1], dispatch_frame=98),
            dict(sender[1], release_count=True),
            dict(sender[1], history_count=True),
        ):
            with self.subTest(changed=changed):
                with self.assertRaises(ValueError):
                    validate_transport([sender[0], changed], receiver, scenario="jitter")

    def test_reorder_requires_release_hash_as_first_dispatch_identity(self):
        sender = [
            base("pad_transport", action="reorder_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="reorder_dispatch", dispatch_frame=99,
                 release_payload_hash="fedcba9876543210",
                 history_count=1, history_first_frame=99, history_last_frame=99,
                 dispatch_payload_hashes=["fedcba9876543210", "0123456789abcdef"]),
        ]
        receiver = [
            base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                 receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
            base("pad_transport_receive", packet_frame=98, packet_player_port=1,
                 receiver_port=1, payload_hash="0123456789abcdef", inputs_to_copy=-1),
        ]
        self.assertTrue(validate_transport(sender, receiver, scenario="reorder")
                        ["redundant_history_observed"])
        with self.assertRaises(ValueError):
            validate_transport([sender[0], dict(sender[1], release_payload_hash="0123456789abcdef")],
                               receiver, scenario="reorder")

    def test_wire_port_domain_rejects_bool_and_out_of_range(self):
        sender = [
            base("pad_transport", action="jitter_hold", packet_frame=98,
                 payload_hash="0123456789abcdef", held_count=1, history_count=1,
                 history_first_frame=98, history_last_frame=98),
            base("pad_transport", action="jitter_release", release_packet_frame=99,
                 dispatch_frame=99, release_count=1, history_count=1,
                 history_first_frame=99, history_last_frame=99,
                 held_frames=[98], held_payload_hashes=["0123456789abcdef"],
                 dispatch_payload_hashes=[], release_payload_hash="fedcba9876543210"),
        ]
        for bad_port in (True, 4):
            receiver = [
                base("pad_transport_receive", packet_frame=98,
                     packet_player_port=bad_port, receiver_port=1,
                     payload_hash="0123456789abcdef", inputs_to_copy=1),
                base("pad_transport_receive", packet_frame=99, packet_player_port=1,
                     receiver_port=1, payload_hash="fedcba9876543210", inputs_to_copy=1),
            ]
            with self.assertRaises(ValueError):
                validate_transport(sender, receiver, scenario="jitter")


if __name__ == "__main__":
    unittest.main()
