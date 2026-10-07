import copy
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "reference-capture" / "dolphin"), str(ROOT / "tests")]

from reference_capture_semantics import pad_snapshot_bytes  # noqa: E402
from test_whole_session_replay import (  # noqa: E402
    _candidate, _pad_consume, _raw_pad_snapshot, _whole_setup)
from whole_session_replay import (  # noqa: E402
    EXPECTED_DOLPHIN_COMMIT, EXPECTED_DOL_SHA1, EXPECTED_DOL_SHA256,
    EXPECTED_OBSERVER_SCHEMA, SCENES, _consumed_ports, _first_css_context,
)
from whole_session_state_compare import (  # noqa: E402
    BrowserReader, Comparator, SourceCollector, _browser_completion_ok, _is_match_field,
    _browser_report, _fighter_entities, _snapshot_values, _state_from_payload,
    _validate_browser_report,
    ComparisonError, CONTEXT_HEADER, CONTEXT_BYTES, MWRC_HEADER, PAD_STATE_BYTES,
    MWRC_V10_VERSION, PRIMARY_STATIC_ENTITY_PROFILE, Recipe, SPAN, V10_DEFAULT_OFF_GATES,
    V10_FIGHTER_ROSTER, V10_FIRST_SETUP_TICK0_SCOPE, V10_PREFIX_BYTE_CAP,
    V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE, V10_FIRST_POSITIVE_RECORD_CAP,
    V10_FIRST_MATCH_CLOCK_GE60_SCOPE, V10_MATCH_CLOCK_RECORD_CAP,
    V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE, MATCH_CLOCK_EXPECTATION_SCHEMA,
    V10_BROWSER_PRODUCER_SCHEMA, V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA,
    V10_PREFIX_RECORD_CAP, V10_RULES_BYTES, WHOLE_SESSION_SCOPE,
    _file_stat_identity, _first_match_required_cursor, _first_match_tick_join_complete,
    _first_match_timeline_index, _first_positive_match_join_complete,
    _load_expectations, _require_stable_mwro_stat,
    _first_difference, _source_prefix_identity, _validate_browser_producer_source,
    _validate_source_capture_report, _validate_source_manifest_capture,
    _validate_v10_browser_provenance, _validate_v10_browser_export,
    _validate_first_positive_audit, _validate_clock60_audit,
    _validate_match_clock_boundary_audit,
    _match_boundary_join_complete,
    _browser_entities, compare_paths,
)


PAD_HEX = pad_snapshot_bytes(_raw_pad_snapshot())


def _word(raw, offset, value):
    struct.pack_into(">I", raw, offset, value)


def _state_slices(motion=80, tick=0, rng=0x12345678, fighter_count=4,
                  match_frame=None):
    if match_frame is None:
        match_frame = tick
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
         "hex": match_frame.to_bytes(4, "big").hex()},
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
        "scope": WHOLE_SESSION_SCOPE,
        "entity_profile": (PRIMARY_STATIC_ENTITY_PROFILE if version == 9 else None),
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


def _v10_setup_bytes():
    setups = []
    for roster in V10_FIGHTER_ROSTER:
        raw = bytearray(0x138)
        raw[:len(V10_RULES_BYTES)] = V10_RULES_BYTES
        for slot, character in enumerate(roster):
            base = 0x60 + slot * 0x24
            raw[base] = character
            raw[base + 1] = 1
            raw[base + 2] = 4
            raw[base + 3] = slot
            raw[base + 4] = 0
            raw[base + 14] = 4
            raw[base + 15] = 9
        for slot in range(4, 6):
            raw[0x60 + slot * 0x24 + 1] = 3
        setups.append(bytes(raw))
    return setups


def _v10_recipe_bytes(*, corrupt_later_roster=False, trailing=b""):
    frame_count = 12
    seed = 0x12345678
    setups = _v10_setup_bytes()
    if corrupt_later_roster:
        changed = bytearray(setups[1])
        changed[0x60] ^= 1
        setups[1] = bytes(changed)
    header = MWRC_HEADER.pack(b"MWRC", MWRC_V10_VERSION, seed, frame_count, 0, 0)
    context = CONTEXT_HEADER.pack(2, 0, CONTEXT_BYTES) + bytes(CONTEXT_BYTES)
    setup_table = struct.pack(">HH", 3, 0) + b"".join(setups)
    route = [SCENES[name] for name in ("css", "sss", "match", "results") * 3]
    spans = struct.pack(">H", len(route)) + b"".join(
        SPAN.pack(scene, 0, 0, index, index) for index, scene in enumerate(route))
    return (header + context + setup_table + bytes(PAD_STATE_BYTES) +
            bytes(frame_count * 44) + spans + trailing)


def _browser_export_rows(recipe, exported_cursor):
    header = {
        "record": "header", "schema": "melee-web-port-session-diagnostic",
        "version": 1, "frames_requested": recipe.frame_count,
        "comparison": "not_run", "cpu_observations": "not_captured",
        "draw_state": "not_captured",
    }
    rows = [header]
    match_spans = [span for span in recipe.spans if span["scene"] == SCENES["match"]]
    match_starts = {span["first_frame"]: match_index
                    for match_index, span in enumerate(match_spans)}
    for index in range(exported_cursor):
        match_index = match_starts.get(index)
        if match_index is not None:
            state = _v9_state({"slices": _state_slices(tick=0, match_frame=0)})
            for entity in state["fighter_entities"]:
                entity["match_index"] = match_index
            rows.append({
                "record": "session_match_enter_complete",
                "rng": state["rng"], "match_frame": state["match_frame"],
                "pad_state_hex": state["pad_state_hex"],
                "fighters": copy.deepcopy(state["fighters"]),
                "fighter_entities": copy.deepcopy(state["fighter_entities"]),
                "declared_setup": recipe.declared_match_setups[match_index],
            })
        scene = recipe.frames[index]["scene"]
        row = {
            "record": "session_frame", "scene": scene, "index": index,
            "supplied_inputs": recipe.frames[index]["pads"],
            "rng": recipe.seed, "pad_state_hex": "00" * PAD_STATE_BYTES,
        }
        if scene == SCENES["match"]:
            state = _v9_state({"slices": _state_slices(tick=index, match_frame=0)})
            for entity in state["fighter_entities"]:
                entity["match_index"] = match_index
            row.update({
                "match_frame": state["match_frame"],
                "fighters": copy.deepcopy(state["fighters"]),
                "fighter_entities": copy.deepcopy(state["fighter_entities"]),
            })
        rows.append(row)
    return rows


def _write_browser_export(path: Path, recipe, exported_cursor):
    rows = _browser_export_rows(recipe, exported_cursor)
    path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                              for row in rows) + "\n", encoding="utf-8")
    return rows


def _v10_positive_recipe_bytes(first_css, consumed_pads):
    route = [
        (SCENES["css"], 0, 0), (SCENES["sss"], 1, 1),
        (SCENES["match"], 2, 4), (SCENES["results"], 5, 5),
        (SCENES["css"], 6, 6), (SCENES["sss"], 7, 7),
        (SCENES["match"], 8, 8), (SCENES["results"], 9, 9),
        (SCENES["css"], 10, 10), (SCENES["sss"], 11, 11),
        (SCENES["match"], 12, 12), (SCENES["results"], 13, 13),
    ]
    frame_count = 14
    frame_pads = list(consumed_pads) + [
        [bytes(11).hex()] * 4 for _ in range(frame_count - len(consumed_pads))]
    if len(frame_pads) != frame_count or any(len(frame) != 4 for frame in frame_pads):
        raise AssertionError("synthetic v10 recipe needs one four-port PAD row per frame")
    context = b"".join(bytes.fromhex(first_css[name]) for name in (
        "game_rules_hex", "save_data_hex", "css_data_hex", "ko_counts_hex"))
    if len(context) != CONTEXT_BYTES:
        raise AssertionError("synthetic first-CSS context does not match MWRC v10 size")
    initial_pad = bytes.fromhex(first_css["pad_state_hex"])
    setups = _v10_setup_bytes()
    header = MWRC_HEADER.pack(
        b"MWRC", MWRC_V10_VERSION, first_css["rng"], frame_count,
        first_css["profile_masks"]["characters"],
        first_css["profile_masks"]["stages"])
    setup_table = struct.pack(">HH", len(setups), 0) + b"".join(setups)
    frames = b"".join(bytes.fromhex(port) for frame in frame_pads for port in frame)
    spans = struct.pack(">H", len(route)) + b"".join(
        SPAN.pack(scene, 0, 0, first, last) for scene, first, last in route)
    return (header + CONTEXT_HEADER.pack(2, 0, CONTEXT_BYTES) + context +
            setup_table + initial_pad + frames + spans)


def _v10_clock60_recipe_bytes(first_css, consumed_pads, terminal_tick=183):
    match_first = 2
    match_last = match_first + terminal_tick
    frame_count = match_last + 10
    route = [
        (SCENES["css"], 0, 0), (SCENES["sss"], 1, 1),
        (SCENES["match"], match_first, match_last),
        (SCENES["results"], match_last + 1, match_last + 1),
        (SCENES["css"], match_last + 2, match_last + 2),
        (SCENES["sss"], match_last + 3, match_last + 3),
        (SCENES["match"], match_last + 4, match_last + 4),
        (SCENES["results"], match_last + 5, match_last + 5),
        (SCENES["css"], match_last + 6, match_last + 6),
        (SCENES["sss"], match_last + 7, match_last + 7),
        (SCENES["match"], match_last + 8, match_last + 8),
        (SCENES["results"], match_last + 9, match_last + 9),
    ]
    frame_pads = list(consumed_pads) + [
        [bytes(11).hex()] * 4 for _ in range(frame_count - len(consumed_pads))]
    if len(frame_pads) != frame_count or any(len(frame) != 4 for frame in frame_pads):
        raise AssertionError("synthetic clock-60 recipe needs one four-port PAD row per frame")
    context = b"".join(bytes.fromhex(first_css[name]) for name in (
        "game_rules_hex", "save_data_hex", "css_data_hex", "ko_counts_hex"))
    if len(context) != CONTEXT_BYTES:
        raise AssertionError("synthetic first-CSS context does not match MWRC v10 size")
    setups = _v10_setup_bytes()
    header = MWRC_HEADER.pack(
        b"MWRC", MWRC_V10_VERSION, first_css["rng"], frame_count,
        first_css["profile_masks"]["characters"], first_css["profile_masks"]["stages"])
    setup_table = struct.pack(">HH", len(setups), 0) + b"".join(setups)
    frames = b"".join(bytes.fromhex(port) for frame in frame_pads for port in frame)
    spans = struct.pack(">H", len(route)) + b"".join(
        SPAN.pack(scene, 0, 0, first, last) for scene, first, last in route)
    return (header + CONTEXT_HEADER.pack(2, 0, CONTEXT_BYTES) + context +
            setup_table + bytes.fromhex(first_css["pad_state_hex"]) + frames + spans)


def _expectation_packet(directory: Path, *, capture_id: str, sequence_id: str,
                        producer_head: str):
    directory.mkdir(parents=True, exist_ok=True)
    selected = {}
    identities = {}
    for name in ("reference", "source_manifest", "source_report", "source_audit",
                 "recipe", "browser_capture_report", "browser_producer_manifest",
                 "browser_report", "port_trace"):
        path = directory / f"{name}.bin"
        path.write_bytes(name.encode("ascii"))
        selected[name] = path
        identities[name] = {"path": str(path), "bytes": path.stat().st_size,
                            "sha256": "a" * 64}
    identities["reference"]["recorded_full_sha256"] = "b" * 64
    identities["recipe"].update({"version": 10, "frame_count": 12, "seed": 7})
    packet = {
        "schema": "melee-web-v10-first-setup-tick0-expectations",
        "version": 1,
        "scope": V10_FIRST_SETUP_TICK0_SCOPE,
        "source": {
            "trace": identities["reference"],
            "manifest": identities["source_manifest"],
            "report": identities["source_report"],
            "audit": identities["source_audit"],
            "capture_id": capture_id,
            "sequence_id": sequence_id,
        },
        "recipe": identities["recipe"],
        "browser": {
            "capture_report": identities["browser_capture_report"],
            "producer_manifest": identities["browser_producer_manifest"],
            "report": identities["browser_report"],
            "trace": identities["port_trace"],
            "disc": {"path": str(directory / "disc.ciso"), "bytes": 5,
                     "sha256": "e" * 64},
            "runtime_data": {"path": str(directory / "runtime.data"), "bytes": 7,
                             "sha256": "f" * 64, "freshly_rehashed": False},
            "producer": {"branch": "codex/test", "head": producer_head,
                         "tree": "c" * 40, "base_main": "d" * 40},
        },
    }
    packet_path = directory / "expectations.json"
    packet_path.write_text(json.dumps(packet), encoding="utf-8")
    return packet_path, selected, packet


def _positive_expectation_packet(directory: Path, *, capture_id="capture-positive",
                                 sequence_id="sequence-positive", producer_head="1" * 40,
                                 source_tick=3, source_sequence=50):
    packet_path, selected, packet = _expectation_packet(
        directory, capture_id=capture_id, sequence_id=sequence_id,
        producer_head=producer_head)
    recipe = Recipe(selected["recipe"], _v10_recipe_bytes(),
                    scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
    timeline_index = _first_match_timeline_index(recipe) + source_tick
    target = {
        "match_index": 0,
        "source_tick": source_tick,
        "source_sequence": source_sequence,
        "pad_consume_sequence": source_sequence - 1,
        "timeline_frame_index": timeline_index,
        "browser_cursor": timeline_index + 1,
        "match_frame": 1,
    }
    packet["schema"] = "melee-web-v10-first-positive-match-frame-expectations"
    packet["scope"] = V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE
    packet["source"]["first_positive_boundary"] = target
    audit_path = directory / "positive-audit.json"
    stat_identity = {"device": 10, "inode": 20,
                     "bytes": packet["source"]["trace"]["bytes"], "mtime_ns": 30}
    expected_source = packet["source"]
    audit = {
        "schema": "melee-web-b4-first-positive-match-frame-source-audit-v1",
        "audit_completed": True,
        "whole_session_equivalent": False,
        "source": {
            "path": expected_source["trace"]["path"],
            "capture_id": capture_id,
            "sequence_id": sequence_id,
            "recorded_full_trace_bytes": expected_source["trace"]["bytes"],
            "recorded_full_trace_sha256": expected_source["trace"]["recorded_full_sha256"],
            "full_trace_rehashed": False,
            "stat_before": stat_identity,
            "stat_after": stat_identity,
            "stat_stable_during_audit": True,
            "provenance": {
                "manifest_sha256": expected_source["manifest"]["sha256"],
                "source_report_sha256": expected_source["report"]["sha256"],
                "audit_sha256": expected_source["audit"]["sha256"],
                "recorded_full_trace_sha256": expected_source["trace"]["recorded_full_sha256"],
                "full_trace_rehashed": False,
                "trace_bytes": expected_source["trace"]["bytes"],
            },
        },
        "observed": {
            "first_positive": {
                "match_index": 0,
                "source_tick": source_tick,
                "pad_consume_source_sequence": source_sequence - 1,
                "timeline_frame_index": timeline_index,
                "cursor_after_frame": timeline_index + 1,
                "match_frame": 1,
            },
            "source_prefix": {
                "last_source_sequence": source_sequence,
                "records_read": source_sequence + 1,
                "bytes_read": 100,
                "sha256": "a" * 64,
            },
            "match_ticks_observed": source_tick + 1,
            "timeline_frames_input_ordered_against_recipe": timeline_index + 1,
            "css_sss_frames_input_ordered_against_recipe": _first_match_timeline_index(recipe),
            "minimum_browser_target_cursor": timeline_index + 1,
            "match_frame_runs": [
                {"first_source_tick": 0, "last_source_tick": source_tick - 1,
                 "first_timeline_frame_index": _first_match_timeline_index(recipe),
                 "last_timeline_frame_index": timeline_index - 1, "match_frame": 0},
                {"first_source_tick": source_tick, "last_source_tick": source_tick,
                 "first_timeline_frame_index": timeline_index,
                 "last_timeline_frame_index": timeline_index, "match_frame": 1},
            ],
        },
    }
    audit_path.write_text(json.dumps(audit), encoding="utf-8")
    packet["source"]["positive_boundary_audit"] = {
        "path": str(audit_path), "bytes": audit_path.stat().st_size,
        "sha256": hashlib.sha256(audit_path.read_bytes()).hexdigest(),
    }
    packet_path.write_text(json.dumps(packet), encoding="utf-8")
    selected["positive_boundary_audit"] = audit_path
    return packet_path, selected, packet, audit_path, audit


def _clock60_comparison_fixture(directory: Path, *, terminal_match_frame: int = 60):
    """Build a synthetic source/browser prefix through the requested match clock."""
    if type(terminal_match_frame) is not int or terminal_match_frame < 60:
        raise ValueError("synthetic terminal match clock must be an integer at least 60")
    terminal_tick = 123 + terminal_match_frame
    terminal_sequence = 11 + 2 * terminal_tick
    terminal_cursor = 3 + terminal_tick
    directory.mkdir(parents=True, exist_ok=True)
    capture_id, sequence_id = "clock60-capture", "clock60-sequence"
    packet_path, selected, packet, positive_path, positive = _positive_expectation_packet(
        directory, capture_id=capture_id, sequence_id=sequence_id,
        source_tick=1, source_sequence=13)

    candidate = _candidate(capture_id=capture_id, sequence_id=sequence_id)
    boundary = lambda name, match=0: next(  # noqa: E731
        row for row in candidate
        if row.get("event") == "boundary" and
        row.get("payload", {}).get("boundary") == name and
        row.get("payload", {}).get("match_index") == match)
    rows = [copy.deepcopy(candidate[0]), copy.deepcopy(candidate[1]),
            copy.deepcopy(boundary("css_enter")), copy.deepcopy(candidate[3]),
            copy.deepcopy(boundary("css_exit")), copy.deepcopy(boundary("sss_enter")),
            copy.deepcopy(candidate[6]), copy.deepcopy(boundary("sss_exit")),
            copy.deepcopy(boundary("entry"))]
    rows[0]["payload"]["fighter_entity_profile"] = "v10-live-static-player-pair"
    raw_setup = _v10_setup_bytes()[0]
    entry_slice = next(item for item in rows[-1]["payload"]["slices"]
                       if item["name"] == "match_setup")
    entry_slice.update({"size": len(raw_setup), "hex": raw_setup.hex()})

    setup = copy.deepcopy(boundary("setup"))
    setup["source_tick"] = 0
    setup["payload"]["gprs"] = [0] * 32
    setup["payload"]["gprs"][3] = 0x80600000
    setup["payload"]["slices"] = _state_slices(tick=0, match_frame=0) + [{
        "name": "match_setup", "address": 0x80600000,
        "size": len(raw_setup), "hex": raw_setup.hex(),
    }]
    rows.append(setup)
    match_pad_rows = []
    for tick in range(terminal_tick + 1):
        pad = _pad_consume(0, 0x30, tick)
        tick_pad_row = len(rows)
        match_pad_rows.append(_consumed_ports(pad, 0))
        rows.append(pad)
        rows.append(_boundary(
            "source_tick", 0, tick,
            slices=_state_slices(tick=tick, rng=0x12345678,
                                 match_frame=0 if tick < 124 else tick - 123)))
    for seq, row in enumerate(rows):
        row["seq"] = seq
        row["draw_ordinal"] = seq

    first_css = _first_css_context([rows[2]])
    source_identity = _source_prefix_identity(rows[0]["payload"], rows[1]["payload"])
    menu_pads = [_consumed_ports(rows[3], 0), _consumed_ports(rows[6], 0)]
    consumed_pads = [*menu_pads, *match_pad_rows]
    recipe_raw = _v10_clock60_recipe_bytes(first_css, consumed_pads, terminal_tick)
    selected["recipe"].write_bytes(recipe_raw)
    generic_terminal_scope = terminal_match_frame > 60
    fixture_scope = (V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE if generic_terminal_scope else
                     V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
    recipe = Recipe(selected["recipe"], recipe_raw,
                    scope=fixture_scope)
    packet["schema"] = (MATCH_CLOCK_EXPECTATION_SCHEMA if generic_terminal_scope else
                        "melee-web-v10-first-match-clock-ge60-expectations")
    packet["scope"] = fixture_scope
    packet["recipe"].update({
        "bytes": len(recipe_raw), "sha256": hashlib.sha256(recipe_raw).hexdigest(),
        "version": 10, "frame_count": recipe.frame_count, "seed": recipe.seed,
    })
    source_manifest = {"input": {
        "capture": source_identity,
        "first_css": first_css,
        "profile_context": first_css["profile_context"],
        "match_setups": [
            {"match_index": index, "start_melee_hex": raw.hex(),
             "declared_setup": recipe.declared_match_setups[index]}
            for index, raw in enumerate(recipe.match_setups)],
        "setup_hex": recipe.setup.hex(),
        "declared_setup": recipe.declared_match_setups[0],
    }}
    source_audit = {
        "first_entry_verified_seq": 8,
        "first_setup": {"seq": 9, "source_tick": 0, "match_index": 0},
        "first_source_tick": {"seq": 11, "source_tick": 0, "match_index": 0},
    }

    record_bytes = [b"R" + seq.to_bytes(4, "big") for seq in range(len(rows))]
    positive_bytes = b"".join(record_bytes[:260])
    clock60_bytes = b"".join(record_bytes[:378])
    terminal_bytes = b"".join(record_bytes[:terminal_sequence + 1])
    full_trace = terminal_bytes + b"synthetic-tail"
    selected["reference"].write_bytes(full_trace)
    stat_identity = _file_stat_identity(selected["reference"])
    packet["source"]["trace"].update({
        "bytes": len(full_trace), "recorded_full_sha256": "b" * 64,
    })
    packet["source"]["first_positive_boundary"] = {
        "match_index": 0, "source_tick": 124, "source_sequence": 259,
        "pad_consume_sequence": 258, "timeline_frame_index": 126,
        "browser_cursor": 127, "match_frame": 1,
    }
    positive["observed"]["first_positive"] = {
        "match_index": 0, "source_tick": 124,
        "pad_consume_source_sequence": 258,
        "timeline_frame_index": 126, "cursor_after_frame": 127,
        "match_frame": 1,
    }
    positive["source"].update({
        "recorded_full_trace_bytes": len(full_trace),
        "recorded_full_trace_sha256": "b" * 64,
        "stat_before": stat_identity, "stat_after": stat_identity,
    })
    positive["source"]["provenance"]["trace_bytes"] = len(full_trace)
    positive["source"]["provenance"]["recorded_full_trace_sha256"] = "b" * 64
    positive["observed"]["source_prefix"].update({
        "bytes_read": len(positive_bytes), "records_read": 260,
        "last_source_sequence": 259,
        "sha256": hashlib.sha256(positive_bytes).hexdigest(),
    })
    positive["observed"].update({
        "match_ticks_observed": 125,
        "timeline_frames_input_ordered_against_recipe": 127,
        "minimum_browser_target_cursor": 127,
        "match_frame_runs": [
            {"first_source_tick": 0, "last_source_tick": 123,
             "first_timeline_frame_index": 2, "last_timeline_frame_index": 125,
             "match_frame": 0},
            {"first_source_tick": 124, "last_source_tick": 124,
             "first_timeline_frame_index": 126, "last_timeline_frame_index": 126,
             "match_frame": 1},
        ],
    })
    positive_path.write_text(json.dumps(positive), encoding="utf-8")
    positive_identity = {
        "path": str(positive_path), "bytes": positive_path.stat().st_size,
        "sha256": hashlib.sha256(positive_path.read_bytes()).hexdigest(),
    }
    packet["source"]["positive_boundary_audit"] = positive_identity
    selected["positive_boundary_audit"] = positive_path

    clock_target = {
        "match_index": 0, "source_tick": 183, "source_sequence": 377,
        "pad_consume_sequence": 376, "timeline_frame_index": 185,
        "browser_cursor": 186, "match_frame": 60,
    }
    packet["source"]["clock60_boundary"] = clock_target
    clock_path = directory / "clock60-audit.json"
    runs = [
        {"first_source_tick": 0, "last_source_tick": 123,
         "first_timeline_frame_index": 2, "last_timeline_frame_index": 125,
         "match_frame": 0},
        {"first_source_tick": 124, "last_source_tick": 124,
         "first_timeline_frame_index": 126, "last_timeline_frame_index": 126,
         "match_frame": 1},
        *[{"first_source_tick": tick, "last_source_tick": tick,
           "first_timeline_frame_index": 2 + tick,
           "last_timeline_frame_index": 2 + tick,
           "match_frame": tick - 123}
          for tick in range(125, 184)],
    ]
    clock_audit = {
        "schema": "melee-web-b4-first-match-clock-ge60-source-audit-v2",
        "audit_completed": True, "whole_session_equivalent": False,
        "status": "first_match_clock_ge60_found", "target_match_frame_at_least": 60,
        "source": {
            "path": packet["source"]["trace"]["path"],
            "capture_id": capture_id, "sequence_id": sequence_id,
            "recorded_full_trace_bytes": len(full_trace),
            "recorded_full_trace_sha256": "b" * 64,
            "full_trace_rehashed": False,
            "prior_first_positive_audit_sha256": positive_identity["sha256"],
            "prior_first_positive_boundary": packet["source"]["first_positive_boundary"],
            "stat_before": stat_identity, "stat_after": stat_identity,
            "stat_stable_during_audit": True,
            "provenance": copy.deepcopy(positive["source"]["provenance"]),
        },
        "observed": {
            "accepted_first_positive_rejoined": True,
            "candidate_browser_cursor": 186,
            "css_sss_frames_input_ordered_against_recipe": 2,
            "timeline_frames_input_ordered_against_recipe": 186,
            "match_ticks_observed": 184,
            "target_clock_at_least_60": {
                "match_index": 0, "source_tick": 183, "source_tick_seq": 377,
                "pad_consume_source_sequence": 376, "timeline_frame_index": 185,
                "cursor_after_frame": 186, "match_frame": 60,
            },
            "source_prefix": {
                "bytes_read": len(clock60_bytes), "records_read": 378,
                "last_source_sequence": 377,
                "sha256": hashlib.sha256(clock60_bytes).hexdigest(),
            },
            "match_frame_runs": runs,
        },
    }
    clock_path.write_text(json.dumps(clock_audit), encoding="utf-8")
    packet["source"]["clock60_boundary_audit"] = {
        "path": str(clock_path), "bytes": clock_path.stat().st_size,
        "sha256": hashlib.sha256(clock_path.read_bytes()).hexdigest(),
    }
    selected["clock60_boundary_audit"] = clock_path

    terminal_target = {
        "target_match_frame_at_least": terminal_match_frame,
        "match_index": 0, "source_tick": terminal_tick,
        "source_sequence": terminal_sequence,
        "pad_consume_sequence": terminal_sequence - 1,
        "timeline_frame_index": 2 + terminal_tick,
        "browser_cursor": terminal_cursor,
        "match_frame": terminal_match_frame,
    }
    terminal_path = None
    terminal_audit = None
    if generic_terminal_scope:
        packet["source"]["match_clock_boundary"] = terminal_target
        terminal_path = directory / "match-clock-boundary-audit.json"
        entities = [
            {"match_index": 0, "slot": slot, "entity_index": 0,
             "fighter_player_id": slot, "generation": 0,
             "fighter_gobj_linked": True}
            for slot in range(4)
        ]

        def observed_boundary(tick, match_frame):
            source_sequence = 11 + 2 * tick
            timeline_index = 2 + tick
            return {
                "match_index": 0, "source_tick": tick,
                "source_tick_seq": source_sequence,
                "pad_consume_source_sequence": source_sequence - 1,
                "timeline_frame_index": timeline_index,
                "cursor_after_frame": timeline_index + 1,
                "match_frame": match_frame, "scene_frame": tick,
                "rng": 0x12345678, "fighter_entities": entities,
            }

        terminal_audit = {
            "schema": f"melee-web-b4-source-clock-ge{terminal_match_frame}-audit-v3",
            "scope": f"source-only-clock-ge{terminal_match_frame}",
            "audit_completed": True, "complete": False,
            "whole_session_equivalent": False, "report_write_failed": False,
            "status": f"first_match_clock_ge{terminal_match_frame}_found",
            "target_match_frame_at_least": terminal_match_frame, "error": None,
            "source": {
                "path": packet["source"]["trace"]["path"],
                "capture_id": capture_id, "sequence_id": sequence_id,
                "recorded_full_trace_bytes": len(full_trace),
                "recorded_full_trace_sha256": "b" * 64,
                "full_trace_rehashed": False,
                "stat_before": stat_identity, "stat_after": stat_identity,
                "stat_stable_during_audit": True,
                "content_opened": True,
                "content_bytes_read": len(terminal_bytes), "stream_attempted": True,
                "provenance": copy.deepcopy(positive["source"]["provenance"]),
            },
            "observed": {
                "first_positive_rejoined": True, "clock60_rejoined": True,
                "first_positive_observed": observed_boundary(124, 1),
                "first_clock60_observed": observed_boundary(183, 60),
                f"target_clock_ge{terminal_match_frame}_observed": observed_boundary(
                    terminal_tick, terminal_match_frame),
                "source_prefix": {
                    "bytes_read": len(terminal_bytes),
                    "records_read": terminal_sequence + 1,
                    "last_source_sequence": terminal_sequence,
                    "sha256": hashlib.sha256(terminal_bytes).hexdigest(),
                },
                "match_ticks_observed": terminal_tick + 1,
                "timeline_frames_input_ordered_against_recipe": terminal_cursor,
                "css_sss_frames_input_ordered_against_recipe": 2,
            },
            "checkpoints": {
                "first_positive_clock1": "pass", "first_clock60": "pass",
            },
        }
        terminal_path.write_text(json.dumps(terminal_audit), encoding="utf-8")
        packet["source"]["match_clock_boundary_audit"] = {
            "path": str(terminal_path), "bytes": terminal_path.stat().st_size,
            "sha256": hashlib.sha256(terminal_path.read_bytes()).hexdigest(),
        }
        selected["match_clock_boundary_audit"] = terminal_path

    source_state_history: dict[int, dict[int, tuple[int, int]]] = {}

    def source_state(payload, context, previous):
        state = _state_from_payload(payload, context)
        state.update(_snapshot_values(payload, context))
        state["fighter_entities"] = _fighter_entities(payload, context, 0, previous)
        return state

    setup_state = source_state(setup["payload"], "browser setup", source_state_history)
    setup_state["declared_setup"] = recipe.declared_match_setups[0]
    browser_rows = [{
        "record": "header", "schema": "melee-web-port-session-diagnostic",
        "version": 1, "frames_requested": recipe.frame_count,
        "comparison": "not_run", "cpu_observations": "not_captured",
        "draw_state": "not_captured",
    }]
    for index in (0, 1):
        browser_rows.append({
            "record": "session_frame", "scene": recipe.frames[index]["scene"],
            "index": index, "supplied_inputs": recipe.frames[index]["pads"],
            "rng": recipe.seed, "pad_state_hex": recipe.initial_pad.hex(),
        })
    browser_rows.append({
        "record": "session_match_enter_complete", "rng": setup_state["rng"],
        "match_frame": setup_state["match_frame"],
        "pad_state_hex": setup_state["pad_state_hex"],
        "fighters": setup_state["fighters"],
        "fighter_entities": setup_state["fighter_entities"],
        "declared_setup": setup_state["declared_setup"],
    })
    browser_entity_history: dict[int, dict[int, tuple[int, int]]] = {}
    for tick in range(terminal_tick + 1):
        index = 2 + tick
        payload = {"slices": _state_slices(tick=tick, rng=0x12345678,
                                            match_frame=0 if tick < 124 else tick - 123)}
        state = source_state(payload, f"browser match tick {tick}", browser_entity_history)
        browser_rows.append({
            "record": "session_frame", "scene": SCENES["match"], "index": index,
            "supplied_inputs": recipe.frames[index]["pads"],
            "rng": state["rng"], "match_frame": state["match_frame"],
            "pad_state_hex": state["pad_state_hex"],
            "fighters": state["fighters"], "fighter_entities": state["fighter_entities"],
        })
    if generic_terminal_scope:
        tail_index = terminal_cursor
        browser_rows.append({
            "record": "session_frame", "scene": recipe.frames[tail_index]["scene"],
            "index": tail_index,
            "supplied_inputs": recipe.frames[tail_index]["pads"],
            "rng": recipe.seed, "pad_state_hex": recipe.initial_pad.hex(),
        })
    port_path = selected["port_trace"]
    port_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                     for row in browser_rows) + "\n",
                         encoding="utf-8")
    packet["browser"]["trace"].update({
        "bytes": port_path.stat().st_size,
        "sha256": hashlib.sha256(port_path.read_bytes()).hexdigest(),
    })
    packet_path.write_text(json.dumps(packet), encoding="utf-8")
    return {
        "packet_path": packet_path, "selected": selected, "packet": packet,
        "recipe": recipe, "rows": rows, "raw_records": record_bytes,
        "source_manifest": source_manifest, "source_audit": source_audit,
        "positive_audit": positive, "positive_path": positive_path,
        "clock_audit": clock_audit, "clock_path": clock_path,
        "terminal_bytes": terminal_bytes, "positive_bytes": positive_bytes,
        "clock_target": clock_target, "match_clock_target": terminal_target,
        "match_clock_audit": terminal_audit, "match_clock_path": terminal_path,
    }


def _run_clock60_comparison(fixture, *, raw_overrides=None,
                            browser_provenance_error: Exception | None = None):
    selected = fixture["selected"]
    packet = fixture["packet"]
    source_stat = _file_stat_identity(selected["reference"])
    source_identity = {
        "trace_bytes": source_stat["bytes"],
        "recorded_full_trace_sha256": packet["source"]["trace"]["recorded_full_sha256"],
        "full_trace_rehashed": False,
        "manifest_sha256": packet["source"]["manifest"]["sha256"],
        "source_report_sha256": packet["source"]["report"]["sha256"],
        "audit_sha256": packet["source"]["audit"]["sha256"],
        "audit_records_decoded": 4116,
        "audit_bytes_read": 5364736,
    }
    match_clock_scope = fixture["match_clock_target"]["target_match_frame_at_least"] > 60
    terminal_target = (fixture["match_clock_target"] if match_clock_scope
                       else fixture["clock_target"])
    browser_cursor = terminal_target["browser_cursor"]
    browser_identity = {
        "required_cursor": browser_cursor, "target_cursor": browser_cursor,
        "observed_cursor": browser_cursor, "requested_cursor": browser_cursor,
        "exported_cursor": browser_cursor,
        "capture_report_sha256": "c" * 64,
        "producer_manifest_sha256": "d" * 64,
        "browser_report_sha256": "e" * 64,
        "port_trace_sha256": "f" * 64,
    }
    raw_records = list(fixture["raw_records"])
    for index, value in (raw_overrides or {}).items():
        raw_records[index] = value
    yielded = []
    observed_limits = {}

    def synthetic_records(path, *, max_bytes, max_records, stats):
        observed_limits.update(max_bytes=max_bytes, max_records=max_records)
        for row, raw in zip(fixture["rows"], raw_records):
            stats.record_bytes(raw)
            stats.records_read += 1
            yielded.append(row["seq"])
            yield row

    browser_provenance_patch = mock.patch(
        "whole_session_state_compare._validate_v10_browser_provenance",
        side_effect=browser_provenance_error) if browser_provenance_error is not None else None
    if browser_provenance_patch is None:
        browser_provenance_patch = mock.patch(
            "whole_session_state_compare._validate_v10_browser_provenance",
            return_value=({}, {}, {}, browser_identity))
    with (mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                     return_value=(fixture["source_manifest"], {},
                                   fixture["source_audit"], source_identity)),
          browser_provenance_patch,
          mock.patch("whole_session_state_compare.iter_records",
                     side_effect=synthetic_records)):
        result = compare_paths(
            selected["reference"], selected["recipe"], selected["port_trace"],
            scope=(V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE if match_clock_scope else
                   V10_FIRST_MATCH_CLOCK_GE60_SCOPE),
            expectations=fixture["packet_path"],
            source_manifest=selected["source_manifest"],
            source_report=selected["source_report"],
            source_audit=selected["source_audit"],
            browser_capture_report=selected["browser_capture_report"],
            browser_producer_manifest=selected["browser_producer_manifest"],
            browser_report=selected["browser_report"],
            positive_boundary_audit=selected["positive_boundary_audit"],
            clock60_boundary_audit=selected["clock60_boundary_audit"],
            match_clock_boundary_audit=(selected.get("match_clock_boundary_audit")
                                        if match_clock_scope else None),
        )
    return result, yielded, observed_limits


class WholeSessionStateCompareTests(unittest.TestCase):
    def test_v10_scope_audit_selection_mismatch_returns_structured_invalid_result(self):
        result = compare_paths(
            "unused.mwro", "unused.mwrc", "unused.jsonl",
            scope=V10_FIRST_SETUP_TICK0_SCOPE,
            expectations="unused-expectations.json",
            source_manifest="unused-manifest.json",
            source_report="unused-report.json",
            source_audit="unused-audit.json",
            browser_capture_report="unused-capture.json",
            browser_producer_manifest="unused-producer.json",
            browser_report="unused-browser-report.json",
            positive_boundary_audit="unused-positive-audit.json")

        self.assertEqual(result["result"], "invalid")
        self.assertFalse(result["complete"])
        self.assertFalse(result["whole_session_equivalent"])
        self.assertIn("scope and positive-boundary audit selection disagree",
                      result["error"])

    def test_bounded_source_trace_stat_identity_must_remain_stable(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.mwro"
            path.write_bytes(b"same-size")
            before = _file_stat_identity(path)
            replacement = Path(directory) / "replacement.mwro"
            replacement.write_bytes(b"same-size")
            replacement.replace(path)
            after = _file_stat_identity(path)
            self.assertNotEqual(before, after)
            with self.assertRaisesRegex(ComparisonError, "stat identity changed"):
                _require_stable_mwro_stat(before, after)

    def test_v10_recipe_is_opt_in_and_validates_all_later_profiles_and_spans(self):
        raw = _v10_recipe_bytes()
        recipe = Recipe(Path("v10.mwrc"), raw, scope=V10_FIRST_SETUP_TICK0_SCOPE)
        self.assertEqual(recipe.version, 10)
        self.assertEqual(recipe.entity_profile, PRIMARY_STATIC_ENTITY_PROFILE)
        self.assertEqual(len(recipe.declared_match_setups), 3)
        self.assertEqual(tuple(tuple(player["character_kind"]
                                     for player in setup["players"])
                               for setup in recipe.declared_match_setups), V10_FIGHTER_ROSTER)
        self.assertEqual(_first_match_required_cursor(recipe), 3)
        with self.assertRaisesRegex(ComparisonError, "v8 or v9"):
            Recipe(Path("v10.mwrc"), raw)
        with self.assertRaisesRegex(ComparisonError, "whole-session comparison version"):
            from whole_session_state_compare import comparison_fields
            comparison_fields(10)
        with self.assertRaisesRegex(ComparisonError, "setup 1"):
            Recipe(Path("bad-roster.mwrc"), _v10_recipe_bytes(corrupt_later_roster=True),
                   scope=V10_FIRST_SETUP_TICK0_SCOPE)
        with self.assertRaisesRegex(ComparisonError, "trailing bytes"):
            Recipe(Path("trailing.mwrc"), _v10_recipe_bytes(trailing=b"\0"),
                   scope=V10_FIRST_SETUP_TICK0_SCOPE)
        missing_packet = compare_paths(
            "source.mwro", "recipe.mwrc", "browser.jsonl",
            scope=V10_FIRST_SETUP_TICK0_SCOPE,
            source_manifest="manifest.json", source_report="source-report.json",
            source_audit="audit.json", browser_capture_report="capture.json",
            browser_producer_manifest="producer.json", browser_report="browser-report.json")
        self.assertEqual(missing_packet["result"], "invalid")
        self.assertFalse(missing_packet["complete"])
        self.assertFalse(missing_packet["whole_session_equivalent"])

    def test_external_expectations_accept_an_independent_capture_and_sequence_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first_path, first_selected, first = _expectation_packet(
                root / "first", capture_id="capture-alt-a", sequence_id="sequence-alt-a",
                producer_head="1" * 40)
            second_path, second_selected, second = _expectation_packet(
                root / "second", capture_id="capture-alt-b", sequence_id="sequence-alt-b",
                producer_head="2" * 40)
            loaded_first, _ = _load_expectations(first_path, first_selected)
            loaded_second, _ = _load_expectations(second_path, second_selected)
            self.assertEqual(loaded_first["source"]["capture_id"], "capture-alt-a")
            self.assertEqual(loaded_first["source"]["sequence_id"], "sequence-alt-a")
            self.assertEqual(loaded_second["source"]["capture_id"], "capture-alt-b")
            self.assertEqual(loaded_second["source"]["sequence_id"], "sequence-alt-b")
            self.assertNotEqual(first["browser"]["producer"]["head"],
                                second["browser"]["producer"]["head"])

    def test_positive_scope_packet_and_audit_bind_dynamic_source_boundary(self):
        with tempfile.TemporaryDirectory() as directory:
            first_path, first_selected, first, first_audit_path, _ = \
                _positive_expectation_packet(
                    Path(directory) / "first", capture_id="capture-one",
                    sequence_id="sequence-one", source_tick=3, source_sequence=50)
            second_path, second_selected, second, second_audit_path, _ = \
                _positive_expectation_packet(
                    Path(directory) / "second", capture_id="capture-two",
                    sequence_id="sequence-two", source_tick=4, source_sequence=78,
                    producer_head="2" * 40)
            loaded_first, _ = _load_expectations(
                first_path, first_selected, scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            loaded_second, _ = _load_expectations(
                second_path, second_selected, scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            self.assertEqual(loaded_first["source"]["capture_id"], "capture-one")
            self.assertEqual(loaded_first["source"]["first_positive_boundary"]["source_sequence"], 50)
            self.assertEqual(loaded_second["source"]["capture_id"], "capture-two")
            self.assertEqual(loaded_second["source"]["first_positive_boundary"]["source_tick"], 4)
            recipe = Recipe(first_selected["recipe"], _v10_recipe_bytes(),
                            scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            _, first_sha = _validate_first_positive_audit(
                first_audit_path, loaded_first, recipe)
            _, second_sha = _validate_first_positive_audit(
                second_audit_path, loaded_second, recipe)
            self.assertEqual(first_sha, first["source"]["positive_boundary_audit"]["sha256"])
            self.assertEqual(second_sha, second["source"]["positive_boundary_audit"]["sha256"])

    def test_positive_scope_rejects_wrong_target_and_audit_binding(self):
        with tempfile.TemporaryDirectory() as directory:
            packet_path, selected, packet, audit_path, _ = _positive_expectation_packet(
                Path(directory), source_tick=3, source_sequence=50)
            packet["source"]["first_positive_boundary"]["timeline_frame_index"] += 1
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "invalid first-positive target"):
                _load_expectations(packet_path, selected,
                                   scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)

            packet["source"]["first_positive_boundary"]["timeline_frame_index"] -= 1
            audit = json.loads(audit_path.read_text(encoding="utf-8"))
            audit["source"]["capture_id"] = "other-capture"
            audit_path.write_text(json.dumps(audit), encoding="utf-8")
            packet["source"]["positive_boundary_audit"].update({
                "bytes": audit_path.stat().st_size,
                "sha256": hashlib.sha256(audit_path.read_bytes()).hexdigest(),
            })
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            loaded, _ = _load_expectations(packet_path, selected,
                                           scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            recipe = Recipe(selected["recipe"], _v10_recipe_bytes(),
                            scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            with self.assertRaisesRegex(ComparisonError, "source trace identity"):
                _validate_first_positive_audit(audit_path, loaded, recipe)

    def test_positive_scope_keeps_legacy_defaults_and_uses_wider_prefix_record_cap(self):
        raw = _v10_recipe_bytes()
        with self.assertRaisesRegex(ComparisonError, "whole-session comparison requires MWRC"):
            Recipe(Path("v10.mwrc"), raw)
        with self.assertRaisesRegex(ComparisonError, "whole-session comparison version"):
            from whole_session_state_compare import comparison_fields
            comparison_fields(10)
        recipe = Recipe(Path("v10.mwrc"), raw,
                        scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
        self.assertEqual(recipe.entity_profile, PRIMARY_STATIC_ENTITY_PROFILE)
        self.assertEqual(V10_PREFIX_RECORD_CAP, 4200)
        self.assertEqual(V10_FIRST_POSITIVE_RECORD_CAP, 8192)

    def test_clock60_scope_is_explicit_and_keeps_whole_session_default(self):
        raw = _v10_recipe_bytes()
        with self.assertRaisesRegex(ComparisonError, "whole-session comparison requires MWRC"):
            Recipe(Path("v10.mwrc"), raw)
        with self.assertRaisesRegex(ComparisonError, "whole-session comparison version"):
            from whole_session_state_compare import comparison_fields
            comparison_fields(10)
        recipe = Recipe(Path("v10.mwrc"), raw,
                        scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
        self.assertEqual(recipe.version, 10)
        self.assertEqual(recipe.entity_profile, PRIMARY_STATIC_ENTITY_PROFILE)
        self.assertEqual(V10_MATCH_CLOCK_RECORD_CAP, 8192)
        result = compare_paths("source.mwro", "recipe.mwrc", "browser.jsonl",
                               scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
        self.assertEqual(result["result"], "invalid")
        self.assertFalse(result["complete"])
        self.assertFalse(result["whole_session_equivalent"])
        self.assertIn("clock-60", result["error"])

    def test_v10_browser_export_validates_all_rows_setup_entities_and_exact_eof(self):
        recipe_raw = _v10_recipe_bytes()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe_path = root / "recipe.mwrc"
            recipe_path.write_bytes(recipe_raw)
            recipe = Recipe(recipe_path, recipe_raw,
                            scope=V10_FIRST_SETUP_TICK0_SCOPE)
            trace_path = root / "port.jsonl"
            good_rows = _browser_export_rows(recipe, 5)

            def write(rows):
                trace_path.write_text("\n".join(
                    json.dumps(row, separators=(",", ":")) for row in rows) + "\n",
                    encoding="utf-8")

            write(good_rows)
            packet = {"source": {}}
            self.assertEqual(_validate_v10_browser_export(
                trace_path, recipe, 5, packet), 7)

            def assert_bad(rows, message):
                write(rows)
                with self.assertRaisesRegex(ComparisonError, message):
                    _validate_v10_browser_export(trace_path, recipe, 5, packet)

            missing_setup = copy.deepcopy(good_rows)
            del missing_setup[next(i for i, row in enumerate(missing_setup)
                                   if row.get("record") == "session_match_enter_complete")]
            assert_bad(missing_setup, "fields differ|record order")

            duplicate_setup = copy.deepcopy(good_rows)
            setup_index = next(i for i, row in enumerate(duplicate_setup)
                               if row.get("record") == "session_match_enter_complete")
            duplicate_setup.insert(setup_index + 1, copy.deepcopy(duplicate_setup[setup_index]))
            assert_bad(duplicate_setup, "fields differ|record order")

            reordered = copy.deepcopy(good_rows)
            reordered[1], reordered[2] = reordered[2], reordered[1]
            assert_bad(reordered, "missing, extra, or reordered")

            wrong_scene = copy.deepcopy(good_rows)
            wrong_scene[2]["scene"] = SCENES["css"]
            assert_bad(wrong_scene, "missing, extra, or reordered")

            bool_scene = copy.deepcopy(good_rows)
            bool_scene[1]["scene"] = True
            assert_bad(bool_scene, "exact integers")

            bool_index = copy.deepcopy(good_rows)
            bool_index[2]["index"] = True
            assert_bad(bool_index, "exact integers")

            wrong_pad = copy.deepcopy(good_rows)
            wrong_pad[1]["supplied_inputs"][0] = "01" + "00" * 10
            assert_bad(wrong_pad, "disagree with the MWRC input cursor")

            wrong_setup = copy.deepcopy(good_rows)
            setup = next(row for row in wrong_setup
                         if row.get("record") == "session_match_enter_complete")
            setup["declared_setup"]["players"][0]["character_kind"] ^= 1
            assert_bad(wrong_setup, "declared_setup differs")

            wrong_entity = copy.deepcopy(good_rows)
            setup = next(row for row in wrong_entity
                         if row.get("record") == "session_match_enter_complete")
            setup["fighter_entities"][2]["entity_index"] = 1
            assert_bad(wrong_entity, "missing, extra, or reordered")

            wrong_generation = copy.deepcopy(good_rows)
            frame = next(row for row in wrong_generation
                         if row.get("record") == "session_frame" and row.get("scene") == SCENES["match"])
            frame["fighter_entities"][0]["generation"] = True
            assert_bad(wrong_generation, "expected integer")

            too_few_match_fighters = copy.deepcopy(good_rows)
            frame = next(row for row in too_few_match_fighters
                         if row.get("record") == "session_frame" and
                         row.get("scene") == SCENES["match"])
            frame["fighters"].pop()
            assert_bad(too_few_match_fighters, "requires four primary fighters")

            extra = copy.deepcopy(good_rows)
            extra.append({"record": "end", "frames": 5, "status": "captured"})
            assert_bad(extra, "records after the exported prefix")

            write(good_rows)
            trace_path.write_text(trace_path.read_text(encoding="utf-8") + "{\"record\":\n",
                                  encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "invalid JSON"):
                _validate_v10_browser_export(trace_path, recipe, 5, packet)

            write(good_rows)
            with mock.patch("whole_session_state_compare.V10_BROWSER_EXPORT_RECORD_CAP", 6):
                with self.assertRaisesRegex(ComparisonError, "bounded record count"):
                    _validate_v10_browser_export(trace_path, recipe, 5, packet)

    def test_v10_export_tail_checks_typed_shape_without_comparing_tail_state(self):
        recipe_raw = _v10_recipe_bytes()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe_path = root / "recipe.mwrc"
            recipe_path.write_bytes(recipe_raw)
            recipe = Recipe(recipe_path, recipe_raw,
                            scope=V10_FIRST_SETUP_TICK0_SCOPE)
            trace_path = root / "port.jsonl"
            rows = _browser_export_rows(recipe, 11)
            tail_match = next(row for row in rows
                              if row.get("record") == "session_frame" and
                              row.get("index") == 10)
            # This is a later, un-compared match row. A valid but different state
            # value remains a shape check, not a source-equivalence assertion.
            tail_match["fighters"][0]["kind"] ^= 1
            trace_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                        for row in rows) + "\n", encoding="utf-8")
            self.assertEqual(_validate_v10_browser_export(
                trace_path, recipe, 11, {"source": {}}), 15)

            tail_match["fighters"][0]["kind"] = True
            trace_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                        for row in rows) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "expected integer"):
                _validate_v10_browser_export(trace_path, recipe, 11, {"source": {}})

            tail_match["fighters"][0]["kind"] = 1
            tail_match["fighter_entities"][0]["generation"] = True
            trace_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                        for row in rows) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "expected integer"):
                _validate_v10_browser_export(trace_path, recipe, 11, {"source": {}})

    def test_invalid_browser_provenance_never_opens_the_original_stream(self):
        recipe_raw = _v10_recipe_bytes()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe_path = root / "recipe.mwrc"
            recipe_path.write_bytes(recipe_raw)
            recipe_sha = hashlib.sha256(recipe_raw).hexdigest()
            reference_path = root / "reference.mwro"
            reference_path.write_bytes(b"reference input remains unopened")
            source_identity = {
                "trace_bytes": reference_path.stat().st_size,
                "recorded_full_trace_sha256": "b" * 64,
                "full_trace_rehashed": False,
                "audit_records_decoded": 1, "audit_bytes_read": 1,
                "manifest_sha256": "c" * 64, "source_report_sha256": "d" * 64,
                "audit_sha256": "e" * 64,
            }
            paths = {
                name: root / f"{name}.json"
                for name in ("source_manifest", "source_report", "source_audit",
                             "browser_capture_report", "browser_producer_manifest",
                             "browser_report", "port_trace")
            }
            packet = {
                "source": {
                    "trace": {"bytes": reference_path.stat().st_size,
                              "recorded_full_sha256": "b" * 64},
                    "capture_id": "capture", "sequence_id": "sequence",
                },
                "recipe": {
                    "path": str(recipe_path), "bytes": len(recipe_raw),
                    "sha256": recipe_sha, "version": 10,
                    "frame_count": 12, "seed": 0x12345678,
                },
                "browser": {},
            }
            with (mock.patch("whole_session_state_compare._load_expectations",
                             return_value=(packet, "f" * 64)),
                  mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                             return_value=({}, {}, {}, source_identity)),
                  mock.patch("whole_session_state_compare._validate_v10_browser_provenance",
                             side_effect=ComparisonError("bad browser export")),
                  mock.patch("whole_session_state_compare.iter_records") as read_source):
                result = compare_paths(
                    reference_path, recipe_path, paths["port_trace"],
                    scope=V10_FIRST_SETUP_TICK0_SCOPE,
                    expectations=root / "frozen-expectations.json",
                    source_manifest=paths["source_manifest"],
                    source_report=paths["source_report"],
                    source_audit=paths["source_audit"],
                    browser_capture_report=paths["browser_capture_report"],
                    browser_producer_manifest=paths["browser_producer_manifest"],
                    browser_report=paths["browser_report"],
                )
            self.assertEqual(result["result"], "invalid")
            self.assertIn("bad browser export", result["error"])
            read_source.assert_not_called()

    def test_positive_scope_passes_caps_to_reader_and_cannot_pass_on_cap_stop(self):
        with tempfile.TemporaryDirectory() as directory:
            packet_path, selected, packet, _, _ = _positive_expectation_packet(Path(directory))
            recipe_raw = _v10_recipe_bytes()
            selected["recipe"].write_bytes(recipe_raw)
            packet["recipe"].update({
                "bytes": len(recipe_raw),
                "sha256": hashlib.sha256(recipe_raw).hexdigest(),
                "version": 10,
                "frame_count": 12,
                "seed": 0x12345678,
            })
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            packet_sha = hashlib.sha256(packet_path.read_bytes()).hexdigest()
            observed_limits = {}

            class FakeBrowser:
                header = {"frames_requested": 12}

                def close(self):
                    pass

            source_identity = {
                "trace_bytes": selected["reference"].stat().st_size,
                "recorded_full_trace_sha256": "b" * 64,
                "full_trace_rehashed": False,
                "audit_records_decoded": 1,
                "audit_bytes_read": 1,
                "manifest_sha256": "a" * 64,
                "source_report_sha256": "a" * 64,
                "audit_sha256": "a" * 64,
            }
            browser_identity = {
                "required_cursor": 6,
                "target_cursor": 6,
                "capture_report_sha256": "c" * 64,
                "producer_manifest_sha256": "c" * 64,
                "browser_report_sha256": "c" * 64,
                "port_trace_sha256": "c" * 64,
            }

            def capped_source(path, *, max_bytes, max_records, stats):
                observed_limits.update(max_bytes=max_bytes, max_records=max_records)
                raise ComparisonError("bounded source record cap reached before target")
                yield  # pragma: no cover

            with (mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                             return_value=({}, {}, {}, source_identity)),
                  mock.patch("whole_session_state_compare._validate_v10_browser_provenance",
                             return_value=({}, {}, {}, browser_identity)),
                  mock.patch("whole_session_state_compare.BrowserReader",
                             return_value=FakeBrowser()),
                  mock.patch("whole_session_state_compare.iter_records",
                             side_effect=capped_source)):
                result = compare_paths(
                    selected["reference"], selected["recipe"], selected["port_trace"],
                    scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
                    expectations=packet_path,
                    source_manifest=selected["source_manifest"],
                    source_report=selected["source_report"],
                    source_audit=selected["source_audit"],
                    browser_capture_report=selected["browser_capture_report"],
                    browser_producer_manifest=selected["browser_producer_manifest"],
                    browser_report=selected["browser_report"],
                    positive_boundary_audit=selected["positive_boundary_audit"],
                )
            self.assertEqual(observed_limits, {
                "max_bytes": V10_PREFIX_BYTE_CAP,
                "max_records": V10_FIRST_POSITIVE_RECORD_CAP,
            })
            self.assertEqual(result["result"], "invalid")
            self.assertFalse(result["complete"])
            self.assertFalse(result["whole_session_equivalent"])
            self.assertEqual(result["expectations"]["sha256"], packet_sha)

    def test_positive_comparator_requires_contiguous_zero_clock_and_exact_target(self):
        pads = ["00" * 11] * 4
        spans = [
            {"scene": SCENES["css"], "first_frame": 0, "last_frame": 0},
            {"scene": SCENES["sss"], "first_frame": 1, "last_frame": 1},
            {"scene": SCENES["match"], "first_frame": 2, "last_frame": 7},
            {"scene": SCENES["results"], "first_frame": 8, "last_frame": 8},
            {"scene": SCENES["css"], "first_frame": 9, "last_frame": 9},
            {"scene": SCENES["sss"], "first_frame": 10, "last_frame": 10},
            {"scene": SCENES["match"], "first_frame": 11, "last_frame": 12},
            {"scene": SCENES["results"], "first_frame": 13, "last_frame": 13},
            {"scene": SCENES["css"], "first_frame": 14, "last_frame": 14},
            {"scene": SCENES["sss"], "first_frame": 15, "last_frame": 15},
            {"scene": SCENES["match"], "first_frame": 16, "last_frame": 16},
            {"scene": SCENES["results"], "first_frame": 17, "last_frame": 17},
        ]
        frames = [{"index": index, "scene": span["scene"], "pads": list(pads)}
                  for span in spans
                  for index in range(span["first_frame"], span["last_frame"] + 1)]
        recipe = SimpleNamespace(
            frame_count=len(frames), version=10,
            scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
            entity_profile=PRIMARY_STATIC_ENTITY_PROFILE,
            spans=spans, frames=frames, seed=0,
        )
        target = {
            "match_index": 0, "source_tick": 3,
            "source_sequence": 50, "pad_consume_sequence": 49,
            "timeline_frame_index": 5, "browser_cursor": 6, "match_frame": 1,
        }

        class FakeBrowser:
            header = {"frames_requested": len(frames)}
            line = 2

            def __init__(self, row):
                self.row = row
                self.reads = 0

            def next(self):
                self.reads += 1
                return self.row

        def frame_for(tick, match_frame, *, sequence=50, pad_sequence=49, pads_value=None):
            state = _v9_state({"slices": _state_slices(tick=tick, match_frame=match_frame)})
            index = _first_match_timeline_index(recipe) + tick
            source_pads = list(pads if pads_value is None else pads_value)
            row = {
                "record": "session_frame", "scene": SCENES["match"], "index": index,
                "supplied_inputs": list(pads), "rng": state["rng"],
                "match_frame": state["match_frame"],
                "pad_state_hex": state["pad_state_hex"],
                "fighters": copy.deepcopy(state["fighters"]),
                "fighter_entities": copy.deepcopy(state["fighter_entities"]),
            }
            frame = {
                "scene": SCENES["match"], "pads": source_pads,
                "state": state, "match_index": 0, "source_tick": tick,
                "source_tick_seq": sequence, "source_seq": pad_sequence,
            }
            return index, frame, row

        target_index, frame, row = frame_for(3, 1)
        browser = FakeBrowser(row)
        comparator = Comparator(recipe, browser, positive_boundary=target)
        comparator.frame_index = target_index
        comparator.compared = target_index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        comparator.on_frame(frame)
        self.assertEqual(comparator.frame_index, target["browser_cursor"])
        self.assertEqual(comparator.match_compared, 4)

        index, source_frame, reordered_row = frame_for(3, 1)
        reordered_row["index"] += 1
        browser = FakeBrowser(reordered_row)
        comparator = Comparator(recipe, browser, positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "missing, extra, or reordered"):
            comparator.on_frame(source_frame)

        index, source_frame, early_clock_row = frame_for(2, 0)
        early_clock_row["match_frame"] = 1
        comparator = Comparator(recipe, FakeBrowser(early_clock_row),
                                positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 2
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "exact state differs at match_frame"):
            comparator.on_frame(source_frame)
        self.assertEqual(comparator.first_difference["field"], "match_frame")

        for tick, clock in ((2, 1), (4, 1)):
            index, bad_frame, bad_row = frame_for(tick, clock)
            browser = FakeBrowser(bad_row)
            comparator = Comparator(recipe, browser, positive_boundary=target)
            comparator.frame_index = index
            comparator.compared = index
            comparator.match_compared = tick
            comparator.setup_count = 1
            comparator.current_match = 0
            with self.assertRaisesRegex(ComparisonError, "clock became positive|advanced past"):
                comparator.on_frame(bad_frame)
            self.assertEqual(browser.reads, 0)

        index, late_clock_frame, late_clock_row = frame_for(3, 0)
        browser = FakeBrowser(late_clock_row)
        comparator = Comparator(recipe, browser, positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "match_frame"):
            comparator.on_frame(late_clock_frame)
        self.assertEqual(browser.reads, 0)

        index, bad_frame, bad_row = frame_for(3, 1, sequence=51)
        browser = FakeBrowser(bad_row)
        comparator = Comparator(recipe, browser, positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "source_sequence"):
            comparator.on_frame(bad_frame)
        self.assertEqual(browser.reads, 0)

        index, bad_pad_frame, bad_row = frame_for(3, 1, pad_sequence=48)
        comparator = Comparator(recipe, FakeBrowser(bad_row), positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "pad_consume_sequence"):
            comparator.on_frame(bad_pad_frame)

        altered_pads = list(pads)
        altered_pads[0] = "01" + "00" * 10
        index, bad_frame, bad_row = frame_for(3, 1, pads_value=altered_pads)
        comparator = Comparator(recipe, FakeBrowser(bad_row), positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "PAD/scene order"):
            comparator.on_frame(bad_frame)

        index, source_frame, divergent_row = frame_for(3, 1)
        divergent_row["match_frame"] = 0
        browser = FakeBrowser(divergent_row)
        comparator = Comparator(recipe, browser, positive_boundary=target)
        comparator.frame_index = index
        comparator.compared = index
        comparator.match_compared = 3
        comparator.setup_count = 1
        comparator.current_match = 0
        with self.assertRaisesRegex(ComparisonError, "exact state differs at match_frame"):
            comparator.on_frame(source_frame)
        self.assertEqual(comparator.first_difference["field"], "match_frame")

    def test_external_expectations_require_recorded_disc_and_runtime_identities(self):
        with tempfile.TemporaryDirectory() as directory:
            packet_path, selected, packet = _expectation_packet(
                Path(directory), capture_id="capture-alt", sequence_id="sequence-alt",
                producer_head="1" * 40)
            packet["browser"].pop("disc")
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "disc identity"):
                _load_expectations(packet_path, selected)
            packet["browser"]["disc"] = {
                "path": str(Path(directory) / "disc.ciso"), "bytes": 5,
                "sha256": "e" * 64,
            }
            packet["browser"].pop("runtime_data")
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "runtime_data identity"):
                _load_expectations(packet_path, selected)

    def test_failure_result_retains_frozen_inputs_provenance_and_last_source_sequence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            packet_path, selected, packet = _expectation_packet(
                root, capture_id="capture-alt", sequence_id="sequence-alt",
                producer_head="1" * 40)
            recipe_raw = _v10_recipe_bytes()
            selected["recipe"].write_bytes(recipe_raw)
            packet["recipe"].update({
                "bytes": len(recipe_raw),
                "sha256": hashlib.sha256(recipe_raw).hexdigest(),
                "version": 10,
                "frame_count": 12,
                "seed": 0x12345678,
            })
            packet_path.write_text(json.dumps(packet), encoding="utf-8")
            packet_sha = hashlib.sha256(packet_path.read_bytes()).hexdigest()

            class FakeBrowser:
                header = {"frames_requested": 12}

                def close(self):
                    pass

            def failed_source(path, *, max_bytes, max_records, stats):
                raw = b"bad-row"
                stats.record_bytes(raw)
                stats.records_read = 1
                yield {"seq": 0, "event": "unexpected", "payload": {}}

            source_identity = {
                "trace_bytes": selected["reference"].stat().st_size,
                "recorded_full_trace_sha256": "b" * 64,
                "full_trace_rehashed": False,
                "audit_records_decoded": 1,
                "audit_bytes_read": 7,
                "manifest_sha256": "a" * 64,
                "source_report_sha256": "a" * 64,
                "audit_sha256": "a" * 64,
            }
            browser_identity = {
                "required_cursor": 3,
                "capture_report_sha256": "c" * 64,
                "producer_manifest_sha256": "c" * 64,
                "browser_report_sha256": "c" * 64,
                "port_trace_sha256": "c" * 64,
            }
            with (mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                             return_value=({}, {}, {}, source_identity)),
                  mock.patch("whole_session_state_compare._validate_v10_browser_provenance",
                             return_value=({}, {}, {}, browser_identity)),
                  mock.patch("whole_session_state_compare.BrowserReader", return_value=FakeBrowser()),
                  mock.patch("whole_session_state_compare.iter_records", side_effect=failed_source)):
                result = compare_paths(
                    selected["reference"], selected["recipe"], selected["port_trace"],
                    scope=V10_FIRST_SETUP_TICK0_SCOPE,
                    expectations=packet_path,
                    source_manifest=selected["source_manifest"],
                    source_report=selected["source_report"],
                    source_audit=selected["source_audit"],
                    browser_capture_report=selected["browser_capture_report"],
                    browser_producer_manifest=selected["browser_producer_manifest"],
                    browser_report=selected["browser_report"],
                )
            self.assertEqual(result["result"], "invalid")
            self.assertEqual(result["expectations"]["sha256"], packet_sha)
            self.assertEqual(result["selected_input_identities"]["source"]["capture_id"],
                             "capture-alt")
            self.assertEqual(result["checks"]["source_provenance"], "pass")
            self.assertEqual(result["checks"]["browser_capture_provenance"], "pass")
            self.assertEqual(result["last_source_sequence"], 0)
            self.assertEqual(result["source_prefix"]["records_read"], 1)
            self.assertFalse(result["source_full_trace"]["full_trace_rehashed"])
            self.assertTrue(result["source_full_trace"]["stat_stable_during_attempt"])
            self.assertIn("stat_before", result["source_full_trace"])
            self.assertIn("stat_after", result["source_full_trace"])

    def test_source_report_and_manifest_reject_mutually_matching_wrong_identity(self):
        expected_source = {
            "capture_id": "capture-alt",
            "sequence_id": "sequence-alt",
            "trace": {"recorded_full_sha256": "a" * 64},
        }
        capture = {
            "capture_id": "capture-alt", "sequence_id": "sequence-alt",
            "match_count": 3, "source_revision": "GALE01r2",
            "observer_schema": EXPECTED_OBSERVER_SCHEMA, "observer_version": 1,
            "dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
            "dol_sha1": EXPECTED_DOL_SHA1, "dol_sha256": EXPECTED_DOL_SHA256,
            "cpu": "JITARM64",
        }
        _validate_source_manifest_capture(capture, expected_source)
        handshake = {
            "schema": EXPECTED_OBSERVER_SCHEMA, "version": 1,
            "capture_id": "capture-alt", "sequence_id": "sequence-alt",
            "match_count": 3, "whole_session": True,
            "dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
            "dol_sha1": EXPECTED_DOL_SHA1, "dol_sha256": EXPECTED_DOL_SHA256,
            "cpu": "JITARM64", "fighter_entity_profile": "v10-live-static-player-pair",
            "writes_guest_memory": False,
        }
        report = {
            "schema": "melee-web-recorded-session-12-character-capture-v1",
            "result": "three_match_source_capture_complete",
            "input_mode": "ordinary-controller-pipe-record",
            "lineup_profile": "v10-fighter-coverage", "readiness_only": False,
            "capture_id": "capture-alt", "raw_observer_sha256": "a" * 64,
            "observer_handshake": handshake,
            "observer_identity": {"capture_id": "capture-alt", "sequence_id": "sequence-alt"},
        }
        identity = _validate_source_capture_report(report, expected_source, capture)
        self.assertEqual(identity["capture_id"], "capture-alt")

        wrong_capture = dict(capture, dolphin_commit="0" * 40)
        with self.assertRaisesRegex(ComparisonError, "dolphin_commit"):
            _validate_source_manifest_capture(wrong_capture, expected_source)
        wrong_handshake = dict(handshake, dolphin_commit="0" * 40)
        wrong_report = dict(report, observer_handshake=wrong_handshake)
        with self.assertRaisesRegex(ComparisonError, "dolphin_commit"):
            _validate_source_capture_report(wrong_report, expected_source, wrong_capture)
        for field, value in (("fighter_entity_profile", "different-profile"),
                             ("writes_guest_memory", True)):
            wrong_report = dict(report, observer_handshake=dict(handshake, **{field: value}))
            with self.subTest(field=field), self.assertRaises(ComparisonError):
                _validate_source_capture_report(wrong_report, expected_source, capture)
        for field, value in (("input_mode", "replay"),
                             ("lineup_profile", "arbitrary-roster"),
                             ("readiness_only", True)):
            wrong_report = dict(report, **{field: value})
            with self.subTest(field=field), self.assertRaises(ComparisonError):
                _validate_source_capture_report(wrong_report, expected_source, capture)

    def test_browser_producer_identity_must_match_the_frozen_packet(self):
        expected = {"producer": {
            "branch": "codex/second-capture", "head": "4" * 40,
            "tree": "5" * 40, "base_main": "6" * 40,
        }}
        producer = {
            "schema": "melee-web-b4-match-entry-producer-source-v1",
            "source": {**expected["producer"], "clean": True},
        }
        self.assertTrue(_validate_browser_producer_source(producer, expected)["clean"])
        producer["source"]["head"] = "7" * 40
        with self.assertRaisesRegex(ComparisonError, "head"):
            _validate_browser_producer_source(producer, expected)

    def test_historical_producer_keeps_shared_identity_contract_and_explicit_scope(self):
        expected = {"producer": {
            "branch": "codex/frozen", "head": "1" * 40,
            "tree": "2" * 40, "base_main": "3" * 40,
        }}
        for schema in (V10_BROWSER_PRODUCER_SCHEMA,
                       V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA):
            producer = {"schema": schema,
                        "source": {**expected["producer"], "clean": True}}
            self.assertTrue(_validate_browser_producer_source(
                producer, expected, scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)["clean"])
            for field in ("branch", "head", "tree", "base_main", "clean"):
                wrong = copy.deepcopy(producer)
                wrong["source"][field] = False if field == "clean" else "different"
                with self.subTest(schema=schema, field=field), self.assertRaises(ComparisonError):
                    _validate_browser_producer_source(
                        wrong, expected, scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
            for malformed in (None, [], {}):
                with self.subTest(schema=schema, malformed=malformed), self.assertRaises(ComparisonError):
                    _validate_browser_producer_source(
                        {"schema": schema, "source": malformed}, expected,
                        scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
        producer = {"schema": V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA,
                    "source": {**expected["producer"], "clean": True}}
        for scope in (None, WHOLE_SESSION_SCOPE, V10_FIRST_SETUP_TICK0_SCOPE,
                      V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE, V10_FIRST_MATCH_CLOCK_GE60_SCOPE):
            with self.subTest(scope=scope), self.assertRaisesRegex(ComparisonError, "post-clock60"):
                _validate_browser_producer_source(producer, expected, scope=scope)
        producer["schema"] = "unrecognized-producer-v1"
        with self.assertRaisesRegex(ComparisonError, "schema is unsupported"):
            _validate_browser_producer_source(
                producer, expected, scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)

    def test_browser_capture_provenance_rejects_pre_stop_errors_without_rehashing_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe_path = root / "recipe.mwrc"
            recipe_raw = _v10_recipe_bytes()
            recipe_path.write_bytes(recipe_raw)
            recipe = Recipe(recipe_path, recipe_raw, scope=V10_FIRST_SETUP_TICK0_SCOPE)
            recipe_sha = hashlib.sha256(recipe_raw).hexdigest()

            trace_path = root / "port.jsonl"
            exported_cursor = 5
            _write_browser_export(trace_path, recipe, exported_cursor)
            trace_sha = hashlib.sha256(trace_path.read_bytes()).hexdigest()
            failures = [
                "whole-session final CSS was not entered",
                "Manual unload stopped the replay",
                "incomplete input timeline",
                "source tick/draw count mismatch",
            ]
            report = {
                "schema": "melee-web-browser-retail-replay", "version": 1,
                "recipe_sha256": recipe_sha, "frames": recipe.frame_count,
                "mode": "state_capture", "complete": False, "pass": False,
                "final_scene": None, "instrumented_timing_resumes": 0,
                "failures": failures,
                "metrics": {"sourceFrames": exported_cursor,
                            "sourceSteps": exported_cursor,
                            "sourceDraws": exported_cursor},
                "trace_sha256": trace_sha,
            }
            browser_report_path = root / "retail-report.json"
            browser_report_path.write_text(json.dumps(report), encoding="utf-8")

            runtime_path = root / "build" / "gameplay_menu_browser.data"
            producer = {
                "schema": "melee-web-b4-match-entry-producer-source-v1",
                "source": {"branch": "codex/frozen", "head": "1" * 40,
                           "tree": "2" * 40, "base_main": "3" * 40, "clean": True},
                "build": {
                    "configuration": "Release", "target": "runtime",
                    "artifact_count": 1,
                    "directory": str(runtime_path.parent),
                    "artifacts": {"gameplay_menu_browser.data": {
                        "bytes": 3, "sha256": "c" * 64,
                    }},
                    "default_off_gates": dict(V10_DEFAULT_OFF_GATES),
                },
            }
            producer_path = root / "producer.json"
            producer_path.write_text(json.dumps(producer), encoding="utf-8")
            producer_sha = hashlib.sha256(producer_path.read_bytes()).hexdigest()

            capture_path = root / "capture.json"
            expected_download_names = ["retail-port.jsonl", "retail-browser-report.json"]
            report_bytes = browser_report_path.stat().st_size
            report_sha = hashlib.sha256(browser_report_path.read_bytes()).hexdigest()
            error_message = "Browser replay report failed: " + json.dumps(
                failures, separators=(",", ":"))
            unloaded_snapshot = {
                "source_cursor": 0, "phase": 0, "running": 0,
                "at_ms": 101, "runtime_error": None,
                "replay_report": report,
                "replay_downloads": expected_download_names,
            }
            wrapper = {
                "schema": "melee-web-headless-whole-session-replay-v1",
                "mode": "state", "result": "incomplete",
                "resume_timing_pauses": False, "timing_pause_resumes": [],
                "probe": None, "verified_runtime_data_aborts": [],
                "inputs": {
                    "recipe": {"path": str(recipe_path), "bytes": recipe_path.stat().st_size,
                               "sha256": recipe_sha},
                    "manifest": {"path": str(producer_path),
                                 "bytes": producer_path.stat().st_size,
                                 "sha256": producer_sha},
                    "disc": {"path": str(root / "rev2.ciso"),
                             "bytes": 1_449_165_376,
                             "sha256": "b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c"},
                    "runtime_data": {"path": str(runtime_path), "bytes": 3,
                             "sha256": "c" * 64},
                },
                "deliberate_prefix_stop": {"requested_cursor": 3,
                                            "observed_cursor": 4},
                "snapshots": [
                    {"source_cursor": None, "phase": None, "running": None,
                     "replay_report": None, "replay_downloads": [],
                     "runtime_error": None, "at_ms": 1.25,
                     "status": "startup text can vary", "reason": "unclassified"},
                    {"source_cursor": 0, "phase": 7, "running": 1,
                     "at_ms": 10, "runtime_error": None,
                     "replay_report": None, "replay_downloads": []},
                    {"source_cursor": 4, "phase": 7, "running": 1,
                     "at_ms": 100, "runtime_error": None,
                     "replay_report": None, "replay_downloads": []},
                    unloaded_snapshot,
                ],
                "final_snapshot": unloaded_snapshot,
                "first_mismatch": {
                    "phase": "whole-session-replay",
                    "failure": failures[0], "snapshot": unloaded_snapshot,
                },
                "first_error": {
                    "kind": "whole-session-replay", "phase": "whole-session-replay",
                    "message": error_message,
                    "details": {"at_ms": 102, "phase": 0, "running": 0,
                                "source_cursor": 0, "runtime_error": None,
                                "replay_report": report,
                                "replay_downloads": expected_download_names},
                },
                "failure": f"Error: {error_message}\n    at fixture",
                "browser_errors": [], "unexpected_requests": [],
                "saved_downloads": [
                    {"name": "retail-port.jsonl", "bytes": trace_path.stat().st_size,
                     "sha256": trace_sha},
                    {"name": "retail-browser-report.json", "bytes": report_bytes,
                     "sha256": report_sha},
                ],
                "browser_report": report,
            }
            capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
            capture_sha = hashlib.sha256(capture_path.read_bytes()).hexdigest()
            expected_files = {
                "capture_report": {"path": str(capture_path), "bytes": capture_path.stat().st_size,
                                   "sha256": capture_sha},
                "producer_manifest": {"path": str(producer_path),
                                      "bytes": producer_path.stat().st_size,
                                      "sha256": producer_sha},
                "report": {"path": str(browser_report_path),
                           "bytes": browser_report_path.stat().st_size,
                           "sha256": hashlib.sha256(browser_report_path.read_bytes()).hexdigest()},
                "trace": {"path": str(trace_path), "bytes": trace_path.stat().st_size,
                          "sha256": trace_sha},
            }
            packet = {"browser": {
                **expected_files,
                "disc": {"path": str(root / "rev2.ciso"),
                         "bytes": 1_449_165_376,
                         "sha256": "b7de482eb955c8a96b6746dfa043b69ae7bf6c7c2a09ac382b9da126faa7055c"},
                "runtime_data": {"path": str(runtime_path), "bytes": 3,
                                 "sha256": "c" * 64, "freshly_rehashed": False},
                "producer": producer["source"],
            }, "source": {"trace": {}, "manifest": {}, "report": {}, "audit": {}},
                "recipe": {}}
            _, _, _, identity = _validate_v10_browser_provenance(
                capture_path, producer_path, browser_report_path, trace_path,
                recipe_path, recipe_sha, recipe, packet)
            self.assertEqual(identity["required_cursor"], 3)
            self.assertEqual(identity["target_cursor"], 4)
            self.assertEqual(identity["requested_cursor"], 3)
            self.assertEqual(identity["observed_cursor"], 4)
            self.assertEqual(identity["exported_cursor"], 5)
            self.assertEqual(identity["browser_records_validated"], 7)
            self.assertFalse(identity["runtime_data_recorded_identity"]["freshly_rehashed"])

            # Read the historical format through the real provenance gates.
            # Export shape has its own tests; this fixture uses tick-0 rows.
            canonical_producer = copy.deepcopy(producer)
            producer["schema"] = V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA
            def write_producer(*, bind=True):
                producer_path.write_text(json.dumps(producer), encoding="utf-8")
                if bind:
                    entry = {"path": str(producer_path),
                             "bytes": producer_path.stat().st_size,
                             "sha256": hashlib.sha256(producer_path.read_bytes()).hexdigest()}
                    packet["browser"]["producer_manifest"] = entry
                    wrapper["inputs"]["manifest"] = dict(entry)
                capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
                packet["browser"]["capture_report"].update(
                    bytes=capture_path.stat().st_size,
                    sha256=hashlib.sha256(capture_path.read_bytes()).hexdigest())
            write_producer()
            recipe.scope = V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE
            with mock.patch("whole_session_state_compare._validate_v10_browser_export", return_value=7):
                _, _, _, historical_identity = _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet, required_cursor=3)
            self.assertEqual(historical_identity["producer_manifest_schema"],
                             V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA)
            self.assertEqual(historical_identity["producer_manifest_sha256"],
                             hashlib.sha256(producer_path.read_bytes()).hexdigest())
            historical_producer = copy.deepcopy(producer)
            for field, value, message in (
                    ("configuration", "Debug", "Release runtime"),
                    ("target", "different", "Release runtime"),
                    ("artifact_count", 2, "Release runtime"),
                    ("default_off_gates", {}, "non-default gate"),
                    ("directory", str(root / "different-build"), "artifact inventory"),
                    ("artifacts", {"gameplay_menu_browser.data": {"bytes": 3, "sha256": "bad"}},
                     "inventory is malformed")):
                producer = copy.deepcopy(historical_producer)
                producer["build"][field] = value
                write_producer()
                with self.subTest(historical_build=field), self.assertRaisesRegex(ComparisonError, message):
                    _validate_v10_browser_provenance(
                        capture_path, producer_path, browser_report_path, trace_path,
                        recipe_path, recipe_sha, recipe, packet, required_cursor=3)
            producer = copy.deepcopy(historical_producer)
            write_producer()
            producer["source"]["head"] = "9" * 40
            write_producer(bind=False)
            with self.assertRaisesRegex(ComparisonError, "manifest hash differs"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet, required_cursor=3)
            producer = canonical_producer
            recipe.scope = V10_FIRST_SETUP_TICK0_SCOPE
            write_producer()

            # Rebind the sidecar hash after every mutation so the transport gate,
            # rather than stale fixture identity, rejects the altered evidence.
            def write_capture():
                capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
                expected_files["capture_report"].update(
                    bytes=capture_path.stat().st_size,
                    sha256=hashlib.sha256(capture_path.read_bytes()).hexdigest())

            wrapper["url"] = "http://127.0.0.1:8813/runtime.html?diagnostic=1"
            wrapper["runtime_data_load"] = {
                "file_bytes": 3, "loaded_bytes": 3, "total_bytes": 3,
                "sha256": "c" * 64, "from_cache": False,
            }
            abort = {
                "requestCount": 1, "responseCount": 1, "failureCount": 1,
                "finishedCount": 0, "url": "http://127.0.0.1:8813/gameplay_menu_browser.data",
                "expectedUrl": "http://127.0.0.1:8813/gameplay_menu_browser.data",
                "method": "GET", "resourceType": "fetch", "errorText": "net::ERR_ABORTED",
                "responseStatus": 200, "contentLength": 3, "loadedBytes": 3,
                "totalBytes": 3, "fileBytes": 3, "expectedBytes": 3,
                "expectedSha256": "c" * 64, "actualSha256": "c" * 64,
                "fromCache": False,
            }
            wrapper["verified_runtime_data_aborts"] = [abort]
            write_capture()
            _validate_v10_browser_provenance(
                capture_path, producer_path, browser_report_path, trace_path,
                recipe_path, recipe_sha, recipe, packet)
            self.assertFalse(runtime_path.exists(), "recorded identity needs no runtime package")

            good_wrapper = copy.deepcopy(wrapper)
            navigation_wrapper = copy.deepcopy(good_wrapper)
            navigation_wrapper["phases"] = [{"name": "http-load", "result": "pass"}]
            navigation_start = navigation_wrapper["snapshots"][0]
            navigation_start.update(reason="phase-start:http-load", at_ms=64)
            navigation_end = {**navigation_start, "reason": "phase-end:http-load",
                              "at_ms": 56.24000000953674}
            navigation_wrapper["snapshots"].insert(1, navigation_end)
            navigation_wrapper["snapshots"][2]["at_ms"] = 70
            wrapper = copy.deepcopy(navigation_wrapper)
            write_capture()
            _validate_v10_browser_provenance(
                capture_path, producer_path, browser_report_path, trace_path,
                recipe_path, recipe_sha, recipe, packet)

            navigation_mutations = []
            for index in (0, 1):
                navigation_mutations.append((f"wrong navigation reason {index}",
                                             ("snapshot", index, "reason", "phase-poll:http-load")))
                for field, value in (("source_cursor", 0), ("phase", 0), ("running", 0),
                                     ("replay_report", {}), ("runtime_error", "failure"),
                                     ("replay_downloads", ["retail-port.jsonl"])):
                    navigation_mutations.append((f"available navigation {index}.{field}",
                                                 ("snapshot", index, field, value)))
            for index in range(5):
                for value in (True, float("inf"), float("nan")):
                    navigation_mutations.append((f"invalid navigation timestamp {index}={value!r}",
                                                 ("snapshot", index, "at_ms", value)))
            navigation_mutations.extend([
                ("missing phase manifest", ("wrapper", "phases", None)),
                ("failed navigation phase", ("wrapper", "phases", [{"name": "http-load", "result": "fail"}])),
                ("wrong first phase", ("wrapper", "phases", [{"name": "runtime-ready", "result": "pass"}])),
                ("later live reversal", ("snapshot", 3, "at_ms", 69)),
                ("later export reversal", ("snapshot", 4, "at_ms", 99)),
                ("late navigation pair", ("late_pair",)),
                ("later unavailable reversal", ("later_unavailable",)),
            ])
            for label, mutation in navigation_mutations:
                with self.subTest(navigation_time=label):
                    wrapper = copy.deepcopy(navigation_wrapper)
                    if mutation[0] == "snapshot":
                        _, index, field, value = mutation
                        wrapper["snapshots"][index][field] = value
                    elif mutation[0] == "wrapper":
                        wrapper[mutation[1]] = mutation[2]
                    elif mutation[0] == "late_pair":
                        wrapper["snapshots"].insert(0, {**navigation_start, "at_ms": 60,
                                                       "reason": "unclassified"})
                    else:
                        wrapper["snapshots"].insert(2, {**navigation_end, "at_ms": 55,
                                                       "reason": "phase-start:runtime-ready"})
                    write_capture()
                    with self.assertRaises(ComparisonError):
                        _validate_v10_browser_provenance(
                            capture_path, producer_path, browser_report_path, trace_path,
                            recipe_path, recipe_sha, recipe, packet)
            wrapper = copy.deepcopy(good_wrapper)
            write_capture()
            mutations = []
            bad_values = {
                "requestCount": 2, "responseCount": 0, "failureCount": 2,
                "finishedCount": 1, "url": "http://127.0.0.1:8813/other.data",
                "expectedUrl": "http://127.0.0.1:8813/other.data", "method": "POST",
                "resourceType": "xhr", "errorText": "net::ERR_FAILED",
                "responseStatus": 206, "contentLength": 2, "loadedBytes": 2,
                "totalBytes": 2, "fileBytes": 2, "expectedBytes": 2,
                "expectedSha256": "d" * 64, "actualSha256": "d" * 64, "fromCache": True,
            }
            for field, value in bad_values.items():
                mutations.append((field, {**abort, field: value}, None))
            for field, value in abort.items():
                if type(value) is int:
                    for invalid in (bool(value), float(value)):
                        mutations.append((f"typed {field}={invalid!r}",
                                          {**abort, field: invalid}, None))
            mutations.extend([
                ("missing field", {k: v for k, v in abort.items() if k != "fileBytes"}, None),
                ("extra field", {**abort, "unrelated": True}, None),
                ("unrelated row", {"errorText": "net::ERR_ABORTED"}, None),
                ("null row", None, None),
            ])
            for field, value in wrapper["runtime_data_load"].items():
                mutations.append((f"disconnected load {field}", abort,
                                  ("runtime_data_load", {**wrapper["runtime_data_load"],
                                                         field: 0 if type(value) is int else None})))
            for field in ("url", "runtime_data_load"):
                mutations.append((f"missing {field}", abort, (field, None)))
            mutations.append(("different navigation", abort,
                              ("url", "http://127.0.0.1:8814/runtime.html")))
            for label, changed_abort, changed_wrapper in mutations:
                with self.subTest(runtime_abort=label):
                    wrapper = copy.deepcopy(good_wrapper)
                    wrapper["verified_runtime_data_aborts"] = [changed_abort]
                    if changed_wrapper:
                        wrapper[changed_wrapper[0]] = changed_wrapper[1]
                    write_capture()
                    with self.assertRaisesRegex(ComparisonError, "runtime-data abort"):
                        _validate_v10_browser_provenance(
                            capture_path, producer_path, browser_report_path, trace_path,
                            recipe_path, recipe_sha, recipe, packet)
            for invalid_rows in (None, {}, [abort, abort]):
                with self.subTest(runtime_abort_list=invalid_rows):
                    wrapper = copy.deepcopy(good_wrapper)
                    wrapper["verified_runtime_data_aborts"] = invalid_rows
                    write_capture()
                    with self.assertRaisesRegex(ComparisonError, "runtime-data abort"):
                        _validate_v10_browser_provenance(
                            capture_path, producer_path, browser_report_path, trace_path,
                            recipe_path, recipe_sha, recipe, packet)

            # Use the actual browser provenance validator in the comparison
            # entry point, while mocking only source-side metadata validation.
            reference_path = root / "unopened.mwro"
            reference_path.write_bytes(b"unopened synthetic source")
            packet["source"]["trace"] = {
                "bytes": reference_path.stat().st_size, "recorded_full_sha256": "b" * 64}
            packet["recipe"] = {
                "path": str(recipe_path), "bytes": len(recipe_raw), "sha256": recipe_sha,
                "version": recipe.version, "frame_count": recipe.frame_count, "seed": recipe.seed}
            wrapper = copy.deepcopy(good_wrapper)
            wrapper["verified_runtime_data_aborts"] = [{**abort, "actualSha256": "d" * 64}]
            write_capture()
            with (mock.patch("whole_session_state_compare._load_expectations",
                             return_value=(packet, "f" * 64)),
                  mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                             return_value=({}, {}, {}, {})),
                  mock.patch("whole_session_state_compare.iter_records") as read_source):
                result = compare_paths(
                    reference_path, recipe_path, trace_path,
                    scope=V10_FIRST_SETUP_TICK0_SCOPE, expectations=root / "packet.json",
                    source_manifest=root / "source-manifest.json",
                    source_report=root / "source-report.json", source_audit=root / "audit.json",
                    browser_capture_report=capture_path, browser_producer_manifest=producer_path,
                    browser_report=browser_report_path)
            self.assertEqual(result["result"], "invalid")
            self.assertIn("runtime-data abort", result["error"])
            read_source.assert_not_called()
            # Actual schema/source failures also precede opening the source.
            for schema, changed_head, message in (
                    (V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA, False, "post-clock60"),
                    ("unknown-producer", False, "schema is unsupported"),
                    (V10_BROWSER_PRODUCER_SCHEMA, True, "head differs")):
                producer = copy.deepcopy(canonical_producer)
                producer["schema"] = schema
                if changed_head:
                    producer["source"]["head"] = "9" * 40
                wrapper = copy.deepcopy(good_wrapper)
                write_producer()
                with (mock.patch("whole_session_state_compare._load_expectations",
                                 return_value=(packet, "f" * 64)),
                      mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                                 return_value=({}, {}, {}, {})),
                      mock.patch("whole_session_state_compare.iter_records") as read_source):
                    result = compare_paths(
                        reference_path, recipe_path, trace_path,
                        scope=V10_FIRST_SETUP_TICK0_SCOPE, expectations=root / "packet.json",
                        source_manifest=root / "source-manifest.json",
                        source_report=root / "source-report.json", source_audit=root / "audit.json",
                        browser_capture_report=capture_path, browser_producer_manifest=producer_path,
                        browser_report=browser_report_path)
                with self.subTest(unopened_schema=schema):
                    self.assertEqual(result["result"], "invalid")
                    self.assertIn(message, result["error"])
                    read_source.assert_not_called()
            producer = copy.deepcopy(canonical_producer)
            wrapper = copy.deepcopy(good_wrapper)
            write_producer()
            write_capture()
            with self.assertRaisesRegex(ComparisonError, "requested/observed cursors"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet, required_cursor=5)

            def assert_bad_snapshots(rows, error):
                wrapper["snapshots"] = rows
                capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
                expected_files["capture_report"]["bytes"] = capture_path.stat().st_size
                expected_files["capture_report"]["sha256"] = hashlib.sha256(
                    capture_path.read_bytes()).hexdigest()
                with self.assertRaisesRegex(ComparisonError, error):
                    _validate_v10_browser_provenance(
                        capture_path, producer_path, browser_report_path, trace_path,
                        recipe_path, recipe_sha, recipe, packet)

            good_rows = wrapper["snapshots"]
            assert_bad_snapshots([
                good_rows[0], good_rows[1],
                {"source_cursor": None, "phase": None, "running": None,
                 "replay_report": None, "replay_downloads": [], "runtime_error": None,
                 "at_ms": 102},
            ], "null cursor after replay observation")
            assert_bad_snapshots([
                {**good_rows[0], "runtime_error": "startup failed"}, *good_rows[1:]],
                "inconsistent unavailable fields")
            assert_bad_snapshots([
                {key: value for key, value in good_rows[0].items()
                 if key != "source_cursor"}, *good_rows[1:]],
                "lacks its cursor or runtime-error field")
            assert_bad_snapshots([
                good_rows[0], {"source_cursor": 2.5, "at_ms": 99,
                               "runtime_error": None}, *good_rows[2:]],
                "expected integer")
            assert_bad_snapshots([
                {**good_rows[0], "at_ms": float("inf")}, *good_rows[1:]],
                "finite ordered observation times")
            wrapper["snapshots"] = good_rows
            capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
            expected_files["capture_report"]["bytes"] = capture_path.stat().st_size
            expected_files["capture_report"]["sha256"] = hashlib.sha256(
                capture_path.read_bytes()).hexdigest()

            valid_failure = wrapper["failure"]
            wrapper["failure"] = ""
            capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
            expected_files["capture_report"]["bytes"] = capture_path.stat().st_size
            expected_files["capture_report"]["sha256"] = hashlib.sha256(
                capture_path.read_bytes()).hexdigest()
            with self.assertRaisesRegex(ComparisonError, "failure text"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet)
            wrapper["failure"] = valid_failure
            capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
            expected_files["capture_report"]["bytes"] = capture_path.stat().st_size
            expected_files["capture_report"]["sha256"] = hashlib.sha256(
                capture_path.read_bytes()).hexdigest()

            packet["browser"]["disc"]["sha256"] = "d" * 64
            with self.assertRaisesRegex(ComparisonError, "disc differs from frozen"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet)
            packet["browser"]["disc"]["sha256"] = wrapper["inputs"]["disc"]["sha256"]
            packet["browser"]["runtime_data"]["sha256"] = "d" * 64
            with self.assertRaisesRegex(ComparisonError, "runtime data differs from frozen"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet)
            packet["browser"]["runtime_data"]["sha256"] = "c" * 64

            wrapper["browser_errors"] = [{"message": "pre-stop failure"}]
            capture_path.write_text(json.dumps(wrapper), encoding="utf-8")
            expected_files["capture_report"]["bytes"] = capture_path.stat().st_size
            expected_files["capture_report"]["sha256"] = hashlib.sha256(
                capture_path.read_bytes()).hexdigest()
            with self.assertRaisesRegex(ComparisonError, "unrelated browser or request failure"):
                _validate_v10_browser_provenance(
                    capture_path, producer_path, browser_report_path, trace_path,
                    recipe_path, recipe_sha, recipe, packet)

    def test_prefix_join_stops_only_at_completed_match_zero_tick(self):
        source = SimpleNamespace(
            match_index=0, pending=None, last_source_tick={0: 0},
            prefix_binding_validated=True, first_source_tick_seq=19,
        )
        comparator = SimpleNamespace(setup_count=1, current_match=0, match_compared=1)
        tick = {"event": "boundary", "seq": 19, "source_tick": 0,
                "payload": {"boundary": "source_tick", "match_index": 0}}
        self.assertTrue(_first_match_tick_join_complete(tick, source, comparator))
        later_match_clock_zero = copy.deepcopy(tick)
        later_match_clock_zero["seq"] = 20
        later_match_clock_zero["payload"]["match_index"] = 1
        self.assertFalse(_first_match_tick_join_complete(
            later_match_clock_zero, source, comparator))
        self.assertFalse(_first_match_tick_join_complete(
            {"event": "end", "seq": 20, "source_tick": 0, "payload": {}},
            source, comparator))
        self.assertFalse(_first_match_tick_join_complete(
            {**tick, "payload": {**tick["payload"], "boundary": "draw_return"}},
            source, comparator))
        source.pending = {"source_tick": 0}
        self.assertFalse(_first_match_tick_join_complete(tick, source, comparator))

    def test_positive_join_stops_only_at_the_frozen_tick_and_browser_cursor(self):
        target = {"match_index": 0, "source_tick": 3, "source_sequence": 50,
                  "pad_consume_sequence": 49, "timeline_frame_index": 5,
                  "browser_cursor": 6, "match_frame": 1}
        source = SimpleNamespace(
            match_index=0, pending=None, last_source_tick={0: 3},
            last_source_tick_seq=50, prefix_binding_validated=True)
        comparator = SimpleNamespace(
            setup_count=1, current_match=0, match_compared=4, frame_index=6)
        row = {"event": "boundary", "seq": 50, "source_tick": 3,
               "payload": {"boundary": "source_tick", "match_index": 0}}
        self.assertTrue(_first_positive_match_join_complete(row, source, comparator, target))
        for changed in (
                {**row, "seq": 51},
                {**row, "source_tick": 2},
                {**row, "payload": {**row["payload"], "match_index": 1}},
                {**row, "payload": {**row["payload"], "boundary": "draw_return"}},
        ):
            self.assertFalse(_first_positive_match_join_complete(
                changed, source, comparator, target))
        comparator.frame_index = 5
        self.assertFalse(_first_positive_match_join_complete(row, source, comparator, target))
        comparator.frame_index = 6
        source.pending = {"source_tick": 3}
        self.assertFalse(_first_positive_match_join_complete(row, source, comparator, target))

    def test_v10_primary_entity_profile_keeps_strict_secondary_and_backlink_rejection(self):
        recipe = Recipe(Path("v10.mwrc"), _v10_recipe_bytes(),
                        scope=V10_FIRST_SETUP_TICK0_SCOPE)
        self.assertEqual(recipe.entity_profile, PRIMARY_STATIC_ENTITY_PROFILE)
        from whole_session_state_compare import comparison_fields
        self.assertIn("fighter_entities", comparison_fields(
            10, scope=V10_FIRST_SETUP_TICK0_SCOPE))
        _, _, state_slices, _ = _source_rows()
        valid_entities = _fighter_entities({"slices": copy.deepcopy(state_slices)},
                                           "v10 primary", 0, {})
        changed_generation = copy.deepcopy(valid_entities)
        changed_generation[0]["generation"] = 1
        self.assertEqual(_browser_entities(changed_generation, "v10 changed generation", 0),
                         changed_generation)
        self.assertEqual(_first_difference(valid_entities, changed_generation)[0],
                         "[0].generation")
        secondary = copy.deepcopy(state_slices)
        next(item for item in secondary if item["name"] == "player_entities").update(
            {"hex": "8068000000000001"})
        with self.assertRaisesRegex(ValueError, "secondary entity"):
            _fighter_entities({"slices": secondary}, "v10 secondary", 0, {})
        detached = copy.deepcopy(state_slices)
        head = next(item for item in detached if item["name"] == "fighter_head")
        head["hex"] = "00000000" + head["hex"][8:]
        with self.assertRaisesRegex(ValueError, "backlink"):
            _fighter_entities({"slices": detached}, "v10 detached", 0, {})

    def test_v10_source_collector_rejects_duplicate_setup_pad_and_wrong_tick_envelopes(self):
        raw_setup = _v10_setup_bytes()[0]
        recipe = SimpleNamespace(
            scope=V10_FIRST_SETUP_TICK0_SCOPE, version=10,
            entity_profile=PRIMARY_STATIC_ENTITY_PROFILE,
            match_setups=[raw_setup, *_v10_setup_bytes()[1:]], setup=raw_setup,
            spans=[{"scene": SCENES["match"]}] * 3,
        )
        state_slices = _state_slices()
        setup_slices = [*state_slices, {
            "name": "match_setup", "address": 0x80600000,
            "size": len(raw_setup), "hex": raw_setup.hex(),
        }]
        setup_row = _boundary("setup", 7, 31, slices=setup_slices)
        setup_row["payload"]["gprs"] = [0] * 32
        setup_row["payload"]["gprs"][3] = 0x80600000

        class Callback:
            setup_count = 0
            current_match = -1
            match_compared = 0

            def on_setup(self, match_index, state, source_seq):
                self.setup_count += 1
                self.current_match = match_index

            def on_frame(self, frame):
                self.match_compared += frame["scene"] == SCENES["match"]

        def collector_for_setup():
            callback = Callback()
            collector = SourceCollector(
                callback, recipe,
                source_audit={
                    "first_setup": {"seq": 7, "source_tick": 31, "match_index": 0},
                    "first_source_tick": {"seq": 9, "source_tick": 0, "match_index": 0},
                    "first_entry_verified_seq": 6,
                },
                source_expectations={"capture_id": "capture-dynamic",
                                     "sequence_id": "sequence-dynamic"},
            )
            collector.scene = "match"
            collector.match_index = 0
            collector.started = True
            collector.prefix_binding_validated = True
            collector.entry_setup_bytes = raw_setup
            return callback, collector

        callback, collector = collector_for_setup()
        collector._setup(setup_row)
        self.assertEqual(callback.setup_count, 1)
        self.assertEqual(collector.first_setup_seq, 7)
        with self.assertRaisesRegex(ComparisonError, "extra or reordered setup"):
            collector._setup({**setup_row, "seq": 8})

        callback, collector = collector_for_setup()
        collector.scene = "sss"
        with self.assertRaisesRegex(ComparisonError, "outside VS"):
            collector._setup(setup_row)

        callback, collector = collector_for_setup()
        collector.pending = {"scene": SCENES["match"], "pads": [],
                             "source_seq": 6, "source_tick": 0}
        with self.assertRaisesRegex(ComparisonError, "setup followed an unjoined PAD"):
            collector._setup(setup_row)

        callback, collector = collector_for_setup()
        collector._setup(setup_row)
        pad = _pad_consume(0, 0x10, 0)
        pad.update({"seq": 8, "source_tick": 0, "draw_ordinal": 8})
        collector._consume(pad)
        with self.assertRaisesRegex(ComparisonError, "multiple PAD consumes"):
            collector._consume(pad)

        tick_row = _boundary("source_tick", 9, 0, slices=state_slices)
        collector._tick(tick_row)
        self.assertEqual(collector.first_source_tick_seq, 9)
        self.assertIsNone(collector.pending)

        callback, collector = collector_for_setup()
        collector._setup(setup_row)
        collector.pending = {"scene": SCENES["match"], "pads": [],
                             "source_seq": 8, "source_tick": 0}
        wrong_envelope = _boundary("source_tick", 9, 1, slices=state_slices)
        with self.assertRaisesRegex(ComparisonError, "bounded identity audit"):
            collector._tick(wrong_envelope)

        callback, collector = collector_for_setup()
        collector._setup(setup_row)
        collector.pending = {"scene": SCENES["match"], "pads": [],
                             "source_seq": 8, "source_tick": 0}
        wrong_state = copy.deepcopy(state_slices)
        next(item for item in wrong_state if item["name"] == "scene_frame")["hex"] = "00000001"
        wrong_scene_frame = _boundary("source_tick", 9, 0, slices=wrong_state)
        with self.assertRaisesRegex(ComparisonError, "scene frame"):
            collector._tick(wrong_scene_frame)

    def test_first_positive_scope_reuses_ordered_pad_and_contiguous_tick_checks(self):
        raw_setup = _v10_setup_bytes()[0]
        recipe = SimpleNamespace(
            scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE, version=10,
            entity_profile=PRIMARY_STATIC_ENTITY_PROFILE,
            match_setups=[raw_setup, *_v10_setup_bytes()[1:]], setup=raw_setup,
            spans=[{"scene": SCENES["match"]}] * 3,
        )
        state_slices = _state_slices(tick=0, match_frame=0)
        setup_slices = [*state_slices, {
            "name": "match_setup", "address": 0x80600000,
            "size": len(raw_setup), "hex": raw_setup.hex(),
        }]
        setup_row = _boundary("setup", 7, 31, slices=setup_slices)
        setup_row["payload"]["gprs"] = [0] * 32
        setup_row["payload"]["gprs"][3] = 0x80600000

        class Callback:
            def __init__(self):
                self.setup_count = 0
                self.current_match = -1
                self.match_compared = 0
                self.frames = []

            def on_setup(self, match_index, state, source_seq):
                self.setup_count += 1
                self.current_match = match_index

            def on_frame(self, frame):
                self.match_compared += frame["scene"] == SCENES["match"]
                self.frames.append(frame)

        callback = Callback()
        collector = SourceCollector(
            callback, recipe,
            source_audit={
                "first_setup": {"seq": 7, "source_tick": 31, "match_index": 0},
                "first_source_tick": {"seq": 9, "source_tick": 0, "match_index": 0},
                "first_entry_verified_seq": 6,
            },
            source_expectations={"capture_id": "dynamic-capture",
                                 "sequence_id": "dynamic-sequence"})
        collector.scene = "match"
        collector.match_index = 0
        collector.started = True
        collector.prefix_binding_validated = True
        collector.entry_setup_bytes = raw_setup
        collector._setup(setup_row)

        pad0 = _pad_consume(0, 0x10, 0)
        pad0.update({"seq": 8, "source_tick": 0, "draw_ordinal": 8})
        collector._consume(pad0)
        collector._tick(_boundary("source_tick", 9, 0, slices=state_slices))
        self.assertEqual(collector.first_source_tick_seq, 9)
        self.assertEqual(collector.last_source_tick_seq, 9)

        pad1 = _pad_consume(0, 0x20, 0)
        pad1.update({"seq": 10, "source_tick": 1, "draw_ordinal": 10})
        collector._consume(pad1)
        with self.assertRaisesRegex(ComparisonError, "multiple PAD consumes"):
            collector._consume({**pad1, "seq": 11})

        skipped_state = _state_slices(tick=2, match_frame=0)
        with self.assertRaisesRegex(ComparisonError, "not contiguous"):
            collector._tick(_boundary("source_tick", 11, 2, slices=skipped_state))

        state1 = _state_slices(tick=1, match_frame=0)
        collector._tick(_boundary("source_tick", 11, 1, slices=state1))
        self.assertEqual(collector.first_source_tick_seq, 9)
        self.assertEqual(collector.last_source_tick_seq, 11)
        self.assertEqual(callback.match_compared, 2)

        collector.pending = None
        with self.assertRaisesRegex(ComparisonError, "preceding VS PAD consume"):
            collector._tick(_boundary("source_tick", 12, 2, slices=state1))

        collector.pending = {"scene": SCENES["match"], "pads": [],
                             "source_seq": 11, "source_tick": 1}
        with self.assertRaisesRegex(ComparisonError, "PAD consume"):
            collector._tick(_boundary("source_tick", 12, 2,
                                      slices=_state_slices(tick=2, match_frame=0)))

    def test_first_positive_scope_compares_real_collector_path_through_clock_one(self):
        candidate = _candidate()
        boundary = lambda name, match=0: next(  # noqa: E731
            row for row in candidate
            if row.get("event") == "boundary" and
            row.get("payload", {}).get("boundary") == name and
            row.get("payload", {}).get("match_index") == match)
        rows = [copy.deepcopy(candidate[0]), copy.deepcopy(candidate[1]),
                copy.deepcopy(boundary("css_enter")),
                copy.deepcopy(candidate[3]),
                copy.deepcopy(boundary("css_exit")),
                copy.deepcopy(boundary("sss_enter")),
                copy.deepcopy(candidate[6]),
                copy.deepcopy(boundary("sss_exit")),
                copy.deepcopy(boundary("entry"))]

        setups = _v10_setup_bytes()
        raw_setup = setups[0]
        entry = rows[-1]
        entry_setup = next(item for item in entry["payload"]["slices"]
                           if item["name"] == "match_setup")
        entry_setup.update({"size": len(raw_setup), "hex": raw_setup.hex()})

        setup = copy.deepcopy(boundary("setup"))
        setup["source_tick"] = 0
        setup["payload"]["gprs"] = [0] * 32
        setup["payload"]["gprs"][3] = 0x80600000
        setup["payload"]["slices"] = _state_slices(tick=0, match_frame=0) + [{
            "name": "match_setup", "address": 0x80600000,
            "size": len(raw_setup), "hex": raw_setup.hex(),
        }]
        rows.append(setup)

        for tick, value in enumerate((0x30, 0x40, 0x50)):
            pad = _pad_consume(0, value, tick)
            rows.append(pad)
            rows.append(_boundary(
                "source_tick", 0, tick,
                slices=_state_slices(tick=tick, match_frame=0 if tick < 2 else 1)))

        for seq, row in enumerate(rows):
            row["seq"] = seq
            row["draw_ordinal"] = seq
        rows[0]["payload"]["fighter_entity_profile"] = "v10-live-static-player-pair"
        css_row = rows[2]
        first_css = _first_css_context([css_row])
        source_identity = _source_prefix_identity(rows[0]["payload"], rows[1]["payload"])

        menu_pads = [_consumed_ports(rows[3], 0), _consumed_ports(rows[6], 0)]
        match_pads = [_consumed_ports(rows[index], 0) for index in (10, 12, 14)]
        recipe_raw = _v10_positive_recipe_bytes(first_css, [*menu_pads, *match_pads])
        recipe = Recipe(Path("synthetic-positive.mwrc"), recipe_raw,
                        scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
        self.assertEqual(_first_match_timeline_index(recipe), 2)

        source_manifest = {"input": {
            "capture": source_identity,
            "first_css": first_css,
            "profile_context": first_css["profile_context"],
            "match_setups": [
                {"match_index": index, "start_melee_hex": raw.hex(),
                 "declared_setup": recipe.declared_match_setups[index]}
                for index, raw in enumerate(recipe.match_setups)],
            "setup_hex": recipe.setup.hex(),
            "declared_setup": recipe.declared_match_setups[0],
        }}
        source_audit = {
            "first_entry_verified_seq": 8,
            "first_setup": {"seq": 9, "source_tick": 0, "match_index": 0},
            "first_source_tick": {"seq": 11, "source_tick": 0, "match_index": 0},
        }
        source_expectations = {
            "capture_id": source_identity["capture_id"],
            "sequence_id": source_identity["sequence_id"],
        }
        target = {
            "match_index": 0, "source_tick": 2,
            "source_sequence": 15, "pad_consume_sequence": 14,
            "timeline_frame_index": 4, "browser_cursor": 5, "match_frame": 1,
        }

        def source_state(payload, context, previous):
            state = _state_from_payload(payload, context)
            state.update(_snapshot_values(payload, context))
            state["fighter_entities"] = _fighter_entities(
                payload, context, 0, previous)
            return state

        setup_state = source_state(setup["payload"], "browser setup", {})
        setup_state["declared_setup"] = recipe.declared_match_setups[0]
        browser_rows = [{
            "record": "header", "schema": "melee-web-port-session-diagnostic",
            "version": 1, "frames_requested": recipe.frame_count,
            "comparison": "not_run", "cpu_observations": "not_captured",
            "draw_state": "not_captured",
        }]
        for index in (0, 1):
            browser_rows.append({
                "record": "session_frame", "scene": recipe.frames[index]["scene"],
                "index": index, "supplied_inputs": recipe.frames[index]["pads"],
                "rng": recipe.seed, "pad_state_hex": recipe.initial_pad.hex(),
            })
        browser_rows.append({
            "record": "session_match_enter_complete",
            "rng": setup_state["rng"], "match_frame": setup_state["match_frame"],
            "pad_state_hex": setup_state["pad_state_hex"],
            "fighters": setup_state["fighters"],
            "fighter_entities": setup_state["fighter_entities"],
            "declared_setup": setup_state["declared_setup"],
        })
        browser_entity_history = {}
        for tick, index in enumerate((2, 3, 4)):
            tick_slices = _state_slices(tick=tick, match_frame=0 if tick < 2 else 1)
            state = source_state({"slices": tick_slices}, f"browser tick {tick}",
                                 browser_entity_history)
            browser_rows.append({
                "record": "session_frame", "scene": SCENES["match"],
                "index": index, "supplied_inputs": recipe.frames[index]["pads"],
                "rng": state["rng"], "match_frame": state["match_frame"],
                "pad_state_hex": state["pad_state_hex"],
                "fighters": state["fighters"],
                "fighter_entities": state["fighter_entities"],
            })
        browser_rows.append({
            "record": "session_frame", "scene": recipe.frames[5]["scene"],
            "index": 5, "supplied_inputs": recipe.frames[5]["pads"],
            "rng": recipe.seed, "pad_state_hex": recipe.initial_pad.hex(),
        })

        with tempfile.TemporaryDirectory() as directory:
            browser_path = Path(directory) / "positive-prefix.jsonl"
            browser_path.write_text(
                "\n".join(json.dumps(row, separators=(",", ":"))
                          for row in browser_rows) + "\n", encoding="utf-8")
            browser = BrowserReader(browser_path)
            try:
                comparator = Comparator(recipe, browser, positive_boundary=target)
                collector = SourceCollector(
                    comparator, recipe, source_manifest, source_audit,
                    source_expectations)
                joined = False
                for row in rows:
                    collector.consume(row)
                    joined = _first_positive_match_join_complete(
                        row, collector, comparator, target)
                    if joined:
                        break
                self.assertTrue(joined)
                self.assertEqual(collector.record_count, 16)
                self.assertEqual(collector.first_setup_seq, 9)
                self.assertEqual(collector.first_source_tick_seq, 11)
                self.assertEqual(collector.last_source_tick_seq, 15)
                self.assertIsNone(collector.pending)
                self.assertEqual(comparator.setup_count, 1)
                self.assertEqual(comparator.compared, target["browser_cursor"])
                self.assertEqual(comparator.nonmatch_compared, 2)
                self.assertEqual(comparator.match_compared, 3)
                self.assertIsNone(comparator.first_difference)
                self.assertEqual(browser.line, 7)
            finally:
                browser.close()

    def test_clock60_scope_rejoins_positive_and_terminal_prefixes_before_incomplete_result(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(Path(directory))
            packet_path = fixture["packet_path"]
            selected = fixture["selected"]
            packet = fixture["packet"]
            recipe = fixture["recipe"]

            loaded, _ = _load_expectations(
                packet_path,
                {name: selected[name] for name in (
                    "reference", "source_manifest", "source_report", "source_audit", "recipe",
                    "browser_capture_report", "browser_producer_manifest", "browser_report",
                    "port_trace", "positive_boundary_audit", "clock60_boundary_audit")},
                scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
            positive_audit, positive_sha = _validate_first_positive_audit(
                selected["positive_boundary_audit"], loaded, recipe)
            clock_audit, clock_sha = _validate_clock60_audit(
                selected["clock60_boundary_audit"], loaded, recipe, positive_sha)
            self.assertEqual(positive_audit["observed"]["source_prefix"]["records_read"], 260)
            self.assertEqual(clock_audit["observed"]["source_prefix"]["records_read"], 378)
            self.assertEqual(clock_sha, packet["source"]["clock60_boundary_audit"]["sha256"])

            result, yielded, observed_limits = _run_clock60_comparison(
                fixture)

            self.assertEqual(observed_limits, {
                "max_bytes": V10_PREFIX_BYTE_CAP,
                "max_records": V10_MATCH_CLOCK_RECORD_CAP,
            })
            self.assertEqual(yielded[-1], fixture["clock_target"]["source_sequence"])
            self.assertEqual(len(yielded), 378)
            self.assertEqual(result["boundary_result"], "equivalent")
            self.assertEqual(result["result"], "incomplete")
            self.assertFalse(result["complete"])
            self.assertFalse(result["whole_session_equivalent"])
            self.assertEqual(result["match_state_frames_compared"], 184)
            self.assertEqual(result["timeline_frames_consumed"], 186)
            self.assertEqual(result["source_prefix"]["last_source_sequence"], 377)
            self.assertEqual(result["source_prefix"]["sha256"],
                             hashlib.sha256(fixture["terminal_bytes"]).hexdigest())
            self.assertTrue(result["first_positive_prefix_rejoin"]["rejoined_before_continuing"])
            self.assertEqual(result["first_positive_prefix_rejoin"]["source_prefix_sha256"],
                             hashlib.sha256(fixture["positive_bytes"]).hexdigest())
            self.assertEqual(result["clock60_match_frame_boundary"]["match_frame"], 60)

    def test_match_clock_boundary_rejoins_all_checkpoints_and_compares_contiguous_ticks(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            selected = fixture["selected"]
            loaded, _ = _load_expectations(
                fixture["packet_path"],
                {name: selected[name] for name in (
                    "reference", "source_manifest", "source_report", "source_audit", "recipe",
                    "browser_capture_report", "browser_producer_manifest", "browser_report",
                    "port_trace", "positive_boundary_audit", "clock60_boundary_audit",
                    "match_clock_boundary_audit")},
                scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
            terminal_audit, terminal_sha = _validate_match_clock_boundary_audit(
                selected["match_clock_boundary_audit"], loaded, fixture["recipe"])
            self.assertEqual(terminal_sha,
                             fixture["packet"]["source"]["match_clock_boundary_audit"]["sha256"])
            self.assertTrue(terminal_audit["observed"]["first_positive_rejoined"])
            self.assertTrue(terminal_audit["observed"]["clock60_rejoined"])

            result, yielded, observed_limits = _run_clock60_comparison(fixture)

            self.assertEqual(observed_limits, {
                "max_bytes": V10_PREFIX_BYTE_CAP,
                "max_records": V10_MATCH_CLOCK_RECORD_CAP,
            })
            self.assertEqual(yielded[-1], fixture["match_clock_target"]["source_sequence"])
            self.assertEqual(len(yielded), fixture["match_clock_target"]["source_sequence"] + 1)
            self.assertEqual(result["boundary_result"], "equivalent")
            self.assertEqual(result["result"], "incomplete")
            self.assertFalse(result["complete"])
            self.assertFalse(result["whole_session_equivalent"])
            self.assertEqual(result["match_state_frames_compared"], 424)
            self.assertEqual(result["timeline_frames_consumed"], 426)
            self.assertEqual(result["source_prefix"]["last_source_sequence"],
                             fixture["match_clock_target"]["source_sequence"])
            self.assertEqual(result["source_prefix"]["sha256"],
                             hashlib.sha256(fixture["terminal_bytes"]).hexdigest())
            self.assertTrue(result["first_positive_prefix_rejoin"]["rejoined_before_continuing"])
            self.assertTrue(result["clock60_prefix_rejoin"]["rejoined_before_continuing"])
            self.assertEqual(result["match_clock_boundary"]["match_frame"], 300)
            self.assertEqual(result["match_clock_boundary"]["source_tick"], 423)

    def test_match_clock_boundary_hash_mismatches_stop_at_each_checkpoint(self):
        cases = (
            ("positive", 20, 259, 260, "first-positive audit before continuation"),
            ("clock60", 280, 377, 378, "clock-60 audit before continuation"),
            ("terminal", 857, 857, 858, "terminal match-clock audit"),
        )
        for checkpoint, record_index, sequence, record_count, message in cases:
            with self.subTest(checkpoint=checkpoint), tempfile.TemporaryDirectory() as directory:
                fixture = _clock60_comparison_fixture(
                    Path(directory), terminal_match_frame=300)
                original = fixture["raw_records"][record_index]
                corrupted = bytes([original[0] ^ 1]) + original[1:]
                result, yielded, _ = _run_clock60_comparison(
                    fixture, raw_overrides={record_index: corrupted})

                self.assertEqual(result["result"], "invalid")
                self.assertIsNone(result["boundary_result"])
                self.assertIn(message, result["error"])
                self.assertEqual(result["last_source_sequence"], sequence)
                self.assertEqual(result["source_prefix"]["records_read"], record_count)
                self.assertEqual(len(yielded), record_count)
                self.assertEqual(yielded[-1], sequence)

    def test_match_clock_audit_rejects_wrong_terminal_tuple(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            audit_path = fixture["match_clock_path"]
            audit = json.loads(audit_path.read_text(encoding="utf-8"))
            audit["observed"]["target_clock_ge300_observed"]["source_tick_seq"] += 1
            audit_path.write_text(json.dumps(audit), encoding="utf-8")
            packet = fixture["packet"]
            packet["source"]["match_clock_boundary_audit"].update({
                "bytes": audit_path.stat().st_size,
                "sha256": hashlib.sha256(audit_path.read_bytes()).hexdigest(),
            })
            fixture["packet_path"].write_text(json.dumps(packet), encoding="utf-8")
            selected = fixture["selected"]
            loaded, _ = _load_expectations(
                fixture["packet_path"],
                {name: selected[name] for name in (
                    "reference", "source_manifest", "source_report", "source_audit", "recipe",
                    "browser_capture_report", "browser_producer_manifest", "browser_report",
                    "port_trace", "positive_boundary_audit", "clock60_boundary_audit",
                    "match_clock_boundary_audit")},
                scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
            with self.assertRaisesRegex(ComparisonError, "terminal match-clock differs"):
                _validate_match_clock_boundary_audit(
                    selected["match_clock_boundary_audit"], loaded, fixture["recipe"])

    def test_match_clock_source_lineage_rejects_regression_jump_early_target_and_bad_order(self):
        cases = (
            ("regression", 200, "clock", 75, "regressed, jumped"),
            ("jump", 200, "clock", 78, "regressed, jumped"),
            ("early-threshold", 422, "clock", 300, "reached a frozen match-clock 300 checkpoint early"),
            ("wrong-match", 200, "match_index", 1, "left match 0"),
            ("wrong-timeline", 200, "timeline", 1, "not contiguous with its recipe timeline"),
            ("wrong-terminal-tuple", 423, "sequence", 1, "boundary differs at source_sequence"),
        )
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            for name, bad_tick, field, replacement, message in cases:
                with self.subTest(case=name):
                    comparator = Comparator(
                        fixture["recipe"],
                        SimpleNamespace(header={"frames_requested": fixture["recipe"].frame_count}),
                        positive_boundary=fixture["packet"]["source"]["first_positive_boundary"],
                        clock60_boundary=fixture["clock_target"],
                        match_clock_boundary=fixture["match_clock_target"])
                    for tick in range(bad_tick + 1):
                        match_frame = 0 if tick < 124 else tick - 123
                        sequence = 11 + 2 * tick
                        frame = {
                            "match_index": 0, "source_tick": tick,
                            "source_tick_seq": sequence, "source_seq": sequence - 1,
                            "state": {"match_frame": match_frame},
                        }
                        index = 2 + tick
                        if tick == bad_tick:
                            if field == "clock":
                                frame["state"]["match_frame"] = replacement
                            elif field == "match_index":
                                frame[field] = replacement
                            elif field == "timeline":
                                index += replacement
                            elif field == "sequence":
                                frame["source_tick_seq"] += replacement
                        if tick == bad_tick:
                            with self.assertRaisesRegex(ComparisonError, message):
                                comparator._validate_match_clock_boundary_source_frame(frame, index)
                        else:
                            comparator._validate_match_clock_boundary_source_frame(frame, index)

    def test_clock_boundary_targets_are_rejected_by_older_comparator_scopes(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            browser = SimpleNamespace(header={"frames_requested": fixture["recipe"].frame_count})
            positive = Recipe(Path("positive.mwrc"), fixture["recipe"].raw,
                              scope=V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE)
            with self.assertRaisesRegex(ComparisonError, "clock-boundary targets"):
                Comparator(positive, browser, positive_boundary={},
                           match_clock_boundary=fixture["match_clock_target"])

            clock60 = Recipe(Path("clock60.mwrc"), fixture["recipe"].raw,
                             scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
            with self.assertRaisesRegex(ComparisonError, "match-clock target is only valid"):
                Comparator(clock60, browser, positive_boundary={}, clock60_boundary={},
                           match_clock_boundary=fixture["match_clock_target"])

    def test_match_clock_provenance_failure_does_not_open_source_records(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            terminal_path = fixture["match_clock_path"]
            terminal = json.loads(terminal_path.read_text(encoding="utf-8"))
            terminal["source"]["capture_id"] = "different-capture"
            terminal_path.write_text(json.dumps(terminal), encoding="utf-8")
            fixture["packet"]["source"]["match_clock_boundary_audit"].update({
                "bytes": terminal_path.stat().st_size,
                "sha256": hashlib.sha256(terminal_path.read_bytes()).hexdigest(),
            })
            fixture["packet_path"].write_text(json.dumps(fixture["packet"]), encoding="utf-8")

            result, yielded, _ = _run_clock60_comparison(fixture)

            self.assertEqual(result["result"], "invalid")
            self.assertIsNone(result["boundary_result"])
            self.assertIn("does not bind the frozen source trace identity", result["error"])
            self.assertEqual(yielded, [])

    def test_match_clock_stale_browser_provenance_does_not_open_source_records(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            result, yielded, _ = _run_clock60_comparison(
                fixture, browser_provenance_error=ComparisonError("stale browser capture identity"))
            self.assertEqual(result["result"], "invalid")
            self.assertIsNone(result["boundary_result"])
            self.assertIn("stale browser capture identity", result["error"])
            self.assertEqual(yielded, [])

    def test_match_clock_source_collector_rejects_a_misjoined_pad_tick(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            pad = next(row for row in fixture["rows"]
                       if row.get("event") == "boundary" and
                       row.get("payload", {}).get("boundary") == "pad_consume" and
                       row.get("source_tick") == 190)
            pad["source_tick"] = 189

            result, yielded, _ = _run_clock60_comparison(fixture)

            expected_sequence = 11 + 2 * 190
            self.assertEqual(result["result"], "invalid")
            self.assertIsNone(result["boundary_result"])
            self.assertIn("disagrees with its PAD consume", result["error"])
            self.assertEqual(result["last_source_sequence"], expected_sequence)
            self.assertEqual(yielded[-1], expected_sequence)

    def test_match_clock_export_validates_tail_shape_and_exact_eof(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            trace_path = fixture["selected"]["port_trace"]
            recipe = fixture["recipe"]
            exported_cursor = fixture["match_clock_target"]["browser_cursor"] + 1
            rows = [json.loads(line) for line in trace_path.read_text().splitlines()]
            tail = next(row for row in rows
                        if row.get("record") == "session_frame" and
                        row.get("index") == exported_cursor - 1)
            self.assertEqual(_validate_v10_browser_export(
                trace_path, recipe, exported_cursor, fixture["packet"]), len(rows))

            tail["rng"] = True
            trace_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                     for row in rows) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "expected integer"):
                _validate_v10_browser_export(
                    trace_path, recipe, exported_cursor, fixture["packet"])

            tail["rng"] = recipe.seed
            rows.append(copy.deepcopy(tail))
            trace_path.write_text("\n".join(json.dumps(row, separators=(",", ":"))
                                     for row in rows) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "records after the exported prefix"):
                _validate_v10_browser_export(
                    trace_path, recipe, exported_cursor, fixture["packet"])

    def test_match_clock_first_state_difference_stops_before_terminal(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(
                Path(directory), terminal_match_frame=300)
            port_path = fixture["selected"]["port_trace"]
            rows = [json.loads(line) for line in port_path.read_text().splitlines()]
            mismatch_index = 2 + 190
            row = next(item for item in rows if item.get("index") == mismatch_index)
            row["rng"] += 1
            port_path.write_text("\n".join(json.dumps(item, separators=(",", ":"))
                                     for item in rows) + "\n", encoding="utf-8")

            result, yielded, _ = _run_clock60_comparison(fixture)

            source_sequence = 11 + 2 * 190
            self.assertEqual(result["result"], "incomplete")
            self.assertEqual(result["boundary_result"], "divergent")
            self.assertIn("exact state differs at rng", result["error"])
            self.assertEqual(result["last_source_sequence"], source_sequence)
            self.assertEqual(len(yielded), source_sequence + 1)
            self.assertEqual(yielded[-1], source_sequence)

    def test_match_clock_compares_every_declared_field_after_clock60(self):
        mutations = (
            ("rng", lambda row: row.__setitem__("rng", row["rng"] + 1), "at rng"),
            ("match_frame", lambda row: row.__setitem__("match_frame", row["match_frame"] + 1),
             "at match_frame"),
            ("pad_state_hex", lambda row: row.__setitem__(
                "pad_state_hex", "ff" + row["pad_state_hex"][2:]), "at pad_state_hex"),
            ("fighters", lambda row: row["fighters"][0].__setitem__(
                "animation", row["fighters"][0]["animation"] ^ 1), "at fighters[0].animation"),
            ("fighter_entities", lambda row: row["fighter_entities"][0].__setitem__(
                "generation", row["fighter_entities"][0]["generation"] + 1),
             "at fighter_entities[0].generation"),
        )
        for name, mutate, difference in mutations:
            with self.subTest(field=name), tempfile.TemporaryDirectory() as directory:
                fixture = _clock60_comparison_fixture(
                    Path(directory), terminal_match_frame=300)
                port_path = fixture["selected"]["port_trace"]
                rows = [json.loads(line) for line in port_path.read_text().splitlines()]
                mismatch_index = 2 + 190
                row = next(item for item in rows if item.get("index") == mismatch_index)
                mutate(row)
                port_path.write_text("\n".join(json.dumps(item, separators=(",", ":"))
                                         for item in rows) + "\n", encoding="utf-8")

                result, yielded, _ = _run_clock60_comparison(fixture)

                source_sequence = 11 + 2 * 190
                self.assertEqual(result["result"], "incomplete")
                self.assertEqual(result["boundary_result"], "divergent")
                self.assertIn(f"exact state differs {difference}", result["error"])
                self.assertEqual(result["last_source_sequence"], source_sequence)
                self.assertEqual(len(yielded), source_sequence + 1)

    def test_clock60_prefix_hash_mismatch_stops_at_the_boundary_that_failed(self):
        for boundary, record_index, expected_sequence, expected_records, error_fragment in (
                ("first-positive", 20, 259, 260, "first-positive audit before continuation"),
                ("clock60", 280, 377, 378, "clock-60 audit at the terminal boundary")):
            with self.subTest(boundary=boundary), tempfile.TemporaryDirectory() as directory:
                fixture = _clock60_comparison_fixture(Path(directory))
                original = fixture["raw_records"][record_index]
                corrupted = bytes([original[0] ^ 1]) + original[1:]
                result, yielded, _ = _run_clock60_comparison(
                    fixture, raw_overrides={record_index: corrupted})

                self.assertEqual(result["result"], "invalid")
                self.assertIsNone(result["boundary_result"])
                self.assertIn(error_fragment, result["error"])
                self.assertEqual(result["last_source_sequence"], expected_sequence)
                self.assertEqual(result["source_prefix"]["records_read"], expected_records)
                self.assertEqual(len(yielded), expected_records)
                self.assertEqual(yielded[-1], expected_sequence)

    def test_clock60_audit_rejects_wrong_prior_audit_and_early_threshold(self):
        for mutation, message in (
                ("prior-audit", "prior audit identities"),
                ("terminal-clock", "differs from frozen target")):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as directory:
                fixture = _clock60_comparison_fixture(Path(directory))
                packet_path = fixture["packet_path"]
                packet = fixture["packet"]
                audit_path = fixture["clock_path"]
                audit = json.loads(audit_path.read_text(encoding="utf-8"))
                if mutation == "prior-audit":
                    audit["source"]["prior_first_positive_audit_sha256"] = "0" * 64
                else:
                    audit["observed"]["target_clock_at_least_60"]["match_frame"] = 61
                audit_path.write_text(json.dumps(audit), encoding="utf-8")
                packet["source"]["clock60_boundary_audit"].update({
                    "bytes": audit_path.stat().st_size,
                    "sha256": hashlib.sha256(audit_path.read_bytes()).hexdigest(),
                })
                packet_path.write_text(json.dumps(packet), encoding="utf-8")
                selected = fixture["selected"]
                loaded, _ = _load_expectations(
                    packet_path,
                    {name: selected[name] for name in (
                        "reference", "source_manifest", "source_report", "source_audit",
                        "recipe", "browser_capture_report", "browser_producer_manifest",
                        "browser_report", "port_trace", "positive_boundary_audit",
                        "clock60_boundary_audit")},
                    scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE)
                _, positive_sha = _validate_first_positive_audit(
                    selected["positive_boundary_audit"], loaded, fixture["recipe"])
                with self.assertRaisesRegex(ComparisonError, message):
                    _validate_clock60_audit(
                        selected["clock60_boundary_audit"], loaded, fixture["recipe"],
                        positive_sha)

    def test_clock60_scope_cannot_pass_when_bounded_reader_stops_before_target(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(Path(directory))
            selected = fixture["selected"]
            packet = fixture["packet"]
            source_stat = _file_stat_identity(selected["reference"])
            source_identity = {
                "trace_bytes": source_stat["bytes"],
                "recorded_full_trace_sha256": packet["source"]["trace"]["recorded_full_sha256"],
                "full_trace_rehashed": False,
                "manifest_sha256": packet["source"]["manifest"]["sha256"],
                "source_report_sha256": packet["source"]["report"]["sha256"],
                "audit_sha256": packet["source"]["audit"]["sha256"],
                "audit_records_decoded": 4116, "audit_bytes_read": 5364736,
            }
            browser_identity = {
                "required_cursor": fixture["clock_target"]["browser_cursor"],
                "target_cursor": fixture["clock_target"]["browser_cursor"],
                "exported_cursor": fixture["clock_target"]["browser_cursor"],
                "capture_report_sha256": "c" * 64,
                "producer_manifest_sha256": "d" * 64,
                "browser_report_sha256": "e" * 64,
                "port_trace_sha256": "f" * 64,
            }
            observed_limits = {}

            def capped_source(path, *, max_bytes, max_records, stats):
                observed_limits.update(max_bytes=max_bytes, max_records=max_records)
                raise ComparisonError("bounded source record cap reached before target")
                yield  # pragma: no cover

            with (mock.patch("whole_session_state_compare._validate_v10_source_provenance",
                             return_value=(fixture["source_manifest"], {},
                                           fixture["source_audit"], source_identity)),
                  mock.patch("whole_session_state_compare._validate_v10_browser_provenance",
                             return_value=({}, {}, {}, browser_identity)),
                  mock.patch("whole_session_state_compare.iter_records",
                             side_effect=capped_source)):
                result = compare_paths(
                    selected["reference"], selected["recipe"], selected["port_trace"],
                    scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                    expectations=fixture["packet_path"],
                    source_manifest=selected["source_manifest"],
                    source_report=selected["source_report"],
                    source_audit=selected["source_audit"],
                    browser_capture_report=selected["browser_capture_report"],
                    browser_producer_manifest=selected["browser_producer_manifest"],
                    browser_report=selected["browser_report"],
                    positive_boundary_audit=selected["positive_boundary_audit"],
                    clock60_boundary_audit=selected["clock60_boundary_audit"],
                )
            self.assertEqual(observed_limits, {
                "max_bytes": V10_PREFIX_BYTE_CAP,
                "max_records": V10_MATCH_CLOCK_RECORD_CAP,
            })
            self.assertEqual(result["result"], "invalid")
            self.assertIsNone(result["boundary_result"])
            self.assertFalse(result["complete"])
            self.assertFalse(result["whole_session_equivalent"])

    def test_clock60_source_boundary_rejects_early_clock_jump_and_wrong_terminal_tuple(self):
        spans = [
            {"scene": SCENES["css"], "first_frame": 0, "last_frame": 0},
            {"scene": SCENES["sss"], "first_frame": 1, "last_frame": 1},
            {"scene": SCENES["match"], "first_frame": 2, "last_frame": 185},
            {"scene": SCENES["results"], "first_frame": 186, "last_frame": 186},
            {"scene": SCENES["css"], "first_frame": 187, "last_frame": 187},
            {"scene": SCENES["sss"], "first_frame": 188, "last_frame": 188},
            {"scene": SCENES["match"], "first_frame": 189, "last_frame": 189},
            {"scene": SCENES["results"], "first_frame": 190, "last_frame": 190},
            {"scene": SCENES["css"], "first_frame": 191, "last_frame": 191},
            {"scene": SCENES["sss"], "first_frame": 192, "last_frame": 192},
            {"scene": SCENES["match"], "first_frame": 193, "last_frame": 193},
            {"scene": SCENES["results"], "first_frame": 194, "last_frame": 194},
        ]
        recipe = SimpleNamespace(
            scope=V10_FIRST_MATCH_CLOCK_GE60_SCOPE, version=10, frame_count=195,
            entity_profile=PRIMARY_STATIC_ENTITY_PROFILE, spans=spans,
            frames=[{"scene": span["scene"], "pads": ["00" * 11] * 4}
                    for span in spans
                    for _ in range(span["last_frame"] - span["first_frame"] + 1)],
            seed=0,
        )
        positive = {
            "match_index": 0, "source_tick": 124, "source_sequence": 259,
            "pad_consume_sequence": 258, "timeline_frame_index": 126,
            "browser_cursor": 127, "match_frame": 1,
        }
        terminal = {
            "match_index": 0, "source_tick": 183, "source_sequence": 377,
            "pad_consume_sequence": 376, "timeline_frame_index": 185,
            "browser_cursor": 186, "match_frame": 60,
        }
        class FakeBrowser:
            header = {"frames_requested": 195}
        comparator = Comparator(recipe, FakeBrowser(), positive_boundary=positive,
                                clock60_boundary=terminal)

        def frame(tick, clock, seq, pad_seq):
            return {
                "scene": SCENES["match"], "pads": ["00" * 11] * 4,
                "state": {"match_frame": clock}, "match_index": 0,
                "source_tick": tick, "source_tick_seq": seq, "source_seq": pad_seq,
            }

        for tick in range(124):
            seq = 11 + 2 * tick
            comparator._validate_clock60_source_frame(
                frame(tick, 0, seq, seq - 1), 2 + tick)
        comparator._validate_clock60_source_frame(frame(124, 1, 259, 258), 126)
        with self.assertRaisesRegex(ComparisonError, "regressed, jumped"):
            comparator._validate_clock60_source_frame(frame(125, 3, 261, 260), 127)

        comparator = Comparator(recipe, FakeBrowser(), positive_boundary=positive,
                                clock60_boundary=terminal)
        for tick in range(183):
            seq = 11 + 2 * tick
            match_clock = 0 if tick < 124 else tick - 123
            comparator._validate_clock60_source_frame(
                frame(tick, match_clock, seq, seq - 1), 2 + tick)
        wrong_terminal = frame(183, 60, 378, 376)
        with self.assertRaisesRegex(ComparisonError, "boundary differs at source_sequence"):
            comparator._validate_clock60_source_frame(wrong_terminal, 185)

    def test_clock60_collector_reuses_strict_pad_to_tick_join(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(Path(directory) / "missing-pad")
            packet = fixture["packet"]
            browser = BrowserReader(fixture["selected"]["port_trace"])
            try:
                comparator = Comparator(
                    fixture["recipe"], browser,
                    positive_boundary=packet["source"]["first_positive_boundary"],
                    clock60_boundary=fixture["clock_target"])
                collector = SourceCollector(
                    comparator, fixture["recipe"], fixture["source_manifest"],
                    fixture["source_audit"], packet["source"])
                for row in fixture["rows"][:10]:
                    collector.consume(row)
                missing_pad_replacement = copy.deepcopy(fixture["rows"][11])
                missing_pad_replacement["seq"] = 10
                missing_pad_replacement["draw_ordinal"] = 10
                with self.assertRaisesRegex(ComparisonError, "preceding VS PAD consume"):
                    collector.consume(missing_pad_replacement)
            finally:
                browser.close()

            wrong_pad = _clock60_comparison_fixture(Path(directory) / "wrong-pad")
            browser = BrowserReader(wrong_pad["selected"]["port_trace"])
            try:
                comparator = Comparator(
                    wrong_pad["recipe"], browser,
                    positive_boundary=wrong_pad["packet"]["source"]["first_positive_boundary"],
                    clock60_boundary=wrong_pad["clock_target"])
                collector = SourceCollector(
                    comparator, wrong_pad["recipe"], wrong_pad["source_manifest"],
                    wrong_pad["source_audit"], wrong_pad["packet"]["source"])
                for row in wrong_pad["rows"][:10]:
                    collector.consume(row)
                malformed = copy.deepcopy(wrong_pad["rows"][10])
                malformed["source_tick"] = 4
                collector.consume(malformed)
                with self.assertRaisesRegex(ComparisonError, "PAD consume"):
                    collector.consume(wrong_pad["rows"][11])
            finally:
                browser.close()

    def test_clock60_browser_prefix_must_reach_target_and_stops_at_first_field_difference(self):
        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(Path(directory))
            selected = fixture["selected"]
            browser_path = selected["port_trace"]
            browser_rows = [json.loads(line) for line in browser_path.read_text().splitlines()]
            browser_path.write_text(
                "\n".join(json.dumps(row, separators=(",", ":"))
                          for row in browser_rows[:-1]) + "\n", encoding="utf-8")
            with self.assertRaisesRegex(ComparisonError, "missing the expected session frame"):
                _validate_v10_browser_export(
                    browser_path, fixture["recipe"],
                    fixture["clock_target"]["browser_cursor"], fixture["packet"])

        with tempfile.TemporaryDirectory() as directory:
            fixture = _clock60_comparison_fixture(Path(directory))
            selected = fixture["selected"]
            browser_path = selected["port_trace"]
            browser_rows = [json.loads(line) for line in browser_path.read_text().splitlines()]
            browser_rows[-1]["rng"] ^= 1
            browser_path.write_text(
                "\n".join(json.dumps(row, separators=(",", ":"))
                          for row in browser_rows) + "\n", encoding="utf-8")
            browser = BrowserReader(browser_path)
            try:
                comparator = Comparator(
                    fixture["recipe"], browser,
                    positive_boundary=fixture["packet"]["source"]["first_positive_boundary"],
                    clock60_boundary=fixture["clock_target"])
                collector = SourceCollector(
                    comparator, fixture["recipe"], fixture["source_manifest"],
                    fixture["source_audit"], fixture["packet"]["source"])
                with self.assertRaisesRegex(ComparisonError, "exact state differs at rng"):
                    for row in fixture["rows"]:
                        collector.consume(row)
                self.assertEqual(comparator.first_difference["field"], "rng")
                self.assertEqual(comparator.first_difference["index"], 185)
                self.assertEqual(comparator.match_compared, 183)
            finally:
                browser.close()

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

    def test_legacy_browser_report_reader_keeps_unbounded_text_and_duplicate_key_behavior(self):
        class TextOnlyPath:
            def __init__(self, text):
                self.text = text
                self.encoding = None

            def read_text(self, *, encoding):
                self.encoding = encoding
                return self.text

            def stat(self):
                raise AssertionError("legacy browser report loading must not add a size/stat cap")

        path = TextOnlyPath('{"complete":true}')
        self.assertEqual(_browser_report(path), {"complete": True})
        self.assertEqual(path.encoding, "utf-8")
        with self.assertRaisesRegex(ComparisonError, "duplicate JSON key"):
            _browser_report(TextOnlyPath('{"complete":true,"complete":false}'))
        self.assertIsNone(_browser_report(None))


if __name__ == "__main__":
    unittest.main()
