import copy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"), str(ROOT / "tests")]

from reference_capture_semantics import pad_snapshot_bytes  # noqa: E402
from test_whole_session_replay import _pad_consume, _raw_pad_snapshot, _whole_setup  # noqa: E402
from whole_session_replay import SCENES, _consumed_ports  # noqa: E402
from whole_session_state_compare import (  # noqa: E402
    BrowserReader, Comparator, SourceCollector, _browser_completion_ok, _is_match_field,
    _fighter_entities, _snapshot_values, _state_from_payload, _validate_browser_report,
)


PAD_HEX = pad_snapshot_bytes(_raw_pad_snapshot())


def _word(raw, offset, value):
    struct.pack_into(">I", raw, offset, value)


def _state_slices(motion=80, tick=0, rng=0x12345678, fighter_count=4):
    slices = [
        {"name": "pad_snapshot", "address": 0x804C1F84, "size": 0x358,
         "hex": _raw_pad_snapshot().hex()},
        {"name": "rng_pointer", "address": 0x804D5F94, "size": 4,
         "hex": (0x80510000).to_bytes(4, "big").hex()},
        {"name": "rng_value", "address": 0x80510000, "size": 4,
         "hex": rng.to_bytes(4, "big").hex()},
        {"name": "scene_frame", "address": 0x80479D58, "size": 4,
         "hex": tick.to_bytes(4, "big").hex()},
        {"name": "match_frame", "address": 0x8046B6C4, "size": 4,
         "hex": tick.to_bytes(4, "big").hex()},
    ]
    for slot in range(fighter_count):
        pointer = 0x80580000 + slot * 0x3000
        entity = 0x80680000 + slot * 0x100
        slices.append({"name": "player_entities", "flags": slot,
                       "address": 0x80453080 + slot * 0xE90 + 0xB0,
                       "size": 8,
                       "hex": entity.to_bytes(4, "big").hex() + "00000000"})
        slices.append({"name": "player_entity_user_data", "flags": slot,
                       "address": entity + 0x2C, "size": 4,
                       "hex": pointer.to_bytes(4, "big").hex()})
        head = bytearray(0x1000)
        _word(head, 0, entity)
        head[0x0C] = slot
        _word(head, 4, 0)
        _word(head, 0x10, motion)
        _word(head, 0x14, 2)
        _word(head, 0x2C, 0x3F800000)
        _word(head, 0x894, 0)
        _word(head, 0x89C, 0x3F800000)
        _word(head, 0xE0, 1)
        head[0x620:0x620 + 0x6C] = bytes(0x6C)
        slices.append({"name": "fighter_head", "flags": slot, "address": pointer,
                       "size": len(head), "hex": bytes(head).hex()})
        tail = bytearray(0x1000)
        _word(tail, 0x830, 0x00000000)
        _word(tail, 0x998, 0x42700000)
        slices.append({"name": "fighter_tail", "flags": slot, "address": pointer + 0x1000,
                       "size": len(tail), "hex": bytes(tail).hex()})
        stock_address = 0x80453080 + slot * 0xE90 + 0x8E
        slices.append({"name": "fighter_stocks", "flags": slot, "address": stock_address,
                       "size": 1, "hex": "04"})
    return slices


def _boundary(name, seq, tick, *, slices=None):
    payload = {"boundary": name, "boundary_kind": 0, "whole_boundary_kind": 0,
               "whole_session": True, "match_index": 0, "slices": slices or []}
    return {"seq": seq, "source_tick": tick, "draw_ordinal": seq,
            "event": "boundary", "payload": payload}


def _profile_slices():
    rules = bytes(0x18)
    save = bytearray(0x55E8)
    save[0:2] = (0).to_bytes(2, "big")
    save[2:4] = (0).to_bytes(2, "big")
    css = bytes(0x148)
    ko = bytes(6)
    return [
        {"name": "profile_game_rules", "address": 0x80610000, "size": len(rules),
         "hex": rules.hex()},
        {"name": "profile_save_data", "address": 0x80620000, "size": len(save),
         "hex": bytes(save).hex()},
        {"name": "menu_css_context", "address": 0x80630000, "size": len(css),
         "hex": css.hex()},
        {"name": "menu_css_ko_counts", "address": 0x80640000, "size": len(ko),
         "hex": ko.hex()},
        {"name": "profile_characters", "address": 0x80650000, "size": 2, "hex": "0000"},
        {"name": "profile_stages", "address": 0x80650002, "size": 2, "hex": "0000"},
    ]


def _source_rows(fighter_count=4):
    consume = _pad_consume(0, 0x10, 0)
    consume["seq"] = 8
    consume["source_tick"] = 0
    consume["draw_ordinal"] = 8
    state_slices = _state_slices(fighter_count=fighter_count)
    css_slices = [item for item in state_slices
                  if item["name"] in {"pad_snapshot", "rng_pointer", "rng_value"}]
    css_slices += _profile_slices()
    setup_slices = state_slices + [{"name": "match_setup", "address": 0x80600000,
                                    "size": 0x138, "hex": bytes.fromhex(_whole_setup()["start_melee_hex"]).hex()}]
    setup = _boundary("setup", 7, 0, slices=setup_slices)
    setup["payload"]["gprs"] = [0] * 32
    setup["payload"]["gprs"][3] = 0x80600000
    tick = _boundary("source_tick", 9, 0, slices=state_slices)
    rows = [
        {"seq": 0, "source_tick": 0, "draw_ordinal": 0, "event": "handshake",
         "payload": {"whole_session": True, "capture_id": "capture-a", "sequence_id": "sequence-a"}},
        {"seq": 1, "source_tick": 0, "draw_ordinal": 0, "event": "start",
         "payload": {"whole_session": True, "capture_id": "capture-a", "sequence_id": "sequence-a",
                     "source_revision": "GALE01r2", "match_count": 1}},
        _boundary("css_enter", 2, 0, slices=css_slices),
        _boundary("css_exit", 3, 0),
        _boundary("sss_enter", 4, 0),
        _boundary("sss_exit", 5, 0),
        _boundary("entry", 6, 0),
        setup,
        consume,
        tick,
        _boundary("draw_return", 10, 0),
        _boundary("vs_exit", 11, 0),
        _boundary("vs_exit_return", 12, 0,
                  slices=[{"name": "result", "address": 0x80660000, "size": 4,
                           "hex": "00000000"}]),
        _boundary("vs_mode_exit", 13, 0),
        _boundary("results_enter", 14, 0),
        _boundary("results_gobj", 15, 0),
        _boundary("results_exit", 16, 0),
        _boundary("results_mode_exit", 17, 0),
        _boundary("scene_teardown", 18, 0),
        _boundary("return_css", 19, 0, slices=css_slices),
        {"seq": 20, "source_tick": 0, "draw_ordinal": 20, "event": "end",
         "payload": {"status": "completed", "natural": True}},
    ]
    return rows, _consumed_ports(consume, 0), state_slices, css_slices


def _browser_trace(path, pads, state, *, declared_setup=None, alter=None, rows_alter=None):
    setup = {"record": "session_match_enter_complete", "rng": state["rng"],
             "match_frame": state["match_frame"], "pad_state_hex": state["pad_state_hex"],
             "fighters": copy.deepcopy(state["fighters"])}
    if declared_setup is not None:
        setup["declared_setup"] = copy.deepcopy(declared_setup)
    if "fighter_entities" in state:
        setup["fighter_entities"] = copy.deepcopy(state["fighter_entities"])
    frame = {"record": "session_frame", "scene": SCENES["match"], "index": 0,
             "supplied_inputs": list(pads), "rng": state["rng"],
             "match_frame": state["match_frame"], "pad_state_hex": state["pad_state_hex"],
             "fighters": copy.deepcopy(state["fighters"])}
    if "fighter_entities" in state:
        frame["fighter_entities"] = copy.deepcopy(state["fighter_entities"])
    if alter:
        alter(setup, frame)
    rows = [
        {"record": "header", "schema": "melee-web-port-session-diagnostic", "version": 1,
         "frames_requested": 1, "comparison": "not_run", "cpu_observations": "not_captured",
         "draw_state": "not_captured"},
        setup, frame,
        {"record": "end", "frames": 1, "status": "captured"},
    ]
    if rows_alter:
        rows_alter(rows)
    path.write_text("\n".join(json.dumps(row, separators=(",", ":")) for row in rows) + "\n",
                    encoding="utf-8")
    return frame


def _v9_state(payload):
    state = _state_from_payload(payload, "v9 fixture")
    state.update(_snapshot_values(payload, "v9 fixture"))
    state["fighter_entities"] = _fighter_entities(payload, "v9 fixture", 0, {})
    return state


def _run_source(browser_path, source_rows, pads, *, finish=True, version=8):
    browser = BrowserReader(browser_path)
    recipe = type("RecipeFixture", (), {
        "frame_count": 1,
        "version": version,
        "frames": [{"index": 0, "scene": SCENES["match"], "pads": pads}],
        "spans": [{"scene": SCENES["match"], "first_frame": 0, "last_frame": 0}],
        "setup": bytes.fromhex(_whole_setup()["start_melee_hex"]),
        "match_setups": [bytes.fromhex(_whole_setup()["start_melee_hex"])],
        "seed": 0x12345678,
        "initial_pad": bytes.fromhex(PAD_HEX),
        "characters": 0,
        "stages": 0,
        "game_rules": bytes(0x18), "save_data": bytes(0x55E8),
        "css_data": bytes(0x148), "ko_counts": bytes(6),
    })()
    try:
        comparator = Comparator(recipe, browser)
        collector = SourceCollector(comparator, recipe)
        for row in source_rows:
            collector.consume(row)
        if finish:
            with mock.patch("whole_session_state_compare.semantics.validate_whole_session_observer_records"):
                collector.finish()
        return comparator, collector
    finally:
        browser.close()


class WholeSessionStateCompareTests(unittest.TestCase):
    def test_source_collector_and_comparator_match_setup_and_tick(self):
        source, pads, _, _ = _source_rows()
        # Build expected state through the same checked source decoder used by
        # SourceCollector; this exercises real setup/tick extraction.
        state_slices = source[9]["payload"]["slices"]
        state = _state_from_payload({"slices": state_slices}, "fixture")
        state.update(_snapshot_values({"slices": state_slices}, "fixture"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state)
            comparator, _ = _run_source(path, source, pads)
            self.assertEqual(comparator.setup_count, 1)
            self.assertEqual(comparator.compared, 1)
            self.assertEqual(comparator.match_compared, 1)

    def test_source_collector_compares_the_two_active_fighters_in_a_versus_match(self):
        source, pads, _, _ = _source_rows(fighter_count=2)
        state_slices = source[9]["payload"]["slices"]
        state = _state_from_payload({"slices": state_slices}, "two-player fixture")
        state.update(_snapshot_values({"slices": state_slices}, "two-player fixture"))
        self.assertEqual([fighter["slot"] for fighter in state["fighters"]], [0, 1])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state)
            comparator, _ = _run_source(path, source, pads)
            self.assertEqual(comparator.setup_count, 1)
            self.assertEqual(comparator.match_compared, 1)

    def test_source_collector_rejects_fewer_than_two_or_noncontiguous_fighter_heads(self):
        with self.assertRaises(ValueError):
            _state_from_payload({"slices": _state_slices(fighter_count=1)}, "one-player fixture")
        noncontiguous = _state_slices(fighter_count=2)
        next(item for item in noncontiguous
             if item["name"] == "fighter_head" and item["flags"] == 1)["flags"] = 2
        with self.assertRaises(ValueError):
            _state_from_payload({"slices": noncontiguous}, "noncontiguous fixture")

    def test_browser_trace_rejects_fighter_counts_outside_two_to_four(self):
        source, pads, _, _ = _source_rows(fighter_count=2)
        state_slices = source[9]["payload"]["slices"]
        state = _state_from_payload({"slices": state_slices}, "two-player fixture")
        state.update(_snapshot_values({"slices": state_slices}, "two-player fixture"))
        alterations = (
            lambda setup, frame: (setup["fighters"].pop(), frame["fighters"].pop()),
            lambda setup, frame: (setup["fighters"].extend([*copy.deepcopy(frame["fighters"]),
                                                               *copy.deepcopy(frame["fighters"])]),
                                  frame["fighters"].extend([*copy.deepcopy(frame["fighters"]),
                                                             *copy.deepcopy(frame["fighters"])])),
        )
        for alter in alterations:
            with self.subTest(alter=alter), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "trace.jsonl"
                _browser_trace(path, pads, state, alter=alter)
                with self.assertRaises(ValueError):
                    _run_source(path, source, pads)

    def test_auxiliary_fighter_flags_use_bounded_composite_identity(self):
        slices = _state_slices(fighter_count=2)
        auxiliary = copy.deepcopy(next(item for item in slices
                                       if item["name"] == "fighter_head" and item["flags"] == 1))
        auxiliary["flags"] = 0x101
        slices.append(auxiliary)
        state = _state_from_payload({"slices": slices}, "auxiliary fixture")
        self.assertEqual([fighter["slot"] for fighter in state["fighters"]], [0, 1])
        for invalid in (0x202, 0x104, 0x10000, 0x10001):
            malformed = copy.deepcopy(slices)
            malformed[-1]["flags"] = invalid
            with self.subTest(flags=hex(invalid)):
                with self.assertRaises(ValueError):
                    _state_from_payload({"slices": malformed}, "invalid auxiliary fixture")
    def test_v9_match_entry_binds_browser_to_that_matches_setup(self):
        source, pads, _, _ = _source_rows()
        payload = source[9]["payload"]
        state = _v9_state(payload)
        setup_payload = source[7]["payload"]
        setup_raw = next(item["hex"] for item in setup_payload["slices"]
                         if item["name"] == "match_setup")
        from whole_session_replay import _decode_setup
        declared = _decode_setup(setup_raw)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state, declared_setup=declared)
            comparator, _ = _run_source(path, source, pads, version=9, finish=False)
            self.assertEqual(comparator.setup_count, 1)

            def change_setup(setup, frame):
                setup["declared_setup"]["players"][0]["character_kind"] += 1
            _browser_trace(path, pads, state, declared_setup=declared, alter=change_setup)
            with self.assertRaisesRegex(ValueError, "declared setup: exact state differs"):
                _run_source(path, source, pads, version=9, finish=False)

    def test_v9_source_entity_coverage_rejects_missing_extra_reordered_and_secondary(self):
        source, _, state_slices, _ = _source_rows()
        from whole_session_state_compare import _fighter_entities
        cases = [
            (lambda values: values.remove(next(item for item in values
                                               if item["name"] == "player_entity_user_data")),
             "exactly four"),
            (lambda values: values.append(copy.deepcopy(next(item for item in values
                                                              if item["name"] == "player_entities"))),
             "exactly four"),
            (lambda values: values.__setitem__(slice(5, 7), reversed(values[5:7])),
             "missing, extra, or reordered"),
            (lambda values: next(item for item in values
                                 if item["name"] == "player_entities").__setitem__(
                                     "hex", "8068000000000001"), "secondary entity"),
            (lambda values: next(item for item in values
                                 if item["name"] == "fighter_head").__setitem__(
                                     "hex", "00" * 0x0C + "03" + "00" * (0x1000 - 0x0D)),
             "Fighter player_id"),
            (lambda values: next(item for item in values
                                 if item["name"] == "fighter_head").__setitem__(
                                     "hex", "00000000" + "00" * (0x0C - 4) + "00" +
                                     "00" * (0x1000 - 0x0D)),
             "GObj backlink"),
            (lambda values: next(item for item in values
                                 if item["name"] == "fighter_head").__setitem__(
                                     "hex", "80680100" + "00" * (0x0C - 4) + "00" +
                                     "00" * (0x1000 - 0x0D)),
             "GObj backlink"),
        ]
        for alter, message in cases:
            with self.subTest(message=message):
                payload = {"slices": copy.deepcopy(state_slices)}
                alter(payload["slices"])
                with self.assertRaisesRegex(ValueError, message):
                    _fighter_entities(payload, "negative fixture", 0, {})

    def test_v9_browser_entity_rows_reject_missing_extra_duplicate_reordered_or_malformed(self):
        source, pads, _, _ = _source_rows()
        payload = source[9]["payload"]
        state = _v9_state(payload)
        invalid_mutations = [
            lambda entries: entries.pop(),
            lambda entries: entries.append(copy.deepcopy(entries[0])),
            lambda entries: entries.__setitem__(1, copy.deepcopy(entries[0])),
            lambda entries: entries.reverse(),
            lambda entries: entries[0].__setitem__("extra", 1),
        ]
        setup_raw = next(item["hex"] for item in source[7]["payload"]["slices"]
                         if item["name"] == "match_setup")
        from whole_session_replay import _decode_setup
        declared = _decode_setup(setup_raw)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            for mutate in invalid_mutations:
                def alter(setup, frame):
                    mutate(setup["fighter_entities"])
                _browser_trace(path, pads, state, declared_setup=declared, alter=alter)
                with self.assertRaises(ValueError):
                    _run_source(path, source, pads, version=9)

    def test_v9_browser_entity_player_id_and_generation_are_compared(self):
        source, pads, _, _ = _source_rows()
        payload = source[9]["payload"]
        state = _v9_state(payload)
        setup_raw = next(item["hex"] for item in source[7]["payload"]["slices"]
                         if item["name"] == "match_setup")
        from whole_session_replay import _decode_setup
        declared = _decode_setup(setup_raw)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            for field, value in (("fighter_player_id", 1), ("generation", 1),
                                 ("fighter_gobj_linked", False)):
                def alter(setup, frame):
                    setup["fighter_entities"][0][field] = value
                _browser_trace(path, pads, state, declared_setup=declared, alter=alter)
                with self.assertRaises(ValueError):
                    _run_source(path, source, pads, version=9)

    def test_v9_comparison_metadata_declares_entity_identity(self):
        from whole_session_state_compare import comparison_fields
        self.assertEqual(comparison_fields(8),
                         ("rng", "match_frame", "pad_state_hex", "fighters"))
        self.assertEqual(comparison_fields(9),
                         ("rng", "match_frame", "pad_state_hex", "fighters",
                          "fighter_entities"))

    def test_real_comparator_rejects_reordered_input_cursor(self):
        source, pads, _, _ = _source_rows()
        state_slices = source[9]["payload"]["slices"]
        state = _state_from_payload({"slices": state_slices}, "fixture")
        state.update(_snapshot_values({"slices": state_slices}, "fixture"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state, alter=lambda setup, frame: frame["supplied_inputs"].__setitem__(0, "01" + "00" * 10))
            with self.assertRaises(ValueError):
                _run_source(path, source, pads)

    def test_every_declared_match_field_is_exact_at_setup_and_tick(self):
        source, pads, _, _ = _source_rows()
        payload = source[9]["payload"]
        state = _state_from_payload(payload, "fixture")
        state.update(_snapshot_values(payload, "fixture"))

        def different(value):
            if isinstance(value, str):
                return ("1" if value[0] == "0" else "0") + value[1:]
            if isinstance(value, list):
                return [different(value[0]), *value[1:]]
            return value + 1

        fields = ["rng", "match_frame", "pad_state_hex"]
        fields += ["fighters." + key for key in state["fighters"][0]]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            for record in (0, 1):
                for field in fields:
                    with self.subTest(record=record, field=field):
                        def alter(setup, frame):
                            target = (setup, frame)[record]
                            key = field
                            if field.startswith("fighters."):
                                target = target["fighters"][0]
                                key = field.split(".", 1)[1]
                            target[key] = different(target[key])
                        _browser_trace(path, pads, state, alter=alter)
                        with self.assertRaises(ValueError):
                            _run_source(path, source, pads)

    def test_raw_source_envelope_requires_zero_origin_and_announcement_order(self):
        source, pads, _, _ = _source_rows()
        payload = source[9]["payload"]
        state = _state_from_payload(payload, "fixture")
        state.update(_snapshot_values(payload, "fixture"))
        variants = []
        shifted = copy.deepcopy(source)
        for row in shifted:
            row["seq"] += 1
        variants.append(shifted)
        reordered = copy.deepcopy(source)
        reordered[0], reordered[1] = reordered[1], reordered[0]
        for index, row in enumerate(reordered):
            row["seq"] = index
        variants.append(reordered)
        missing_start = copy.deepcopy(source)
        del missing_start[1]
        for index, row in enumerate(missing_start):
            row["seq"] = index
        variants.append(missing_start)
        gapped = copy.deepcopy(source)
        gapped[9]["seq"] += 1
        variants.append(gapped)
        variants.append(copy.deepcopy(source[:-1]))
        after_end = copy.deepcopy(source)
        after_end.append({"seq": len(source), "event": "boundary", "payload": {}})
        variants.append(after_end)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state)
            for index, rows in enumerate(variants):
                with self.subTest(variant=index), self.assertRaises(ValueError):
                    _run_source(path, rows, pads)

    def test_real_comparator_rejects_missing_or_extra_terminal_records(self):
        source, pads, _, _ = _source_rows()
        state = _state_from_payload({"slices": source[9]["payload"]["slices"]}, "fixture")
        state.update(_snapshot_values({"slices": source[9]["payload"]["slices"]}, "fixture"))
        for rows_alter in (
                lambda rows: rows.pop(),
                lambda rows: rows.append({"record": "extra"}),
        ):
            with self.subTest(rows_alter=rows_alter):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "trace.jsonl"
                    _browser_trace(path, pads, state, rows_alter=rows_alter)
                    with self.assertRaises(ValueError):
                        _run_source(path, source, pads)

    def test_real_comparator_rejects_missing_or_extra_session_frames(self):
        source, pads, _, _ = _source_rows()
        state = _state_from_payload({"slices": source[9]["payload"]["slices"]}, "fixture")
        state.update(_snapshot_values({"slices": source[9]["payload"]["slices"]}, "fixture"))
        for rows_alter in (
                lambda rows: rows.pop(2),
                lambda rows: rows.insert(-1, copy.deepcopy(rows[2])),
                lambda rows: rows.__setitem__(slice(1, 3), [rows[2], rows[1]]),
        ):
            with self.subTest(rows_alter=rows_alter):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "trace.jsonl"
                    _browser_trace(path, pads, state, rows_alter=rows_alter)
                    with self.assertRaises(ValueError):
                        _run_source(path, source, pads)

    def test_real_comparator_rejects_setup_omission_and_noncontiguous_tick(self):
        source, pads, _, _ = _source_rows()
        state = _state_from_payload({"slices": source[9]["payload"]["slices"]}, "fixture")
        state.update(_snapshot_values({"slices": source[9]["payload"]["slices"]}, "fixture"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state, rows_alter=lambda rows: rows.pop(1))
            with self.assertRaises(ValueError):
                _run_source(path, source, pads)
        source[9]["source_tick"] = 2
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state)
            with self.assertRaises(ValueError):
                _run_source(path, source, pads)

    def test_browser_header_and_nested_match_field_guards(self):
        source, pads, _, _ = _source_rows()
        state = _state_from_payload({"slices": source[9]["payload"]["slices"]}, "fixture")
        state.update(_snapshot_values({"slices": source[9]["payload"]["slices"]}, "fixture"))
        for alter in (
                lambda rows: rows[0].__setitem__("frames_requested", 2),
                lambda rows: rows[0].__setitem__("cpu_observations", "captured"),
        ):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "trace.jsonl"
                _browser_trace(path, pads, state, rows_alter=alter)
                with self.assertRaises(ValueError):
                    _run_source(path, source, pads)
        self.assertTrue(_is_match_field("fighters[0].position_bits[0]"))
        self.assertTrue(_is_match_field("match_frame"))
        self.assertFalse(_is_match_field("supplied_inputs"))

    def test_v8_primary_state_rejects_unscoped_entity_rows(self):
        source, pads, _, _ = _source_rows()
        state = _state_from_payload({"slices": source[9]["payload"]["slices"]}, "fixture")
        state.update(_snapshot_values({"slices": source[9]["payload"]["slices"]}, "fixture"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.jsonl"
            _browser_trace(path, pads, state,
                           rows_alter=lambda rows: rows[2].__setitem__("fighter_entities", []))
            with self.assertRaisesRegex(ValueError, "fields differ"):
                _run_source(path, source, pads)

    def test_browser_completion_report_requires_bound_final_css_endpoint(self):
        report = {
            "schema": "melee-web-browser-retail-replay", "version": 1,
            "recipe_sha256": "r", "trace_sha256": "t", "frames": 1,
            "final_scene": 1, "complete": True, "pass": True,
            "metrics": {"sourceFrames": 1, "sourceSteps": 1, "sourceDraws": 1},
        }
        _validate_browser_report(report, recipe_sha="r", trace_sha="t", frame_count=1)
        self.assertTrue(_browser_completion_ok(report))
        with self.assertRaises(ValueError):
            _validate_browser_report(report, recipe_sha="wrong", trace_sha="t", frame_count=1)
        with self.assertRaises(ValueError):
            _validate_browser_report(report, recipe_sha="r", trace_sha="wrong", frame_count=1)
        report["final_scene"] = None
        _validate_browser_report(report, recipe_sha="r", trace_sha="t", frame_count=1)
        self.assertFalse(_browser_completion_ok(report))


if __name__ == "__main__":
    unittest.main()
