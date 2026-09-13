#!/usr/bin/env python3
"""Validate and compare two read-only retail replay candidates.

The candidate format is deliberately small and strict.  It is an observation
of one ordinary GALE01r2 execution, rather than a port or performance oracle.
Both files must independently pass the schema, provenance, sequence, input,
and state checks before their observations are compared.

``match_enter_complete.scene_frame`` is validated as an unsigned source word
but is omitted from the initial-state comparison.  The match-entry callback
can still contain the historical scene-frame value from the preceding SSS
scene.  ``frame.index``, ``frame.match_frame``, and every other captured field
remain part of the exact comparison.  ``consumed_inputs`` preserves all 11
semantic PAD bytes, including stale members in a disconnected PAD error
status; the validator never fills, clears, or otherwise normalizes those
bytes.

The ``repeatable`` result means only that these two retail candidates agree.
It is not port equivalence, performance acceptance, or gold/admission
evidence.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
import struct
from typing import Any, Iterable
from pad_state import PAD_STATE_BYTES, decode_pad_state


SCHEMA = "melee-web-retail-replay-candidate"
VERSION = 1
PHASE = "HSD_GObj_80390CFC_return"
INPUT_PHASE = "HSD_PadRenewMasterStatus_entry_queue"
DEQUEUED_INPUT_PHASE = "HSD_PadRenewMasterStatus_dequeued_slot"
INITIAL_PHASE = "gm_Scene_Vs_OnEnter_entry"
GAME_REVISION = "GALE01r2"
MAX_FRAMES = 36000
CPU_PROFILES = {"Interpreter64": 0, "JITARM64": 4}

# These values are copied from the pinned provenance used by the collector.
# A candidate with a different executable, Dolphin build, source revision,
# CPU mode, cheats setting, or fixed RTC is not a reference candidate for this
# comparison.  cpu_thread=False is the Dolphin single-CPU-thread setting.
EXPECTED_PROVENANCE = {
    "dol_sha1": "08e0bf20134dfcb260699671004527b2d6bb1a45",
    "dolphin_version": "2606a",
    "dolphin_commit": "c77bbaa0f372c3f72281602a8b087206706542cb",
    "source_revision": "b43912cc78606f96c9569f5d6229bc9d7e265ea5",
    "cpu": "Interpreter64",
    "cpu_thread": False,
    "cheats": False,
    "fixed_rtc": 1704067200,
}
# Explicit names keep the pinned values easy to audit from a report or test.
EXPECTED_DOL_SHA1 = EXPECTED_PROVENANCE["dol_sha1"]
EXPECTED_DOLPHIN_VERSION = EXPECTED_PROVENANCE["dolphin_version"]
EXPECTED_DOLPHIN_COMMIT = EXPECTED_PROVENANCE["dolphin_commit"]
EXPECTED_SOURCE_REVISION = EXPECTED_PROVENANCE["source_revision"]
EXPECTED_FIXED_RTC = EXPECTED_PROVENANCE["fixed_rtc"]

HEADER_KEYS = {
    "record", "schema", "version", "phase", "input_phase", "initial_phase",
    "game_revision", "frames_requested", "provenance", "collector_sha256",
    "writes_game_state", "capture_id",
}
MATCH_ENTER_KEYS = {"record", "rng", "start_melee_hex", "pad_lib_hex",
                    "pad_master_hex", "pad_game_hex"}
STATE_KEYS = {"rng", "scene_frame", "match_frame", "fighters"}
FRAME_KEYS = {"record", "index", "consumed_inputs", *STATE_KEYS}
END_KEYS = {"record", "frames", "status"}
FIGHTER_KEYS = {
    "slot", "kind", "motion", "animation", "facing_bits", "position_bits",
    "velocity_bits", "knockback_bits", "ground_air", "animation_frame_bits",
    "animation_speed_bits", "damage_bits", "shield_bits", "stocks", "input_hex",
}
FLOAT_FIELDS = (
    "facing_bits", "animation_frame_bits", "animation_speed_bits", "damage_bits",
    "shield_bits",
)
VECTOR_FIELDS = ("position_bits", "velocity_bits", "knockback_bits")

SCHEMA_NOTE = (
    "match_enter_complete.scene_frame is validated but excluded from the initial "
    "comparison because it may retain the preceding SSS scene's historical value; "
    "frame.index and frame.match_frame are compared exactly"
)
SCOPE = (
    "reference repeatability only; this does not establish port equivalence, "
    "performance acceptance, or gold admission"
)
INDEPENDENCE_NOTE = (
    "distinct capture IDs are required for pairing but do not by themselves "
    "prove independent executions; retain the pinned provenance and run procedure"
)


class CaptureError(ValueError):
    """A candidate is malformed, incomplete, or not from the pinned setup."""


# Naming used by the older comparison tools and convenient for callers that
# treat all trace validators as one family.
TraceError = CaptureError
ReplayValidationError = CaptureError


class _DuplicateKey(ValueError):
    pass


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateKey(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def read_jsonl(path: str | Path) -> list[dict[str, Any]]:
    """Read candidate JSONL and reject malformed lines before schema checks."""

    source = Path(path)
    try:
        raw = source.read_bytes()
    except OSError as error:
        raise CaptureError(f"{source}: cannot read capture: {error}") from error
    if not raw:
        raise CaptureError(f"{source}: capture is empty")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CaptureError(f"{source}: capture is not UTF-8: {error}") from error

    rows: list[dict[str, Any]] = []
    # A blank line would make the physical JSONL sequence ambiguous, so reject
    # it instead of silently dropping a truncated record.
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
        raise CaptureError(f"{source}: capture is empty")
    return rows


# The old comparison tools expose ``read``; retaining this alias makes the
# validator convenient to use from small local probes without another parser.
read = read_jsonl


def _type_name(value: Any) -> str:
    return type(value).__name__


def _require_keys(row: dict[str, Any], expected: set[str], context: str) -> None:
    actual = set(row)
    missing = sorted(expected - actual, key=str)
    unknown = sorted(actual - expected, key=str)
    if missing or unknown:
        details = []
        if missing:
            details.append("missing " + ", ".join(missing))
        if unknown:
            details.append("unknown " + ", ".join(unknown))
        raise CaptureError(f"{context}: invalid fields ({'; '.join(details)})")


def _int(value: Any, context: str, *, minimum: int | None = None,
         maximum: int | None = None) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise CaptureError(f"{context}: expected integer, got {_type_name(value)}")
    if minimum is not None and value < minimum:
        raise CaptureError(f"{context}: integer is below {minimum}")
    if maximum is not None and value > maximum:
        raise CaptureError(f"{context}: integer is above {maximum}")
    return value


def _u32(value: Any, context: str) -> int:
    return _int(value, context, minimum=0, maximum=0xFFFFFFFF)


def _hex(value: Any, byte_count: int, context: str) -> str:
    if not isinstance(value, str):
        raise CaptureError(f"{context}: expected hexadecimal string")
    if len(value) != byte_count * 2 or re.fullmatch(r"[0-9a-fA-F]+", value) is None:
        raise CaptureError(
            f"{context}: expected exactly {byte_count} bytes of hexadecimal data")
    # Case is representation only; preserve every byte and do not interpret or
    # repair any PAD member.  The comparison uses this byte-exact spelling.
    return value.lower()


def _finite_float_bits(value: Any, context: str) -> str:
    bits = _hex(value, 4, context)
    if not math.isfinite(struct.unpack(">f", bytes.fromhex(bits))[0]):
        raise CaptureError(f"{context}: float bits are non-finite")
    return bits


def _validate_provenance(value: Any, context: str, *, cpu: str = "Interpreter64") -> dict[str, Any]:
    if cpu not in CPU_PROFILES:
        raise CaptureError("Unsupported reference CPU profile")
    if not isinstance(value, dict):
        raise CaptureError(f"{context}: provenance must be an object")
    for key, expected in dict(EXPECTED_PROVENANCE, cpu=cpu).items():
        if key not in value:
            raise CaptureError(f"{context}: missing pinned field {key}")
        # ``0 == False`` in Python, but these provenance values describe
        # different configuration types and must retain their exact shape.
        if type(value[key]) is not type(expected) or value[key] != expected:
            raise CaptureError(f"{context}: pinned provenance mismatch for {key}")
    # Hashes are optional extensions to the version-one provenance object, but
    # if present they must be complete hashes rather than unchecked paths.
    for key in ("dolphin_binary_sha256", "Dolphin.ini_sha256", "GCPadNew.ini_sha256", "input_plan_sha256"):
        if key in value and (not isinstance(value[key], str)
                             or re.fullmatch(r"[0-9a-fA-F]{64}", value[key]) is None):
            raise CaptureError(f"{context}: invalid {key}")
    return value


def _pad_state(value, context):
    _hex(value, PAD_STATE_BYTES, context)
    try: return decode_pad_state(value)
    except ValueError as error: raise CaptureError(context+": "+str(error)) from error


def _validate_fighter(value: Any, slot: int, context: str) -> None:
    if not isinstance(value, dict):
        raise CaptureError(f"{context}: fighter must be an object")
    _require_keys(value, FIGHTER_KEYS, context)
    if _int(value["slot"], f"{context}.slot", minimum=0, maximum=1) != slot:
        raise CaptureError(f"{context}.slot: expected supported slot {slot}")
    for field in ("kind", "motion", "animation", "ground_air"):
        _u32(value[field], f"{context}.{field}")
    for field in FLOAT_FIELDS:
        _finite_float_bits(value[field], f"{context}.{field}")
    for field in VECTOR_FIELDS:
        vector = value[field]
        if not isinstance(vector, list) or len(vector) != 3:
            raise CaptureError(f"{context}.{field}: expected three float-bit components")
        for index, component in enumerate(vector):
            _finite_float_bits(component, f"{context}.{field}[{index}]")
    _int(value["stocks"], f"{context}.stocks", minimum=-128, maximum=127)
    # The collector takes exactly 0x6c bytes from the fighter PADStatus area.
    _hex(value["input_hex"], 0x6C, f"{context}.input_hex")


def _validate_state(value: Any, context: str, version: int = 1) -> None:
    if not isinstance(value, dict):
        raise CaptureError(f"{context}: state must be an object")
    _require_keys(value, STATE_KEYS | ({"pad_state_hex"} if version == 2 else set()), context)
    if version == 2: _pad_state(value["pad_state_hex"], context+".pad_state_hex")
    _u32(value["rng"], f"{context}.rng")
    _u32(value["scene_frame"], f"{context}.scene_frame")
    _u32(value["match_frame"], f"{context}.match_frame")
    fighters = value["fighters"]
    if not isinstance(fighters, list) or len(fighters) != 2:
        raise CaptureError(f"{context}.fighters: exactly two fighter records are required")
    for slot, fighter in enumerate(fighters):
        _validate_fighter(fighter, slot, f"{context}.fighters[{slot}]")


def _validate_inputs(value: Any, context: str) -> None:
    if not isinstance(value, list) or len(value) != 1:
        raise CaptureError(
            f"{context}: exactly one consumed input vector is required per frame")
    vector = value[0]
    if not isinstance(vector, list) or len(vector) != 4:
        raise CaptureError(f"{context}: consumed input vector must contain four ports")
    for port, raw in enumerate(vector):
        # PADStatus has a 12-byte C layout with one padding byte at offset 11;
        # the candidate stores exactly the 11 semantic bytes per port.
        _hex(raw, 11, f"{context}[0][{port}]")


def _validate_scene_continuity(frames: list[dict[str, Any]], context: str) -> None:
    if not frames:
        raise CaptureError(f"{context}: no frame records")
    previous_scene = frames[0]["scene_frame"]
    if previous_scene != 0:
        raise CaptureError(f"{context}: first scene_frame must be zero at match entry")
    previous_match = frames[0]["match_frame"]
    for index, frame in enumerate(frames[1:], 1):
        scene = frame["scene_frame"]
        expected_scene = (previous_scene + 1) & 0xFFFFFFFF
        if scene != expected_scene:
            raise CaptureError(
                f"{context}: scene_frame is not contiguous at frame {index} "
                f"(expected {expected_scene}, got {scene})")
        # Match frame can remain at zero (and can remain unchanged during the
        # Ready/Go portion), so only reject backwards movement here.  Its value
        # is still compared exactly in every frame.
        if previous_match != 0xFFFFFFFF and frame["match_frame"] < previous_match:
            raise CaptureError(
                f"{context}: match_frame moved backwards at frame {index} "
                f"({previous_match} -> {frame['match_frame']})")
        previous_scene = scene
        previous_match = frame["match_frame"]


class _Capture:
    __slots__ = ("rows", "header", "match_enter", "initial", "frames", "end",
                 "sha256", "raw")

    def __init__(self, rows: tuple[dict[str, Any], ...], header: dict[str, Any],
                 match_enter: dict[str, Any], initial: dict[str, Any],
                 frames: tuple[dict[str, Any], ...], end: dict[str, Any],
                 sha256: str | None, raw: bytes | None = None):
        self.rows = rows
        self.header = header
        self.match_enter = match_enter
        self.initial = initial
        self.frames = frames
        self.end = end
        self.sha256 = sha256
        self.raw = raw


def _validate_capture(rows: Iterable[dict[str, Any]], context: str,
                      sha256: str | None = None,
                      raw: bytes | None = None, *, cpu: str = "Interpreter64") -> _Capture:
    if not isinstance(rows, (list, tuple)):
        raise CaptureError(f"{context}: capture rows must be a list")
    rows_tuple = tuple(rows)
    if not rows_tuple:
        raise CaptureError(f"{context}: capture is empty")
    if any(not isinstance(row, dict) for row in rows_tuple):
        raise CaptureError(f"{context}: every record must be an object")
    for index, row in enumerate(rows_tuple):
        if row.get("record") == "error":
            raise CaptureError(
                f"{context}: collector error at record {index}: "
                f"{row.get('error', 'unknown error')}")

    header = rows_tuple[0]
    if header.get("record") != "header":
        raise CaptureError(f"{context}: first record must be header")
    _require_keys(header, HEADER_KEYS, f"{context}.header")
    if (header["schema"] != SCHEMA or type(header["version"]) is not int
            or header["version"] not in (1, 2)):
        raise CaptureError(f"{context}.header: unsupported schema or version")
    if header["phase"] != PHASE:
        raise CaptureError(f"{context}.header: unsupported phase")
    if header["input_phase"] not in (INPUT_PHASE, DEQUEUED_INPUT_PHASE):
        raise CaptureError(f"{context}.header: unsupported input phase")
    if header["initial_phase"] != INITIAL_PHASE:
        raise CaptureError(f"{context}.header: unsupported initial phase")
    if header["game_revision"] != GAME_REVISION:
        raise CaptureError(f"{context}.header: unsupported game revision")
    version = header["version"]
    state_keys = STATE_KEYS | ({"pad_state_hex"} if version == 2 else set())
    requested = _int(header["frames_requested"], f"{context}.header.frames_requested",
                     minimum=1, maximum=MAX_FRAMES)
    _validate_provenance(header["provenance"], f"{context}.header.provenance", cpu=cpu)
    _hex(header["collector_sha256"], 32, f"{context}.header.collector_sha256")
    capture_id = header["capture_id"]
    _hex(capture_id, 16, f"{context}.header.capture_id")
    if capture_id[12].lower() != "4" or capture_id[16].lower() not in "89ab":
        raise CaptureError(f"{context}.header.capture_id: expected a UUID4 hex value")
    if header["writes_game_state"] is not False:
        raise CaptureError(f"{context}.header.writes_game_state: must be false")

    expected_length = requested + 4
    if len(rows_tuple) != expected_length:
        raise CaptureError(
            f"{context}: expected header, match_enter, initial state, {requested} "
            f"frames, and end ({expected_length} records); got {len(rows_tuple)}")

    match_enter = rows_tuple[1]
    if match_enter.get("record") != "match_enter":
        raise CaptureError(f"{context}: record 1 must be match_enter")
    _require_keys(match_enter, MATCH_ENTER_KEYS if version == 1 else
                  {"record", "rng", "start_melee_hex", "pad_state_hex"}, f"{context}.match_enter")
    _u32(match_enter["rng"], f"{context}.match_enter.rng")
    _hex(match_enter["start_melee_hex"], 0x138,
         f"{context}.match_enter.start_melee_hex")
    if version == 2:
        _pad_state(match_enter["pad_state_hex"], f"{context}.match_enter.pad_state_hex")
    else:
        _hex(match_enter["pad_lib_hex"], 0x20, f"{context}.match_enter.pad_lib_hex")
        _hex(match_enter["pad_master_hex"], 0x110,
             f"{context}.match_enter.pad_master_hex")
        _hex(match_enter["pad_game_hex"], 0x110,
             f"{context}.match_enter.pad_game_hex")

    initial = rows_tuple[2]
    if initial.get("record") != "match_enter_complete":
        raise CaptureError(f"{context}: record 2 must be match_enter_complete")
    _require_keys(initial, {"record", *state_keys},
                  f"{context}.match_enter_complete")
    initial_state = {key: initial.get(key) for key in state_keys}
    _validate_state(initial_state, f"{context}.match_enter_complete", version)

    frames: list[dict[str, Any]] = []
    for expected_index, row in enumerate(rows_tuple[3:-1]):
        context_row = f"{context}.frame[{expected_index}]"
        if row.get("record") != "frame":
            raise CaptureError(
                f"{context}: record {expected_index + 3} must be frame "
                f"(got {row.get('record')!r})")
        _require_keys(row, FRAME_KEYS | ({"pad_state_hex"} if version == 2 else set()), context_row)
        if _int(row["index"], f"{context_row}.index", minimum=0) != expected_index:
            raise CaptureError(
                f"{context_row}.index: expected {expected_index}, got {row['index']!r}")
        _validate_inputs(row["consumed_inputs"], f"{context_row}.consumed_inputs")
        _validate_state({key: row.get(key) for key in state_keys}, context_row, version)
        frames.append(row)
    _validate_scene_continuity(frames, context)

    end = rows_tuple[-1]
    if end.get("record") != "end":
        raise CaptureError(f"{context}: final record must be end")
    _require_keys(end, END_KEYS, f"{context}.end")
    if _int(end["frames"], f"{context}.end.frames", minimum=0) != requested:
        raise CaptureError(
            f"{context}.end.frames: expected {requested}, got {end['frames']!r}")
    if end["status"] != "captured":
        raise CaptureError(f"{context}.end.status: expected captured")
    return _Capture(rows_tuple, header, match_enter, initial, tuple(frames), end,
                    sha256, raw)


def validate_capture(rows: list[dict[str, Any]] | tuple[dict[str, Any], ...],
                     context: str = "capture", *, cpu: str = "Interpreter64") -> list[dict[str, Any]] | tuple[dict[str, Any], ...]:
    """Validate already-decoded rows and return them unchanged.

    The returned rows are intentionally the caller's objects; validation never
    repairs or normalizes captured state or raw PAD bytes.
    """

    _validate_capture(rows, context, cpu=cpu)
    return rows


def _load(value: str | Path | list[dict[str, Any]] | tuple[dict[str, Any], ...],
          context: str, *, cpu: str = "Interpreter64") -> _Capture:
    if isinstance(value, (str, Path)):
        path = Path(value)
        try:
            raw = path.read_bytes()
        except OSError as error:
            raise CaptureError(f"{context}: cannot read capture: {error}") from error
        digest = hashlib.sha256(raw).hexdigest()
        # Parse the already-read bytes so the digest identifies exactly what
        # was validated.  Keep read_jsonl's duplicate-key and UTF-8 handling.
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError as error:
            raise CaptureError(f"{context}: capture is not UTF-8: {error}") from error
        rows: list[dict[str, Any]] = []
        if not raw:
            raise CaptureError(f"{context}: capture is empty")
        for line_number, line in enumerate(text.splitlines(), 1):
            if not line.strip():
                raise CaptureError(f"{context}: blank JSONL line {line_number}")
            try:
                row = json.loads(line, object_pairs_hook=_reject_duplicate_keys)
            except (_DuplicateKey, json.JSONDecodeError) as error:
                raise CaptureError(f"{context}: invalid JSON at line {line_number}: {error}") from error
            if not isinstance(row, dict):
                raise CaptureError(f"{context}: line {line_number} is not an object")
            rows.append(row)
        return _validate_capture(rows, context, digest, raw, cpu=cpu)
    return _validate_capture(value, context, cpu=cpu)


def load_capture(path: str | Path, *, cpu: str = "Interpreter64") -> _Capture:
    """Load, hash, and validate one candidate file."""

    return _load(path, "capture", cpu=cpu)


def _lower_hex_strings(value: Any) -> Any:
    """Canonicalize hex spelling only; never changes bytes or missing members."""

    if isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]+", value):
        return value.lower()
    if isinstance(value, list):
        return [_lower_hex_strings(item) for item in value]
    if isinstance(value, dict):
        return {key: _lower_hex_strings(item) for key, item in value.items()}
    return value


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
            if key not in expected:
                child = f"{path}.{key}" if path else key
                return child, None, actual[key]
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}.length", len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = _first_difference(left, right, f"{path}[{index}]")
            if difference:
                return difference
        return None
    return None if expected == actual else (path or "$", expected, actual)


def _initial_semantics(capture: _Capture) -> dict[str, Any]:
    # scene_frame is intentionally the sole excluded field here; all other
    # initial match state, including match_frame and RNG, is compared exactly.
    initial = {
        "rng": capture.initial["rng"],
        "match_frame": capture.initial["match_frame"],
        "fighters": capture.initial["fighters"],
    }
    if capture.header["version"] == 2: initial["pad_state_hex"] = capture.initial["pad_state_hex"]
    return _lower_hex_strings(initial)


def _capture_metadata(capture: _Capture | None,
                      sha256: str | None = None) -> dict[str, Any] | None:
    if capture is None:
        if sha256 is None:
            return None
        return {"sha256": sha256, "provenance": None,
                "frames_requested": None, "frames_actual": None}
    return {
        "sha256": capture.sha256 if capture.sha256 is not None else sha256,
        "capture_id": capture.header["capture_id"],
        "provenance": capture.header["provenance"],
        "frames_requested": capture.header["frames_requested"],
        "frames_actual": len(capture.frames),
    }


def _base_report(first: _Capture | None, second: _Capture | None,
                 first_sha256: str | None = None,
                 second_sha256: str | None = None) -> dict[str, Any]:
    first_meta = _capture_metadata(first, first_sha256)
    second_meta = _capture_metadata(second, second_sha256)
    return {
        "status": "repeatable",
        "repeatable": True,
        "equivalent": True,
        "scope": SCOPE,
        "schema": SCHEMA,
        "version": first.header["version"] if first else VERSION,
        "schema_note": SCHEMA_NOTE,
        "independence_note": INDEPENDENCE_NOTE,
        "captures": {"a": first_meta, "b": second_meta},
        "capture_a_sha256": first_meta["sha256"] if first_meta else None,
        "capture_b_sha256": second_meta["sha256"] if second_meta else None,
    }


def _compare_validated(first_capture: _Capture, second_capture: _Capture) -> dict[str, Any]:
    """Compare two already-loaded captures without touching their files again."""

    report = _base_report(first_capture, second_capture)
    if (first_capture.header["capture_id"].lower() ==
            second_capture.header["capture_id"].lower()):
        report.update({
            "status": "invalid_capture", "repeatable": False, "equivalent": False,
            "invalid_capture": {
                "capture": "a_and_b",
                "error": "capture_a and capture_b have the same capture_id; "
                         "independent executions require distinct IDs",
            },
        })
        return report
    # Header configuration is independently pinned above.  The requested
    # count and collector identity still belong to this candidate schema and
    # are useful first divergences when otherwise-valid files differ.
    header_left = {
        "input_phase": first_capture.header["input_phase"],
        "cpu": first_capture.header["provenance"]["cpu"],
        "dolphin_binary_sha256": first_capture.header["provenance"].get("dolphin_binary_sha256"),
        "version": first_capture.header["version"],
        "frames_requested": first_capture.header["frames_requested"],
        "collector_sha256": first_capture.header["collector_sha256"],
        "input_plan_sha256": first_capture.header['provenance'].get('input_plan_sha256'),
    }
    header_right = {
        "input_phase": second_capture.header["input_phase"],
        "cpu": second_capture.header["provenance"]["cpu"],
        "dolphin_binary_sha256": second_capture.header["provenance"].get("dolphin_binary_sha256"),
        "version": second_capture.header["version"],
        "frames_requested": second_capture.header["frames_requested"],
        "collector_sha256": second_capture.header["collector_sha256"],
        "input_plan_sha256": second_capture.header['provenance'].get('input_plan_sha256'),
    }
    checks = {
        "schema": "pass", "version": "pass", "phase": "pass",
        "input_phase": "pass", "initial_phase": "pass", "provenance": "pass",
        "record_order": "pass", "frame_counts": "pass",
        "scene_frame_continuity": "pass", "match_frame_order": "pass",
        "semantic_state": "pass", "consumed_inputs": "pass",
        "initial_setup": "pass",
    }
    report["checks"] = checks
    report["frames_compared"] = len(first_capture.frames)

    comparisons: list[tuple[str, int | None, str, Any, Any]] = [
        ("header", None, "header", header_left, header_right),
        ("match_enter", None, "match_enter",
         _lower_hex_strings(first_capture.match_enter),
         _lower_hex_strings(second_capture.match_enter)),
        ("match_enter_complete", None, "state", _initial_semantics(first_capture),
         _initial_semantics(second_capture)),
    ]
    for index, (left, right) in enumerate(zip(first_capture.frames, second_capture.frames)):
        comparisons.append(("frame", index, "frame", _lower_hex_strings(left),
                            _lower_hex_strings(right)))
    comparisons.append(("end", None, "end", first_capture.end, second_capture.end))

    for record, frame, root, expected, actual in comparisons:
        difference = _first_difference(expected, actual, root)
        if difference:
            field, expected_value, actual_value = difference
            report.update({
                "status": "diverged", "repeatable": False, "equivalent": False,
                "first_divergence": {
                    "record": record, "frame": frame, "field": field,
                    "expected": expected_value, "actual": actual_value,
                },
            })
            if record == "header":
                checks["frame_counts"] = "fail"
                checks["initial_setup"] = "fail"
            elif record == "match_enter":
                checks["initial_setup"] = "fail"
            elif record == "match_enter_complete":
                checks["semantic_state"] = "fail"
            elif record == "frame":
                checks["semantic_state"] = "fail"
                if "consumed_inputs" in field:
                    checks["consumed_inputs"] = "fail"
            return report
    report["frames_compared"] = len(first_capture.frames)
    return report


def compare(first: str | Path | list[dict[str, Any]] | tuple[dict[str, Any], ...],
            second: str | Path | list[dict[str, Any]] | tuple[dict[str, Any], ...],
            *, cpu: str = "Interpreter64") -> dict[str, Any]:
    """Return a repeatability report for two independent candidate captures.

    Invalid inputs are reported with ``status == "invalid_capture"``.  A
    valid but different pair is ``"diverged"`` and includes the first record,
    frame, field, expected value, and actual value.  This shape lets the CLI
    emit machine-readable output even for malformed/truncated candidates.
    """

    first_capture: _Capture | None = None
    second_capture: _Capture | None = None
    try:
        first_capture = _load(first, "capture_a", cpu=cpu)
    except CaptureError as error:
        report = _base_report(None, None)
        report.update({
            "status": "invalid_capture", "repeatable": False, "equivalent": False,
            "invalid_capture": {"capture": "a", "error": str(error)},
        })
        return report
    try:
        second_capture = _load(second, "capture_b", cpu=cpu)
    except CaptureError as error:
        report = _base_report(first_capture, None)
        report.update({
            "status": "invalid_capture", "repeatable": False, "equivalent": False,
            "invalid_capture": {"capture": "b", "error": str(error)},
        })
        return report
    return _compare_validated(first_capture, second_capture)


# Descriptive alias for callers that use the operation name rather than the
# shorter ``compare`` API.
compare_captures = compare
compare_validated = _compare_validated


def status_exit_code(status: str) -> int:
    """CLI status mapping: success, semantic divergence, invalid candidate."""

    return {"repeatable": 0, "diverged": 1, "invalid_capture": 2}.get(status, 2)


def cli_main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Validate and compare two pinned retail replay candidates")
    parser.add_argument("capture_a", type=Path)
    parser.add_argument("capture_b", type=Path)
    parser.add_argument("--output", type=Path,
                        help="optional path for the machine-readable JSON report")
    parser.add_argument("--cpu", choices=CPU_PROFILES, default="Interpreter64")
    args = parser.parse_args(argv)
    report = compare(args.capture_a, args.capture_b, cpu=args.cpu)
    encoded = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        try:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(encoded, encoding="utf-8")
        except OSError as error:
            parser.exit(2, f"cannot write report: {error}\n")
    print(encoded, end="")
    return status_exit_code(report["status"])


def main() -> int:
    return cli_main()


if __name__ == "__main__":
    raise SystemExit(main())
