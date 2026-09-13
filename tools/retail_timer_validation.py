#!/usr/bin/env python3
"""Strict validation and comparison for the retail match-timer sidecar.

The sidecar is an observation made after the source tick and before audio
transport.  It proves only exact timer-field repeatability for the supplied
workload; it is not a gameplay, rendering, performance, or gold-corpus gate.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
from typing import Any, Iterable


SCHEMA = "melee-web-match-timer-audit"
VERSION = 1
PHASE = "after_source_tick_before_audio_transport"
MAX_FRAMES = 36000
MAX_BYTES = 8 * 1024 * 1024
SETUP_BYTES = 0x138

HEADER_KEYS = {"record", "schema", "version", "frames_requested", "setup_hex", "phase"}
INITIAL_KEYS = {"record", "match_frame", "seconds", "subframe", "outcome", "end_state"}
FRAME_KEYS = {"record", "index", "match_frame", "seconds", "subframe", "outcome", "end_state"}
END_KEYS = {"record", "frames", "status"}

SCOPE = (
    "exact timer-sidecar repeatability and port comparison only; this does not "
    "establish gameplay equivalence, rendering/audio agreement, performance, or "
    "gold admission"
)


class TimerValidationError(ValueError):
    """A timer sidecar, setup, or reference binding is invalid."""


class _DuplicateKey(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateKey(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _type_name(value: Any) -> str:
    return type(value).__name__


def _integer(value: Any, context: str, *, minimum: int | None = None,
             maximum: int | None = None) -> int:
    if type(value) is not int:
        raise TimerValidationError(
            f"{context}: expected integer, got {_type_name(value)}")
    if minimum is not None and value < minimum:
        raise TimerValidationError(f"{context}: integer is below {minimum}")
    if maximum is not None and value > maximum:
        raise TimerValidationError(f"{context}: integer is above {maximum}")
    return value


def _require_keys(row: dict[str, Any], expected: set[str], context: str) -> None:
    actual = set(row)
    missing = sorted(expected - actual)
    unknown = sorted(actual - expected)
    if missing or unknown:
        details = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if unknown:
            details.append("unknown " + ", ".join(unknown))
        raise TimerValidationError(f"{context}: invalid fields ({'; '.join(details)})")


def _setup_hex(value: Any, context: str = "setup_hex") -> str:
    if (not isinstance(value, str) or len(value) != SETUP_BYTES * 2 or
            re.fullmatch(r"[0-9a-f]+", value) is None):
        raise TimerValidationError(
            f"{context}: expected exactly {SETUP_BYTES} lowercase hexadecimal bytes")
    return value


def validate_setup(setup_hex: str) -> dict[str, Any]:
    """Validate and decode the canonical retail StartMeleeData setup."""

    setup_hex = _setup_hex(setup_hex)
    raw = bytes.fromhex(setup_hex)
    timer_enabled = bool(raw[0] & 2)
    timer_counts_up = bool(raw[0] & 1)
    time_limit = int.from_bytes(raw[0x10:0x14], "big")
    if not timer_enabled:
        raise TimerValidationError("setup: timer_enabled must be true")
    if timer_counts_up:
        raise TimerValidationError("setup: timer_counts_up must be false")
    # Match the shared native admission helper.  The ordinary VS Rule Plus
    # menu stores whole minutes and the source handoff expands them to seconds;
    # x14 selects a nonstandard initial subframe and the hours bit changes the
    # HUD format, so neither belongs to this sidecar's supported profile.
    if raw[1] & 2:
        raise TimerValidationError("setup: timer_shows_hours must be false")
    if raw[0x14] != 0:
        raise TimerValidationError("setup: x14 must be zero")
    if time_limit < 60 or time_limit > 99 * 60 or time_limit % 60:
        raise TimerValidationError(
            "setup: time_limit must be a whole 1..99 minute countdown in seconds")
    return {
        "setup_hex": setup_hex,
        "setup_sha256": hashlib.sha256(raw).hexdigest(),
        "timer_enabled": timer_enabled,
        "timer_counts_up": timer_counts_up,
        "time_limit": time_limit,
    }


def _validate_timer_fields(row: dict[str, Any], context: str) -> None:
    _integer(row["match_frame"], f"{context}.match_frame", minimum=0,
             maximum=0xFFFFFFFF)
    _integer(row["seconds"], f"{context}.seconds", minimum=0, maximum=0xFFFFFFFF)
    _integer(row["subframe"], f"{context}.subframe", minimum=0, maximum=59)
    _integer(row["outcome"], f"{context}.outcome", minimum=0, maximum=9)
    _integer(row["end_state"], f"{context}.end_state", minimum=0, maximum=3)


@dataclass(frozen=True)
class TimerCapture:
    rows: tuple[dict[str, Any], ...]
    header: dict[str, Any]
    initial: dict[str, Any]
    frames: tuple[dict[str, Any], ...]
    end: dict[str, Any]
    setup: dict[str, Any]
    sha256: str | None = None
    path: Path | None = None


def validate_rows(rows: Iterable[dict[str, Any]], context: str = "capture") -> TimerCapture:
    """Validate an already-decoded sidecar without repairing or normalizing it."""

    if not isinstance(rows, (list, tuple)):
        raise TimerValidationError(f"{context}: expected a JSONL record sequence")
    rows_tuple = tuple(rows)
    if len(rows_tuple) < 3:
        raise TimerValidationError(f"{context}: sidecar is incomplete")
    if any(not isinstance(row, dict) for row in rows_tuple):
        raise TimerValidationError(f"{context}: every record must be an object")

    header = rows_tuple[0]
    _require_keys(header, HEADER_KEYS, f"{context}.header")
    if header["record"] != "header":
        raise TimerValidationError(f"{context}: first record must be header")
    if header["schema"] != SCHEMA or type(header["version"]) is not int or header["version"] != VERSION:
        raise TimerValidationError(f"{context}.header: unsupported schema or version")
    requested = _integer(header["frames_requested"], f"{context}.header.frames_requested",
                         minimum=1, maximum=MAX_FRAMES)
    setup = validate_setup(header["setup_hex"])
    if header["phase"] != PHASE:
        raise TimerValidationError(f"{context}.header.phase: unsupported phase")
    expected_length = requested + 3
    if len(rows_tuple) != expected_length:
        raise TimerValidationError(
            f"{context}: expected header, initial, {requested} frames, and end "
            f"({expected_length} records); got {len(rows_tuple)}")

    initial = rows_tuple[1]
    _require_keys(initial, INITIAL_KEYS, f"{context}.initial")
    if initial["record"] != "initial":
        raise TimerValidationError(f"{context}: record 1 must be initial")
    _validate_timer_fields(initial, f"{context}.initial")

    frames: list[dict[str, Any]] = []
    for expected_index, row in enumerate(rows_tuple[2:-1]):
        row_context = f"{context}.frame[{expected_index}]"
        _require_keys(row, FRAME_KEYS, row_context)
        if row["record"] != "frame":
            raise TimerValidationError(f"{row_context}: record must be frame")
        if _integer(row["index"], f"{row_context}.index", minimum=0) != expected_index:
            raise TimerValidationError(
                f"{row_context}.index: expected {expected_index}, got {row['index']!r}")
        _validate_timer_fields(row, row_context)
        frames.append(row)

    end = rows_tuple[-1]
    _require_keys(end, END_KEYS, f"{context}.end")
    if end["record"] != "end":
        raise TimerValidationError(f"{context}: final record must be end")
    if _integer(end["frames"], f"{context}.end.frames", minimum=0) != requested:
        raise TimerValidationError(
            f"{context}.end.frames: expected {requested}, got {end['frames']!r}")
    if end["status"] != "captured":
        raise TimerValidationError(f"{context}.end.status: expected captured")
    return TimerCapture(rows_tuple, header, initial, tuple(frames), end, setup)


def _read_jsonl(path: Path, context: str) -> tuple[list[dict[str, Any]], str]:
    try:
        if path.stat().st_size > MAX_BYTES:
            raise TimerValidationError(
                f"{context}: sidecar exceeds the {MAX_BYTES}-byte limit")
        with path.open("rb") as stream:
            raw = stream.read(MAX_BYTES + 1)
    except OSError as error:
        raise TimerValidationError(f"{context}: cannot read sidecar: {error}") from error
    if len(raw) > MAX_BYTES:
        raise TimerValidationError(
            f"{context}: sidecar exceeds the {MAX_BYTES}-byte limit")
    if not raw:
        raise TimerValidationError(f"{context}: sidecar is empty")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise TimerValidationError(f"{context}: sidecar is not UTF-8: {error}") from error
    rows: list[dict[str, Any]] = []
    for line_number, line in enumerate(text.splitlines(), 1):
        if not line.strip():
            raise TimerValidationError(f"{context}:{line_number}: blank JSONL line")
        try:
            row = json.loads(line, object_pairs_hook=_reject_duplicate_keys)
        except (_DuplicateKey, json.JSONDecodeError) as error:
            raise TimerValidationError(f"{context}:{line_number}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise TimerValidationError(f"{context}:{line_number}: record must be an object")
        rows.append(row)
    return rows, hashlib.sha256(raw).hexdigest()


def load_capture(path: str | Path, *, context: str = "capture") -> TimerCapture:
    path = Path(path)
    rows, digest = _read_jsonl(path, context)
    capture = validate_rows(rows, context)
    return TimerCapture(capture.rows, capture.header, capture.initial,
                        capture.frames, capture.end, capture.setup, digest,
                        path.resolve())


def _first_difference(expected: Any, actual: Any, path: str = "") -> tuple[str, Any, Any] | None:
    if type(expected) is not type(actual):
        return path or "$", expected, actual
    if isinstance(expected, dict):
        for key in expected:
            child = f"{path}.{key}" if path else key
            if key not in actual:
                return child, expected[key], None
            difference = _first_difference(expected[key], actual[key], child)
            if difference:
                return difference
        for key in actual:
            child = f"{path}.{key}" if path else key
            if key not in expected:
                return child, None, actual[key]
        return None
    if isinstance(expected, (list, tuple)):
        if len(expected) != len(actual):
            return f"{path}.length", len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = _first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if expected == actual else (path or "$", expected, actual)


def _capture_difference(expected: TimerCapture, actual: TimerCapture) -> dict[str, Any] | None:
    difference = _first_difference(expected.rows, actual.rows, "records")
    if difference is None:
        return None
    field, left, right = difference
    return {"field": field, "expected": left, "actual": right}


def _capture_meta(capture: TimerCapture) -> dict[str, Any]:
    return {
        "sha256": capture.sha256,
        "frames": capture.header["frames_requested"],
    }


def _invalid_report(error: str, *, capture: str | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "version": VERSION,
        "scope": SCOPE,
        "status": "invalid_capture",
        "invalid_capture": {"error": error},
    }
    if capture is not None:
        result["invalid_capture"]["capture"] = capture
    return result


def _resolve_setup_hex(setup_hex: str | None, reference_capture: str | Path | None,
                       *, cpu: str) -> tuple[str, str | None, dict[str, Any] | None]:
    if setup_hex is not None and reference_capture is not None:
        raise TimerValidationError("provide either setup_hex or reference_capture, not both")
    if setup_hex is not None:
        setup = validate_setup(setup_hex)
        return setup["setup_hex"], None, None
    if reference_capture is None:
        raise TimerValidationError("an expected setup_hex or reference_capture is required")
    try:
        # This is the pinned complete retail candidate loader.  It validates
        # the source capture before its canonical 0x138-byte match entry is
        # used as the timer binding.
        from retail_replay_validation import load_capture as load_reference_capture
        reference = load_reference_capture(reference_capture, cpu=cpu)
    except Exception as error:
        raise TimerValidationError(
            f"reference_capture: cannot load pinned retail capture: {error}") from error
    setup = validate_setup(reference.match_enter["start_melee_hex"].lower())
    return setup["setup_hex"], reference.sha256, {
        "frames_requested": reference.header["frames_requested"],
        "initial_match_frame": reference.initial["match_frame"],
        "frame_match_frames": tuple(frame["match_frame"] for frame in reference.frames),
    }


def _validate_reference_binding(capture: TimerCapture,
                                binding: dict[str, Any], context: str) -> None:
    expected_frames = binding["frames_requested"]
    if capture.header["frames_requested"] != expected_frames:
        raise TimerValidationError(
            f"{context}: frames_requested does not match reference capture "
            f"({capture.header['frames_requested']} != {expected_frames})")
    if capture.initial["match_frame"] != binding["initial_match_frame"]:
        raise TimerValidationError(
            f"{context}: initial.match_frame does not match reference capture")
    expected_match_frames = binding["frame_match_frames"]
    if len(capture.frames) != len(expected_match_frames):
        raise TimerValidationError(
            f"{context}: frame count does not match reference capture")
    for index, (row, expected) in enumerate(zip(capture.frames, expected_match_frames)):
        if row["match_frame"] != expected:
            raise TimerValidationError(
                f"{context}: frame[{index}].match_frame does not match reference capture")


def compare_paths(capture_a: str | Path, capture_b: str | Path,
                  port: str | Path, *, setup_hex: str | None = None,
                  reference_capture: str | Path | None = None,
                  cpu: str = "Interpreter64") -> dict[str, Any]:
    """Validate A/B, require their exact repeatability, then compare port."""

    paths = [Path(capture_a), Path(capture_b), Path(port)]
    if reference_capture is not None:
        paths.append(Path(reference_capture))
    resolved = [path.resolve() for path in paths]
    if len(set(resolved)) != len(resolved):
        return _invalid_report("capture and reference paths must be distinct")
    try:
        expected_setup, reference_sha, reference_binding = _resolve_setup_hex(
            setup_hex, reference_capture, cpu=cpu)
        expected = validate_setup(expected_setup)
    except TimerValidationError as error:
        return _invalid_report(str(error), capture="setup")

    captures: list[TimerCapture] = []
    for label, path in zip(("a", "b", "port"), paths[:3]):
        try:
            capture = load_capture(path, context=f"capture_{label}")
        except TimerValidationError as error:
            return _invalid_report(str(error), capture=label)
        if capture.setup["setup_hex"] != expected_setup:
            return _invalid_report(
                "sidecar setup_hex does not match the canonical expected setup",
                capture=label)
        if reference_binding is not None:
            try:
                _validate_reference_binding(capture, reference_binding,
                                            f"capture_{label}")
            except TimerValidationError as error:
                return _invalid_report(str(error), capture=label)
        captures.append(capture)
    first, second, candidate = captures
    report: dict[str, Any] = {
        "schema": SCHEMA,
        "version": VERSION,
        "scope": SCOPE,
        "status": "pass",
        "setup": {
            "sha256": expected["setup_sha256"],
            "timer_enabled": expected["timer_enabled"],
            "timer_counts_up": expected["timer_counts_up"],
            "time_limit": expected["time_limit"],
            "reference_capture_sha256": reference_sha,
        },
        "captures": {
            "a": _capture_meta(first),
            "b": _capture_meta(second),
            "port": _capture_meta(candidate),
        },
        "reference": {"status": "repeatable", "frames_compared": len(first.frames)},
        "port": {"status": "not_run"},
    }
    if reference_binding is not None:
        report["reference_binding"] = {
            "frames_requested": reference_binding["frames_requested"],
            "initial_match_frame": reference_binding["initial_match_frame"],
            "match_frames": "exact",
        }
    difference = _capture_difference(first, second)
    if difference is not None:
        report["status"] = "reference_nonrepeatable"
        report["reference"] = {
            "status": "diverged", "frames_compared": min(len(first.frames), len(second.frames)),
            "first_divergence": difference,
        }
        return report
    difference = _capture_difference(first, candidate)
    if difference is not None:
        report["status"] = "port_diverged"
        report["port"] = {
            "status": "diverged", "frames_compared": min(len(first.frames), len(candidate.frames)),
            "first_divergence": difference,
        }
        return report
    report["port"] = {"status": "match", "frames_compared": len(first.frames)}
    return report


compare = compare_paths
compare_sidecars = compare_paths
validate_capture = validate_rows
load_timer_capture = load_capture


def status_exit_code(status: str) -> int:
    return {"pass": 0, "reference_nonrepeatable": 1,
            "port_diverged": 1, "invalid_capture": 2}.get(status, 2)
