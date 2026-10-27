"""Synthetic ownership negatives; retained original data has a separate opt-in check."""
import copy
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("paired_rng", ROOT / "tools/compare_stadium_paired_v11_rng.py")
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


def fixture():
    matches, rows, probes = [], [], []
    for mi in range(2):
        action = {"kind": "tick", "action_index": 0, "source_tick": 0,
                  "draw_ordinal": 0, "frame_index": 10 + mi}
        matches.append({"match_index": mi, "actions": [action], "selected": {0: []},
                        "selected_event_count": 0, "unpaired_original_guard_events": 0})
        for kind in ("action_begin", "action_end"):
            rows.append({"record": "stadium_paired_v11", "schema": compare.ROW_SCHEMA,
                         "kind": kind, "order": len(rows), "match_index": mi,
                         "local_action_index": 0, "diagnostic_action_index": mi,
                         "source_tick": 0, "draw_ordinal": 0, "input_cursor": 10 + mi,
                         "world_generation": 5 + mi,
                         "payload": {"action": "tick", "seed_before_hex": "00000001",
                                     "seed_after_hex": "00000001"}})
        probes.append({"schema": "melee-web-rng-draw-probe", "version": 3,
                       "source_cursor": mi, "overflowed": False, "draws": []})
    return matches, rows, probes


class PairedRngTests(unittest.TestCase):
    def test_declared_empty_actions_require_actual_probe_rows(self):
        matches, rows, probes = fixture()
        self.assertEqual([r["compared_actions"] for r in compare.compare_browser(matches, rows, probes)], [1, 1])
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes[:-1])

    def test_duplicate_owner_or_wrong_input_cursor_refuses_even_with_equal_seeds(self):
        matches, rows, probes = fixture()
        for field, value in (("world_generation", 5), ("input_cursor", 10)):
            changed = copy.deepcopy(rows)
            for row in changed[2:]:
                row[field] = value
            with self.assertRaises(compare.semantics.EvidenceError):
                compare.compare_browser(matches, changed, probes)

    def test_probe_reorder_and_missing_action_end_refuse(self):
        matches, rows, probes = fixture()
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, list(reversed(probes)))
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows[:-1], probes)

    def test_missing_rng_is_not_inferred_from_unchanged_event_count(self):
        matches, rows, probes = fixture()
        rows[1]["payload"]["seed_after_hex"] = "00000002"
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    def test_guard_action_remains_unpaired_and_seed_chain_is_checked(self):
        matches, rows, probes = fixture()
        matches[0]["selected"] = {}
        owner = {"status": "outside_proc", "callback_symbol": None,
                 "gobj": "0000000000000000", "proc": "0000000000000000",
                 "callback": "0000000000000000", "classifier": 0, "p_link": 0, "s_link": 0,
                 "fighter": None}
        seed = f"{(214013 + 2531011) & 0xffffffff:08x}"
        probes[0]["draws"] = [{"kind": "HSD_Rand", "seed_after": seed, "owner": owner}]
        rows[1]["payload"]["seed_after_hex"] = seed
        report = compare.compare_browser(matches, rows, probes)[0]
        self.assertEqual(report["unpaired_browser_events"], 1)
        self.assertEqual(report["compared_actions"], 0)
        probes[0]["draws"][0]["seed_after"] = "00000002"
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    def test_selected_event_compares_semantics_and_refuses_extra_event(self):
        matches, rows, probes = fixture()
        seed = f"{(214013 + 2531011) & 0xffffffff:08x}"
        native = {"kind": "HSD_Rand", "seed_after": seed, "owner": {
            "status": "scheduled_proc", "callback_symbol": "Fighter_8006A360",
            "gobj": "0000000010000000", "proc": "0000000010000004", "callback": "0000000000000042",
            "classifier": 4, "p_link": 8, "s_link": 1,
            "fighter": {"slot": 0, "entity": 0, "kind": 8}}}
        original = {"rng_ordinal": 0, "primitive": "HSD_Rand", "seed_after": seed, "owner": {
            "status": "scheduled_proc", "callback_symbol": "Fighter_8006A360",
            "gobj": "0x80001000", "proc": "0x80001004", "callback": "0x8006a360",
            "classifier": 4, "p_link": 8, "s_link": 1,
            "source_slot": 0, "entity": 0, "fighter_kind": 8}}
        matches[0]["selected"][0] = [original]
        matches[0]["selected_event_count"] = 1
        probes[0]["draws"] = [native]
        rows[1]["payload"]["seed_after_hex"] = seed
        self.assertEqual(compare.compare_browser(matches, rows, probes)[0]["selected_event_count"], 1)
        native["owner"]["fighter"]["slot"] = 1
        with self.assertRaises(compare.semantics.EvidenceError):
            compare.compare_browser(matches, rows, probes)

    @unittest.skipUnless(os.environ.get("MELEE_PAIRED_FIRST_GO_PROJECTION"), "retained paired projection unavailable")
    def test_retained_original_windows_bind_all_selected_events_and_guards(self):
        path = Path(os.environ["MELEE_PAIRED_FIRST_GO_PROJECTION"])
        matches, _ = compare.load_reference(path, compare.identity(path)["sha256"])
        self.assertEqual([m["selected_event_count"] for m in matches], [118035, 94417])
        self.assertEqual([m["unpaired_original_guard_events"] for m in matches], [8, 29])


if __name__ == "__main__":
    unittest.main()
