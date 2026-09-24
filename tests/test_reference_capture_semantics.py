import importlib.util
from copy import deepcopy
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "reference-capture" / "dolphin"))
import reference_capture_semantics as semantics  # noqa: E402
import reference_observer_stream as observer_stream  # noqa: E402


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


def _whole_pad_state():
    config = bytes.fromhex(
        "0000002d00000008001e0000000000007f0000ff0000ff007fffff000000")
    return (config + bytes(12 * 66)).hex()


def _whole_setup():
    raw = bytearray(0x138)
    raw[4] = 0x40
    raw[0x2C:0x30] = bytes.fromhex("3f800000")
    for index in range(6):
        base = 0x60 + index * 0x24
        if index < 2:
            raw[base] = 8
            raw[base + 1] = 0 if index == 0 else 1
            raw[base + 2] = 4
            raw[base + 4] = 0
            if index == 1:
                raw[base + 14] = 4
                raw[base + 15] = 9
        else:
            raw[base + 1] = 3
    start_melee_hex = raw.hex()
    return {"start_melee_hex": start_melee_hex,
            "declared_setup": semantics._decode_setup(start_melee_hex)}


def _whole_rows(match_count=3):
    sequence_id = "sequence-36"
    capture_id = "capture-36"
    rows = [{"record": "header", "schema": semantics.WHOLE_SESSION_SCHEMA,
             "version": semantics.WHOLE_SESSION_VERSION,
             "sequence_id": sequence_id, "capture_id": capture_id,
             "match_count": match_count, "source_revision": "GALE01r2",
             "observer_schema": "melee-web-passive-dolphin-observer",
             "observer_version": 1}]

    trace_events = {"css_enter": "capture_begin", "css_exit": "css_exit_complete",
                    "sss_enter": "sss_enter_complete", "sss_exit": "sss_exit_complete",
                    "return_css": "css_enter_complete"}
    observer_boundaries = {"vs_enter": ("setup", 0x8016E9C4),
                           "final_draw": ("draw_return", 0x80391040),
                           "scene_reset": ("scene_teardown", 0x8039157C)}
    source_hooks = {
        "vs_exit": [("gm_Scene_Vs_OnExit", 0x8016E9C8),
                    ("gmVsMelee_ExitVs", 0x801A5AF0)],
        "results_enter": [("gm_Scene_Results_OnEnter", 0x80177368)],
        "results_gobj": [("results_gobj_procs", None)],
        "results_exit": [("gm_Scene_Results_OnExit", 0x80177704),
                         ("gmVsMelee_ExitResults", 0x801A5F64)],
    }

    def lifecycle(event, match_index):
        if event in trace_events:
            route = "match" if event == "sss_exit" else None
            value = {"kind": "transition_trace", "event": trace_events[event],
                     "run": 0, "index": match_index, "route": route}
            menu = {"trace_event": trace_events[event], "run": 0,
                    "index": match_index, "route": route,
                    "audio": {"active": True, "owner_epoch": 0,
                               "stream": "menu01.hps"}, "selection": None}
            return value, menu
        if event in observer_boundaries:
            boundary, pc = observer_boundaries[event]
            return ({"kind": "passive_observer", "boundary": boundary, "seq": match_index,
                     "source_tick": match_index, "draw_ordinal": match_index, "pc": pc}, None)
        return ({"kind": "source_hooks",
                 "hooks": [{"name": name, "pc": pc} for name, pc in source_hooks[event]],
                 "source_tick": match_index, "draw_ordinal": match_index}, None)

    def boundary(event, match_index, setup=None, result=None):
        lifecycle_value, menu_context = lifecycle(event, match_index)
        draw = ({"source_index": match_index, "draw_ordinal": match_index}
                if event == "final_draw" else None)
        exit_value = ({"scene_request": 1, "source_ticks": 60 + match_index}
                      if event == "vs_exit" else None)
        teardown = ({"released_fighter_slots": [0, 1], "remaining_fighter_slots": [],
                     "entity_list_count": 2, "entity_heads_hex": "00" * 8}
                    if event == "scene_reset" else None)
        return {
            "record": "boundary", "schema": semantics.WHOLE_SESSION_SCHEMA,
            "version": semantics.WHOLE_SESSION_VERSION,
            "sequence_id": sequence_id, "capture_id": capture_id,
            "event": event, "match_index": match_index,
            "snapshot": {"pad_state_hex": _whole_pad_state(),
                          "rng": 0x100 + match_index,
                          "setup": deepcopy(setup),
                          "lifecycle": lifecycle_value,
                          "menu_context": menu_context},
            "result": deepcopy(result),
            "draw": draw, "exit": exit_value, "teardown": teardown,
        }

    rows.append(boundary("css_enter", 0))
    for match_index in range(match_count):
        rows.append(boundary("css_exit", match_index))
        rows.append(boundary("sss_enter", match_index))
        rows.append(boundary("sss_exit", match_index))
        setup = _whole_setup()
        rows.append(boundary("vs_enter", match_index, setup))
        rows.append(boundary("final_draw", match_index, setup))
        result = ({"outcome": 2, "winners": [0]} if match_index == 0 else
                  {"outcome": 7, "winners": []} if match_index == 1 else
                  {"outcome": 1, "winners": [0]})
        rows.append(boundary("vs_exit", match_index, setup, result))
        rows.append(boundary("results_enter", match_index, setup, result))
        rows.append(boundary("results_gobj", match_index, setup, result))
        rows.append(boundary("results_exit", match_index, setup, result))
        rows.append(boundary("scene_reset", match_index, setup, result))
        rows.append(boundary("return_css", match_index))
    return rows


def _observer_frame(event, sequence, payload, *, pc=0, source_tick=0,
                    draw_ordinal=0):
    header = observer_stream.HEADER.pack(
        observer_stream.MAGIC_U32, observer_stream.SCHEMA_VERSION, event,
        sequence, sequence + 10, pc, source_tick, draw_ordinal,
        len(payload), zlib.crc32(payload) & 0xffffffff)
    return header + payload


def _typed_profile_context_slices(kind):
    """Authored GameRules and save-block bytes, published at CSS entry."""
    if kind != 13:
        return b"", b""
    rules = bytearray(semantics.GAME_RULES_SIZE)
    rules[0x02] = 1                      # mode
    rules[0x04] = 4                      # stock_count
    rules[0x14:0x18] = (-3).to_bytes(4, "big", signed=True)
    save = bytearray(semantics.SAVE_DATA_SIZE)
    save[0:2] = (0x0005).to_bytes(2, "big")                  # unlocked characters 0 and 2
    save[2:4] = (0x0002).to_bytes(2, "big")                  # unlocked stage 1
    save[4:5] = (0x21).to_bytes(1, "big")                    # unlocked features 0 and 5
    counters = dict(semantics.SAVE_MATCH_COUNTERS)
    save[counters["stock_matches"]:counters["stock_matches"] + 4] = (7).to_bytes(4, "big")
    save[semantics.SAVE_TROPHY_COUNT:semantics.SAVE_TROPHY_COUNT + 2] = (2).to_bytes(2, "big", signed=True)
    save[semantics.SAVE_TROPHY_CATEGORY_FLAGS:semantics.SAVE_TROPHY_CATEGORY_FLAGS + 2] = \
        (0x0003).to_bytes(2, "big")
    save[semantics.SAVE_TROPHY_FLAGS + 2 * 41:semantics.SAVE_TROPHY_FLAGS + 2 * 41 + 2] = \
        (1).to_bytes(2, "big")
    return bytes(rules), bytes(save)


def _typed_css_context_slices(kind):
    """Complete source CSSData plus its authored six-byte KO owner."""
    if kind != 13:
        return b"", b""
    css_address = 0x80530000
    ko_address = 0x80540000
    start = bytearray.fromhex(_whole_setup()["start_melee_hex"])
    # Initial ordinary VS CSS is a two-human, untimed character-select
    # payload.  The test keeps all pointer/callback bytes zero so the runtime
    # decoder can prove its known-owner translation independently.
    start[0] = 2 << 2
    start[0x0E:0x10] = (0x20).to_bytes(2, "big")
    for index in range(6):
        base = 0x60 + index * 0x24
        if index < 2:
            start[base] = 8
            start[base + 1] = 0
            start[base + 2] = 0
            start[base + 4] = 0
            start[base + 0x0C] = 0x80
        else:
            start[base + 1] = 3
    css = bytearray(0x148)
    css[0:2] = (1).to_bytes(2, "big")
    css[2] = 0
    css[3] = 0
    css[4:8] = ko_address.to_bytes(4, "big")
    css[8:16] = bytes.fromhex("ffffffff00000000")
    css[0x10:] = start
    return bytes(css), bytes.fromhex("010203040506")


def _observer_boundary(kind, match_index, *, result=False):
    raw = b"result" if result else b"state"
    tag = 15 if result else 31
    rules, save = _typed_profile_context_slices(kind)
    css, ko = _typed_css_context_slices(kind)
    # Descriptors and payload bytes share one order: the reader rejects a gap
    # or a reordered slice, so the typed context follows the existing masks.
    published = [(tag, 0, 0x80479D98 if result else 0x804D6CC0, raw),
                 (36, 0, 0x8045C538, b"\x07\xff"),
                 (37, 0, 0x8045C53A, b"\x01\xc0")]
    if rules:
        published += [(38, 0, 0x804D1850, rules), (39, 0, 0x804D1868, save)]
    if css:
        published += [(50, 0, 0x80530000, css), (51, 0, 0x80540000, ko)]
    prefix = observer_stream.BOUNDARY.pack(
        kind, observer_stream.WHOLE_SESSION_FLAG, 0x80300000, 32, len(published), 0)
    registers = list(range(32))
    if css:
        registers[3] = 0x80530000
    gprs = struct.pack("<32I", *registers)
    offset = (observer_stream.BOUNDARY.size + 32 * 4 +
              observer_stream.SLICE.size * len(published))
    descriptors = b""
    payload = b""
    for slice_tag, flags, address, value in published:
        descriptors += observer_stream.SLICE.pack(slice_tag, flags, address, len(value), offset)
        offset += len(value)
        payload += value
    metadata = observer_stream.WHOLE_METADATA.pack(match_index, kind, 0)
    return prefix + gprs + descriptors + payload + metadata


def _decoded_observer_rows(match_count=3, *, include_prize=False,
                            include_startup_prize=False):
    kinds = (
        (14, "css_exit"), (15, "sss_enter"),
        (16, "sss_exit"), (4, "entry"), (5, "setup"), (8, "draw_return"),
        (17, "vs_exit"), (18, "vs_exit_return"), (19, "vs_mode_exit"),
        (20, "results_enter"), (23, "results_gobj"), (21, "results_exit"),
        (22, "results_mode_exit"), (11, "scene_teardown"), (24, "return_css"),
    )
    if include_prize:
        kinds = kinds[:-2] + (
            (26, "prize_mode_enter"), (11, "scene_teardown"), (27, "prize_scene_enter"),
            (28, "prize_scene_exit"), (29, "prize_mode_exit"), (24, "return_css"),
        )
    pcs = {
        13: 0x8026688C, 14: 0x80266D70, 15: 0x8025A998, 16: 0x8025BBD0,
        4: 0x8016E934, 5: 0x8016E9C4, 8: 0x80391040, 17: 0x8016E9C8,
        18: 0x8016EBBC, 19: 0x801A5AF0, 20: 0x80177368, 21: 0x80177704,
        22: 0x801A5F64, 23: 0x80179350, 11: 0x8039157C,
        24: 0x8026688C, 26: 0x801BFCFC, 27: 0x802FEBE0,
        28: 0x802FED10, 29: 0x801A6308, 30: 0x801BFF7C,
    }
    stream_bytes = _observer_frame(
        1, 0, json.dumps({"schema": "melee-web-passive-dolphin-observer",
                          "version": 1, "whole_session": True,
                          "match_count": match_count, "capture_id": "capture-36",
                          "sequence_id": "sequence-36"}).encode())
    stream_bytes += _observer_frame(
        2, 1, json.dumps({"whole_session": True, "match_count": match_count,
                          "capture_id": "capture-36", "sequence_id": "sequence-36"}).encode())
    sequence = 2
    for match_index in range(match_count):
        startup = (
            ((26, "prize_mode_enter"), (27, "prize_scene_enter"),
             (28, "prize_scene_exit"), (30, "startup_prize_mode_exit"))
            if match_index == 0 and include_startup_prize else ())
        match_kinds = (startup + ((13, "css_enter"),) + kinds
                       if match_index == 0 else kinds)
        for kind, _name in match_kinds:
            stream_bytes += _observer_frame(
                3, sequence,
                _observer_boundary(kind, match_index, result=kind in (17, 18, 23)),
                pc=pcs[kind], source_tick=sequence, draw_ordinal=sequence)
            sequence += 1
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "whole.mwro"
        path.write_bytes(stream_bytes)
        return list(observer_stream.iter_records(path))


class ReferenceCaptureSemanticsTests(unittest.TestCase):
    def test_title_demo_entry_preserves_sequence_without_arming_a_match(self):
        session = semantics.SemanticSession()
        registers = [0] * 32
        registers[3] = 0x80500000
        setup = bytearray(0x138)
        setup[0x61] = setup[0x85] = 1
        row = {"seq": 17, "source_tick": 0, "draw_ordinal": 0,
               "event": "boundary", "payload": {"pc": 0x8016E934, "boundary": "entry",
                   "gprs": registers, "slices": [{"address": registers[3], "hex": setup.hex()}]}}
        result = session.consume(row)
        self.assertEqual(result["event"], "non_vs_entry")
        self.assertEqual(result["seq"], 17)
        self.assertFalse(result["payload"]["versus_match"])
        self.assertIsNone(session.enter)
        setup[4] = 0x40
        row["payload"]["slices"][0]["hex"] = setup.hex()
        with self.assertRaisesRegex(semantics.SemanticError, "one human P1"):
            session.consume(row)

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

    def test_whole_session_reports_typed_first_css_profile_context(self):
        report = semantics.validate_whole_session_observer_records(_decoded_observer_rows())
        context = report["matches"][0]["loaded_profile_context"]
        self.assertEqual(context["boundary"], "css_enter")
        self.assertEqual(context["game_rules"]["mode"], 1)
        self.assertEqual(context["game_rules"]["stock_count"], 4)
        self.assertEqual(context["game_rules"]["unk_14"], -3)
        self.assertEqual(context["unlocked_characters"]["set"], [0, 2])
        self.assertEqual(context["unlocked_stages"]["set"], [1])
        self.assertEqual(context["unlocked_features"]["set"], [0, 5])
        self.assertEqual(context["match_counters"]["stock_matches"], 7)
        self.assertEqual(context["trophy_count"], 2)
        self.assertEqual(context["trophy_category_flags"], 3)
        self.assertEqual([index for index, flag in enumerate(context["trophy_flags"]) if flag], [41])
        self.assertNotIn("final_profile_semantics", report["missing_coverage"])
        self.assertIn("persistent_record_semantics", report["missing_coverage"])

    def test_whole_session_reports_missing_profile_semantics_for_untyped_stream(self):
        rows = _decoded_observer_rows()
        for row in rows:
            if row["payload"].get("boundary") == "css_enter":
                row["payload"]["slices"] = [item for item in row["payload"]["slices"]
                                            if item["name"] not in {"profile_game_rules",
                                                                    "profile_save_data"}]
        report = semantics.validate_whole_session_observer_records(rows)
        self.assertIsNone(report["matches"][0]["loaded_profile_context"])
        self.assertIn("final_profile_semantics", report["missing_coverage"])

    def test_whole_session_preserves_three_match_boundary_sequence(self):
        report = semantics.validate_whole_session(_whole_rows())
        self.assertTrue(report["complete"])
        self.assertEqual(report["sequence_id"], "sequence-36")
        self.assertEqual(report["match_count"], 3)
        self.assertTrue(report["experimental"])
        self.assertFalse(report["accepted_for_reference_bundle"])
        self.assertEqual(report["event_count"], 34)
        self.assertEqual(report["transition_order"], [
            "css_enter", "css_exit", "sss_enter", "sss_exit", "vs_enter",
            "final_draw", "vs_exit", "results_enter", "results_gobj",
            "results_exit", "scene_reset", "return_css",
            "css_exit", "sss_enter", "sss_exit", "vs_enter", "final_draw",
            "vs_exit", "results_enter", "results_gobj", "results_exit",
            "scene_reset", "return_css",
            "css_exit", "sss_enter", "sss_exit", "vs_enter", "final_draw",
            "vs_exit", "results_enter", "results_gobj", "results_exit",
            "scene_reset", "return_css",
        ])
        self.assertEqual([match["match_index"] for match in report["matches"]], [0, 1, 2])
        self.assertEqual(report["matches"][1]["result"],
                         {"outcome": 7, "winners": []})
        self.assertEqual(report["claims"]["original_repeatability"], "not_evaluated")

    def test_whole_session_rejects_v1_header_instead_of_promoting_single_match(self):
        row = _whole_rows()[0]
        row["schema"] = "melee-web-retail-replay-candidate"
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "Unsupported whole-session header"):
            semantics.validate_whole_session([row])

    def test_whole_session_rejects_missing_result_and_out_of_order_boundary(self):
        rows = _whole_rows()
        rows.pop(next(index for index, row in enumerate(rows)
                      if row.get("event") == "results_enter" and row["match_index"] == 1))
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "Expected results_enter for match 1"):
            semantics.validate_whole_session(rows)

        rows = _whole_rows()
        vs_index = next(index for index, row in enumerate(rows)
                        if row.get("event") == "vs_enter" and row["match_index"] == 0)
        rows.insert(vs_index + 1, deepcopy(rows[vs_index]))
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "Expected final_draw for match 0"):
            semantics.validate_whole_session(rows)

    def test_whole_session_rejects_spliced_capture_and_missing_teardown(self):
        rows = _whole_rows()
        rows[6]["capture_id"] = "single-match-capture"
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "spliced captures"):
            semantics.validate_whole_session(rows)

        rows = _whole_rows()
        rows.pop(next(index for index, row in enumerate(rows)
                      if row.get("event") == "scene_reset" and row["match_index"] == 2))
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "Expected scene_reset for match 2"):
            semantics.validate_whole_session(rows)

    def test_whole_session_requires_each_boundary_snapshot(self):
        rows = _whole_rows()
        rows[1]["snapshot"]["pad_state_hex"] = "00" * 822
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "Invalid PAD processing configuration"):
            semantics.validate_whole_session(rows)

        rows = _whole_rows()
        vs_index = next(index for index, row in enumerate(rows)
                        if row.get("event") == "vs_enter")
        rows[vs_index]["snapshot"]["setup"]["declared_setup"]["stage"] = 99
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "disagrees with StartMeleeData"):
            semantics.validate_whole_session(rows)

    def test_whole_session_rejects_none_outcome_and_untyped_lifecycle(self):
        rows = _whole_rows()
        vs_exit = next(index for index, row in enumerate(rows)
                       if row.get("event") == "vs_exit")
        rows[vs_exit]["result"]["outcome"] = 0
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "supported terminal source outcome"):
            semantics.validate_whole_session(rows)

        rows = _whole_rows()
        rows[1]["snapshot"]["lifecycle"] = {"phase": "css_enter"}
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "missing or unrecognized fields"):
            semantics.validate_whole_session(rows)

    def test_whole_session_preserves_tied_no_contest_result_slots(self):
        rows = _whole_rows()
        no_contest = next(index for index, row in enumerate(rows)
                          if row.get("event") == "vs_exit" and row["match_index"] == 1)
        for row in rows:
            if row.get("match_index") == 1 and row.get("result") is not None:
                row["result"]["winners"] = [0, 1]
        report = semantics.validate_whole_session(rows)
        self.assertEqual(report["matches"][1]["result"],
                         {"outcome": 7, "winners": [0, 1]})

    def test_whole_session_rejects_missing_results_exit_hook(self):
        rows = _whole_rows()
        results_exit = next(index for index, row in enumerate(rows)
                            if row.get("event") == "results_exit")
        rows[results_exit]["snapshot"]["lifecycle"]["hooks"].pop()
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "source-hook order is incomplete"):
            semantics.validate_whole_session(rows)

    def test_observer_adapter_reports_real_boundaries_as_explicitly_incomplete(self):
        records = _decoded_observer_rows()
        report = semantics.validate_whole_session_observer_records(records)
        self.assertFalse(report["complete"])
        self.assertFalse(report["accepted_for_reference_bundle"])
        self.assertEqual(report["match_count"], 3)
        self.assertEqual([item["match_index"] for item in report["matches"]], [0, 1, 2])
        self.assertIn("menu_audio_owner_epoch", report["missing_coverage"])
        self.assertEqual(report["matches"][2]["return_css_seq"],
                         report["matches"][2]["scene_reset_seq"] + 1)
        self.assertEqual(report["capture_identity"],
                         {"capture_id": "capture-36", "sequence_id": "sequence-36"})

    def test_observer_adapter_requires_completed_return_result(self):
        records = _decoded_observer_rows()
        returned = next(row for row in records
                        if row.get("payload", {}).get("boundary") == "vs_exit_return")
        returned["payload"]["slices"] = [
            item for item in returned["payload"]["slices"] if item["name"] != "result"]
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "VS exit return lacks its completed source Result"):
            semantics.validate_whole_session_observer_records(records)

    def test_observer_adapter_preserves_repeated_results_process_frames(self):
        records = _decoded_observer_rows()
        position = next(i for i, row in enumerate(records)
                        if row.get("payload", {}).get("boundary") == "results_gobj")
        repeated = deepcopy(records[position])
        records.insert(position + 1, repeated)
        report = semantics.validate_whole_session_observer_records(records)
        self.assertEqual(report["matches"][0]["boundary_order"].count("results_gobj"), 2)
        records.pop(position + 1)
        exit_position = next(i for i, row in enumerate(records)
                             if row.get("payload", {}).get("boundary") == "results_exit")
        records.insert(exit_position + 1, repeated)
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "out-of-order source lifecycle"):
            semantics.validate_whole_session_observer_records(records)

    def test_observer_adapter_requires_source_loaded_profile_masks(self):
        records = _decoded_observer_rows()
        first_css = next(row for row in records
                         if row.get("payload", {}).get("boundary") == "css_enter")
        first_css["payload"]["slices"] = [
            item for item in first_css["payload"]["slices"]
            if item["name"] != "profile_stages"
        ]
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "loaded profile_stages"):
            semantics.validate_whole_session_observer_records(records)

    def test_observer_adapter_retains_optional_prize_lifecycle(self):
        records = _decoded_observer_rows(include_prize=True)
        report = semantics.validate_whole_session_observer_records(records)
        prize = [row for row in report["matches"][0]["boundary_order"]
                  if row.startswith("prize_")]
        self.assertEqual(prize, ["prize_mode_enter", "prize_scene_enter",
                                 "prize_scene_exit", "prize_mode_exit"])
        self.assertEqual(report["matches"][0]["loaded_profile_masks"]["css_enter"],
                         {"characters": 0x07FF, "stages": 0x01C0})

    def test_observer_adapter_retains_startup_prize_as_separate_prelude(self):
        records = _decoded_observer_rows(include_startup_prize=True)
        report = semantics.validate_whole_session_observer_records(records)
        self.assertEqual(report["matches"][0]["startup_prize_prelude"], [
            "prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
            "startup_prize_mode_exit",
        ])
        self.assertEqual(report["matches"][0]["boundary_order"][:4],
                         report["matches"][0]["startup_prize_prelude"])
        self.assertEqual(report["matches"][0]["boundary_order"][4], "css_enter")
        self.assertEqual(report["matches"][0]["loaded_profile_masks"]["css_enter"],
                         {"characters": 0x07FF, "stages": 0x01C0})

    def test_observer_adapter_rejects_startup_prize_on_later_match(self):
        records = _decoded_observer_rows()
        # A later match must not acquire a pre-CSS lifecycle by relabeling its
        # first source boundary; the observer's match index is part of the
        # same-capture ordering contract.
        row = records[2].copy()
        row["payload"] = {"boundary": "startup_prize_mode_exit", "boundary_kind": 30,
                          "pc": 0x801BFF7C, "whole_session": True,
                          "whole_boundary_kind": 30, "match_index": 1,
                          "slices": records[2]["payload"]["slices"]}
        insert_at = next(index for index, item in enumerate(records)
                         if item.get("payload", {}).get("match_index") == 1)
        records.insert(insert_at, row)
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "startup Prize prelude"):
            semantics.validate_whole_session_observer_records(records)

    def test_observer_adapter_joins_same_capture_menu_trace_and_audio_epoch(self):
        records = _decoded_observer_rows()
        menu_names = {"css_enter", "css_exit", "sss_enter", "sss_exit", "return_css"}
        for row in records:
            payload = row.get("payload", {})
            if payload.get("boundary") not in menu_names:
                continue
            payload["audio_owner_epoch"] = 0
            payload["slices"].extend([
                {"name": "menu_audio", "hex": (b"menu01.hps\0" + bytes(55)).hex()},
                {"name": "menu_audio_voice", "hex": "00000001"},
            ])
            if payload["boundary"] == "sss_exit":
                payload["slices"].append({"name": "menu_sss_route", "hex": "01"})
        trace = [{"record": "header", "schema": "melee-web-transition-trace", "version": 1,
                  "capture_id": "capture-36", "sequence_id": "sequence-36"}]
        event_index = 0
        for row in records:
            boundary = row.get("payload", {}).get("boundary")
            mapping = {"css_enter": "capture_begin", "css_exit": "css_exit_complete",
                       "sss_enter": "sss_enter_complete", "sss_exit": "sss_exit_complete",
                       "entry": "match_enter_complete"}
            if boundary not in mapping:
                continue
            event = {"record": "event", "run": 0, "index": event_index,
                     "event": mapping[boundary], "capture_id": "capture-36",
                     "sequence_id": "sequence-36",
                     "audio": {"active": True, "owner_epoch": 0,
                               "stream": "menu01.hps"}}
            if boundary == "sss_exit":
                event["route"] = "match"
            trace.append(event)
            event_index += 1
        report = semantics.validate_whole_session_observer_records(
            records, transition_trace=trace)
        self.assertTrue(report["complete"])
        self.assertEqual(report["observer_integration"], "complete")
        self.assertEqual(len(report["transition_join"]), event_index)
        self.assertIn("audio_pcm", report["missing_coverage"])
        self.assertFalse(report["accepted_for_reference_bundle"])

        for omitted in ("capture_begin", "css_exit_complete", "sss_enter_complete"):
            with self.subTest(omitted=omitted):
                incomplete = deepcopy(trace)
                incomplete.pop(next(index for index, row in enumerate(incomplete)
                                    if row.get("event") == omitted))
                for index, row in enumerate(incomplete[1:]):
                    row["index"] = index
                with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                            "next passive boundary"):
                    semantics.validate_whole_session_observer_records(
                        records, transition_trace=incomplete)

    def test_observer_adapter_rejects_transition_trace_from_another_capture(self):
        records = _decoded_observer_rows()
        for row in records:
            if row.get("payload", {}).get("boundary") in {
                    "css_enter", "css_exit", "sss_enter", "sss_exit", "return_css"}:
                row["payload"]["audio_owner_epoch"] = 0
        trace = [{"record": "header", "schema": "melee-web-transition-trace", "version": 1,
                  "capture_id": "other-capture", "sequence_id": "sequence-36"}]
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError, "identity"):
            semantics.validate_whole_session_observer_records(records, transition_trace=trace)

    def test_observer_adapter_retains_sss_cancel_cycle_before_match(self):
        records = _decoded_observer_rows()
        match_zero = [index for index, row in enumerate(records)
                      if row.get("payload", {}).get("match_index") == 0]
        first_exit = next(index for index in match_zero
                          if records[index]["payload"].get("boundary") == "sss_exit")
        entry = next(index for index in match_zero
                     if records[index]["payload"].get("boundary") == "entry")
        cancel = deepcopy(records[first_exit])
        cancel["payload"]["boundary"] = "css_cancel_enter"
        cancel["payload"]["boundary_kind"] = 25
        cancel["payload"]["whole_boundary_kind"] = 25
        cancel["payload"]["pc"] = 0x8026688C
        repeated = [deepcopy(records[index]) for index in (first_exit - 2, first_exit - 1, first_exit)]
        records[first_exit + 1:first_exit + 1] = [cancel, *repeated]
        # The retained fixture has no extended audio metadata, so this test
        # exercises the source order reducer while preserving incomplete
        # capture evidence semantics.
        report = semantics.validate_whole_session_observer_records(records)
        self.assertIn("transition_trace_css_sss_join", report["missing_coverage"])
        names = report["matches"][0]["boundary_order"]
        self.assertIn("css_cancel_enter", names)
        self.assertLess(names.index("css_cancel_enter"), names.index("entry"))

    def test_observer_adapter_rejects_missing_or_duplicate_source_boundaries(self):
        records = _decoded_observer_rows()
        records.pop(next(index for index, row in enumerate(records)
                         if row["payload"].get("boundary") == "results_exit"
                         and row["payload"]["match_index"] == 1))
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "missing or duplicate results_exit"):
            semantics.validate_whole_session_observer_records(records)

        records = _decoded_observer_rows()
        duplicate = next(row for row in records
                         if row["payload"].get("boundary") == "vs_mode_exit"
                         and row["payload"]["match_index"] == 0)
        records.insert(records.index(duplicate) + 1, duplicate)
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "missing or duplicate vs_mode_exit"):
            semantics.validate_whole_session_observer_records(records)

        records = _decoded_observer_rows()
        bad_pc = next(row for row in records
                      if row["payload"].get("boundary") == "results_enter")
        bad_pc["payload"]["pc"] = 0x1234
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "unpinned source PC"):
            semantics.validate_whole_session_observer_records(records)

        records = _decoded_observer_rows()
        records.pop(next(index for index, row in enumerate(records)
                         if row["payload"].get("boundary") == "return_css"
                         and row["payload"]["match_index"] == 2))
        with self.assertRaisesRegex(semantics.WholeSessionSemanticError,
                                    "missing or duplicate return_css"):
            semantics.validate_whole_session_observer_records(records)


if __name__ == "__main__":
    unittest.main()
