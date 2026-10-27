import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "compare_stadium_long_b_rng.py"
SPEC = importlib.util.spec_from_file_location("long_b_rng_compare", SCRIPT)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


def original_fighter_record(*, seq=10, ordinal=0, seed="00000001", slot=0):
    return {
        "rng_ordinal": ordinal,
        "observer_next_sequence": seq,
        "source_tick": 85,
        "draw_ordinal": 0,
        "match": 1,
        "primitive": "HSD_Rand",
        "seed_after": seed,
        "owner": {
            "status": "scheduled_proc",
            "callback_symbol": "Fighter_8006A360",
            "gobj": "0x80001000",
            "proc": "0x80001004",
            "callback": "0x8006a360",
            "classifier": 4,
            "p_link": 8,
            "s_link": 1,
            "source_slot": slot,
            "entity": 0,
            "fighter_kind": 8,
        },
    }


def browser_fighter_draw(*, seed="00000001", slot=0):
    return {
        "kind": "HSD_Rand",
        "seed_after": seed,
        "owner": {
            "status": "scheduled_proc",
            "callback_symbol": "Fighter_8006A360",
            "gobj": "0000000010000000",
            "proc": "0000000010000004",
            "callback": "0000000000000080",
            "classifier": 4,
            "p_link": 8,
            "s_link": 1,
            "fighter": {"slot": slot, "entity": 0, "kind": 8},
        },
    }


class SequenceAssignmentTests(unittest.TestCase):
    def test_go_partial_tick_is_guard_and_exact_sequence_interval_is_selected(self):
        records = [
            {"rng_ordinal": 0, "observer_next_sequence": 12, "source_tick": 84,
             "draw_ordinal": 1},  # actual GO-containing partial tick
            {"rng_ordinal": 1, "observer_next_sequence": 13, "source_tick": 85,
             "draw_ordinal": 0},
            {"rng_ordinal": 2, "observer_next_sequence": 14, "source_tick": 85,
             "draw_ordinal": 0},  # after selected source target: guard
        ]
        selected = compare._select_action_ordinals(
            records, 1, 3, action_index=169, source_tick=85, draw_ordinal=0,
            after_sequence=11, through_sequence=14,
            containing_sequence=12, target_sequence=13)
        self.assertEqual(selected, [1])

    def test_out_of_action_sequence_refuses(self):
        records = [{"rng_ordinal": 0, "observer_next_sequence": 9,
                    "source_tick": 85, "draw_ordinal": 0}]
        with self.assertRaises(compare.EvidenceError) as caught:
            compare._select_action_ordinals(
                records, 0, 1, action_index=169, source_tick=85, draw_ordinal=0,
                after_sequence=10, through_sequence=14,
                containing_sequence=12, target_sequence=13)
        self.assertEqual(caught.exception.first_mismatch["kind"], "original_action_assignment")

    def test_wrong_source_coordinate_refuses_even_when_seed_matches(self):
        records = [{"rng_ordinal": 0, "observer_next_sequence": 13,
                    "source_tick": 86, "draw_ordinal": 0}]
        with self.assertRaises(compare.EvidenceError):
            compare._select_action_ordinals(
                records, 0, 1, action_index=169, source_tick=85, draw_ordinal=0,
                after_sequence=10, through_sequence=14,
                containing_sequence=12, target_sequence=13)


class ProjectionBindingTests(unittest.TestCase):
    def test_projection_compact_summary_matches_decoder_shape_without_path(self):
        summary = {
            "valid": True,
            "output_sha256": "a" * 64,
            "output_bytes": 24,
            "record_count": 3,
            "header": {"capture_id": "capture", "sequence_id": "sequence"},
            "windows": [],
            "unresolved_owner_events": 0,
            "owner_comparison_ready": True,
            "source_schedule_verified": True,
            "all_rng_writers_observed": False,
            "lifecycle_parity": False,
            "C3": False,
        }
        compare._verify_compact_projection_summary(summary, dict(summary), 3)

    def test_projection_compact_summary_rejects_unbound_extra_path(self):
        summary = {"valid": True, "record_count": 3}
        with self.assertRaises(compare.EvidenceError) as caught:
            compare._verify_compact_projection_summary(
                {**summary, "output_path": "/unbound/path"}, summary, 3)
        self.assertIn("strict compact decode", str(caught.exception))


class SemanticComparisonTests(unittest.TestCase):
    def test_browser_seed_chain_requires_authored_lcg_successor(self):
        seed = compare._check_seed_transition(None, "00000001", action_index=168, event_index=0)
        expected = (214013 * seed + 2531011) & 0xffffffff
        self.assertEqual(compare._check_seed_transition(
            seed, f"{expected:08x}", action_index=168, event_index=1), expected)
        with self.assertRaises(compare.EvidenceError) as caught:
            compare._check_seed_transition(seed, "00000002", action_index=168, event_index=1)
        self.assertEqual(caught.exception.first_mismatch["kind"], "browser_seed_discontinuity")

    def test_equal_semantics_do_not_compare_cross_runtime_pointers(self):
        original_record = original_fighter_record()
        original = compare._original_event(original_record, owner_tools=None)
        browser = compare._native_draw(browser_fighter_draw(), 169, 0, owner_tools=None)
        self.assertNotEqual(original["local_pointers"], browser["local_pointers"])
        self.assertEqual(original["owner"], browser["owner"])
        compared = compare._compare_action([original], [browser], 169, [original_record])
        self.assertEqual(len(compared), 1)
        self.assertEqual(compared[0]["original_rng_ordinal"], 0)

    def test_changed_owner_or_event_order_fails_despite_matching_seeds(self):
        left_record = original_fighter_record(ordinal=0, seed="00000001", slot=0)
        right_record = original_fighter_record(ordinal=1, seed="00000002", slot=1)
        left = compare._original_event(left_record, owner_tools=None)
        right = compare._original_event(right_record, owner_tools=None)
        native_right = compare._native_draw(browser_fighter_draw(seed="00000002", slot=1), 169, 0, None)
        native_left = compare._native_draw(browser_fighter_draw(seed="00000001", slot=0), 169, 1, None)
        with self.assertRaises(compare.EvidenceError) as caught:
            compare._compare_action([left, right], [native_right, native_left], 169,
                                    [left_record, right_record])
        self.assertEqual(caught.exception.first_mismatch["kind"], "event_value")

    def test_event_count_and_fighter_identity_are_strict(self):
        record = original_fighter_record()
        original = compare._original_event(record, None)
        with self.assertRaises(compare.EvidenceError) as count:
            compare._compare_action([original], [], 169, [record])
        self.assertEqual(count.exception.first_mismatch["kind"], "event_count")
        wrong_port = compare._native_draw(browser_fighter_draw(slot=1), 169, 0, None)
        with self.assertRaises(compare.EvidenceError) as owner:
            compare._compare_action([original], [wrong_port], 169, [record])
        self.assertEqual(owner.exception.first_mismatch["field"], "owner")


if __name__ == "__main__":
    unittest.main()
