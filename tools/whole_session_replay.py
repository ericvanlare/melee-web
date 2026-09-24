#!/usr/bin/env python3
"""Derive a checked MWRC v8 recipe from raw whole-session observer streams.

The only replay inputs admitted here are bytes from the source PAD queue at the
observer's pad_consume boundary.  Lifecycle boundaries assign each consumed
sample to the source scene that was active at that point.  No CPU observation,
host input, or intended menu schedule is used as an input.

This producer is deliberately stricter than the v8 reader.  It requires two
complete, independently identified streams, the same source setup/profile, and
the same ordered source-consumed PAD history.  It rejects missing boundaries,
unsupported phases, changed setups, and any partial stream before creating an
MWRC file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any, Iterable, Mapping


ROOT = Path(__file__).resolve().parents[1]
OBSERVER_ROOT = ROOT / "reference-capture" / "dolphin"
sys.path.insert(0, str(OBSERVER_ROOT))
sys.path.insert(0, str(ROOT / "tools"))

from reference_observer_stream import iter_records, read_status  # noqa: E402
import reference_capture_semantics as semantics  # noqa: E402
from retail_setup_validation import _decode_setup  # noqa: E402


MAGIC = b"MWRC"
MWRC_VERSION = 8
LEGACY_WHOLE_SESSION_VERSION = 7
GAME_INFO_SIZE = 0x138
PAD_STATE_BYTES = 822
PAD_SEMANTIC_SIZE = 11
PORT_COUNT = 4
FRAME_INPUT_SIZE = PORT_COUNT * PAD_SEMANTIC_SIZE
MAX_FRAMES = 36000
MAX_SPANS = 32
HEADER = struct.Struct(">4sIIIHH")
CONTEXT_HEADER = struct.Struct(">HHI")
CONTEXT_VERSION = 2
CSS_DATA_SIZE = 0x148
KO_COUNTS_SIZE = 6
CONTEXT_BYTES = (semantics.GAME_RULES_SIZE + semantics.SAVE_DATA_SIZE +
                 CSS_DATA_SIZE + KO_COUNTS_SIZE)
SPAN = struct.Struct(">BBHII")

EXPECTED_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EXPECTED_DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
EXPECTED_DOLPHIN_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
EXPECTED_OBSERVER_SCHEMA = "melee-web-passive-dolphin-observer"

SCHEMA = "melee-web-whole-session-replay-candidate"
SCHEMA_VERSION = 1
SCOPE = (
    "original source-consumed PAD repeatability and scoped MWRC v8 whole-session "
    "workload; no CPU-decision input, port-equivalence, performance, pixel, PCM, "
    "or tournament-admission claim"
)
RUNTIME_CONTEXT_STATUS = (
    "v8 carries source first-CSS PAD/RNG/GameRules/SaveData/CSSData/KO context "
    "for the typed consumer; end-to-end runtime equivalence remains unevaluated"
)

SCENES = {"css": 1, "sss": 2, "match": 3, "results": 4, "prize": 5}
OWNER_ENTRY_BOUNDARIES = {
    "css_enter", "css_cancel_enter", "sss_enter", "entry", "results_enter",
    "prize_mode_enter", "return_css",
}
LIFECYCLE_NAMES = set(semantics.WHOLE_OBSERVER_PCS)
IDENTITY = re.compile(r"[A-Za-z0-9_.-]{1,128}\Z")


class WholeSessionReplayError(ValueError):
    """The raw streams cannot be admitted as a complete MWRC v8 workload."""


def _fail(message: str) -> None:
    raise WholeSessionReplayError(message)


def _integer(value: Any, context: str, low: int = 0, high: int = 0xFFFFFFFF) -> int:
    if type(value) is not int or not low <= value <= high:
        _fail(f"{context} must be an integer in [{low}, {high}]")
    return value


def _hex_bytes(value: Any, size: int, context: str) -> bytes:
    if not isinstance(value, str) or len(value) != size * 2:
        _fail(f"{context} must be exactly {size:#x} bytes of hexadecimal data")
    try:
        result = bytes.fromhex(value)
    except ValueError as error:
        raise WholeSessionReplayError(f"{context} is not hexadecimal") from error
    if len(result) != size:
        _fail(f"{context} must be exactly {size:#x} bytes")
    return result


def _payload(row: Mapping[str, Any], index: int) -> Mapping[str, Any]:
    value = row.get("payload")
    if not isinstance(value, Mapping):
        _fail(f"record {index} payload is not an object")
    return value


def _slice(row: Mapping[str, Any], name: str, size: int, context: str) -> tuple[Mapping[str, Any], bytes]:
    payload = row.get("payload", {})
    items = payload.get("slices", []) if isinstance(payload, Mapping) else []
    matches = [item for item in items if isinstance(item, Mapping) and item.get("name") == name]
    if len(matches) != 1:
        _fail(f"{context} must contain exactly one {name} slice")
    item = matches[0]
    if item.get("size") != size:
        _fail(f"{context} {name} slice must be exactly {size:#x} bytes")
    return item, _hex_bytes(item.get("hex"), size, f"{context} {name}")


def _gpr(payload: Mapping[str, Any], index: int, context: str) -> int:
    registers = payload.get("gprs")
    if not isinstance(registers, list) or len(registers) != 32:
        _fail(f"{context} must contain 32 source GPRs")
    value = registers[index]
    if isinstance(value, str):
        try:
            value = int(value, 16)
        except ValueError as error:
            raise WholeSessionReplayError(f"{context} GPR {index} is not hexadecimal") from error
    return _integer(value, f"{context} GPR {index}")


def _record_envelope(
        records: list[Mapping[str, Any]]
) -> tuple[Mapping[str, Any], Mapping[str, Any], Mapping[str, Any]]:
    if len(records) < 3:
        _fail("whole-session observer stream is truncated before its end record")
    for event in ("handshake", "start", "end"):
        if sum(row.get("event") == event for row in records) != 1:
            _fail(f"whole-session observer stream must contain exactly one {event} record")
    for index, row in enumerate(records):
        if not isinstance(row, Mapping):
            _fail(f"record {index} is not an object")
        if row.get("event") not in {"handshake", "start", "boundary", "progress", "error", "end"}:
            _fail(f"record {index} has an unsupported event")
        if row.get("seq") != index:
            _fail(f"observer sequence is not contiguous at record {index}")
        if row.get("event") == "error":
            _fail(f"observer stream contains error record {index}")
    if records[0].get("event") != "handshake" or records[1].get("event") != "start":
        _fail("whole-session observer stream must begin with handshake then start")
    if records[-1].get("event") != "end":
        _fail("whole-session observer stream must end with an end record")
    end = _payload(records[-1], len(records) - 1)
    if end.get("status") != "completed" or end.get("natural") is not True:
        _fail("observer stream did not complete naturally")
    return _payload(records[0], 0), _payload(records[1], 1), end


def _capture_identity(records: list[Mapping[str, Any]]) -> dict[str, Any]:
    handshake, start, _ = _record_envelope(records)
    if handshake.get("schema") != EXPECTED_OBSERVER_SCHEMA or handshake.get("version") != 1:
        _fail("observer handshake is not the pinned passive observer schema")
    for field, expected in (
        ("dolphin_commit", EXPECTED_DOLPHIN_COMMIT),
        ("dol_sha1", EXPECTED_DOL_SHA1),
        ("dol_sha256", EXPECTED_DOL_SHA256),
        ("cpu", "JITARM64"),
    ):
        if handshake.get(field) != expected:
            _fail(f"observer handshake {field} is not the pinned reference identity")
    if handshake.get("writes_guest_memory") is not False:
        _fail("observer handshake must declare writes_guest_memory=false")
    for announcement, context in ((handshake, "handshake"), (start, "start")):
        if announcement.get("whole_session") is not True:
            _fail(f"{context} does not declare whole_session=true")
        _integer(announcement.get("match_count"), f"{context} match_count", 3, 64)
        capture_id = announcement.get("capture_id")
        sequence_id = announcement.get("sequence_id")
        if not isinstance(capture_id, str) or IDENTITY.fullmatch(capture_id) is None:
            _fail(f"{context} capture_id is not a safe identity")
        if not isinstance(sequence_id, str) or IDENTITY.fullmatch(sequence_id) is None:
            _fail(f"{context} sequence_id is not a safe identity")
    for field in ("match_count", "capture_id", "sequence_id"):
        if handshake.get(field) != start.get(field):
            _fail(f"observer {field} disagrees between handshake and start")
    if start.get("source_revision") != "GALE01r2":
        _fail("observer start is not for GALE01r2")
    return {
        "capture_id": handshake["capture_id"],
        "sequence_id": handshake["sequence_id"],
        "match_count": handshake["match_count"],
        "source_revision": start["source_revision"],
        "observer_schema": handshake["schema"],
        "observer_version": handshake["version"],
        "dolphin_commit": handshake["dolphin_commit"],
        "dol_sha1": handshake["dol_sha1"],
        "dol_sha256": handshake["dol_sha256"],
        "cpu": handshake["cpu"],
    }


def _lifecycle_rows(records: Iterable[Mapping[str, Any]]) -> list[Mapping[str, Any]]:
    result = []
    for index, row in enumerate(records):
        if row.get("event") != "boundary":
            continue
        payload = row.get("payload")
        if not isinstance(payload, Mapping):
            _fail(f"boundary record {index} has no payload")
        if payload.get("whole_session") is not True:
            _fail(f"boundary record {index} is missing whole-session metadata")
        if payload.get("boundary") in LIFECYCLE_NAMES:
            result.append(row)
    return result


def _validate_lifecycle(records: list[Mapping[str, Any]], match_count: int) -> dict[str, Any]:
    lifecycle = _lifecycle_rows(records)
    try:
        report = semantics.validate_whole_session_observer_records(
            [row for row in records
             if row.get("event") != "boundary" or
             row.get("payload", {}).get("boundary") in LIFECYCLE_NAMES])
    except (KeyError, TypeError, semantics.WholeSessionSemanticError) as error:
        raise WholeSessionReplayError(f"whole-session lifecycle is not complete: {error}") from error
    if report.get("match_count") != match_count:
        _fail("whole-session lifecycle match_count disagrees with start")
    if len(lifecycle) == 0:
        _fail("whole-session observer stream has no lifecycle boundaries")
    return report


def _first_css_context(records: list[Mapping[str, Any]]) -> dict[str, Any]:
    rows = [row for row in _lifecycle_rows(records)
            if row.get("payload", {}).get("boundary") == "css_enter" and
            row.get("payload", {}).get("match_index") == 0]
    if len(rows) != 1:
        _fail("whole-session stream must contain exactly one first CSS entry")
    row = rows[0]
    try:
        masks = semantics._whole_profile_masks(row)
        profile_context = semantics._whole_profile_context(row)
    except semantics.WholeSessionSemanticError as error:
        raise WholeSessionReplayError(f"first CSS profile context is invalid: {error}") from error
    if profile_context is None:
        _fail("first CSS lacks typed profile game-rules/save-data context")
    _, raw_rules = _slice(row, "profile_game_rules", semantics.GAME_RULES_SIZE,
                          "first CSS")
    _, raw_save = _slice(row, "profile_save_data", semantics.SAVE_DATA_SIZE,
                         "first CSS")
    if int.from_bytes(raw_save[0:2], "big") != masks["characters"] or \
            int.from_bytes(raw_save[2:4], "big") != masks["stages"]:
        _fail("first CSS profile masks disagree with typed SaveData")
    try:
        css_item, raw_css = _slice(row, "menu_css_context", CSS_DATA_SIZE, "first CSS")
    except WholeSessionReplayError as error:
        raise WholeSessionReplayError(
            f"first CSS lacks typed CSSData context: {error}") from error
    css_pointer = _gpr(row["payload"], 3, "first CSS")
    if css_item.get("address") != css_pointer:
        _fail("first CSS CSSData slice address disagrees with source r3")
    if raw_css[2] != 0 or raw_css[3] != 0:
        _fail("first CSS CSSData is not an ordinary initial VS context")
    if any(raw_css[0x10 + 0x38:0x10 + 0x5C]):
        _fail("first CSS CSSData contains unsupported callback or private pointers")
    ko_pointer = int.from_bytes(raw_css[4:8], "big")
    if not ko_pointer:
        _fail("first CSS CSSData has no source KO-count owner")
    try:
        ko_item, raw_ko = _slice(row, "menu_css_ko_counts", KO_COUNTS_SIZE, "first CSS")
    except WholeSessionReplayError as error:
        raise WholeSessionReplayError(
            f"first CSS lacks typed CSS KO-count context: {error}") from error
    if ko_item.get("address") != ko_pointer:
        _fail("first CSS KO-count slice address disagrees with CSSData")
    pad_item, raw_pad = _slice(row, "pad_snapshot", 0x358, "first CSS")
    if pad_item.get("address") != 0x804C1F84:
        _fail("first CSS PAD snapshot escaped the pinned source address")
    rng_pointer_item, raw_rng_pointer = _slice(row, "rng_pointer", 4, "first CSS")
    if rng_pointer_item.get("address") != 0x804D5F94:
        _fail("first CSS RNG pointer escaped the pinned source address")
    rng_item, raw_rng = _slice(row, "rng_value", 4, "first CSS")
    rng_pointer = int.from_bytes(raw_rng_pointer, "big")
    if rng_item.get("address") != rng_pointer:
        _fail("first CSS RNG value address disagrees with the source RNG pointer")
    try:
        pad_state_hex = semantics.pad_snapshot_bytes(raw_pad)
    except (TypeError, ValueError) as error:
        raise WholeSessionReplayError(f"first CSS source PAD snapshot is invalid: {error}") from error
    return {
        "rng": int.from_bytes(raw_rng, "big"),
        "pad_state_hex": pad_state_hex,
        "profile_masks": masks,
        "profile_context": profile_context,
        "game_rules_hex": raw_rules.hex(),
        "save_data_hex": raw_save.hex(),
        "css_data_hex": raw_css.hex(),
        "ko_counts_hex": raw_ko.hex(),
        "profile_game_rules_sha256": _slice_hash(row, "profile_game_rules",
                                                  semantics.GAME_RULES_SIZE, "first CSS"),
        "profile_save_data_sha256": _slice_hash(row, "profile_save_data",
                                                 semantics.SAVE_DATA_SIZE, "first CSS"),
        "observer_seq": row["seq"],
        "source_tick": row["source_tick"],
    }


def _slice_hash(row: Mapping[str, Any], name: str, size: int, context: str) -> str:
    _, raw = _slice(row, name, size, context)
    return hashlib.sha256(raw).hexdigest()


def _match_setups(records: list[Mapping[str, Any]], match_count: int) -> tuple[str, dict[str, Any]]:
    entries: dict[int, Mapping[str, Any]] = {}
    for row in _lifecycle_rows(records):
        payload = row["payload"]
        if payload.get("boundary") == "entry":
            match = _integer(payload.get("match_index"), "entry match_index", 0, match_count - 1)
            if match in entries:
                _fail(f"match {match} has duplicate source entry boundaries")
            entries[match] = row
    if set(entries) != set(range(match_count)):
        _fail("whole-session stream is missing one or more source entry boundaries")
    setup_hex = None
    declared = None
    for match in range(match_count):
        setup_item, raw = _slice(entries[match], "match_setup", GAME_INFO_SIZE,
                                  f"match {match} entry")
        setup_pointer = _gpr(entries[match]["payload"], 3, f"match {match} entry")
        if setup_item.get("address") != setup_pointer:
            _fail(f"match {match} setup slice address disagrees with source r3")
        current = raw.hex()
        if setup_hex is None:
            setup_hex = current
            try:
                declared = _decode_setup(current)
            except (KeyError, TypeError, ValueError) as error:
                raise WholeSessionReplayError(f"source setup is unsupported: {error}") from error
        elif current != setup_hex:
            _fail("source StartMeleeData changed between matches; MWRC v8 has one setup")
    return setup_hex, declared


def _consumed_ports(row: Mapping[str, Any], index: int) -> list[str]:
    payload = row.get("payload")
    if not isinstance(payload, Mapping) or payload.get("boundary") != "pad_consume":
        _fail(f"record {index} is not a PAD consume boundary")
    queue_item, queue = _slice(row, "pad_queue", 0xC, f"PAD consume record {index}")
    if queue_item.get("address") != 0x804C1F78:
        _fail(f"PAD consume record {index} queue slice escaped the pinned source address")
    slot_item, slot = _slice(row, "pad_slot", 0x30, f"PAD consume record {index}")
    read = _gpr(payload, 6, f"PAD consume record {index}") & 0xFF
    slot_address = _gpr(payload, 25, f"PAD consume record {index}")
    if queue[0] == 0 or read >= queue[0]:
        _fail(f"PAD consume record {index} has an invalid queue read index")
    queue_base = int.from_bytes(queue[8:12], "big")
    if queue_base + read * 0x30 != slot_address:
        _fail(f"PAD consume record {index} does not identify its source queue slot")
    address = slot_item.get("address")
    if isinstance(address, str):
        try:
            address = int(address, 16)
        except ValueError as error:
            raise WholeSessionReplayError(
                f"PAD consume record {index} has a non-hex slot address") from error
    if address != slot_address:
        _fail(f"PAD consume record {index} slot slice address disagrees with r25")
    return [slot[offset:offset + 11].hex() for offset in range(0, 0x30, 12)]


def _scene_transition(boundary: str, current: str | None) -> str | None:
    if boundary in {"css_enter", "css_cancel_enter", "return_css"}:
        return "css"
    if boundary == "sss_enter":
        return "sss"
    if boundary == "entry":
        return "match"
    if boundary == "results_enter":
        return "results"
    if boundary == "prize_mode_enter":
        return "prize"
    return current


def _timeline(
        records: list[Mapping[str, Any]], match_count: int
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    frames: list[dict[str, Any]] = []
    spans: list[dict[str, Any]] = []
    current_match = 0
    scene: str | None = None
    started = False
    finished = False
    last_seq = -1
    owner_segment = -1
    consumed_steps: set[tuple[int, int]] = set()
    lifecycle_matches = {row["seq"]: row["payload"]["match_index"]
                         for row in _lifecycle_rows(records)}
    for index, row in enumerate(records):
        seq = _integer(row.get("seq"), f"record {index} sequence")
        source_tick = _integer(row.get("source_tick"), f"record {index} source_tick")
        if seq <= last_seq:
            _fail("observer records are out of sequence")
        last_seq = seq
        if row.get("event") != "boundary":
            continue
        payload = row.get("payload")
        if not isinstance(payload, Mapping) or payload.get("whole_session") is not True:
            _fail(f"boundary record {index} lacks whole-session metadata")
        boundary = payload.get("boundary")
        match = _integer(payload.get("match_index"), f"{boundary} match_index", 0, match_count - 1)
        if boundary in LIFECYCLE_NAMES:
            if row["seq"] not in lifecycle_matches:
                _fail("internal lifecycle index is inconsistent")
            if not started:
                if boundary == "css_enter" and match == 0:
                    started = True
                    current_match = 0
                elif boundary in {"prize_mode_enter", "prize_scene_enter",
                                  "prize_scene_exit", "startup_prize_mode_exit"}:
                    continue
                else:
                    _fail(f"source boundary {boundary} preceded first CSS")
            if match != current_match:
                _fail(f"{boundary} match_index does not match the active source session")
            next_scene = _scene_transition(boundary, scene)
            if next_scene is None:
                _fail(f"source boundary {boundary} has no admitted scene owner")
            if next_scene != scene or boundary in OWNER_ENTRY_BOUNDARIES:
                owner_segment += 1
            scene = next_scene
            if scene is None:
                _fail(f"source boundary {boundary} has no admitted scene owner")
            if boundary == "return_css":
                if finished:
                    _fail("source boundary occurred after final return to CSS")
                if current_match + 1 < match_count:
                    current_match += 1
                    scene = "css"
                else:
                    finished = True
            continue
        if boundary == "pad_consume":
            if not started or finished or scene is None:
                # Startup PAD traffic is diagnostic; it is never a replay input.
                if not started:
                    continue
                _fail("PAD was consumed outside the admitted whole-session lifecycle")
            if match != current_match:
                _fail("PAD consume match_index disagrees with the active lifecycle")
            step_key = (owner_segment, source_tick)
            if step_key in consumed_steps:
                _fail(
                    f"multiple PAD consumes share source step {source_tick} "
                    f"within {scene} owner segment")
            consumed_steps.add(step_key)
            pads = _consumed_ports(row, index)
            frame_index = len(frames)
            frames.append({
                "index": frame_index,
                "scene": scene,
                "scene_code": SCENES[scene],
                "source_seq": seq,
                "source_tick": source_tick,
                "pads": pads,
            })
            if spans and spans[-1]["scene"] == SCENES[scene]:
                spans[-1]["last_frame"] = frame_index
            else:
                spans.append({"scene": SCENES[scene], "first_frame": frame_index,
                              "last_frame": frame_index})
    if not started:
        _fail("whole-session stream never entered first CSS")
    if not finished:
        _fail("whole-session stream did not return to CSS after its final match")
    if not frames:
        _fail("whole-session stream contains no source-consumed PAD samples")
    if len(frames) > MAX_FRAMES:
        _fail(f"source-consumed PAD sample count exceeds {MAX_FRAMES}")
    if not 1 <= len(spans) <= MAX_SPANS:
        _fail(f"whole-session span count must be between 1 and {MAX_SPANS}")
    next_frame = 0
    for span in spans:
        if span["first_frame"] != next_frame or span["last_frame"] < span["first_frame"]:
            _fail("whole-session spans are not ordered and contiguous")
        next_frame = span["last_frame"] + 1
    if next_frame != len(frames):
        _fail("whole-session spans do not cover every consumed PAD sample")
    return frames, spans


def capture_from_records(records: Iterable[Mapping[str, Any]], source: str = "<records>") -> dict[str, Any]:
    """Validate and normalize one decoded raw MWRO stream."""
    rows = list(records)
    identity = _capture_identity(rows)
    report = _validate_lifecycle(rows, identity["match_count"])
    first_css = _first_css_context(rows)
    setup_hex, declared_setup = _match_setups(rows, identity["match_count"])
    frames, spans = _timeline(rows, identity["match_count"])
    return {
        "source": source,
        "identity": identity,
        "lifecycle": report,
        "first_css": first_css,
        "setup_hex": setup_hex,
        "declared_setup": declared_setup,
        "frames": frames,
        "spans": spans,
    }


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise WholeSessionReplayError(f"cannot read observer stream {path}: {error}") from error
    return digest.hexdigest()


def capture_from_path(path: str | Path, status_path: str | Path | None = None) -> dict[str, Any]:
    candidate = Path(path).expanduser().resolve()
    try:
        rows = list(iter_records(candidate))
    except Exception as error:
        raise WholeSessionReplayError(f"cannot decode raw observer stream {candidate}: {error}") from error
    capture = capture_from_records(rows, str(candidate))
    capture["raw_sha256"] = _sha256_file(candidate)
    if status_path is not None:
        try:
            status = read_status(Path(status_path))
        except Exception as error:
            raise WholeSessionReplayError(f"cannot validate observer status: {error}") from error
        if status["state"] != "completed" or not status["completed"] or status["invalid"]:
            _fail("observer status does not report a completed valid capture")
        if status["event_count"] != len(rows) or status["last_seq"] != rows[-1]["seq"]:
            _fail("observer status counters disagree with the raw stream")
        capture["status"] = status
    return capture


def _input_bytes(capture: Mapping[str, Any]) -> bytes:
    return b"".join(
        _hex_bytes(pad, PAD_SEMANTIC_SIZE, f"frame {frame['index']} port")
        for frame in capture["frames"] for pad in frame["pads"])


def _recipe_key(capture: Mapping[str, Any]) -> tuple[Any, ...]:
    first_css = capture["first_css"]
    frames = tuple((frame["scene_code"], tuple(frame["pads"])) for frame in capture["frames"])
    spans = tuple((span["scene"], span["first_frame"], span["last_frame"])
                  for span in capture["spans"])
    return (
        capture["setup_hex"],
        first_css["rng"],
        first_css["pad_state_hex"],
        first_css["profile_masks"]["characters"],
        first_css["profile_masks"]["stages"],
        first_css["profile_game_rules_sha256"],
        first_css["profile_save_data_sha256"],
        first_css["game_rules_hex"],
        first_css["save_data_hex"],
        first_css["css_data_hex"],
        first_css["ko_counts_hex"],
        frames,
        spans,
    )


def encode_v8(capture: Mapping[str, Any]) -> tuple[bytes, dict[str, Any]]:
    """Encode one checked normalized capture as MWRC v8."""
    frames = capture["frames"]
    spans = capture["spans"]
    if not 1 <= len(frames) <= MAX_FRAMES:
        _fail(f"frame count must be between 1 and {MAX_FRAMES}")
    if not 1 <= len(spans) <= MAX_SPANS:
        _fail(f"span count must be between 1 and {MAX_SPANS}")
    setup = _hex_bytes(capture["setup_hex"], GAME_INFO_SIZE, "source setup")
    initial_pad = _hex_bytes(capture["first_css"]["pad_state_hex"], PAD_STATE_BYTES,
                              "first CSS semantic PAD state")
    try:
        semantics._pad_state(initial_pad.hex(), "first CSS semantic PAD state")
    except (TypeError, ValueError) as error:
        raise WholeSessionReplayError(f"first CSS semantic PAD state is invalid: {error}") from error
    seed = _integer(capture["first_css"]["rng"], "first CSS RNG")
    characters = _integer(capture["first_css"]["profile_masks"]["characters"],
                          "unlocked character mask", 0, 0xFFFF)
    stages = _integer(capture["first_css"]["profile_masks"]["stages"],
                      "unlocked stage mask", 0, 0xFFFF)
    game_rules = _hex_bytes(capture["first_css"].get("game_rules_hex"),
                            semantics.GAME_RULES_SIZE, "first CSS GameRules")
    save_data = _hex_bytes(capture["first_css"].get("save_data_hex"),
                           semantics.SAVE_DATA_SIZE, "first CSS SaveData")
    css_data = _hex_bytes(capture["first_css"].get("css_data_hex"),
                          CSS_DATA_SIZE, "first CSS CSSData")
    ko_counts = _hex_bytes(capture["first_css"].get("ko_counts_hex"),
                           KO_COUNTS_SIZE, "first CSS KO counts")
    input_bytes = _input_bytes(capture)
    if len(input_bytes) != len(frames) * FRAME_INPUT_SIZE:
        _fail("source-consumed PAD bytes do not match frame count")
    span_bytes = bytearray(struct.pack(">H", len(spans)))
    next_frame = 0
    for span in spans:
        scene = _integer(span["scene"], "span scene", 1, 5)
        first = _integer(span["first_frame"], "span first_frame", 0, len(frames) - 1)
        last = _integer(span["last_frame"], "span last_frame", first, len(frames) - 1)
        if first != next_frame:
            _fail("whole-session spans are not contiguous at encoding")
        next_frame = last + 1
        span_bytes += SPAN.pack(scene, 0, 0, first, last)
    if next_frame != len(frames):
        _fail("whole-session spans do not cover every frame at encoding")
    payload = bytearray(HEADER.pack(MAGIC, MWRC_VERSION, seed, len(frames),
                                    characters, stages))
    payload += CONTEXT_HEADER.pack(CONTEXT_VERSION, 0, CONTEXT_BYTES)
    payload += (game_rules + save_data + css_data + ko_counts + setup +
                initial_pad + input_bytes + span_bytes)
    expected = (HEADER.size + CONTEXT_HEADER.size + CONTEXT_BYTES + GAME_INFO_SIZE +
                PAD_STATE_BYTES + len(input_bytes) + len(span_bytes))
    if len(payload) != expected:
        _fail("generated MWRC v8 size does not match its source timeline")
    return bytes(payload), {
        "version": MWRC_VERSION,
        "seed": seed,
        "frame_count": len(frames),
        "input_bytes_sha256": hashlib.sha256(input_bytes).hexdigest(),
        "output_sha256": hashlib.sha256(payload).hexdigest(),
    }


def encode_v7(capture: Mapping[str, Any]) -> tuple[bytes, dict[str, Any]]:
    """Reject the provisional whole-session format explicitly.

    Keeping this symbol makes callers fail at the producer boundary instead of
    accidentally writing a payload that the v8 consumer must ignore.
    """
    del capture
    _fail("MWRC v7 cannot carry first-CSS source context; use encode_v8")


def _output_paths(output_path: str | Path, sidecar_path: str | Path | None,
                  sources: Iterable[Path]) -> tuple[Path, Path | None]:
    output = Path(output_path).expanduser().resolve()
    sidecar = Path(sidecar_path).expanduser().resolve() if sidecar_path else None
    source_set = set(sources)
    if output in source_set or sidecar in source_set:
        _fail("MWRC output paths must be separate from the raw observer streams")
    if sidecar is not None and sidecar == output:
        _fail("MWRC output and provenance sidecar must be different paths")
    if output.exists():
        _fail(f"MWRC output already exists: {output}")
    if sidecar is not None and sidecar.exists():
        _fail(f"MWRC provenance sidecar already exists: {sidecar}")
    output.parent.mkdir(parents=True, exist_ok=True)
    if sidecar is not None:
        sidecar.parent.mkdir(parents=True, exist_ok=True)
    return output, sidecar


def _write_result(payload: bytes, result: dict[str, Any], output: Path,
                  sidecar: Path | None) -> dict[str, Any]:
    output.write_bytes(payload)
    if sidecar is not None:
        sidecar.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")
    return result


def _capture_report(capture: Mapping[str, Any],
                    transport: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "setup_hex": capture["setup_hex"],
        "declared_setup": capture["declared_setup"],
        "first_css": {
            key: capture["first_css"][key]
            for key in ("rng", "pad_state_hex", "profile_masks",
                        "game_rules_hex", "save_data_hex", "css_data_hex",
                        "ko_counts_hex",
                        "profile_game_rules_sha256", "profile_save_data_sha256",
                        "observer_seq", "source_tick")
        },
        "profile_context": capture["first_css"]["profile_context"],
        "spans": capture["spans"],
        "capture": {"path": capture["source"], "raw_sha256": capture.get("raw_sha256"),
                    **capture["identity"]},
        "transport": transport,
    }


def export_single(capture_path: str | Path, output_path: str | Path,
                  sidecar_path: str | Path | None = None,
                  status_path: str | Path | None = None) -> dict[str, Any]:
    """Write a valid v8 workload from one complete source capture.

    This mode proves transport completeness and source ownership only. It
    intentionally does not claim independent execution or repeatability.
    """
    capture = capture_from_path(capture_path, status_path)
    payload, transport = encode_v8(capture)
    source = Path(capture_path).expanduser().resolve()
    output, sidecar = _output_paths(output_path, sidecar_path, (source,))
    result = {
        "schema": SCHEMA,
        "version": SCHEMA_VERSION,
        "scope": SCOPE,
        "transport": transport,
        "input": _capture_report(capture, transport),
        "claims": {
            "source_consumed_workload": "pass",
            "reference_repeatability": "not_evaluated",
            "source_consumed_pad_repeatability": "not_evaluated",
            "independent_execution_identity": "not_evaluated",
            "runtime_initial_css_context": RUNTIME_CONTEXT_STATUS,
            "port_equivalence": "not_evaluated",
            "performance": "not_evaluated",
            "pixels": "not_evaluated",
            "audio_pcm": "not_evaluated",
            "cpu_decisions_as_inputs": "excluded",
        },
        "output": {"path": str(output), "sha256": transport["output_sha256"]},
    }
    return _write_result(payload, result, output, sidecar)


def export_pair(first_path: str | Path, second_path: str | Path, output_path: str | Path,
                sidecar_path: str | Path | None = None,
                status_a: str | Path | None = None,
                status_b: str | Path | None = None) -> dict[str, Any]:
    """Validate two independent streams and write a v8 recipe plus provenance."""
    first = Path(first_path).expanduser().resolve()
    second = Path(second_path).expanduser().resolve()
    if first == second:
        _fail("capture A and capture B resolve to the same path")
    capture_a = capture_from_path(first, status_a)
    capture_b = capture_from_path(second, status_b)
    if capture_a["raw_sha256"] == capture_b["raw_sha256"]:
        _fail("capture A and capture B have identical raw stream bytes")
    if capture_a["identity"]["capture_id"] == capture_b["identity"]["capture_id"]:
        _fail("capture A and capture B reuse the same capture_id")
    if capture_a["identity"]["match_count"] != capture_b["identity"]["match_count"]:
        _fail("capture A and capture B declare different whole-session match counts")
    if _recipe_key(capture_a) != _recipe_key(capture_b):
        _fail("independent whole-session streams are not source-consumed repeatable")
    payload, transport = encode_v8(capture_a)
    output, sidecar = _output_paths(output_path, sidecar_path, (first, second))
    input_report = _capture_report(capture_a, transport)
    input_report["capture_a"] = input_report.pop("capture")
    input_report["capture_b"] = _capture_report(capture_b, transport)["capture"]
    result = {
        "schema": SCHEMA,
        "version": SCHEMA_VERSION,
        "scope": SCOPE,
        "transport": transport,
        "input": input_report,
        "claims": {
            "source_consumed_workload": "pass",
            "reference_repeatability": "pass",
            "source_consumed_pad_repeatability": "pass",
            "independent_execution_identity": "checked_by_distinct_ids_and_bytes",
            "runtime_initial_css_context": RUNTIME_CONTEXT_STATUS,
            "port_equivalence": "not_evaluated",
            "performance": "not_evaluated",
            "pixels": "not_evaluated",
            "audio_pcm": "not_evaluated",
            "cpu_decisions_as_inputs": "excluded",
        },
        "output": {"path": str(output), "sha256": transport["output_sha256"]},
    }
    return _write_result(payload, result, output, sidecar)


def _main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_a", type=Path, help="first raw MWRO stream")
    parser.add_argument("capture_b", nargs="?", type=Path, help="second raw MWRO stream")
    parser.add_argument("--single", action="store_true",
                        help="export one complete stream without a repeatability claim")
    parser.add_argument("--output", required=True, type=Path, help="new MWRC v8 output path")
    parser.add_argument("--sidecar", type=Path, help="new JSON provenance sidecar path")
    parser.add_argument("--status-a", type=Path, help="observer status for capture A")
    parser.add_argument("--status-b", type=Path, help="observer status for capture B")
    args = parser.parse_args(argv)
    try:
        if args.single:
            if args.capture_b is not None:
                parser.error("--single accepts exactly one capture path")
            result = export_single(args.capture_a, args.output, args.sidecar, args.status_a)
        else:
            if args.capture_b is None:
                parser.error("pair mode requires capture A and capture B; use --single for one")
            result = export_pair(args.capture_a, args.capture_b, args.output, args.sidecar,
                                 args.status_a, args.status_b)
    except (OSError, WholeSessionReplayError) as error:
        parser.error(str(error))
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main(sys.argv[1:]))
