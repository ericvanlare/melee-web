"""Strict, privacy-minimized decoding for Slippi corpus admission.

This module intentionally starts with the Game Start event. It does not claim
to be a replacement for the official slippi-js event parser; later replay
normalization is cross-checked against that pinned implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import struct
from typing import BinaryIO


SCHEMA = "melee-web-slippi-corpus"
SCHEMA_VERSION = 1
SPEC_URL = "https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md"
RAW_PREFIX = b"{U\x03raw[$U#l"
RAW_OFFSET = 15
GAME_START = 0x36
PRE_FRAME = 0x37
POST_FRAME = 0x38
GAME_END = 0x39
FRAME_START = 0x3A
FRAME_BOOKEND = 0x3C
EVENT_PAYLOADS = 0x35
GAME_INFO_SIZE = 0x138
FIRST_FRAME = -123


class SlippiFormatError(ValueError):
    """The file cannot supply a trustworthy Slippi Game Start record."""


def _version_at_least(version: tuple[int, int, int, int], expected: tuple[int, ...]):
    return version[:len(expected)] >= expected


def _u32(data: bytes, offset: int):
    if offset < 0 or offset + 4 > len(data):
        raise SlippiFormatError(f"truncated uint32 at 0x{offset:x}")
    return struct.unpack_from(">I", data, offset)[0]


def _u16(data: bytes, offset: int):
    if offset < 0 or offset + 2 > len(data):
        raise SlippiFormatError(f"truncated uint16 at 0x{offset:x}")
    return struct.unpack_from(">H", data, offset)[0]


def _i32(data: bytes, offset: int):
    if offset < 0 or offset + 4 > len(data):
        raise SlippiFormatError(f"truncated int32 at 0x{offset:x}")
    return struct.unpack_from(">i", data, offset)[0]


def _i8(data: bytes, offset: int):
    if offset < 0 or offset + 1 > len(data):
        raise SlippiFormatError(f"truncated int8 at 0x{offset:x}")
    return struct.unpack_from("b", data, offset)[0]


def _float_bits(data: bytes, offset: int):
    return _u32(data, offset)


def _float(data: bytes, offset: int):
    if offset < 0 or offset + 4 > len(data):
        raise SlippiFormatError(f"truncated float at 0x{offset:x}")
    value = struct.unpack_from(">f", data, offset)[0]
    if not math.isfinite(value):
        raise SlippiFormatError(f"non-finite float at 0x{offset:x}")
    return value


_TRIGGER_RAW_BY_BITS = {
    struct.unpack(">I", struct.pack(">f", value / 140.0))[0]: value
    for value in range(141)
}


def _normalized_trigger_raw(bits: int):
    """Invert Melee's clamped trigger byte / 140.0f when it is exact."""
    return _TRIGGER_RAW_BY_BITS.get(bits)


def _event_has(event: bytes, offset: int, width: int = 1):
    """Offsets include the event command, matching the published Slippi spec."""
    return offset >= 1 and offset + width <= len(event)


def _require_event(event: bytes, offset: int, width: int, name: str):
    if not _event_has(event, offset, width):
        raise SlippiFormatError(f"{name} event is missing field at 0x{offset:x}")


def _fix_name(dashback: int, shield_drop: int):
    if dashback == shield_drop == 0:
        return "none"
    if dashback == shield_drop == 1:
        return "ucf"
    if dashback == shield_drop == 2:
        return "dween"
    return "mixed_or_unknown"


@dataclass(frozen=True)
class SlippiHeader:
    raw_length: int
    game_start_payload_size: int
    version: tuple[int, int, int, int]
    game_info: bytes
    random_seed: int
    players: tuple[dict, ...]
    pal: bool | None
    frozen_stadium: bool | None
    minor_scene: int | None
    major_scene: int | None
    event_payload_sizes: dict[int, int]

    def classification(self):
        reasons = []
        if self.pal is None:
            reasons.append("PAL flag unavailable before Slippi 1.5.0")
        elif self.pal:
            reasons.append("PAL recording")
        for player in self.players:
            if player["controller_fix"] != "none":
                reasons.append(
                    f"port {player['port']} controller fix is "
                    f"{player['controller_fix']}")
        if self.frozen_stadium:
            reasons.append("Frozen Stadium enabled")
        if self.major_scene is not None and self.major_scene != 2:
            reasons.append(f"major scene {self.major_scene} is not offline VS mode")
        # Version 3.17 introduced the fields in the format specification, but
        # official 3.17 fixtures declare a 0x40-byte Pre-Frame payload and omit
        # both raw C-stick bytes. Inspect the payload, not the version label.
        raw_pad = self.event_payload_sizes.get(PRE_FRAME, 0) >= 0x42
        return {
            "vanilla_profile_candidate": not reasons,
            "vanilla_rejection_reasons": reasons,
            "raw_pad_fields_complete": raw_pad,
            "input_evidence": "physical_pad_candidate" if raw_pad else "processed_only",
        }

    def record(self):
        version = ".".join(str(value) for value in self.version[:3])
        return {
            "slippi_version": version,
            "raw_length": self.raw_length,
            "game_start_payload_size": self.game_start_payload_size,
            "game_info_hex": self.game_info.hex(),
            "random_seed": self.random_seed,
            "stage_id": _u16(self.game_info, 0xE),
            "is_teams": bool(self.game_info[8]),
            "item_spawn_frequency": self.game_info[0xB] - 256
                if self.game_info[0xB] >= 128 else self.game_info[0xB],
            "players": list(self.players),
            "pal": self.pal,
            "frozen_stadium": self.frozen_stadium,
            "minor_scene": self.minor_scene,
            "major_scene": self.major_scene,
            "event_payloads": {
                f"0x{command:02x}": size
                for command, size in sorted(self.event_payload_sizes.items())
            },
            "classification": self.classification(),
        }


@dataclass(frozen=True)
class SlippiPreFrame:
    frame: int
    port: int
    is_follower: bool
    random_seed: int
    action_state: int
    position_bits: tuple[int, int]
    facing_bits: int
    processed_stick_bits: tuple[int, int]
    processed_cstick_bits: tuple[int, int]
    processed_trigger_bits: int
    processed_buttons: int
    physical_buttons: int | None = None
    physical_trigger_bits: tuple[int | None, int | None] = (None, None)
    raw_stick: tuple[int | None, int | None] = (None, None)
    raw_cstick: tuple[int | None, int | None] = (None, None)

    @property
    def physical_complete(self):
        return (self.physical_buttons is not None
                and None not in (*self.raw_stick, *self.raw_cstick,
                                 *self.physical_trigger_bits)
                and all(_normalized_trigger_raw(value) is not None
                        for value in self.physical_trigger_bits))

    def record(self):
        bits = lambda value: f"0x{value:08x}"
        return {
            "port": self.port,
            "follower": self.is_follower,
            "pre_random_seed": self.random_seed,
            "pre_action_state": self.action_state,
            "processed": {
                "stick_bits": [bits(value) for value in self.processed_stick_bits],
                "cstick_bits": [bits(value) for value in self.processed_cstick_bits],
                "trigger_bits": bits(self.processed_trigger_bits),
                "buttons": self.processed_buttons,
            },
            "physical": {
                "buttons": self.physical_buttons,
                "trigger_bits": [
                    None if value is None else bits(value)
                    for value in self.physical_trigger_bits
                ],
                "trigger_bytes": [
                    None if value is None else _normalized_trigger_raw(value)
                    for value in self.physical_trigger_bits
                ],
                "stick": list(self.raw_stick),
                "cstick": list(self.raw_cstick),
                "unobserved_pad_fields": ["analogA", "analogB"],
                "complete": self.physical_complete,
            },
        }

    def reconstructed_pad(self):
        """Return the gameplay-relevant raw PADStatus fields.

        Slippi records the four raw axes and constant button bits. Its two
        trigger floats come from Melee's clamped byte divided by 140. Analog
        A/B pressure is not recorded; the original gameplay source never reads
        those pressure fields, so they remain explicit unknowns in `record()`.
        """
        triggers = tuple(_normalized_trigger_raw(value)
                         if value is not None else None
                         for value in self.physical_trigger_bits)
        if (self.physical_buttons is None or
                None in (*self.raw_stick, *self.raw_cstick, *triggers)):
            raise SlippiFormatError(
                f"frame {self.frame} port {self.port} cannot reconstruct raw PAD input")
        return {
            "buttons": self.physical_buttons,
            "stick": self.raw_stick,
            "cstick": self.raw_cstick,
            "triggers": triggers,
        }


@dataclass(frozen=True)
class SlippiPostFrame:
    frame: int
    port: int
    is_follower: bool
    character_id: int
    action_state: int
    position_bits: tuple[int, int]
    facing_bits: int
    percent_bits: int
    shield_bits: int
    stocks: int | None
    state_flags: tuple[int, ...]
    ground_or_air: int | None
    last_ground_id: int | None
    jumps_remaining: int | None
    hurtbox_state: int | None
    action_state_counter_bits: int | None = None
    misc_action_state_bits: int | None = None
    l_cancel_status: int | None = None

    def record(self):
        bits = lambda value: f"0x{value:08x}"
        return {
            "port": self.port,
            "follower": self.is_follower,
            "character_id": self.character_id,
            "action_state": self.action_state,
            "position_bits": [bits(value) for value in self.position_bits],
            "facing_bits": bits(self.facing_bits),
            "percent_bits": bits(self.percent_bits),
            "shield_bits": bits(self.shield_bits),
            "stocks": self.stocks,
            "state_flags": list(self.state_flags),
            "ground_or_air": self.ground_or_air,
            "last_ground_id": self.last_ground_id,
            "jumps_remaining": self.jumps_remaining,
            "hurtbox_state": self.hurtbox_state,
            "action_state_counter_bits":
                None if self.action_state_counter_bits is None
                else bits(self.action_state_counter_bits),
            "misc_action_state_bits":
                None if self.misc_action_state_bits is None
                else bits(self.misc_action_state_bits),
            "l_cancel_status": self.l_cancel_status,
        }


@dataclass(frozen=True)
class SlippiFrame:
    number: int
    start_random_seed: int | None
    scene_frame_counter: int | None
    inputs: tuple[SlippiPreFrame, ...]
    expected: tuple[SlippiPostFrame, ...]

    def record(self):
        return {
            "frame": self.number,
            "start_random_seed": self.start_random_seed,
            "scene_frame_counter": self.scene_frame_counter,
            "inputs": [value.record() for value in self.inputs],
            "expected": [value.record() for value in self.expected],
        }


@dataclass(frozen=True)
class SlippiTimeline:
    header: SlippiHeader
    frames: tuple[SlippiFrame, ...]
    finalized_through: int | None
    duplicate_updates: int
    game_end_method: int | None
    lras_initiator: int | None

    def classification(self):
        reasons = list(self.header.classification()["vanilla_rejection_reasons"])
        leaders = [value for frame in self.frames for value in frame.inputs
                   if not value.is_follower]
        if not leaders or not all(value.physical_complete for value in leaders):
            reasons.append("physical PAD axes are incomplete")
        if any(value.is_follower for frame in self.frames for value in frame.inputs):
            reasons.append("follower inputs require a later replay profile")
        if len(self.header.players) != 2 or self.header.record()["is_teams"]:
            reasons.append("first replay profile requires two-player singles")
        if self.game_end_method is None:
            reasons.append("Game End event is unavailable")
        return {
            "profile": "gale01-revision-2-vanilla-singles-v1",
            "exact_input_candidate": not reasons,
            "rejection_reasons": reasons,
        }

    def record(self):
        return {
            "schema": "melee-web-normalized-replay",
            "schema_version": 1,
            "match": self.header.record(),
            "first_frame": self.frames[0].number if self.frames else None,
            "last_frame": self.frames[-1].number if self.frames else None,
            "frame_count": len(self.frames),
            "finalized_through": self.finalized_through,
            "duplicate_updates": self.duplicate_updates,
            "game_end_method": self.game_end_method,
            "lras_initiator": self.lras_initiator,
            "classification": self.classification(),
            "frames": [frame.record() for frame in self.frames],
        }


def decode_header(data: bytes):
    """Decode a completed `.slp` prefix containing the full Game Start event."""
    if not isinstance(data, bytes):
        raise TypeError("Slippi input must be bytes")
    if len(data) < RAW_OFFSET + 2 or data[:len(RAW_PREFIX)] != RAW_PREFIX:
        raise SlippiFormatError("unsupported or incomplete Slippi UBJSON envelope")
    raw_length = _u32(data, len(RAW_PREFIX))
    if raw_length == 0:
        raise SlippiFormatError("live/incomplete Slippi raw length is zero")
    if data[RAW_OFFSET] != EVENT_PAYLOADS:
        raise SlippiFormatError("raw stream does not begin with Event Payloads")
    payload_size = data[RAW_OFFSET + 1]
    if payload_size < 4 or (payload_size - 1) % 3:
        raise SlippiFormatError("invalid Event Payloads size")
    table_end = RAW_OFFSET + 1 + payload_size
    if table_end > RAW_OFFSET + raw_length:
        raise SlippiFormatError(
            "Event Payloads table extends beyond the declared raw stream")
    if table_end > len(data):
        raise SlippiFormatError("truncated Event Payloads table")
    sizes = {}
    cursor = RAW_OFFSET + 2
    while cursor < table_end:
        command = data[cursor]
        if command in sizes:
            raise SlippiFormatError(f"duplicate payload declaration 0x{command:02x}")
        sizes[command] = _u16(data, cursor + 1)
        cursor += 3
    game_start_size = sizes.get(GAME_START)
    if game_start_size is None:
        raise SlippiFormatError("Event Payloads does not declare Game Start")
    game_start = table_end
    event_end = game_start + 1 + game_start_size
    if event_end - RAW_OFFSET > raw_length:
        raise SlippiFormatError("Game Start extends beyond the declared raw stream")
    if event_end > len(data):
        raise SlippiFormatError(
            f"Game Start needs {event_end} prefix bytes, received {len(data)}")
    if data[game_start] != GAME_START:
        raise SlippiFormatError("Game Start is not the first declared event")
    game_start_event = data[game_start:event_end]
    version = tuple(data[game_start + 1:game_start + 5])
    info_start = game_start + 5
    info_end = info_start + GAME_INFO_SIZE
    if info_end > event_end:
        raise SlippiFormatError("Game Start does not contain the complete Game Info Block")
    game_info = data[info_start:info_end]
    _require_event(game_start_event, 0x13D, 4, "Game Start")
    seed = _u32(game_start_event, 0x13D)
    players = []
    for index in range(4):
        offset = 0x60 + 0x24 * index
        player_type = game_info[offset + 1]
        if player_type == 3:
            continue
        dashback = shield_drop = None
        if _version_at_least(version, (1, 0, 0)):
            fix_offset = 0x141 + 0x8 * index
            _require_event(game_start_event, fix_offset, 8, "Game Start")
            dashback = _u32(game_start_event, fix_offset)
            shield_drop = _u32(game_start_event, fix_offset + 4)
        players.append({
            "port": index + 1,
            "character_id": game_info[offset],
            "player_type": player_type,
            "stocks": game_info[offset + 2],
            "costume": game_info[offset + 3],
            "dashback_fix": dashback,
            "shield_drop_fix": shield_drop,
            "controller_fix": _fix_name(dashback, shield_drop)
                if dashback is not None else "unavailable",
        })
    if _version_at_least(version, (1, 5, 0)):
        _require_event(game_start_event, 0x1A1, 1, "Game Start")
    pal = (bool(game_start_event[0x1A1])
           if _version_at_least(version, (1, 5, 0)) else None)
    if _version_at_least(version, (2, 0, 0)):
        _require_event(game_start_event, 0x1A2, 1, "Game Start")
    frozen = (bool(game_start_event[0x1A2])
              if _version_at_least(version, (2, 0, 0)) else None)
    if _version_at_least(version, (3, 7, 0)):
        _require_event(game_start_event, 0x1A4, 1, "Game Start")
    minor_scene = (game_start_event[0x1A3]
                   if _version_at_least(version, (3, 7, 0)) else None)
    major_scene = (game_start_event[0x1A4]
                   if _version_at_least(version, (3, 7, 0)) else None)
    return SlippiHeader(raw_length, game_start_size, version, game_info, seed,
                         tuple(players), pal, frozen, minor_scene, major_scene, sizes)


def read_header(stream: BinaryIO):
    """Read only as many bytes as the declared Game Start event requires."""
    prefix = stream.read(RAW_OFFSET + 2)
    if len(prefix) < RAW_OFFSET + 2:
        raise SlippiFormatError("truncated Slippi prefix")
    if prefix[:len(RAW_PREFIX)] != RAW_PREFIX:
        raise SlippiFormatError("unsupported Slippi UBJSON envelope")
    raw_length = _u32(prefix, len(RAW_PREFIX))
    if raw_length == 0:
        raise SlippiFormatError("live/incomplete Slippi raw length is zero")
    payload_size = prefix[RAW_OFFSET + 1]
    if payload_size < 4 or (payload_size - 1) % 3:
        raise SlippiFormatError("invalid Event Payloads size")
    if 1 + payload_size > raw_length:
        raise SlippiFormatError(
            "Event Payloads table extends beyond the declared raw stream")
    table_tail = stream.read(payload_size - 1)
    table = prefix + table_tail
    if len(table_tail) != payload_size - 1:
        raise SlippiFormatError("truncated Event Payloads table")
    sizes = {}
    cursor = RAW_OFFSET + 2
    table_end = RAW_OFFSET + 1 + payload_size
    while cursor < table_end:
        if table[cursor] in sizes:
            raise SlippiFormatError(
                f"duplicate payload declaration 0x{table[cursor]:02x}")
        sizes[table[cursor]] = _u16(table, cursor + 1)
        cursor += 3
    game_start_size = sizes.get(GAME_START)
    if game_start_size is None:
        raise SlippiFormatError("Event Payloads does not declare Game Start")
    event = stream.read(1 + game_start_size)
    if len(event) != 1 + game_start_size:
        raise SlippiFormatError("truncated Game Start event")
    return decode_header(table + event)


def _decode_pre_frame(event: bytes):
    # The original processed fields and physical trigger values are present in
    # old files, while raw stick bytes were added later. Require the stable
    # prefix and inspect each raw field independently.
    _require_event(event, 1, 6, "Pre-Frame Update")
    frame = _i32(event, 1)
    port = event[5]
    follower = event[6]
    if port > 3 or follower > 1:
        raise SlippiFormatError(
            f"invalid Pre-Frame Update identity on frame {frame}")
    axis_fields = ((_float(event, offset), _float_bits(event, offset))
                   for offset in (0x19, 0x1D, 0x21, 0x25))
    axes = tuple(axis_fields)
    trigger = _float(event, 0x29)
    for name, value in zip(("stick X", "stick Y", "c-stick X", "c-stick Y"),
                           (entry[0] for entry in axes)):
        if not -1.000001 <= value <= 1.000001:
            raise SlippiFormatError(
                f"Pre-Frame Update {name} outside [-1,1] on frame {frame}")
    for name, value in (("trigger", trigger),):
        if not 0 <= value <= 1.000001:
            raise SlippiFormatError(
                f"Pre-Frame Update {name} outside [0,1] on frame {frame}")
    raw_x = _i8(event, 0x3B) if _event_has(event, 0x3B) else None
    raw_y = _i8(event, 0x40) if _event_has(event, 0x40) else None
    raw_cx = _i8(event, 0x41) if _event_has(event, 0x41) else None
    raw_cy = _i8(event, 0x42) if _event_has(event, 0x42) else None
    physical_l = (_float(event, 0x33)
                  if raw_x is not None and _event_has(event, 0x33, 4)
                  else None)
    physical_r = (_float(event, 0x37)
                  if raw_x is not None and _event_has(event, 0x37, 4)
                  else None)
    physical_buttons = (_u16(event, 0x31)
                        if _event_has(event, 0x31, 2) else None)
    physical_triggers = (
        _float_bits(event, 0x33) if raw_x is not None and
        _event_has(event, 0x33, 4) else None,
        _float_bits(event, 0x37) if raw_x is not None and
        _event_has(event, 0x37, 4) else None,
    )
    # Before rawJoystickX was added, the bytes at the physical-trigger
    # offsets belonged to a different tail. They are not physical trigger
    # observations even though the payload is long enough to read them.
    if raw_x is not None and physical_l is not None and physical_r is not None:
        for name, value in (("physical L", physical_l),
                            ("physical R", physical_r)):
            if not 0 <= value <= 1.000001:
                raise SlippiFormatError(
                    f"Pre-Frame Update {name} outside [0,1] on frame {frame}")
    return SlippiPreFrame(
        frame=frame, port=port + 1, is_follower=bool(follower),
        random_seed=_u32(event, 0x7), action_state=_u16(event, 0xB),
        position_bits=(_float_bits(event, 0xD), _float_bits(event, 0x11)),
        facing_bits=_float_bits(event, 0x15),
        processed_stick_bits=(axes[0][1], axes[1][1]),
        processed_cstick_bits=(axes[2][1], axes[3][1]),
        processed_trigger_bits=_float_bits(event, 0x29),
        processed_buttons=_u32(event, 0x2D),
        physical_buttons=physical_buttons,
        physical_trigger_bits=physical_triggers,
        raw_stick=(raw_x, raw_y), raw_cstick=(raw_cx, raw_cy))


def _decode_post_frame(event: bytes):
    # The normalized object needs the stable identity and position/state
    # prefix. Everything after the shield float is independently optional;
    # Slippi versions may stop at any of those offsets.
    _require_event(event, 0x1A, 4, "Post-Frame Update")
    frame = _i32(event, 1)
    port = event[5]
    follower = event[6]
    if port > 3 or follower > 1:
        raise SlippiFormatError(
            f"invalid Post-Frame Update identity on frame {frame}")
    # Payloads grow over time. Every field is independently optional: for
    # example, the 0x33-byte payload used by some official fixtures contains
    # the state fields through lCancelStatus but has no hurtbox byte at 0x34.
    # Do not use the last field as a proxy for all preceding fields.
    flags = tuple(event[offset] for offset in range(0x26, 0x2B)
                  if _event_has(event, offset))
    return SlippiPostFrame(
        frame=frame, port=port + 1, is_follower=bool(follower),
        character_id=event[7], action_state=_u16(event, 8),
        position_bits=(_float_bits(event, 0xA), _float_bits(event, 0xE)),
        facing_bits=_float_bits(event, 0x12),
        percent_bits=_float_bits(event, 0x16),
        shield_bits=_float_bits(event, 0x1A),
        stocks=event[0x21] if _event_has(event, 0x21) else None,
        state_flags=flags,
        ground_or_air=event[0x2F] if _event_has(event, 0x2F) else None,
        last_ground_id=_u16(event, 0x30)
            if _event_has(event, 0x30, 2) else None,
        jumps_remaining=event[0x32] if _event_has(event, 0x32) else None,
        hurtbox_state=event[0x34] if _event_has(event, 0x34) else None,
        action_state_counter_bits=_float_bits(event, 0x22)
            if _event_has(event, 0x22, 4) else None,
        misc_action_state_bits=_float_bits(event, 0x2B)
            if _event_has(event, 0x2B, 4) else None,
        l_cancel_status=event[0x33] if _event_has(event, 0x33) else None)


def _raw_stream(data: bytes):
    if not isinstance(data, bytes):
        raise TypeError("Slippi input must be bytes")
    if len(data) < RAW_OFFSET or data[:len(RAW_PREFIX)] != RAW_PREFIX:
        raise SlippiFormatError("unsupported or incomplete Slippi UBJSON envelope")
    raw_length = _u32(data, len(RAW_PREFIX))
    if raw_length == 0:
        raise SlippiFormatError("live/incomplete Slippi raw length is zero")
    end = RAW_OFFSET + raw_length
    if end > len(data):
        raise SlippiFormatError(
            f"raw stream needs {end} bytes, received {len(data)}")
    return data[RAW_OFFSET:end]


def decode_timeline(data: bytes):
    """Decode the finalized, engine-neutral input and observed-state timeline."""
    header = decode_header(data)
    raw = _raw_stream(data)
    if len(raw) < 2 or raw[0] != EVENT_PAYLOADS:
        raise SlippiFormatError("raw stream does not contain an Event Payloads table")
    payload_size = raw[1]
    table_end = 1 + payload_size
    if table_end > len(raw):
        raise SlippiFormatError("Event Payloads table extends beyond the raw stream")
    cursor = table_end
    first_event = True
    # Keep every revision until its frame is finalized. A final frame is the
    # event-time snapshot of the official parser's current frame object; using
    # one last-write map for the whole file would let a late speculative write
    # alter an already-finalized frame.
    input_updates = {}
    expected_updates = {}
    revision_phase = {}
    starts = {}
    finalized_frames = {}
    last_finalized = FIRST_FRAME - 1
    requested_finalized = None
    observed_bookend_finalized = None
    bookend_seen = False
    latest_frame_index = None
    duplicate_updates = 0
    game_end_method = lras_initiator = None
    game_end_seen = False

    active_ports = tuple(player["port"] for player in header.players)
    if not active_ports:
        raise SlippiFormatError("replay has no active players")

    def _update(mapping, value, kind):
        nonlocal duplicate_updates, latest_frame_index
        frame_mapping = mapping.setdefault(value.frame, {})
        key = (value.port, value.is_follower)
        versions = frame_mapping.setdefault(key, [])
        phase_key = (value.frame, key)
        phase = revision_phase.get(phase_key, "pre")
        is_pre = kind == "Pre-Frame Update"
        if (is_pre and phase != "pre") or (not is_pre and phase != "post"):
            expected = "post" if not is_pre else "pre"
            raise SlippiFormatError(
                f"frame {value.frame} port {value.port} has {kind} "
                f"before its matching {expected} update")
        revision_phase[phase_key] = "post" if is_pre else "pre"
        # slippi-js updates latestFrameIndex for every pre/post event, even
        # when the frame was already finalized.
        latest_frame_index = value.frame
        if value.frame <= last_finalized:
            # The official parser keeps accepting transfer messages after a
            # frame was emitted as finalized (some fixture streams resend the
            # just-bookended frame). Its emitted frame remains the earlier
            # snapshot, so ignore this late revision after accounting for a
            # duplicate. This is what keeps a finalized pre/post pair stable.
            duplicate_updates += bool(versions)
            return
        duplicate_updates += bool(versions)
        versions.append(value)

    def _snapshot(number):
        frame_inputs = input_updates.get(number, {})
        frame_expected = expected_updates.get(number, {})
        frame_input_keys = set(frame_inputs)
        frame_expected_keys = set(frame_expected)

        frame_inputs_out = []
        frame_expected_out = []
        for port in active_ports:
            key = (port, False)
            input_versions = frame_inputs.get(key)
            expected_versions = frame_expected.get(key)
            if not input_versions:
                # Match slippi-js strict finalization: defeated players in
                # free-for-all/doubles may have no frame entry at all.
                if (len(active_ports) > 2 and
                        not expected_versions):
                    continue
                raise SlippiFormatError(
                    f"frame {number} is missing port {port} input")
            if not expected_versions:
                raise SlippiFormatError(
                    f"frame {number} is missing port {port} post-state")
            if len(input_versions) != len(expected_versions):
                raise SlippiFormatError(
                    f"frame {number} port {port} has incoherent pre/post revisions")
            frame_inputs_out.append(input_versions[-1])
            frame_expected_out.append(expected_versions[-1])

        # Followers are optional, but when one appears the official frame must
        # contain a matching pre/post update. Indexing by frame avoids scanning
        # the entire replay once for every frame.
        follower_keys = sorted(
            key for key in frame_input_keys if key[1])
        for key in follower_keys:
            input_versions = frame_inputs[key]
            expected_versions = frame_expected.get(key)
            if not expected_versions:
                raise SlippiFormatError(
                    f"frame {number} is missing port {key[0]} follower post-state")
            if len(input_versions) != len(expected_versions):
                raise SlippiFormatError(
                    f"frame {number} port {key[0]} follower has incoherent "
                    "pre/post revisions")
            frame_inputs_out.append(input_versions[-1])
            frame_expected_out.append(expected_versions[-1])

        # A post update with no corresponding input cannot be represented by a
        # normalized input/state pair. Report it explicitly instead of silently
        # dropping it.
        for key in sorted(frame_expected_keys - frame_input_keys):
            if key[1]:
                label = f"port {key[0]} follower"
            else:
                label = f"port {key[0]}"
            raise SlippiFormatError(
                f"frame {number} is missing {label} input")

        start = starts.get(number, (None, None))
        return SlippiFrame(number, start[0], start[1],
                           tuple(frame_inputs_out), tuple(frame_expected_out))

    def _finalize_through(target):
        nonlocal last_finalized
        if target is None or target <= last_finalized:
            return
        # Match official _finalizeFrames: a missing frame stops advancement;
        # later validation turns an unfillable gap into a clear format error.
        while last_finalized < target:
            number = last_finalized + 1
            if number not in input_updates:
                break
            finalized_frames[number] = _snapshot(number)
            last_finalized = number

    while cursor < len(raw):
        command = raw[cursor]
        size = header.event_payload_sizes.get(command)
        if size is None:
            raise SlippiFormatError(
                f"raw event 0x{command:02x} at 0x{cursor:x} was not declared")
        end = cursor + 1 + size
        if end > len(raw):
            raise SlippiFormatError(
                f"raw event 0x{command:02x} at 0x{cursor:x} is truncated")
        if game_end_seen:
            raise SlippiFormatError(
                f"raw event 0x{command:02x} appears after Game End")
        event = raw[cursor:end]
        if first_event and command != GAME_START:
            raise SlippiFormatError("Game Start is not the first declared event")
        first_event = False
        if command == PRE_FRAME:
            value = _decode_pre_frame(event)
            _update(input_updates, value, "Pre-Frame Update")
        elif command == POST_FRAME:
            value = _decode_post_frame(event)
            _update(expected_updates, value, "Post-Frame Update")
        elif command == FRAME_START:
            _require_event(event, 5, 4, "Frame Start")
            frame = _i32(event, 1)
            if frame > last_finalized:
                starts[frame] = (_u32(event, 5),
                                 _u32(event, 9) if _event_has(event, 9, 4) else None)
        elif command == FRAME_BOOKEND:
            _require_event(event, 5, 4, "Frame Bookend")
            frame = _i32(event, 1)
            finalized = _i32(event, 5)
            bookend_seen = True
            observed_bookend_finalized = (
                finalized if observed_bookend_finalized is None
                else max(observed_bookend_finalized, finalized))
            # slippi-js uses latestFinalizedFrame only for online games. For
            # other modes it finalizes conservatively seven frames behind the
            # current bookend frame.
            if header.major_scene == 8 and finalized >= FIRST_FRAME:
                requested_finalized = (finalized if requested_finalized is None
                                       else max(requested_finalized, finalized))
                _finalize_through(requested_finalized)
            else:
                _finalize_through(frame - 7)
        elif command == GAME_END:
            _require_event(event, 1, 1, "Game End")
            game_end_method = event[1]
            lras_initiator = _i8(event, 2) if _event_has(event, 2) else None
            game_end_seen = True
        cursor = end
    if not input_updates:
        raise SlippiFormatError("replay has no Pre-Frame Update events")

    main_frames = sorted(input_updates)
    if main_frames[0] != FIRST_FRAME:
        raise SlippiFormatError(
            f"normalized replay starts at {main_frames[0]}, expected {FIRST_FRAME}")

    # Game End asks the official parser to finalize through the latest frame
    # it received, including the final speculative frame after a bookend. This
    # is what makes rollbackFrameTest.slp end at 9153 rather than 9152.
    if game_end_method is not None and latest_frame_index is not None:
        _finalize_through(latest_frame_index)

    # If a valid online watermark leaves a gap, no later event can make that
    # finalized range trustworthy. Keep the distinction between a temporary
    # missing frame (handled while streaming) and a malformed completed file.
    if bookend_seen and header.major_scene == 8 and requested_finalized is not None:
        required_through = max(requested_finalized,
                               latest_frame_index
                               if game_end_method is not None and latest_frame_index is not None
                               else requested_finalized)
    elif game_end_method is not None and latest_frame_index is not None:
        required_through = latest_frame_index
    elif bookend_seen and observed_bookend_finalized is not None:
        # Offline bookends carry the current frame again in the second field,
        # but that field is not a finalization watermark. The official parser
        # deliberately stays seven frames behind until GAME_END arrives.
        # Therefore EOF without GAME_END may legitimately end at the last
        # conservative snapshot already emitted.
        required_through = last_finalized
    else:
        required_through = last_finalized
    if last_finalized < required_through:
        missing = last_finalized + 1
        if missing not in input_updates:
            raise SlippiFormatError(f"normalized replay is missing frame {missing}")
        # The frame exists but could not be materialized; call the snapshot to
        # expose the precise missing port/revision reason.
        _snapshot(missing)
        raise SlippiFormatError(
            f"normalized replay could not finalize frame {missing}")

    if not finalized_frames:
        raise SlippiFormatError("replay has no finalized input frames")
    output_frames = sorted(finalized_frames)
    if not output_frames:
        raise SlippiFormatError("replay has no finalized input frames")
    for previous, current in zip(output_frames, output_frames[1:]):
        if current != previous + 1:
            raise SlippiFormatError(
                f"normalized replay is missing frame {previous + 1}")

    # Keep the historical API: finalized_through reports the latest explicit
    # frame-bookend watermark. Files without bookends have no such watermark.
    # Older callers treated any explicit bookend as a useful watermark even
    # for offline recordings. Keep that API while using the official seven
    # frame fallback to decide when snapshots become available.
    finalized_through = requested_finalized
    frames = tuple(finalized_frames[number] for number in output_frames)
    return SlippiTimeline(header, frames, finalized_through,
                           duplicate_updates, game_end_method, lras_initiator)


def read_timeline(stream: BinaryIO):
    """Read a completed replay without parsing its privacy-bearing metadata."""
    prefix = stream.read(RAW_OFFSET)
    if len(prefix) < RAW_OFFSET:
        raise SlippiFormatError("truncated Slippi prefix")
    if prefix[:len(RAW_PREFIX)] != RAW_PREFIX:
        raise SlippiFormatError("unsupported Slippi UBJSON envelope")
    raw_length = _u32(prefix, len(RAW_PREFIX))
    if raw_length == 0:
        raise SlippiFormatError("live/incomplete Slippi raw length is zero")
    raw = stream.read(raw_length)
    if len(raw) != raw_length:
        raise SlippiFormatError("truncated Slippi raw stream")
    return decode_timeline(prefix + raw)
