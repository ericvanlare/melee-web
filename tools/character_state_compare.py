#!/usr/bin/env python3
"""Exact bounded comparison of a four-player source-state prefix.

The source MWRO is streamed only through the selected match setup and the
number of source ticks represented by the selected port prefix. This is not a
whole-session, draw, CPU, timing, PCM, or performance comparison.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any, Mapping, Sequence

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "reference-capture" / "dolphin"), str(ROOT / "tools")]

from reference_capture_semantics import (  # noqa: E402
    SemanticError,
    SliceMemory,
    fighter_state,
    state_snapshot,
)
from reference_observer_stream import ObserverStreamError, iter_records  # noqa: E402
from retail_replay_validation import FIGHTER_KEYS, _pad_state, _validate_fighter  # noqa: E402


SCHEMA = "melee-web-character-state-comparison-v1"
PORT_SCHEMA = "melee-web-port-session-diagnostic"
PORT_VERSION = 2
PORT_ENTITY_INVENTORY = "all_player_entity_slots"
PLAYER_COUNT = 4
MATCH_SCENE = 3
PAD_STATE_BYTES = 822
MAX_MATCH_INDEX = 63
COMPARE_FIELDS = ("rng", "match_frame", "pad_state_hex", "fighters", "fighter_entities")
STATE_KEYS = set(COMPARE_FIELDS)
ENTITY_KEYS = set(FIGHTER_KEYS) | {"entity_index"}
HEX_RE = re.compile(r"^[0-9a-f]+$")
FIGHTER_FIELD_ORDER = (
    "slot", "kind", "motion", "animation", "ground_air", "facing_bits",
    "position_bits", "velocity_bits", "knockback_bits", "animation_frame_bits",
    "animation_speed_bits", "damage_bits", "shield_bits", "stocks", "input_hex",
)


class ComparisonError(ValueError):
    """The bounded capture or trace cannot support an exact comparison."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _file_info(path: Path) -> dict[str, Any]:
    return {"path": str(path), "bytes": path.stat().st_size, "sha256": _sha256(path)}


def _object_no_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ComparisonError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _require_keys(value: Mapping[str, Any], expected: set[str], context: str) -> None:
    actual = set(value)
    if actual != expected:
        raise ComparisonError(
            f"{context}: fields differ (missing={sorted(expected - actual)}, "
            f"unknown={sorted(actual - expected)})")


def _uint(value: Any, context: str, maximum: int = 0xFFFFFFFF) -> int:
    if type(value) is not int or not 0 <= value <= maximum:
        raise ComparisonError(f"{context}: expected integer in [0, {maximum}]")
    return value


def _hex(value: Any, size: int, context: str) -> str:
    if not isinstance(value, str) or len(value) != size * 2 or HEX_RE.fullmatch(value) is None:
        raise ComparisonError(f"{context}: expected lowercase hexadecimal {size:#x}-byte value")
    return value


def _validate_fighter_record(value: Any, slot: int, context: str) -> None:
    try:
        _validate_fighter(value, slot, context)
    except (KeyError, TypeError, ValueError) as error:
        raise ComparisonError(str(error)) from error


def _entity_key(value: Mapping[str, Any], context: str) -> tuple[int, int]:
    return (_uint(value.get("slot"), f"{context}.slot", PLAYER_COUNT - 1),
            _uint(value.get("entity_index"), f"{context}.entity_index", 1))


def _validate_entities(value: Any, context: str) -> list[dict[str, Any]]:
    if not isinstance(value, list) or not value:
        raise ComparisonError(f"{context}: expected a nonempty entity inventory")
    entities: list[dict[str, Any]] = []
    keys: list[tuple[int, int]] = []
    for index, entity in enumerate(value):
        if not isinstance(entity, dict):
            raise ComparisonError(f"{context}[{index}]: entity must be an object")
        _require_keys(entity, ENTITY_KEYS, f"{context}[{index}]")
        key = _entity_key(entity, f"{context}[{index}]")
        _validate_fighter_record(
            {name: entity[name] for name in FIGHTER_KEYS}, key[0], f"{context}[{index}]")
        keys.append(key)
        entities.append(entity)
    if keys != sorted(keys) or len(set(keys)) != len(keys):
        raise ComparisonError(f"{context}: duplicate or unordered slot/entity_index identity")
    by_slot: dict[int, list[int]] = {slot: [] for slot in range(PLAYER_COUNT)}
    for slot, entity_index in keys:
        by_slot[slot].append(entity_index)
    if any(indices != list(range(len(indices))) for indices in by_slot.values()):
        raise ComparisonError(f"{context}: each slot must have contiguous entity indices from zero")
    return entities


def _validate_state(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ComparisonError(f"{context}: state must be an object")
    _require_keys(value, STATE_KEYS, context)
    _uint(value["rng"], f"{context}.rng")
    _uint(value["match_frame"], f"{context}.match_frame")
    _hex(value["pad_state_hex"], PAD_STATE_BYTES, f"{context}.pad_state_hex")
    try:
        _pad_state(value["pad_state_hex"], f"{context}.pad_state_hex")
    except (KeyError, TypeError, ValueError) as error:
        raise ComparisonError(str(error)) from error
    fighters = value["fighters"]
    if not isinstance(fighters, list) or len(fighters) != PLAYER_COUNT:
        raise ComparisonError(f"{context}.fighters: exactly four primary fighters are required")
    for slot, fighter in enumerate(fighters):
        if not isinstance(fighter, dict):
            raise ComparisonError(f"{context}.fighters[{slot}]: fighter must be an object")
        _validate_fighter_record(fighter, slot, f"{context}.fighters[{slot}]")
    entities = _validate_entities(value["fighter_entities"], f"{context}.fighter_entities")
    primary = [(fighter["slot"], 0) for fighter in fighters]
    entity_primary = [(entity["slot"], entity["entity_index"])
                      for entity in entities if entity["entity_index"] == 0]
    if primary != [(slot, 0) for slot in range(PLAYER_COUNT)] or entity_primary != primary:
        raise ComparisonError(f"{context}: primary fighter/entity inventories disagree")
    by_entity = {(entity["slot"], entity["entity_index"]): entity for entity in entities}
    for slot, fighter in enumerate(fighters):
        entity = by_entity[slot, 0]
        if any(fighter[field] != entity[field] for field in FIGHTER_KEYS):
            raise ComparisonError(
                f"{context}: primary fighter slot {slot} disagrees with entity_index 0")
    return value


def _compare_lists(expected: list[dict[str, Any]], actual: list[dict[str, Any]],
                   field: str, entities: bool) -> tuple[str, Any, Any] | None:
    expected_keys = [(item["slot"], item.get("entity_index", 0)) for item in expected]
    actual_keys = [(item["slot"], item.get("entity_index", 0)) for item in actual]
    if expected_keys != actual_keys:
        return f"{field}.inventory", expected_keys, actual_keys
    for index, (left, right) in enumerate(zip(expected, actual)):
        for name in FIGHTER_FIELD_ORDER:
            if left[name] != right[name]:
                if entities:
                    slot, entity_index = expected_keys[index]
                    return (f"{field}[slot={slot},entity_index={entity_index}].{name}",
                            left[name], right[name])
                return f"{field}[{index}].{name}", left[name], right[name]
    return None


def compare_states(expected: Mapping[str, Any], actual: Mapping[str, Any], *,
                   reference_seq: int | None = None,
                   reference_source_tick: int | None = None,
                   port_index: int | None = None,
                   port_line: int | None = None) -> dict[str, Any] | None:
    left = _validate_state(dict(expected), "reference state")
    right = _validate_state(dict(actual), "port state")
    difference: tuple[str, Any, Any] | None = None
    for field in COMPARE_FIELDS:
        if field in ("rng", "match_frame", "pad_state_hex"):
            if left[field] != right[field]:
                difference = field, left[field], right[field]
                break
        else:
            difference = _compare_lists(left[field], right[field], field,
                                        field == "fighter_entities")
            if difference is not None:
                break
    if difference is None:
        return None
    field, reference, port = difference
    return {
        "field": field, "reference": reference, "port": port,
        "reference_seq": reference_seq, "reference_source_tick": reference_source_tick,
        "port_index": port_index, "port_line": port_line,
        "reference_state": left, "port_state": right,
    }


def compare_decoded_prefix(reference_setup: Mapping[str, Any],
                           reference_ticks: Sequence[Mapping[str, Any]],
                           port_setup: Mapping[str, Any],
                           port_frames: Sequence[Mapping[str, Any]], *,
                           reference_setup_seq: int | None = None,
                           reference_tick_metadata: Sequence[Mapping[str, Any]] | None = None,
                           port_setup_line: int | None = None) -> dict[str, Any]:
    """Compare decoded rows for focused synthetic tests."""
    difference = compare_states(reference_setup, port_setup,
                                reference_seq=reference_setup_seq,
                                port_line=port_setup_line)
    if difference is not None:
        return {"result": "diverged", "complete": False, "compared_ticks": 0,
                "first_difference": difference}
    if not reference_ticks:
        raise ComparisonError("selected source match has no source_tick records")
    metadata = list(reference_tick_metadata or [{} for _ in reference_ticks])
    if len(metadata) != len(reference_ticks):
        raise ComparisonError("reference tick metadata count disagrees with source ticks")
    for index, (reference, actual) in enumerate(zip(reference_ticks, port_frames)):
        meta = metadata[index]
        difference = compare_states(
            reference, actual, reference_seq=meta.get("reference_seq"),
            reference_source_tick=meta.get("reference_source_tick"),
            port_index=actual.get("index"), port_line=actual.get("_line"))
        if difference is not None:
            return {"result": "diverged", "complete": False,
                    "compared_ticks": index, "first_difference": difference}
    return {"result": "prefix_unverified", "complete": False,
            "compared_ticks": min(len(reference_ticks), len(port_frames)),
            "first_difference": None,
            "prefix_end": "port_prefix_exhausted" if len(port_frames) < len(reference_ticks)
            else "bounded_prefix_complete",
            "unverified_remainder": True}


def _source_state(payload: Mapping[str, Any], context: str) -> tuple[dict[str, Any], int]:
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    typed: dict[tuple[str, int], dict[str, Any]] = {}
    sizes = {"fighter_head": 0x100, "fighter_input_anim": 0x280,
             "fighter_damage_shield": 0x16C}
    for item in slices:
        if not isinstance(item, dict) or item.get("name") not in sizes:
            continue
        name, flags = item["name"], item.get("flags")
        if type(flags) is not int or not 0 <= flags <= 0xFFFF:
            raise ComparisonError(f"{context}: invalid {name} flags")
        slot, entity_index = flags & 0xFF, flags >> 8
        if slot >= PLAYER_COUNT or entity_index > 1:
            raise ComparisonError(f"{context}: unsupported fighter identity flags {flags:#x}")
        if (name, flags) in typed:
            raise ComparisonError(f"{context}: duplicate {name} slice for flags {flags:#x}")
        if item.get("size") != sizes[name] or type(item.get("address")) is not int:
            raise ComparisonError(f"{context}: malformed {name} slice for flags {flags:#x}")
        typed[name, flags] = item
    heads: dict[tuple[int, int], int] = {}
    for (name, flags), item in typed.items():
        if name != "fighter_head":
            continue
        identity = (flags & 0xFF, flags >> 8)
        heads[identity] = item["address"]
        for companion in ("fighter_input_anim", "fighter_damage_shield"):
            if (companion, flags) not in typed:
                raise ComparisonError(f"{context}: {identity} lacks {companion} slice")
    if {slot for slot, entity in heads if entity == 0} != set(range(PLAYER_COUNT)):
        raise ComparisonError(f"{context}: expected primary fighter heads for slots 0..3")
    for name, count, size in (("fighter_stocks", PLAYER_COUNT, 1),
                              ("rng_value", 1, 4), ("rng_pointer", 1, 4),
                              ("pad_snapshot", 1, 0x358)):
        found = [item for item in slices if isinstance(item, dict) and item.get("name") == name]
        if len(found) != count or any(item.get("size") != size for item in found):
            raise ComparisonError(f"{context}: malformed {name} slices")
        if name == "fighter_stocks" and {item.get("flags") for item in found} != set(range(PLAYER_COUNT)):
            raise ComparisonError(f"{context}: fighter_stocks inventory is not 0..3")
    try:
        memory = SliceMemory(slices)
        entities: list[dict[str, Any]] = []
        for slot, entity_index in sorted(heads):
            fighter = fighter_state(memory, slot, heads[slot, entity_index])
            fighter["entity_index"] = entity_index
            entities.append(fighter)
        state = state_snapshot(memory, {slot: heads[slot, 0] for slot in range(PLAYER_COUNT)})
        scene_frame = state.pop("scene_frame")
    except (SemanticError, KeyError, TypeError, ValueError) as error:
        raise ComparisonError(f"{context}: cannot decode fighter state: {error}") from error
    state["fighter_entities"] = entities
    return _validate_state(state, context), scene_frame


def _source_prefix(path: Path, match_index: int, tick_limit: int) -> dict[str, Any]:
    """Stream only the selected setup and requested source-tick prefix."""
    if tick_limit <= 0:
        raise ComparisonError("selected port prefix has no scene=3 rows")
    setup: tuple[int, dict[str, Any]] | None = None
    ticks: list[dict[str, Any]] = []
    previous_tick: int | None = None
    source = iter_records(path)
    try:
        for row in source:
            if row.get("event") == "start":
                payload = row.get("payload")
                if not isinstance(payload, dict) or payload.get("whole_session") is not True:
                    raise ComparisonError("source MWRO is not a whole-session capture")
                if payload.get("source_revision") != "GALE01r2":
                    raise ComparisonError("source MWRO revision is not GALE01r2")
                continue
            if row.get("event") != "boundary":
                continue
            payload = row.get("payload")
            if not isinstance(payload, dict) or payload.get("whole_session") is not True:
                continue
            if _uint(payload.get("match_index"), "source match_index", MAX_MATCH_INDEX) != match_index:
                continue
            boundary = payload.get("boundary")
            if boundary == "setup":
                if setup is not None:
                    raise ComparisonError(f"source has duplicate setup for match {match_index}")
                state, _ = _source_state(payload, f"source setup match {match_index}")
                setup = row["seq"], state
            elif boundary == "source_tick":
                if setup is None:
                    raise ComparisonError(f"source_tick for match {match_index} precedes setup")
                state, scene_frame = _source_state(
                    payload, f"source tick match {match_index} seq {row['seq']}")
                source_tick = row["source_tick"]
                if source_tick != scene_frame:
                    raise ComparisonError(
                        f"source_tick envelope disagrees with typed scene frame at seq {row['seq']}")
                if previous_tick is not None and source_tick != previous_tick + 1:
                    raise ComparisonError(f"source match {match_index} source_tick is not contiguous")
                ticks.append({"seq": row["seq"], "source_tick": source_tick, "state": state})
                previous_tick = source_tick
                if len(ticks) == tick_limit:
                    break
    except (OSError, ObserverStreamError, ValueError) as error:
        if isinstance(error, ComparisonError):
            raise
        raise ComparisonError(f"source MWRO cannot be read: {error}") from error
    finally:
        source.close()
    if setup is None:
        raise ComparisonError(f"source has no selected match setup {match_index}")
    if not ticks:
        raise ComparisonError(f"source selected match {match_index} has no source_tick records")
    return {"setup": {"seq": setup[0], "state": setup[1]}, "ticks": ticks,
            "prefix_complete": len(ticks) == tick_limit}


def _port_state(row: Mapping[str, Any], context: str) -> dict[str, Any]:
    try:
        state = {key: row[key] for key in STATE_KEYS}
    except KeyError as error:
        raise ComparisonError(f"{context}: missing state field {error.args[0]!r}") from error
    return _validate_state(state, context)


def _read_port(path: Path, match_index: int, max_ticks: int | None) -> dict[str, Any]:
    setups = 0
    selected_setup: tuple[int, dict[str, Any]] | None = None
    selected_frames: list[tuple[int, dict[str, Any]]] = []
    selected_scene3_total = 0
    selected_active = False
    selected_done = False
    expected_index = 0
    end_seen = False
    header: dict[str, Any] | None = None
    try:
        with path.open("r", encoding="utf-8") as stream:
            for line_number, line in enumerate(stream, 1):
                if not line.strip():
                    raise ComparisonError(f"port trace line {line_number}: blank line")
                try:
                    row = json.loads(line, object_pairs_hook=_object_no_duplicates)
                except (UnicodeDecodeError, json.JSONDecodeError, ComparisonError) as error:
                    raise ComparisonError(f"port trace line {line_number}: invalid JSON: {error}") from error
                if not isinstance(row, dict):
                    raise ComparisonError(f"port trace line {line_number}: record is not an object")
                record = row.get("record")
                if header is None:
                    header = row
                    _require_keys(header, {"record", "schema", "version", "fighter_entities",
                                           "frames_requested", "comparison", "cpu_observations",
                                           "draw_state"}, "port header")
                    if (header.get("record") != "header" or header.get("schema") != PORT_SCHEMA or
                            header.get("version") != PORT_VERSION or
                            header.get("fighter_entities") != PORT_ENTITY_INVENTORY or
                            header.get("comparison") != "not_run" or
                            header.get("cpu_observations") != "not_captured" or
                            header.get("draw_state") != "not_captured"):
                        raise ComparisonError("port trace requires diagnostic schema v2 with explicit entity inventory")
                    _uint(header.get("frames_requested"), "port header.frames_requested")
                    continue
                if end_seen:
                    raise ComparisonError(f"port trace line {line_number}: record follows end")
                if record == "session_match_enter_complete":
                    _require_keys(row, STATE_KEYS | {"record"}, f"port setup line {line_number}")
                    state = _port_state(row, f"port setup line {line_number}")
                    if setups == match_index:
                        if selected_setup is not None:
                            raise ComparisonError(f"port has duplicate selected setup {match_index}")
                        selected_setup = line_number, state
                        selected_active = True
                    elif selected_active:
                        selected_done = True
                    setups += 1
                    continue
                if record == "session_frame":
                    common = {"record", "scene", "index", "supplied_inputs", "rng", "pad_state_hex"}
                    expected = common | ({"match_frame", "fighters", "fighter_entities"}
                                         if row.get("scene") == MATCH_SCENE else set())
                    _require_keys(row, expected, f"port frame line {line_number}")
                    if row.get("scene") not in {1, 2, 3, 4, 5}:
                        raise ComparisonError(f"port frame line {line_number}: unsupported scene")
                    if row.get("index") != expected_index:
                        raise ComparisonError(f"port frame line {line_number}: unexpected session frame index")
                    expected_index += 1
                    inputs = row.get("supplied_inputs")
                    if not isinstance(inputs, list) or len(inputs) != PLAYER_COUNT:
                        raise ComparisonError(f"port frame line {line_number}: expected four supplied inputs")
                    for port, value in enumerate(inputs):
                        _hex(value, 11, f"port frame line {line_number}.supplied_inputs[{port}]")
                    _uint(row.get("rng"), f"port frame line {line_number}.rng")
                    _hex(row.get("pad_state_hex"), PAD_STATE_BYTES,
                         f"port frame line {line_number}.pad_state_hex")
                    if row.get("scene") == MATCH_SCENE:
                        _port_state(row, f"port frame line {line_number}")
                    if selected_active and not selected_done:
                        if row.get("scene") == MATCH_SCENE:
                            selected_scene3_total += 1
                            if max_ticks is None or len(selected_frames) < max_ticks:
                                row["_line"] = line_number
                                selected_frames.append((line_number, row))
                        elif selected_scene3_total == 0:
                            raise ComparisonError(
                                f"port frame line {line_number}: selected setup not followed by scene=3")
                        else:
                            selected_done = True
                    continue
                if record == "end":
                    _require_keys(row, {"record", "frames", "status"}, f"port end line {line_number}")
                    if row.get("frames") != expected_index or row.get("status") != "captured":
                        raise ComparisonError(f"port end line {line_number}: terminal count/status disagrees")
                    end_seen = True
                    continue
                raise ComparisonError(f"port trace line {line_number}: unexpected record {record!r}")
    except OSError as error:
        raise ComparisonError(f"port trace cannot be read: {error}") from error
    if header is None:
        raise ComparisonError("port trace is empty")
    if selected_setup is None:
        raise ComparisonError(f"port has no selected match setup {match_index}")
    if not selected_frames:
        raise ComparisonError(f"port selected match {match_index} has no scene=3 rows")
    return {"header": header, "setup": selected_setup, "frames": selected_frames,
            "scene3_total": selected_scene3_total, "trace_complete": end_seen,
            "session_frames": expected_index}


def _report_base(capture: Path, port_trace: Path, match_index: int) -> dict[str, Any]:
    return {
        "schema": SCHEMA,
        "scope": "Exact bounded four-player source-consumed state prefix; complete=false by design.",
        "fields_compared": list(COMPARE_FIELDS),
        "selected_match_index": match_index,
        "scheduling": {
            "source": "MWRO setup/source_tick boundaries with typed source-owned slices",
            "port": "session_match_enter_complete followed by the next scene=3 session_frame rows",
            "players": "Exactly four primary slots (0..3); secondary entity_index records are retained",
            "cpu": "Port CPU decisions are recomputed; CPU observations are not compared",
            "draw": "Draw records are not compared or inferred",
            "performance": "No performance or cadence claim",
        },
        "inputs": {"capture": _file_info(capture), "port_trace": _file_info(port_trace)},
    }


def compare_paths(capture: str | Path, port_trace: str | Path, match_index: int = 0,
                  max_ticks: int | None = None) -> dict[str, Any]:
    capture_path, port_path = Path(capture), Path(port_trace)
    if type(match_index) is not int or not 0 <= match_index <= MAX_MATCH_INDEX:
        raise ComparisonError(f"match_index must be in [0, {MAX_MATCH_INDEX}]")
    if max_ticks is not None and (type(max_ticks) is not int or max_ticks <= 0):
        raise ComparisonError("max_ticks must be a positive integer")
    report = _report_base(capture_path, port_path, match_index)
    try:
        port = _read_port(port_path, match_index, max_ticks)
        source = _source_prefix(capture_path, match_index, len(port["frames"]))
        setup_line, port_setup = port["setup"]
        setup_difference = compare_states(
            source["setup"]["state"], port_setup,
            reference_seq=source["setup"]["seq"], port_line=setup_line)
        if setup_difference is not None:
            report.update(result="diverged", complete=False, compared_ticks=0,
                          first_difference=setup_difference)
            return report
        for index, source_tick in enumerate(source["ticks"]):
            port_line, port_row = port["frames"][index]
            difference = compare_states(
                source_tick["state"], _port_state(port_row, f"port frame line {port_line}"),
                reference_seq=source_tick["seq"], reference_source_tick=source_tick["source_tick"],
                port_index=port_row["index"], port_line=port_line)
            if difference is not None:
                report.update(result="diverged", complete=False, compared_ticks=index,
                              first_difference=difference)
                return report
        if not source["prefix_complete"]:
            prefix_end = "source_prefix_exhausted_before_requested_rows"
        elif not port["trace_complete"]:
            prefix_end = "port_trace_prefix_exhausted_without_end"
        elif port["scene3_total"] > len(port["frames"]):
            prefix_end = "bounded_max_ticks_reached"
        else:
            prefix_end = "bounded_prefix_compared"
        report.update(
            result="prefix_unverified", complete=False,
            compared_ticks=len(source["ticks"]), first_difference=None,
            prefix_end=prefix_end, unverified_remainder=True,
            source_setup_seq=source["setup"]["seq"],
            source_first_tick={"seq": source["ticks"][0]["seq"],
                               "source_tick": source["ticks"][0]["source_tick"]},
            source_last_tick={"seq": source["ticks"][-1]["seq"],
                              "source_tick": source["ticks"][-1]["source_tick"]},
            port_setup_line=setup_line,
            port_first_index=port["frames"][0][1]["index"],
            port_last_index=port["frames"][-1][1]["index"],
            port_scene3_rows_available=port["scene3_total"],
            unverified_scope="No source or port rows after this aligned prefix were compared.")
        return report
    except (ComparisonError, OSError, KeyError, TypeError, ValueError) as error:
        report.update(result="invalid", complete=False, error=str(error))
        return report


def _write_report(path: Path, report: Mapping[str, Any]) -> None:
    if path.exists() or path.is_symlink():
        raise SystemExit(f"refusing to overwrite {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--port-trace", type=Path, required=True)
    parser.add_argument("--match-index", type=int, default=0)
    parser.add_argument("--max-ticks", type=int,
                        help="compare at most this many aligned scene=3 rows")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    if args.out.exists() or args.out.is_symlink():
        raise SystemExit(f"refusing to overwrite {args.out}")
    report = compare_paths(args.capture, args.port_trace, args.match_index, args.max_ticks)
    _write_report(args.out, report)
    print(json.dumps({key: value for key, value in report.items()
                      if key not in {"first_difference"}}, indent=2, sort_keys=True))
    return {"prefix_unverified": 0, "diverged": 1, "invalid": 2}[report["result"]]


if __name__ == "__main__":
    sys.exit(main())
