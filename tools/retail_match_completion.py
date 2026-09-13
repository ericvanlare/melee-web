"""Validate the read-only completion observation for a retail capture.

The completion sidecar is deliberately separate from the JSONL replay
candidate.  A valid sidecar can establish that the observed retail source
execution reached the narrow elimination or timer-timeout boundary described
here; it says nothing about port equivalence, rendering, performance, audio,
or content admission.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from retail_replay_validation import (
    CaptureError,
    _int,
    _reject_duplicate_keys,
    _require_keys,
    _u32,
)


SCHEMA = "melee-web-retail-match-completion"
VERSION = 1
PHASES = {"after_final_source_draw", "after_final_scheduler"}
EXIT_PHASE = "gm_801A4B60_return"
FINAL_EXIT_CALLER = 0x8016D884
MAX_BYTES = 1024 * 1024

TOP_LEVEL_KEYS = {
    "schema",
    "version",
    "capture_sha256",
    "frames",
    "phase",
    "scene_request",
    "match_end_state",
    "match_result",
    "final_draw_source_index",
    "exit_observation",
}
EXIT_KEYS = {"phase", "index", "caller", "scene_request"}

SCOPE = (
    "source retail match completion only: the validated capture's final fighter "
    "stock observation, elimination or timer-timeout result, scene "
    "request/end-state/result bytes, callback-return "
    "observation, and final source-draw marker; no port equivalence, rendering, "
    "performance, audio, or gold/content admission"
)


def _read_sidecar(path: Path) -> tuple[dict[str, Any], bytes]:
    """Read one bounded JSON sidecar and preserve the exact bytes for hashing."""

    try:
        with path.open("rb") as stream:
            raw = stream.read(MAX_BYTES + 1)
    except OSError as error:
        raise CaptureError(f"{path}: cannot read completion sidecar: {error}") from error
    if not raw:
        raise CaptureError(f"{path}: completion sidecar is empty")
    if len(raw) > MAX_BYTES:
        raise CaptureError(f"{path}: completion sidecar exceeds byte limit")
    try:
        text = raw.decode("utf-8")
    except UnicodeDecodeError as error:
        raise CaptureError(f"{path}: completion sidecar is not UTF-8: {error}") from error
    try:
        value = json.loads(
            text,
            object_pairs_hook=_reject_duplicate_keys,
            parse_constant=lambda constant: (_ for _ in ()).throw(
                ValueError(f"invalid JSON constant {constant}")),
        )
    except (ValueError, TypeError) as error:
        raise CaptureError(f"{path}: invalid completion JSON: {error}") from error
    if not isinstance(value, dict):
        raise CaptureError(f"{path}: completion sidecar must be a JSON object")
    return value, raw


def _optional_source_index(value: Any, frame_count: int, context: str) -> int | None:
    if value is None:
        return None
    return _int(value, context, minimum=0, maximum=frame_count - 1)


def _validate_exit(value: Any, frame_count: int, *, allow_one_past: bool = False) -> dict[str, Any] | None:
    if value is None:
        return None
    context = "completion.exit_observation"
    if not isinstance(value, dict):
        raise CaptureError(f"{context}: expected object or null")
    _require_keys(value, EXIT_KEYS, context)
    if value["phase"] != EXIT_PHASE or type(value["phase"]) is not str:
        raise CaptureError(f"{context}.phase: expected {EXIT_PHASE!r}")
    # The fixed collector retains the raw scheduler count.  When the exit
    # callback is observed after the final source draw, that count is one past
    # the final frame index; the draw marker still identifies frame_count - 1.
    maximum = frame_count if allow_one_past else frame_count - 1
    _int(value["index"], f"{context}.index", minimum=0, maximum=maximum)
    _u32(value["caller"], f"{context}.caller")
    _u32(value["scene_request"], f"{context}.scene_request")
    return dict(value)


def _validate_sidecar(value: dict[str, Any], capture: Any) -> dict[str, Any]:
    _require_keys(value, TOP_LEVEL_KEYS, "completion")
    if value["schema"] != SCHEMA or type(value["schema"]) is not str:
        raise CaptureError("completion.schema: unsupported schema")
    if type(value["version"]) is not int or value["version"] != VERSION:
        raise CaptureError("completion.version: unsupported version")

    capture_hash = getattr(capture, "sha256", None)
    if (type(capture_hash) is not str or len(capture_hash) != 64
            or any(character not in "0123456789abcdef" for character in capture_hash)):
        raise CaptureError("capture.sha256: a byte-validated capture hash is required")
    if type(value["capture_sha256"]) is not str:
        raise CaptureError("completion.capture_sha256: expected a SHA-256 string")
    if value["capture_sha256"] != capture_hash:
        raise CaptureError("completion.capture_sha256: does not match capture.sha256")

    frames = getattr(capture, "frames", None)
    if not isinstance(frames, (list, tuple)) or not frames:
        raise CaptureError("capture.frames: a non-empty validated frame sequence is required")
    frame_count = len(frames)
    if _int(value["frames"], "completion.frames", minimum=0) != frame_count:
        raise CaptureError(
            f"completion.frames: expected {frame_count}, got {value['frames']!r}")
    if type(value["phase"]) is not str or value["phase"] not in PHASES:
        raise CaptureError("completion.phase: unsupported phase")
    scene_request = _u32(value["scene_request"], "completion.scene_request")
    match_end_state = _int(value["match_end_state"], "completion.match_end_state",
                           minimum=0, maximum=0xFF)
    match_result = _int(value["match_result"], "completion.match_result",
                        minimum=0, maximum=0xFF)
    final_draw = _optional_source_index(
        value["final_draw_source_index"], frame_count,
        "completion.final_draw_source_index")
    exit_observation = _validate_exit(
        value["exit_observation"], frame_count,
        allow_one_past=value["phase"] == "after_final_source_draw")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "capture_sha256": capture_hash,
        "frames": frame_count,
        "phase": value["phase"],
        "scene_request": scene_request,
        "match_end_state": match_end_state,
        "match_result": match_result,
        "final_draw_source_index": final_draw,
        "exit_observation": exit_observation,
    }


def _stocks_show_elimination(capture: Any) -> bool:
    """Return whether the final frame leaves one active fighter."""

    try:
        fighters = capture.frames[-1]["fighters"]
        stocks = [fighter["stocks"] for fighter in fighters]
    except (AttributeError, IndexError, KeyError, TypeError):
        return False
    active_player_count = getattr(capture, "active_player_count", len(stocks))
    if (not 2 <= active_player_count <= 4 or
            len(stocks) != active_player_count or
            any(type(stock) is not int for stock in stocks)):
        return False
    return (stocks.count(0) == active_player_count - 1 and
            len([stock for stock in stocks if stock > 0]) == 1 and
            all(stock >= 0 for stock in stocks))


def _timer_setup_is_enabled(capture: Any) -> bool:
    try:
        setup = bytes.fromhex(capture.match_enter["start_melee_hex"])
    except (AttributeError, KeyError, TypeError, ValueError):
        return False
    return (len(setup) == 0x138 and bool(setup[0] & 0x02) and
            int.from_bytes(setup[0x10:0x14], "big") > 0)


def _stocks_show_timeout(capture: Any) -> bool:
    """Return whether a source timeout left a valid active-player stock set."""

    try:
        fighters = capture.frames[-1]["fighters"]
        stocks = [fighter["stocks"] for fighter in fighters]
    except (AttributeError, IndexError, KeyError, TypeError):
        return False
    active_player_count = getattr(capture, "active_player_count", len(stocks))
    return (_timer_setup_is_enabled(capture) and
            2 <= active_player_count <= 4 and
            len(stocks) == active_player_count and
            all(type(stock) is int and stock >= 0 for stock in stocks))


def _ending(sidecar: dict[str, Any], capture: Any) -> dict[str, Any] | None:
    if sidecar["match_result"] == 2 and _stocks_show_elimination(capture):
        fighters = capture.frames[-1]["fighters"]
        return {"mode": "elimination",
                "winner_slots": [fighter["slot"] for fighter in fighters
                                  if fighter["stocks"] > 0],
                "stock_tie": False}
    if sidecar["match_result"] == 1 and _stocks_show_timeout(capture):
        # MatchEnd's winner/tie standings are outside this historical sidecar;
        # expose only the stock ranking.  A tied-stock timeout is not reduced
        # to an invented source winner.
        fighters = capture.frames[-1]["fighters"]
        highest = max(fighter["stocks"] for fighter in fighters)
        stock_winners = [fighter["slot"] for fighter in fighters
                         if fighter["stocks"] == highest]
        return {"mode": "timeout", "stock_winner_slots": stock_winners,
                "stock_tie": len(stock_winners) != 1}
    return None


def _is_complete(sidecar: dict[str, Any], capture: Any) -> bool:
    final_index = len(capture.frames) - 1
    exit_observation = sidecar["exit_observation"]
    ending = _ending(sidecar, capture)
    return (
        sidecar["phase"] == "after_final_source_draw"
        and sidecar["scene_request"] == 1
        and sidecar["match_end_state"] == 3
        and ending is not None
        and sidecar["final_draw_source_index"] == final_index
        and exit_observation is not None
        and exit_observation["phase"] == EXIT_PHASE
        and exit_observation["index"] in (final_index, final_index + 1)
        and exit_observation["caller"] == FINAL_EXIT_CALLER
        and exit_observation["scene_request"] == 1
    )


def load_match_completion(capture: Any, path: str | Path,
                          require_complete: bool = False) -> dict[str, Any]:
    """Load a completion sidecar bound to ``capture`` and return its report.

    A valid sidecar that only describes a prefix is reported as
    ``bounded_prefix``.  ``require_complete`` turns that bounded result into a
    hard ``CaptureError`` for callers that require the full source ending gate.
    """

    if type(require_complete) is not bool:
        raise CaptureError("require_complete must be boolean")
    sidecar_path = Path(path)
    sidecar, raw = _read_sidecar(sidecar_path)
    sidecar = _validate_sidecar(sidecar, capture)
    complete = _is_complete(sidecar, capture)
    status = "source_match_complete" if complete else "bounded_prefix"
    if require_complete and not complete:
        raise CaptureError(f"{sidecar_path}: completion sidecar does not prove full source ending")
    report = dict(sidecar)
    report.update({
        "status": status,
        "completion_sha256": hashlib.sha256(raw).hexdigest(),
        "capture_sha256": sidecar["capture_sha256"],
        "gold_admitted": False,
        "content_admitted": False,
        "port_equivalence": "not_evaluated",
        "performance": "not_evaluated",
        "ending": _ending(sidecar, capture),
        "scope": SCOPE,
    })
    return report
