#!/usr/bin/env python3
"""Compare selected original compact RNG events with long-B browser actions.

This is a strict offline component comparator. It assigns original records by
pinned projection sequence brackets and browser rows by completed action index;
it never pairs guest/native addresses or guard rows by seed.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import sys
from pathlib import Path
from typing import Any

SCHEMA = "melee-web-stadium-long-b-rng-comparison-v1"
INPUT_SCHEMA = "melee-web-stadium-long-b-rng-comparison-input-v1"
REFERENCE_ID = "original-b-5422-951-long-v1"
RECIPE_SHA256 = "e669f831538b90e39241a569b6c38884300bfe246b252cacda994cbc45c10bbf"
PROJECTION_SHA256 = "dead7b678716df87432febba4c8dfedf43acdb47f8d1254bec7b6970f00d3ba1"
BINDING_SHA256 = "07bc3806e8425d416d645bca05d5e821b81a51c34cf47b1ad9b0940106c6fa14"
COMPACT_SHA256 = "7b736093706e8357cb04deecac7c892cacd435dc1b74f1a60fa2f51e1ca10fec"
FIRST_ACTION = 168
LAST_ACTION = 7167
FIRST_PARITY_ACTION = 169
LAST_PARITY_ACTION = 7166
ACTION_COUNT = 7000
FULL_ACTION_COUNT = 7168
MAX_FILE_BYTES = 128 * 1024 * 1024
SEED = re.compile(r"^[0-9a-f]{8}$")
ORIGINAL_POINTER = re.compile(r"^0x[0-9a-f]{8}$")
NATIVE_POINTER = re.compile(r"^[0-9a-f]{16}$")

ROLE_NAMES = {
    "original_binding", "projection", "compact_rng", "primary", "primary_status",
    "si", "si_status", "launch", "setup_receipt", "original_report", "original_producer_manifest", "projection_builder",
    "compact_decoder", "semantic_owner_comparator", "browser_capture_manifest",
    "browser_runtime_build_result", "browser_runtime_inventory", "browser_report",
    "browser_gate2_actions", "browser_rng_probe", "browser_action_rng",
    "browser_capture_harness", "browser_action_binder", "browser_rng_probe_validator",
    "browser_action_consumer", "browser_runner_gate", "browser_runtime_js",
    "browser_runtime_wasm", "browser_runtime_data",
}
BROWSER_SOURCE_ROLES = {
    "browser_capture_harness", "browser_action_binder", "browser_rng_probe_validator",
    "browser_action_consumer", "browser_runner_gate", "browser_runtime_js",
    "browser_runtime_wasm", "browser_runtime_data", "browser_runtime_build_result",
    "browser_runtime_inventory",
}


class EvidenceError(ValueError):
    def __init__(self, message: str, *, first_mismatch: dict[str, Any] | None = None):
        super().__init__(message)
        self.first_mismatch = first_mismatch or {"kind": "validation", "detail": message}


def require(ok: bool, message: str, *, mismatch: dict[str, Any] | None = None) -> None:
    if not ok:
        raise EvidenceError(message, first_mismatch=mismatch)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def parse_json(data: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise EvidenceError(f"{label}: invalid UTF-8 JSON: {exc}") from exc
    require(isinstance(value, dict), f"{label}: expected object")
    return value


def _pin(path_value: Any, label: str) -> tuple[Path, bytes, str]:
    require(isinstance(path_value, dict) and set(path_value) == {"path", "bytes", "sha256"},
            f"{label}: pin fields differ")
    raw_path = path_value["path"]
    require(isinstance(raw_path, str) and raw_path, f"{label}: path missing")
    path = Path(raw_path)
    require(path.is_absolute(), f"{label}: path must be absolute")
    require(not path.is_symlink(), f"{label}: symlink input refused")
    path = path.resolve()
    require(path.is_file(), f"{label}: file missing")
    size = path.stat().st_size
    require(type(path_value["bytes"]) is int and path_value["bytes"] == size and
            0 <= size <= MAX_FILE_BYTES, f"{label}: byte count/bound differs")
    data = path.read_bytes()
    digest = sha256(data)
    require(path_value["sha256"] == digest, f"{label}: SHA-256 differs")
    return path, data, digest


def _pin_manifest(path: Path) -> tuple[dict[str, Any], dict[str, tuple[Path, bytes, str]]]:
    data = path.read_bytes()
    manifest = parse_json(data, "comparison input manifest")
    require(set(manifest) == {"schema", "version", "reference_id", "roles"} and
            manifest.get("schema") == INPUT_SCHEMA and type(manifest.get("version")) is int and
            manifest["version"] == 1 and manifest.get("reference_id") == REFERENCE_ID,
            "comparison input manifest schema/identity differs")
    roles = manifest.get("roles")
    require(isinstance(roles, dict) and set(roles) == ROLE_NAMES,
            "comparison input manifest role set differs")
    pinned = {name: _pin(roles[name], name) for name in sorted(ROLE_NAMES)}
    return manifest, pinned


def _load_module(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    require(spec is not None and spec.loader is not None, f"cannot import pinned {name}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _read_jsonl(data: bytes, label: str) -> list[dict[str, Any]]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label}: not UTF-8") from exc
    require(text.endswith("\n") if text else False, f"{label}: final newline missing")
    rows = []
    for index, line in enumerate(text.splitlines()):
        require(bool(line), f"{label}: empty row {index}")
        try:
            row = json.loads(line, object_pairs_hook=_unique_object)
        except (json.JSONDecodeError, ValueError) as exc:
            raise EvidenceError(f"{label}: invalid JSON row {index}: {exc}") from exc
        require(isinstance(row, dict), f"{label}: row {index} is not an object")
        rows.append(row)
    return rows


def _role_identity(pin: tuple[Path, bytes, str]) -> dict[str, Any]:
    path, data, digest = pin
    return {"path": str(path), "bytes": len(data), "sha256": digest}


def _select_action_ordinals(records: list[dict[str, Any]], begin: int, end: int, *,
                            action_index: int, source_tick: int, draw_ordinal: int | None,
                            after_sequence: int, through_sequence: int,
                            containing_sequence: int, target_sequence: int) -> list[int]:
    """Validate a literal source bracket and select only its declared interval."""
    require(type(begin) is int and type(end) is int and 0 <= begin <= end <= len(records),
            f"action {action_index}: invalid original ordinal span")
    require(type(after_sequence) is int and type(through_sequence) is int and
            after_sequence < through_sequence and type(containing_sequence) is int and
            type(target_sequence) is int and containing_sequence < target_sequence,
            f"action {action_index}: invalid sequence bracket/window")
    selected = []
    previous_sequence = -1
    for ordinal in range(begin, end):
        event = records[ordinal]
        sequence = event.get("observer_next_sequence")
        require(event.get("rng_ordinal") == ordinal and type(sequence) is int and
                previous_sequence <= sequence and after_sequence < sequence <= through_sequence and
                event.get("source_tick") == source_tick and event.get("draw_ordinal") == draw_ordinal,
                f"original return is outside source action bracket at action {action_index}, ordinal {ordinal}",
                mismatch={"kind": "original_action_assignment", "action_index": action_index,
                          "rng_ordinal": ordinal, "observer_next_sequence": sequence})
        if containing_sequence < sequence <= target_sequence:
            selected.append(ordinal)
        previous_sequence = sequence
    return selected


def _match_binding_role(pin: tuple[Path, bytes, str], binding: dict[str, Any], role: str) -> None:
    item = binding.get("files", {}).get(role)
    require(isinstance(item, dict) and set(item) == {"path", "bytes", "sha256"},
            f"original binding role {role} is malformed")
    actual = _role_identity(pin)
    require(Path(item["path"]).resolve() == Path(actual["path"]) and
            item["bytes"] == actual["bytes"] and item["sha256"] == actual["sha256"],
            f"input role does not match original binding: {role}")


def _verify_compact_projection_summary(reported: Any, decoded_summary: dict[str, Any],
                                       decoded_record_count: int) -> None:
    """Bind projection metadata to the strict decoder result without inventing a path field."""
    require(isinstance(reported, dict) and reported == decoded_summary,
            "supplied projection compact validation differs from strict compact decode")
    require(reported.get("record_count") == decoded_record_count,
            "projection compact count differs")


def _verify_original(manifest: dict[str, Any], pins: dict[str, tuple[Path, bytes, str]],
                     compact_decoder: Any):
    binding_path, binding_raw, binding_sha = pins["original_binding"]
    binding = parse_json(binding_raw, "original binding")
    require(binding_sha == BINDING_SHA256 and binding.get("schema") ==
            "melee-web-stadium-later-b-long-reference-operands-v1",
            "original binding identity/schema differs")
    require(set(binding) == {"schema", "files"} and isinstance(binding.get("files"), dict) and
            set(binding["files"]) == {"primary", "primary_status", "si", "si_status", "launch",
                                      "report", "setup_receipt", "producer_manifest", "compact_rng"},
            "original binding role set differs")
    for input_name, binding_name in (("primary", "primary"), ("primary_status", "primary_status"),
                                     ("si", "si"), ("si_status", "si_status"), ("launch", "launch"),
                                     ("setup_receipt", "setup_receipt"), ("original_report", "report"),
                                     ("original_producer_manifest", "producer_manifest"),
                                     ("compact_rng", "compact_rng")):
        _match_binding_role(pins[input_name], binding, binding_name)
    compact_path, compact_raw, compact_sha = pins["compact_rng"]
    require(compact_sha == COMPACT_SHA256 and
            Path(binding["files"]["compact_rng"]["path"]).resolve() == compact_path,
            "original compact role is not the bound selected-B stream")
    status = parse_json(pins["primary_status"][1], "original primary status")
    require(status.get("state") == "completed" and status.get("completed") is True and
            status.get("invalid") is False and status.get("error") is None,
            "original primary producer is not completed and valid")
    header = {"capture_id": "stadium-whole3", "sequence_id": "primary3-allocation2-feedback",
              "dol_sha256": compact_decoder.DOL}
    try:
        decoded = compact_decoder.decode_live_compact(compact_raw, **header, primary_status=status)
    except Exception as exc:
        raise EvidenceError(f"existing strict compact decoder rejected original stream: {exc}") from exc
    require(decoded["valid"] is True and decoded["owner_comparison_ready"] is True and
            decoded["unresolved_owner_events"] == 0,
            "original compact stream contains unresolved owners")
    projection_path, projection_raw, projection_sha = pins["projection"]
    require(projection_sha == PROJECTION_SHA256, "projection hash is not the reviewed V5 projection")
    projection = parse_json(projection_raw, "long-B projection")
    require(projection.get("schema") == "melee-web-stadium-later-b-long-reference-projection-v1" and
            projection.get("reference_id") == REFERENCE_ID and
            projection.get("recipe_sha256") == RECIPE_SHA256 and
            projection.get("same_run_operand_joins_verified") is True and
            projection.get("producer_approval_verified") is False and
            projection.get("runtime_admission") is False and
            projection.get("browser_parity") is False and
            projection.get("per_owner_rng_parity") is False and projection.get("C3") is False,
            "projection schema/source scope or explicit non-admission flags differ")
    require(projection.get("binding_sha256") == binding_sha and projection.get("source_binding") == binding,
            "projection is not bound to the pinned original producer inputs")
    compact_claim = projection.get("compact_rng", {})
    require(compact_claim.get("source") == binding["files"]["compact_rng"],
            "projection compact source differs from the original binding")
    decoded_summary = {k: v for k, v in decoded.items() if k != "records"}
    reported = compact_claim.get("validated")
    _verify_compact_projection_summary(reported, decoded_summary, len(decoded["records"]))
    require(decoded["record_count"] == len(decoded["records"]),
            "projection compact count differs")
    return binding, projection, decoded, projection_sha


def _validate_projection_actions(projection: dict[str, Any], records: list[dict[str, Any]]) -> dict[str, Any]:
    action_rng = projection.get("action_rng")
    selector = {"schema": "melee-web-later-b-long-action-rng-v1",
                "reference_id": REFERENCE_ID, "prefix_cursor": 951,
                "reference_go_tick": 84, "first_action": FIRST_ACTION, "last_action": LAST_ACTION}
    require(isinstance(action_rng, dict) and action_rng.get("selector") == selector and
            action_rng.get("row_schema") == "melee-web-stadium-long-b-action-rng-row-v1" and
            action_rng.get("native_parity") is False,
            "original action RNG selector/schema/admission differs")
    actions = projection.get("actions")
    rows = action_rng.get("rows")
    require(isinstance(actions, list) and len(actions) == FULL_ACTION_COUNT and
            isinstance(rows, list) and len(rows) == ACTION_COUNT,
            "long-B actual action or RNG projection count differs")
    window = projection["compact_rng"]["validated"]["windows"]
    require(len(window) == 1 and window[0]["match"] == 1, "original selected-B window count differs")
    w = window[0]
    require(w["go_tick"] == 84 and w["go_sequence"] == projection["go"]["coordinate"]["sequence"] and
            w["target_source_tick"] == 3584 and w["target_source_sequence"] == 22272 and
            w["containing_source_sequence"] == 4776,
            "original actual-GO/selected-boundary identity differs")
    require(len(records) == w["count"] == action_rng.get("record_count") == 91383 and
            action_rng.get("selected_record_count") == 91318,
            "original event totals differ from the admitted selected-B projection")
    cursor = 0
    selected_total = 0
    selected_by_action: dict[int, list[dict[str, Any]]] = {}
    closing_by_action: list[dict[str, Any]] = []
    guard_first_count = 0
    close_guard_count = 0
    for offset, row in enumerate(rows):
        index = FIRST_ACTION + offset
        action = actions[index]
        require(isinstance(row, dict) and row.get("action_index") == index and
                row.get("kind") == action.get("kind"), f"projection action order differs at {index}")
        source = action.get("source")
        require(isinstance(source, dict), f"projection action {index} has no actual source coordinate")
        coordinate = {"source_tick": source.get("source_tick"), "draw_ordinal": source.get("draw_ordinal")}
        require(row.get("source_coordinate") == coordinate,
                f"projection action coordinate differs at {index}")
        lower_boundary = action.get("consume") if action["kind"] == "tick" else action.get("draw_enter")
        upper_boundary = action.get("source")
        require(isinstance(lower_boundary, dict) and isinstance(upper_boundary, dict),
                f"projection action boundary missing at {index}")
        lower, upper = lower_boundary.get("sequence"), upper_boundary.get("sequence")
        require(type(lower) is int and type(upper) is int and lower < upper and
                row.get("original_boundary_bracket") == {"after_sequence": lower, "through_sequence": upper},
                f"projection sequence bracket differs at action {index}")
        span = row.get("event_ordinals")
        chosen = row.get("selected_event_ordinals")
        require(isinstance(span, list) and len(span) == 2 and isinstance(chosen, list) and len(chosen) == 2 and
                all(type(v) is int for v in span + chosen) and span[0] == cursor and
                span[0] <= span[1] <= len(records),
                f"projection event ordinal span is missing/reordered at action {index}")
        action_events = records[span[0]:span[1]]
        computed = _select_action_ordinals(records, span[0], span[1], action_index=index,
            source_tick=coordinate["source_tick"], draw_ordinal=coordinate["draw_ordinal"],
            after_sequence=lower, through_sequence=upper,
            containing_sequence=w["containing_source_sequence"],
            target_sequence=w["target_source_sequence"])
        require(all(records[o].get("match") == 1 for o in range(span[0], span[1])),
                f"original match identity differs at action {index}")
        expected_chosen = [computed[0], computed[-1] + 1] if computed else [span[1], span[1]]
        require(chosen == expected_chosen and row.get("event_count") == span[1] - span[0] and
                row.get("selected_event_count") == len(computed),
                f"projection selected event range differs at action {index}")
        if index == FIRST_ACTION:
            require(action["kind"] == "tick" and len(computed) == 0 and
                    all(records[o]["observer_next_sequence"] <= w["containing_source_sequence"]
                        for o in range(span[0], span[1])),
                    "original GO-containing partial tick is not a separate guard row")
            guard_first_count = len(action_events)
        else:
            selected_by_action[index] = [records[o] for o in computed]
        if index == LAST_ACTION:
            close_guard_count = len(action_events)
            require(action["kind"] == "draw" and len(computed) == 0 and
                    upper == w["close_sequence"],
                    "original final draw is not the separate natural-close guard")
            closing_by_action = action_events
        selected_total += len(computed)
        cursor = span[1]
    require(cursor == len(records) and selected_total == 91318,
            "projection did not account for every original record exactly once")
    return {"selected_by_action": selected_by_action, "go_guard_events": guard_first_count,
            "closing_draw_guard_events": close_guard_count, "selected_event_count": selected_total,
            "closing_by_action": closing_by_action, "record_count": cursor, "window": w}


def _pointer_identity(owner: dict[str, Any], width: int, label: str) -> dict[str, str]:
    pointer_re = ORIGINAL_POINTER if width == 8 else NATIVE_POINTER
    values = {}
    for field in ("gobj", "proc", "callback"):
        value = owner.get(field)
        require(isinstance(value, str) and pointer_re.fullmatch(value), f"{label}: malformed local {field} pointer")
        values[field] = value
    numeric = [int(value, 16) for value in values.values()]
    if width == 8:
        if owner.get("status") == "outside_proc":
            require(numeric == [0, 0, 0], f"{label}: original outside-proc pointers are nonzero")
        else:
            require(all(0x80000000 <= value <= 0x817ffffc and value % 4 == 0 for value in numeric),
                    f"{label}: original scheduled pointers are invalid")
    else:
        if owner.get("status") == "outside_proc":
            require(numeric == [0, 0, 0], f"{label}: browser outside-proc pointers are nonzero")
        else:
            require(all(value > 0 and value % 4 == 0 for value in numeric[:2]) and numeric[2] > 0,
                    f"{label}: browser scheduled pointers/token are invalid")
    return values


def _owner_semantics(owner: Any, side: str, label: str, owner_tools: Any) -> tuple[dict[str, Any], dict[str, str]]:
    require(isinstance(owner, dict), f"{label}: owner is missing")
    width = 8 if side == "original" else 16
    pointers = _pointer_identity(owner, width, label)
    symbol = owner.get("callback_symbol")
    if symbol == "Fighter_8006A360":
        require(owner.get("status") == "scheduled_proc" and owner.get("classifier") == 4 and
                owner.get("p_link") == 8 and owner.get("s_link") == 1,
                f"{label}: fighter callback owner tuple differs")
        if side == "original":
            fighter = {"slot": owner.get("source_slot"), "entity": owner.get("entity"),
                       "kind": owner.get("fighter_kind")}
        else:
            fighter = owner.get("fighter")
        require(isinstance(fighter, dict) and set(fighter) == {"slot", "entity", "kind"} and
                type(fighter["slot"]) is int and 0 <= fighter["slot"] < 4 and
                type(fighter["entity"]) is int and 0 <= fighter["entity"] < 2 and
                type(fighter["kind"]) is int and 0 <= fighter["kind"] <= 0xffffffff,
                f"{label}: fighter slot/entity/kind is unresolved")
        return ({"status": "scheduled_proc", "callback_symbol": symbol, "classifier": 4,
                 "p_link": 8, "s_link": 1, "fighter": fighter}, pointers)
    if side == "original":
        try:
            semantic, _ = owner_tools._original_owner(owner, label)
        except Exception as exc:
            raise EvidenceError(f"{label}: existing original semantic owner validator rejected row: {exc}") from exc
        require(owner.get("source_slot") is None and owner.get("entity") is None and
                owner.get("fighter_kind") is None,
                f"{label}: non-fighter original owner claims a port")
    else:
        try:
            semantic = owner_tools._native_owner(owner, label)
        except Exception as exc:
            raise EvidenceError(f"{label}: existing browser semantic owner validator rejected row: {exc}") from exc
        require("fighter" in owner and owner.get("fighter") is None,
                f"{label}: non-fighter browser owner claims fighter identity")
    return semantic, pointers


def _native_draw(draw: Any, action_index: int, ordinal: int, owner_tools: Any) -> dict[str, Any]:
    label = f"browser action {action_index} RNG event {ordinal}"
    require(isinstance(draw, dict) and set(draw) == {"kind", "seed_after", "owner"} and
            draw.get("kind") in {"HSD_Rand", "HSD_Randf"} and
            isinstance(draw.get("seed_after"), str) and SEED.fullmatch(draw["seed_after"]),
            f"{label}: malformed draw kind/seed")
    semantic, pointers = _owner_semantics(draw.get("owner"), "browser", label, owner_tools)
    return {"kind": draw["kind"], "seed_after": draw["seed_after"],
            "owner": semantic, "local_pointers": pointers}


def _original_event(record: dict[str, Any], owner_tools: Any) -> dict[str, Any]:
    semantic, pointers = _owner_semantics(record.get("owner"), "original",
                                           f"original RNG ordinal {record['rng_ordinal']}", owner_tools)
    require(record.get("primitive") in {"HSD_Rand", "HSD_Randf"} and
            isinstance(record.get("seed_after"), str) and SEED.fullmatch(record["seed_after"]),
            f"original RNG ordinal {record.get('rng_ordinal')}: primitive/seed unsupported")
    return {"kind": record["primitive"], "seed_after": record["seed_after"],
            "owner": semantic, "local_pointers": pointers}


def _canonical_semantics(event: dict[str, Any]) -> bytes:
    return json.dumps({"kind": event["kind"], "seed_after": event["seed_after"],
                       "owner": event["owner"]}, sort_keys=True, separators=(",", ":")).encode()


def _check_seed_transition(previous_seed: int | None, seed_after: str, *,
                           action_index: int, event_index: int) -> int:
    require(isinstance(seed_after, str) and SEED.fullmatch(seed_after),
            f"browser seed is malformed at action {action_index} event {event_index}")
    current = int(seed_after, 16)
    if previous_seed is not None:
        expected = (214013 * previous_seed + 2531011) & 0xffffffff
        require(current == expected,
                f"browser seed chain discontinuity at action {action_index} event {event_index}",
                mismatch={"kind": "browser_seed_discontinuity", "action_index": action_index,
                          "event_index": event_index, "expected_seed_after": f"{expected:08x}",
                          "actual_seed_after": seed_after})
    return current


def _compare_action(original: list[dict[str, Any]], native: list[dict[str, Any]],
                    action_index: int, original_records: list[dict[str, Any]]) -> list[dict[str, Any]]:
    require(len(original) == len(native),
            f"action {action_index}: RNG event count differs (original {len(original)}, browser {len(native)})",
            mismatch={"kind": "event_count", "action_index": action_index,
                      "original_count": len(original), "browser_count": len(native)})
    compared = []
    for ordinal, (left, right) in enumerate(zip(original, native)):
        for field in ("kind", "seed_after", "owner"):
            require(left[field] == right[field],
                    f"action {action_index} event {ordinal}: {field} differs",
                    mismatch={"kind": "event_value", "action_index": action_index,
                              "event_index": ordinal, "original_rng_ordinal": original_records[ordinal]["rng_ordinal"],
                              "field": field, "original": left[field], "browser": right[field]})
        compared.append({"action_index": action_index, "event_index": ordinal,
                         "original_rng_ordinal": original_records[ordinal]["rng_ordinal"],
                         "kind": left["kind"], "seed_after": left["seed_after"], "owner": left["owner"]})
    return compared


def _verify_capture_report(report: dict[str, Any], projection: dict[str, Any],
                           projection_pin: tuple[Path, bytes, str], binding_pin: tuple[Path, bytes, str],
                           rng_raw_pin: tuple[Path, bytes, str], action_rng_pin: tuple[Path, bytes, str],
                           action_count: int) -> dict[str, Any]:
    gate = report.get("later_b_gate2")
    browser_report = report.get("browser_report")
    probe = report.get("rng_draw_probe")
    require(report.get("result") == "later_b_gate2_component_pass_replay_incomplete" and
            isinstance(gate, dict) and gate.get("component_pass") is True and
            gate.get("accepted_actions") == FULL_ACTION_COUNT and
            gate.get("native_cleanup") == "ACKNOWLEDGED_ONE_SHOT_NATIVE_RETIREMENT_JS_ONLY_FINALIZATION" and
            isinstance(gate.get("token_id"), str) and gate["token_id"],
            "browser report is not the completed bounded long-B component retirement")
    require(isinstance(browser_report, dict) and browser_report.get("complete") is False,
            "browser replay terminal must remain explicitly incomplete")
    transfer = browser_report.get("later_b_transfer", {})
    require(isinstance(transfer, dict) and isinstance(transfer.get("action_evidence"), list) and
            len(transfer["action_evidence"]) == FULL_ACTION_COUNT,
            "browser report omits the complete declared Gate2 action evidence")
    terminal = transfer.get("native_terminal", {})
    receipt = terminal.get("receipt", {})
    state = receipt.get("matched_setup_cf", {})
    require(terminal.get("kind") == "retired" and receipt.get("native_retired") is True and
            receipt.get("terminal") is True and
            receipt.get("later_b_transfer", {}).get("token") == gate["token_id"] and
            receipt.get("later_b_transfer", {}).get("parent_close_checked") is True and
            state.get("schema") == "melee-web-stadium-later-b-long-native-v1" and
            state.get("action_count") == FULL_ACTION_COUNT and state.get("cursor") == FULL_ACTION_COUNT and
            state.get("complete") is True and state.get("source_ticks") == 3585 and
            state.get("source_draws") == 3583 and state.get("post_go_typed22_state_count") == 3501 and
            state.get("actual_go", {}).get("observed") is True and
            state.get("actual_go", {}).get("tick") == 84,
            "browser report lacks the exact correlated full action/GO/retirement receipt")
    for key in ("comparison", "prepared_start_comparison", "prepared_setup_comparison"):
        require(report.get("current_sss_exit", {}).get(key, {}).get("pass") is True,
                f"browser report does not retain passing {key}")
    require(isinstance(probe, dict) and probe.get("complete") is True and
            probe.get("projection_sha256") == sha256(projection_pin[1]) and
            probe.get("captured_action_count") == ACTION_COUNT and probe.get("missing_action_count") == 0,
            "browser action RNG probe is incomplete or bound to another projection")
    action_rng = projection["action_rng"]
    expected_cursors = list(range(FIRST_ACTION, LAST_ACTION + 1))
    require(probe.get("configuration") == action_rng["selector"] and
            probe.get("coordinates") == action_rng["rows"] and
            probe.get("selected_cursors") == expected_cursors,
            "browser RNG request/coordinates differ from the strict original action projection")
    request = probe.get("request")
    require(isinstance(request, dict) and request.get("kind") == "cursors" and
            request.get("cursors") == expected_cursors,
            "browser RNG request does not select every declared action cursor")
    raw_identity = {"name": "rng-draw-probe.jsonl", "bytes": len(rng_raw_pin[1]),
                    "sha256": rng_raw_pin[2]}
    action_identity = {"name": "long-b-action-rng.jsonl", "bytes": len(action_rng_pin[1]),
                       "sha256": action_rng_pin[2]}
    require(probe.get("raw_artifact") == raw_identity and probe.get("action_artifact") == action_identity,
            "browser report does not bind exact raw and completed-action RNG artifacts")
    inputs = report.get("inputs", {})
    for key, pin in (("later_b_gate2_projection", projection_pin), ("later_b_gate2_binding", binding_pin)):
        actual = inputs.get(key)
        require(isinstance(actual, dict) and actual.get("sha256") == pin[2] and
                actual.get("bytes") == len(pin[1]) and Path(actual.get("path", "")).resolve() == pin[0],
                f"browser report {key} input identity differs")
    require(inputs.get("recipe", {}).get("sha256") == RECIPE_SHA256 and
            inputs.get("recipe", {}).get("bytes") == 262226,
            "browser report literal recipe differs from the original long-B reference")
    return {"token_id": gate["token_id"], "native_action_count": state["action_count"],
            "native_cursor": state["cursor"], "native_go_tick": state["actual_go"]["tick"],
            "native_cleanup": gate["native_cleanup"], "full_replay_complete": False}


def _verify_capture_manifest(capture_manifest: dict[str, Any], pins: dict[str, tuple[Path, bytes, str]],
                             roles: dict[str, tuple[Path, bytes, str]]) -> dict[str, Any]:
    require(capture_manifest.get("schema") == "stadium-long-b-browser-capture-v1" and
            isinstance(capture_manifest.get("head"), str) and isinstance(capture_manifest.get("tree"), str),
            "browser capture manifest schema/producer identity differs")
    runtime = capture_manifest.get("runtime")
    require(isinstance(runtime, dict) and runtime.get("head") == capture_manifest["head"] and
            runtime.get("tree") == capture_manifest["tree"] and runtime.get("dirty") is False,
            "browser runtime source identity is not clean/capture-bound")
    build = parse_json(pins["browser_runtime_build_result"][1], "browser runtime build result")
    inventory = parse_json(pins["browser_runtime_inventory"][1], "browser runtime inventory")
    require(build.get("passed") is True and build.get("head") == runtime["head"] and
            build.get("tree") == runtime["tree"] and inventory.get("schema") == "stadium-long-b-runtime-v1" and
            inventory.get("producer", {}).get("head") == runtime["head"] and
            inventory.get("producer", {}).get("tree") == runtime["tree"] and
            inventory.get("producer", {}).get("dirty") is False,
            "browser runtime build/inventory does not bind to the capture source")
    packet_pins = capture_manifest.get("pins")
    require(isinstance(packet_pins, dict), "browser capture manifest producer pins are missing")
    for role in BROWSER_SOURCE_ROLES:
        path, data, digest = roles[role]
        require(packet_pins.get(str(path)) == digest,
                f"capture manifest does not pin actual browser producer role {role}")
    runtime_rows = inventory.get("files")
    require(isinstance(runtime_rows, list), "runtime inventory file identities missing")
    for role, basename in (("browser_runtime_js", "gameplay_menu_browser.js"),
                            ("browser_runtime_wasm", "gameplay_menu_browser.wasm"),
                            ("browser_runtime_data", "gameplay_menu_browser.data")):
        path, data, digest = roles[role]
        matches = [row for row in runtime_rows if isinstance(row, dict) and row.get("relative_path", "").endswith(basename)]
        require(any(row.get("sha256") == digest and
                    (Path(row.get("path", "")).resolve() == path or
                     Path(row.get("source", "")).resolve() == path) for row in matches),
                f"runtime inventory does not bind served artifact {role}")
    for role in ("browser_capture_manifest", "browser_runtime_build_result", "browser_runtime_inventory"):
        path, data, digest = roles[role]
        if role != "browser_capture_manifest":
            require(packet_pins.get(str(path)) == digest,
                    f"capture manifest does not pin {role}")
    return {"head": runtime["head"], "tree": runtime["tree"],
            "dirty": runtime["dirty"], "original_producer": capture_manifest.get("original_producer"),
            "original_harness": capture_manifest.get("original_harness")}


def _verify_report_inputs(report: dict[str, Any], capture_manifest: dict[str, Any]) -> list[str]:
    inputs = report.get("inputs")
    packet_pins = capture_manifest.get("pins")
    require(isinstance(inputs, dict) and isinstance(packet_pins, dict),
            "browser report or capture manifest inputs are missing")
    expected = {"recipe": (RECIPE_SHA256, 262226),
                "current_sss_expected": ("f78de780278400e5a917d53890658d6b84d22870b94264872be39d8687d304c4", 449518),
                "runtime_data": (None, None),
                "later_b_gate2_projection": (PROJECTION_SHA256, None),
                "later_b_gate2_binding": (BINDING_SHA256, None)}
    verified = []
    for key, (expected_sha, expected_bytes) in expected.items():
        item = inputs.get(key)
        require(isinstance(item, dict) and set(item) == {"path", "bytes", "sha256"} and
                isinstance(item.get("path"), str) and Path(item["path"]).is_absolute() and
                type(item.get("bytes")) is int and isinstance(item.get("sha256"), str),
                f"browser report input {key} is malformed")
        require(expected_sha is None or item["sha256"] == expected_sha,
                f"browser report input {key} SHA differs")
        require(expected_bytes is None or item["bytes"] == expected_bytes,
                f"browser report input {key} byte count differs")
        require(packet_pins.get(item["path"]) == item["sha256"],
                f"capture manifest does not pin the exact browser report input {key}")
        verified.append(key)
    command = capture_manifest.get("commands", {}).get("owner")
    require(isinstance(command, list), "capture manifest owner command is missing")
    for flag, key in (("--recipe", "recipe"), ("--runtime-data", "runtime_data"),
                      ("--stadium-current-sss-exit-expected", "current_sss_expected"),
                      ("--stadium-later-b-gate2-projection", "later_b_gate2_projection"),
                      ("--stadium-later-b-gate2-binding", "later_b_gate2_binding")):
        require(flag in command and command.index(flag) + 1 < len(command) and
                command[command.index(flag) + 1] == inputs[key]["path"],
                f"capture command does not use report input {key}")
    return verified


def _verify_action_receipts(projection: dict[str, Any], rows: list[dict[str, Any]]) -> dict[int, dict[str, Any]]:
    actions = projection["actions"]
    require(len(rows) == FULL_ACTION_COUNT, "browser action receipt count is not the complete declared route")
    receipts = {}
    for index, row in enumerate(rows):
        require(set(row) == {"phase", "action_index", "observation"} and
                row.get("phase") == "actual-action-receipt" and row.get("action_index") == index,
                f"browser action receipt order/phase differs at {index}")
        observation = row.get("observation")
        last = observation.get("last_action") if isinstance(observation, dict) else None
        action = actions[index]
        source = action.get("source", {})
        expected_coord = {"source_tick": source.get("source_tick"), "draw_ordinal": source.get("draw_ordinal")}
        expected_ticks = expected_coord["source_tick"]
        expected_draws = expected_coord["draw_ordinal"]
        expected_ordinal = action.get("bundle") if action["kind"] == "tick" else action.get("draw")
        expected_coordinates = ({"source_tick": expected_ticks, "draw_ordinal": None,
                                "recipe_input_index": 951 + action["bundle"]}
                               if action["kind"] == "tick" else
                               {"source_tick": expected_ticks, "draw_ordinal": expected_draws,
                                "recipe_input_index": None})
        require(isinstance(last, dict) and last.get("complete") is True and
                last.get("schedule_index") == index and last.get("action") == action["kind"] and
                last.get("ordinal") == expected_ordinal and last.get("source_coordinates") == expected_coordinates and
                last.get("source_ticks_before") == expected_ticks and
                last.get("source_draws_before") == expected_draws and
                last.get("rng_action_scope") == "diagnostic-action-index" and
                last.get("rng_action_index") == (index if index >= FIRST_ACTION else None),
                f"browser action receipt does not bind the projected source action at {index}")
        receipts[index] = observation
    return receipts


def _verify_browser_rows(projection: dict[str, Any], receipts: dict[int, dict[str, Any]],
                         raw_rows: list[dict[str, Any]], envelopes: list[dict[str, Any]],
                         owner_tools: Any, original_selected: dict[str, Any]) -> dict[str, Any]:
    require(len(raw_rows) == ACTION_COUNT and len(envelopes) == ACTION_COUNT,
            "browser RNG action stream omitted or added rows")
    expected_cursors = list(range(FIRST_ACTION, LAST_ACTION + 1))
    selected_by_action = original_selected["selected_by_action"]
    total_native = 0
    native_by_action: dict[int, list[dict[str, Any]]] = {}
    previous_native_seed: int | None = None
    native_full_tick_guard = 0
    native_closing_guard = 0
    for offset, (raw, envelope, index) in enumerate(zip(raw_rows, envelopes, expected_cursors)):
        expected = projection["action_rng"]["rows"][offset]
        action = projection["actions"][index]
        coordinate = {"source_tick": action["source"]["source_tick"],
                      "draw_ordinal": action["source"]["draw_ordinal"]}
        require(raw.get("schema") == "melee-web-rng-draw-probe" and raw.get("version") == 3 and
                raw.get("source_cursor") == index and raw.get("overflowed") is False and
                isinstance(raw.get("draws"), list) and len(raw["draws"]) <= 1024,
                f"browser RNG raw row identity/bound differs at action {index}")
        require(set(envelope) == {"schema", "reference_id", "action_index", "kind", "source_coordinate", "probe"} and
                envelope.get("schema") == "melee-web-stadium-long-b-action-rng-row-v1" and
                envelope.get("reference_id") == REFERENCE_ID and envelope.get("action_index") == index and
                envelope.get("kind") == action.get("kind") and envelope.get("source_coordinate") == coordinate and
                envelope.get("probe") == raw,
                f"browser completed-action envelope does not bind raw probe/action at {index}")
        receipt = receipts.get(index, {}).get("last_action", {})
        require(receipt.get("rng_action_index") == index and receipt.get("action") == action["kind"] and
                receipt.get("source_ticks_before") == coordinate["source_tick"] and
                receipt.get("source_draws_before") == coordinate["draw_ordinal"],
                f"browser raw probe action lost its completed receipt join at {index}")
        normalized = []
        for event_index, draw in enumerate(raw["draws"]):
            event = _native_draw(draw, index, event_index, owner_tools)
            previous_native_seed = _check_seed_transition(previous_native_seed, event["seed_after"],
                                                          action_index=index, event_index=event_index)
            normalized.append(event)
        native_by_action[index] = normalized
        total_native += len(normalized)
        if index == FIRST_ACTION:
            native_full_tick_guard = len(normalized)
        if index == LAST_ACTION:
            native_closing_guard = len(normalized)
    compared_digest = hashlib.sha256()
    matched = 0
    first_action, last_action = {}, {}
    for index in range(FIRST_PARITY_ACTION, LAST_ACTION):
        original_records = selected_by_action.get(index, [])
        original = [_original_event(row, owner_tools) for row in original_records]
        native = native_by_action[index]
        compared = _compare_action(original, native, index, original_records)
        for event in compared:
            encoded = json.dumps(event, sort_keys=True, separators=(",", ":")).encode("utf-8")
            compared_digest.update(encoded + b"\n")
        matched += len(compared)
        if index == FIRST_PARITY_ACTION:
            first_action = {"action_index": index, "matched_events": len(compared)}
        if index == LAST_ACTION - 1:
            last_action = {"action_index": index, "matched_events": len(compared)}
    # Closing draw is a separate same-coordinate guard, never part of the
    # selected post-GO interval. Compare it only against its exact projected
    # terminal action bracket and report it separately.
    closing_original = [_original_event(row, owner_tools)
                        for row in original_selected["closing_by_action"]]
    closing_browser = native_by_action[LAST_ACTION]
    closing_compared = _compare_action(closing_original, closing_browser, LAST_ACTION,
                                       original_selected["closing_by_action"])
    closing_digest = hashlib.sha256()
    for event in closing_compared:
        closing_digest.update(json.dumps(event, sort_keys=True, separators=(",", ":")).encode() + b"\n")
    require(matched == original_selected["selected_event_count"] == 91318,
            "selected original/browser event total differs")
    return {"browser_rng_event_count_all_rows": total_native,
            "browser_full_tick_168_guard_events_unpaired": native_full_tick_guard,
            "browser_final_draw_7167_guard_events_compared_separately": native_closing_guard,
            "selected_action_first": first_action, "selected_action_last": last_action,
            "selected_matched_event_count": matched,
            "selected_pair_semantic_sha256": compared_digest.hexdigest(),
            "final_close_guard_matched_event_count": len(closing_compared),
            "final_close_guard_semantic_sha256": closing_digest.hexdigest()}


def compare_payloads(*, projection: dict[str, Any], decoded: dict[str, Any],
                     report: dict[str, Any], receipts_rows: list[dict[str, Any]],
                     raw_rows: list[dict[str, Any]], envelopes: list[dict[str, Any]],
                     owner_tools: Any) -> dict[str, Any]:
    original = _validate_projection_actions(projection, decoded["records"])
    receipt_map = _verify_action_receipts(projection, receipts_rows)
    native_report = report.get("rng_draw_probe", {})
    # Artifact pins and producer identities are verified by compare_manifest before this reducer.
    # This function receives already parsed files only and does not invent a provenance claim.
    require(isinstance(native_report, dict), "browser report RNG section is missing")
    browser = _verify_browser_rows(projection, receipt_map, raw_rows, envelopes, owner_tools, original)
    return {"status": "PASS_COMPONENT" if browser["selected_matched_event_count"] == 91318 else "FAIL",
            "scope": "selected original sequence-bracketed RNG events vs completed browser actions only",
            "original_event_count_accounted_once": original["record_count"],
            "original_selected_event_count": original["selected_event_count"],
            "original_partial_actual_GO_tick_guard_events_unpaired": original["go_guard_events"],
            "original_final_close_draw_guard_events": original["closing_draw_guard_events"],
            "selected_action_indices": [FIRST_PARITY_ACTION, LAST_ACTION - 1],
            "browser_native_full_action_168_guard_events_unpaired": browser["browser_full_tick_168_guard_events_unpaired"],
            "browser_native_action_7167_close_guard_events": browser["browser_final_draw_7167_guard_events_compared_separately"],
            "browser_rng_event_count_in_all_7000_rows": browser["browser_rng_event_count_all_rows"],
            "selected_matched_event_count": browser["selected_matched_event_count"],
            "selected_pair_semantic_sha256": browser["selected_pair_semantic_sha256"],
            "original_final_close_guard_matched_separately": browser["final_close_guard_matched_event_count"],
            "final_close_guard_semantic_sha256": browser["final_close_guard_semantic_sha256"],
            "first_and_last_selected_action": [browser["selected_action_first"], browser["selected_action_last"]],
            "raw_guest_native_pointer_equality_compared": False,
            "full_original_browser_lifecycle_parity": False,
            "all_rng_writers_observed": False,
            "C3": False}


def compare_manifest(path: Path, expected_manifest_sha256: str) -> dict[str, Any]:
    raw_manifest = path.read_bytes()
    require(sha256(raw_manifest) == expected_manifest_sha256,
            "comparison input manifest SHA-256 differs from caller pin")
    manifest, pins = _pin_manifest(path)
    roles = manifest["roles"]
    # Keep all known producer roles bound to the exact browser packet and build.
    capture_manifest = parse_json(pins["browser_capture_manifest"][1], "browser capture manifest")
    browser_identity = _verify_capture_manifest(capture_manifest, pins, pins)
    decoder = _load_module(pins["compact_decoder"][0], "pinned_live_compact_decoder")
    owner_tools = _load_module(pins["semantic_owner_comparator"][0], "pinned_rng_owner_comparator")
    binding, projection, decoded, projection_sha = _verify_original(manifest, pins, decoder)
    report = parse_json(pins["browser_report"][1], "browser report")
    verified_report_inputs = _verify_report_inputs(report, capture_manifest)
    report_identity = _verify_capture_report(report, projection, pins["projection"],
        pins["original_binding"], pins["browser_rng_probe"], pins["browser_action_rng"], 7168)
    receipts = _read_jsonl(pins["browser_gate2_actions"][1], "Gate2 action receipts")
    raw_rows = _read_jsonl(pins["browser_rng_probe"][1], "browser raw RNG probe")
    envelopes = _read_jsonl(pins["browser_action_rng"][1], "browser completed-action RNG envelopes")
    result = compare_payloads(projection=projection, decoded=decoded, report=report,
        receipts_rows=receipts, raw_rows=raw_rows, envelopes=envelopes, owner_tools=owner_tools)
    result.update({"schema": SCHEMA, "reference_id": REFERENCE_ID,
        "input_manifest_sha256": expected_manifest_sha256,
        "original_binding_sha256": pins["original_binding"][2],
        "projection_sha256": projection_sha, "compact_rng_sha256": pins["compact_rng"][2],
        "original_producer_manifest_sha256": pins["original_producer_manifest"][2],
        "browser_capture_manifest_sha256": pins["browser_capture_manifest"][2],
        "browser_runtime_build_result_sha256": pins["browser_runtime_build_result"][2],
        "browser_runtime_inventory_sha256": pins["browser_runtime_inventory"][2],
        "browser_report_sha256": pins["browser_report"][2],
        "browser_action_receipts_sha256": pins["browser_gate2_actions"][2],
        "browser_rng_probe_sha256": pins["browser_rng_probe"][2],
        "browser_action_rng_sha256": pins["browser_action_rng"][2],
        "browser_producer": browser_identity, "browser_terminal": report_identity,
        "capture_manifest_report_inputs_verified": verified_report_inputs,
        "projection_builder_sha256": pins["projection_builder"][2],
        "compact_decoder_sha256": pins["compact_decoder"][2],
        "semantic_owner_comparator_sha256": pins["semantic_owner_comparator"][2],
        "browser_code_sha256": {role: pins[role][2] for role in sorted(BROWSER_SOURCE_ROLES)}})
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--manifest-sha256", required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    result = {"schema": SCHEMA, "status": "FAIL", "C3": False}
    try:
        result = compare_manifest(args.manifest, args.manifest_sha256)
    except Exception as exc:
        result.update({"status": "FAIL", "first_mismatch": getattr(exc, "first_mismatch", None) or
                       {"kind": "validation", "detail": str(exc)}, "error": str(exc), "C3": False})
    output = json.dumps(result, sort_keys=True, separators=(",", ":")) + "\n"
    if args.out.exists() or args.out.is_symlink():
        raise SystemExit(f"refusing to overwrite comparison output: {args.out}")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(output, encoding="utf-8")
    print(output, end="")
    return 0 if result.get("status") == "PASS_COMPONENT" else 1


if __name__ == "__main__":
    raise SystemExit(main())
