#!/usr/bin/env python3
"""Strict offline comparison of a same-primary original RNG sidecar and Gate2 rows.

The comparison joins original returns to checked MWRO/projection sequence
intervals. It never pairs by source_tick, raw address, or seed alone.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable

SCHEMA = "melee-web-stadium-rng-owner-window-comparison-v1"
INPUT_SCHEMA = "melee-web-stadium-rng-owner-window-input-v1"
ORIGINAL_SCHEMA = "melee-web-cpu-register-probe"
NATIVE_SCHEMA = "melee-web-rng-draw-probe"
PC_TO_KIND = {"0x80380524": "HSD_Rand", "0x8038057c": "HSD_Randf"}
UPDATE_PC_TO_KIND = {**PC_TO_KIND, "0x803805b8": "HSD_Rand"}
PC_TO_LABEL = {"0x80380524": "rand_return", "0x8038057c": "randf_return",
               "0x803805b8": "randi_return"}
SEED_HEX = re.compile(r"^[0-9a-f]{8}$")
ORIG_PTR = re.compile(r"^0x[0-9a-f]{8}$")


class EvidenceError(ValueError):
    pass


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def _sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def _read_json(path: Path, label: str) -> dict[str, Any]:
    try:
        return _parse_json(path.read_bytes(), label)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"{label}: cannot read JSON: {exc}") from exc


def _parse_json(data: bytes, label: str) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"{label}: cannot parse pinned JSON: {exc}") from exc
    _require(isinstance(value, dict), f"{label}: expected an object")
    return value


def _pin_path(base: Path, role: str, entry: Any) -> tuple[Path, bytes, str]:
    _require(isinstance(entry, dict), f"input role {role} is not pinned")
    _require(set(entry) == {"path", "bytes", "sha256"},
             f"input role {role} has unknown or missing pin fields")
    rel = entry.get("path")
    _require(isinstance(rel, str) and rel, f"input role {role} has no path")
    path = Path(rel)
    if not path.is_absolute():
        path = base / path
    _require(not path.is_symlink(), f"input role {role} is symlinked: {path}")
    path = path.resolve()
    _require(path.is_file(), f"input role {role} is missing: {path}")
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    _require(type(entry.get("bytes")) is int and entry["bytes"] == len(data),
             f"input role {role} byte count changed")
    _require(entry.get("sha256") == digest, f"input role {role} SHA-256 changed")
    return path, data, digest


def load_pinned_inputs(manifest_path: Path) -> tuple[dict[str, Any], dict[str, tuple[Path, bytes, str]]]:
    manifest = _read_json(manifest_path, "input manifest")
    _require(set(manifest) == {"schema", "version", "source_checkout", "roles"},
             "input manifest contains an unknown or missing field")
    _require(manifest.get("schema") == INPUT_SCHEMA and
             type(manifest.get("version")) is int and manifest.get("version") == 1,
             "unsupported comparison input manifest")
    roles = manifest.get("roles")
    required = {
        "original_binding", "original_report", "original_status", "original_primary_mwro",
        "original_rng_probe", "projection", "browser_report", "native_rng_jsonl",
        "observer_decoder", "projection_builder", "rng_probe_validator",
    }
    _require(isinstance(roles, dict) and set(roles) == required,
             "input roles must be exactly the supported pinned role set")
    pinned = {name: _pin_path(manifest_path.parent, name, roles[name]) for name in sorted(required)}
    return manifest, pinned


def verify_source_checkout(manifest: dict[str, Any]) -> Path:
    source = manifest.get("source_checkout")
    _require(isinstance(source, dict) and set(source) == {"path", "head"},
             "input manifest source checkout identity is malformed")
    root = Path(source["path"]).resolve()
    _require(root.is_dir() and isinstance(source["head"], str) and
             re.fullmatch(r"[0-9a-f]{40}", source["head"]) is not None,
             "input manifest source checkout path/head is invalid")
    try:
        head = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                              check=True, capture_output=True, text=True, timeout=10).stdout.strip()
        status = subprocess.run(["git", "-C", str(root), "status", "--porcelain=v1", "--untracked-files=no"],
                                check=True, capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.SubprocessError) as exc:
        raise EvidenceError(f"source checkout identity could not be verified: {exc}") from exc
    _require(head == source["head"] and not status,
             "source checkout differs from the pinned clean projection-builder revision")
    return root


def _load_tool_module(path: Path, name: str, source_root: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    _require(spec is not None and spec.loader is not None, f"cannot load pinned {name}")
    sys.path.insert(0, str(source_root))
    sys.path.insert(0, str(source_root / "tools"))
    sys.path.insert(0, str(source_root / "reference-capture" / "dolphin"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _canonical(value: Any) -> bytes:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def validate_original_binding(binding_path: Path, binding: dict[str, Any],
                              pinned: dict[str, tuple[Path, bytes, str]],
                              source_root: Path) -> tuple[dict[str, Any], Any, dict[str, Any]]:
    builder_path = pinned["projection_builder"][0]
    validator_path = pinned["rng_probe_validator"][0]
    _require(builder_path == (source_root / "tools" / "stadium_later_b_reference.py").resolve(),
             "projection builder is outside the pinned source checkout")
    _require(validator_path == (source_root / "scripts" / "reference_capture_app.py").resolve(),
             "RNG validator is outside the pinned source checkout")
    _require(pinned["observer_decoder"][0] ==
             (source_root / "reference-capture" / "dolphin" / "reference_observer_stream.py").resolve(),
             "MWRO decoder is outside the pinned source checkout")
    builder = _load_tool_module(builder_path, "pinned_stadium_later_b_reference", source_root)
    # Existing binding validation hashes every role and rejects unknown/missing roles.
    try:
        bound_paths = builder.verified_files(binding)
    except Exception as exc:
        raise EvidenceError(f"existing strict operand binding rejected: {exc}") from exc
    for role, external_role in (("report", "original_report"),
                                ("primary_status", "original_status"),
                                ("primary", "original_primary_mwro")):
        _require(bound_paths[role].resolve() == pinned[external_role][0].resolve(),
                 f"external {external_role} is not the exact operand-binding {role}")
    try:
        launch = json.loads(bound_paths["launch"].read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise EvidenceError(f"strict binding launch metadata is unreadable: {exc}") from exc
    _require(isinstance(launch, dict), "strict binding launch metadata is not an object")
    # The original binding and the projection are the same-run relation. Re-run the
    # existing projection decoder and demand exact canonical output; leave its
    # same_run_provenance_verified=false field untouched.
    try:
        projected = builder.build_gate2_projection(binding_path)
    except Exception as exc:
        raise EvidenceError(f"existing strict Gate2 projection decoder rejected binding: {exc}") from exc
    rng_validator = _load_tool_module(validator_path, "pinned_reference_capture_app", source_root)
    _require(callable(getattr(rng_validator, "validate_cpu_rng_probe", None)),
             "pinned RNG validator has no combined-probe validator")
    return projected, rng_validator, launch


def _load_decoder(path: Path):
    spec = importlib.util.spec_from_file_location("pinned_reference_observer_stream", path)
    _require(spec is not None and spec.loader is not None, "cannot load pinned MWRO decoder")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    _require(callable(getattr(module, "iter_records", None)),
             "pinned decoder has no iter_records API")
    return module


def _load_jsonl(data: bytes, label: str) -> list[dict[str, Any]]:
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise EvidenceError(f"{label}: JSONL is not UTF-8") from exc
    rows: list[dict[str, Any]] = []
    for index, line in enumerate(text.splitlines()):
        if not line:
            raise EvidenceError(f"{label}: empty line at row {index}")
        try:
            row = json.loads(line)
        except json.JSONDecodeError as exc:
            raise EvidenceError(f"{label}: invalid JSON at row {index}: {exc}") from exc
        _require(isinstance(row, dict), f"{label}: row {index} is not an object")
        rows.append(row)
    return rows


def _hex_or_int(value: Any, label: str) -> int:
    if isinstance(value, bool):
        raise EvidenceError(f"{label}: boolean is not an integer coordinate")
    if isinstance(value, int) and value >= 0:
        return value
    if isinstance(value, str) and re.fullmatch(r"0x[0-9a-fA-F]+", value):
        return int(value, 16)
    raise EvidenceError(f"{label}: invalid address/coordinate")


def _verify_coordinate(record: dict[str, Any], coordinate: dict[str, Any],
                       boundary: str, label: str) -> None:
    payload = record.get("payload")
    _require(record.get("event") == "boundary" and isinstance(payload, dict),
             f"{label}: referenced sequence is not an MWRO boundary")
    _require(payload.get("boundary") == boundary,
             f"{label}: expected {boundary}, observed {payload.get('boundary')!r}")
    for field in ("source_tick", "draw_ordinal", "match_index"):
        if field in coordinate:
            _require(record.get(field) == coordinate[field] if field != "match_index"
                     else payload.get(field) == coordinate[field],
                     f"{label}: {field} differs from raw MWRO")
    for field in ("pc", "lr"):
        if field in coordinate:
            _require(_hex_or_int(payload.get(field), f"{label}.{field}") ==
                     _hex_or_int(coordinate[field], f"projection.{label}.{field}"),
                     f"{label}: {field} differs from raw MWRO")


def _actual_go(projection: dict[str, Any], by_seq: dict[int, dict[str, Any]]) -> tuple[int, int, dict[str, Any]]:
    go = projection.get("go")
    coordinate = go.get("coordinate") if isinstance(go, dict) else None
    _require(isinstance(coordinate, dict) and coordinate.get("phase") == "go_after",
             "projection has no authored actual-GO coordinate")
    seq, tick = coordinate.get("sequence"), coordinate.get("source_tick")
    _require(type(seq) is int and type(tick) is int and seq in by_seq,
             "actual-GO coordinate is not present in primary MWRO")
    record = by_seq[seq]
    payload = record.get("payload")
    _require(record.get("event") == "progress" and isinstance(payload, dict) and
             payload.get("phase") == "go_after",
             "projected GO does not bind the actual progress/go_after event")
    _require(record.get("source_tick") == tick and record.get("draw_ordinal") == coordinate.get("draw_ordinal"),
             "projected GO tick/draw identity differs from raw event")
    for field in ("pc", "lr"):
        if field in coordinate:
            _require(_hex_or_int(payload.get(field), f"raw GO {field}") ==
                     _hex_or_int(coordinate[field], f"projection GO {field}"),
                     f"projected GO {field} differs from raw event")
    if "match_index" in coordinate:
        _require(payload.get("match_index") == coordinate["match_index"],
                 "projected GO match identity differs from raw event")
    return tick, seq, coordinate


def _action_index(projection: dict[str, Any]) -> tuple[dict[int, dict[str, Any]], dict[int, dict[str, Any]]]:
    actions = projection.get("actions")
    _require(isinstance(actions, list) and actions,
             "Gate2 projection has no actual tick/draw action list")
    ticks: dict[int, dict[str, Any]] = {}
    draws: dict[int, dict[str, Any]] = {}
    last_index = -1
    for action in actions:
        _require(isinstance(action, dict), "projection contains malformed action")
        index = action.get("action_index")
        _require(type(index) is int and index > last_index,
                 "projection action indexes are duplicate or reordered")
        last_index = index
        source_tick = action.get("source_tick")
        _require(type(source_tick) is int and source_tick >= 0,
                 "projection action source_tick is invalid")
        target = ticks if action.get("kind") == "tick" else draws if action.get("kind") == "draw" else None
        _require(target is not None, "projection contains unsupported action kind")
        _require(source_tick not in target, "projection duplicates an action source_tick")
        target[source_tick] = action
    return ticks, draws


def derive_intervals(projection: dict[str, Any], by_seq: dict[int, dict[str, Any]],
                     first_offset: int = 1, last_offset: int = 3) -> tuple[list[dict[str, Any]], dict[str, int]]:
    _require(first_offset == 1 and last_offset == 3,
             "this comparison version supports only post-GO offsets 1:3")
    _require(projection.get("schema") == "melee-web-stadium-later-b-gate2-reference-projection-v1",
             "unsupported Gate2 projection schema")
    _require(projection.get("selected_match") == 1 and
             projection.get("setup_global_cursor") == 943 and
             isinstance(projection.get("actions"), list) and len(projection["actions"]) == 360,
             "projection is outside the currently admitted match-1 943-prefix/360-action Gate2 contract")
    _require(projection.get("C3") is False,
             "projection must retain its explicit non-C3 status")
    go_tick, go_seq, go_coordinate = _actual_go(projection, by_seq)
    ticks, draws = _action_index(projection)
    rows: list[dict[str, Any]] = []
    for offset in range(0, last_offset + 2):  # offset 0 and 4 are explicit guards
        tick_number = go_tick + offset
        tick_action = ticks.get(tick_number)
        draw_action = draws.get(tick_number + 1)
        _require(isinstance(tick_action, dict) and isinstance(draw_action, dict),
                 f"projection lacks the complete action pair for GO-relative tick {offset}")
        _require(draw_action["action_index"] == tick_action["action_index"] + 1,
                 f"projection tick/draw actions are not adjacent at offset {offset}")
        _require(tick_action.get("source_tick") == tick_number and
                 draw_action.get("source_tick") == tick_number + 1,
                 f"projection source-tick relation is invalid at offset {offset}")
        consume = tick_action.get("consume")
        tick_return = tick_action.get("tick")
        draw_enter = draw_action.get("draw_enter")
        draw_return = draw_action.get("draw_return")
        _require(all(isinstance(value, dict) for value in
                     (consume, tick_return, draw_enter, draw_return)),
                 f"projection omitted a boundary at GO-relative tick {offset}")
        sequences = [value.get("sequence") for value in
                     (consume, tick_return, draw_enter, draw_return)]
        _require(all(type(value) is int for value in sequences) and
                 sequences[0] < sequences[1] < sequences[2] < sequences[3],
                 f"projection boundary sequence order is invalid at offset {offset}")
        _verify_coordinate(by_seq.get(sequences[0], {}), consume, "pad_consume", f"offset{offset}.consume")
        _verify_coordinate(by_seq.get(sequences[1], {}), tick_return, "source_tick", f"offset{offset}.tick")
        _verify_coordinate(by_seq.get(sequences[2], {}), draw_enter, "draw_enter", f"offset{offset}.draw_enter")
        _verify_coordinate(by_seq.get(sequences[3], {}), draw_return, "draw_return", f"offset{offset}.draw_return")
        if offset == 0:
            _require(sequences[0] < go_seq < sequences[3],
                     "first partial guard does not straddle actual GO")
        else:
            _require(go_seq < sequences[0],
                     f"GO is not before the full tick/draw interval at offset {offset}")
        rows.append({
            "offset": offset,
            "source_tick": tick_number,
            "draw_ordinal": tick_return.get("draw_ordinal"),
            "consume_sequence": sequences[0],
            "tick_return_sequence": sequences[1],
            "draw_enter_sequence": sequences[2],
            "draw_return_sequence": sequences[3],
            "partial": offset == 0,
            "selected": first_offset <= offset <= last_offset,
        })
    config = projection.get("setup_global_cursor")
    _require(type(config) is int and config >= 0, "projection setup_global_cursor is invalid")
    return rows, {"go_tick": go_tick, "go_sequence": go_seq, "prefix_cursor": config}


def _original_rng_sites(probe: dict[str, Any]) -> dict[str, str]:
    _require(probe.get("schema") == ORIGINAL_SCHEMA,
             "original CPU RNG sidecar schema is unsupported")
    version, selector = probe.get("version"), probe.get("rng_return_site")
    if version == 4 and selector == "rng_returns":
        sites = PC_TO_KIND
    elif version == 5 and selector == "rng_updates":
        sites = UPDATE_PC_TO_KIND
    else:
        raise EvidenceError("original CPU RNG sidecar coverage/version is unsupported")
    _require(probe.get("rng_return_pcs") == list(sites),
             "original RNG sidecar lacks the complete selected site/window")
    return sites


def _validate_original_probe(probe: dict[str, Any], validation: dict[str, Any],
                             status: dict[str, Any], primary_identity: dict[str, Any],
                             intervals: list[dict[str, Any]], go: dict[str, int],
                             probe_sha256: str) -> list[dict[str, Any]]:
    sites = _original_rng_sites(probe)
    _require(probe.get("diagnostic_only") is True and probe.get("window_complete") is True and
             probe.get("source_revision") == "GALE01r2" and
             probe.get("match") == 1,
             "original RNG sidecar lacks the complete selected site/window")
    _require(probe.get("window_basis") == "actual_go_raw_guard_band" and
             probe.get("requested_first_offset") == 1 and
             probe.get("requested_last_offset") == 3 and
             probe.get("first_guard_tick_partial") is True and
             probe.get("go_tick") == go["go_tick"] and
             probe.get("go_observer_sequence") == go["go_sequence"],
             "original sidecar GO-relative window metadata is invalid")
    first_raw = go["go_tick"]
    last_raw = go["go_tick"] + 4
    if "first_tick" in probe:
        _require(probe["first_tick"] == first_raw, "original sidecar first raw guard tick changed")
    if "last_tick" in probe:
        _require(probe["last_tick"] == last_raw, "original sidecar last raw guard tick changed")
    for key in ("capture_id", "sequence_id", "dol_sha256"):
        _require(probe.get(key) == primary_identity.get(key),
                 f"original sidecar {key} differs from primary MWRO producer")
    records = probe.get("records")
    _require(isinstance(records, list) and 1 <= len(records) <= 4096 and
             probe.get("record_count") == len(records),
             "original RNG sidecar records/count are invalid")
    _require(validation.get("valid") is True and validation.get("output_sha256") is not None and
             validation.get("capture_id") == probe["capture_id"] and
             validation.get("sequence_id") == probe["sequence_id"] and
             validation.get("dol_sha256") == probe["dol_sha256"],
             "original RNG sidecar validation receipt is missing or mismatched")
    close = status.get("cpu_probe_close")
    _require(isinstance(close, dict) and close.get("window_basis") == probe["window_basis"] and
             close.get("requested_first_offset") == 1 and close.get("requested_last_offset") == 3 and
             close.get("go_tick") == go["go_tick"] and
             close.get("go_observer_sequence") == go["go_sequence"] and
             close.get("first_guard_tick_partial") is True and
             close.get("configured_match") == 1 and close.get("first_tick") == first_raw and
             close.get("last_tick") == last_raw and
             close.get("reason") == "source_tick_after_window" and
             type(close.get("source_tick")) is int and close["source_tick"] > last_raw,
             "original RNG sidecar close status is not a matching natural guard-band close")
    _require(status.get("state") == "completed" and status.get("completed") is True and
             status.get("invalid") is False and status.get("error") is None,
             "primary original status is not a clean completed stream")
    _require(validation.get("output_sha256") == probe_sha256,
             "original validator output hash does not match pinned sidecar bytes")
    ordered = []
    previous_ord = -1
    previous_seq = -1
    previous_draw = -1
    last_seed: dict[str, int] = {}
    for index, record in enumerate(records):
        _require(isinstance(record, dict) and record.get("rng_ordinal") == index,
                 f"original RNG record ordinal is invalid at {index}")
        seq = record.get("observer_next_sequence")
        draw_ordinal = record.get("draw_ordinal")
        source_tick = record.get("source_tick")
        _require(type(seq) is int and seq > 0 and seq >= previous_seq and
                 type(draw_ordinal) is int and draw_ordinal >= previous_draw and
                 type(source_tick) is int and first_raw <= source_tick <= last_raw,
                 f"original RNG record coordinates are invalid at {index}")
        previous_ord, previous_seq, previous_draw = index, seq, draw_ordinal
        pc = record.get("pc")
        kind = sites.get(pc)
        _require(kind is not None and record.get("label") == PC_TO_LABEL.get(pc),
                 f"original RNG record site/kind is unsupported at {index}")
        _require(record.get("match") == 1,
                 f"original RNG record match identity is invalid at {index}")
        pointer = record.get("seed_owner_pointer")
        _require(isinstance(pointer, str) and ORIG_PTR.fullmatch(pointer) and
                 0x80000000 <= int(pointer, 16) <= 0x817ffffc and int(pointer, 16) % 4 == 0,
                 f"original RNG actual seed owner pointer is invalid at {index}")
        legacy_pair = record.get("random_seed_and_pointer")
        _require(isinstance(legacy_pair, str) and re.fullmatch(r"0x[0-9a-f]{16}", legacy_pair) and
                 int(legacy_pair[-8:], 16) == int(pointer, 16),
                 f"original RNG source global/pointer provenance is malformed at {index}")
        seed = record.get("seed_after")
        _require(isinstance(seed, str) and SEED_HEX.fullmatch(seed),
                 f"original RNG actual postseed is invalid at {index}")
        if pointer in last_seed:
            expected = (214013 * last_seed[pointer] + 2531011) & 0xffffffff
            _require(int(seed, 16) == expected,
                     f"original actual seed discontinuity at RNG ordinal {index}")
        last_seed[pointer] = int(seed, 16)
        original_owner = record.get("owner")
        owner, owner_pointers = _original_owner(original_owner, f"original RNG ordinal {index}")
        ordered.append({"ordinal": index, "observer_next_sequence": seq,
                        "source_tick": source_tick, "draw_ordinal": draw_ordinal,
                        "kind": kind, "seed_after": seed, "owner": owner,
                        "raw_owner_pointer": pointer,
                        "raw_global_seed_pointer_snapshot": legacy_pair,
                        "owner_raw_pointers": owner_pointers})
        if probe["version"] == 5:
            gpr = record.get("gpr")
            _require(isinstance(gpr, list) and len(gpr) == 32 and
                     all(isinstance(word, str) and ORIG_PTR.fullmatch(word) for word in gpr),
                     f"original RNG raw register provenance is malformed at {index}")
            # Randi's scaled integer return is provenance, never the primitive
            # seed or a cross-platform return-value comparison operand.
            ordered[-1]["raw_return_site"] = {"pc": pc, "label": record["label"],
                                               "gpr3": gpr[3]}
    return ordered


def _semantic_owner(owner: Any, label: str) -> dict[str, Any]:
    _require(isinstance(owner, dict), f"{label}: owner object is missing")
    status = owner.get("status")
    _require(status in {"scheduled_proc", "outside_proc"}, f"{label}: owner status is unresolved")
    fields = ("classifier", "p_link", "s_link")
    normalized: dict[str, Any] = {"status": status}
    for field in fields:
        value = owner.get(field)
        _require(type(value) is int and 0 <= value <= {"classifier": 65535, "p_link": 255, "s_link": 255}[field],
                 f"{label}: owner {field} is invalid")
        normalized[field] = value
    symbol = owner.get("callback_symbol")
    if status == "outside_proc":
        _require(symbol is None and all(normalized[field] == 0 for field in fields),
                 f"{label}: outside-proc semantic owner is inconsistent")
        normalized["callback_symbol"] = None
    else:
        _require(symbol in {"grStadium_801D1390", "efLib_particles_proc_main"},
                 f"{label}: callback symbol is unresolved")
        if symbol == "efLib_particles_proc_main":
            _require(tuple(normalized[field] for field in fields) == (8, 11, 15),
                     f"{label}: particle callback semantic owner tuple differs")
        normalized["callback_symbol"] = symbol
    return normalized


def _raw_owner_pointers(owner: Any, label: str, width: int) -> dict[str, str]:
    _require(isinstance(owner, dict), f"{label}: owner object is missing")
    pattern = re.compile(rf"^0x[0-9a-f]{{8}}$" if width == 8 else rf"^[0-9a-f]{{{width}}}$")
    out = {}
    for name in ("gobj", "proc", "callback"):
        value = owner.get(name)
        _require(isinstance(value, str) and pattern.fullmatch(value),
                 f"{label}: raw {name} pointer is malformed")
        out[name] = value
    return out


def _native_owner(owner: Any, label: str) -> dict[str, Any]:
    _require(isinstance(owner, dict), f"{label}: native owner object is missing")
    # GObj/proc are memory pointers; callback is a WASM function-table token.
    # All raw numbers stay local provenance and are never paired numerically.
    semantic = _semantic_owner(owner, label)
    pointers = _raw_owner_pointers(owner, label, 16)
    numbers = [int(value, 16) for value in pointers.values()]
    if semantic["status"] == "outside_proc":
        _require(numbers == [0, 0, 0], f"{label}: native outside-proc pointers are nonzero")
    else:
        _require(all(value > 0 and value % 4 == 0 for value in numbers[:2]) and numbers[2] > 0,
                 f"{label}: native scheduled-owner pointers/token are invalid")
    return semantic


def _original_owner(owner: Any, label: str) -> tuple[dict[str, Any], dict[str, str]]:
    semantic = _semantic_owner(owner, label)
    pointers = _raw_owner_pointers(owner, label, 8)
    numbers = [int(value, 16) for value in pointers.values()]
    if semantic["status"] == "outside_proc":
        _require(numbers == [0, 0, 0], f"{label}: original outside-proc pointers are nonzero")
    else:
        _require(all(0x80000000 <= value <= 0x817ffffc and value % 4 == 0 for value in numbers),
                 f"{label}: original scheduled-owner pointers are invalid")
    return semantic, pointers


def _validate_native(report: dict[str, Any], rows: list[dict[str, Any]],
                     projection_sha: str, coordinates: list[dict[str, Any]],
                     go: dict[str, int], offsets: Iterable[int],
                     native_jsonl_bytes: int, native_jsonl_sha256: str) -> dict[int, list[dict[str, Any]]]:
    gate = report.get("later_b_gate2")
    probe = report.get("rng_draw_probe")
    _require(isinstance(gate, dict) and gate.get("component_pass") is True,
             "native Gate2 browser report does not pass its component gate")
    transfer = report.get("browser_report", {}).get("later_b_transfer", {})
    terminal = transfer.get("native_terminal", {})
    receipt = terminal.get("receipt", {})
    state = receipt.get("matched_setup_cf", {})
    _require(terminal.get("kind") == "retired" and receipt.get("native_retired") is True and
             receipt.get("later_b_transfer", {}).get("token") == gate.get("token_id") and
             isinstance(gate.get("token_id"), str) and
             receipt.get("later_b_transfer", {}).get("parent_close_checked") is True and
             state.get("schema") == "melee-web-stadium-later-b-gate2-native-v1" and
             state.get("cursor") == 360 and state.get("complete") is True,
             "native Gate2 report lacks its correlated checked retirement receipt")
    actual_go = state.get("actual_go")
    _require(isinstance(actual_go, dict) and actual_go.get("observed") is True and
             actual_go.get("tick") == go["go_tick"],
             "native Gate2 report does not confirm the projection's actual GO tick")
    _require(gate.get("accepted_actions") == 360 and
             report.get("result") == "later_b_gate2_component_pass_replay_incomplete",
             "native report is not the bounded accepted Gate2 component result")
    _require(isinstance(probe, dict) and probe.get("complete") is True and
             probe.get("projection_sha256") == projection_sha,
             "native RNG probe is incomplete or bound to another projection")
    config = probe.get("configuration")
    _require(isinstance(config, dict) and config.get("go_tick") == go["go_tick"] and
             config.get("prefix_cursor") == go["prefix_cursor"] and
             config.get("first_tick") == go["go_tick"] + 1 and
             config.get("last_tick") == go["go_tick"] + 3,
             "native RNG probe requested a different GO-relative interval")
    _require(probe.get("coordinates") == coordinates,
             "native RNG report coordinates differ from the validated projection")
    requested_cursors = [go["prefix_cursor"] + go["go_tick"] + i for i in offsets]
    request = probe.get("request")
    _require(isinstance(request, dict) and request.get("kind") == "cursors" and
             request.get("cursors") == requested_cursors and
             probe.get("selected_cursors") == requested_cursors and
             probe.get("captured_cursors") == requested_cursors and
             probe.get("missing_cursors") == [],
             "native RNG report selected/missed an unexpected cursor")
    artifact = probe.get("artifact")
    _require(isinstance(artifact, dict) and artifact.get("name") == "rng-draw-probe.jsonl" and
             artifact.get("bytes") == native_jsonl_bytes and
             artifact.get("sha256") == native_jsonl_sha256,
             "native RNG report has no exact JSONL artifact identity")
    _require(len(rows) == len(requested_cursors),
             "native RNG JSONL must retain every selected row, including empty rows")
    result: dict[int, list[dict[str, Any]]] = {}
    for index, (row, tick) in enumerate(zip(rows, (go["go_tick"] + i for i in offsets))):
        expected_cursor = go["prefix_cursor"] + tick
        _require(row.get("schema") == NATIVE_SCHEMA and row.get("version") == 2 and
                 row.get("source_cursor") == expected_cursor and row.get("overflowed") is False and
                 isinstance(row.get("draws"), list),
                 f"native RNG row identity/cursor is invalid at selected tick {tick}")
        selected: list[dict[str, Any]] = []
        for draw_index, draw in enumerate(row["draws"]):
            _require(isinstance(draw, dict) and draw.get("kind") in {"HSD_Rand", "HSD_Randf"} and
                     isinstance(draw.get("seed_after"), str) and SEED_HEX.fullmatch(draw["seed_after"]),
                     f"native RNG draw {draw_index} at tick {tick} is malformed")
            selected.append({"kind": draw["kind"], "seed_after": draw["seed_after"],
                             "owner": _native_owner(draw.get("owner"), f"native draw {draw_index} at tick {tick}"),
                             "owner_raw_pointers": _raw_owner_pointers(
                                 draw.get("owner"), f"native draw {draw_index} at tick {tick}", 16)})
        result[tick] = selected
    return result


def compare_event_lists(original: list[dict[str, Any]], native: list[dict[str, Any]],
                        tick: int) -> list[dict[str, Any]]:
    _require(len(original) == len(native),
             f"tick {tick}: RNG event count differs (original {len(original)}, native {len(native)})")
    compared: list[dict[str, Any]] = []
    for ordinal, (left, right) in enumerate(zip(original, native)):
        for field in ("kind", "seed_after", "owner"):
            _require(left[field] == right[field],
                     f"tick {tick} event {ordinal}: {field} differs")
        compared.append({"ordinal": ordinal, "kind": left["kind"],
                         "seed_after": left["seed_after"], "owner": left["owner"],
                         "original_rng_ordinal": left["ordinal"],
                         "original_observer_next_sequence": left["observer_next_sequence"],
                         "original_sequence_phase": left["phase"],
                         "original_source_tick_diagnostic_only": left["source_tick"],
                         "original_draw_ordinal": left["draw_ordinal"],
                         "original_seed_owner_pointer_provenance": left["raw_owner_pointer"],
                         "original_global_seed_pointer_snapshot_provenance": left["raw_global_seed_pointer_snapshot"],
                         "original_owner_pointers_local_provenance": left["owner_raw_pointers"],
                         "native_owner_pointers_local_provenance": right["owner_raw_pointers"]})
        if "raw_return_site" in left:
            compared[-1]["original_raw_return_site_provenance"] = dict(left["raw_return_site"])
    return compared


def assign_original_events(original: list[dict[str, Any]], intervals: list[dict[str, Any]],
                           go_sequence: int) -> tuple[dict[int, list[dict[str, Any]]], dict[int, int]]:
    """Assign returns by next-observer-sequence bracket, never by source_tick."""
    buckets: dict[int, list[dict[str, Any]]] = {row["source_tick"]: [] for row in intervals}
    guard_counts = {0: 0, 4: 0}
    for event in original:
        sequence = event["observer_next_sequence"]
        _require(sequence > go_sequence,
                 f"original RNG event {event['ordinal']} predates actual GO")
        candidates = [row for row in intervals
                      if row["consume_sequence"] < sequence <= row["draw_return_sequence"]]
        if len(candidates) != 1:
            raise EvidenceError(f"original RNG event {event['ordinal']} maps to {len(candidates)} source intervals")
        row = candidates[0]
        _require(event["draw_ordinal"] == row["draw_ordinal"],
                 f"original RNG event {event['ordinal']} draw ordinal disagrees with sequence interval")
        if sequence <= row["tick_return_sequence"]:
            phase = "partial_tick_after_go" if row["offset"] == 0 else "tick"
        elif sequence <= row["draw_enter_sequence"]:
            phase = "tick_to_draw_gap"
        else:
            phase = "draw"
        if row["offset"] == 0:
            _require(sequence > go_sequence,
                     f"partial GO guard event {event['ordinal']} predates actual GO")
        event["phase"] = phase
        buckets[row["source_tick"]].append(event)
        if row["offset"] in guard_counts:
            guard_counts[row["offset"]] += 1
    return buckets, guard_counts


def _validate_dynamic_launch_request(environment_delta, probe, sidecar_path, go):
    # This comparator admits only the explicit whole3 B-state adjunct protocol.
    # Its launch fields are requested GO-relative offsets, not absolute ticks.
    # No generic/absolute protocol is inferred from numeric values.
    _require(environment_delta.get("MWRC_STADIUM_WHOLE_RNG_RETURNS") == "1" and
             environment_delta.get("MWRC_STADIUM_WHOLE_B_STATE") == "1" and
             environment_delta.get("MWRC_WHOLE_SESSION_MATCHES") == "3",
             "same-run launch is not the explicit whole3 B-state dynamic RNG protocol")
    _require(environment_delta.get("MWRC_CPU_PROBE_RNG_RETURN_SITE") == probe.get("rng_return_site") and
             environment_delta.get("MWRC_CPU_PROBE_MATCH") == "1" and
             environment_delta.get("MWRC_CPU_PROBE_FIRST_TICK") == "1" and
             environment_delta.get("MWRC_CPU_PROBE_LAST_TICK") == "3" and
             Path(environment_delta.get("MWRC_CPU_PROBE_OUTPUT", "")).resolve() == sidecar_path.resolve(),
             "same-run launch does not bind exact relative RNG offsets/output")
    _require(probe.get("window_basis") == "actual_go_raw_guard_band" and
             probe.get("requested_first_offset") == 1 and probe.get("requested_last_offset") == 3 and
             probe.get("go_tick") == go["go_tick"] and
             probe.get("go_observer_sequence") == go["go_sequence"] and
             probe.get("first_tick") == go["go_tick"] and probe.get("last_tick") == go["go_tick"] + 4,
             "dynamic RNG request does not join the actual GO and raw guard bounds")


def compare_inputs(projection: dict[str, Any], raw_records: list[dict[str, Any]],
                   original_probe: dict[str, Any], original_status: dict[str, Any],
                   primary_report: dict[str, Any], browser_report: dict[str, Any],
                   native_rows: list[dict[str, Any]], hashes: dict[str, str],
                   sizes: dict[str, int], binding: dict[str, Any],
                   binding_sha256: str, projected_from_binding: dict[str, Any],
                   rng_validator: Any, sidecar_path: Path,
                   launch: dict[str, Any]) -> dict[str, Any]:
    _require(projection.get("binding_sha256") == binding_sha256,
             "projection was not generated from this exact same-run operand binding")
    _require(_canonical(projection) == _canonical(projected_from_binding),
             "pinned projection differs from the existing strict same-binding projection decoder")
    _require(projection.get("same_run_provenance_verified") is False,
             "projection's historical provenance flag was altered; it must remain false")
    native = primary_report.get("native")
    _require(isinstance(native, dict), "original report has no primary native identity")
    primary_identity = native.get("primary_identity")
    _require(isinstance(primary_identity, dict) and native.get("primary_sha256") == hashes["original_primary_mwro"],
             "original report does not bind the pinned primary MWRO")
    _require(primary_report.get("result") not in {"failed", "FAIL"},
             "original primary capture report is marked failed")
    _require(binding.get("files", {}).get("primary", {}).get("sha256") == hashes["original_primary_mwro"] and
             binding.get("files", {}).get("primary_status", {}).get("sha256") == hashes["original_status"] and
             binding.get("files", {}).get("report", {}).get("sha256") == hashes["original_report"],
             "original primary/status/report are not the same strict-binding producer")
    _require(projection.get("recipe_sha256") == browser_report.get("inputs", {}).get("recipe", {}).get("sha256"),
             "Gate2 projection and browser report recipe identities differ")
    projection_input = browser_report.get("inputs", {}).get("later_b_gate2_projection")
    _require(isinstance(projection_input, dict) and
             projection_input.get("sha256") == hashes["projection"] and
             projection_input.get("bytes") == sizes["projection"],
             "browser report did not consume this exact projection")
    by_seq: dict[int, dict[str, Any]] = {}
    for record in raw_records:
        seq = record.get("seq")
        _require(type(seq) is int and seq not in by_seq, "primary MWRO has duplicate/noninteger sequence")
        by_seq[seq] = record
    status = original_status
    _require(status.get("event_count") == len(raw_records) and status.get("last_seq") == max(by_seq, default=-1),
             "primary MWRO count/last sequence differs from its status")
    intervals, go = derive_intervals(projection, by_seq)
    environment_delta = launch.get("environment_delta")
    _require(isinstance(environment_delta, dict), "same-run launch has no observer environment identity")
    probe_identity = {
        "capture_id": environment_delta.get("MWRC_CAPTURE_ID"),
        "sequence_id": environment_delta.get("MWRC_SEQUENCE_ID"),
        "dol_sha256": environment_delta.get("MWRC_DOL_SHA256"),
    }
    _require(probe_identity["capture_id"] and probe_identity["sequence_id"] and
             probe_identity["dol_sha256"] == primary_identity.get("dol_sha256") and
             primary_identity.get("capture_id") == probe_identity["capture_id"] and
             primary_identity.get("sequence_id") == probe_identity["sequence_id"],
             "sidecar launch identity, primary MWRO report, and DOL identity differ")
    raw_first, raw_last = go["go_tick"], go["go_tick"] + 4
    _original_rng_sites(original_probe)
    coverage = original_probe["rng_return_site"]
    _validate_dynamic_launch_request(environment_delta, original_probe, sidecar_path, go)
    request = {"site": coverage, "match": 1,
               "first_tick": raw_first, "last_tick": raw_last}
    try:
        original_validation = rng_validator.validate_cpu_rng_probe(
            sidecar_path, request, probe_identity["capture_id"],
            probe_identity["sequence_id"], probe_identity["dol_sha256"], status)
    except Exception as exc:
        raise EvidenceError(f"existing strict original combined RNG validator rejected sidecar: {exc}") from exc
    original = _validate_original_probe(original_probe, original_validation, status,
                                        probe_identity, intervals, go,
                                        hashes["original_rng_probe"])
    # A sidecar record is assigned solely by the next MWRO sequence number.
    # source_tick and draw_ordinal remain consistency/provenance fields.
    buckets, guard_counts = assign_original_events(original, intervals, go["go_sequence"])
    selected_offsets = (1, 2, 3)
    expected_coords = []
    for row in intervals:
        if row["selected"]:
            expected_coords.append({
                "source_tick": row["source_tick"],
                "recipe_cursor": go["prefix_cursor"] + row["source_tick"],
                "tick_action_index": next(a["action_index"] for a in projection["actions"]
                                           if a.get("kind") == "tick" and a.get("source_tick") == row["source_tick"]),
                "draw_action_index": next(a["action_index"] for a in projection["actions"]
                                           if a.get("kind") == "draw" and a.get("source_tick") == row["source_tick"] + 1),
                "consume_sequence": row["consume_sequence"],
                "tick_return_sequence": row["tick_return_sequence"],
                "draw_enter_sequence": row["draw_enter_sequence"],
                "draw_return_sequence": row["draw_return_sequence"],
            })
    native_by_tick = _validate_native(browser_report, native_rows, hashes["projection"],
                                      expected_coords, go, selected_offsets,
                                      sizes["native_rng_jsonl"], hashes["native_rng_jsonl"])
    matches = []
    for offset in selected_offsets:
        tick = go["go_tick"] + offset
        matches.append({"go_offset": offset, "source_tick": tick,
                        "original_event_count": len(buckets[tick]),
                        "native_event_count": len(native_by_tick[tick]),
                        "events": compare_event_lists(buckets[tick], native_by_tick[tick], tick)})
    return {
        "schema": SCHEMA,
        "result": "PASS_DECLARED_RNG_OWNER_WINDOW_1_3",
        "scope": "Three post-actual-GO tick/draw intervals only; all-writer coverage and full C3 remain false.",
        "C3": False,
        "all_rng_writers_observed": False,
        "actual_go": {"source_tick": go["go_tick"], "observer_sequence": go["go_sequence"]},
        "same_run_binding_sha256": binding_sha256,
        "projection_same_run_provenance_verified": False,
        "requested_offsets": {"first": 1, "last": 3},
        "raw_guard_band": {"first_source_tick": go["go_tick"],
                            "last_source_tick": go["go_tick"] + 4,
                            "first_guard_tick_partial": True,
                            "offset0_original_rng_records": guard_counts[0],
                            "offset4_original_rng_records": guard_counts[4]},
        "selected_empty_native_windows": [item["go_offset"] for item in matches
                                           if item["native_event_count"] == 0],
        "matched_ticks": matches,
        "input_sha256": hashes,
        "input_bytes": sizes,
    }


def run(manifest_path: Path) -> dict[str, Any]:
    manifest_bytes = manifest_path.read_bytes()
    manifest, pinned = load_pinned_inputs(manifest_path)
    _require(hashlib.sha256(manifest_bytes).hexdigest() == _sha256(manifest_path),
             "input manifest changed while its roles were being pinned")
    source_root = verify_source_checkout(manifest)
    obj = {name: _parse_json(pinned[name][1], name) for name in (
        "original_binding", "original_report", "original_status", "original_rng_probe",
        "projection", "browser_report")}
    projected_from_binding, rng_validator, launch = validate_original_binding(
        pinned["original_binding"][0], obj["original_binding"], pinned, source_root)
    _require(_canonical(obj["projection"]) == _canonical(projected_from_binding),
             "browser projection file is not the exact existing strict projection for the fresh binding")
    consumed_projection = obj["browser_report"].get("inputs", {}).get("later_b_gate2_projection")
    _require(isinstance(consumed_projection, dict) and
             Path(consumed_projection.get("path", "")).resolve() == pinned["projection"][0].resolve() and
             consumed_projection.get("bytes") == len(pinned["projection"][1]) and
             consumed_projection.get("sha256") == pinned["projection"][2],
             "browser report projection input path/bytes/hash differs from the pinned projection")
    _require(pinned["observer_decoder"][0].resolve() ==
             (source_root / "reference-capture" / "dolphin" / "reference_observer_stream.py").resolve(),
             "pinned decoder is not the decoder bound to this source revision")
    decoder = _load_decoder(pinned["observer_decoder"][0])
    hashes = {name: values[2] for name, values in pinned.items()}
    sizes = {name: len(values[1]) for name, values in pinned.items()}
    try:
        raw_records = list(decoder.iter_records(pinned["original_primary_mwro"][0]))
    except Exception as exc:
        raise EvidenceError(f"pinned primary MWRO failed strict decoding: {exc}") from exc
    _require(_sha256(pinned["original_primary_mwro"][0]) == hashes["original_primary_mwro"],
             "primary MWRO changed while it was being decoded")
    _require(pinned["native_rng_jsonl"][0].resolve() ==
             (pinned["browser_report"][0].parent / "rng-draw-probe.jsonl").resolve(),
             "native RNG JSONL is not the artifact adjacent to its pinned browser report")
    native_rows = _load_jsonl(pinned["native_rng_jsonl"][1], "native RNG JSONL")
    result = compare_inputs(
        projection=obj["projection"], raw_records=raw_records,
        original_probe=obj["original_rng_probe"],
        original_status=obj["original_status"], primary_report=obj["original_report"],
        browser_report=obj["browser_report"],
        native_rows=native_rows, hashes=hashes, sizes=sizes,
        binding=obj["original_binding"], binding_sha256=hashes["original_binding"],
        projected_from_binding=projected_from_binding, rng_validator=rng_validator,
        sidecar_path=pinned["original_rng_probe"][0], launch=launch)
    result["input_manifest_sha256"] = hashlib.sha256(manifest_bytes).hexdigest()
    result["projection_builder_source_head"] = manifest["source_checkout"]["head"]
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args(argv)
    try:
        result = run(args.manifest.resolve())
        code = 0
    except Exception as exc:
        result = {"schema": SCHEMA, "result": "REFUSE_RNG_OWNER_WINDOW",
                  "first_failure": str(exc), "C3": False,
                  "all_rng_writers_observed": False}
        code = 2
    args.output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with args.output.open("x", encoding="utf-8") as stream:
            json.dump(result, stream, sort_keys=True, indent=2)
            stream.write("\n")
    except FileExistsError:
        print(f"refusing to overwrite reducer output: {args.output}", file=sys.stderr)
        return 3
    print(json.dumps({"result": result["result"],
                      "first_failure": result.get("first_failure"),
                      "output": str(args.output)}))
    return code


if __name__ == "__main__":
    raise SystemExit(main())
