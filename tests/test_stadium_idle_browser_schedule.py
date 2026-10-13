"""Small refusal controls for the sealed-run offline schedule extractor."""
from __future__ import annotations

import copy
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from stadium_idle_browser_schedule import (
    EXPECTED_PACKET_SHA256, StadiumIdleScheduleError, _boundary_rng_observations, _build_schedule,
    _identity, _load_and_validate_api, _profile_provenance, _validate_source_manifest,
    _validate_retained_summaries,
)


def small_rows(c_tick=1):
    """Four source ticks and three draws, including a real tick/tick group."""
    rows = []
    def row(boundary, tick, draw, pc, **extra):
        payload = dict(boundary=boundary, source_tick=tick, draw_ordinal=draw,
                       pc=pc, **extra)
        value = dict(seq=len(rows), event="boundary", source_tick=tick,
                     draw_ordinal=draw, payload=payload)
        rows.append(value)
        return value
    row("setup", 0, 0, 0x8016E9C4)
    tick, draw, go = 0, 0, None
    for kind in ("tick", "draw", "tick", "tick", "draw", "tick", "draw"):
        if kind == "tick":
            row("pad_consume", tick, draw, 0,
                statuses=[f"{tick * 4 + port:022x}" for port in range(4)])
            if tick == c_tick:
                value = row("unused", tick, draw, 0x8016B824, phase="go_after")
                value["event"] = "progress"
                value["payload"].pop("boundary")
                go = value["seq"]
            row("source_tick", tick, draw, 0x80390EB4)
            tick += 1
        else:
            row("draw_enter", tick, draw, 0x80390FC0)
            row("draw_return", tick, draw, 0x80391040)
            draw += 1
    return rows, go


def extract_small(rows, go, c_tick=1):
    return _build_schedule(rows, lambda row, sequence: row["payload"]["statuses"],
                           setup_sequence=0, go_sequence=go,
                           terminal_sequence=len(rows) - 1,
                           c_tick=c_tick, f_tick=c_tick + 1,
                           expected_ticks=4, expected_draws=3,
                           expected_post_go_counts={"source_tick":4-c_tick,
                               "draw_enter":2, "draw_return":2})


class StadiumIdleBrowserIdentityTests(unittest.TestCase):
    def test_capture_summary_uses_exact_producer_projection(self):
        route = {"source": {"sha256": "source"}, "C": 2}
        extracted = {"route": route, "decision": "source-only",
                     "seed_observations": [{"seed": "12345678"}],
                     "literal_input_bundles": [{"bundle": 0}],
                     "source_action_schedule": [{"action": "tick"}]}
        summary = {key:value for key,value in extracted.items()
                   if key not in ("literal_input_bundles", "source_action_schedule")}
        report = {"stadium_go_prefix": copy.deepcopy(route),
                  "original_idle_3500": copy.deepcopy(summary)}
        _validate_retained_summaries(report, copy.deepcopy(extracted), route, extracted)
        wrong_route = copy.deepcopy(report)
        wrong_route["stadium_go_prefix"]["C"] = 3
        with self.assertRaisesRegex(StadiumIdleScheduleError, "report route"):
            _validate_retained_summaries(wrong_route, extracted, route, extracted)
        for key in extracted:
            with self.subTest(changed_extracted_field=key):
                changed = copy.deepcopy(extracted)
                changed[key] = None
                with self.assertRaisesRegex(StadiumIdleScheduleError, "complete idle extract"):
                    _validate_retained_summaries(report, changed, route, extracted)
        for key in summary:
            with self.subTest(changed_summary_field=key):
                changed = copy.deepcopy(report)
                changed["original_idle_3500"][key] = None
                with self.assertRaisesRegex(StadiumIdleScheduleError, "exact producer projection"):
                    _validate_retained_summaries(changed, extracted, route, extracted)
        old_bug_shape = dict(report, original_idle_3500=route)
        with self.assertRaisesRegex(StadiumIdleScheduleError, "exact producer projection"):
            _validate_retained_summaries(old_bug_shape, extracted, route, extracted)

    def test_api_imports_are_real_current_exports(self):
        api = _load_and_validate_api(ROOT)
        for name in ("validate_stream", "validate_input_status", "iter_records",
                     "validate_stadium_go_prefix", "validate_stadium_idle_3500_reference",
                     "decode_start_melee_data", "_slice", "_consumed_ports",
                     "_validate_stadium_idle_state_record", "canonical"):
            self.assertTrue(callable(api[name]), name)
        self.assertEqual(api["FIRST_CSS_DRAW_ENTER_PC"], 0x80390FC0)
        self.assertEqual(api["STADIUM_IDLE_STATE_TAGS"], frozenset(range(67, 90)))

    def test_source_inventory_count_comes_from_hash_bound_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            declared = []
            for name in ("stadium_go_prefix.py", "extra_dependency.py"):
                path = Path(temporary) / name
                path.write_bytes(name.encode())
                declared.append(_identity(path))
            for entries in (declared[:1], declared):
                checked = _validate_source_manifest(entries, {"sha256":EXPECTED_PACKET_SHA256})
                self.assertEqual(len(checked), len(entries))
            with self.assertRaisesRegex(StadiumIdleScheduleError, "hash differs"):
                declared[1]["sha256"] = "a" * 64
                _validate_source_manifest(declared, {"sha256":EXPECTED_PACKET_SHA256})

    def test_source_manifest_refuses_unbound_and_duplicate_inventory(self):
        with self.assertRaisesRegex(StadiumIdleScheduleError, "not bound"):
            _validate_source_manifest([], {"sha256":"0" * 64})
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "stadium_go_prefix.py"
            path.write_bytes(b"source")
            record = _identity(path)
            with self.assertRaisesRegex(StadiumIdleScheduleError, "malformed"):
                _validate_source_manifest([record, record], {"sha256":EXPECTED_PACKET_SHA256})

    def test_postrun_mutation_does_not_relabel_prelaunch_profile(self):
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            source, capture = base / "prelaunch", base / "capture"
            source.mkdir()
            config = capture / "user/Config"
            config.mkdir(parents=True)
            prelaunch = []
            for name in ("Dolphin.ini", "GCPadNew.ini"):
                path = source / name
                path.write_bytes(("prelaunch " + name).encode())
                prelaunch.append(_identity(path))
                (config / name).write_bytes(("postrun changed " + name).encode())
            launch = {f"Config/{Path(item['path']).name}":item["sha256"]
                      for item in prelaunch}
            actual = _profile_provenance(prelaunch, launch, capture)
            self.assertEqual(actual["owner_prelaunch_profile_files"], prelaunch)
            self.assertEqual(actual["launch_profile_sha256"], launch)
            self.assertTrue(actual["postrun_configs_are_not_prelaunch_profile_identity"])
            for item in actual["postrun_user_config_observations"]:
                self.assertNotEqual(item["sha256"], launch[f"Config/{Path(item['path']).name}"])
            changed = dict(launch, **{"Config/GCPadNew.ini":"b" * 64})
            with self.assertRaisesRegex(StadiumIdleScheduleError, "launch profile hash map"):
                _profile_provenance(prelaunch, changed, capture)


class StadiumIdleBrowserScheduleTests(unittest.TestCase):
    def test_fresh_c_f_seed_words_bind_by_raw_sequence_and_dynamic_coordinates(self):
        expected = {
            "C": {"raw_identity": {"sequence": 9001, "event": "boundary",
                "boundary": "source_tick", "source_tick": 42, "draw_ordinal": 40}},
            "F": {"raw_identity": {"sequence": 9007, "event": "boundary",
                "boundary": "source_tick", "source_tick": 43, "draw_ordinal": 41}},
        }
        rows = [{"ordinal": i, "sequence": 9001 if i == 0 else 9007 if i == 1 else 10_000 + i,
                 "pointer_hex": "804d5f90",
                 "seed_hex": f"{i + 1:08x}"} for i in range(3501)]
        found = _boundary_rng_observations({"seed_observations": rows}, expected)
        self.assertEqual(found, [
            {"label": "C", "sequence": 9001, "source_tick": 42,
             "draw_ordinal": 40, "post_go_ordinal": 0,
             "pointer_hex": "804d5f90", "seed_hex": "00000001"},
            {"label": "F", "sequence": 9007, "source_tick": 43,
             "draw_ordinal": 41, "post_go_ordinal": 1,
             "pointer_hex": "804d5f90", "seed_hex": "00000002"},
        ])
        swapped = copy.deepcopy(rows)
        swapped[1]["sequence"] = 9001
        with self.assertRaisesRegex(StadiumIdleScheduleError, "duplicate source sequence"):
            _boundary_rng_observations({"seed_observations": swapped}, expected)
        missing = copy.deepcopy(rows)
        missing[1]["sequence"] = 9006
        with self.assertRaisesRegex(StadiumIdleScheduleError, "does not match its raw source row"):
            _boundary_rng_observations({"seed_observations": missing}, expected)
        wrong_coord = copy.deepcopy(expected)
        wrong_coord["F"]["raw_identity"]["sequence"] = 9002
        with self.assertRaisesRegex(StadiumIdleScheduleError, "does not match its raw source row"):
            _boundary_rng_observations({"seed_observations": rows}, wrong_coord)

    def test_dynamic_go_literal_ports_and_consecutive_ticks(self):
        for c_tick in (1, 2):
            with self.subTest(c_tick=c_tick):
                rows, go = small_rows(c_tick)
                packet = extract_small(rows, go, c_tick)
                payload = packet["runtime_payload"]
                self.assertEqual([row["action"] for row in payload["schedule"]],
                                 ["tick", "draw", "tick", "tick", "draw", "tick", "draw"])
                self.assertEqual(packet["counts"]["combined_driver_actions"], 7)
                self.assertEqual(packet["counts"]["ticks"], 4)
                self.assertEqual(packet["counts"]["combined_draws"], 3)
                self.assertEqual(packet["bundle_provenance"][2]["draw_ordinal"], 1)
                self.assertEqual(payload["bundles"][2]["ports"][3]["status_hex"], f"{11:022x}")
                self.assertEqual(set(payload), {"schema", "version", "bundles", "schedule"})
                self.assertNotIn("source_tick", payload["schedule"][0])
                self.assertEqual(packet["action_provenance"][-1]["draw_return_sequence"], len(rows)-1)

    def test_missing_pad_wrong_counters_and_draw_pair_fail_at_boundary(self):
        rows, go = small_rows()
        controls = [
            (1, "boundary", "pad_poll", "no preceding consumed PAD"),
            (2, "pc", 0, "PC differs"),
            (3, "source_tick", 0, "actual action prefix"),
            (4, "draw_ordinal", 1, "identities differ"),
        ]
        for index, key, value, message in controls:
            with self.subTest(index=index, key=key):
                changed = copy.deepcopy(rows)
                changed[index]["payload"][key] = value
                if key in ("source_tick", "draw_ordinal"):
                    changed[index][key] = value
                with self.assertRaisesRegex(StadiumIdleScheduleError, message):
                    extract_small(changed, go)

    def test_row_reorder_malformed_ports_and_missing_terminal_refused(self):
        rows, go = small_rows()
        changed = copy.deepcopy(rows)
        changed[1]["seq"] = 2
        with self.assertRaisesRegex(StadiumIdleScheduleError, "reordered"):
            extract_small(changed, go)
        changed = copy.deepcopy(rows)
        changed[1]["payload"]["statuses"][3] = "00"
        with self.assertRaisesRegex(StadiumIdleScheduleError, "11-byte"):
            extract_small(changed, go)
        with self.assertRaisesRegex(StadiumIdleScheduleError, "incomplete"):
            extract_small(rows[:-1], go)

    def test_wrong_go_and_sealed_full_count_controls(self):
        rows, go = small_rows()
        with self.assertRaisesRegex(StadiumIdleScheduleError, "GO marker"):
            extract_small(rows, go + 1)
        with self.assertRaisesRegex(StadiumIdleScheduleError, "SourceTick coverage"):
            _build_schedule(rows, lambda row, seq: row["payload"]["statuses"],
                            setup_sequence=0, go_sequence=go,
                            terminal_sequence=len(rows)-1, c_tick=1, f_tick=2)


if __name__ == "__main__":
    unittest.main()
