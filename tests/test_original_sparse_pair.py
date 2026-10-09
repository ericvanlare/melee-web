"""Small source/layout controls for the declared original P1/P3 probe."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "scripts"))

from authored_sd_reference_plan import make_input_plan, recipe
from original_source_ports import (DEFAULT_SOURCE_SLOTS, SPARSE_SOURCE_SLOTS,
                                   expand_lanes, inactive_source_slots,
                                   lane_source_slot, project_sources, signed_pad_errors)
from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD, verify_entry
from sd_original_menu_plan import gci_sparse_pair_packet, validate_packet
from sd_reference_diagnostic import (PCS, Receiver, SCOPE, SdDiagnosticError,
                                     RulesMenuReceiver, profile_rumble_copy, stage_state)
from reference_versus_sequence_capture import raw_pad


def sparse_setup_bytes():
    raw = bytearray(0x138)
    raw[0] = 0x20  # Ordinary stock VS; timer disabled.
    raw[2] = 0x80  # Four-stock setup, pausing enabled.
    raw[4] = 0x40  # Ordinary VS.
    raw[0x0e:0x10] = (32).to_bytes(2, "big")
    raw[0x0b] = 0xff
    raw[0x20:0x28] = b"\xff" * 8
    raw[0x2c:0x30] = bytes.fromhex("3f800000")
    raw[0x30:0x34] = bytes.fromhex("3f800000")
    raw[0x34:0x38] = bytes.fromhex("3f800000")
    for index in range(6):
        base = 0x60 + index * 0x24
        if index in SPARSE_SOURCE_SLOTS:
            costume = 1 if index == 0 else 0
            raw[base:base + 5] = bytes((8, 0, 4, costume, 0))
            raw[base + 12] = 0x80  # Rumble follows the unchanged original profile.
        else:
            raw[base + 1] = 3  # Original NA; sparse sources stay at records 0 and 2.
    return raw


def status_vector(pads):
    return b"".join(bytes.fromhex(pad).ljust(12, b"\0") for pad in pads)


def progress(seq, tick, name, consumed, tag, raw, *, address=0x80001000,
             extra_slices=()):
    slices = [{"tag": tag, "flags": 0, "address": address, "hex": raw.hex()}]
    slices.extend(extra_slices)
    return {
        "seq": seq,
        "source_tick": tick,
        "event": "progress",
        "payload": {
            "diagnostic": SCOPE,
            "name": name,
            "consumed": consumed,
            "menu_consumed": 0,
            "pc": PCS[name],
            "slices": slices,
        },
    }


def sparse_input_progress(seq, tick, consumed, vector, queue_slot_index):
    queue = bytearray(0x0c)
    queue[0] = 2
    queue_base = 0x80001000
    queue[8:12] = queue_base.to_bytes(4, "big")
    return progress(seq, tick, "input", consumed, 3, status_vector(vector),
                    address=queue_base + queue_slot_index * 0x30,
                    extra_slices=({"tag": 2, "flags": 0, "address": 0x804c1f78,
                                  "hex": queue.hex()},))


class OriginalSparsePairTests(unittest.TestCase):
    def test_lane_mapping_keeps_default_pair_and_maps_sparse_pair_without_compaction(self):
        self.assertEqual([lane_source_slot(i, DEFAULT_SOURCE_SLOTS) for i in range(2)], [0, 1])
        self.assertEqual([lane_source_slot(i, SPARSE_SOURCE_SLOTS) for i in range(2)], [0, 2])
        self.assertEqual(expand_lanes(["p1", "p3"], SPARSE_SOURCE_SLOTS,
                                      inactive="NA"), ["p1", "NA", "p3", "NA"])
        self.assertEqual(project_sources(["p1", "NA", "p3", "NA"], SPARSE_SOURCE_SLOTS),
                         ["p1", "p3"])
        self.assertEqual(inactive_source_slots(SPARSE_SOURCE_SLOTS), (1, 3))

    def test_default_0_1_rumble_derivation_preserves_the_accepted_branch(self):
        persistent = bytearray(0x138)
        for slot in range(6):
            base = 0x60 + slot * 0x24
            persistent[base + 1] = 0 if slot < 2 else 3
            if slot < 2:
                persistent[base + 0x0a] = 120
                persistent[base + 0x0c] = 0x80
        preferences = bytes((1, 0, 1, 0))
        expected = bytearray(persistent)
        for slot in range(6):
            base = 0x60 + slot * 0x24
            expected[base + 0x0c] &= ~0x80
        expected[0x60 + 0x0c] |= 0x80
        self.assertEqual(profile_rumble_copy(persistent, preferences), expected)

    def test_frozen_recipe_and_normalized_setup_keep_original_source_indices(self):
        plan = make_input_plan(7)
        declaration = recipe(7)
        self.assertEqual(declaration["source_slots"], [0, 2])
        self.assertEqual(declaration["expected_game_rules"], {
            "mode": 1, "time_limit": 2, "stock_count": 4, "handicap": 0,
            "damage_ratio": 10, "stock_time_limit": 0, "friendly_fire": 0, "pause": 1,
        })
        self.assertEqual(plan["controlled_ports"], [1, 3])
        self.assertEqual(len(plan["frames"]), 2)
        self.assertNotEqual(plan["frames"][0][0], plan["frames"][0][2])
        self.assertEqual(plan["frames"][0][1], DISCONNECTED_PAD)
        self.assertEqual(plan["frames"][0][3], DISCONNECTED_PAD)
        self.assertEqual(plan["frames"][1], declaration["input_witness"]["release"])
        verify_entry(plan, sparse_setup_bytes().hex())

    def test_sparse_setup_rejects_an_unexpected_active_source_record(self):
        plan = make_input_plan(7)
        raw = sparse_setup_bytes()
        raw[0x61 + 0x24] = 0  # Original P2 unexpectedly joined.
        with self.assertRaisesRegex(ValueError, "undeclared sparse source slot"):
            verify_entry(plan, raw.hex())

    def test_sparse_menu_packet_is_the_exact_declared_original_route(self):
        packet = gci_sparse_pair_packet()
        self.assertEqual(validate_packet(deepcopy(packet)), packet)
        self.assertEqual(packet["version"], 9)
        self.assertEqual(packet["css"]["source_slots"], [0, 2])
        self.assertEqual(packet["css"]["ports"], [0, 1])  # Logical Pipe lanes.

    def test_consumed_press_and_release_keep_full_queue_identity_without_poll_join(self):
        plan = make_input_plan(7)
        receiver = Receiver(plan, sparse_pair=True)
        receiver.started = True
        receiver.order = len(receiver.phase_order)

        receiver.accept(sparse_input_progress(0, 43, 1, plan["frames"][0], 0))
        receiver.accept(sparse_input_progress(1, 51, 2, plan["frames"][1], 1))

        self.assertEqual(receiver.witness_phase, 2)
        self.assertEqual(receiver.sparse_source_samples, 2)
        self.assertEqual([record["kind"] for record in receiver.witness_records],
                         ["distinct_press", "verified_release"])
        self.assertEqual(receiver.witness_records[0]["pads"], plan["frames"][0])
        self.assertEqual(receiver.witness_records[1]["pads"], plan["frames"][1])
        self.assertEqual([record["source_consumed"] for record in receiver.witness_records], [1, 2])
        self.assertEqual([record["queue_slot_index"] for record in receiver.witness_records], [0, 1])
        self.assertEqual(signed_pad_errors(status_vector(plan["frames"][0])), [0, -1, 0, -1])

    def test_consumed_slot_outside_observed_queue_is_rejected(self):
        plan = make_input_plan(7)
        receiver = Receiver(plan, sparse_pair=True)
        receiver.started = True
        receiver.order = len(receiver.phase_order)
        with self.assertRaisesRegex(SdDiagnosticError, "queue bounds"):
            receiver.accept(sparse_input_progress(0, 43, 1, plan["frames"][0], 2))

    def test_sparse_menu_uses_strict_confirmation_countdown_and_retirement(self):
        plan = make_input_plan(7)
        receiver = RulesMenuReceiver(plan, profile_campaign=True, full_route=True,
                                     sparse_pair=True)
        receiver.started = True
        receiver.order = 0
        receiver.ready = True
        neutral = [NEUTRAL_PAD, DISCONNECTED_PAD, NEUTRAL_PAD, DISCONNECTED_PAD]
        press = [raw_pad(buttons=["A"]), DISCONNECTED_PAD,
                 NEUTRAL_PAD, DISCONNECTED_PAD]

        def menu_input_row(seq, tick, count, pads):
            row = progress(seq, tick, "menu_input", 0, 3, status_vector(pads))
            row["payload"]["menu_consumed"] = count
            return row

        def stage_row(seq, tick, cooldown, route):
            values = ((40, 0x804a04f0, b"\x09"),
                      (17, 0x80479d30, bytes(route)),
                      (55, 0x804d6ca4, cooldown.to_bytes(4, "big")),
                      (41, 0x804d6cae, b"\x00"),
                      (42, 0x803f06db, b"\x20"))
            slices = [{"tag": tag, "flags": 0, "address": address, "hex": raw.hex()}
                      for tag, address, raw in values]
            return {"seq": seq, "source_tick": tick, "event": "progress",
                    "payload": {"diagnostic": SCOPE, "name": "menu", "consumed": 0,
                                "menu_consumed": receiver.menu_consumed, "pc": PCS["menu"],
                                "slices": slices}}

        stage_payload = {"slices": [
            {"tag": 55, "flags": 0, "address": 0x804d6ca4, "hex": (21).to_bytes(4, "big").hex()},
            {"tag": 41, "flags": 0, "address": 0x804d6cae, "hex": "00"},
            {"tag": 42, "flags": 0, "address": 0x803f06db, "hex": "20"},
        ]}
        with self.assertRaisesRegex(SdDiagnosticError, "constructor cooldown"):
            stage_state({(55, 0): (21).to_bytes(4, "big"), (41, 0): b"\x00",
                         (42, 0): b"\x20"}, stage_payload)

        active_route = (2, 2, 1, 1, 0, 0)
        receiver.accept(stage_row(receiver.seq, 4, 0, active_route))
        for tick, pads in ((5, neutral), (6, press)):
            receiver.accept(menu_input_row(receiver.seq, tick, receiver.menu_consumed + 1, pads))
        self.assertTrue(receiver.sss_guard)
        self.assertEqual(receiver.sss_confirmation["source_tick"], 6)

        receiver.accept(stage_row(receiver.seq, 7, 30, active_route))
        for tick, pads in ((7, press), (8, neutral)):
            receiver.accept(menu_input_row(receiver.seq, tick, receiver.menu_consumed + 1, pads))
        self.assertTrue(receiver.sss_confirmation_neutral)
        for cooldown in range(29, -1, -1):
            receiver.accept(stage_row(receiver.seq, 8 + (29 - cooldown), cooldown, active_route))
        retirement = (2, 2, 1, 2, 1, 0)
        receiver.accept(stage_row(receiver.seq, 38, 0, retirement))
        receiver.accept(stage_row(receiver.seq, 38, 0, retirement))
        self.assertEqual(receiver.sss_countdown, 0)
        self.assertEqual(len(receiver.sss_retirement_inventory), 2)
        self.assertEqual(receiver.sss_retirement_inventory[0]["stage"]["kind"], 32)


if __name__ == "__main__":
    unittest.main()
