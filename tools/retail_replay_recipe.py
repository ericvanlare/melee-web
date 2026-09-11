#!/usr/bin/env python3
"""Export two repeatable retail candidates to the versioned MWRC input recipe.

The existing ``gameplay_retail_trace`` executable consumes this fixed-width
binary format::

    magic ``MWRC`` | version u32 | entry RNG u32 | frame count u32
    StartMeleeData[0x138]
    version 2 only: semantic initial PAD configuration/history[822]
    for each frame: four semantic PAD vectors, 11 bytes per port

The exporter uses the first candidate's setup and consumed inputs only after
the two independent candidate files pass the strict retail repeatability
comparator.  Its sidecar records file hashes, capture IDs, input bytes hash,
and scope.  It makes no port-equivalence, performance, or gold/admission
claim.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
from typing import Any

from retail_replay_validation import (CaptureError, MAX_FRAMES,
                                      compare_validated, load_capture, PAD_STATE_BYTES)


MAGIC = b"MWRC"
VERSION = 1
GAME_INFO_SIZE = 0x138
PAD_SEMANTIC_SIZE = 11
PORT_COUNT = 4
FRAME_INPUT_SIZE = PORT_COUNT * PAD_SEMANTIC_SIZE
MAX_TRANSPORT_SIZE = 16 + GAME_INFO_SIZE + PAD_STATE_BYTES + MAX_FRAMES * FRAME_INPUT_SIZE

HEADER = struct.Struct(">4sIII")
SCOPE = (
    "validated retail candidate input recipe and scoped source-state workload "
    "only; no port equivalence, performance acceptance, or gold admission claim"
)
INDEPENDENCE_NOTE = (
    "distinct capture IDs and distinct file bytes are required pairing checks; "
    "they do not by themselves prove independent executions, so retain the "
    "pinned provenance and run procedure"
)


class RecipeError(ValueError):
    """The pair is not independent/repeatable or cannot form an MWRC recipe."""


def _hex_bytes(value: Any, byte_count: int, context: str) -> bytes:
    if not isinstance(value, str) or len(value) != byte_count * 2:
        raise RecipeError(f"{context}: expected exactly {byte_count} bytes of hexadecimal data")
    try:
        result = bytes.fromhex(value)
    except ValueError as error:
        raise RecipeError(f"{context}: invalid hexadecimal data") from error
    if len(result) != byte_count:
        raise RecipeError(f"{context}: expected exactly {byte_count} bytes")
    return result


def _pair(first_path: str | Path, second_path: str | Path):
    first = Path(first_path).expanduser().resolve()
    second = Path(second_path).expanduser().resolve()
    if first == second:
        raise RecipeError(
            "capture A and capture B resolve to the same path; two executions are required")
    try:
        first_capture = load_capture(first)
        second_capture = load_capture(second)
    except CaptureError as error:
        raise RecipeError(f"retail candidate validation failed: {error}") from error
    first_hash = first_capture.sha256
    second_hash = second_capture.sha256
    if first_capture.raw == second_capture.raw:
        raise RecipeError(
            "capture A and capture B contain identical file bytes; "
            "two independent executions are required")
    report = compare_validated(first_capture, second_capture)
    if report.get("status") != "repeatable":
        if report.get("status") == "invalid_capture":
            detail = report.get("invalid_capture", {}).get("error", "invalid capture")
        else:
            difference = report.get("first_divergence", {})
            detail = (
                "first divergence record={record} frame={frame} field={field} "
                "expected={expected!r} actual={actual!r}"
            ).format(
                record=difference.get("record"), frame=difference.get("frame"),
                field=difference.get("field"), expected=difference.get("expected"),
                actual=difference.get("actual"))
        raise RecipeError(f"retail candidates are not repeatable: {detail}")
    return first, second, first_hash, second_hash, first_capture, second_capture, report


def encode_mwrc(capture) -> tuple[bytes, str]:
    """Encode one already-validated candidate and return (bytes, input hash)."""

    frames = capture.frames
    frame_count = len(frames)
    if not 1 <= frame_count <= MAX_FRAMES:
        raise RecipeError(f"frame count must be between 1 and {MAX_FRAMES}")
    setup = _hex_bytes(capture.match_enter["start_melee_hex"], GAME_INFO_SIZE,
                       "match_enter.start_melee_hex")
    seed = capture.match_enter["rng"]
    if not isinstance(seed, int) or isinstance(seed, bool) or not 0 <= seed <= 0xFFFFFFFF:
        raise RecipeError("match_enter.rng is outside the MWRC u32 range")

    input_bytes = bytearray()
    for index, frame in enumerate(frames):
        consumed = frame.get("consumed_inputs")
        if not isinstance(consumed, list) or len(consumed) != 1:
            raise RecipeError(f"frame {index}: expected exactly one consumed input vector")
        vector = consumed[0]
        if not isinstance(vector, list) or len(vector) != PORT_COUNT:
            raise RecipeError(f"frame {index}: expected four consumed PAD ports")
        for port, raw in enumerate(vector):
            input_bytes += _hex_bytes(raw, PAD_SEMANTIC_SIZE,
                                      f"frame {index} consumed_inputs port {port}")

    version = capture.header["version"]
    payload = bytearray(HEADER.pack(MAGIC, version, seed, frame_count))
    payload += setup
    if version == 2:
        payload += _hex_bytes(capture.match_enter["pad_state_hex"], PAD_STATE_BYTES, "entry PAD history")
    payload += input_bytes
    expected_size = 16 + GAME_INFO_SIZE + (PAD_STATE_BYTES if version == 2 else 0) + frame_count * FRAME_INPUT_SIZE
    if len(payload) != expected_size or len(payload) > MAX_TRANSPORT_SIZE:
        raise RecipeError("generated MWRC size does not match its frame count")
    return bytes(payload), hashlib.sha256(bytes(input_bytes)).hexdigest()


def _write_bytes(path: Path, payload: bytes) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
    except OSError as error:
        raise RecipeError(f"cannot write {path}: {error}") from error


def _write_json(path: Path, value: dict[str, Any]) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    except OSError as error:
        raise RecipeError(f"cannot write {path}: {error}") from error


def export_pair(first_path: str | Path, second_path: str | Path,
                output_path: str | Path, sidecar_path: str | Path | None = None) -> dict[str, Any]:
    """Validate two independent candidates and write a versioned MWRC recipe.

    Capture A supplies the output setup/input bytes after A and B have matched
    exactly.  The pair's hashes and provenance are retained in the sidecar.
    """

    output = Path(output_path).expanduser().resolve()
    sidecar = (Path(sidecar_path).expanduser().resolve() if sidecar_path is not None
               else Path(str(output) + ".json"))
    if output == sidecar:
        raise RecipeError("MWRC output and sidecar must be different paths")
    first, second, first_hash, second_hash, first_capture, second_capture, report = _pair(
        first_path, second_path)
    if output in (first, second) or sidecar in (first, second):
        raise RecipeError(
            "MWRC output and sidecar must not overwrite either reference capture")
    payload, input_hash = encode_mwrc(first_capture)
    _write_bytes(output, payload)
    sidecar_data = {
        "schema": "melee-web-retail-replay-recipe",
        "version": 1,
        "status": "exported",
        "scope": SCOPE,
        "independence_note": INDEPENDENCE_NOTE,
        "transport": {
            "magic": MAGIC.decode("ascii"),
            "version": first_capture.header["version"],
            "frames": len(first_capture.frames),
            "bytes": len(payload),
            "entry_rng": first_capture.match_enter["rng"],
            "start_melee_bytes": GAME_INFO_SIZE,
            "initial_pad_bytes": PAD_STATE_BYTES if first_capture.header["version"] == 2 else 0,
            "pad_bytes_per_port": PAD_SEMANTIC_SIZE,
            "ports": PORT_COUNT,
        },
        "input_sha256": input_hash,
        "output_sha256": hashlib.sha256(payload).hexdigest(),
        "captures": {
            "a": {
                "sha256": first_hash,
                "capture_id": first_capture.header["capture_id"],
                "frames": len(first_capture.frames),
                "provenance": first_capture.header["provenance"],
            },
            "b": {
                "sha256": second_hash,
                "capture_id": second_capture.header["capture_id"],
                "frames": len(second_capture.frames),
                "provenance": second_capture.header["provenance"],
            },
        },
        "repeatability": {
            "status": report["status"],
            "frames_compared": report.get("frames_compared"),
            "schema_note": report["schema_note"],
            "independence_note": report["independence_note"],
        },
        "claims": {
            "reference_repeatability": "pass",
            "port_equivalence": "not_claimed",
            "performance_acceptance": "not_claimed",
            "gold_admission": "not_claimed",
        },
    }
    _write_json(sidecar, sidecar_data)
    return sidecar_data


def main() -> int:
    import argparse

    parser = argparse.ArgumentParser(
        description="Export two repeatable retail candidates to versioned MWRC")
    parser.add_argument("capture_a", type=Path)
    parser.add_argument("capture_b", type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="MWRC binary output path")
    parser.add_argument("--sidecar", type=Path,
                        help="optional sidecar path (default: OUTPUT.json)")
    args = parser.parse_args()
    try:
        sidecar = export_pair(args.capture_a, args.capture_b, args.output, args.sidecar)
    except RecipeError as error:
        parser.exit(2, f"retail replay export failed: {error}\n")
    print(json.dumps({
        "status": sidecar["status"],
        "output_sha256": sidecar["output_sha256"],
        "input_sha256": sidecar["input_sha256"],
        "frames": sidecar["transport"]["frames"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
