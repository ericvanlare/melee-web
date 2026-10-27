import importlib.util
import unittest
import copy
import json
import hashlib
import os
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "tools" / "compare_rng_owner_window.py"
SPEC = importlib.util.spec_from_file_location("compare_rng_owner_window", SCRIPT)
compare = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(compare)


def boundary(seq, source_tick, draw_ordinal, name, *, pc=0x80380000):
    return {
        "event": "boundary",
        "seq": seq,
        "source_tick": source_tick,
        "draw_ordinal": draw_ordinal,
        "payload": {
            "boundary": name,
            "source_tick": source_tick,
            "draw_ordinal": draw_ordinal,
            "match_index": 1,
            "pc": pc,
            "lr": 0x80381000,
        },
    }


def source_fixture(go_tick=2):
    def projected_coordinate(record):
        payload = record["payload"]
        return {
            "sequence": record["seq"],
            "source_tick": record["source_tick"],
            "draw_ordinal": record["draw_ordinal"],
            "match_index": payload["match_index"],
            "pc": payload["pc"],
            "lr": payload["lr"],
        }

    records = []
    actions = []
    sequence = 100
    go_coordinate = None
    for tick in range(180):
        consume = sequence
        records.append(boundary(consume, tick, tick, "pad_consume"))
        if tick == go_tick:
            go_sequence = consume + 1
            records.append({
                "event": "progress", "seq": go_sequence,
                "source_tick": tick, "draw_ordinal": tick,
                "payload": {"phase": "go_after", "match_index": 1,
                            "pc": 0x80382000, "lr": 0x80382004},
            })
            go_coordinate = {"phase": "go_after", "sequence": go_sequence,
                             "source_tick": tick, "draw_ordinal": tick,
                             "match_index": 1, "pc": 0x80382000, "lr": 0x80382004}
            tick_return = consume + 2
            draw_enter = consume + 3
            draw_return = consume + 4
            sequence = consume + 5
        else:
            tick_return = consume + 1
            draw_enter = consume + 2
            draw_return = consume + 3
            sequence = consume + 4
        tick_event = boundary(tick_return, tick, tick, "source_tick")
        enter_event = boundary(draw_enter, tick + 1, tick, "draw_enter")
        return_event = boundary(draw_return, tick + 1, tick, "draw_return")
        records.extend((tick_event, enter_event, return_event))
        coordinates = (consume, tick_return, draw_enter, draw_return)
        actions.extend((
            {"action_index": 2 * tick, "kind": "tick", "source_tick": tick,
             "consume": projected_coordinate(records[-4] if tick != go_tick else records[-5]),
             "tick": projected_coordinate(tick_event)},
            {"action_index": 2 * tick + 1, "kind": "draw", "source_tick": tick + 1,
             "draw_enter": projected_coordinate(enter_event),
             "draw_return": projected_coordinate(return_event)},
        ))
    records.sort(key=lambda row: row["seq"])
    return ({"schema": "melee-web-stadium-later-b-gate2-reference-projection-v1",
             "selected_match": 1, "setup_global_cursor": 943,
             "actions": actions, "go": {"coordinate": go_coordinate}, "C3": False},
            {row["seq"]: row for row in records})


def interval_rows():
    return [
        {"offset": 0, "source_tick": 70, "draw_ordinal": 5,
         "consume_sequence": 100, "tick_return_sequence": 110,
         "draw_enter_sequence": 111, "draw_return_sequence": 120},
        {"offset": 1, "source_tick": 71, "draw_ordinal": 6,
         "consume_sequence": 121, "tick_return_sequence": 130,
         "draw_enter_sequence": 131, "draw_return_sequence": 140},
        {"offset": 2, "source_tick": 72, "draw_ordinal": 7,
         "consume_sequence": 141, "tick_return_sequence": 150,
         "draw_enter_sequence": 151, "draw_return_sequence": 160},
        {"offset": 3, "source_tick": 73, "draw_ordinal": 8,
         "consume_sequence": 161, "tick_return_sequence": 170,
         "draw_enter_sequence": 171, "draw_return_sequence": 180},
        {"offset": 4, "source_tick": 74, "draw_ordinal": 9,
         "consume_sequence": 181, "tick_return_sequence": 190,
         "draw_enter_sequence": 191, "draw_return_sequence": 200},
    ]


class RngOwnerWindowTests(unittest.TestCase):
    def original_probe_fixture(self, count=1):
        records = []
        seed = 1
        for index in range(count):
            if index:
                seed = (214013 * seed + 2531011) & 0xffffffff
            records.append({
                "label": "rand_return", "pc": "0x80380524", "match": 1,
                "source_tick": 11, "rng_ordinal": index,
                "observer_next_sequence": 110 + index,
                "draw_ordinal": 1, "seed_owner_pointer": "0x804d6000",
                "seed_after": f"{seed:08x}",
                # First word is preserved provenance, not interpreted as seed_after.
                "random_seed_and_pointer": f"0x1234567800000000"[:-8] + "804d6000",
                "owner": {"status": "outside_proc", "gobj": "0x00000000",
                          "proc": "0x00000000", "callback": "0x00000000",
                          "classifier": 0, "p_link": 0, "s_link": 0,
                          "callback_symbol": None},
            })
        probe = {
            "schema": compare.ORIGINAL_SCHEMA, "version": 4,
            "diagnostic_only": True, "window_complete": True,
            "source_revision": "GALE01r2", "match": 1,
            "rng_return_site": "rng_returns",
            "rng_return_pcs": list(compare.PC_TO_KIND),
            "window_basis": "actual_go_raw_guard_band",
            "requested_first_offset": 1, "requested_last_offset": 3,
            "first_guard_tick_partial": True, "go_tick": 10,
            "go_observer_sequence": 105, "first_tick": 10, "last_tick": 14,
            "capture_id": "capture", "sequence_id": "sequence",
            "dol_sha256": "d" * 64, "record_count": count, "records": records,
        }
        status = {
            "state": "completed", "completed": True, "invalid": False, "error": None,
            "cpu_probe_close": {
                "window_basis": probe["window_basis"],
                "requested_first_offset": 1, "requested_last_offset": 3,
                "go_tick": 10, "go_observer_sequence": 105,
                "first_guard_tick_partial": True, "configured_match": 1,
                "first_tick": 10, "last_tick": 14,
                "reason": "source_tick_after_window", "source_tick": 15,
            },
        }
        identity = {"capture_id": "capture", "sequence_id": "sequence",
                    "dol_sha256": "d" * 64}
        validation = {"valid": True, "output_sha256": "a" * 64,
                      "capture_id": "capture", "sequence_id": "sequence",
                      "dol_sha256": "d" * 64}
        intervals = [{"offset": 1, "source_tick": 11, "draw_ordinal": 1,
                      "consume_sequence": 100, "tick_return_sequence": 115,
                      "draw_enter_sequence": 116, "draw_return_sequence": 120}]
        return probe, status, identity, validation, intervals

    def test_original_actual_dereference_and_legacy_snapshot_are_distinct(self):
        probe, status, identity, validation, intervals = self.original_probe_fixture()
        records = compare._validate_original_probe(probe, validation, status, identity,
                                                   intervals, {"go_tick": 10, "go_sequence": 105},
                                                   "a" * 64)
        self.assertEqual(records[0]["seed_after"], "00000001")
        self.assertEqual(records[0]["raw_owner_pointer"], "0x804d6000")
        self.assertNotEqual(records[0]["raw_global_seed_pointer_snapshot"][2:10],
                            records[0]["seed_after"])

    def test_original_actual_seed_chain_and_pointer_provenance_fail_closed(self):
        probe, status, identity, validation, intervals = self.original_probe_fixture(count=2)
        valid = compare._validate_original_probe(probe, validation, status, identity,
                                                 intervals, {"go_tick": 10, "go_sequence": 105},
                                                 "a" * 64)
        self.assertEqual(len(valid), 2)
        probe["records"][1]["seed_after"] = "ffffffff"
        with self.assertRaisesRegex(compare.EvidenceError, "seed discontinuity"):
            compare._validate_original_probe(probe, validation, status, identity,
                                             intervals, {"go_tick": 10, "go_sequence": 105},
                                             "a" * 64)
        probe["records"][1]["seed_after"] = f"{(214013 + 2531011) & 0xffffffff:08x}"
        probe["records"][1]["random_seed_and_pointer"] = "0x12345678804d6004"
        with self.assertRaisesRegex(compare.EvidenceError, "global/pointer provenance"):
            compare._validate_original_probe(probe, validation, status, identity,
                                             intervals, {"go_tick": 10, "go_sequence": 105},
                                             "a" * 64)

    def test_original_missing_writer_two_lcg_steps_refuses(self):
        probe, status, identity, validation, intervals = self.original_probe_fixture(count=2)
        prior = int(probe["records"][0]["seed_after"], 16)
        next_seed = (214013 * prior + 2531011) & 0xffffffff
        two_steps = (214013 * next_seed + 2531011) & 0xffffffff
        self.assertNotEqual(next_seed, two_steps)
        self.assertEqual(probe["records"][0]["seed_owner_pointer"],
                         probe["records"][1]["seed_owner_pointer"])
        # A missing writer is not excused merely because the observed value
        # is reachable in two LCG steps at a raw source-tick boundary.
        probe["records"][1]["source_tick"] = 12
        probe["records"][1]["seed_after"] = f"{two_steps:08x}"
        with self.assertRaisesRegex(compare.EvidenceError, "seed discontinuity"):
            compare._validate_original_probe(probe, validation, status, identity,
                                             intervals, {"go_tick": 10, "go_sequence": 105},
                                             "a" * 64)

    def test_updates_v5_normalizes_randi_primitive_and_preserves_raw_result(self):
        probe, status, identity, validation, intervals = self.original_probe_fixture(count=3)
        probe.update(version=5, rng_return_site="rng_updates",
                     rng_return_pcs=list(compare.UPDATE_PC_TO_KIND))
        for row, pc in zip(probe["records"], compare.UPDATE_PC_TO_KIND):
            row.update(pc=pc, label=compare.PC_TO_LABEL[pc], gpr=["0x00000000"] * 32)
        probe["records"][2]["gpr"][3] = "0x00000007"  # scaled Randi result
        def validate():
            return compare._validate_original_probe(probe, validation, status, identity,
                intervals, {"go_tick": 10, "go_sequence": 105}, "a" * 64)
        rows = validate()
        self.assertEqual([r["kind"] for r in rows], ["HSD_Rand", "HSD_Randf", "HSD_Rand"])
        native = [{"kind": r["kind"], "seed_after": r["seed_after"], "owner": r["owner"],
                   "owner_raw_pointers": {key: "0000000000000000" for key in ("gobj", "proc", "callback")}}
                  for r in rows]
        for r in rows:
            r["phase"] = "tick"
        matched = compare.compare_event_lists(rows, native, 1)
        self.assertEqual(matched[2]["original_raw_return_site_provenance"],
                         {"pc": "0x803805b8", "label": "randi_return", "gpr3": "0x00000007"})
        self.assertNotEqual(rows[2]["seed_after"], "00000007")
        probe["records"][2]["gpr"][3] = "0x00000008"
        self.assertEqual(validate()[2]["seed_after"], rows[2]["seed_after"])
        with self.assertRaisesRegex(compare.EvidenceError, "differs"):
            compare.compare_event_lists(rows, list(reversed(native)), 1)
        for bad_pcs in (list(compare.PC_TO_KIND), list(reversed(compare.UPDATE_PC_TO_KIND))):
            probe["rng_return_pcs"] = bad_pcs
            with self.assertRaisesRegex(compare.EvidenceError, "complete selected site"):
                validate()
        probe["rng_return_pcs"] = list(compare.UPDATE_PC_TO_KIND)
        probe["records"][2]["label"] = "rand_return"
        with self.assertRaisesRegex(compare.EvidenceError, "site/kind is unsupported"):
            validate()
        probe["records"][2]["label"] = "randi_return"
        probe["records"][2]["seed_after"] = f"{(214013 * int(rows[2]['seed_after'], 16) + 2531011) & 0xffffffff:08x}"
        with self.assertRaisesRegex(compare.EvidenceError, "seed discontinuity"):
            validate()
        probe["version"] = 4
        with self.assertRaisesRegex(compare.EvidenceError, "coverage/version"):
            validate()

    def test_dynamic_go_projection_yields_partial_and_selected_intervals(self):
        projection, by_seq = source_fixture(go_tick=2)
        rows, go = compare.derive_intervals(projection, by_seq)
        self.assertEqual(go, {"go_tick": 2, "go_sequence": projection["go"]["coordinate"]["sequence"],
                              "prefix_cursor": 943})
        self.assertEqual([row["offset"] for row in rows], [0, 1, 2, 3, 4])
        self.assertEqual([row["selected"] for row in rows], [False, True, True, True, False])
        self.assertTrue(rows[0]["partial"])
        self.assertTrue(all(not row["partial"] for row in rows[1:]))

    def test_projection_must_bind_actual_go_event(self):
        projection, by_seq = source_fixture(go_tick=2)
        projection["go"]["coordinate"]["sequence"] += 1
        with self.assertRaisesRegex(compare.EvidenceError, "actual progress/go_after"):
            compare.derive_intervals(projection, by_seq)

    def test_projection_rejects_incomplete_or_nonadjacent_tick_draw(self):
        projection, by_seq = source_fixture(go_tick=2)
        projection["actions"][7]["action_index"] += 1
        with self.assertRaisesRegex(compare.EvidenceError, "duplicate or reordered"):
            compare.derive_intervals(projection, by_seq)
        projection, by_seq = source_fixture(go_tick=2)
        projection["actions"].pop(3)
        with self.assertRaisesRegex(compare.EvidenceError, "360-action"):
            compare.derive_intervals(projection, by_seq)

    def test_records_join_by_next_observer_sequence_not_raw_tick(self):
        rows = interval_rows()
        event = {"ordinal": 1, "observer_next_sequence": 135,
                 "source_tick": 999, "draw_ordinal": 6}
        buckets, guards = compare.assign_original_events([event], rows, go_sequence=105)
        self.assertEqual(buckets[71], [event])
        self.assertEqual(event["phase"], "draw")
        self.assertEqual(guards, {0: 0, 4: 0})

    def test_records_before_go_or_outside_source_brackets_refuse(self):
        rows = interval_rows()
        with self.assertRaisesRegex(compare.EvidenceError, "predates actual GO"):
            compare.assign_original_events([{"ordinal": 0, "observer_next_sequence": 104,
                "source_tick": 71, "draw_ordinal": 6}], rows, 105)
        with self.assertRaisesRegex(compare.EvidenceError, "maps to 0 source intervals"):
            compare.assign_original_events([{"ordinal": 0, "observer_next_sequence": 205,
                "source_tick": 71, "draw_ordinal": 7}], rows, 105)

    def test_overlapping_source_intervals_are_ambiguous(self):
        rows = interval_rows()
        rows[1]["consume_sequence"] = 105
        with self.assertRaisesRegex(compare.EvidenceError, "maps to 2 source intervals"):
            compare.assign_original_events([{"ordinal": 0, "observer_next_sequence": 111,
                "source_tick": 71, "draw_ordinal": 6}], rows, 105)

    def test_sequence_bracket_keeps_tick_and_draw_phase_distinct(self):
        rows = interval_rows()
        events = [
            {"ordinal": 0, "observer_next_sequence": 125, "source_tick": 72, "draw_ordinal": 6},
            {"ordinal": 1, "observer_next_sequence": 130, "source_tick": 72, "draw_ordinal": 6},
            {"ordinal": 2, "observer_next_sequence": 135, "source_tick": 71, "draw_ordinal": 6},
        ]
        buckets, _ = compare.assign_original_events(events, rows, 105)
        self.assertEqual([event["phase"] for event in buckets[71]], ["tick", "tick", "draw"])
        self.assertEqual([event["ordinal"] for event in buckets[71]], [0, 1, 2])

    def test_semantic_owner_rejects_unknown_symbols_and_bad_outside_state(self):
        unknown = {"status": "scheduled_proc", "classifier": 1, "p_link": 0,
                   "s_link": 0, "callback_symbol": "unknown"}
        with self.assertRaisesRegex(compare.EvidenceError, "callback symbol is unresolved"):
            compare._semantic_owner(unknown, "fixture")
        inconsistent = {"status": "outside_proc", "classifier": 1, "p_link": 0,
                        "s_link": 0, "callback_symbol": None}
        with self.assertRaisesRegex(compare.EvidenceError, "outside-proc semantic owner"):
            compare._semantic_owner(inconsistent, "fixture")

    def test_observed_particle_owner_is_exact_and_ordered(self):
        # Labeled synthetic controls for the actual registration tuple; no
        # fallback interpretation of an immutable raw 'unknown' symbol.
        owner = {"status": "scheduled_proc", "classifier": 8, "p_link": 11,
                 "s_link": 15, "callback_symbol": "efLib_particles_proc_main"}
        original = {**owner, "gobj": "0x80c121e0", "proc": "0x80c12240",
                    "callback": "0x8005c9a4"}
        native = {**owner, "gobj": "0000000000010000", "proc": "0000000000020000",
                  "callback": "0000000000030000"}
        semantic, pointers = compare._original_owner(original, "particle original")
        self.assertEqual(semantic, compare._native_owner(native, "particle native"))
        old = [{"ordinal": i, "kind": kind, "seed_after": seed, "owner": semantic,
                "observer_next_sequence": i + 1, "phase": "draw", "source_tick": 1,
                "draw_ordinal": 0, "raw_owner_pointer": "0x804d5f94",
                "raw_global_seed_pointer_snapshot": "0xdeadbeef804d5f94",
                "owner_raw_pointers": pointers}
               for i, (kind, seed) in enumerate((("HSD_Rand", "00000001"),
                                                 ("HSD_Randf", "00000002")))]
        new = [{"kind": row["kind"], "seed_after": row["seed_after"], "owner": semantic,
                "owner_raw_pointers": {key: native[key] for key in ("gobj", "proc", "callback")}}
               for row in old]
        self.assertEqual(len(compare.compare_event_lists(old, new, 1)), 2)
        with self.assertRaisesRegex(compare.EvidenceError, "differs"):
            compare.compare_event_lists(old, list(reversed(new)), 1)
        for symbol in ("unknown", "efLib_particles_proc_other"):
            with self.assertRaisesRegex(compare.EvidenceError, "callback symbol is unresolved"):
                compare._original_owner({**original, "callback_symbol": symbol}, "particle wrong symbol")
        for field in ("classifier", "p_link", "s_link"):
            with self.assertRaisesRegex(compare.EvidenceError, "semantic owner tuple differs"):
                compare._native_owner({**native, field: native[field] + 1}, "particle wrong tuple")

    def test_owner_pointer_addresses_are_retained_but_not_mapped(self):
        original = {"status": "scheduled_proc", "classifier": 1, "p_link": 2,
                    "s_link": 3, "callback_symbol": "grStadium_801D1390",
                    "gobj": "0x80001000", "proc": "0x80002000",
                    "callback": "0x801d1390"}
        native = {"status": "scheduled_proc", "classifier": 1, "p_link": 2,
                  "s_link": 3, "callback_symbol": "grStadium_801D1390",
                  "gobj": "0000000000010000", "proc": "0000000000020000",
                  "callback": "00000000003d1390"}
        old_semantic, old_ptrs = compare._original_owner(original, "old")
        new_semantic = compare._native_owner(native, "new")
        self.assertEqual(old_semantic, new_semantic)
        self.assertNotEqual(old_ptrs["gobj"], native["gobj"])

    def test_event_order_kind_seed_and_owner_are_exact(self):
        owner = {"status": "outside_proc", "classifier": 0, "p_link": 0,
                 "s_link": 0, "callback_symbol": None}
        old = [{"ordinal": 0, "kind": "HSD_Rand", "seed_after": "00000001",
                "owner": owner, "observer_next_sequence": 1,
                "phase": "tick", "source_tick": 1, "draw_ordinal": 0,
                "raw_owner_pointer": "0x804d5f94",
                "raw_global_seed_pointer_snapshot": "0xdeadbeef804d5f94",
                "owner_raw_pointers": {name: "0x00000000" for name in ("gobj", "proc", "callback")}},
               {"ordinal": 1, "kind": "HSD_Randf", "seed_after": "00000002",
                "owner": owner, "observer_next_sequence": 2,
                "phase": "draw", "source_tick": 2, "draw_ordinal": 0,
                "raw_owner_pointer": "0x804d5f94",
                "raw_global_seed_pointer_snapshot": "0xdeadbeef804d5f94",
                "owner_raw_pointers": {name: "0x00000000" for name in ("gobj", "proc", "callback")}}]
        new = [{"kind": event["kind"], "seed_after": event["seed_after"],
                "owner": owner, "owner_raw_pointers": {name: "0000000000000000"
                                                        for name in ("gobj", "proc", "callback")}}
               for event in old]
        compared = compare.compare_event_lists(old, new, 1)
        self.assertEqual(len(compared), 2)
        for source, native, result in zip(old, new, compared):
            self.assertEqual(result["seed_after"], source["seed_after"])
            self.assertEqual(result["original_global_seed_pointer_snapshot_provenance"],
                             source["raw_global_seed_pointer_snapshot"])
            self.assertEqual(result["original_seed_owner_pointer_provenance"],
                             source["raw_owner_pointer"])
            self.assertEqual(result["original_owner_pointers_local_provenance"],
                             source["owner_raw_pointers"])
            self.assertEqual(result["native_owner_pointers_local_provenance"],
                             native["owner_raw_pointers"])
        # Distinct events cannot be reordered to obtain a match.
        with self.assertRaisesRegex(compare.EvidenceError, "differs"):
            compare.compare_event_lists(old, list(reversed(new)), 1)
        for field, bad in (("kind", "HSD_Randf"), ("seed_after", "ffffffff"),
                           ("owner", {**owner, "classifier": 1})):
            changed = [dict(item) for item in new]
            changed[0][field] = bad
            with self.assertRaisesRegex(compare.EvidenceError, "differs"):
                compare.compare_event_lists(old, changed, 1)
        with self.assertRaisesRegex(compare.EvidenceError, "event count differs"):
            compare.compare_event_lists(old, new[:1], 1)

    def test_native_report_requires_empty_selected_rows_and_exact_artifact(self):
        go_tick, prefix, projection_sha = 6, 943, "a" * 64
        ticks = [7, 8, 9]
        report = {"later_b_gate2": {"component_pass": True, "accepted_actions": 360,
                    "token_id": "modeled-component-token"},
                  "browser_report": {"later_b_transfer": {"native_terminal": {"kind": "retired", "receipt": {
                      "native_retired": True, "later_b_transfer": {"token": "modeled-component-token", "parent_close_checked": True},
                      "matched_setup_cf": {"schema": "melee-web-stadium-later-b-gate2-native-v1", "cursor": 360,
                        "complete": True, "actual_go": {"observed": True, "tick": go_tick}}}}}},
                  "result": "later_b_gate2_component_pass_replay_incomplete",
                  "rng_draw_probe": {
                      "complete": True, "projection_sha256": projection_sha,
                      "configuration": {"go_tick": go_tick, "prefix_cursor": prefix,
                                        "first_tick": 7, "last_tick": 9},
                      "coordinates": [],
                      "request": {"kind": "cursors", "cursors": [prefix + tick for tick in ticks]},
                      "selected_cursors": [prefix + tick for tick in ticks],
                      "captured_cursors": [prefix + tick for tick in ticks], "missing_cursors": [],
                      "artifact": {"name": "rng-draw-probe.jsonl", "bytes": 1, "sha256": "b" * 64}}}
        rows = [{"schema": compare.NATIVE_SCHEMA, "version": 2,
                 "source_cursor": prefix + tick, "overflowed": False, "draws": []}
                for tick in ticks]
        result = compare._validate_native(report, rows, projection_sha, [],
                                          {"go_tick": go_tick, "prefix_cursor": prefix},
                                          [1, 2, 3], 1, "b" * 64)
        self.assertEqual(result, {tick: [] for tick in ticks})
        with self.assertRaisesRegex(compare.EvidenceError, "every selected row"):
            compare._validate_native(report, rows[:2], projection_sha, [],
                                     {"go_tick": go_tick, "prefix_cursor": prefix},
                                     [1, 2, 3], 1, "b" * 64)


class ActualMetadataControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = os.environ.get("MELEE_STADIUM_RNG_OWNER_EVIDENCE_ROOT")
        if root is None:
            raise unittest.SkipTest("Set MELEE_STADIUM_RNG_OWNER_EVIDENCE_ROOT for retained actual metadata controls")
        cls.evidence_root = Path(root)
        if not cls.evidence_root.is_dir():
            raise AssertionError("Explicit RNG owner evidence root is missing")

    def test_actual_dynamic_launch_and_falsified_metadata(self):
        base = self.evidence_root
        original = base / 'rng-replay-original-capture-v1'
        binding = json.loads((original / 'same-run-reference-binding.json').read_bytes())
        env = json.loads(Path(binding['files']['launch']['path']).read_bytes())['environment_delta']
        probe = json.loads((original / 'rng-returns.json').read_bytes())
        projection = json.loads((original / 'same-run-reference-projection.json').read_bytes())
        go = {'go_tick': projection['go']['coordinate']['source_tick'],
              'go_sequence': projection['go']['coordinate']['sequence']}
        sidecar = original / 'rng-returns.json'
        compare._validate_dynamic_launch_request(env, probe, sidecar, go)
        for key, value in [('MWRC_CPU_PROBE_FIRST_TICK','84'), ('MWRC_CPU_PROBE_LAST_TICK','88'),
                           ('MWRC_CPU_PROBE_OUTPUT',str(sidecar)+'.wrong'),
                           ('MWRC_STADIUM_WHOLE_RNG_RETURNS','0')]:
            changed = dict(env, **{key:value})
            with self.assertRaises(compare.EvidenceError):
                compare._validate_dynamic_launch_request(changed, probe, sidecar, go)
        for key in ('go_tick','go_observer_sequence','first_tick','last_tick','requested_first_offset','requested_last_offset'):
            changed = dict(probe, **{key:probe[key]+1})
            with self.assertRaises(compare.EvidenceError):
                compare._validate_dynamic_launch_request(env, changed, sidecar, go)

    def test_actual_native_report_shape_and_wrong_token_go_refuse(self):
        base = self.evidence_root
        browser = base / 'rng-browser-capture-v3/browser-run'
        report = json.loads((browser / 'report.json').read_bytes())
        raw = (browser / 'rng-draw-probe.jsonl').read_bytes()
        rows = [json.loads(line) for line in raw.splitlines()]
        probe = report['rng_draw_probe']
        go = {'go_tick':probe['configuration']['go_tick'], 'prefix_cursor':probe['configuration']['prefix_cursor']}
        coordinates = probe['coordinates']
        validate = lambda value: compare._validate_native(value,rows,probe['projection_sha256'],coordinates,
            go,[1,2,3],len(raw),hashlib.sha256(raw).hexdigest())
        self.assertEqual(sum(map(len,validate(report).values())),9)
        owner = rows[0]['draws'][0]['owner']
        self.assertNotEqual(int(owner['callback'],16)%4,0)  # Actual table token, not aligned memory.
        for key,value in [('gobj','0000000000000001'),('proc','0000000000000001'),('callback','0000000000000000')]:
            with self.assertRaises(compare.EvidenceError):
                compare._native_owner(dict(owner, **{key:value}), 'falsified actual owner')

        for mutation in ('token','go','missing-receipt'):
            changed = copy.deepcopy(report)
            terminal = changed['browser_report']['later_b_transfer']['native_terminal']
            if mutation == 'token':
                terminal['receipt']['later_b_transfer']['token'] = 'foreign'
            elif mutation == 'go':
                terminal['receipt']['matched_setup_cf']['actual_go']['tick'] += 1
            else:
                del terminal['receipt']
                changed['later_b_gate2']['actual_go'] = {'observed':True,'tick':go['go_tick']}
            with self.assertRaises(compare.EvidenceError):
                validate(changed)


if __name__ == "__main__":
    unittest.main()
