"""Small source/layout controls for the declared original P1/P3 probe."""
from copy import deepcopy
from pathlib import Path
import sys
import json
import struct
import shutil
import subprocess
from unittest.mock import patch
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
                                     RulesMenuReceiver, GciRulesMenuReceiver, css_state, profile_rumble_copy, stage_state)
from capture_sd_reference_prefix import require_css_join_owner
from reference_versus_sequence_capture import raw_pad
from owned_test_workspace import OwnedWorkspaceTests


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

    def test_vacant_cursor_initializer_is_zero_for_default_second_and_sparse_third(self):
        for slots in ((0, 1), (0, 2)):
            slot = slots[1]
            css = {"players": [{}, {"character": 26, "kind": 3, "slot": 0}],
                   "doors": [{}, {"kind": 3, "costume": 0, "icon": 25}],
                   "cursors": [{}, {"port": slot, "state": 0, "held": 0,
                                     "x": 15.0 * slot - 31.0, "y": -21.5}],
                   "models": [{}, {"owner": 0}]}
            if slots == (0, 2):
                css["source_slots"] = list(slots)
            require_css_join_owner(css, 1, initial=True)
            css["cursors"][1]["held"] = slot
            with self.assertRaisesRegex(ValueError, "initialized owner"):
                require_css_join_owner(css, 1, initial=True)

    def test_default_css_row_shape_unchanged_and_sparse_metadata_explicit(self):
        for slots in ((0, 1), (0, 2)):
            live, doors = bytearray(0x148), bytearray(0x90)
            data = {(48, 0): live, (44, 0): doors}
            for slot in range(4):
                live[0x70 + slot*0x24 + 1] = 0 if slot in slots else 3
            for slot in slots:
                cursor = bytearray(0x14); cursor[4] = slot
                cursor[12:20] = struct.pack(">ff", 15.0*slot-31.0, -21.5)
                data[(43, slot)] = cursor; data[(47, slot)] = bytes(0x18)
            css = css_state(data, slots)
            self.assertEqual(set(css["cursors"][1]),
                             {"port", "state", "held", "x", "y"} |
                             ({"source_slot"} if slots == (0, 2) else set()))
            self.assertEqual(set(css["players"][1]), {"character", "kind", "slot"} |
                             ({"source_slot"} if slots == (0, 2) else set()))

    def test_actual_loaded_profile_precedes_target_menu_settings(self):
        from sd_original_menu_plan import SPARSE_LOADED_RULES_HEX
        from sd_gci_profile import SAVE_BYTES, BANK_BYTES
        fixture = json.loads((ROOT / "tests/fixtures/original-sparse-initial-profile.json").read_text())
        self.assertEqual(fixture["game_rules_hex"], SPARSE_LOADED_RULES_HEX)
        rules = bytes.fromhex(fixture["game_rules_hex"])
        self.assertEqual((rules[2], rules[4], rules[8]), (0, 3, 0))
        self.assertEqual(bytes.fromhex(fixture["save_items_hex"])[0], 2)
        packet = gci_sparse_pair_packet()
        labels = [a["label"] for a in packet["actions"]]
        self.assertEqual(labels[6:9], ["stock-mode", "stock-row", "four-stocks"])
        self.assertNotIn("one-minute-stock-timer", labels)
        self.assertEqual(labels[-5:], ["items-frequency-2", "items-frequency-1",
                         "items-frequency-0", "commit-items-none", "Rules-start-CSS"])
        self.assertEqual(next(a for a in packet["actions"] if a["label"] == "open-items")["before"],
                         {"scene": 1, "kind": 13, "row": 5, "cooldown": 0, "entering": 1})
        # Synthetic surrounding ABI extents; Rules/Items bytes are retained actual.
        save = bytearray(0x55e8); save[:4] = bytes.fromhex("07ff07ff")
        save[0x448:0x468] = bytes.fromhex(fixture["save_items_hex"])
        receiver = GciRulesMenuReceiver.__new__(GciRulesMenuReceiver)
        receiver.sparse_pair = True; receiver.competitive_entry = False
        receiver.plan = make_input_plan(7)
        receiver.profile = {"save": bytes(save[:SAVE_BYTES]), "banks": [bytes(BANK_BYTES)]*2}
        root = 0x80400000
        values = [(39, root+0x1868, bytes(save)), (38, root+0x1850, rules),
                  (36, root+0x1868, bytes.fromhex("07ff")),
                  (37, root+0x186a, bytes.fromhex("07ff")), (54, root+0x1cc0, b"\0")]
        row = progress(563, 1, "rules_ready", 0, 39, bytes(save))
        row["payload"]["slices"] = [{"tag": tag, "flags": 0, "address": address,
                                     "hex": raw.hex()} for tag,address,raw in values]
        with patch.object(RulesMenuReceiver, "accept", return_value=None):
            receiver.accept(row)
            self.assertEqual(receiver.loaded_context["game_rules_hex"], rules.hex())
            wrong = deepcopy(row)
            wrong["payload"]["slices"][1]["hex"] = (rules[:2]+b"\1"+rules[3:]).hex()
            with self.assertRaisesRegex(SdDiagnosticError, "initial loaded GameRules"):
                receiver.accept(wrong)

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
        self.assertTrue(receiver.items_guard and receiver.guarded_items)
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


class OriginalSparsePadPredicateTests(OwnedWorkspaceTests):
    @classmethod
    def setUpClass(cls):
        cls.workspace = cls.new_workspace(ROOT, "original-sparse-pad-predicate-")

    def test_actual_neutral_padding_and_all_semantic_byte_negatives(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        if compiler is None:
            self.skipTest("A native C++ compiler is not installed")
        source = (ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp").read_text()
        predicate = source[source.index("bool SparsePadStatusMatches("):
                           source.index("bool ActivationRequested()")]
        # Actual input_rejected seq1716 from observer.bin SHA256
        # 5c1afd4e9628a01ecc08d34473c7f781f2d965807db0055e757654ae0128421b.
        # All 48 observed bytes remain intact, including trailing ABI padding.
        captured = bytes.fromhex(
            "00000000000000000000009c00000000000000000000ff98"
            "0000000000000000000000c800000000000000000000ff48")
        self.assertEqual(len(captured), 48)
        values = ",".join(str(b) for b in captured)
        harness = """#include <array>
#include <cassert>
#include <cstdint>
using u8 = uint8_t;
using u32 = uint32_t;
""" + predicate + """
int main() {
  const std::array<u8, 48> actual = {""" + values + """};
  auto neutral = actual;
  assert(SparsePadStatusMatches(neutral.data(), false));
  assert(!SparsePadStatusMatches(neutral.data(), true));
  assert(neutral == actual);
  auto pressed = actual; // Synthetic declared press atop actual trailing bytes.
  pressed[0] = 1; pressed[2] = 35; pressed[24] = 2; pressed[27] = 221;
  assert(SparsePadStatusMatches(pressed.data(), true));
  assert(!SparsePadStatusMatches(pressed.data(), false));
  for (u32 port = 0; port < 4; ++port) {
    for (u32 byte = 0; byte < 11; ++byte) {
      auto bad_neutral = actual;
      bad_neutral[12 * port + byte] ^= 1;
      assert(!SparsePadStatusMatches(bad_neutral.data(), false));
      auto bad_press = pressed;
      bad_press[12 * port + byte] ^= 1;
      assert(!SparsePadStatusMatches(bad_press.data(), true));
    }
    auto other_padding = actual;
    other_padding[12 * port + 11] ^= 255;
    assert(SparsePadStatusMatches(other_padding.data(), false));
  }
  assert(actual == neutral); // The actual production predicate is read-only.
}
"""
        path = self.workspace
        (path / "predicate.cpp").write_text(harness)
        built = subprocess.run([compiler, "-std=c++17", "-Wall", "-Werror",
                                str(path / "predicate.cpp"), "-o", str(path / "predicate")],
                               capture_output=True, text=True)
        (path / "compile.stdout-stderr.log").write_text(built.stdout + built.stderr)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        checked = subprocess.run([str(path / "predicate")], capture_output=True, text=True)
        (path / "run.stdout-stderr.log").write_text(checked.stdout + checked.stderr)
        self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)


if __name__ == "__main__":
    unittest.main()
