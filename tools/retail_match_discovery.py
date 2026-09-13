"""Validate a source-driven retail match-length discovery artifact.

Discovery output is deliberately a different schema from a replay candidate.
It records the bounded source prefix and the original exit/final-draw
observation, but it is never accepted by ``retail_replay_validation`` or by
the replay recipe tools.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

from retail_replay_validation import (
    CaptureError,
    DEQUEUED_INPUT_PHASE,
    FRAME_KEYS,
    GAME_REVISION,
    INITIAL_PHASE,
    MAX_FRAMES,
    PHASE,
    STATE_KEYS,
    _hex,
    _int,
    _pad_state,
    _reject_duplicate_keys,
    _require_keys,
    _u32,
    _validate_inputs,
    _validate_provenance,
    _validate_scene_continuity,
    _validate_state,
)
from retail_input_plan import verify_entry, verify_tick


SCHEMA = "melee-web-retail-match-discovery"
VERSION = 1
EXIT_PHASE = "gm_801A4B60_return"
FINAL_EXIT_CALLER = 0x8016D884
HEADER_KEYS = {
    "record", "schema", "version", "capture_id", "phase", "input_phase",
    "initial_phase", "game_revision", "frames_cap", "input_plan_frames",
    "input_plan_sha256", "provenance", "collector_sha256", "writes_game_state",
}
MATCH_ENTER_DISCOVERY_KEYS = {"record", "rng", "start_melee_hex", "pad_state_hex"}
FRAME_DISCOVERY_KEYS = FRAME_KEYS | {"pad_state_hex"}
END_KEYS = {
    "record", "frames", "stop_reason", "status", "scene_request",
    "match_end_state", "match_result", "final_draw_source_index",
    "exit_observation",
}
EXIT_KEYS = {"phase", "frame_index", "source_scene_frame", "caller", "scene_request"}


class DiscoveryError(CaptureError):
    """A discovery artifact is malformed, incomplete, or not source-complete."""


def _checked(function, *args, **kwargs):
    """Translate shared candidate-validator errors to the discovery type."""
    try:
        return function(*args, **kwargs)
    except DiscoveryError:
        raise
    except CaptureError as error:
        raise DiscoveryError(str(error)) from error


class Discovery:
    __slots__ = ("rows", "header", "match_enter", "initial", "frames", "end",
                 "sha256", "raw", "complete", "report")

    def __init__(self, rows: tuple[dict[str, Any], ...], header: dict[str, Any],
                 match_enter: dict[str, Any], initial: dict[str, Any],
                 frames: tuple[dict[str, Any], ...], end: dict[str, Any],
                 sha256: str | None, raw: bytes | None, complete: bool,
                 report: dict[str, Any]):
        self.rows = rows
        self.header = header
        self.match_enter = match_enter
        self.initial = initial
        self.frames = frames
        self.end = end
        self.sha256 = sha256
        self.raw = raw
        self.complete = complete
        self.report = report


def _validate_capture_id(value: Any) -> str:
    capture_id = _hex(value, 16, "discovery.header.capture_id")
    if capture_id[12] != "4" or capture_id[16] not in "89ab":
        raise DiscoveryError("discovery.header.capture_id: expected a UUID4 hex value")
    return capture_id


def _validate_exit(value: Any, frame_count: int) -> dict[str, Any] | None:
    if value is None:
        return None
    context = "discovery.end.exit_observation"
    if not isinstance(value, dict):
        raise DiscoveryError(f"{context}: expected object or null")
    _require_keys(value, EXIT_KEYS, context)
    if value["phase"] != EXIT_PHASE or type(value["phase"]) is not str:
        raise DiscoveryError(f"{context}.phase: expected {EXIT_PHASE!r}")
    # This is the raw scheduler frame count at the callback.  In the normal
    # pre-final-scheduler order it is the final source index; if the callback
    # runs after the final draw it is one past the source index.  Keeping this
    # raw value preserves ordering evidence rather than rewriting it.
    _int(value["frame_index"], f"{context}.frame_index", minimum=0,
         maximum=frame_count)
    _u32(value["source_scene_frame"], f"{context}.source_scene_frame")
    _u32(value["caller"], f"{context}.caller")
    _u32(value["scene_request"], f"{context}.scene_request")
    return dict(value)


def _stocks_show_elimination(capture_frames: tuple[dict[str, Any], ...]) -> bool:
    try:
        stocks = [fighter["stocks"] for fighter in capture_frames[-1]["fighters"]]
    except (IndexError, KeyError, TypeError):
        return False
    return (len(stocks) == 2 and all(type(stock) is int for stock in stocks)
            and stocks.count(0) == 1 and any(stock > 0 for stock in stocks))


def _validate_plan_prefix(plan: dict[str, Any], capture: Discovery) -> None:
    try:
        verify_entry(plan, capture.match_enter["start_melee_hex"])
        if len(capture.frames) > len(plan["frames"]):
            raise ValueError("discovery consumed more frames than the input plan")
        for index, frame in enumerate(capture.frames):
            if len(frame["consumed_inputs"]) != 1:
                raise ValueError("discovery requires one consumed vector per tick")
            verify_tick(plan, index, frame["consumed_inputs"][0])
    except (KeyError, IndexError, TypeError, ValueError) as error:
        raise DiscoveryError(f"discovery input plan prefix mismatch: {error}") from error


def _validate_rows(rows: Iterable[dict[str, Any]], context: str,
                  *, cpu: str = "Interpreter64", plan: dict[str, Any] | None = None,
                  plan_sha256: str | None = None, sha256: str | None,
                  raw: bytes | None) -> Discovery:
    if not isinstance(rows, (list, tuple)):
        raise DiscoveryError(f"{context}: discovery rows must be a list")
    rows_tuple = tuple(rows)
    if not rows_tuple or any(not isinstance(row, dict) for row in rows_tuple):
        raise DiscoveryError(f"{context}: every discovery record must be an object")
    if any(row.get("record") == "error" for row in rows_tuple):
        error = next(row.get("error", "unknown error") for row in rows_tuple
                     if row.get("record") == "error")
        raise DiscoveryError(f"{context}: collector error: {error}")

    header = rows_tuple[0]
    if header.get("record") != "header":
        raise DiscoveryError(f"{context}: first record must be header")
    _require_keys(header, HEADER_KEYS, f"{context}.header")
    if header["schema"] != SCHEMA or type(header["version"]) is not int \
            or header["version"] != VERSION:
        raise DiscoveryError(f"{context}.header: unsupported discovery schema or version")
    if header["phase"] != PHASE or header["input_phase"] != DEQUEUED_INPUT_PHASE \
            or header["initial_phase"] != INITIAL_PHASE \
            or header["game_revision"] != GAME_REVISION:
        raise DiscoveryError(f"{context}.header: unsupported source boundary")
    _checked(_validate_capture_id, header["capture_id"])
    cap = _int(header["frames_cap"], f"{context}.header.frames_cap",
               minimum=1, maximum=MAX_FRAMES)
    plan_frames = _int(header["input_plan_frames"],
                       f"{context}.header.input_plan_frames",
                       minimum=cap, maximum=MAX_FRAMES)
    plan_hash = _hex(header["input_plan_sha256"], 32,
                     f"{context}.header.input_plan_sha256")
    _checked(_validate_provenance, header["provenance"],
             f"{context}.header.provenance", cpu=cpu)
    if header["provenance"].get("input_plan_sha256") != plan_hash:
        raise DiscoveryError("discovery.header.provenance.input_plan_sha256: does not match header")
    _hex(header["collector_sha256"], 32, f"{context}.header.collector_sha256")
    if header["writes_game_state"] is not False:
        raise DiscoveryError(f"{context}.header.writes_game_state: must be false")
    if plan is not None:
        if type(plan) is not dict or len(plan.get("frames", [])) != plan_frames:
            raise DiscoveryError("discovery input plan length does not match header")
        if plan_sha256 is not None and plan_hash != plan_sha256.lower():
            raise DiscoveryError("discovery input plan hash does not match supplied plan")

    if len(rows_tuple) < 5:
        raise DiscoveryError(f"{context}: discovery has no complete record sequence")
    match_enter = rows_tuple[1]
    if match_enter.get("record") != "match_enter":
        raise DiscoveryError(f"{context}: record 1 must be match_enter")
    _require_keys(match_enter, MATCH_ENTER_DISCOVERY_KEYS, f"{context}.match_enter")
    _u32(match_enter["rng"], f"{context}.match_enter.rng")
    _checked(_hex, match_enter["start_melee_hex"], 0x138,
             f"{context}.match_enter.start_melee_hex")
    _checked(_pad_state, match_enter["pad_state_hex"],
             f"{context}.match_enter.pad_state_hex")

    initial = rows_tuple[2]
    if initial.get("record") != "match_enter_complete":
        raise DiscoveryError(f"{context}: record 2 must be match_enter_complete")
    _require_keys(initial, {"record", *STATE_KEYS, "pad_state_hex"},
                  f"{context}.match_enter_complete")
    _checked(_validate_state,
             {key: initial.get(key) for key in STATE_KEYS | {"pad_state_hex"}},
             f"{context}.match_enter_complete", 2)

    end = rows_tuple[-1]
    if end.get("record") != "end":
        raise DiscoveryError(f"{context}: final record must be end")
    _require_keys(end, END_KEYS, f"{context}.end")
    frame_count = _int(end["frames"], f"{context}.end.frames", minimum=1, maximum=cap)
    frame_rows = rows_tuple[3:-1]
    if len(frame_rows) != frame_count:
        raise DiscoveryError(
            f"{context}: end.frames {frame_count} does not match {len(frame_rows)} frame rows")
    frames: list[dict[str, Any]] = []
    for expected_index, row in enumerate(frame_rows):
        row_context = f"{context}.frame[{expected_index}]"
        if row.get("record") != "frame":
            raise DiscoveryError(f"{row_context}: expected frame record")
        _require_keys(row, FRAME_DISCOVERY_KEYS, row_context)
        if _int(row["index"], f"{row_context}.index", minimum=0) != expected_index:
            raise DiscoveryError(f"{row_context}.index: noncontiguous frame index")
        _checked(_validate_inputs, row["consumed_inputs"],
                 f"{row_context}.consumed_inputs")
        _checked(_validate_state,
                 {key: row.get(key) for key in STATE_KEYS | {"pad_state_hex"}},
                 row_context, 2)
        frames.append(row)
    try:
        _validate_scene_continuity(frames, context)
    except CaptureError as error:
        raise DiscoveryError(str(error)) from error

    stop_reason = end["stop_reason"]
    status = end["status"]
    if stop_reason not in {"match_end", "frame_cap"}:
        raise DiscoveryError(f"{context}.end.stop_reason: unsupported reason")
    expected_status = {"match_end": "complete", "frame_cap": "cap_exhausted"}[stop_reason]
    if status != expected_status:
        raise DiscoveryError(f"{context}.end.status: expected {expected_status!r}")
    if stop_reason == "frame_cap" and frame_count != cap:
        raise DiscoveryError(f"{context}.end.frames: cap exhaustion must reach frames_cap")
    _u32(end["scene_request"], f"{context}.end.scene_request")
    _int(end["match_end_state"], f"{context}.end.match_end_state", minimum=0, maximum=0xFF)
    _int(end["match_result"], f"{context}.end.match_result", minimum=0, maximum=0xFF)
    final_draw = _int(end["final_draw_source_index"],
                      f"{context}.end.final_draw_source_index",
                      minimum=0, maximum=frame_count - 1)
    if final_draw != frame_count - 1:
        raise DiscoveryError(f"{context}.end.final_draw_source_index: final source draw required")
    exit_observation = _checked(_validate_exit, end["exit_observation"], frame_count)

    source_complete = (
        stop_reason == "match_end" and status == "complete"
        and end["scene_request"] == 1 and end["match_end_state"] == 3
        and end["match_result"] == 2 and exit_observation is not None
        and exit_observation["caller"] == FINAL_EXIT_CALLER
        and exit_observation["scene_request"] == 1
        and exit_observation["source_scene_frame"] == frame_count - 1
        and exit_observation["frame_index"] in (frame_count - 1, frame_count)
        and _stocks_show_elimination(tuple(frames)))
    if stop_reason == "match_end" and not source_complete:
        raise DiscoveryError(f"{context}.end: match_end lacks complete source evidence")
    if plan is not None:
        capture = Discovery(rows_tuple, header, match_enter, initial, tuple(frames),
                            end, sha256, raw, source_complete, {})
        _validate_plan_prefix(plan, capture)
    # The source boundary is meaningful only when the caller also supplies the
    # immutable plan file and its byte hash.  A structural match-end artifact
    # without those arguments remains useful diagnostics, but cannot be a
    # successful discovery report.
    input_plan_verified = plan is not None and plan_sha256 is not None
    complete = source_complete and input_plan_verified

    report = {
        "schema": SCHEMA,
        "version": VERSION,
        "status": ("source_match_complete" if complete else
                    ("cap_exhausted" if stop_reason == "frame_cap"
                     else "unverified_match_end")),
        "complete": complete,
        "frames_cap": cap,
        "frames_actual": frame_count,
        "input_plan_frames": plan_frames,
        "input_plan_sha256": plan_hash,
        "collector_sha256": header["collector_sha256"].lower(),
        "discovery_sha256": sha256,
        "input_plan_verified": input_plan_verified,
        "stop_reason": stop_reason,
        "scene_request": end["scene_request"],
        "match_end_state": end["match_end_state"],
        "match_result": end["match_result"],
        "final_draw_source_index": final_draw,
        "exit_observation": exit_observation,
        "gold_admitted": False,
        "scope": (
            "source retail match-length discovery only: exact four-port input prefix, "
            "source scheduler continuity, original exit callback and final source draw; "
            "cap exhaustion is incomplete; no port equivalence, rendering, performance, "
            "or reference/gold admission claim"
        ),
    }
    return Discovery(rows_tuple, header, match_enter, initial, tuple(frames), end,
                     sha256, raw, complete, report)


def load_discovery(path: str | Path, *, cpu: str = "Interpreter64",
                   plan: dict[str, Any] | None = None,
                   plan_sha256: str | None = None) -> Discovery:
    source = Path(path)
    try:
        raw = source.read_bytes()
    except OSError as error:
        raise DiscoveryError(f"{source}: cannot read discovery: {error}") from error
    if not raw:
        raise DiscoveryError(f"{source}: discovery is empty")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise DiscoveryError(f"{source}: discovery is not UTF-8: {error}") from error
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            raise DiscoveryError(f"{source}:{line_number}: blank JSONL line")
        try:
            row = json.loads(line, object_pairs_hook=_reject_duplicate_keys,
                             parse_constant=lambda constant: (_ for _ in ()).throw(
                                 ValueError(f"invalid JSON constant {constant}")))
        except (ValueError, TypeError) as error:
            raise DiscoveryError(f"{source}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise DiscoveryError(f"{source}:{line_number}: each record must be an object")
        rows.append(row)
    if not rows:
        raise DiscoveryError(f"{source}: discovery is empty")
    return _validate_rows(rows, "discovery", cpu=cpu, plan=plan,
                          plan_sha256=plan_sha256,
                          sha256=hashlib.sha256(raw).hexdigest(), raw=raw)


def validate_discovery(rows: list[dict[str, Any]] | tuple[dict[str, Any], ...],
                       *, cpu: str = "Interpreter64",
                       plan: dict[str, Any] | None = None,
                       plan_sha256: str | None = None) -> Discovery:
    """Validate decoded rows without rewriting them or inventing a hash."""
    raw = None  # In-memory callers have no immutable artifact bytes to hash.
    return _validate_rows(rows, "discovery", cpu=cpu, plan=plan,
                          plan_sha256=plan_sha256,
                          sha256=None, raw=raw)
