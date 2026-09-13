"""Diagnose an incomplete port replay against a verified retail prefix.

This module is deliberately separate from :mod:`port_replay_validation`.
The strict validator requires a successful end/teardown record and remains the
only path that can produce a complete port comparison.  This diagnostic path
accepts a prefix that stopped before that record so a crash can be located;
the result is never a port-equivalence or gold/admission result.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from port_replay_validation import (
    HEADER,
    STATE,
    _DuplicateKey,
    _first_difference,
    _hex,
    _int,
    _reject_duplicate_keys,
    _require_keys,
    _state,
    _u32,
    _validate_inputs,
)
from retail_replay_recipe import RecipeError, _pair
from retail_replay_validation import CaptureError, MAX_FRAMES, _lower_hex_strings


MAX_CAPTURE_BYTES = 128 * 1024 * 1024
SCOPE = (
    "incomplete port prefix diagnostic against a verified independent retail "
    "reference pair; observed inputs, PAD state where present, fighter fields, "
    "RNG and match clock only; no completion, performance, rendering, or gold "
    "admission claim"
)


class _PortPrefix:
    __slots__ = ("rows", "header", "match_enter", "initial", "frames", "sha256")

    def __init__(self, rows: tuple[dict[str, Any], ...], header: dict[str, Any],
                 match_enter: dict[str, Any], initial: dict[str, Any],
                 frames: tuple[dict[str, Any], ...], sha256: str | None):
        self.rows = rows
        self.header = header
        self.match_enter = match_enter
        self.initial = initial
        self.frames = frames
        self.sha256 = sha256


def _read_port(path: str | Path) -> tuple[list[dict[str, Any]], str]:
    source = Path(path)
    try:
        size = source.stat().st_size
    except OSError as error:
        raise CaptureError(f"{source}: cannot read port capture: {error}") from error
    if size > MAX_CAPTURE_BYTES:
        raise CaptureError(f"{source}: port capture exceeds byte limit")
    try:
        # Read one sentinel byte beyond the accepted limit so a file that grew
        # after stat() is rejected without materializing an unbounded capture.
        with source.open("rb") as stream:
            raw = stream.read(MAX_CAPTURE_BYTES + 1)
    except OSError as error:
        raise CaptureError(f"{source}: cannot read port capture: {error}") from error
    if len(raw) > MAX_CAPTURE_BYTES:
        raise CaptureError(f"{source}: port capture exceeds byte limit")
    if not raw:
        raise CaptureError(f"{source}: port capture is empty")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CaptureError(f"{source}: port capture is not UTF-8: {error}") from error

    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            raise CaptureError(f"{source}:{line_number}: blank JSONL line")
        try:
            row = json.loads(line, object_pairs_hook=_reject_duplicate_keys)
        except (_DuplicateKey, json.JSONDecodeError) as error:
            raise CaptureError(f"{source}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise CaptureError(f"{source}:{line_number}: each record must be an object")
        rows.append(row)
    if not rows:
        raise CaptureError(f"{source}: port capture is empty")
    return rows, hashlib.sha256(raw).hexdigest()


def _validate_prefix(rows: list[dict[str, Any]] | tuple[dict[str, Any], ...],
                     context: str = "port") -> _PortPrefix:
    """Validate a strict header/entry/initial/frame prefix with no end record."""

    if not isinstance(rows, (list, tuple)) or not rows:
        raise CaptureError(f"{context}: expected JSON object records")
    rows_tuple = tuple(rows)
    if any(not isinstance(row, dict) for row in rows_tuple):
        raise CaptureError(f"{context}: every record must be an object")

    header = rows_tuple[0]
    _require_keys(header, HEADER, f"{context}.header")
    version = _int(header["version"], f"{context}.version", minimum=1, maximum=2)
    fixed = {
        "record": "header",
        "schema": "melee-web-port-replay-candidate",
        "phase": "after_source_tick_before_audio_transport",
        "comparison": "not_run",
    }
    for key, expected in fixed.items():
        if type(header[key]) is not type(expected) or header[key] != expected:
            raise CaptureError(f"{context}.header: unsupported {key}")
    if header["rendering"] not in ("excluded", "source_draws"):
        raise CaptureError(f"{context}.header: unsupported rendering mode")
    requested = _int(header["frames_requested"], f"{context}.frames_requested",
                     minimum=1, maximum=MAX_FRAMES)

    # An end record means the caller supplied a complete capture (or an
    # attempted completion).  It must go through the strict validator so this
    # diagnostic cannot become a second acceptance path.
    if any(row.get("record") == "end" for row in rows_tuple):
        raise CaptureError(
            f"{context}: port capture contains an end record; it is complete or "
            "attempted completion, use port_replay_validation.compare_paths")
    if len(rows_tuple) < 3:
        raise CaptureError(
            f"{context}: incomplete prefix requires header, match_enter, and "
            "match_enter_complete records")

    state_keys = STATE | ({"pad_state_hex"} if version == 2 else set())
    match_keys = {"record", "rng", "start_melee_hex"}
    if version == 2:
        match_keys.add("pad_state_hex")
    _require_keys(rows_tuple[1], match_keys, f"{context}.match_enter")
    if rows_tuple[1]["record"] != "match_enter":
        raise CaptureError(f"{context}: missing match entry")
    _u32(rows_tuple[1]["rng"], f"{context}.entry.rng")
    _hex(rows_tuple[1]["start_melee_hex"], 0x138, f"{context}.entry.setup")
    if version == 2:
        from retail_replay_validation import _pad_state
        _pad_state(rows_tuple[1]["pad_state_hex"], f"{context}.entry.pad_state_hex")

    initial = rows_tuple[2]
    _require_keys(initial, state_keys | {"record"}, f"{context}.initial")
    if initial["record"] != "match_enter_complete":
        raise CaptureError(f"{context}: missing initial state")
    _state(initial, f"{context}.initial", version)

    if len(rows_tuple) - 3 > requested:
        raise CaptureError(
            f"{context}: observed frame prefix exceeds frames_requested")
    frames: list[dict[str, Any]] = []
    previous_match = initial["match_frame"]
    for index, row in enumerate(rows_tuple[3:]):
        row_context = f"{context}.frame[{index}]"
        _require_keys(row, state_keys | {"record", "index", "supplied_inputs"}, row_context)
        if row["record"] != "frame":
            raise CaptureError(
                f"{context}: unexpected record {row.get('record')!r} after frame prefix")
        if _int(row["index"], f"{row_context}.index", minimum=0) != index:
            raise CaptureError(f"{context}: noncontiguous frame sequence")
        _validate_inputs([row["supplied_inputs"]], f"{row_context}.supplied_inputs")
        _state(row, row_context, version)
        if row["match_frame"] < previous_match:
            raise CaptureError(f"{row_context}: match clock moved backwards")
        previous_match = row["match_frame"]
        frames.append(row)
    return _PortPrefix(rows_tuple, header, rows_tuple[1], initial,
                       tuple(frames), None)


def _diagnostic_report(reference, prefix: _PortPrefix,
                       *, reference_a_sha256: str | None = None,
                       reference_b_sha256: str | None = None,
                       port_sha256: str | None = None) -> dict[str, Any]:
    if reference.header["version"] != prefix.header["version"]:
        raise CaptureError("port and reference schema versions must agree")
    version = prefix.header["version"]
    groups = ("rng", "match_frame", "fighters")
    if version == 2:
        groups += ("pad_state_hex",)
    report: dict[str, Any] = {
        "status": "incomplete_capture_diagnostic",
        "gold_admitted": False,
        "performance": "not_evaluated",
        "complete": False,
        "scope": SCOPE,
        "schema": "melee-web-port-replay-candidate",
        "version": version,
        "requested_frames": prefix.header["frames_requested"],
        "observed_frames": len(prefix.frames),
        "frames_compared": len(prefix.frames),
        "reference_frames": len(reference.frames),
        "missing_completion_reason": (
            "port capture ended after the observed prefix without the required "
            "successful end/teardown record"
        ),
        "first_divergence": None,
        "first_divergence_by_group": {},
        "checks": {
            "header": "pass",
            "entry": "pass",
            "inputs": "pass",
            "rng": "pass",
            "match_frame": "pass",
            "fighters": "pass",
            **({"pad_state_hex": "pass"} if version == 2 else {}),
        },
    }
    first: dict[str, dict[str, Any]] = {}

    def check(group: str, expected: Any, actual: Any, record: str,
              frame: int | None = None) -> None:
        difference = _first_difference(
            _lower_hex_strings(expected), _lower_hex_strings(actual))
        if difference is None or group in first:
            return
        field, before, after = difference
        item = {"record": record, "frame": frame, "field": field,
                "expected": before, "actual": after}
        first[group] = item
        if report["first_divergence"] is None:
            report["first_divergence"] = item

    check("header", {"version": reference.header["version"],
                      "frames_requested": len(reference.frames)},
          {"version": prefix.header["version"],
           "frames_requested": prefix.header["frames_requested"]}, "header")
    entry_keys = ("rng", "start_melee_hex") + (("pad_state_hex",) if version == 2 else ())
    check("entry", {key: reference.match_enter[key] for key in entry_keys},
          {key: prefix.match_enter[key] for key in entry_keys}, "match_enter")
    for group in groups:
        check(group, {group: reference.initial[group]},
              {group: prefix.initial[group]}, "match_enter_complete")
    for index, (expected, actual) in enumerate(zip(reference.frames, prefix.frames)):
        check("inputs", expected["consumed_inputs"][0], actual["supplied_inputs"],
              "frame", index)
        for group in groups:
            check(group, {group: expected[group]}, {group: actual[group]},
                  "frame", index)
    for group in ("header", "entry", "inputs", *groups):
        if group in first:
            report["checks"][group] = "diverged"
    report["first_divergence_by_group"] = first
    report["capture_hashes"] = {
        "reference_a": reference_a_sha256,
        "reference_b": reference_b_sha256,
        "port": port_sha256,
    }
    report["reference_repeatability"] = "pass"
    return report


def diagnose_rows(reference, rows: list[dict[str, Any]] | tuple[dict[str, Any], ...],
                  *, reference_a_sha256: str | None = None,
                  reference_b_sha256: str | None = None,
                  port_sha256: str | None = None) -> dict[str, Any]:
    """Diagnose an already verified full reference against an incomplete prefix."""

    prefix = _validate_prefix(rows)
    if len(prefix.frames) > len(reference.frames):
        raise CaptureError("port prefix is longer than the verified reference")
    return _diagnostic_report(
        reference, prefix, reference_a_sha256=reference_a_sha256,
        reference_b_sha256=reference_b_sha256, port_sha256=port_sha256)


def diagnose_paths(reference_a: str | Path, reference_b: str | Path,
                   port: str | Path, *, cpu: str = "Interpreter64") -> dict[str, Any]:
    """Verify the full retail pair, then diagnose the port's observed prefix."""

    report: dict[str, Any] = {
        "status": "invalid_capture",
        "gold_admitted": False,
        "performance": "not_evaluated",
        "scope": SCOPE,
    }
    try:
        _, _, reference_a_hash, reference_b_hash, reference, _, _ = _pair(
            reference_a, reference_b, cpu=cpu)
        rows, port_hash = _read_port(port)
        report = diagnose_rows(
            reference, rows, reference_a_sha256=reference_a_hash,
            reference_b_sha256=reference_b_hash, port_sha256=port_hash)
        report["reference_cpu"] = cpu
    except (CaptureError, RecipeError, OSError, UnicodeError,
            json.JSONDecodeError, _DuplicateKey) as error:
        report["error"] = str(error)
    return report


def status_exit_code(status: str) -> int:
    return {"incomplete_capture_diagnostic": 0, "invalid_capture": 2}.get(status, 2)


__all__ = ["diagnose_paths", "diagnose_rows", "status_exit_code"]
