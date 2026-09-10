"""Strict, privacy-minimized decoding for Slippi corpus admission.

This module intentionally starts with the Game Start event. It does not claim
to be a replacement for the official slippi-js event parser; later replay
normalization is cross-checked against that pinned implementation.
"""

from __future__ import annotations

from dataclasses import dataclass
import struct
from typing import BinaryIO


SCHEMA = "melee-web-slippi-corpus"
SCHEMA_VERSION = 1
SPEC_URL = "https://github.com/project-slippi/slippi-wiki/blob/master/SPEC.md"
RAW_PREFIX = b"{U\x03raw[$U#l"
RAW_OFFSET = 15
GAME_START = 0x36
EVENT_PAYLOADS = 0x35
GAME_INFO_SIZE = 0x138


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
        raw_pad = _version_at_least(self.version, (3, 17, 0))
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
    version = tuple(data[game_start + 1:game_start + 5])
    info_start = game_start + 5
    info_end = info_start + GAME_INFO_SIZE
    if info_end > event_end:
        raise SlippiFormatError("Game Start does not contain the complete Game Info Block")
    game_info = data[info_start:info_end]
    seed = _u32(data, game_start + 0x13D)
    players = []
    for index in range(4):
        offset = 0x60 + 0x24 * index
        player_type = game_info[offset + 1]
        if player_type == 3:
            continue
        dashback = shield_drop = None
        if _version_at_least(version, (1, 0, 0)):
            dashback = _u32(data, game_start + 0x141 + 0x8 * index)
            shield_drop = _u32(data, game_start + 0x145 + 0x8 * index)
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
    pal = bool(data[game_start + 0x1A1]) \
        if _version_at_least(version, (1, 5, 0)) else None
    frozen = bool(data[game_start + 0x1A2]) \
        if _version_at_least(version, (2, 0, 0)) else None
    minor_scene = data[game_start + 0x1A3] \
        if _version_at_least(version, (3, 7, 0)) else None
    major_scene = data[game_start + 0x1A4] \
        if _version_at_least(version, (3, 7, 0)) else None
    return SlippiHeader(raw_length, game_start_size, version, game_info, seed,
                         tuple(players), pal, frozen, minor_scene, major_scene, sizes)


def read_header(stream: BinaryIO):
    """Read only as many bytes as the declared Game Start event requires."""
    prefix = stream.read(RAW_OFFSET + 2)
    if len(prefix) < RAW_OFFSET + 2:
        raise SlippiFormatError("truncated Slippi prefix")
    if prefix[:len(RAW_PREFIX)] != RAW_PREFIX:
        raise SlippiFormatError("unsupported Slippi UBJSON envelope")
    payload_size = prefix[RAW_OFFSET + 1]
    if payload_size < 4 or (payload_size - 1) % 3:
        raise SlippiFormatError("invalid Event Payloads size")
    table_tail = stream.read(payload_size - 1)
    table = prefix + table_tail
    if len(table_tail) != payload_size - 1:
        raise SlippiFormatError("truncated Event Payloads table")
    sizes = {}
    cursor = RAW_OFFSET + 2
    table_end = RAW_OFFSET + 1 + payload_size
    while cursor < table_end:
        sizes[table[cursor]] = _u16(table, cursor + 1)
        cursor += 3
    game_start_size = sizes.get(GAME_START)
    if game_start_size is None:
        raise SlippiFormatError("Event Payloads does not declare Game Start")
    event = stream.read(1 + game_start_size)
    if len(event) != 1 + game_start_size:
        raise SlippiFormatError("truncated Game Start event")
    return decode_header(table + event)
