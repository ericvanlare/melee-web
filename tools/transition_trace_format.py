"""Versioned semantic layout shared by retail transition capture and comparison."""

import struct


SCHEMA = "melee-web-transition-trace"
VERSION = 1
EXPECTED_EVENTS = (
    "capture_begin",
    "css_exit_complete",
    "sss_enter_complete",
    "sss_exit_complete",
    "css_enter_complete",
    "css_exit_complete",
    "sss_enter_complete",
    "sss_exit_complete",
    "match_enter_complete",
)
RULE_KEYS = {
    "match_kind", "hud_layout", "timer_enabled", "timer_counts_up",
    "friendly_fire", "is_stock", "single_button", "disable_pausing", "is_vs",
    "is_teams", "item_frequency", "stage_kind", "time_limit", "item_mask",
    "damage_ratio_bits", "game_speed_bits",
}
PLAYER_KEYS = {
    "ckind", "slot_type", "stocks", "color", "slot", "spawn",
    "spawn_direction", "sub_color", "handicap", "team", "nametag", "flags_c",
    "flags_d", "cpu_kind", "cpu_level", "damage_10", "damage_12", "hp",
    "attack_ratio_bits", "defense_ratio_bits", "model_scale_bits",
}


def _signed_byte(value):
    return value - 256 if value >= 128 else value


def _hex32(data, offset):
    return f"{struct.unpack_from('>I', data, offset)[0]:08x}"


def _player(data, offset):
    row = data[offset:offset + 0x24]
    if len(row) != 0x24:
        raise ValueError("StartMeleeData player record is truncated")
    return {
        "ckind": _signed_byte(row[0]),
        "slot_type": row[1],
        "stocks": _signed_byte(row[2]),
        "color": row[3],
        "slot": row[4],
        "spawn": _signed_byte(row[5]),
        "spawn_direction": _signed_byte(row[6]),
        "sub_color": row[7],
        "handicap": _signed_byte(row[8]),
        "team": row[9],
        "nametag": row[10],
        "flags_c": row[12],
        "flags_d": row[13],
        "cpu_kind": row[14],
        "cpu_level": row[15],
        "damage_10": struct.unpack_from(">H", row, 0x10)[0],
        "damage_12": struct.unpack_from(">H", row, 0x12)[0],
        "hp": struct.unpack_from(">H", row, 0x14)[0],
        "attack_ratio_bits": _hex32(row, 0x18),
        "defense_ratio_bits": _hex32(row, 0x1C),
        "model_scale_bits": _hex32(row, 0x20),
    }


def decode_start_melee_data(data):
    """Decode semantic fields from one big-endian retail StartMeleeData."""
    if not isinstance(data, bytes) or len(data) != 0xF0:
        raise ValueError("StartMeleeData must contain exactly 0xf0 retail bytes")
    return {
        "rules": {
            "match_kind": data[0] >> 5,
            "hud_layout": (data[0] >> 2) & 7,
            "timer_enabled": bool(data[0] & 2),
            "timer_counts_up": bool(data[0] & 1),
            "friendly_fire": bool(data[1] & 1),
            "is_stock": bool(data[2] & 0x80),
            "single_button": bool(data[2] & 0x10),
            "disable_pausing": bool(data[2] & 8),
            "is_vs": bool(data[4] & 0x40),
            "is_teams": data[8],
            "item_frequency": _signed_byte(data[0xB]),
            "stage_kind": struct.unpack_from(">H", data, 0xE)[0],
            "time_limit": struct.unpack_from(">I", data, 0x10)[0],
            "item_mask": f"{struct.unpack_from('>Q', data, 0x20)[0]:016x}",
            "damage_ratio_bits": _hex32(data, 0x30),
            "game_speed_bits": _hex32(data, 0x34),
        },
        "players": [_player(data, 0x60 + index * 0x24) for index in range(4)],
    }
