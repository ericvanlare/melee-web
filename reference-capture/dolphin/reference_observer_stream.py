"""Strict reader for the passive Dolphin reference observer stream.

The observer writes little-endian, length-delimited records.  The stream is an
immutable diagnostic input: malformed, truncated, reordered, or checksum-failed
records are errors rather than records to skip.  The producer lives on Dolphin's
CPU thread and a dedicated writer drains its bounded ring, so a valid stream
never hides an overflow or write failure.
"""

from __future__ import annotations

import json
from pathlib import Path
import struct
import zlib
from typing import Any, Iterator


MAGIC = b"MWRO"
MAGIC_U32 = int.from_bytes(MAGIC, "little")
SCHEMA_VERSION = 1
HEADER = struct.Struct("<IHHQQIIIII")
BOUNDARY = struct.Struct("<HHIIII")
SLICE = struct.Struct("<HHIII")
WHOLE_SESSION_FLAG = 1
WHOLE_METADATA = struct.Struct("<HHI")
# Version 1 whole-session records retain the original eight-byte tail.  The
# extended tail carries the source audio owner epoch while keeping the old
# decoder shape readable for preserved diagnostic streams.
WHOLE_AUDIO_METADATA = struct.Struct("<HHII")
MAX_PAYLOAD = 1024 * 1024
MAX_SLICES = 256
MAX_GPRS = 32

EVENT_NAMES = {
    1: "handshake",
    2: "start",
    3: "boundary",
    4: "progress",
    5: "error",
    6: "end",
}
BOUNDARY_NAMES = {
    1: "pad_poll",
    2: "pad_consume",
    3: "fighter_create",
    4: "entry",
    5: "setup",
    6: "source_tick",
    7: "draw_enter",
    8: "draw_return",
    9: "result_enter",
    10: "result_return",
    11: "scene_teardown",
    12: "scene_exit",
    13: "css_enter",
    14: "css_exit",
    15: "sss_enter",
    16: "sss_exit",
    17: "vs_exit",
    18: "vs_exit_return",
    19: "vs_mode_exit",
    20: "results_enter",
    21: "results_exit",
    22: "results_mode_exit",
    23: "results_gobj",
    24: "return_css",
    25: "css_cancel_enter",
    26: "prize_mode_enter",
    27: "prize_scene_enter",
    28: "prize_scene_exit",
    29: "prize_mode_exit",
    30: "startup_prize_mode_exit",
}

# Keep these labels stable: they are the semantic memory names shared by the
# offline adapter and the existing retail_cpu_observation.py implementation.
SLICE_NAMES = {
    1: "pad_status_all4",
    2: "pad_queue",
    3: "pad_slot",
    4: "match_setup",
    5: "fighter_head",
    6: "fighter_input_anim",
    7: "fighter_damage_shield",
    8: "fighter_stocks",
    9: "cpu_state",
    10: "camera",
    11: "camera_projection",
    12: "hud",
    13: "magnifier",
    14: "match_clock",
    15: "result",
    16: "scene_entity_heads",
    17: "scene_routing",
    18: "fighter_subject",
    19: "rng_pointer",
    20: "rng_value",
    21: "pad_snapshot",
    22: "retrace_count",
    23: "source_vi_count",
    24: "fighter_create_context",
    25: "scene_entity_count",
    26: "scene_entity_heads_pointer",
    27: "pad_poll_caller",
    28: "camera_object_pointer",
    29: "scene_request",
    30: "scene_frame",
    31: "menu_css_state",
    32: "menu_sss_state",
    33: "menu_audio",
    34: "menu_audio_voice",
    35: "menu_sss_route",
    36: "profile_characters",
    37: "profile_stages",
    38: "profile_game_rules",
    39: "profile_save_data",
    40: "scene_kind",
    41: "stage_select_index",
    42: "stage_select_kind",
    43: "menu_css_cursor",
    44: "menu_css_doors",
    45: "menu_main_flow",
    46: "menu_main_input",
    47: "menu_css_model",
    48: "menu_css_live_state",
    49: "menu_css_slider",
}


class ObserverStreamError(ValueError):
    """The stream cannot be admitted as a complete raw observer input."""


def _json_payload(raw: bytes, context: str) -> dict[str, Any]:
    try:
        value = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ObserverStreamError(f"{context}: invalid JSON payload") from exc
    if not isinstance(value, dict):
        raise ObserverStreamError(f"{context}: payload must be a JSON object")
    return value


def _boundary_payload(raw: bytes, pc: int, source_tick: int,
                     draw_ordinal: int, context: str) -> dict[str, Any]:
    if len(raw) < BOUNDARY.size:
        raise ObserverStreamError(f"{context}: truncated boundary metadata")
    kind, flags, lr, gpr_count, slice_count, reserved = BOUNDARY.unpack_from(raw)
    if reserved != 0:
        raise ObserverStreamError(f"{context}: nonzero boundary reserved field")
    if flags & ~WHOLE_SESSION_FLAG:
        raise ObserverStreamError(f"{context}: unknown boundary flags")
    if kind >= 13 and not flags & WHOLE_SESSION_FLAG:
        raise ObserverStreamError(f"{context}: whole-session boundary lacks its opt-in flag")
    if kind not in BOUNDARY_NAMES:
        raise ObserverStreamError(f"{context}: unknown boundary kind {kind}")
    if gpr_count != MAX_GPRS:
        raise ObserverStreamError(f"{context}: expected {MAX_GPRS} GPRs, got {gpr_count}")
    if slice_count > MAX_SLICES:
        raise ObserverStreamError(f"{context}: slice count exceeds bound")
    gpr_end = BOUNDARY.size + 4 * gpr_count
    descriptor_end = gpr_end + SLICE.size * slice_count
    if descriptor_end > len(raw):
        raise ObserverStreamError(f"{context}: truncated slice descriptors")
    payload_end = len(raw)
    match_index = None
    whole_boundary_kind = None
    audio_owner_epoch = None
    if flags & WHOLE_SESSION_FLAG:
        if len(raw) < descriptor_end + WHOLE_METADATA.size:
            raise ObserverStreamError(f"{context}: truncated whole-session metadata")
        # Prefer the extended form only when its kind and reserved word are
        # valid.  This makes the old format unambiguous and preserves old
        # captures as explicit, incomplete evidence in the semantic adapter.
        extended_end = len(raw) - WHOLE_AUDIO_METADATA.size
        if extended_end >= descriptor_end:
            candidate_match, candidate_kind, candidate_epoch, candidate_reserved = (
                WHOLE_AUDIO_METADATA.unpack_from(raw, extended_end))
            if candidate_kind == kind and candidate_reserved == 0:
                payload_end = extended_end
                match_index = candidate_match
                whole_boundary_kind = candidate_kind
                audio_owner_epoch = candidate_epoch
        if match_index is None:
            payload_end = len(raw) - WHOLE_METADATA.size
            match_index, whole_boundary_kind, metadata_reserved = WHOLE_METADATA.unpack_from(
                raw, payload_end)
            if metadata_reserved != 0 or whole_boundary_kind != kind:
                raise ObserverStreamError(f"{context}: invalid whole-session metadata")
        elif audio_owner_epoch is None:
            raise ObserverStreamError(f"{context}: invalid whole-session metadata")
    gprs = list(struct.unpack_from(f"<{gpr_count}I", raw, BOUNDARY.size))
    slices: list[dict[str, Any]] = []
    previous_end = descriptor_end
    for index in range(slice_count):
        offset = gpr_end + index * SLICE.size
        tag, slice_flags, address, size, data_offset = SLICE.unpack_from(raw, offset)
        if tag not in SLICE_NAMES:
            raise ObserverStreamError(f"{context}: unknown slice tag {tag}")
        if size == 0 or data_offset < descriptor_end or data_offset + size > payload_end:
            raise ObserverStreamError(f"{context}: invalid slice range")
        # The producer packs bytes in descriptor order.  Reject overlap and
        # hidden holes so a decoder cannot silently reinterpret omitted bytes.
        if data_offset < previous_end:
            raise ObserverStreamError(f"{context}: overlapping or reordered slices")
        if data_offset != previous_end:
            raise ObserverStreamError(f"{context}: slice payload has an unexplained gap")
        previous_end = data_offset + size
        slices.append({
            "name": SLICE_NAMES[tag],
            "tag": tag,
            "flags": slice_flags,
            "address": address,
            "size": size,
            "hex": raw[data_offset:previous_end].hex(),
        })
    if previous_end != payload_end:
        raise ObserverStreamError(f"{context}: trailing boundary payload bytes")
    decoded = {
        "pc": pc,
        "lr": lr,
        "gprs": gprs,
        "boundary": BOUNDARY_NAMES[kind],
        "boundary_kind": kind,
        "flags": flags,
        "source_tick": source_tick,
        "draw_ordinal": draw_ordinal,
        "slices": slices,
    }
    if flags & WHOLE_SESSION_FLAG:
        decoded.update({"whole_session": True, "match_index": match_index,
                        "whole_boundary_kind": whole_boundary_kind})
        if audio_owner_epoch is not None:
            decoded["audio_owner_epoch"] = audio_owner_epoch
    return decoded


def _decode_record(header: tuple[int, ...], payload: bytes, context: str) -> dict[str, Any]:
    (magic, version, event_code, sequence, timestamp_ns, pc, source_tick,
     draw_ordinal, payload_size, checksum) = header
    if magic != MAGIC_U32:
        raise ObserverStreamError(f"{context}: bad magic")
    if version != SCHEMA_VERSION:
        raise ObserverStreamError(f"{context}: unsupported schema {version}")
    event = EVENT_NAMES.get(event_code)
    if event is None:
        raise ObserverStreamError(f"{context}: unknown event {event_code}")
    if payload_size != len(payload) or payload_size > MAX_PAYLOAD:
        raise ObserverStreamError(f"{context}: invalid payload length")
    if zlib.crc32(payload) & 0xFFFFFFFF != checksum:
        raise ObserverStreamError(f"{context}: payload checksum mismatch")
    if event == "boundary":
        decoded = _boundary_payload(payload, pc, source_tick, draw_ordinal, context)
    elif event in ("handshake", "start", "progress", "error", "end"):
        decoded = _json_payload(payload, context)
    else:  # guarded by EVENT_NAMES above
        raise ObserverStreamError(f"{context}: unsupported event")
    return {
        "seq": sequence,
        "event": event,
        "timestamp_ns": timestamp_ns,
        "source_tick": source_tick,
        "draw_ordinal": draw_ordinal,
        "payload": decoded,
    }


def iter_records(path: str | Path) -> Iterator[dict[str, Any]]:
    """Yield every record, rejecting any malformed or incomplete stream."""

    stream_path = Path(path)
    try:
        stream = stream_path.open("rb")
    except OSError as exc:
        raise ObserverStreamError(f"cannot open observer stream {stream_path}: {exc}") from exc
    previous_sequence = None
    whole_session_announced = False
    whole_session_count = None
    with stream:
        index = 0
        while True:
            raw_header = stream.read(HEADER.size)
            if not raw_header:
                break
            context = f"record {index}"
            if len(raw_header) != HEADER.size:
                raise ObserverStreamError(f"{context}: truncated header")
            header = HEADER.unpack(raw_header)
            payload_size = header[8]
            if payload_size > MAX_PAYLOAD:
                raise ObserverStreamError(f"{context}: payload exceeds bound")
            payload = stream.read(payload_size)
            if len(payload) != payload_size:
                raise ObserverStreamError(f"{context}: truncated payload")
            sequence = header[3]
            if previous_sequence is not None and sequence != previous_sequence + 1:
                raise ObserverStreamError(
                    f"{context}: sequence gap/reorder ({previous_sequence} -> {sequence})")
            previous_sequence = sequence
            decoded = _decode_record(header, payload, context)
            if decoded["event"] in {"handshake", "start"}:
                announcement = decoded["payload"]
                if announcement.get("whole_session") is True:
                    count = announcement.get("match_count")
                    if type(count) is not int or not 3 <= count <= 64:
                        raise ObserverStreamError(
                            f"{context}: invalid whole-session match_count")
                    if whole_session_count is not None and count != whole_session_count:
                        raise ObserverStreamError(
                            f"{context}: whole-session match_count disagrees with prior announcement")
                    whole_session_count = count
                    whole_session_announced = True
            elif decoded["event"] == "boundary":
                boundary_payload = decoded["payload"]
                if boundary_payload.get("whole_session") and not whole_session_announced:
                    raise ObserverStreamError(
                        f"{context}: whole-session boundary precedes opt-in announcement")
            yield decoded
            index += 1


def read_status(path: str | Path) -> dict[str, Any]:
    """Read the supervisor status sidecar and validate its public shape."""

    try:
        value = json.loads(Path(path).read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ObserverStreamError(f"cannot read observer status: {exc}") from exc
    if not isinstance(value, dict):
        raise ObserverStreamError("observer status must be a JSON object")
    required = {"state", "event_count", "last_seq", "source_tick", "draw_ordinal",
                "completed", "invalid", "error"}
    missing = sorted(required - value.keys())
    if missing:
        raise ObserverStreamError(f"observer status missing fields: {', '.join(missing)}")
    if value["state"] not in {"starting", "recording", "completed", "invalid", "interrupted"}:
        raise ObserverStreamError("observer status has an unknown state")
    if not isinstance(value["event_count"], int) or value["event_count"] < 0:
        raise ObserverStreamError("observer status event_count is invalid")
    if not isinstance(value["last_seq"], int) or value["last_seq"] < -1:
        raise ObserverStreamError("observer status last_seq is invalid")
    if not isinstance(value["source_tick"], int) or not 0 <= value["source_tick"] <= 0xFFFFFFFF:
        raise ObserverStreamError("observer status source_tick is invalid")
    if not isinstance(value["draw_ordinal"], int) or not 0 <= value["draw_ordinal"] <= 0xFFFFFFFF:
        raise ObserverStreamError("observer status draw_ordinal is invalid")
    if not isinstance(value["completed"], bool) or not isinstance(value["invalid"], bool):
        raise ObserverStreamError("observer status completion flags are invalid")
    if value["completed"] != (value["state"] == "completed"):
        raise ObserverStreamError("completed status flag disagrees with state")
    if value["invalid"] and value["state"] != "invalid":
        raise ObserverStreamError("invalid status must use state=invalid")
    if not (value["error"] is None or isinstance(value["error"], str)):
        raise ObserverStreamError("observer status error is invalid")
    return value
