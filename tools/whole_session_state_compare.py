#!/usr/bin/env python3
"""Strict state/input comparison for one MWRO + MWRC whole-session run.

The raw observer is the source of expected order.  Its ``pad_consume`` rows
bind one-for-one to MWRC frames; no scene or input search is performed.  Menu
rows validate browser field shape and exact scene/input order; their scalar
state is explicitly outside this comparator's source-to-browser join. VS
``setup`` and ``source_tick`` rows additionally carry exact state for the two
to four active fighters in the match.

This module is intentionally separate from the older indexed-prefix
comparator.  It checks the complete source-consumed timeline and reports the
known boundary limitation: non-match source snapshots have no match clock or
FighterHead state, and the browser report supplies the final CSS endpoint
without a scalar CSS state join.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys
from typing import Any, Iterable, Mapping, Sequence
from urllib.parse import urljoin


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "reference-capture" / "dolphin"), str(ROOT / "tools")]

from reference_observer_stream import (  # noqa: E402
    ObserverStreamError,
    ObserverStreamStats,
    iter_records,
)
import reference_capture_semantics as semantics  # noqa: E402
from reference_capture_semantics import SliceMemory, pad_snapshot_bytes, state_snapshot  # noqa: E402
from whole_session_replay import (  # noqa: E402
    CONTEXT_BYTES,
    CONTEXT_HEADER,
    CSS_DATA_SIZE,
    GAME_INFO_SIZE,
    KO_COUNTS_SIZE,
    MAX_SPANS,
    PAD_STATE_BYTES,
    SCENES,
    SPAN,
    EXPECTED_DOLPHIN_COMMIT,
    EXPECTED_DOL_SHA1,
    EXPECTED_DOL_SHA256,
    EXPECTED_OBSERVER_SCHEMA,
    V9_MILESTONE_RULES,
    WholeSessionReplayError,
    _consumed_ports,
    _decode_setup,
    _first_css_context,
    _gpr,
    validate_milestone_setups,
)
from retail_replay_validation import (  # noqa: E402
    CaptureError as RetailCaptureError,
    _validate_fighter as _validate_retail_fighter,
)


SCHEMA = "melee-web-whole-session-state-comparison-v1"
BROWSER_SCHEMA = "melee-web-port-session-diagnostic"
BROWSER_VERSION = 1
MWRC_HEADER = struct.Struct(">4sIIIHH")
MWRC_CONTEXT_VERSION = 2
MWRC_V8_VERSION = 8
MWRC_VERSION = 9
MWRC_V10_VERSION = 10
WHOLE_SESSION_SCOPE = "whole-session"
V10_FIRST_SETUP_TICK0_SCOPE = "v10-first-setup-tick0"
V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE = "v10-first-positive-match-frame"
V10_FIRST_MATCH_CLOCK_GE60_SCOPE = "v10-first-match-clock-ge60"
V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE = "v10-first-match-clock-boundary"
V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE = "v10-first-match-clock-ordered-lineage"
PRIMARY_STATIC_ENTITY_PROFILE = "primary-static-player-pair-v1"
FRAME_BYTES = 44
PORT_BYTES = 11
PORT_COUNT = 4
MAX_UINT32 = 0xFFFFFFFF
SCENE_NAMES = {value: key for key, value in SCENES.items()}
COMPARE_FIELDS = ("rng", "match_frame", "pad_state_hex", "fighters")
V9_COMPARE_FIELDS = COMPARE_FIELDS + ("fighter_entities",)
V10_FIGHTER_ROSTER = (
    (1, 5, 11, 12),
    (15, 10, 24, 18),
    (4, 14, 16, 17),
)
# Keep the complete authored rules bytes, including fields not exposed by the
# decoded setup dictionary, in step with kMilestoneRules in the native reader.
V10_RULES_BYTES = bytes.fromhex(
    "3000864cc3000000000000ffff6e002000000000000000000000000000000000ffffffffffffffff000000003f8000003f8000003f80000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
)
V10_SOURCE_REPORT_SCHEMA = "melee-web-recorded-session-12-character-capture-v1"
V10_SOURCE_LINEUP_PROFILE = "v10-fighter-coverage"
V10_SOURCE_INPUT_MODE = "ordinary-controller-pipe-record"
V10_BROWSER_CAPTURE_SCHEMA = "melee-web-headless-whole-session-replay-v1"
V10_BROWSER_PRODUCER_SCHEMA = "melee-web-b4-match-entry-producer-source-v1"
# Retained historical producer format: the shared source/build/input contract
# is unchanged. New producers must use V10_BROWSER_PRODUCER_SCHEMA.
V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA = "melee-web-b4-first-match-clock300-browser-producer-v1"
V10_PREFIX_BYTE_CAP = 32 * 1024 * 1024
V10_PREFIX_RECORD_CAP = 4200
V10_FIRST_POSITIVE_RECORD_CAP = 8192
V10_MATCH_CLOCK_RECORD_CAP = V10_FIRST_POSITIVE_RECORD_CAP
V10_MATCH_CLOCK_REJOIN_FRAME = 300
V10_ORDERED_LINEAGE_BYTE_CAP = 64 * 1024 * 1024
V10_ORDERED_LINEAGE_RECORD_CAP = 12000
V10_BROWSER_EXPORT_RECORD_CAP = 8192
V10_MANUAL_UNLOAD_FAILURES = (
    "whole-session final CSS was not entered",
    "Manual unload stopped the replay",
    "incomplete input timeline",
    "source tick/draw count mismatch",
)
V10_DEFAULT_OFF_GATES = {
    "MELEE_WEB_AUDIO_PREVIEW_RUNTIME": "OFF",
    "MELEE_WEB_AURORA_FUTURE_OWNER_DIAGNOSTIC": "OFF",
    "MELEE_WEB_AURORA_QUIESCENCE_DIAGNOSTIC": "OFF",
    "MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE": "OFF",
    "MELEE_WEB_PIPELINE_PROVENANCE": "OFF",
    "MELEE_WEB_PUBLIC_RUNTIME": "OFF",
    "MELEE_WEB_RESULTS_RENDERED_TRACE": "OFF",
    "MELEE_WEB_SELECTIVE_PIPELINES": "OFF",
    "MELEE_WEB_SLIPPI_PROFILE_BROWSER": "OFF",
    "MELEE_WEB_STADIUM_C1A_DIAGNOSTIC": "OFF",
}
NONMATCH_FIELDS: tuple[str, ...] = ()
FIGHTER_KEYS = {
    "slot", "kind", "motion", "animation", "ground_air", "facing_bits",
    "position_bits", "velocity_bits", "knockback_bits", "animation_frame_bits",
    "animation_speed_bits", "damage_bits", "shield_bits", "stocks", "input_hex",
}
HEX_RE = re.compile(r"^[0-9a-f]+$")


def _is_v10_prefix_scope(scope: str) -> bool:
    return scope in {V10_FIRST_SETUP_TICK0_SCOPE, V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
                     V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                     V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                     V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}


class ComparisonError(ValueError):
    """Input or observer evidence cannot be admitted to this comparison."""


def comparison_fields(version: int, *, scope: str = WHOLE_SESSION_SCOPE) -> tuple[str, ...]:
    if _is_v10_prefix_scope(scope):
        if version == MWRC_V10_VERSION:
            return V9_COMPARE_FIELDS
        message = ("v10 first-positive match-frame scope requires MWRC v10"
                   if scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE else
                   "v10 first-match clock-60 scope requires MWRC v10"
                   if scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE else
                   "v10 first-match clock-boundary scope requires MWRC v10"
                   if scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE else
                   "v10 ordered match-clock lineage scope requires MWRC v10"
                   if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE else
                   "v10 first-setup/tick-0 scope requires MWRC v10")
        raise ComparisonError(message)
    if scope != WHOLE_SESSION_SCOPE:
        raise ComparisonError(f"unsupported whole-session comparison scope {scope!r}")
    if version == MWRC_VERSION:
        return V9_COMPARE_FIELDS
    if version == MWRC_V8_VERSION:
        return COMPARE_FIELDS
    raise ComparisonError(f"unsupported whole-session comparison version {version}")


def _source_prefix_identity(handshake: Mapping[str, Any],
                             start: Mapping[str, Any]) -> dict[str, Any]:
    """Validate the pinned passive-observer identity needed before first tick."""
    if handshake.get("schema") != EXPECTED_OBSERVER_SCHEMA or handshake.get("version") != 1:
        raise ComparisonError("source handshake is not the pinned passive observer schema/version")
    for field, expected in (
        ("dolphin_commit", EXPECTED_DOLPHIN_COMMIT),
        ("dol_sha1", EXPECTED_DOL_SHA1),
        ("dol_sha256", EXPECTED_DOL_SHA256),
        ("cpu", "JITARM64"),
    ):
        if handshake.get(field) != expected:
            raise ComparisonError(f"source handshake {field} is not the pinned reference identity")
    if handshake.get("writes_guest_memory") is not False:
        raise ComparisonError("source observer must declare writes_guest_memory=false")
    if handshake.get("fighter_entity_profile") != "v10-live-static-player-pair":
        raise ComparisonError("source observer does not declare the v10 primary-entity profile")
    for announcement, context in ((handshake, "handshake"), (start, "start")):
        if announcement.get("whole_session") is not True:
            raise ComparisonError(f"source {context} does not declare whole_session=true")
        if announcement.get("match_count") != 3:
            raise ComparisonError(f"source {context} must declare exactly three matches")
        for field in ("capture_id", "sequence_id"):
            value = announcement.get(field)
            if not isinstance(value, str) or re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value) is None:
                raise ComparisonError(f"source {context} {field} is not a safe identity")
    for field in ("capture_id", "sequence_id", "match_count"):
        if handshake.get(field) != start.get(field):
            raise ComparisonError(f"source {field} disagrees between handshake and start")
    if start.get("source_revision") != "GALE01r2":
        raise ComparisonError("source start is not for GALE01r2")
    return {
        "capture_id": handshake["capture_id"],
        "sequence_id": handshake["sequence_id"],
        "match_count": 3,
        "source_revision": "GALE01r2",
        "observer_schema": EXPECTED_OBSERVER_SCHEMA,
        "observer_version": 1,
        "dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
        "dol_sha1": EXPECTED_DOL_SHA1,
        "dol_sha256": EXPECTED_DOL_SHA256,
        "cpu": "JITARM64",
        "fighter_entity_profile": "v10-live-static-player-pair",
        "writes_guest_memory": False,
        "whole_session": True,
    }


def _is_match_field(field: Any) -> bool:
    if not isinstance(field, str):
        return False
    root = field.split(".", 1)[0].split("[", 1)[0]
    return root in V9_COMPARE_FIELDS


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ComparisonError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _int(value: Any, context: str, low: int = 0, high: int = MAX_UINT32) -> int:
    if type(value) is not int or not low <= value <= high:
        raise ComparisonError(f"{context}: expected integer in [{low}, {high}]")
    return value


def _hex(value: Any, size: int, context: str) -> str:
    if not isinstance(value, str) or len(value) != size * 2 or not HEX_RE.fullmatch(value):
        raise ComparisonError(f"{context}: expected lowercase hexadecimal {size:#x}-byte value")
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
            child = f"{path}.{key}" if path else key
            if key not in expected:
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


def _slice(payload: Mapping[str, Any], name: str, size: int, context: str) -> tuple[dict[str, Any], bytes]:
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    matches = [item for item in slices if isinstance(item, dict) and item.get("name") == name]
    if len(matches) != 1:
        raise ComparisonError(f"{context}: expected exactly one {name} slice")
    item = matches[0]
    if item.get("size") != size:
        raise ComparisonError(f"{context}: {name} slice has wrong size")
    raw = bytes.fromhex(item.get("hex", ""))
    if len(raw) != size:
        raise ComparisonError(f"{context}: {name} slice bytes disagree with size")
    return item, raw


def _snapshot_values(payload: Mapping[str, Any], context: str) -> dict[str, Any]:
    rng_items = [x for x in payload.get("slices", []) if x.get("name") == "rng_value"]
    pad_items = [x for x in payload.get("slices", []) if x.get("name") == "pad_snapshot"]
    if len(rng_items) != 1 or len(pad_items) != 1:
        raise ComparisonError(f"{context}: expected one typed RNG and PAD snapshot")
    rng_item = rng_items[0]
    pad_item = pad_items[0]
    rng_raw = bytes.fromhex(rng_item.get("hex", ""))
    pad_raw = bytes.fromhex(pad_item.get("hex", ""))
    if len(rng_raw) != 4:
        raise ComparisonError(f"{context}: RNG slice must be four bytes")
    if len(pad_raw) != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot slice must be 0x358 bytes")
    # The first-CSS context intentionally includes a large SaveData slice that
    # exceeds SliceMemory's small typed-span limit.  The observer stream has
    # already validated descriptor bounds; validate only the exact fields used
    # by this comparator and convert the canonical source PAD block.
    return {"rng": int.from_bytes(rng_raw, "big"),
            "pad_state_hex": pad_snapshot_bytes(pad_raw)}


def _validate_browser_fighters(fighters: Any, context: str) -> None:
    if not isinstance(fighters, list) or not 2 <= len(fighters) <= 4:
        raise ComparisonError(f"{context}: expected two to four fighters")
    for slot, fighter in enumerate(fighters):
        if not isinstance(fighter, dict) or set(fighter) != FIGHTER_KEYS or fighter.get("slot") != slot:
            raise ComparisonError(f"{context}: malformed fighter {slot}")


FIGHTER_IDENTITY_SLICES = {
    "fighter_head", "fighter_input_anim", "fighter_damage_shield",
    "fighter_subject", "cpu_state", "fighter_create_context",
}


def _validate_fighter_slice_flags(slices: list[Any], context: str) -> None:
    seen: set[tuple[str, int]] = set()
    for item in slices:
        if not isinstance(item, dict) or item.get("name") not in FIGHTER_IDENTITY_SLICES:
            continue
        name = item["name"]
        flags = item.get("flags")
        if type(flags) is not int or not 0 <= flags <= 0xFFFF:
            raise ComparisonError(f"{context}: invalid {name} flags")
        slot, entity_index = flags & 0xFF, flags >> 8
        if slot >= 4 or entity_index > 1:
            raise ComparisonError(f"{context}: unsupported fighter identity flags {flags:#x}")
        identity = (name, flags)
        if identity in seen:
            raise ComparisonError(f"{context}: duplicate {name} slice for flags {flags:#x}")
        seen.add(identity)


def _pad_value(payload: Mapping[str, Any], context: str) -> str:
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    matches = [item for item in slices if isinstance(item, dict) and item.get("name") == "pad_snapshot"]
    if len(matches) != 1:
        raise ComparisonError(f"{context}: expected one typed PAD snapshot")
    item = matches[0]
    if item.get("size") != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot slice must be 0x358 bytes")
    raw = bytes.fromhex(item.get("hex", ""))
    if len(raw) != 0x358:
        raise ComparisonError(f"{context}: PAD snapshot bytes disagree with size")
    return pad_snapshot_bytes(raw)


def _state_from_payload(payload: Mapping[str, Any], context: str) -> dict[str, Any]:
    slices = payload.get("slices", [])
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed slices")
    _validate_fighter_slice_flags(slices, context)
    heads: dict[int, int] = {}
    for item in slices:
        if item.get("name") == "fighter_head":
            flags = item["flags"]
            slot, entity_index = flags & 0xFF, flags >> 8
            if entity_index:
                continue
            if slot in heads:
                raise ComparisonError(f"{context}: duplicate primary fighter head slot")
            heads[slot] = item.get("address")
    if not 2 <= len(heads) <= 4 or set(heads) != set(range(len(heads))):
        raise ComparisonError(f"{context}: expected two to four contiguous fighter heads")
    state = state_snapshot(SliceMemory(slices), heads)
    if len(state["fighters"]) != len(heads):
        raise ComparisonError(f"{context}: fighter state count disagrees with fighter heads")
    return state


def _browser_entities(value: Any, context: str, match_index: int) -> list[dict[str, int]]:
    if not isinstance(value, list) or len(value) != 4:
        raise ComparisonError(f"{context}: expected four ordered fighter entity identities")
    expected_keys = {"match_index", "slot", "entity_index", "generation",
                     "fighter_player_id", "fighter_gobj_linked"}
    result: list[dict[str, int]] = []
    for slot, item in enumerate(value):
        if not isinstance(item, dict) or set(item) != expected_keys:
            raise ComparisonError(f"{context}: malformed fighter entity identity at slot {slot}")
        if (item.get("match_index") != match_index or item.get("slot") != slot or
                item.get("entity_index") != 0 or item.get("fighter_player_id") != slot):
            raise ComparisonError(f"{context}: fighter entity identities are missing, extra, or reordered")
        _int(item.get("generation"), f"{context}.fighter_entities[{slot}].generation")
        _int(item.get("fighter_player_id"),
             f"{context}.fighter_entities[{slot}].fighter_player_id", slot, slot)
        if item.get("fighter_gobj_linked") is not True:
            raise ComparisonError(f"{context}: Fighter GObj backlink is absent for slot {slot}")
        result.append(item)
    return result


def _fighter_entities(payload: Mapping[str, Any], context: str, match_index: int,
                      previous: dict[int, tuple[int, int]]) -> list[dict[str, int]]:
    """Bind four source StaticPlayer slots to their primary Fighter heads.

    Guest addresses remain source-local.  Only ordered slot/entity identity and
    its generation enter the browser comparison.
    """
    slices = payload.get("slices")
    if not isinstance(slices, list):
        raise ComparisonError(f"{context}: missing typed entity slices")
    _validate_fighter_slice_flags(slices, context)
    entity_rows = [item for item in slices
                   if isinstance(item, dict) and item.get("name") == "player_entities"]
    user_data_rows = [item for item in slices
                      if isinstance(item, dict) and item.get("name") == "player_entity_user_data"]
    if len(entity_rows) != 4 or len(user_data_rows) != 4:
        raise ComparisonError(f"{context}: expected exactly four primary/secondary entity pairs and user_data links")
    related_order = [(item.get("name"), item.get("flags")) for item in slices
                     if isinstance(item, dict) and item.get("name") in
                     {"player_entities", "player_entity_user_data"}]
    if related_order != [(name, slot) for slot in range(4)
                         for name in ("player_entities", "player_entity_user_data")]:
        raise ComparisonError(f"{context}: entity relationship records are missing, extra, or reordered")
    heads: dict[int, int] = {}
    for item in slices:
        if isinstance(item, dict) and item.get("name") == "fighter_head":
            flags = item["flags"]
            slot, entity_index = flags & 0xFF, flags >> 8
            if entity_index:
                continue
            if type(slot) is not int or not 0 <= slot < 4 or slot in heads:
                raise ComparisonError(f"{context}: invalid Fighter head while binding entity slots")
            address = item.get("address")
            if type(address) is not int:
                raise ComparisonError(f"{context}: Fighter head has an invalid source address")
            heads[slot] = address
    if set(heads) != set(range(4)):
        raise ComparisonError(f"{context}: entity coverage requires all four Fighter heads")
    fighter_player_ids: dict[int, int] = {}
    fighter_gobjs: dict[int, int] = {}
    for item in slices:
        if isinstance(item, dict) and item.get("name") == "fighter_head":
            flags = item["flags"]
            slot, entity_index = flags & 0xFF, flags >> 8
            if entity_index:
                continue
            try:
                raw_head = bytes.fromhex(item.get("hex", ""))
            except (TypeError, ValueError) as error:
                raise ComparisonError(f"{context}: invalid Fighter bytes for slot {slot}") from error
            if item.get("size") != len(raw_head) or len(raw_head) <= 0x0C:
                raise ComparisonError(f"{context}: Fighter head is too short to prove its player slot")
            fighter_player_id = raw_head[0x0C]
            if fighter_player_id != slot:
                raise ComparisonError(f"{context}: Fighter player_id is detached from player slot {slot}")
            fighter_player_ids[slot] = fighter_player_id
            fighter_gobjs[slot] = int.from_bytes(raw_head[:4], "big")

    seen_primary: set[int] = set()
    result: list[dict[str, int]] = []
    for slot in range(4):
        pair = entity_rows[slot]
        link = user_data_rows[slot]
        expected_static = 0x80453080 + slot * 0xE90 + 0xB0
        if (pair.get("flags") != slot or pair.get("address") != expected_static or
                pair.get("size") != 8 or link.get("flags") != slot or
                link.get("size") != 4):
            raise ComparisonError(f"{context}: malformed or misaddressed player entity pair for slot {slot}")
        try:
            pair_raw = bytes.fromhex(pair.get("hex", ""))
            link_raw = bytes.fromhex(link.get("hex", ""))
        except (TypeError, ValueError) as error:
            raise ComparisonError(f"{context}: invalid entity bytes for slot {slot}") from error
        if len(pair_raw) != 8 or len(link_raw) != 4:
            raise ComparisonError(f"{context}: entity bytes have an invalid size for slot {slot}")
        primary = int.from_bytes(pair_raw[:4], "big")
        secondary = int.from_bytes(pair_raw[4:], "big")
        user_data = int.from_bytes(link_raw, "big")
        if not 0x80000000 <= primary < 0x81800000:
            raise ComparisonError(f"{context}: primary player entity for slot {slot} is outside source MEM1")
        if fighter_gobjs[slot] != primary:
            raise ComparisonError(f"{context}: Fighter GObj backlink is detached from primary entity for slot {slot}")
        if secondary != 0:
            raise ComparisonError(f"{context}: unsupported secondary entity in slot {slot}")
        if primary in seen_primary:
            raise ComparisonError(f"{context}: duplicate primary player entity across slots")
        seen_primary.add(primary)
        if link.get("address") != primary + 0x2C or user_data != heads[slot]:
            raise ComparisonError(f"{context}: primary entity user_data is detached from Fighter head for slot {slot}")
        old = previous.get(slot)
        generation = 0 if old is None else old[1] + (old[0] != primary)
        previous[slot] = (primary, generation)
        result.append({"match_index": match_index, "slot": slot,
                       "entity_index": 0, "generation": generation,
                       "fighter_player_id": fighter_player_ids[slot],
                       "fighter_gobj_linked": True})
    return result


def _validate_v10_setups(setups: list[bytes]) -> list[dict[str, Any]]:
    """Validate all three v10 StartMeleeData rows against the native profile."""
    if len(setups) != 3:
        raise ComparisonError("MWRC v10 requires exactly three match setups")
    if len(V10_RULES_BYTES) != 0x60:
        raise ComparisonError("internal v10 rules profile has the wrong size")
    declared: list[dict[str, Any]] = []
    for match_index, raw in enumerate(setups):
        if len(raw) != GAME_INFO_SIZE:
            raise ComparisonError(f"MWRC v10 setup {match_index} is not 0x138 bytes")
        if raw[:len(V10_RULES_BYTES)] != V10_RULES_BYTES:
            raise ComparisonError(f"MWRC v10 setup {match_index} rules differ from the CPU9 profile")
        try:
            setup = _decode_setup(raw.hex())
        except (KeyError, TypeError, ValueError) as error:
            raise ComparisonError(f"MWRC v10 setup {match_index} is unsupported: {error}") from error
        rules = {key: value for key, value in setup.items() if key != "players"}
        if rules != V9_MILESTONE_RULES:
            raise ComparisonError(f"MWRC v10 setup {match_index} decoded rules differ from the CPU9 profile")
        players = setup["players"]
        if len(players) != 4 or [player["port"] for player in players] != [1, 2, 3, 4]:
            raise ComparisonError(f"MWRC v10 setup {match_index} must contain four contiguous CPU players")
        for slot, player in enumerate(players):
            if (player["player_type"] != 1 or player.get("cpu_kind") != 4 or
                    player.get("cpu_level") != 9 or player["stocks"] != 4 or
                    player["costume"] != slot or player["rumble_enabled"]):
                raise ComparisonError(
                    f"MWRC v10 setup {match_index} port {slot + 1} is not the declared CPU9 profile")
        actual = tuple(player["character_kind"] for player in players)
        if actual != V10_FIGHTER_ROSTER[match_index]:
            raise ComparisonError(f"MWRC v10 setup {match_index} differs from the fighter-coverage roster")
        declared.append(setup)
    if len({player["character_kind"] for setup in declared for player in setup["players"]}) != 12:
        raise ComparisonError("MWRC v10 does not contain twelve distinct fighter identities")
    return declared


class Recipe:
    """Decoded MWRC recipe; v10 is admitted only by its explicit prefix scope."""

    def __init__(self, path: Path, raw: bytes, *, scope: str = WHOLE_SESSION_SCOPE) -> None:
        self.path = path
        self.raw = raw
        if scope not in {WHOLE_SESSION_SCOPE, V10_FIRST_SETUP_TICK0_SCOPE,
                         V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
                         V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                         V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                         V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
            raise ComparisonError(f"unsupported whole-session comparison scope {scope!r}")
        self.scope = scope
        if len(raw) < MWRC_HEADER.size + CONTEXT_HEADER.size:
            raise ComparisonError("MWRC recipe is truncated")
        magic, version, self.seed, count, self.characters, self.stages = MWRC_HEADER.unpack_from(raw)
        allowed_versions = ((MWRC_V8_VERSION, MWRC_VERSION) if scope == WHOLE_SESSION_SCOPE
                            else (MWRC_V10_VERSION,))
        if magic != b"MWRC" or version not in allowed_versions:
            if scope == WHOLE_SESSION_SCOPE:
                raise ComparisonError("whole-session comparison requires MWRC v8 or v9")
            message = ("v10 first-positive match-frame scope requires MWRC v10"
                       if scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE else
                       "v10 first-match clock-60 scope requires MWRC v10"
                       if scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE else
                       "v10 first-match clock-boundary scope requires MWRC v10"
                       if scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE else
                       "v10 ordered match-clock lineage scope requires MWRC v10"
                       if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE else
                       "v10 first-setup/tick-0 scope requires MWRC v10")
            raise ComparisonError(message)
        self.version = version
        self.entity_profile = (PRIMARY_STATIC_ENTITY_PROFILE
                               if version in (MWRC_VERSION, MWRC_V10_VERSION) else None)
        self.frame_count = count
        if not 1 <= count <= 108000:
            raise ComparisonError(f"MWRC frame count is outside v{version} bounds")
        cursor = MWRC_HEADER.size
        context_version, flags, context_size = CONTEXT_HEADER.unpack_from(raw, cursor)
        cursor += CONTEXT_HEADER.size
        if context_version != MWRC_CONTEXT_VERSION or flags != 0 or context_size != CONTEXT_BYTES:
            raise ComparisonError("MWRC first-CSS context header is unsupported")
        if cursor + CONTEXT_BYTES > len(raw):
            raise ComparisonError("MWRC context is truncated")
        context = raw[cursor:cursor + CONTEXT_BYTES]
        cursor += CONTEXT_BYTES
        self.game_rules = context[:0x18]
        self.save_data = context[0x18:0x18 + 0x55E8]
        self.css_data = context[0x18 + 0x55E8:0x18 + 0x55E8 + CSS_DATA_SIZE]
        self.ko_counts = context[-KO_COUNTS_SIZE:]
        self.match_setups: list[bytes] = []
        if version == MWRC_V8_VERSION:
            if cursor + GAME_INFO_SIZE + PAD_STATE_BYTES > len(raw):
                raise ComparisonError("MWRC first-CSS context or setup is truncated")
            self.setup = raw[cursor:cursor + GAME_INFO_SIZE]
            self.match_setups = [self.setup]
            cursor += GAME_INFO_SIZE
        else:
            if cursor + 4 > len(raw):
                raise ComparisonError(f"MWRC v{version} setup table header is truncated")
            setup_count, setup_flags = struct.unpack_from(">HH", raw, cursor)
            cursor += 4
            if (setup_flags != 0 or
                    (version == MWRC_V10_VERSION and setup_count != 3) or
                    (version == MWRC_VERSION and not 3 <= setup_count <= 64)):
                raise ComparisonError(f"MWRC v{version} setup table count or flags are invalid")
            setup_end = cursor + setup_count * GAME_INFO_SIZE
            if setup_end + PAD_STATE_BYTES > len(raw):
                raise ComparisonError(f"MWRC v{version} setup table is truncated")
            self.match_setups = [raw[cursor + index * GAME_INFO_SIZE:
                                      cursor + (index + 1) * GAME_INFO_SIZE]
                                 for index in range(setup_count)]
            self.setup = self.match_setups[0]
            cursor = setup_end
        self.initial_pad = raw[cursor:cursor + PAD_STATE_BYTES]
        cursor += PAD_STATE_BYTES
        self.frames: list[dict[str, Any]] = []
        for index in range(count):
            end = cursor + FRAME_BYTES
            if end > len(raw):
                raise ComparisonError(f"MWRC frame {index} is truncated")
            frame_raw = raw[cursor:end]
            pads = [frame_raw[offset:offset + PORT_BYTES].hex()
                    for offset in range(0, FRAME_BYTES, PORT_BYTES)]
            self.frames.append({"index": index, "pads": pads})
            cursor = end
        if cursor + 2 > len(raw):
            raise ComparisonError("MWRC span table is truncated")
        span_count = int.from_bytes(raw[cursor:cursor + 2], "big")
        cursor += 2
        if not 1 <= span_count <= MAX_SPANS:
            raise ComparisonError("MWRC span count is outside bounds")
        self.spans: list[dict[str, int]] = []
        next_frame = 0
        for index in range(span_count):
            if cursor + SPAN.size > len(raw):
                raise ComparisonError("MWRC span table is truncated")
            scene, span_flags, reserved, first, last = SPAN.unpack_from(raw, cursor)
            cursor += SPAN.size
            if span_flags != 0 or reserved != 0 or scene not in SCENE_NAMES:
                raise ComparisonError(f"MWRC span {index} has unsupported flags or scene")
            if not (0 <= first <= last < count) or first != next_frame:
                raise ComparisonError(f"MWRC span {index} is not ordered and contiguous")
            self.spans.append({"scene": scene, "first_frame": first, "last_frame": last})
            next_frame = last + 1
        if next_frame != count or self.spans[0]["scene"] != SCENES["css"]:
            raise ComparisonError("MWRC spans do not cover the admitted whole-session timeline")
        if version == MWRC_V10_VERSION:
            expected_route = [SCENES[name] for name in ("css", "sss", "match", "results") * 3]
            if [span["scene"] for span in self.spans] != expected_route:
                raise ComparisonError("MWRC v10 scene spans do not follow the three-match CSS/SSS route")
        if self.spans[-1]["scene"] not in (SCENES["results"], SCENES["prize"]):
            raise ComparisonError("MWRC timeline does not end in Results or Prize")
        self.declared_match_setups: list[dict[str, Any]] = []
        if version in (MWRC_VERSION, MWRC_V10_VERSION):
            match_spans = sum(span["scene"] == SCENES["match"] for span in self.spans)
            if len(self.match_setups) != 3 or match_spans != 3:
                raise ComparisonError(f"MWRC v{version} requires exactly three match setups and match spans")
            try:
                if version == MWRC_VERSION:
                    self.declared_match_setups = validate_milestone_setups(
                        [setup.hex() for setup in self.match_setups])
                else:
                    self.declared_match_setups = _validate_v10_setups(self.match_setups)
            except ValueError as error:
                raise ComparisonError(str(error)) from error
        if cursor != len(raw):
            raise ComparisonError("MWRC recipe has trailing bytes")
        for span in self.spans:
            for index in range(span["first_frame"], span["last_frame"] + 1):
                self.frames[index]["scene"] = span["scene"]


class BrowserReader:
    """Strict streaming reader for the browser diagnostic JSONL."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self.stream = path.open("r", encoding="utf-8")
        self.line = 0
        try:
            self.header = self._read()
        except StopIteration as error:
            self.stream.close()
            raise ComparisonError("browser trace is empty") from error
        except Exception:
            self.stream.close()
            raise
        try:
            self._require(self.header, {"record", "schema", "version", "frames_requested",
                                        "comparison", "cpu_observations", "draw_state"}, "browser header")
            if self.header["record"] != "header" or self.header["schema"] != BROWSER_SCHEMA \
                    or self.header["version"] != BROWSER_VERSION:
                raise ComparisonError("unsupported browser diagnostic header")
            if self.header["comparison"] != "not_run":
                raise ComparisonError("browser trace header comparison must be not_run")
            if self.header["cpu_observations"] != "not_captured" or self.header["draw_state"] != "not_captured":
                raise ComparisonError("browser trace carries an unsupported observation mode")
        except Exception:
            self.stream.close()
            raise

    @staticmethod
    def _require(row: Mapping[str, Any], keys: set[str], context: str) -> None:
        if set(row) != keys:
            missing = sorted(keys - set(row))
            unknown = sorted(set(row) - keys)
            raise ComparisonError(f"{context}: fields differ (missing={missing}, unknown={unknown})")

    def _read(self) -> dict[str, Any]:
        line = self.stream.readline()
        if not line:
            raise StopIteration
        self.line += 1
        if not line.strip():
            raise ComparisonError(f"browser trace line {self.line}: blank line")
        try:
            row = json.loads(line, object_pairs_hook=_reject_duplicate_keys)
        except (json.JSONDecodeError, ComparisonError) as error:
            raise ComparisonError(f"browser trace line {self.line}: invalid JSON: {error}") from error
        if not isinstance(row, dict):
            raise ComparisonError(f"browser trace line {self.line}: record is not an object")
        return row

    def next(self) -> dict[str, Any]:
        try:
            row = self._read()
        except StopIteration as error:
            raise ComparisonError("browser trace ended before the expected record") from error
        return row

    def expect_eof(self, *, max_records: int) -> None:
        """Require no records after the bounded export without reading past its cap."""
        if self.line >= max_records:
            if self.stream.read(1):
                raise ComparisonError("browser trace exceeds its bounded record count")
            return
        try:
            self._read()
        except StopIteration:
            return
        raise ComparisonError("browser trace contains records after the exported prefix")

    def close(self) -> None:
        self.stream.close()


class Comparator:
    def __init__(self, recipe: Recipe, browser: BrowserReader, *,
                 positive_boundary: Mapping[str, int] | None = None,
                 clock60_boundary: Mapping[str, int] | None = None,
                 match_clock_boundary: Mapping[str, int] | None = None,
                 ordered_clock_checkpoints: Sequence[Mapping[str, Any]] | None = None) -> None:
        self.recipe = recipe
        self.browser = browser
        self.ordered_clock_checkpoints: tuple[Mapping[str, int], ...] | None = None
        if recipe.scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE:
            if (not isinstance(positive_boundary, Mapping) or
                    not isinstance(clock60_boundary, Mapping) or
                    not isinstance(match_clock_boundary, Mapping) or
                    not isinstance(ordered_clock_checkpoints, Sequence) or
                    isinstance(ordered_clock_checkpoints, (str, bytes)) or
                    len(ordered_clock_checkpoints) < 3):
                raise ComparisonError("ordered match-clock scope lacks all frozen checkpoints")
            tuples: list[Mapping[str, int]] = []
            labels: set[str] = set()
            for index, checkpoint in enumerate(ordered_clock_checkpoints):
                label = checkpoint.get("label") if isinstance(checkpoint, Mapping) else None
                if (not isinstance(checkpoint, Mapping) or
                        not isinstance(label, str) or not label or label in labels or
                        not isinstance(checkpoint.get("tuple"), Mapping)):
                    raise ComparisonError("ordered match-clock checkpoint selection is malformed")
                if ((index == 0 and label != "clock1") or
                        (index == 1 and label != "clock60") or
                        (index == len(ordered_clock_checkpoints) - 1 and label != "target")):
                    raise ComparisonError("ordered match-clock checkpoint positions are malformed")
                labels.add(label)
                tuples.append(checkpoint["tuple"])
            if (_first_difference(tuples[0], positive_boundary) or
                    _first_difference(tuples[1], clock60_boundary) or
                    _first_difference(tuples[-1], {
                        key: value for key, value in match_clock_boundary.items()
                        if key != "target_match_frame_at_least"})):
                raise ComparisonError("ordered match-clock boundaries disagree with checkpoint list")
            self.ordered_clock_checkpoints = tuple(tuples)
            self.positive_boundary = positive_boundary
            self.clock60_boundary = clock60_boundary
            self.match_clock_boundary = match_clock_boundary
        elif ordered_clock_checkpoints is not None:
            raise ComparisonError("ordered clock checkpoints are only valid in their explicit scope")
        if recipe.scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE:
            pass
        elif recipe.scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE:
            if not isinstance(positive_boundary, Mapping):
                raise ComparisonError("first-positive scope lacks its frozen boundary target")
            if clock60_boundary is not None or match_clock_boundary is not None:
                raise ComparisonError("clock-boundary targets are only valid in their explicit scope")
            self.positive_boundary = positive_boundary
            self.clock60_boundary = None
            self.match_clock_boundary = None
        elif recipe.scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE:
            if (not isinstance(positive_boundary, Mapping) or
                    not isinstance(clock60_boundary, Mapping)):
                raise ComparisonError("clock-60 scope lacks its two frozen source boundaries")
            if match_clock_boundary is not None:
                raise ComparisonError("match-clock target is only valid in its explicit scope")
            self.positive_boundary = positive_boundary
            self.clock60_boundary = clock60_boundary
            self.match_clock_boundary = None
        elif recipe.scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE:
            if (not isinstance(positive_boundary, Mapping) or
                    not isinstance(clock60_boundary, Mapping) or
                    not isinstance(match_clock_boundary, Mapping)):
                raise ComparisonError(
                    "match-clock boundary scope lacks its positive, clock-60, and terminal checkpoints")
            self.positive_boundary = positive_boundary
            self.clock60_boundary = clock60_boundary
            self.match_clock_boundary = match_clock_boundary
        else:
            if (positive_boundary is not None or clock60_boundary is not None or
                    match_clock_boundary is not None):
                raise ComparisonError("bounded target is only valid in its explicit scope")
            self.positive_boundary = None
            self.clock60_boundary = None
            self.match_clock_boundary = None
        self.compare_fields = comparison_fields(recipe.version, scope=recipe.scope)
        if browser.header["frames_requested"] != recipe.frame_count:
            raise ComparisonError("browser header frames_requested disagrees with MWRC recipe")
        self.frame_index = 0
        self.current_match = -1
        self.setup_count = 0
        self.compared = 0
        self.nonmatch_compared = 0
        self.match_compared = 0
        self.first_difference: dict[str, Any] | None = None
        self.divergence: str | None = None
        self.source_spans: list[dict[str, int]] = []
        self.setup_states: list[dict[str, Any]] = []
        self.final_css_source: dict[str, Any] | None = None
        self.boundary_checkpoints: list[dict[str, Any]] = []
        self.last_observed_match_frame: int | None = None

    def fail(self, message: str, *, record: str | None = None,
             index: int | None = None, field: str | None = None,
             expected: Any = None, actual: Any = None) -> None:
        if self.divergence is None:
            self.divergence = message
            self.first_difference = {
                "record": record, "index": index, "field": field,
                "expected": expected, "actual": actual, "message": message,
            }
        raise ComparisonError(message)

    def _compare(self, expected: Mapping[str, Any], actual: Mapping[str, Any],
                 fields: Iterable[str], *, record: str, index: int,
                 context: str) -> None:
        left = {field: expected[field] for field in fields}
        right = {field: actual[field] for field in fields}
        difference = _first_difference(left, right)
        if difference:
            field, wanted, got = difference
            self.fail(f"{context}: exact state differs at {field}", record=record,
                      index=index, field=field, expected=wanted, actual=got)

    def _expect_frame_row(self, expected_scene: int, index: int) -> dict[str, Any]:
        try:
            row = self.browser.next()
        except ComparisonError as error:
            self.fail("browser trace is missing the expected session frame", record="session_frame",
                      index=index, expected={"scene": expected_scene, "index": index},
                      actual=str(error))
        expected_keys = {"record", "scene", "index", "supplied_inputs", "rng", "pad_state_hex"}
        if expected_scene == SCENES["match"]:
            expected_keys |= {"match_frame", "fighters"}
            if self.recipe.entity_profile is not None:
                expected_keys.add("fighter_entities")
        BrowserReader._require(row, expected_keys,
                               f"browser frame index {index} line {self.browser.line}")
        if row.get("record") != "session_frame":
            self.fail("browser record order contains a non-session frame", record=row.get("record"),
                      index=index, expected="session_frame", actual=row.get("record"))
        if row["scene"] != expected_scene or row["index"] != index:
            self.fail("browser scene/index is missing, extra, or reordered", record="session_frame",
                      index=index, field="scene/index",
                      expected={"scene": expected_scene, "index": index},
                      actual={"scene": row.get("scene"), "index": row.get("index")})
        inputs = row["supplied_inputs"]
        if not isinstance(inputs, list) or len(inputs) != PORT_COUNT:
            self.fail("browser supplied_inputs has the wrong port count", record="session_frame",
                      index=index, field="supplied_inputs", expected=PORT_COUNT, actual=inputs)
        for port, value in enumerate(inputs):
            _hex(value, PORT_BYTES, f"browser frame {index} supplied_inputs[{port}]")
        if inputs != self.recipe.frames[index]["pads"]:
            self.fail("browser supplied_inputs disagree with the MWRC input cursor",
                      record="session_frame", index=index, field="supplied_inputs",
                      expected=self.recipe.frames[index]["pads"], actual=inputs)
        _int(row["rng"], f"browser frame {index}.rng")
        _hex(row["pad_state_hex"], PAD_STATE_BYTES, f"browser frame {index}.pad_state_hex")
        if expected_scene == SCENES["match"]:
            _int(row["match_frame"], f"browser frame {index}.match_frame")
            _validate_browser_fighters(row["fighters"], f"browser frame {index}")
            if self.recipe.entity_profile is not None:
                row["fighter_entities"] = _browser_entities(
                    row["fighter_entities"], f"browser frame {index}", self.current_match)
        return row

    def on_setup(self, match_index: int, state: dict[str, Any], source_seq: int) -> None:
        if match_index != self.setup_count:
            raise ComparisonError(f"source setup match order is not contiguous ({match_index})")
        try:
            row = self.browser.next()
        except ComparisonError as error:
            self.fail("browser trace is missing the expected match setup record",
                      record="session_match_enter_complete", index=match_index,
                      expected="session_match_enter_complete", actual=str(error))
        expected_keys = {"record", "rng", "match_frame", "pad_state_hex", "fighters"}
        if self.recipe.entity_profile is not None:
            expected_keys |= {"declared_setup", "fighter_entities"}
        BrowserReader._require(row, expected_keys,
                               f"browser match_enter_complete before match {match_index}")
        _int(row["match_frame"], f"browser match setup {match_index}.match_frame")
        _validate_browser_fighters(row["fighters"], f"browser match setup {match_index}")
        if row.get("record") != "session_match_enter_complete":
            self.fail("browser match setup record is missing or reordered", record=row.get("record"),
                      index=self.frame_index, expected="session_match_enter_complete",
                      actual=row.get("record"))
        if self.recipe.entity_profile is not None:
            row["fighter_entities"] = _browser_entities(
                row["fighter_entities"], f"browser match {match_index} setup", match_index)
        self._compare(state, row, self.compare_fields, record="match_enter_complete",
                      index=match_index, context=f"match {match_index} setup")
        if self.recipe.entity_profile is not None:
            self._compare({"declared_setup": state["declared_setup"]}, row,
                          ("declared_setup",), record="match_enter_complete",
                          index=match_index, context=f"match {match_index} declared setup")
        self.setup_states.append(state)
        self.current_match = match_index
        self.setup_count += 1
        self.boundary_checkpoints.append({"match_index": match_index,
                                          "setup_source_seq": source_seq,
                                          "match_entry_frame": self.frame_index})

    def on_frame(self, frame: dict[str, Any]) -> None:
        index = self.frame_index
        if index >= self.recipe.frame_count:
            self.fail("source has an extra PAD-consumed frame", record="source_frame", index=index)
        recipe_frame = self.recipe.frames[index]
        expected_scene = frame["scene"]
        if recipe_frame["pads"] != frame["pads"] or recipe_frame["scene"] != expected_scene:
            self.fail("MWRC recipe disagrees with source PAD/scene order", record="source_frame",
                      index=index, field="scene/pads",
                      expected={"scene": recipe_frame["scene"], "pads": recipe_frame["pads"]},
                      actual={"scene": expected_scene, "pads": frame["pads"]})
        if self.source_spans and self.source_spans[-1]["scene"] == expected_scene:
            self.source_spans[-1]["last_frame"] = index
        else:
            self.source_spans.append({"scene": expected_scene, "first_frame": index,
                                      "last_frame": index})
        if (self.ordered_clock_checkpoints is not None and
                expected_scene == SCENES["match"]):
            checkpoints = self.ordered_clock_checkpoints
            assert self.match_clock_boundary is not None
            self._validate_match_clock_lineage_frame(
                frame, index, positive=checkpoints[0], intermediate=checkpoints[1:-1],
                terminal=self.match_clock_boundary,
                label=f"match-clock {self.match_clock_boundary['target_match_frame_at_least']}")
        elif (self.positive_boundary is not None and
                self.recipe.scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE and
                expected_scene == SCENES["match"]):
            boundary = self.positive_boundary
            match_index = _int(frame.get("match_index"), "source match index", 0, 2)
            source_tick = _int(frame.get("source_tick"), "source match tick")
            source_tick_seq = _int(frame.get("source_tick_seq"), "source match tick sequence",
                                   0, (1 << 64) - 1)
            pad_consume_seq = _int(frame.get("source_seq"), "source PAD consume sequence",
                                   0, (1 << 64) - 1)
            target_tick = boundary["source_tick"]
            state = frame.get("state")
            if match_index != boundary["match_index"]:
                raise ComparisonError("first-positive prefix left match 0 before its target")
            if index != _first_match_timeline_index(self.recipe) + source_tick:
                raise ComparisonError("source match tick is not contiguous with its recipe timeline")
            if source_tick < target_tick:
                if not isinstance(state, dict) or state.get("match_frame") != 0:
                    raise ComparisonError("source match clock became positive before the frozen target")
            elif source_tick == target_tick:
                actual_target = {
                    "match_index": match_index,
                    "source_tick": source_tick,
                    "source_sequence": source_tick_seq,
                    "pad_consume_sequence": pad_consume_seq,
                    "timeline_frame_index": index,
                    "browser_cursor": index + 1,
                    "match_frame": state.get("match_frame") if isinstance(state, dict) else None,
                }
                expected_target = {key: boundary[key] for key in actual_target}
                difference = _first_difference(expected_target, actual_target)
                if difference:
                    field, wanted, got = difference
                    raise ComparisonError(
                        f"source first-positive boundary differs at {field}: "
                        f"expected {wanted!r}, got {got!r}")
                if boundary["match_frame"] != 1:
                    raise ComparisonError("frozen first-positive target is not match_frame 1")
            else:
                raise ComparisonError("source prefix advanced past the frozen first-positive target")
        elif (self.match_clock_boundary is not None and
              expected_scene == SCENES["match"]):
            self._validate_match_clock_boundary_source_frame(frame, index)
        elif (self.clock60_boundary is not None and
              expected_scene == SCENES["match"]):
            self._validate_clock60_source_frame(frame, index)
        row = self._expect_frame_row(expected_scene, index)
        if index == 0 and expected_scene == SCENES["css"]:
            self._compare({"rng": self.recipe.seed}, row,
                          ("rng",), record="initial_css",
                          index=index, context="initial CSS")
        if expected_scene == SCENES["match"]:
            if "state" not in frame:
                raise ComparisonError(f"source match frame {index} lacks state snapshot")
            self._compare(frame["state"], row, self.compare_fields, record="session_frame",
                          index=index, context=f"match frame {index}")
            self.match_compared += 1
        else:
            # The original observer has no match clock or FighterHead slices
            # in menu/result scenes. Keep this explicit in the report.
            self.nonmatch_compared += 1
        self.compared += 1
        self.frame_index += 1

    def _validate_clock60_source_frame(self, frame: Mapping[str, Any], index: int) -> None:
        """Validate both frozen clock boundaries and unbroken clock lineage."""
        positive = self.positive_boundary
        terminal = self.clock60_boundary
        assert positive is not None and terminal is not None
        self._validate_match_clock_lineage_frame(
            frame, index, positive=positive, intermediate=(), terminal=terminal,
            label="clock-60")

    def _validate_match_clock_boundary_source_frame(self, frame: Mapping[str, Any],
                                                     index: int) -> None:
        """Validate the frozen clock-1, clock-60, and terminal clock checkpoints."""
        positive = self.positive_boundary
        clock60 = self.clock60_boundary
        terminal = self.match_clock_boundary
        assert positive is not None and clock60 is not None and terminal is not None
        self._validate_match_clock_lineage_frame(
            frame, index, positive=positive, intermediate=(clock60,), terminal=terminal,
            label=f"match-clock {terminal['target_match_frame_at_least']}")

    def _validate_match_clock_lineage_frame(self, frame: Mapping[str, Any], index: int, *,
                                            positive: Mapping[str, int],
                                            intermediate: Sequence[Mapping[str, int]],
                                            terminal: Mapping[str, int],
                                            label: str) -> None:
        """Apply the same strict match-0 clock lineage checks to any frozen boundary."""
        match_index = _int(frame.get("match_index"), "source match index", 0, 2)
        source_tick = _int(frame.get("source_tick"), "source match tick")
        source_tick_seq = _int(frame.get("source_tick_seq"), "source match tick sequence",
                               0, (1 << 64) - 1)
        pad_consume_seq = _int(frame.get("source_seq"), "source PAD consume sequence",
                               0, (1 << 64) - 1)
        state = frame.get("state")
        if not isinstance(state, Mapping):
            message = ("clock-60 source frame lacks typed state" if label == "clock-60"
                       else "match-clock source frame lacks typed state")
            raise ComparisonError(message)
        match_frame = _int(state.get("match_frame"), "source match clock")
        if match_index != 0:
            message = ("clock-60 prefix left match 0 before its target" if label == "clock-60"
                       else "match-clock prefix left match 0 before its target")
            raise ComparisonError(message)
        first_match_index = _first_match_timeline_index(self.recipe)
        if index != first_match_index + source_tick:
            raise ComparisonError("source match tick is not contiguous with its recipe timeline")
        if source_tick > terminal["source_tick"]:
            message = ("source prefix advanced past the frozen clock-60 target"
                       if label == "clock-60" else
                       "source prefix advanced past the frozen match-clock target")
            raise ComparisonError(message)

        actual = {
            "match_index": match_index,
            "source_tick": source_tick,
            "source_sequence": source_tick_seq,
            "pad_consume_sequence": pad_consume_seq,
            "timeline_frame_index": index,
            "browser_cursor": index + 1,
            "match_frame": match_frame,
        }
        checkpoints = (positive, *intermediate, terminal)
        exact_checkpoint = next((checkpoint for checkpoint in checkpoints
                                 if source_tick == checkpoint["source_tick"]), None)
        if source_tick < positive["source_tick"]:
            if match_frame != 0:
                raise ComparisonError("source match clock became positive before its frozen first-positive boundary")
        elif source_tick == positive["source_tick"]:
            difference = _first_difference(positive, actual)
            if difference:
                raise ComparisonError(
                    f"source first-positive boundary differs at {difference[0]}")
        else:
            previous = self.last_observed_match_frame
            regression_or_jump = (previous is None or match_frame < previous or
                                  match_frame > previous + 1)
            if exact_checkpoint is not None:
                if regression_or_jump or previous >= exact_checkpoint["match_frame"]:
                    if label == "clock-60":
                        raise ComparisonError(
                            "source match clock regressed, jumped, or reached 60 before the frozen target")
                    raise ComparisonError(
                        f"source match clock regressed, jumped, or reached frozen {label} before its tick")
                boundary_expected = {
                    key: value for key, value in exact_checkpoint.items()
                    if key != "target_match_frame_at_least"
                }
                difference = _first_difference(boundary_expected, actual)
                if difference:
                    boundary_name = "clock-60" if label == "clock-60" else label
                    raise ComparisonError(f"source {boundary_name} boundary differs at {difference[0]}")
            else:
                next_checkpoint = next(checkpoint for checkpoint in checkpoints
                                       if checkpoint["source_tick"] > source_tick)
                if regression_or_jump or match_frame >= next_checkpoint["match_frame"]:
                    if label == "clock-60":
                        raise ComparisonError(
                            "source match clock regressed, jumped, or reached 60 before the frozen target")
                    raise ComparisonError(
                        f"source match clock regressed, jumped, or reached a frozen {label} checkpoint early")
        self.last_observed_match_frame = match_frame

    def on_final_css(self, state: dict[str, Any], source_seq: int) -> None:
        self.final_css_source = state
        self.boundary_checkpoints.append({"final_css_source_seq": source_seq,
                                          "final_css_frame": None})

    def finalize(self) -> dict[str, Any]:
        if self.frame_index != self.recipe.frame_count:
            raise ComparisonError("source frame count does not cover the MWRC recipe")
        if self.source_spans != self.recipe.spans:
            raise ComparisonError("source scene spans disagree with the MWRC recipe")
        if self.setup_count == 0 or (self.recipe.entity_profile is not None and self.setup_count != 3):
            raise ComparisonError("source session has the wrong number of match setups")
        row = self.browser.next()
        if row.get("record") != "end":
            raise ComparisonError("browser trace has an extra or missing terminal record")
        BrowserReader._require(row, {"record", "frames", "status"}, "browser end")
        if row["frames"] != self.recipe.frame_count or row["status"] != "captured":
            raise ComparisonError("browser terminal record disagrees with the recipe frame count")
        try:
            extra = self.browser.next()
        except ComparisonError as error:
            if "ended before the expected record" in str(error):
                extra = None
            else:
                raise
        if extra is not None:
            raise ComparisonError("browser trace contains records after end")
        return {}


class SourceCollector:
    """Stream MWRO and invoke a comparator without retaining its huge slices."""

    def __init__(self, callback: Comparator, recipe: Recipe,
                 source_manifest: Mapping[str, Any] | None = None,
                 source_audit: Mapping[str, Any] | None = None,
                 source_expectations: Mapping[str, Any] | None = None) -> None:
        self.callback = callback
        self.recipe = recipe
        self.source_manifest = source_manifest
        self.source_audit = source_audit
        self.source_expectations = source_expectations
        self.handshake: dict[str, Any] | None = None
        self.start: dict[str, Any] | None = None
        self.end: dict[str, Any] | None = None
        self.scene: str | None = None
        self.match_index = 0
        self.match_count = sum(1 for span in recipe.spans if span["scene"] == SCENES["match"])
        self.started = False
        self.finished = False
        self.pending: dict[str, Any] | None = None
        self.lifecycle: dict[int, list[tuple[str, int, int, int]]] = {}
        self.lifecycle_rows: list[dict[str, Any]] = []
        self.first_css: dict[str, Any] | None = None
        self.setup_bytes: list[bytes] = []
        self.entry_setup_bytes: bytes | None = None
        self.first_entry_seq: int | None = None
        self.first_setup_seq: int | None = None
        self.first_setup_source_tick: int | None = None
        self.first_source_tick_seq: int | None = None
        self.first_source_tick: int | None = None
        self.prefix_binding_validated = False
        self.entity_previous: dict[int, dict[int, tuple[int, int]]] = {}
        self.final_css: dict[str, Any] | None = None
        self.last_source_tick: dict[int, int] = {}
        self.last_source_tick_seq: int | None = None
        self.record_count = 0

    def _lifecycle(self, boundary: str, row: Mapping[str, Any]) -> None:
        payload = row["payload"]
        match = _int(payload.get("match_index"), f"{boundary}.match_index", 0, 63)
        if match != self.match_index:
            raise ComparisonError(f"{boundary} match index {match} disagrees with active {self.match_index}")
        self.lifecycle.setdefault(match, []).append((boundary, row["seq"], row["source_tick"], row["draw_ordinal"]))
        self.lifecycle_rows.append(dict(row))

    def _transition(self, boundary: str, row: Mapping[str, Any]) -> None:
        self._lifecycle(boundary, row)
        if boundary in {"prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                        "startup_prize_mode_exit"} and not self.started:
            return
        if boundary == "css_enter":
            if self.started or self.scene is not None or self.match_index != 0:
                raise ComparisonError("source CSS entry is duplicated or reordered")
            self.started = True
            self.scene = "css"
            return
        if not self.started:
            raise ComparisonError(f"source boundary {boundary} preceded first CSS entry")
        if boundary in {"css_cancel_enter", "return_css"}:
            if boundary == "return_css":
                if self.scene not in {"results", "prize"}:
                    raise ComparisonError("source return_css arrived outside Results/Prize")
                if self.pending is not None:
                    raise ComparisonError("source return_css has an uncommitted VS state")
                if self.match_index + 1 > self.match_count:
                    raise ComparisonError("source return_css match index is invalid")
                if self.match_index + 1 < self.match_count:
                    self.match_index += 1
                    self.scene = "css"
                else:
                    self.finished = True
                    self.scene = "css"
            else:
                self.scene = "css"
            return
        expected = {
            "css_exit": "css", "sss_enter": "css", "sss_exit": "sss",
            "entry": "sss", "setup": "match", "draw_return": "match",
            "vs_exit": "match", "vs_exit_return": "match", "vs_mode_exit": "match",
            "results_enter": "match", "results_gobj": "results",
            "results_exit": "results", "results_mode_exit": "results",
            "scene_teardown": "results", "prize_mode_enter": "results",
            "prize_scene_enter": "prize", "prize_scene_exit": "prize",
            "prize_mode_exit": "prize",
        }.get(boundary)
        if expected is None:
            raise ComparisonError(f"unsupported source lifecycle boundary {boundary}")
        if self.scene != expected:
            raise ComparisonError(f"source {boundary} expected scene {expected}, got {self.scene}")
        if boundary == "sss_enter":
            self.scene = "sss"
        elif boundary == "entry":
            self.scene = "match"
        elif boundary == "results_enter":
            self.scene = "results"
        elif boundary == "prize_mode_enter":
            self.scene = "prize"

    def _poll(self, row: Mapping[str, Any]) -> None:
        # pad_poll is retained as source evidence, but it has no browser join:
        # it carries PAD without an RNG slice and can be interrupted by VI
        # polls.  Do not assign it to a session_frame by adjacency.
        if self.scene in {"css", "sss", "match", "results"} and not self.finished:
            _pad_value(row["payload"], "source pad_poll")

    def _consume(self, row: Mapping[str, Any]) -> None:
        # Startup PAD traffic precedes the first CSS lifecycle owner and is
        # outside the admitted MWRC timeline.  Once the session has started,
        # every consume must belong to a scene (including the final CSS
        # transition, which deliberately has no consumed frame).
        if not self.started and self.scene is None:
            return
        if self.scene not in {"css", "sss", "match", "results"} or self.finished:
            raise ComparisonError("source PAD consume occurred outside the admitted session")
        if self.scene == "match" and self.pending is not None:
            raise ComparisonError("source VS has multiple PAD consumes before source_tick")
        frame = {"scene": SCENES[self.scene], "pads": _consumed_ports(row, 0),
                 "source_seq": row["seq"],
                 "source_tick": row["source_tick"]}
        if self.scene == "match":
            self.pending = frame
        else:
            self.callback.on_frame(frame)

    def _setup(self, row: Mapping[str, Any]) -> None:
        if self.scene != "match":
            raise ComparisonError("source setup occurred outside VS")
        if _is_v10_prefix_scope(self.recipe.scope) and (
                self.match_index != 0 or self.setup_bytes):
            raise ComparisonError("v10 first-match prefix contains an extra or reordered setup")
        if _is_v10_prefix_scope(self.recipe.scope) and self.pending is not None:
            raise ComparisonError("v10 first-match setup followed an unjoined PAD consume")
        _, raw = _slice(row["payload"], "match_setup", GAME_INFO_SIZE, "source setup")
        self.setup_bytes.append(raw)
        if _is_v10_prefix_scope(self.recipe.scope):
            first_setup = (self.source_audit or {}).get("first_setup")
            if (not isinstance(first_setup, dict) or
                    row.get("seq") != first_setup.get("seq") or
                    row.get("source_tick") != first_setup.get("source_tick")):
                raise ComparisonError("source first setup is detached from the bounded identity audit")
            if self.entry_setup_bytes is None or raw != self.entry_setup_bytes:
                raise ComparisonError("source setup bytes disagree with the bound entry setup")
            self._validate_prefix_binding()
            self.first_setup_seq = row["seq"]
            self.first_setup_source_tick = row["source_tick"]
        state = self._match_state(row["payload"], "source setup")
        if self.recipe.entity_profile is not None:
            state["declared_setup"] = _decode_setup(raw.hex())
        self.callback.on_setup(self.match_index, state, row["seq"])

    def _entry(self, row: Mapping[str, Any]) -> None:
        if not _is_v10_prefix_scope(self.recipe.scope):
            return
        expected_entry_seq = (self.source_audit or {}).get("first_entry_verified_seq")
        if (self.match_index != 0 or type(expected_entry_seq) is not int or
                row.get("seq") != expected_entry_seq):
            raise ComparisonError("source first entry is detached from the bounded identity audit")
        if self.entry_setup_bytes is not None:
            raise ComparisonError("source has duplicate first entry setup")
        item, raw = _slice(row["payload"], "match_setup", GAME_INFO_SIZE, "source entry")
        pointer = _gpr(row["payload"], 3, "source entry")
        if item.get("address") != pointer:
            raise ComparisonError("source entry setup slice address disagrees with source r3")
        self.entry_setup_bytes = raw
        self.first_entry_seq = row["seq"]

    def _tick(self, row: Mapping[str, Any]) -> None:
        if self.scene != "match" or self.pending is None:
            raise ComparisonError("source_tick is missing its preceding VS PAD consume")
        if _is_v10_prefix_scope(self.recipe.scope):
            if self.callback.setup_count != 1 or not self.prefix_binding_validated:
                raise ComparisonError("source first tick preceded its bound setup")
            if self.first_source_tick_seq is None:
                first_tick = (self.source_audit or {}).get("first_source_tick")
                if (not isinstance(first_tick, dict) or self.match_index != 0 or
                        row.get("seq") != first_tick.get("seq") or
                        row.get("source_tick") != first_tick.get("source_tick") or
                        row.get("source_tick") != 0):
                    raise ComparisonError(
                        "source first tick is detached from the bounded identity audit")
        state = self._match_state(row["payload"], "source_tick")
        expected_tick = self.last_source_tick.get(self.match_index, -1) + 1
        if row["source_tick"] != expected_tick:
            raise ComparisonError(
                f"source_tick for match {self.match_index} is not contiguous "
                f"(expected {expected_tick}, got {row['source_tick']})")
        if self.pending["source_tick"] != row["source_tick"]:
            raise ComparisonError("source_tick envelope disagrees with its PAD consume")
        if state["scene_frame"] != row["source_tick"]:
            raise ComparisonError("source_tick envelope disagrees with source scene frame")
        frame = self.pending
        frame["state"] = state
        frame["match_index"] = self.match_index
        frame["source_tick_seq"] = row["seq"]
        self.callback.on_frame(frame)
        self.pending = None
        self.last_source_tick[self.match_index] = row["source_tick"]
        self.last_source_tick_seq = row["seq"]
        if _is_v10_prefix_scope(self.recipe.scope) and self.first_source_tick_seq is None:
            self.first_source_tick_seq = row["seq"]
            self.first_source_tick = row["source_tick"]

    def _validate_prefix_binding(self) -> None:
        """Bind source identity, CSS context, lifecycle, and setup before tick 0."""
        if self.prefix_binding_validated:
            return
        if not _is_v10_prefix_scope(self.recipe.scope):
            raise ComparisonError("prefix binding is only valid in the explicit v10 prefix scope")
        if (self.source_manifest is None or self.source_audit is None or
                self.source_expectations is None):
            raise ComparisonError("v10 prefix validation lacks frozen source identities")
        if self.handshake is None or self.start is None or self.first_css is None:
            raise ComparisonError("v10 prefix is missing its source announcement or first CSS context")
        identity = _source_prefix_identity(self.handshake, self.start)
        source_input = self.source_manifest.get("input")
        capture = source_input.get("capture") if isinstance(source_input, dict) else None
        if not isinstance(capture, dict):
            raise ComparisonError("source candidate manifest lacks capture identity")
        if (identity["capture_id"] != self.source_expectations.get("capture_id") or
                identity["sequence_id"] != self.source_expectations.get("sequence_id")):
            raise ComparisonError("source observer identity disagrees with frozen expectations")
        for field in ("capture_id", "sequence_id", "match_count", "source_revision",
                      "observer_schema", "observer_version", "dolphin_commit", "dol_sha1",
                      "dol_sha256", "cpu"):
            if capture.get(field) != identity.get(field):
                raise ComparisonError(f"source candidate manifest capture.{field} disagrees with observer")

        names = [row["payload"].get("boundary") for row in self.lifecycle_rows]
        startup_prelude = ["prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                           "startup_prize_mode_exit"]
        if names[:len(startup_prelude)] == startup_prelude:
            route_rows = self.lifecycle_rows[len(startup_prelude):]
        elif any(name in startup_prelude for name in names):
            raise ComparisonError("source startup Prize lifecycle prefix is incomplete or reordered")
        else:
            route_rows = self.lifecycle_rows
        expected_lifecycle = list(semantics.WHOLE_OBSERVER_ORDER[:6])
        actual_lifecycle = [row["payload"].get("boundary") for row in route_rows]
        if actual_lifecycle != expected_lifecycle:
            raise ComparisonError("source first CSS/SSS/entry/setup lifecycle prefix is missing or reordered")
        for row in self.lifecycle_rows:
            payload = row["payload"]
            name = payload.get("boundary")
            if (payload.get("match_index") != 0 or
                    payload.get("pc") != semantics.WHOLE_OBSERVER_PCS[name]):
                raise ComparisonError(f"source first lifecycle boundary {name} has wrong match or source PC")

        if self.entry_setup_bytes != self.recipe.match_setups[0]:
            raise ComparisonError("MWRC first setup differs from source entry raw StartMeleeData")
        if self.setup_bytes != [self.recipe.match_setups[0]]:
            raise ComparisonError("source first setup differs from the complete MWRC setup table")

        css_row = {"event": "boundary", "seq": self.first_css["source_seq"],
                   "source_tick": self.first_css["source_tick"],
                   "draw_ordinal": 0, "payload": self.first_css["payload"]}
        try:
            css = _first_css_context([css_row])
        except (WholeSessionReplayError, KeyError, TypeError, ValueError) as error:
            raise ComparisonError(f"source first CSS context is invalid: {error}") from error
        expected_css = self.source_manifest["input"]["first_css"]
        for field in ("game_rules_hex", "save_data_hex", "css_data_hex", "ko_counts_hex",
                      "pad_state_hex", "rng", "profile_masks",
                      "profile_game_rules_sha256", "profile_save_data_sha256",
                      "observer_seq", "source_tick"):
            if css.get(field) != expected_css.get(field):
                raise ComparisonError(f"source first CSS {field} disagrees with candidate manifest")
        if self.source_manifest["input"].get("profile_context") != css.get("profile_context"):
            raise ComparisonError("source first CSS profile context disagrees with candidate manifest")
        if (bytes.fromhex(css["game_rules_hex"]) != self.recipe.game_rules or
                bytes.fromhex(css["save_data_hex"]) != self.recipe.save_data or
                bytes.fromhex(css["css_data_hex"]) != self.recipe.css_data or
                bytes.fromhex(css["ko_counts_hex"]) != self.recipe.ko_counts or
                bytes.fromhex(css["pad_state_hex"]) != self.recipe.initial_pad or
                css["rng"] != self.recipe.seed or
                css["profile_masks"]["characters"] != self.recipe.characters or
                css["profile_masks"]["stages"] != self.recipe.stages):
            raise ComparisonError("MWRC first-CSS context disagrees with typed source context")
        first_setup = self.source_manifest["input"]["match_setups"][0]
        if (first_setup.get("match_index") != 0 or
                first_setup.get("start_melee_hex") != self.recipe.match_setups[0].hex() or
                first_setup.get("declared_setup") != self.recipe.declared_match_setups[0] or
                self.source_manifest["input"].get("setup_hex") != self.recipe.setup.hex() or
                self.source_manifest["input"].get("declared_setup") !=
                self.recipe.declared_match_setups[0]):
            raise ComparisonError("source candidate manifest first setup disagrees with MWRC v10")
        self.prefix_binding_validated = True

    def _validate_initial_css_binding(self) -> None:
        """Bind source first-CSS typed context, seed, and initial PAD to MWRC."""
        if self.first_css is None:
            raise ComparisonError("source stream lacks first CSS boundary")
        payload = self.first_css["payload"]
        _, rules = _slice(payload, "profile_game_rules", 0x18, "first CSS")
        _, save = _slice(payload, "profile_save_data", 0x55E8, "first CSS")
        _, css = _slice(payload, "menu_css_context", CSS_DATA_SIZE, "first CSS")
        _, ko = _slice(payload, "menu_css_ko_counts", KO_COUNTS_SIZE, "first CSS")
        if (rules != self.recipe.game_rules or save != self.recipe.save_data or
                css != self.recipe.css_data or ko != self.recipe.ko_counts):
            raise ComparisonError("MWRC first-CSS typed context disagrees with source")
        chars = int.from_bytes(_slice(payload, "profile_characters", 2, "first CSS")[1], "big")
        stages = int.from_bytes(_slice(payload, "profile_stages", 2, "first CSS")[1], "big")
        if chars != self.recipe.characters or stages != self.recipe.stages:
            raise ComparisonError("MWRC unlock masks disagree with source first CSS")
        if self.first_css["rng"] != self.recipe.seed:
            raise ComparisonError("MWRC seed disagrees with source first CSS RNG")
        if bytes.fromhex(self.first_css["pad_state_hex"]) != self.recipe.initial_pad:
            raise ComparisonError("MWRC initial PAD state disagrees with source first CSS")

    def _match_state(self, payload: Mapping[str, Any], context: str) -> dict[str, Any]:
        state = _state_from_payload(payload, context)
        state.update(_snapshot_values(payload, context))
        if self.recipe.entity_profile is not None:
            previous = self.entity_previous.setdefault(self.match_index, {})
            state["fighter_entities"] = _fighter_entities(
                payload, context, self.match_index, previous)
        return state

    def consume(self, row: Mapping[str, Any]) -> None:
        event = row.get("event")
        if self.end is not None:
            raise ComparisonError("source stream contains records after end")
        # The raw decoder checks continuity after its first record. This
        # complete-session consumer also requires the original zero origin
        # and announcement order; lifecycle filtering must not repair them.
        sequence = _int(row.get("seq"), "source sequence", 0, (1 << 64) - 1)
        if sequence != self.record_count:
            raise ComparisonError(f"source sequence must equal record index {self.record_count}")
        if self.record_count < 2 and event != ("handshake", "start")[self.record_count]:
            raise ComparisonError("source stream must begin with handshake then start")
        self.record_count += 1
        if event == "handshake":
            if self.handshake is not None:
                raise ComparisonError("source has duplicate handshake")
            self.handshake = row["payload"]
            return
        if event == "start":
            if self.start is not None:
                raise ComparisonError("source has duplicate start")
            self.start = row["payload"]
            return
        if event == "end":
            if self.end is not None:
                raise ComparisonError("source has duplicate end")
            self.end = row["payload"]
            return
        if event != "boundary":
            raise ComparisonError(f"source contains unsupported event {event!r}")
        payload = row.get("payload")
        if not isinstance(payload, dict) or payload.get("whole_session") is not True:
            raise ComparisonError("source boundary is not flagged whole-session")
        boundary = payload.get("boundary")
        if boundary in {"css_enter", "css_cancel_enter", "css_exit", "sss_enter", "sss_exit",
                        "entry", "setup", "draw_return", "vs_exit", "vs_exit_return",
                        "vs_mode_exit", "results_enter", "results_gobj", "results_exit",
                        "results_mode_exit", "scene_teardown", "return_css", "prize_mode_enter",
                        "prize_scene_enter", "prize_scene_exit", "prize_mode_exit",
                        "startup_prize_mode_exit"}:
            self._transition(boundary, row)
            if boundary == "css_enter" and self.first_css is None:
                self.first_css = {**_snapshot_values(payload, "first CSS"), "payload": payload,
                                  "source_seq": row["seq"], "source_tick": row["source_tick"]}
            elif boundary == "entry":
                self._entry(row)
            elif boundary == "setup":
                self._setup(row)
            elif boundary == "return_css" and self.finished:
                self.final_css = {**_snapshot_values(payload, "final CSS"),
                                  "source_seq": row["seq"], "source_tick": row["source_tick"]}
                self.callback.on_final_css(self.final_css, row["seq"])
            return
        if boundary == "pad_poll":
            self._poll(row)
        elif boundary == "pad_consume":
            self._consume(row)
        elif boundary == "source_tick":
            self._tick(row)
        else:
            # Typed diagnostics such as fighter_create/draw_enter are retained
            # by the raw stream but do not define the replay timeline. Unknown
            # boundaries remain errors in the decoder, while known diagnostics
            # are deliberately ignored here after their stream-level checks.
            if boundary not in {"fighter_create", "draw_enter", "scene_exit", "result_enter",
                                "result_return", "scene_reset", "css_exit", "sss_exit"}:
                raise ComparisonError(f"source contains unsupported boundary {boundary!r}")

    def finish(self) -> None:
        if _is_v10_prefix_scope(self.recipe.scope):
            if self.recipe.scope == V10_FIRST_SETUP_TICK0_SCOPE:
                raise ComparisonError(
                    "v10 first-setup/tick-0 evidence cannot finish a whole-session comparison")
            raise ComparisonError(
                "v10 first-positive match-frame evidence cannot finish a whole-session comparison")
        if self.handshake is None or self.start is None or self.end is None:
            raise ComparisonError("source stream is missing handshake, start, or end")
        if self.handshake.get("whole_session") is not True or self.start.get("whole_session") is not True:
            raise ComparisonError("source stream is not a whole-session capture")
        if self.handshake.get("capture_id") != self.start.get("capture_id") \
                or self.handshake.get("sequence_id") != self.start.get("sequence_id"):
            raise ComparisonError("source capture/sequence identity disagrees")
        if self.start.get("source_revision") != "GALE01r2":
            raise ComparisonError("source revision is not GALE01r2")
        if self.start.get("match_count") != len(self.setup_bytes):
            raise ComparisonError("source match count disagrees with setup boundaries")
        if self.start.get("match_count") != self.match_count:
            raise ComparisonError("source match count disagrees with MWRC scene spans")
        try:
            semantics.validate_whole_session_observer_records(
                [{"event": "handshake", "payload": self.handshake},
                 {"event": "start", "payload": self.start},
                 *self.lifecycle_rows])
        except (semantics.SemanticError, KeyError, TypeError, ValueError) as error:
            raise ComparisonError(f"source lifecycle is missing, extra, or reordered: {error}") from error
        if not self.finished or self.pending is not None:
            raise ComparisonError("source stream did not complete the final return to CSS")
        if self.end.get("status") != "completed" or self.end.get("natural") is not True:
            raise ComparisonError("source stream did not complete naturally")
        if self.first_css is None or self.final_css is None:
            raise ComparisonError("source stream lacks first or final CSS boundary")
        if self.recipe.version == MWRC_V8_VERSION:
            if (self.setup_bytes[0] != self.recipe.setup or
                    any(raw != self.recipe.setup for raw in self.setup_bytes)):
                raise ComparisonError("source StartMeleeData setup changed or disagrees with MWRC v8")
        elif self.setup_bytes != self.recipe.match_setups:
            raise ComparisonError("source per-match StartMeleeData disagrees with the MWRC v9 setup table")
        try:
            self._validate_initial_css_binding()
        except (WholeSessionReplayError, KeyError, TypeError, ValueError) as error:
            raise ComparisonError(f"source first-CSS context is invalid: {error}") from error
        self.callback.finalize()


def _read_json_sidecar(path: Path, context: str, *, max_bytes: int = 16 * 1024 * 1024
                       ) -> tuple[dict[str, Any], str, int]:
    try:
        size = path.stat().st_size
        if size > max_bytes:
            raise ComparisonError(f"{context} exceeds its {max_bytes}-byte bound")
        raw = path.read_bytes()
    except OSError as error:
        raise ComparisonError(f"{context} cannot be read: {error}") from error
    if len(raw) != size:
        raise ComparisonError(f"{context} changed size while being read")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError, ComparisonError) as error:
        raise ComparisonError(f"{context} is invalid JSON: {error}") from error
    if not isinstance(value, dict):
        raise ComparisonError(f"{context} must be a JSON object")
    return value, hashlib.sha256(raw).hexdigest(), size


def _browser_report(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (OSError, UnicodeDecodeError, json.JSONDecodeError, ComparisonError) as error:
        raise ComparisonError(f"browser report cannot be read: {error}") from error
    if not isinstance(value, dict):
        raise ComparisonError("browser report must be an object")
    return value


def _path_identity(value: Any, expected: Path, context: str) -> None:
    if not isinstance(value, str) or Path(value).resolve() != expected.resolve():
        raise ComparisonError(f"{context} path does not bind to the selected input")


EXPECTATION_SCHEMA = "melee-web-v10-first-setup-tick0-expectations"
POSITIVE_EXPECTATION_SCHEMA = "melee-web-v10-first-positive-match-frame-expectations"
CLOCK60_EXPECTATION_SCHEMA = "melee-web-v10-first-match-clock-ge60-expectations"
MATCH_CLOCK_EXPECTATION_SCHEMA = "melee-web-v10-first-match-clock-boundary-expectations"
ORDERED_CLOCK_LINEAGE_SCHEMA = "melee-web-v10-ordered-match-clock-lineage-v1"
ORDERED_CLOCK_LINEAGE_EXPECTATION_SCHEMA = "melee-web-v10-first-match-clock-ordered-lineage-expectations"
ORDERED_CLOCK_ANCHORS = ("clock1", "clock60")
ORDERED_CLOCK_TARGET = "target"
ORDERED_CLOCK_TUPLE_FIELDS = frozenset({
    "match_index", "source_tick", "source_sequence", "pad_consume_sequence",
    "timeline_frame_index", "browser_cursor", "match_frame",
})
ORDERED_CLOCK_PREFIX_FIELDS = frozenset({
    "bytes_read", "records_read", "last_source_sequence", "sha256",
})


def _ordered_audit_checkpoint_status_key(checkpoint: Mapping[str, Any],
                                         target_clock: int) -> str:
    label = checkpoint["label"]
    if label == "clock1":
        return "first_positive_clock1"
    if label == "clock60":
        return "first_clock60"
    if label == ORDERED_CLOCK_TARGET:
        return f"target_clock{target_clock}"
    return f"first_match_{label}"


def _ordered_audit_checkpoint_observation_key(checkpoint: Mapping[str, Any],
                                               target_clock: int) -> str:
    label = checkpoint["label"]
    if label == "clock1":
        return "first_positive_observed"
    if label == "clock60":
        return "clock60_observed"
    if label == ORDERED_CLOCK_TARGET:
        return f"target_clock_ge{target_clock}_observed"
    return f"{label}_observed"


def _ordered_runner_checkpoint_observation_key(checkpoint: Mapping[str, Any]) -> str:
    label = checkpoint["label"]
    if label == "clock1":
        return "first_positive"
    if label == "clock60":
        return "target_clock_at_least_60"
    return f"target_clock_ge{label[5:]}_observed"


def _validate_ordered_clock_lineage_expectations(source: Mapping[str, Any],
                                                 recipe: Mapping[str, Any]
                                                 ) -> list[dict[str, Any]]:
    lineage = source.get("ordered_clock_lineage")
    if (not isinstance(lineage, dict) or
            lineage.get("schema") != ORDERED_CLOCK_LINEAGE_SCHEMA or
            set(lineage) != {"schema", "checkpoints", "runner_packet",
                             "supporting_expectations"}):
        raise ComparisonError("ordered clock-lineage expectations schema is malformed")
    checkpoints = lineage.get("checkpoints")
    if not isinstance(checkpoints, list) or len(checkpoints) < 3:
        raise ComparisonError("ordered clock-lineage needs two anchors and a terminal checkpoint")
    normalized: list[dict[str, Any]] = []
    seen_labels: set[str] = set()
    for index, item in enumerate(checkpoints):
        if (not isinstance(item, dict) or set(item) != {"label", "tuple", "prefix", "audit"} or
                not isinstance(item.get("label"), str) or not item["label"] or
                item["label"] in seen_labels):
            raise ComparisonError("ordered clock-lineage checkpoint labels or shape are malformed")
        label = item["label"]
        seen_labels.add(label)
        if ((index < len(ORDERED_CLOCK_ANCHORS) and label != ORDERED_CLOCK_ANCHORS[index]) or
                (index == len(checkpoints) - 1 and label != ORDERED_CLOCK_TARGET) or
                (len(ORDERED_CLOCK_ANCHORS) <= index < len(checkpoints) - 1 and
                 re.fullmatch(r"clock[1-9][0-9]*", label) is None)):
            raise ComparisonError("ordered clock-lineage anchor or terminal position is malformed")
        value = item.get("tuple")
        if not isinstance(value, dict) or set(value) != ORDERED_CLOCK_TUPLE_FIELDS:
            raise ComparisonError(f"ordered clock-lineage {label} tuple fields are malformed")
        for field, entry in value.items():
            _int(entry, f"ordered clock-lineage {label}.{field}",
                 1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
                 (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
                 else MAX_UINT32)
        prefix = item.get("prefix")
        if not isinstance(prefix, dict) or set(prefix) != ORDERED_CLOCK_PREFIX_FIELDS:
            raise ComparisonError(f"ordered clock-lineage {label} prefix fields are malformed")
        _int(prefix.get("bytes_read"), f"ordered clock-lineage {label} prefix bytes",
             1, V10_ORDERED_LINEAGE_BYTE_CAP)
        records = _int(prefix.get("records_read"),
                       f"ordered clock-lineage {label} prefix records",
                       1, V10_ORDERED_LINEAGE_RECORD_CAP)
        last_sequence = _int(prefix.get("last_source_sequence"),
                             f"ordered clock-lineage {label} last source sequence",
                             1, (1 << 64) - 1)
        if (records != value["source_sequence"] + 1 or
                last_sequence != value["source_sequence"] or
                not isinstance(prefix.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", prefix["sha256"]) is None):
            raise ComparisonError(f"ordered clock-lineage {label} prefix identity is malformed")
        audit = item.get("audit")
        if (not isinstance(audit, dict) or set(audit) != {"path", "bytes", "sha256"} or
                not isinstance(audit.get("path"), str) or not audit["path"] or
                type(audit.get("bytes")) is not int or audit["bytes"] <= 0 or
                not isinstance(audit.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", audit["sha256"]) is None):
            raise ComparisonError(f"ordered clock-lineage {label} audit identity is malformed")
        if (value["match_index"] != 0 or
                value["timeline_frame_index"] + 1 != value["browser_cursor"]):
            raise ComparisonError(f"ordered clock-lineage {label} tuple is outside match-0 timeline")
        label_clock = (int(label[5:]) if label.startswith("clock") else
                       value["match_frame"])
        if value["match_frame"] != label_clock:
            raise ComparisonError(f"ordered clock-lineage {label} label differs from its clock")
        if value["source_sequence"] <= value["pad_consume_sequence"]:
            raise ComparisonError(f"ordered clock-lineage {label} PAD join is malformed")
        if index and (
                value["source_tick"] <= normalized[-1]["tuple"]["source_tick"] or
                value["source_sequence"] <= normalized[-1]["tuple"]["source_sequence"] or
                value["match_frame"] <= normalized[-1]["tuple"]["match_frame"] or
                value["browser_cursor"] <= normalized[-1]["tuple"]["browser_cursor"] or
                prefix["bytes_read"] <= normalized[-1]["prefix"]["bytes_read"] or
                records <= normalized[-1]["prefix"]["records_read"]):
            raise ComparisonError("ordered clock-lineage checkpoints are not strictly ascending")
        normalized.append(item)

    positive = source.get("first_positive_boundary")
    clock60 = source.get("clock60_boundary")
    target = source.get("match_clock_boundary")
    if (not isinstance(positive, dict) or not isinstance(clock60, dict) or
            not isinstance(target, dict)):
        raise ComparisonError("ordered clock-lineage lacks positive, clock-60, or terminal target")
    if (_first_difference(normalized[0]["tuple"], positive) or
            _first_difference(normalized[1]["tuple"], clock60) or
            _first_difference(normalized[-1]["tuple"],
                              {key: value for key, value in target.items()
                               if key != "target_match_frame_at_least"})):
        raise ComparisonError("ordered clock-lineage checkpoints disagree with frozen source targets")
    if (normalized[0]["tuple"]["match_frame"] != 1 or
            normalized[1]["tuple"]["match_frame"] != 60 or
            normalized[0]["audit"] != source.get("positive_boundary_audit") or
            normalized[1]["audit"] != source.get("clock60_boundary_audit") or
            normalized[-1]["audit"] != source.get("match_clock_boundary_audit")):
        raise ComparisonError("ordered clock-lineage prior checkpoint identities or clocks differ")
    threshold = _int(target.get("target_match_frame_at_least"),
                     "ordered clock-lineage target threshold",
                     normalized[-2]["tuple"]["match_frame"] + 1, MAX_UINT32)
    if (normalized[-1]["tuple"]["match_frame"] != threshold or
            normalized[-1]["tuple"]["source_sequence"] + 1 > V10_ORDERED_LINEAGE_RECORD_CAP or
            normalized[-1]["prefix"]["bytes_read"] > V10_ORDERED_LINEAGE_BYTE_CAP or
            normalized[-1]["tuple"]["browser_cursor"] > recipe.get("frame_count", 0)):
        raise ComparisonError("ordered clock-lineage terminal target exceeds its frozen bounds")
    for name, identity in (("runner packet", lineage.get("runner_packet")),):
        if (not isinstance(identity, dict) or set(identity) != {"path", "bytes", "sha256"} or
                not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None):
            raise ComparisonError(f"ordered clock-lineage {name} identity is malformed")
    supporting = lineage.get("supporting_expectations")
    intermediate_labels = {item["label"] for item in normalized[2:-1]}
    if (not isinstance(supporting, dict) or set(supporting) != intermediate_labels):
        raise ComparisonError("ordered clock-lineage supporting expectations are incomplete")
    for label, identity in supporting.items():
        if (not isinstance(identity, dict) or set(identity) != {"path", "bytes", "sha256"} or
                not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None):
            raise ComparisonError(f"ordered clock-lineage {label} expectations identity is malformed")
    return normalized


def _load_expectations(path: Path, selected: Mapping[str, Path], *,
                       scope: str = V10_FIRST_SETUP_TICK0_SCOPE
                       ) -> tuple[dict[str, Any], str]:
    packet, packet_sha, _ = _read_json_sidecar(path, "v10 comparison expectations", max_bytes=1024 * 1024)
    expected_schema = (ORDERED_CLOCK_LINEAGE_EXPECTATION_SCHEMA
                       if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE else
                       POSITIVE_EXPECTATION_SCHEMA
                       if scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE else
                       CLOCK60_EXPECTATION_SCHEMA
                       if scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE else EXPECTATION_SCHEMA)
    if scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE:
        expected_schema = MATCH_CLOCK_EXPECTATION_SCHEMA
    if packet.get("schema") != expected_schema or packet.get("version") != 1:
        raise ComparisonError("v10 comparison expectations schema/version is unsupported")
    if packet.get("scope") != scope or not _is_v10_prefix_scope(scope):
        raise ComparisonError("v10 comparison expectations scope is unsupported")
    source = packet.get("source")
    recipe = packet.get("recipe")
    browser = packet.get("browser")
    if not all(isinstance(section, dict) for section in (source, recipe, browser)):
        raise ComparisonError("v10 comparison expectations require source, recipe, and browser identities")
    named_paths = {
        "reference": source.get("trace"),
        "source_manifest": source.get("manifest"),
        "source_report": source.get("report"),
        "source_audit": source.get("audit"),
        "recipe": recipe,
        "browser_capture_report": browser.get("capture_report"),
        "browser_producer_manifest": browser.get("producer_manifest"),
        "browser_report": browser.get("report"),
        "port_trace": browser.get("trace"),
    }
    if scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE:
        named_paths["positive_boundary_audit"] = source.get("positive_boundary_audit")
    elif scope in {V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                   V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                   V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
        named_paths["positive_boundary_audit"] = source.get("positive_boundary_audit")
        named_paths["clock60_boundary_audit"] = source.get("clock60_boundary_audit")
    if scope in {V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                 V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
        named_paths["match_clock_boundary_audit"] = source.get("match_clock_boundary_audit")
    for name, expected_path in selected.items():
        identity = named_paths.get(name)
        if not isinstance(identity, dict):
            raise ComparisonError(f"v10 comparison expectations lack {name} identity")
        _path_identity(identity.get("path"), expected_path, f"expectations {name}")
        digest = (identity.get("recorded_full_sha256") if name == "reference"
                  else identity.get("sha256"))
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ComparisonError(f"v10 comparison expectations {name} lacks a frozen SHA-256")
        if name != "reference" and (
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0):
            raise ComparisonError(f"v10 comparison expectations {name} lacks a frozen byte size")
    source_trace = source.get("trace")
    if (not isinstance(source_trace, dict) or
            type(source_trace.get("bytes")) is not int or source_trace["bytes"] <= 0 or
            not isinstance(source_trace.get("recorded_full_sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", source_trace["recorded_full_sha256"]) is None):
        raise ComparisonError("v10 expectations lack the recorded full MWRO hash/size")
    for field in ("capture_id", "sequence_id"):
        value = source.get(field)
        if not isinstance(value, str) or re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value) is None:
            raise ComparisonError(f"v10 expectations source.{field} is invalid")
    if (type(recipe.get("bytes")) is not int or recipe["bytes"] <= 0 or
            recipe.get("version") != MWRC_V10_VERSION or
            type(recipe.get("frame_count")) is not int or recipe["frame_count"] <= 0 or
            type(recipe.get("seed")) is not int or not 0 <= recipe["seed"] <= MAX_UINT32):
        raise ComparisonError("v10 expectations lack the recipe version, size, seed, or frame count")
    if scope in {V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
                 V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                 V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                 V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
        target = source.get("first_positive_boundary")
        required_target_fields = {
            "match_index", "source_tick", "source_sequence", "pad_consume_sequence",
            "timeline_frame_index", "browser_cursor", "match_frame",
        }
        if not isinstance(target, dict) or set(target) != required_target_fields:
            raise ComparisonError("positive-boundary expectations lack the exact frozen target fields")
        for field in required_target_fields:
            _int(target[field], f"positive-boundary expectations {field}",
                 1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
                 (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
                 else MAX_UINT32)
        if (target["match_index"] != 0 or target["source_tick"] == 0 or
                target["match_frame"] != 1 or
                target["timeline_frame_index"] + 1 != target["browser_cursor"] or
                target["source_sequence"] <= target["pad_consume_sequence"] or
                target["source_sequence"] >= V10_FIRST_POSITIVE_RECORD_CAP or
                target["browser_cursor"] > recipe["frame_count"]):
            raise ComparisonError("positive-boundary expectations contain an invalid first-positive target")
    if scope in {V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                 V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                 V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
        target = source.get("clock60_boundary")
        required_target_fields = {
            "match_index", "source_tick", "source_sequence", "pad_consume_sequence",
            "timeline_frame_index", "browser_cursor", "match_frame",
        }
        if not isinstance(target, dict) or set(target) != required_target_fields:
            raise ComparisonError("clock-60 expectations lack the exact frozen target fields")
        for field in required_target_fields:
            _int(target[field], f"clock-60 expectations {field}",
                 1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
                 (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
                 else MAX_UINT32)
        first_positive = source["first_positive_boundary"]
        if (target["match_index"] != 0 or target["match_frame"] != 60 or
                target["source_tick"] <= first_positive["source_tick"] or
                target["timeline_frame_index"] + 1 != target["browser_cursor"] or
                target["source_sequence"] <= target["pad_consume_sequence"] or
                target["source_sequence"] >= V10_MATCH_CLOCK_RECORD_CAP or
                target["browser_cursor"] > recipe["frame_count"]):
            raise ComparisonError("clock-60 expectations contain an invalid first-match target")
    if scope in {V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                 V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
        target = source.get("match_clock_boundary")
        required_target_fields = {
            "target_match_frame_at_least", "match_index", "source_tick",
            "source_sequence", "pad_consume_sequence", "timeline_frame_index",
            "browser_cursor", "match_frame",
        }
        if not isinstance(target, dict) or set(target) != required_target_fields:
            raise ComparisonError("match-clock expectations lack the exact frozen target fields")
        minimum_threshold = 61
        threshold = _int(target["target_match_frame_at_least"],
                         "match-clock target threshold", minimum_threshold, MAX_UINT32)
        for field in required_target_fields - {"target_match_frame_at_least"}:
            _int(target[field], f"match-clock expectations {field}",
                 1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
                 (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
                 else MAX_UINT32)
        clock60 = source["clock60_boundary"]
        record_cap = (V10_ORDERED_LINEAGE_RECORD_CAP
                      if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE
                      else V10_MATCH_CLOCK_RECORD_CAP)
        if (target["match_index"] != 0 or target["match_frame"] != threshold or
                target["source_tick"] <= clock60["source_tick"] or
                target["match_frame"] <= clock60["match_frame"] or
                target["timeline_frame_index"] + 1 != target["browser_cursor"] or
                target["source_sequence"] <= target["pad_consume_sequence"] or
                target["source_sequence"] + 1 > record_cap or
                target["browser_cursor"] > recipe["frame_count"]):
            raise ComparisonError("match-clock expectations contain an invalid post-clock-60 target")
    if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE:
        source["ordered_clock_lineage"]["checkpoints"] = \
            _validate_ordered_clock_lineage_expectations(source, recipe)
    producer = browser.get("producer")
    if not isinstance(producer, dict) or any(
            not isinstance(producer.get(field), str) or not producer[field]
            for field in ("branch", "head", "tree", "base_main")):
        raise ComparisonError("v10 expectations lack the browser producer identity")
    for field in ("head", "tree", "base_main"):
        if re.fullmatch(r"[0-9a-f]{40}", producer[field]) is None:
            raise ComparisonError(f"v10 expectations browser producer {field} is not a commit identity")
    trace = browser.get("trace")
    if not isinstance(trace, dict) or type(trace.get("bytes")) is not int or trace["bytes"] <= 0:
        raise ComparisonError("v10 expectations lack the browser trace size")
    for name in ("disc", "runtime_data"):
        identity = browser.get(name)
        if (not isinstance(identity, dict) or
                not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None):
            raise ComparisonError(f"v10 expectations lack the recorded browser {name} identity")
        if name == "runtime_data" and identity.get("freshly_rehashed") is not False:
            raise ComparisonError("v10 expectations must label runtime data as recorded, not freshly rehashed")
    return packet, packet_sha


def _verify_expected_file(identity: Mapping[str, Any], path: Path, digest: str,
                          size: int, context: str) -> None:
    _path_identity(identity.get("path"), path, context)
    if identity.get("sha256") != digest:
        raise ComparisonError(f"{context} hash differs from frozen expectations")
    if identity.get("bytes") is not None and identity.get("bytes") != size:
        raise ComparisonError(f"{context} byte size differs from frozen expectations")


def _expectation_files(packet: Mapping[str, Any]) -> dict[str, Mapping[str, Any]]:
    source = packet["source"]
    recipe = packet["recipe"]
    browser = packet["browser"]
    files = {
        "reference": source["trace"],
        "source_manifest": source["manifest"],
        "source_report": source["report"],
        "source_audit": source["audit"],
        "recipe": recipe,
        "browser_capture_report": browser["capture_report"],
        "browser_producer_manifest": browser["producer_manifest"],
        "browser_report": browser["report"],
        "port_trace": browser["trace"],
    }
    if "positive_boundary_audit" in source:
        files["positive_boundary_audit"] = source["positive_boundary_audit"]
    return files


def _validate_source_manifest_capture(capture: Mapping[str, Any],
                                      expected_source: Mapping[str, Any]) -> None:
    expected_capture = {
        "capture_id": expected_source["capture_id"],
        "sequence_id": expected_source["sequence_id"],
        "match_count": 3,
        "source_revision": "GALE01r2",
        "observer_schema": EXPECTED_OBSERVER_SCHEMA,
        "observer_version": 1,
        "dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
        "dol_sha1": EXPECTED_DOL_SHA1,
        "dol_sha256": EXPECTED_DOL_SHA256,
        "cpu": "JITARM64",
    }
    for field, expected in expected_capture.items():
        if capture.get(field) != expected:
            raise ComparisonError(f"source candidate capture.{field} differs from the expected v10 identity")


def _source_manifest_profile(manifest: Mapping[str, Any], recipe: Recipe,
                             recipe_sha: str, recipe_path: Path,
                             reference_path: Path, expected_source: Mapping[str, Any]) -> None:
    if manifest.get("schema") != "melee-web-whole-session-replay-candidate" or \
            manifest.get("version") != 1:
        raise ComparisonError("source candidate manifest schema/version is unsupported")
    source_input = manifest.get("input")
    transport = manifest.get("transport")
    output = manifest.get("output")
    if not isinstance(source_input, dict) or not isinstance(transport, dict) or \
            not isinstance(output, dict):
        raise ComparisonError("source candidate manifest lacks input, transport, or output identity")
    capture = source_input.get("capture")
    if not isinstance(capture, dict):
        raise ComparisonError("source candidate manifest lacks capture identity")
    _validate_source_manifest_capture(capture, expected_source)
    _path_identity(capture.get("path"), reference_path, "source candidate capture")
    if capture.get("raw_sha256") != expected_source["trace"]["recorded_full_sha256"]:
        raise ComparisonError("source candidate full-trace hash disagrees with the frozen expectations")
    _path_identity(output.get("path"), recipe_path, "source candidate output")
    for field, expected in (("version", MWRC_V10_VERSION),
                            ("frame_count", recipe.frame_count),
                            ("seed", recipe.seed), ("output_sha256", recipe_sha)):
        if transport.get(field) != expected or output.get("sha256") != recipe_sha:
            raise ComparisonError(f"source candidate transport.{field} disagrees with MWRC")
    pad_bytes = b"".join(bytes.fromhex(pad)
                         for frame in recipe.frames for pad in frame["pads"])
    if (len(pad_bytes) != recipe.frame_count * FRAME_BYTES or
            transport.get("input_bytes_sha256") != hashlib.sha256(pad_bytes).hexdigest()):
        raise ComparisonError("source candidate input-byte identity disagrees with MWRC PAD frames")
    if source_input.get("spans") != recipe.spans:
        raise ComparisonError("source candidate spans disagree with the complete v10 recipe")
    setups = source_input.get("match_setups")
    if not isinstance(setups, list) or len(setups) != 3:
        raise ComparisonError("source candidate manifest must declare all three v10 setups")
    for index, (entry, raw, declared) in enumerate(zip(
            setups, recipe.match_setups, recipe.declared_match_setups)):
        if (not isinstance(entry, dict) or entry.get("match_index") != index or
                entry.get("start_melee_hex") != raw.hex() or
                entry.get("declared_setup") != declared):
            raise ComparisonError(f"source candidate manifest setup {index} disagrees with MWRC")
    if source_input.get("setup_hex") != recipe.setup.hex() or \
            source_input.get("declared_setup") != recipe.declared_match_setups[0]:
        raise ComparisonError("source candidate first setup aliases disagree with MWRC v10")
    expected_css = source_input.get("first_css")
    if not isinstance(expected_css, dict):
        raise ComparisonError("source candidate manifest lacks first-CSS binding")
    for key, expected in (("game_rules_hex", recipe.game_rules.hex()),
                          ("save_data_hex", recipe.save_data.hex()),
                          ("css_data_hex", recipe.css_data.hex()),
                          ("ko_counts_hex", recipe.ko_counts.hex()),
                          ("pad_state_hex", recipe.initial_pad.hex()),
                          ("rng", recipe.seed),
                          ("profile_masks", {"characters": recipe.characters,
                                             "stages": recipe.stages})):
        if expected_css.get(key) != expected:
            raise ComparisonError(f"source candidate first_css.{key} disagrees with MWRC")


def _validate_source_capture_report(source_report: Mapping[str, Any],
                                   expected_source: Mapping[str, Any],
                                   manifest_capture: Mapping[str, Any]) -> dict[str, Any]:
    if (source_report.get("schema") != V10_SOURCE_REPORT_SCHEMA or
            source_report.get("result") != "three_match_source_capture_complete" or
            source_report.get("input_mode") != V10_SOURCE_INPUT_MODE or
            source_report.get("lineup_profile") != V10_SOURCE_LINEUP_PROFILE or
            source_report.get("readiness_only") is not False or
            source_report.get("capture_id") != expected_source.get("capture_id") or
            source_report.get("raw_observer_sha256") !=
            expected_source.get("trace", {}).get("recorded_full_sha256")):
        raise ComparisonError("source capture report is not the expected ordinary v10 capture")
    handshake = source_report.get("observer_handshake")
    if not isinstance(handshake, dict):
        raise ComparisonError("source capture report lacks the passive observer handshake")
    identity = _source_prefix_identity(handshake, {
        "capture_id": handshake.get("capture_id"),
        "sequence_id": handshake.get("sequence_id"),
        "whole_session": handshake.get("whole_session"),
        "match_count": handshake.get("match_count"),
        "source_revision": manifest_capture.get("source_revision"),
    })
    if (identity["capture_id"] != expected_source.get("capture_id") or
            identity["sequence_id"] != expected_source.get("sequence_id")):
        raise ComparisonError("source report observer identity differs from frozen expectations")
    if source_report.get("observer_identity") != {
            "capture_id": identity["capture_id"], "sequence_id": identity["sequence_id"]}:
        raise ComparisonError("source report observer_identity disagrees with its handshake")
    return identity


def _validate_v10_source_provenance(reference_path: Path, recipe_path: Path,
                                    recipe: Recipe, recipe_sha: str,
                                    manifest_path: Path, source_report_path: Path,
                                    audit_path: Path, packet: Mapping[str, Any]
                                    ) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any],
                                               dict[str, Any]]:
    expected_files = _expectation_files(packet)
    expected_source = packet["source"]
    expected_recipe = packet["recipe"]
    manifest, manifest_sha, _ = _read_json_sidecar(manifest_path, "source candidate manifest")
    source_report, report_sha, _ = _read_json_sidecar(source_report_path, "source capture report")
    audit, audit_sha, _ = _read_json_sidecar(audit_path, "first-setup identity audit")
    _verify_expected_file(expected_files["source_manifest"], manifest_path,
                          manifest_sha, manifest_path.stat().st_size, "source candidate manifest")
    _verify_expected_file(expected_files["source_report"], source_report_path,
                          report_sha, source_report_path.stat().st_size, "source capture report")
    _verify_expected_file(expected_files["source_audit"], audit_path,
                          audit_sha, audit_path.stat().st_size, "first-setup identity audit")
    _verify_expected_file(expected_files["recipe"], recipe_path,
                          recipe_sha, recipe_path.stat().st_size, "MWRC recipe")
    if (recipe.version != expected_recipe["version"] or
            recipe.frame_count != expected_recipe["frame_count"] or
            recipe.seed != expected_recipe["seed"]):
        raise ComparisonError("MWRC version, frame count, or seed differs from frozen expectations")
    _source_manifest_profile(manifest, recipe, recipe_sha, recipe_path,
                             reference_path, expected_source)
    _path_identity(audit.get("manifest"), manifest_path, "identity audit manifest")
    _path_identity(audit.get("source_report"), source_report_path, "identity audit source report")
    _path_identity(audit.get("trace"), reference_path, "identity audit source trace")
    if (audit.get("manifest_sha256") != manifest_sha or
            audit.get("source_report_sha256") != report_sha or
            audit.get("recipe_sha256_verified") != recipe_sha):
        raise ComparisonError("identity audit sidecar hashes disagree with selected inputs")
    if (audit.get("trace_bytes") != expected_source["trace"]["bytes"] or
            audit.get("recorded_full_trace_sha256") != expected_source["trace"]["recorded_full_sha256"] or
            audit.get("full_trace_rehashed") is not False or
            audit.get("byte_cap") != V10_PREFIX_BYTE_CAP or
            audit.get("record_cap") != V10_PREFIX_RECORD_CAP or
            type(audit.get("records_decoded")) is not int or
            not 1 <= audit["records_decoded"] <= V10_PREFIX_RECORD_CAP or
            type(audit.get("bytes_read")) is not int or
            not 1 <= audit["bytes_read"] <= V10_PREFIX_BYTE_CAP or
            audit.get("first_setup_matches_manifest") is not True):
        raise ComparisonError("first-setup audit does not bind the required bounded source prefix")
    stat = reference_path.stat()
    if (stat.st_size != audit["trace_bytes"] or
            stat.st_size != expected_source["trace"]["bytes"]):
        raise ComparisonError("source trace size differs from its recorded, unrehashed full-trace identity")
    first_setup = audit.get("first_setup")
    first_tick = audit.get("first_source_tick")
    first_entry_seq = audit.get("first_entry_verified_seq")
    if (type(first_entry_seq) is not int or first_entry_seq < 0 or
            not isinstance(first_setup, dict) or first_setup.get("match_index") != 0 or
            type(first_setup.get("seq")) is not int or first_setup["seq"] <= first_entry_seq or
            type(first_setup.get("source_tick")) is not int or first_setup["source_tick"] < 0 or
            not isinstance(first_tick, dict) or first_tick.get("match_index") != 0 or
            type(first_tick.get("seq")) is not int or first_tick["seq"] <= first_setup["seq"] or
            first_tick.get("source_tick") != 0 or
            first_tick["seq"] != audit["records_decoded"] - 1):
        raise ComparisonError("first-setup audit records differ from the v10 tick-0 boundary")

    capture = manifest["input"]["capture"]
    identity = _validate_source_capture_report(source_report, expected_source, capture)
    for status_name in ("observer_status", "final_observer_status"):
        status = source_report.get(status_name)
        if (not isinstance(status, dict) or status.get("state") != "completed" or
                status.get("completed") is not True or status.get("invalid") is not False or
                status.get("error") is not None or type(status.get("event_count")) is not int or
                status["event_count"] <= 0 or
                status.get("last_seq") != status["event_count"] - 1):
            raise ComparisonError(f"source report {status_name} is not a completed observer stream")
    if (source_report["observer_status"]["event_count"] !=
            source_report["final_observer_status"]["event_count"] or
            source_report["observer_status"]["last_seq"] !=
            source_report["final_observer_status"]["last_seq"]):
        raise ComparisonError("source report observer status changed between capture and finalization")
    for status_name in ("input_status", "final_input_status"):
        status = source_report.get(status_name)
        if (not isinstance(status, dict) or status.get("complete") is not True or
                status.get("invalid") is not False or status.get("mode") != "record"):
            raise ComparisonError(f"source report {status_name} is not a complete recorded input stream")
    counts = source_report.get("observer_counts")
    final_status = source_report["final_observer_status"]
    if (not isinstance(counts, dict) or set(counts) != {"boundary", "end", "handshake", "start"} or
            any(type(value) is not int or value < 0 for value in counts.values()) or
            counts["handshake"] != 1 or counts["start"] != 1 or counts["end"] != 1 or
            sum(counts.values()) != final_status["event_count"]):
        raise ComparisonError("source report observer counts do not describe one complete stream")
    source_input_capture = manifest["input"]["capture"]
    if source_input_capture.get("raw_sha256") != source_report.get("raw_observer_sha256"):
        raise ComparisonError("source report and candidate disagree on the recorded full-trace hash")
    if not isinstance(source_report.get("setup_records"), list) or \
            len(source_report["setup_records"]) != 3:
        raise ComparisonError("source report does not contain all three setup identities")
    previous_sequence = -1
    for index, (record, raw, declared) in enumerate(zip(
            source_report["setup_records"], recipe.match_setups,
            recipe.declared_match_setups)):
        if (not isinstance(record, dict) or record.get("match_index") != index or
                type(record.get("source_sequence")) is not int or
                record["source_sequence"] <= previous_sequence or
                (index == 0 and record.get("source_sequence") != first_entry_seq) or
                record.get("raw_hex") != raw.hex() or record.get("decoded") != declared):
            raise ComparisonError(f"source report setup record {index} disagrees with v10 recipe")
        previous_sequence = record["source_sequence"]
    if source_report["setup_records"][0]["source_sequence"] != first_entry_seq:
        raise ComparisonError("source report first entry sequence disagrees with the bounded audit")
    return manifest, source_report, audit, {
        "manifest_sha256": manifest_sha,
        "source_report_sha256": report_sha,
        "audit_sha256": audit_sha,
        "recorded_full_trace_sha256": expected_source["trace"]["recorded_full_sha256"],
        "full_trace_rehashed": False,
        "trace_bytes": stat.st_size,
        "audit_records_decoded": audit["records_decoded"],
        "audit_bytes_read": audit["bytes_read"],
        "first_entry_seq": first_entry_seq,
        "first_setup_seq": first_setup["seq"],
        "first_setup_source_tick": first_setup["source_tick"],
        "first_source_tick_seq": first_tick["seq"],
    }


def _first_match_required_cursor(recipe: Recipe) -> int:
    return _first_match_timeline_index(recipe) + 1


def _first_match_timeline_index(recipe: Recipe) -> int:
    match_spans = [span for span in recipe.spans if span["scene"] == SCENES["match"]]
    if len(match_spans) != 3:
        raise ComparisonError("v10 recipe does not contain three ordered match spans")
    first_match = match_spans[0]
    if first_match["first_frame"] == 0:
        raise ComparisonError("v10 first match is not preceded by its CSS/SSS route")
    return first_match["first_frame"]


def _first_positive_match_join_complete(row: Mapping[str, Any], source: SourceCollector,
                                        comparator: Comparator,
                                        target: Mapping[str, int]) -> bool:
    return _match_boundary_join_complete(row, source, comparator, target)


def _match_boundary_join_complete(row: Mapping[str, Any], source: SourceCollector,
                                  comparator: Comparator,
                                  target: Mapping[str, int]) -> bool:
    payload = row.get("payload")
    return (row.get("event") == "boundary" and isinstance(payload, dict) and
            payload.get("boundary") == "source_tick" and
            payload.get("match_index") == target.get("match_index") == 0 and
            row.get("source_tick") == target.get("source_tick") and
            row.get("seq") == target.get("source_sequence") and
            source.match_index == 0 and source.pending is None and
            source.last_source_tick.get(0) == target.get("source_tick") and
            source.last_source_tick_seq == target.get("source_sequence") and
            source.prefix_binding_validated and
            comparator.setup_count == 1 and comparator.current_match == 0 and
            comparator.match_compared == target.get("source_tick", -1) + 1 and
            comparator.frame_index == target.get("browser_cursor"))


def _consume_ordered_clock_lineage(records: Iterable[Mapping[str, Any]],
                                   source: SourceCollector, comparator: Comparator,
                                   stats: ObserverStreamStats,
                                   checkpoints: Sequence[Mapping[str, Any]]
                                   ) -> tuple[bool, int | None]:
    """Consume through each ordered join, stopping at its first prefix mismatch."""
    checkpoint_index = 0
    last_sequence: int | None = None
    for row in records:
        last_sequence = row["seq"]
        source.consume(row)
        checkpoint = checkpoints[checkpoint_index]
        if _match_boundary_join_complete(row, source, comparator, checkpoint["tuple"]):
            prefix = checkpoint["prefix"]
            if (stats.records_read != prefix["records_read"] or
                    stats.bytes_read != prefix["bytes_read"] or
                    stats.prefix_sha256 != prefix["sha256"]):
                raise ComparisonError(
                    f"fresh source prefix differs from the accepted {checkpoint['label']} checkpoint")
            checkpoint_index += 1
            if checkpoint_index == len(checkpoints):
                return True, last_sequence
    return False, last_sequence


def _validate_first_positive_audit(path: Path, packet: Mapping[str, Any],
                                   recipe: Recipe) -> tuple[dict[str, Any], str]:
    expected = packet["source"]["positive_boundary_audit"]
    audit, digest, size = _read_json_sidecar(
        path, "first-positive source audit", max_bytes=8 * 1024 * 1024)
    _verify_expected_file(expected, path, digest, size, "first-positive source audit")
    if (audit.get("schema") != "melee-web-b4-first-positive-match-frame-source-audit-v1" or
            audit.get("audit_completed") is not True or
            audit.get("whole_session_equivalent") is not False):
        raise ComparisonError("first-positive source audit schema or bounded status is unsupported")
    source = audit.get("source")
    provenance = source.get("provenance") if isinstance(source, dict) else None
    expected_source = packet["source"]
    trace = expected_source["trace"]
    source_path = source.get("path") if isinstance(source, dict) else None
    if (not isinstance(source, dict) or not isinstance(provenance, dict) or
            not isinstance(source_path, str) or
            Path(source_path).resolve() != Path(trace["path"]).resolve() or
            source.get("capture_id") != expected_source["capture_id"] or
            source.get("sequence_id") != expected_source["sequence_id"] or
            source.get("recorded_full_trace_bytes") != trace["bytes"] or
            source.get("recorded_full_trace_sha256") != trace["recorded_full_sha256"] or
            source.get("full_trace_rehashed") is not False):
        raise ComparisonError("first-positive audit does not bind the frozen source trace identity")
    expected_provenance = {
        "manifest_sha256": expected_source["manifest"]["sha256"],
        "source_report_sha256": expected_source["report"]["sha256"],
        "audit_sha256": expected_source["audit"]["sha256"],
        "recorded_full_trace_sha256": trace["recorded_full_sha256"],
        "full_trace_rehashed": False,
        "trace_bytes": trace["bytes"],
    }
    for field, value in expected_provenance.items():
        if provenance.get(field) != value:
            raise ComparisonError(f"first-positive audit source provenance {field} differs from expectations")
    source_stat = source.get("stat_before")
    if (not isinstance(source_stat, dict) or
            set(source_stat) != {"device", "inode", "bytes", "mtime_ns"} or
            any(type(value) is not int for value in source_stat.values()) or
            source_stat.get("bytes") != trace["bytes"] or
            source_stat != source.get("stat_after") or
            source.get("stat_stable_during_audit") is not True):
        raise ComparisonError("first-positive audit source trace stat identity was not stable")

    target = expected_source.get("first_positive_boundary")
    observed = audit.get("observed")
    first_positive = observed.get("first_positive") if isinstance(observed, dict) else None
    prefix = observed.get("source_prefix") if isinstance(observed, dict) else None
    if not isinstance(target, dict) or not isinstance(observed, dict) or \
            not isinstance(first_positive, dict) or not isinstance(prefix, dict):
        raise ComparisonError("first-positive audit lacks its source target observation")
    first_match_index = _first_match_timeline_index(recipe)
    if (target["timeline_frame_index"] != first_match_index + target["source_tick"] or
            target["browser_cursor"] != target["timeline_frame_index"] + 1 or
            target["browser_cursor"] > recipe.frame_count):
        raise ComparisonError("frozen positive target does not align with the complete MWRC timeline")
    audit_target = {
        "match_index": first_positive.get("match_index"),
        "source_tick": first_positive.get("source_tick"),
        "source_sequence": prefix.get("last_source_sequence"),
        "pad_consume_sequence": first_positive.get("pad_consume_source_sequence"),
        "timeline_frame_index": first_positive.get("timeline_frame_index"),
        "browser_cursor": first_positive.get("cursor_after_frame"),
        "match_frame": first_positive.get("match_frame"),
    }
    difference = _first_difference(target, audit_target)
    if difference:
        field = difference[0]
        raise ComparisonError(f"first-positive source audit differs from frozen target at {field}")
    prefix_bytes = _int(prefix.get("bytes_read"), "first-positive audit prefix bytes",
                        1, V10_PREFIX_BYTE_CAP)
    prefix_records = _int(prefix.get("records_read"), "first-positive audit prefix records",
                          1, V10_FIRST_POSITIVE_RECORD_CAP)
    if (not isinstance(prefix.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", prefix["sha256"]) is None or
            _int(prefix.get("last_source_sequence"),
                 "first-positive audit last source sequence", 0, (1 << 64) - 1) !=
            target["source_sequence"] or
            prefix_records != target["source_sequence"] + 1 or
            _int(observed.get("match_ticks_observed"),
                 "first-positive audit match tick count", 1, MAX_UINT32) !=
            target["source_tick"] + 1 or
            _int(observed.get("timeline_frames_input_ordered_against_recipe"),
                 "first-positive audit timeline frame count", 1, MAX_UINT32) !=
            target["browser_cursor"] or
            _int(observed.get("css_sss_frames_input_ordered_against_recipe"),
                 "first-positive audit CSS/SSS frame count", 1, MAX_UINT32) !=
            first_match_index or
            _int(observed.get("minimum_browser_target_cursor"),
                 "first-positive audit minimum browser cursor", 1, MAX_UINT32) !=
            target["browser_cursor"]):
        raise ComparisonError("first-positive source audit prefix counts disagree with the frozen target")
    expected_runs = []
    if target["source_tick"] > 0:
        expected_runs.append({
            "first_source_tick": 0,
            "last_source_tick": target["source_tick"] - 1,
            "first_timeline_frame_index": first_match_index,
            "last_timeline_frame_index": target["timeline_frame_index"] - 1,
            "match_frame": 0,
        })
    expected_runs.append({
        "first_source_tick": target["source_tick"],
        "last_source_tick": target["source_tick"],
        "first_timeline_frame_index": target["timeline_frame_index"],
        "last_timeline_frame_index": target["timeline_frame_index"],
        "match_frame": 1,
    })
    if _first_difference(expected_runs, observed.get("match_frame_runs")):
        raise ComparisonError("first-positive source audit clock runs differ from the frozen boundary")
    return audit, digest


def _validate_clock60_audit(path: Path, packet: Mapping[str, Any], recipe: Recipe,
                            positive_audit_sha256: str
                            ) -> tuple[dict[str, Any], str]:
    """Validate the reviewed source-only clock-60 boundary audit sidecar."""
    expected = packet["source"]["clock60_boundary_audit"]
    audit, digest, size = _read_json_sidecar(
        path, "clock-60 source audit", max_bytes=8 * 1024 * 1024)
    _verify_expected_file(expected, path, digest, size, "clock-60 source audit")
    if (audit.get("schema") != "melee-web-b4-first-match-clock-ge60-source-audit-v2" or
            audit.get("audit_completed") is not True or
            audit.get("whole_session_equivalent") is not False or
            audit.get("status") != "first_match_clock_ge60_found" or
            audit.get("target_match_frame_at_least") != 60):
        raise ComparisonError("clock-60 source audit schema or bounded status is unsupported")

    source = audit.get("source")
    provenance = source.get("provenance") if isinstance(source, dict) else None
    expected_source = packet["source"]
    trace = expected_source["trace"]
    if (not isinstance(source, dict) or not isinstance(provenance, dict) or
            not isinstance(source.get("path"), str) or
            Path(source["path"]).resolve() != Path(trace["path"]).resolve() or
            source.get("capture_id") != expected_source["capture_id"] or
            source.get("sequence_id") != expected_source["sequence_id"] or
            source.get("recorded_full_trace_bytes") != trace["bytes"] or
            source.get("recorded_full_trace_sha256") != trace["recorded_full_sha256"] or
            source.get("full_trace_rehashed") is not False or
            source.get("prior_first_positive_audit_sha256") != positive_audit_sha256):
        raise ComparisonError("clock-60 audit does not bind the frozen source and prior audit identities")
    expected_positive = expected_source["first_positive_boundary"]
    if _first_difference(expected_positive, source.get("prior_first_positive_boundary")):
        raise ComparisonError("clock-60 audit prior first-positive target differs from expectations")

    expected_provenance = {
        "manifest_sha256": expected_source["manifest"]["sha256"],
        "source_report_sha256": expected_source["report"]["sha256"],
        "audit_sha256": expected_source["audit"]["sha256"],
        "recorded_full_trace_sha256": trace["recorded_full_sha256"],
        "full_trace_rehashed": False,
        "trace_bytes": trace["bytes"],
    }
    for field, value in expected_provenance.items():
        if provenance.get(field) != value:
            raise ComparisonError(f"clock-60 audit source provenance {field} differs from expectations")
    source_stat = source.get("stat_before")
    if (not isinstance(source_stat, dict) or
            set(source_stat) != {"device", "inode", "bytes", "mtime_ns"} or
            any(type(value) is not int for value in source_stat.values()) or
            source_stat.get("bytes") != trace["bytes"] or
            source_stat != source.get("stat_after") or
            source.get("stat_stable_during_audit") is not True):
        raise ComparisonError("clock-60 audit source trace stat identity was not stable")

    observed = audit.get("observed")
    terminal = observed.get("target_clock_at_least_60") if isinstance(observed, dict) else None
    prefix = observed.get("source_prefix") if isinstance(observed, dict) else None
    expected_terminal = expected_source.get("clock60_boundary")
    if not isinstance(observed, dict) or not isinstance(terminal, dict) or \
            not isinstance(prefix, dict) or not isinstance(expected_terminal, dict):
        raise ComparisonError("clock-60 audit lacks its terminal source observation")
    audit_terminal = {
        "match_index": terminal.get("match_index"),
        "source_tick": terminal.get("source_tick"),
        "source_sequence": terminal.get("source_tick_seq"),
        "pad_consume_sequence": terminal.get("pad_consume_source_sequence"),
        "timeline_frame_index": terminal.get("timeline_frame_index"),
        "browser_cursor": terminal.get("cursor_after_frame"),
        "match_frame": terminal.get("match_frame"),
    }
    difference = _first_difference(expected_terminal, audit_terminal)
    if difference:
        raise ComparisonError(f"clock-60 source audit differs from frozen target at {difference[0]}")

    first_match_index = _first_match_timeline_index(recipe)
    if (expected_terminal["timeline_frame_index"] !=
            first_match_index + expected_terminal["source_tick"] or
            expected_terminal["browser_cursor"] !=
            expected_terminal["timeline_frame_index"] + 1 or
            expected_terminal["browser_cursor"] > recipe.frame_count):
        raise ComparisonError("frozen clock-60 target does not align with the complete MWRC timeline")
    prefix_bytes = _int(prefix.get("bytes_read"), "clock-60 audit prefix bytes",
                        1, V10_PREFIX_BYTE_CAP)
    prefix_records = _int(prefix.get("records_read"), "clock-60 audit prefix records",
                          1, V10_MATCH_CLOCK_RECORD_CAP)
    if (not isinstance(prefix.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", prefix["sha256"]) is None or
            _int(prefix.get("last_source_sequence"),
                 "clock-60 audit last source sequence", 0, (1 << 64) - 1) !=
            expected_terminal["source_sequence"] or
            prefix_records != expected_terminal["source_sequence"] + 1 or
            _int(observed.get("match_ticks_observed"),
                 "clock-60 audit match tick count", 1, MAX_UINT32) !=
            expected_terminal["source_tick"] + 1 or
            _int(observed.get("timeline_frames_input_ordered_against_recipe"),
                 "clock-60 audit timeline frame count", 1, MAX_UINT32) !=
            expected_terminal["browser_cursor"] or
            _int(observed.get("css_sss_frames_input_ordered_against_recipe"),
                 "clock-60 audit CSS/SSS frame count", 1, MAX_UINT32) !=
            first_match_index or
            _int(observed.get("candidate_browser_cursor"),
                 "clock-60 audit candidate browser cursor", 1, MAX_UINT32) !=
            expected_terminal["browser_cursor"] or
            observed.get("accepted_first_positive_rejoined") is not True):
        raise ComparisonError("clock-60 audit prefix counts disagree with the frozen target")

    if expected_terminal["match_frame"] != 60:
        raise ComparisonError("clock-60 source audit did not select the first exact clock-60 boundary")
    return audit, digest


def _validate_match_clock_boundary_audit(path: Path, packet: Mapping[str, Any],
                                         recipe: Recipe, *, ordered_lineage: bool = False,
                                         positive_audit: Mapping[str, Any] | None = None,
                                         clock60_audit: Mapping[str, Any] | None = None
                                         ) -> tuple[dict[str, Any], str]:
    """Validate the packet-bound terminal audit produced by the bounded clock runner."""
    if ordered_lineage and (not isinstance(positive_audit, Mapping) or
                            not isinstance(clock60_audit, Mapping)):
        raise ComparisonError("ordered clock validation lacks its validated clock-1/60 audits")
    expected_source = packet["source"]
    expected = expected_source["match_clock_boundary_audit"]
    audit, digest, size = _read_json_sidecar(
        path, "match-clock source audit", max_bytes=8 * 1024 * 1024)
    _verify_expected_file(expected, path, digest, size, "match-clock source audit")
    target = expected_source["match_clock_boundary"]
    threshold = target["target_match_frame_at_least"]
    suffix = str(threshold)
    _validate_match_clock_audit_status(audit, threshold, ordered_lineage=ordered_lineage)

    source = audit.get("source")
    provenance = source.get("provenance") if isinstance(source, dict) else None
    trace = expected_source["trace"]
    if (not isinstance(source, dict) or not isinstance(provenance, dict) or
            not isinstance(source.get("path"), str) or
            Path(source["path"]).resolve() != Path(trace["path"]).resolve() or
            source.get("capture_id") != expected_source["capture_id"] or
            source.get("sequence_id") != expected_source["sequence_id"] or
            source.get("recorded_full_trace_bytes") != trace["bytes"] or
            source.get("recorded_full_trace_sha256") != trace["recorded_full_sha256"] or
            source.get("full_trace_rehashed") is not False or
            source.get("content_opened") is not True):
        raise ComparisonError("match-clock audit does not bind the frozen source trace identity")
    expected_provenance = {
        "manifest_sha256": expected_source["manifest"]["sha256"],
        "source_report_sha256": expected_source["report"]["sha256"],
        "audit_sha256": expected_source["audit"]["sha256"],
        "recorded_full_trace_sha256": trace["recorded_full_sha256"],
        "full_trace_rehashed": False,
        "trace_bytes": trace["bytes"],
    }
    for field, value in expected_provenance.items():
        if provenance.get(field) != value:
            raise ComparisonError(f"match-clock audit source provenance {field} differs from expectations")
    source_stat = source.get("stat_before")
    if (not isinstance(source_stat, dict) or
            set(source_stat) != {"device", "inode", "bytes", "mtime_ns"} or
            any(type(value) is not int for value in source_stat.values()) or
            source_stat.get("bytes") != trace["bytes"] or
            source_stat != source.get("stat_after") or
            source.get("stat_stable_during_audit") is not True):
        raise ComparisonError("match-clock audit source trace stat identity was not stable")

    observed = audit.get("observed")
    prefix = observed.get("source_prefix") if isinstance(observed, dict) else None
    ordered_checkpoints = (expected_source["ordered_clock_lineage"]["checkpoints"]
                           if ordered_lineage else [])
    positive_key = (_ordered_audit_checkpoint_observation_key(ordered_checkpoints[0], threshold)
                    if ordered_lineage else "first_positive_observed")
    clock60_key = (_ordered_audit_checkpoint_observation_key(ordered_checkpoints[1], threshold)
                   if ordered_lineage else "first_clock60_observed")
    positive_observed = observed.get(positive_key) if isinstance(observed, dict) else None
    clock60_observed = observed.get(clock60_key) if isinstance(observed, dict) else None
    terminal = (observed.get(f"target_clock_ge{suffix}_observed")
                if isinstance(observed, dict) else None)
    if not all(isinstance(value, dict) for value in
               (observed, prefix, positive_observed, clock60_observed, terminal)):
        raise ComparisonError("match-clock source audit lacks its checkpoint or target observations")

    expected_positive = expected_source["first_positive_boundary"]
    expected_clock60 = expected_source["clock60_boundary"]
    differences = (
        (expected_positive, _lineage_boundary_from_observation(positive_observed, context="match-clock audit"),
         "first-positive"),
        (expected_clock60, _lineage_boundary_from_observation(clock60_observed, context="match-clock audit"),
         "clock-60"),
        ({key: value for key, value in target.items() if key != "target_match_frame_at_least"},
         _lineage_boundary_from_observation(terminal, context="match-clock audit"),
         "terminal match-clock"),
    )
    ordered_differences = list(differences)
    if ordered_lineage:
        for checkpoint in ordered_checkpoints[2:-1]:
            label = checkpoint["label"]
            observation = observed.get(
                _ordered_audit_checkpoint_observation_key(checkpoint, threshold))
            if not isinstance(observation, dict):
                raise ComparisonError(f"match-clock source audit lacks its {label} observation")
            ordered_differences.append((
                checkpoint["tuple"],
                _lineage_boundary_from_observation(observation, context="match-clock audit"), label))
    for expected_boundary, observed_boundary, name in ordered_differences:
        difference = _first_difference(expected_boundary, observed_boundary)
        if difference:
            raise ComparisonError(f"match-clock source audit {name} differs at {difference[0]}")
    observations = [
        (positive_observed, expected_positive, "first-positive"),
        (clock60_observed, expected_clock60, "clock-60"),
    ]
    if ordered_lineage:
        observations.extend((observed.get(
                             _ordered_audit_checkpoint_observation_key(checkpoint, threshold)),
                             checkpoint["tuple"], checkpoint["label"])
                            for checkpoint in ordered_checkpoints[2:-1])
    observations.append((terminal, target, "terminal"))
    for observation, expected_boundary, name in observations:
        if not isinstance(observation, dict):
            raise ComparisonError(f"match-clock source audit {name} observation is malformed")
        if (type(observation.get("scene_frame")) is not int or
                not 0 <= observation["scene_frame"] <= MAX_UINT32 or
                type(observation.get("rng")) is not int or
                not 0 <= observation["rng"] <= MAX_UINT32):
            raise ComparisonError(f"match-clock source audit {name} state values are malformed")
        if observation["scene_frame"] != expected_boundary["source_tick"]:
            raise ComparisonError(f"match-clock source audit {name} scene frame differs from its tick")
        _browser_entities(observation.get("fighter_entities"),
                          f"match-clock source audit {name}", 0)

    first_match_index = _first_match_timeline_index(recipe)
    if (target["timeline_frame_index"] != first_match_index + target["source_tick"] or
            target["browser_cursor"] != target["timeline_frame_index"] + 1 or
            target["browser_cursor"] > recipe.frame_count):
        raise ComparisonError("frozen match-clock target does not align with the complete MWRC timeline")
    prefix_byte_cap = (V10_ORDERED_LINEAGE_BYTE_CAP if ordered_lineage
                       else V10_PREFIX_BYTE_CAP)
    prefix_record_cap = (V10_ORDERED_LINEAGE_RECORD_CAP if ordered_lineage
                         else V10_MATCH_CLOCK_RECORD_CAP)
    prefix_bytes = _int(prefix.get("bytes_read"), "match-clock audit prefix bytes",
                        1, prefix_byte_cap)
    prefix_records = _int(prefix.get("records_read"), "match-clock audit prefix records",
                          1, prefix_record_cap)
    expected_checkpoints = {
        "first_positive_clock1": "pass", "first_clock60": "pass",
    }
    if ordered_lineage:
        expected_checkpoints = {
            _ordered_audit_checkpoint_status_key(checkpoint, threshold):
            ("observed_after_rejoins" if checkpoint["label"] == ORDERED_CLOCK_TARGET
             else "pass")
            for checkpoint in ordered_checkpoints
        }
    elif threshold > V10_MATCH_CLOCK_REJOIN_FRAME:
        expected_checkpoints.update({
            "first_match_clock300": "pass",
            f"target_clock{suffix}": "observed_after_rejoins",
        })
    if (not isinstance(prefix.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", prefix["sha256"]) is None or
            _int(prefix.get("last_source_sequence"),
                 "match-clock audit last source sequence", 0, (1 << 64) - 1) !=
            target["source_sequence"] or
            prefix_records != target["source_sequence"] + 1 or
            _int(observed.get("match_ticks_observed"),
                 "match-clock audit match tick count", 1, MAX_UINT32) !=
            target["source_tick"] + 1 or
            _int(observed.get("timeline_frames_input_ordered_against_recipe"),
                 "match-clock audit timeline frame count", 1, MAX_UINT32) !=
            target["browser_cursor"] or
            _int(observed.get("css_sss_frames_input_ordered_against_recipe"),
                 "match-clock audit CSS/SSS frame count", 1, MAX_UINT32) != first_match_index or
            (not ordered_lineage and
             (observed.get("first_positive_rejoined") is not True or
              observed.get("clock60_rejoined") is not True)) or
            _int(source.get("content_bytes_read"),
                 "match-clock audit source content bytes read", 1,
                 prefix_byte_cap) != prefix_bytes or
            source.get("stream_attempted") is not True or
            audit.get("checkpoints") != expected_checkpoints):
        raise ComparisonError("match-clock audit prefix counts or checkpoint rejoin evidence disagree")
    if ordered_lineage:
        rejoined = observed.get("checkpoint_rejoins")
        expected_rejoins = {item["label"]: True for item in ordered_checkpoints[:-1]}
        if not isinstance(rejoined, dict) or rejoined != expected_rejoins:
            raise ComparisonError("ordered match-clock audit lacks every prior checkpoint rejoin")
        positive_observed = positive_audit.get("observed", {})
        positive_boundary = positive_observed.get("first_positive")
        clock60_observed = clock60_audit.get("observed", {})
        clock60_boundary = clock60_observed.get("target_clock_at_least_60")
        if (not isinstance(positive_boundary, dict) or
                not isinstance(clock60_boundary, dict) or
                _lineage_boundary_from_observation({**positive_boundary,
                    "source_tick_seq": positive_observed.get("source_prefix", {}).get("last_source_sequence")})
                != ordered_checkpoints[0]["tuple"] or
                _lineage_prefix_from_observation(positive_observed) != ordered_checkpoints[0]["prefix"] or
                _lineage_boundary_from_observation(clock60_boundary) != ordered_checkpoints[1]["tuple"] or
                _lineage_prefix_from_observation(clock60_observed) != ordered_checkpoints[1]["prefix"] or
                _lineage_prefix_from_observation(observed) != ordered_checkpoints[-1]["prefix"]):
            raise ComparisonError("ordered match-clock audit prefixes differ from validated checkpoint audits")
    if ordered_lineage or threshold > V10_MATCH_CLOCK_REJOIN_FRAME:
        limits = audit.get("limits")
        if (not isinstance(limits, dict) or
                limits.get("max_bytes") != prefix_byte_cap or
                limits.get("max_records") != prefix_record_cap):
            raise ComparisonError("later match-clock audit caps differ from the frozen bounded reader")
        if ordered_lineage:
            _validate_ordered_clock_audit_lineage(audit, packet, recipe)
        else:
            _validate_clock300_checkpoint_lineage(audit, packet, recipe)
    return audit, digest


def _validate_match_clock_audit_status(audit: Mapping[str, Any], threshold: int, *,
                                       ordered_lineage: bool) -> None:
    """Accept the exact retained clock-1000 v1 header in ordered scope only."""
    legacy_clock1000 = (
        ordered_lineage and threshold == 1000 and
        audit.get("schema") == "melee-web-b4-source-clock1000-audit-v1")
    expected_schema = ("melee-web-b4-source-clock1000-audit-v1"
                       if legacy_clock1000 else
                       f"melee-web-b4-source-clock-ge{threshold}-audit-v3")
    report_write_failed = audit.get("report_write_failed")
    if (audit.get("schema") != expected_schema or
            audit.get("scope") != f"source-only-clock-ge{threshold}" or
            audit.get("audit_completed") is not True or
            audit.get("complete") is not False or
            audit.get("whole_session_equivalent") is not False or
            audit.get("status") != f"first_match_clock_ge{threshold}_found" or
            audit.get("target_match_frame_at_least") != threshold or
            audit.get("error") is not None):
        raise ComparisonError("match-clock source audit schema or bounded status is unsupported")
    if legacy_clock1000:
        if "report_write_failed" in audit and report_write_failed is not False:
            raise ComparisonError("legacy clock-1000 report-write status is contradictory")
    elif report_write_failed is not False:
        raise ComparisonError("match-clock source audit report-write status is unsupported")


def _validate_ordered_clock_runner_reference(
        audit_reference: Any, runner_identity: Mapping[str, Any], *,
        allow_legacy_pathless: bool) -> None:
    """Bind the report's embedded runner reference to its externally pinned file."""
    identity_fields = {"path", "bytes", "sha256"}
    if (not isinstance(runner_identity, Mapping) or
            set(runner_identity) != identity_fields or
            not isinstance(runner_identity.get("path"), str) or
            not runner_identity["path"] or
            type(runner_identity.get("bytes")) is not int or
            runner_identity["bytes"] <= 0 or
            not isinstance(runner_identity.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", runner_identity["sha256"]) is None):
        raise ComparisonError("ordered clock runner packet identity is malformed")
    if not isinstance(audit_reference, Mapping):
        raise ComparisonError("ordered clock audit runner reference is malformed")
    if (type(audit_reference.get("bytes")) is not int or
            audit_reference["bytes"] <= 0 or
            not isinstance(audit_reference.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", audit_reference["sha256"]) is None or
            ("path" in audit_reference and
             (not isinstance(audit_reference["path"], str) or
              not audit_reference["path"]))):
        raise ComparisonError("ordered clock audit runner reference is malformed")
    if set(audit_reference) == identity_fields:
        matches = dict(audit_reference) == dict(runner_identity)
    elif allow_legacy_pathless and set(audit_reference) == {"bytes", "sha256"}:
        matches = {
            "bytes": audit_reference.get("bytes"),
            "sha256": audit_reference.get("sha256"),
        } == {"bytes": runner_identity["bytes"],
              "sha256": runner_identity["sha256"]}
    else:
        matches = False
    if not matches:
        raise ComparisonError("ordered clock audit does not bind its frozen runner packet")


def _validate_ordered_clock_runner_prefix(packet_prefix: Any,
                                          expected_prefix: Mapping[str, Any],
                                          label: str) -> None:
    """Validate exact data fields with an optional nonempty v1 hash annotation."""
    if not isinstance(packet_prefix, Mapping):
        raise ComparisonError(f"ordered clock runner packet {label} prefix differs")
    fields = set(packet_prefix)
    if fields == ORDERED_CLOCK_PREFIX_FIELDS | {"hash_basis"}:
        if (not isinstance(packet_prefix.get("hash_basis"), str) or
                not packet_prefix["hash_basis"]):
            raise ComparisonError(
                f"ordered clock runner packet {label} hash-basis annotation is malformed")
    elif fields != ORDERED_CLOCK_PREFIX_FIELDS:
        raise ComparisonError(f"ordered clock runner packet {label} prefix differs")
    if (any(type(packet_prefix.get(name)) is not int or packet_prefix[name] <= 0
            for name in ("bytes_read", "records_read")) or
            type(packet_prefix.get("last_source_sequence")) is not int or
            packet_prefix["last_source_sequence"] < 0 or
            not isinstance(packet_prefix.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", packet_prefix["sha256"]) is None):
        raise ComparisonError(f"ordered clock runner packet {label} prefix differs")
    if {key: packet_prefix[key] for key in ORDERED_CLOCK_PREFIX_FIELDS} != dict(expected_prefix):
        raise ComparisonError(f"ordered clock runner packet {label} prefix differs")


def _lineage_boundary_from_observation(observation: Mapping[str, Any], *,
                                        context: str = "ordered clock-lineage observation"
                                        ) -> dict[str, int]:
    value = {
        "match_index": observation.get("match_index"),
        "source_tick": observation.get("source_tick"),
        "source_sequence": observation.get("source_tick_seq"),
        "pad_consume_sequence": observation.get("pad_consume_source_sequence"),
        "timeline_frame_index": observation.get("timeline_frame_index"),
        "browser_cursor": observation.get("cursor_after_frame"),
        "match_frame": observation.get("match_frame"),
    }
    for field, entry in value.items():
        _int(entry, f"{context} {field}",
             1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
             (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
             else MAX_UINT32)
    return value


def _lineage_prefix_from_observation(observation: Mapping[str, Any]) -> dict[str, Any]:
    prefix = observation.get("source_prefix")
    if not isinstance(prefix, dict):
        raise ComparisonError("ordered clock-lineage audit lacks a source-prefix identity")
    value = {field: prefix.get(field) for field in ORDERED_CLOCK_PREFIX_FIELDS}
    for field in ("bytes_read", "records_read", "last_source_sequence"):
        _int(value[field], f"ordered clock-lineage prefix {field}", 1,
             V10_ORDERED_LINEAGE_RECORD_CAP if field == "records_read" else
             V10_ORDERED_LINEAGE_BYTE_CAP if field == "bytes_read" else (1 << 64) - 1)
    if (not isinstance(value["sha256"], str) or
            re.fullmatch(r"[0-9a-f]{64}", value["sha256"]) is None):
        raise ComparisonError("ordered clock-lineage source-prefix hash is malformed")
    return value


def _load_prior_clock_expectations(identity: Mapping[str, Any],
                                   audit_identity: Mapping[str, Any],
                                   current_packet: Mapping[str, Any],
                                   recipe: Recipe, checkpoint: Mapping[str, Any]
                                   ) -> tuple[dict[str, Any], dict[str, Any]]:
    label = checkpoint["label"]
    path = Path(identity["path"])
    prior, digest, size = _read_json_sidecar(
        path, f"ordered {label} expectations", max_bytes=1024 * 1024)
    _verify_expected_file(identity, path, digest, size, f"ordered {label} expectations")
    if (prior.get("schema") != MATCH_CLOCK_EXPECTATION_SCHEMA or
            prior.get("scope") != V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE or
            prior.get("version") != 1):
        raise ComparisonError(f"ordered {label} expectations schema or scope is unsupported")
    prior_source = prior.get("source")
    current_source = current_packet["source"]
    for field in ("capture_id", "sequence_id", "trace", "manifest", "report", "audit",
                  "positive_boundary_audit", "first_positive_boundary",
                  "clock60_boundary_audit", "clock60_boundary"):
        if not isinstance(prior_source, dict) or prior_source.get(field) != current_source.get(field):
            raise ComparisonError(f"ordered {label} source expectations differ at {field}")
    if prior.get("recipe") != current_packet.get("recipe"):
        raise ComparisonError(f"ordered {label} expectations bind a different MWRC recipe")
    if prior_source.get("match_clock_boundary_audit") != audit_identity:
        raise ComparisonError(f"ordered {label} expectations bind a different audit")
    expected_target = prior_source.get("match_clock_boundary")
    checkpoint_tuple = checkpoint["tuple"]
    if (not isinstance(expected_target, dict) or
            expected_target.get("target_match_frame_at_least") != checkpoint_tuple["match_frame"] or
            _first_difference({key: value for key, value in expected_target.items()
                               if key != "target_match_frame_at_least"}, checkpoint_tuple)):
        raise ComparisonError(f"ordered {label} expectations differ from the frozen checkpoint")
    files = _expectation_files(prior)
    files["clock60_boundary_audit"] = prior_source.get("clock60_boundary_audit")
    files["match_clock_boundary_audit"] = prior_source.get("match_clock_boundary_audit")
    selected: dict[str, Path] = {}
    for name, expected_file in files.items():
        if (not isinstance(expected_file, dict) or
                not isinstance(expected_file.get("path"), str)):
            raise ComparisonError(f"ordered {label} expectations contain a malformed {name} identity")
        selected[name] = Path(expected_file["path"])
    loaded, _ = _load_expectations(path, selected,
                                   scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
    return loaded, prior_source


def _validate_ordered_clock_audit_lineage(audit: Mapping[str, Any],
                                          packet: Mapping[str, Any],
                                          recipe: Recipe) -> None:
    """Validate every checkpoint in the frozen ordered audit chain."""
    expected_source = packet["source"]
    lineage = expected_source["ordered_clock_lineage"]
    runner_identity = lineage["runner_packet"]
    threshold = expected_source["match_clock_boundary"]["target_match_frame_at_least"]
    legacy_clock1000 = (
        threshold == 1000 and
        audit.get("schema") == "melee-web-b4-source-clock1000-audit-v1")
    _validate_ordered_clock_runner_reference(
        audit.get("packet"), runner_identity, allow_legacy_pathless=legacy_clock1000)
    runner_path = Path(runner_identity["path"])
    runner, digest, size = _read_json_sidecar(
        runner_path, "ordered clock source runner packet", max_bytes=2 * 1024 * 1024)
    _verify_expected_file(runner_identity, runner_path, digest, size,
                          "ordered clock source runner packet")
    target = expected_source["match_clock_boundary"]
    if (runner.get("schema") != f"melee-web-b4-source-clock{threshold}-launch-v1" or
            runner.get("scope") != f"source-only-clock-ge{threshold}" or
            runner.get("version") != 1 or
            runner.get("target_match_frame_ge") != threshold):
        raise ComparisonError("ordered clock runner packet schema or target is unsupported")
    source_trace = runner.get("source_trace")
    if (not isinstance(source_trace, dict) or
            not isinstance(source_trace.get("path"), str) or
            type(source_trace.get("bytes")) is not int or
            source_trace.get("bytes") <= 0 or
            not isinstance(source_trace.get("recorded_full_sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", source_trace["recorded_full_sha256"]) is None or
            Path(source_trace["path"]).resolve() !=
            Path(expected_source["trace"]["path"]).resolve() or
            source_trace.get("bytes") != expected_source["trace"]["bytes"] or
            source_trace.get("recorded_full_sha256") !=
            expected_source["trace"]["recorded_full_sha256"]):
        raise ComparisonError("ordered clock runner packet binds a different original trace")
    limits = runner.get("caps")
    if limits != {"max_bytes": V10_ORDERED_LINEAGE_BYTE_CAP,
                  "max_records": V10_ORDERED_LINEAGE_RECORD_CAP}:
        raise ComparisonError("ordered clock runner packet caps differ from this bounded scope")
    target_packet = runner.get("target")
    if (not isinstance(target_packet, dict) or
            target_packet.get("derive_tuple_from_source") is not True or
            target_packet.get("match_index") != 0 or
            target_packet.get("predicate") != f"first observed match_frame >= {threshold}" or
            target_packet.get("tuple") is not None):
        raise ComparisonError("ordered clock runner packet does not derive its target from source")
    checkout = runner.get("source_checkout")
    if (not isinstance(checkout, dict) or checkout.get("clean") is not True or
            not isinstance(checkout.get("head"), str) or
            re.fullmatch(r"[0-9a-f]{40}", checkout["head"]) is None or
            not isinstance(checkout.get("tree"), str) or
            re.fullmatch(r"[0-9a-f]{40}", checkout["tree"]) is None):
        raise ComparisonError("ordered clock runner packet lacks its clean source identity")
    frozen_stat = source_trace.get("stat_at_packet_freeze")
    if (not isinstance(frozen_stat, dict) or
            set(frozen_stat) != {"bytes", "device", "inode", "mtime_ns"} or
            any(type(value) is not int for value in frozen_stat.values()) or
            frozen_stat["bytes"] != source_trace["bytes"]):
        raise ComparisonError("ordered clock runner packet source stat identity is malformed")

    checkpoints = lineage["checkpoints"]
    packet_checkpoints = runner.get("checkpoints")
    if (not isinstance(packet_checkpoints, list) or
            len(packet_checkpoints) != len(checkpoints) - 1):
        raise ComparisonError("ordered clock runner packet checkpoint list is malformed")
    for item, expected in zip(packet_checkpoints, checkpoints[:-1]):
        label = expected["label"]
        if (not isinstance(item, dict) or
                set(item) != {"label", "audit_observation_key", "tuple", "prefix"} or
                item.get("label") != label or
                item.get("audit_observation_key") !=
                _ordered_runner_checkpoint_observation_key(expected) or
                item.get("tuple") != expected["tuple"]):
            raise ComparisonError(f"ordered clock runner packet {label} checkpoint differs")
        packet_prefix = item.get("prefix")
        _validate_ordered_clock_runner_prefix(packet_prefix, expected["prefix"], label)

    small_inputs = runner.get("small_inputs")
    selected_paths = runner.get("selected_paths")
    base_input_names = {"clock60_boundary_audit", "positive_boundary_audit", "recipe",
                        "source_audit", "source_manifest", "source_report"}
    intermediate_labels = [item["label"] for item in checkpoints[2:-1]]
    expected_small_names = base_input_names | {
        f"{label}_{kind}" for label in intermediate_labels
        for kind in ("boundary_audit", "expectations")}
    if (not isinstance(small_inputs, dict) or set(small_inputs) != expected_small_names or
            not isinstance(selected_paths, dict)):
        raise ComparisonError("ordered clock runner packet input identity map is malformed")
    expected_selected_names = expected_small_names | {"out", "reference"}
    if set(selected_paths) != expected_selected_names:
        raise ComparisonError("ordered clock runner packet selected-path map is malformed")
    for name, identity in small_inputs.items():
        if (not isinstance(identity, dict) or set(identity) != {"bytes", "path", "sha256"} or
                not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None or
                selected_paths.get(name) != identity["path"]):
            raise ComparisonError(f"ordered clock runner packet {name} identity is malformed")
    if (selected_paths.get("reference") != expected_source["trace"]["path"] or
            selected_paths.get("out") != audit.get("report_path")):
        raise ComparisonError("ordered clock runner packet reference or report path differs")
    expected_small_identities = {
        "positive_boundary_audit": expected_source["positive_boundary_audit"],
        "clock60_boundary_audit": expected_source["clock60_boundary_audit"],
        "recipe": packet["recipe"],
        "source_audit": expected_source["audit"],
        "source_manifest": expected_source["manifest"],
        "source_report": expected_source["report"],
    }
    for checkpoint in checkpoints[2:-1]:
        label = checkpoint["label"]
        expected_small_identities[f"{label}_boundary_audit"] = checkpoint["audit"]
        expected_small_identities[f"{label}_expectations"] = \
            lineage["supporting_expectations"][label]
    for name, identity in expected_small_identities.items():
        actual = small_inputs[name]
        if (actual.get("path") != identity.get("path") or
                actual.get("bytes") != identity.get("bytes") or
                actual.get("sha256") != identity.get("sha256")):
            raise ComparisonError(f"ordered clock runner packet {name} identity differs")
    stat_at_freeze = source_trace.get("stat_at_packet_freeze")
    terminal_stat = audit.get("source", {}).get("stat_before")
    if (not isinstance(stat_at_freeze, dict) or stat_at_freeze != terminal_stat):
        raise ComparisonError("ordered clock runner packet and audit trace stat identities differ")
    source_code = runner.get("source_code")
    runner_source = runner.get("runner")
    if (not isinstance(source_code, dict) or not source_code or
            not isinstance(runner_source, dict) or
            not isinstance(runner_source.get("path"), str) or
            type(runner_source.get("bytes")) is not int or runner_source["bytes"] <= 0 or
            not isinstance(runner_source.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", runner_source["sha256"]) is None):
        raise ComparisonError("ordered clock runner packet lacks source/runner identities")
    for name, identity in source_code.items():
        if (not isinstance(name, str) or not isinstance(identity, dict) or
                set(identity) != {"bytes", "path", "sha256"} or
                not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None):
            raise ComparisonError("ordered clock runner packet source-code identity is malformed")

    for checkpoint in checkpoints[2:-1]:
        label = checkpoint["label"]
        expected_exp = lineage["supporting_expectations"][label]
        input_exp = small_inputs[f"{label}_expectations"]
        if expected_exp != input_exp:
            raise ComparisonError(f"ordered clock runner packet {label} expectations identity differs")
        audit_identity = small_inputs[f"{label}_boundary_audit"]
        if checkpoint["audit"] != audit_identity:
            raise ComparisonError(f"ordered clock runner packet {label} audit identity differs")
        nested_packet, nested_source = _load_prior_clock_expectations(
            expected_exp, audit_identity, packet, recipe, checkpoint)
        nested_audit, _ = _validate_match_clock_boundary_audit(
            Path(audit_identity["path"]), nested_packet, recipe)
        nested_observed = nested_audit["observed"].get(
            f"target_clock_ge{nested_source['match_clock_boundary']['target_match_frame_at_least']}_observed")
        if not isinstance(nested_observed, dict):
            raise ComparisonError(f"ordered {label} audit lacks its target observation")
        if (_lineage_boundary_from_observation(nested_observed) != checkpoint["tuple"] or
                _lineage_prefix_from_observation(nested_audit["observed"]) != checkpoint["prefix"]):
            raise ComparisonError(f"ordered {label} audit differs from its frozen checkpoint")

    expected_positive_audit = lineage["checkpoints"][0]["audit"]
    expected_clock60_audit = lineage["checkpoints"][1]["audit"]
    if (expected_positive_audit != expected_source.get("positive_boundary_audit") or
            expected_clock60_audit != expected_source.get("clock60_boundary_audit") or
            lineage["checkpoints"][-1]["audit"] != expected_source.get("match_clock_boundary_audit")):
        raise ComparisonError("ordered clock lineage audit identities differ from source expectations")


def _validate_clock300_checkpoint_lineage(audit: Mapping[str, Any],
                                          packet: Mapping[str, Any],
                                          recipe: Recipe) -> None:
    """Validate the standard clock-300 rejoin carried by later bounded audits."""
    expected_source = packet["source"]
    prior_boundary = expected_source.get("prior_match_clock_boundary")
    prior_audit_identity = expected_source.get("prior_match_clock_boundary_audit")
    prior_expectations_identity = expected_source.get("prior_match_clock_expectations")
    launch_packet_identity = expected_source.get("match_clock_boundary_source_packet")
    if not all(isinstance(value, dict) for value in (
            prior_boundary, prior_audit_identity,
            prior_expectations_identity, launch_packet_identity)):
        raise ComparisonError("later match-clock audit lacks its frozen clock-300 lineage identities")
    for name, identity in (("audit", prior_audit_identity),
                           ("expectations", prior_expectations_identity),
                           ("runner packet", launch_packet_identity)):
        if (not isinstance(identity.get("path"), str) or not identity["path"] or
                type(identity.get("bytes")) is not int or identity["bytes"] <= 0 or
                not isinstance(identity.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", identity["sha256"]) is None):
            raise ComparisonError(f"prior clock-300 {name} identity is malformed")
    prior_fields = {
        "target_match_frame_at_least", "match_index", "source_tick", "source_sequence",
        "pad_consume_sequence", "timeline_frame_index", "browser_cursor", "match_frame",
    }
    if set(prior_boundary) != prior_fields:
        raise ComparisonError("frozen prior clock-300 boundary fields are malformed")
    for field in prior_fields:
        _int(prior_boundary[field], f"prior clock-300 boundary {field}",
             1 if field in {"source_sequence", "browser_cursor", "match_frame",
                            "target_match_frame_at_least"} else 0,
             (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
             else MAX_UINT32)
    if (prior_boundary["target_match_frame_at_least"] != V10_MATCH_CLOCK_REJOIN_FRAME or
            prior_boundary["match_index"] != 0 or
            prior_boundary["match_frame"] != V10_MATCH_CLOCK_REJOIN_FRAME or
            prior_boundary["timeline_frame_index"] + 1 != prior_boundary["browser_cursor"] or
            prior_boundary["source_sequence"] <= prior_boundary["pad_consume_sequence"]):
        raise ComparisonError("frozen prior match-clock boundary is not the clock-300 checkpoint")

    prior_expectations_path = Path(prior_expectations_identity.get("path", ""))
    prior_expectations, prior_expectations_sha, prior_expectations_size = _read_json_sidecar(
        prior_expectations_path, "prior clock-300 expectations", max_bytes=1024 * 1024)
    _verify_expected_file(prior_expectations_identity, prior_expectations_path,
                          prior_expectations_sha, prior_expectations_size,
                          "prior clock-300 expectations")
    audit_expectations = audit.get("expectations")
    if audit_expectations != prior_expectations_identity:
        raise ComparisonError("later match-clock audit lacks its prior expectations identity")
    if (prior_expectations.get("schema") != MATCH_CLOCK_EXPECTATION_SCHEMA or
            prior_expectations.get("scope") != V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE or
            prior_expectations.get("version") != 1):
        raise ComparisonError("prior clock-300 expectations schema or scope is unsupported")
    if not all(isinstance(prior_expectations.get(section), dict)
               for section in ("source", "recipe", "browser")):
        raise ComparisonError("prior clock-300 expectations sections are malformed")
    prior_source = prior_expectations.get("source")
    for field in ("capture_id", "sequence_id", "trace", "manifest", "report", "audit",
                  "positive_boundary_audit", "first_positive_boundary",
                  "clock60_boundary_audit", "clock60_boundary"):
        if prior_source.get(field) != expected_source.get(field):
            raise ComparisonError(f"prior clock-300 source expectations differ at {field}")
    if prior_expectations.get("recipe") != packet.get("recipe"):
        raise ComparisonError("prior clock-300 expectations bind a different recipe")
    if (prior_source.get("match_clock_boundary") != prior_boundary or
            prior_source.get("match_clock_boundary_audit") != prior_audit_identity):
        raise ComparisonError("prior clock-300 expectations differ from the frozen boundary/audit")

    try:
        prior_files = _expectation_files(prior_expectations)
    except (KeyError, TypeError, AttributeError) as error:
        raise ComparisonError("prior clock-300 expectations file identities are malformed") from error
    prior_files.update({
        "clock60_boundary_audit": prior_source.get("clock60_boundary_audit"),
        "match_clock_boundary_audit": prior_source.get("match_clock_boundary_audit"),
    })
    if any(not isinstance(identity, dict) or not isinstance(identity.get("path"), str)
           for identity in prior_files.values()):
        raise ComparisonError("prior clock-300 expectations contain incomplete file identities")
    prior_selected = {name: Path(identity["path"])
                      for name, identity in prior_files.items()}
    prior_packet, _ = _load_expectations(
        prior_expectations_path, prior_selected,
        scope=V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE)
    prior_audit, _ = _validate_match_clock_boundary_audit(
        Path(prior_audit_identity.get("path", "")), prior_packet, recipe)
    prior_observed = prior_audit["observed"].get(
        f"target_clock_ge{V10_MATCH_CLOCK_REJOIN_FRAME}_observed")
    observed = audit.get("observed")
    if (not isinstance(observed, dict) or observed.get("clock300_rejoined") is not True or
            not isinstance(prior_observed, dict) or
            observed.get("prior_clock300_observed") != prior_observed):
        raise ComparisonError("later match-clock audit did not rejoin the exact clock-300 observation")

    audit_packet_identity = audit.get("packet")
    if audit_packet_identity != launch_packet_identity:
        raise ComparisonError("later match-clock audit lacks its frozen runner packet identity")
    launch_packet_path = Path(launch_packet_identity.get("path", ""))
    launch_packet, launch_packet_sha, launch_packet_size = _read_json_sidecar(
        launch_packet_path, "match-clock source audit packet", max_bytes=2 * 1024 * 1024)
    _verify_expected_file(launch_packet_identity, launch_packet_path,
                          launch_packet_sha, launch_packet_size,
                          "match-clock source audit packet")
    threshold = expected_source["match_clock_boundary"]["target_match_frame_at_least"]
    if (launch_packet.get("schema") != f"melee-web-b4-source-clock{threshold}-launch-v3" or
            launch_packet.get("scope") != f"source-only-clock-ge{threshold}" or
            launch_packet.get("version") != 3 or
            launch_packet.get("target_match_frame_ge") != threshold):
        raise ComparisonError("match-clock source audit runner packet schema/target is unsupported")
    launch_source = launch_packet.get("source")
    target_limits = launch_packet.get("target")
    if (not isinstance(launch_source, dict) or not isinstance(target_limits, dict) or
            target_limits.get("caps") != {
                "max_bytes": V10_PREFIX_BYTE_CAP,
                "max_records": V10_MATCH_CLOCK_RECORD_CAP,
            }):
        raise ComparisonError("match-clock source audit runner packet lacks source lineage")
    launch_trace = launch_source.get("trace")
    if not isinstance(launch_trace, dict):
        raise ComparisonError("match-clock runner packet trace identity is malformed")
    launch_trace_path = launch_trace.get("path")
    if (not isinstance(launch_trace_path, str) or
            Path(launch_trace_path).resolve() != Path(expected_source["trace"]["path"]).resolve()):
        raise ComparisonError("match-clock runner packet nested trace path differs from source identity")
    if (launch_source.get("capture_id") != expected_source["capture_id"] or
            launch_source.get("sequence_id") != expected_source["sequence_id"] or
            launch_source.get("path") != expected_source["trace"]["path"] or
            launch_trace.get("bytes") != expected_source["trace"]["bytes"] or
            launch_trace.get("recorded_full_sha256") !=
            expected_source["trace"]["recorded_full_sha256"] or
            launch_source.get("first_positive_boundary") != expected_source["first_positive_boundary"] or
            launch_source.get("clock60_boundary") != expected_source["clock60_boundary"] or
            launch_source.get("clock300_audit") != prior_audit_identity or
            launch_source.get("match_clock_boundary") != prior_boundary):
        raise ComparisonError("match-clock runner packet differs from its frozen source lineage")

    accepted = launch_source.get("accepted_checkpoint_rejoins")
    if not isinstance(accepted, list) or len(accepted) != 3:
        raise ComparisonError("match-clock runner packet lacks the exact clock-1/60/300 rejoin list")
    positive_audit, positive_sha = _validate_first_positive_audit(
        Path(expected_source["positive_boundary_audit"]["path"]), packet, recipe)
    clock60_audit, _ = _validate_clock60_audit(
        Path(expected_source["clock60_boundary_audit"]["path"]), packet, recipe, positive_sha)
    checkpoint_expectations = (
        (expected_source["first_positive_boundary"],
         positive_audit["observed"]["source_prefix"], "clock-1"),
        (expected_source["clock60_boundary"],
         clock60_audit["observed"]["source_prefix"], "clock-60"),
        ({key: value for key, value in prior_boundary.items()
          if key != "target_match_frame_at_least"},
         prior_audit["observed"]["source_prefix"], "clock-300"),
    )
    tuple_keys = {"browser_cursor", "match_frame", "match_index", "pad_consume_sequence",
                  "source_sequence", "source_tick", "timeline_frame_index"}
    for index, (expected_tuple, expected_prefix, name) in enumerate(checkpoint_expectations):
        entry = accepted[index]
        if not isinstance(entry, dict):
            raise ComparisonError(f"match-clock runner packet {name} checkpoint is malformed")
        actual_tuple = entry.get("tuple")
        if not isinstance(actual_tuple, dict) or set(actual_tuple) != tuple_keys:
            raise ComparisonError(f"match-clock runner packet {name} tuple fields are malformed")
        for field, value in actual_tuple.items():
            _int(value, f"match-clock runner packet {name} tuple {field}",
                 1 if field in {"source_sequence", "browser_cursor", "match_frame"} else 0,
                 (1 << 64) - 1 if field in {"source_sequence", "pad_consume_sequence"}
                 else MAX_UINT32)
        if _first_difference(expected_tuple, actual_tuple):
            raise ComparisonError(f"match-clock runner packet {name} tuple differs from its audit")
        actual_prefix = entry.get("fresh_prefix")
        if not isinstance(actual_prefix, dict) or set(actual_prefix) != {
                "bytes_read", "hash_basis", "last_source_sequence", "records_read", "sha256"}:
            raise ComparisonError(f"match-clock runner packet {name} prefix identity is malformed")
        if not isinstance(actual_prefix.get("hash_basis"), str) or not actual_prefix["hash_basis"]:
            raise ComparisonError(f"match-clock runner packet {name} prefix basis is missing")
        _int(actual_prefix.get("bytes_read"),
             f"match-clock runner packet {name} prefix bytes", 1, V10_PREFIX_BYTE_CAP)
        _int(actual_prefix.get("records_read"),
             f"match-clock runner packet {name} prefix records", 1, V10_MATCH_CLOCK_RECORD_CAP)
        last_sequence = _int(actual_prefix.get("last_source_sequence"),
                             f"match-clock runner packet {name} prefix last sequence",
                             0, (1 << 64) - 1)
        if (actual_prefix["records_read"] != actual_tuple["source_sequence"] + 1 or
                last_sequence != actual_tuple["source_sequence"] or
                not isinstance(actual_prefix.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", actual_prefix["sha256"]) is None):
            raise ComparisonError(f"match-clock runner packet {name} prefix sequence/hash is malformed")
        normalized_prefix = {key: actual_prefix[key] for key in (
            "bytes_read", "last_source_sequence", "records_read", "sha256")}
        expected_prefix_identity = {key: expected_prefix[key] for key in (
            "bytes_read", "last_source_sequence", "records_read", "sha256")}
        if _first_difference(expected_prefix_identity, normalized_prefix):
            raise ComparisonError(f"match-clock runner packet {name} prefix differs from its audit")


def _validate_browser_producer_source(producer: Mapping[str, Any],
                                      expected_browser: Mapping[str, Any], *,
                                      scope: str | None = None) -> Mapping[str, Any]:
    schema = producer.get("schema")
    if schema == V10_HISTORICAL_CLOCK300_PRODUCER_SCHEMA:
        if scope != V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE:
            raise ComparisonError("historical clock300 producer requires the post-clock60 boundary scope")
    elif schema != V10_BROWSER_PRODUCER_SCHEMA:
        raise ComparisonError("browser producer manifest schema is unsupported")
    source = producer.get("source")
    expected = expected_browser.get("producer")
    if not isinstance(source, dict) or not isinstance(expected, dict):
        raise ComparisonError("browser producer or frozen producer identity is malformed")
    for field in ("branch", "head", "tree", "base_main"):
        value = source.get(field)
        if not isinstance(value, str) or value != expected.get(field):
            raise ComparisonError(f"browser producer {field} differs from frozen expectations")
    if source.get("clean") is not True:
        raise ComparisonError("browser producer source was not clean at capture")
    return source


def _first_match_tick_join_complete(row: Mapping[str, Any], source: SourceCollector,
                                    comparator: Comparator) -> bool:
    payload = row.get("payload")
    return (row.get("event") == "boundary" and isinstance(payload, dict) and
            payload.get("boundary") == "source_tick" and
            payload.get("match_index") == 0 and row.get("source_tick") == 0 and
            source.match_index == 0 and source.pending is None and
            source.last_source_tick.get(0) == 0 and source.prefix_binding_validated and
            comparator.setup_count == 1 and comparator.current_match == 0 and
            comparator.match_compared == 1 and
            source.first_source_tick_seq == row.get("seq"))


def _file_stat_identity(path: Path) -> dict[str, int]:
    stat = path.stat()
    return {"device": stat.st_dev, "inode": stat.st_ino,
            "bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns}


def _require_stable_mwro_stat(before: Mapping[str, int], after: Mapping[str, int]) -> None:
    if before != after:
        raise ComparisonError("source MWRO stat identity changed during bounded prefix read")


def _validate_v10_browser_setup_row(browser: BrowserReader, recipe: Recipe,
                                    match_index: int) -> None:
    if browser.line >= V10_BROWSER_EXPORT_RECORD_CAP:
        raise ComparisonError("browser trace reached its bounded record count before match setup")
    row = browser.next()
    expected_keys = {"record", "rng", "match_frame", "pad_state_hex", "fighters",
                     "declared_setup", "fighter_entities"}
    BrowserReader._require(row, expected_keys,
                           f"browser match setup {match_index} line {browser.line}")
    if row.get("record") != "session_match_enter_complete":
        raise ComparisonError("browser match setup record is missing or reordered")
    _int(row["rng"], f"browser match setup {match_index}.rng")
    _int(row["match_frame"], f"browser match setup {match_index}.match_frame")
    _hex(row["pad_state_hex"], PAD_STATE_BYTES,
         f"browser match setup {match_index}.pad_state_hex")
    _validate_v10_browser_fighters(row["fighters"],
                                   f"browser match setup {match_index}")
    _validate_v10_browser_entities(row["fighter_entities"],
                                   f"browser match {match_index} setup", match_index)
    difference = _first_difference(recipe.declared_match_setups[match_index],
                                   row["declared_setup"])
    if difference:
        field, expected, actual = difference
        raise ComparisonError(
            f"browser match setup {match_index} declared_setup differs at {field}: "
            f"expected {expected!r}, got {actual!r}")


def _validate_v10_browser_fighters(fighters: Any, context: str) -> None:
    """Check the full v10 fighter shape without assigning state equivalence."""
    _validate_browser_fighters(fighters, context)
    if len(fighters) != 4:
        raise ComparisonError(f"{context}: v10 requires four primary fighters")
    for slot, fighter in enumerate(fighters):
        try:
            _validate_retail_fighter(fighter, slot, f"{context}.fighters[{slot}]")
        except RetailCaptureError as error:
            raise ComparisonError(str(error)) from error


def _validate_v10_browser_entities(value: Any, context: str,
                                   match_index: int) -> list[dict[str, int]]:
    if not isinstance(value, list) or len(value) != 4:
        raise ComparisonError(f"{context}: expected four ordered v10 fighter entities")
    for slot, item in enumerate(value):
        if not isinstance(item, dict):
            raise ComparisonError(f"{context}: malformed fighter entity at slot {slot}")
        for field in ("match_index", "slot", "entity_index"):
            _int(item.get(field), f"{context}.fighter_entities[{slot}].{field}")
    return _browser_entities(value, context, match_index)


def _validate_v10_browser_export(port_trace_path: Path, recipe: Recipe,
                                 exported_cursor: int, packet: Mapping[str, Any]) -> int:
    """Validate the bounded, possibly overshot browser export without source state."""
    exported_cursor = _int(exported_cursor, "browser exported cursor", 1,
                           min(recipe.frame_count, V10_BROWSER_EXPORT_RECORD_CAP - 1))
    browser = BrowserReader(port_trace_path)
    try:
        if (type(browser.header.get("frames_requested")) is not int or
                browser.header["frames_requested"] != recipe.frame_count):
            raise ComparisonError("browser trace header frames_requested disagrees with MWRC recipe")
        first_positive = packet["source"].get("first_positive_boundary")
        positive_boundary = (first_positive if recipe.scope in {
            V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE, V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
            V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
            V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE
        } else None)
        clock60_boundary = (packet["source"].get("clock60_boundary")
                            if recipe.scope in {V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                                                V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                                                V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}
                            else None)
        match_clock_boundary = (packet["source"].get("match_clock_boundary")
                                if recipe.scope in {V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                                                    V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}
                                else None)
        ordered_clock_checkpoints = (
            packet["source"]["ordered_clock_lineage"]["checkpoints"]
            if recipe.scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE else None)
        frame_validator = Comparator(recipe, browser, positive_boundary=positive_boundary,
                                     clock60_boundary=clock60_boundary,
                                     match_clock_boundary=match_clock_boundary,
                                     ordered_clock_checkpoints=ordered_clock_checkpoints)
        match_starts = {
            span["first_frame"]: match_index
            for match_index, span in enumerate(
                span for span in recipe.spans if span["scene"] == SCENES["match"])
        }
        if _first_match_timeline_index(recipe) >= exported_cursor:
            raise ComparisonError("browser export ends before its first match setup boundary")
        for index in range(exported_cursor):
            match_index = match_starts.get(index)
            if match_index is not None:
                _validate_v10_browser_setup_row(browser, recipe, match_index)
                frame_validator.current_match = match_index
            if browser.line >= V10_BROWSER_EXPORT_RECORD_CAP:
                raise ComparisonError("browser trace reached its bounded record count before EOF")
            row = frame_validator._expect_frame_row(recipe.frames[index]["scene"], index)
            if type(row["scene"]) is not int or type(row["index"]) is not int:
                raise ComparisonError(
                    f"browser frame {index}: scene and index must be exact integers")
            if recipe.frames[index]["scene"] == SCENES["match"]:
                _validate_v10_browser_fighters(
                    row["fighters"], f"browser frame {index}")
                _validate_v10_browser_entities(row["fighter_entities"],
                                               f"browser frame {index}",
                                               frame_validator.current_match)
        browser.expect_eof(max_records=V10_BROWSER_EXPORT_RECORD_CAP)
        if browser.line > V10_BROWSER_EXPORT_RECORD_CAP:
            raise ComparisonError("browser trace exceeds its bounded record count")
        return browser.line
    finally:
        browser.close()


def _validate_v10_runtime_data_aborts(wrapper: Mapping[str, Any]) -> None:
    """Mirror validateRuntimeDataAbort in scripts/whole_session_capture_result.mjs.

    This validates retained transport evidence, not the abort's causal origin.
    Runtime input identity has already been bound to the producer and packet;
    neither the runtime package nor the original trace is opened here.
    """
    rows = wrapper.get("verified_runtime_data_aborts")
    if not isinstance(rows, list) or len(rows) > 1:
        raise ComparisonError("browser capture runtime-data abort evidence is not empty or singleton")
    if not rows:
        return
    evidence = rows[0]
    fields = {"requestCount", "responseCount", "failureCount", "finishedCount",
              "url", "expectedUrl", "method", "resourceType", "errorText",
              "responseStatus", "contentLength", "loadedBytes", "totalBytes",
              "fileBytes", "expectedBytes", "expectedSha256", "actualSha256",
              "fromCache"}
    if not isinstance(evidence, dict) or set(evidence) != fields:
        raise ComparisonError("browser capture runtime-data abort evidence fields are malformed")
    runtime = wrapper.get("inputs", {}).get("runtime_data")
    actual = wrapper.get("runtime_data_load")
    navigation_url = wrapper.get("url")
    if (not isinstance(runtime, dict) or not isinstance(actual, dict) or
            not isinstance(navigation_url, str) or not navigation_url):
        raise ComparisonError("browser capture runtime-data abort lacks bound load or navigation identity")
    expected_url = urljoin(navigation_url, "gameplay_menu_browser.data")
    expected_bytes = runtime.get("bytes")
    expected_sha = runtime.get("sha256")
    integer_values = {"requestCount": 1, "responseCount": 1,
                      "failureCount": 1, "finishedCount": 0, "responseStatus": 200,
                      **{name: expected_bytes for name in
                         ("contentLength", "loadedBytes", "totalBytes", "fileBytes",
                          "expectedBytes")}}
    if (type(expected_bytes) is not int or not 0 < expected_bytes <= 2**53 - 1 or
            not isinstance(expected_sha, str) or
            re.fullmatch(r"[0-9a-f]{64}", expected_sha) is None or
            any(type(evidence[name]) is not int or evidence[name] != value
                for name, value in integer_values.items()) or
            evidence["url"] != expected_url or evidence["expectedUrl"] != expected_url or
            evidence["method"] != "GET" or evidence["resourceType"] != "fetch" or
            evidence["errorText"] != "net::ERR_ABORTED" or
            evidence["expectedSha256"] != expected_sha or
            evidence["actualSha256"] != expected_sha or evidence["fromCache"] is not False or
            any(type(actual.get(name)) is not int or actual[name] != expected_bytes
                for name in ("loaded_bytes", "total_bytes", "file_bytes")) or
            actual.get("sha256") != expected_sha or actual.get("from_cache") is not False):
        raise ComparisonError("browser capture runtime-data abort differs from verified complete-package evidence")


def _validate_v10_manual_unload(wrapper: Mapping[str, Any],
                                browser_report: Mapping[str, Any], *,
                                required_cursor: int, requested_cursor: int,
                                observed_cursor: int, recipe: Recipe,
                                download_identities: Mapping[str, Mapping[str, Any]]) -> float:
    expected_download_names = ["retail-port.jsonl", "retail-browser-report.json"]
    if (wrapper.get("mode") != "state" or wrapper.get("result") != "incomplete" or
            wrapper.get("resume_timing_pauses") is not False or
            wrapper.get("timing_pause_resumes") != [] or wrapper.get("probe") is not None):
        raise ComparisonError("browser capture is not an ordinary deliberate state-prefix stop")
    for name, value in wrapper.items():
        if name != "first_error" and name.endswith("_error") and value not in (None, "", [], {}):
            raise ComparisonError(f"browser capture recorded an unrelated {name}")
    if (wrapper.get("browser_errors") != [] or wrapper.get("unexpected_requests") != []):
        raise ComparisonError("browser capture has an unrelated browser or request failure")
    _validate_v10_runtime_data_aborts(wrapper)
    if (browser_report.get("instrumented_timing_resumes") != 0 or
            browser_report.get("errors") not in (None, [])):
        raise ComparisonError("retail browser report has unrelated instrumentation or errors")
    if browser_report.get("failures") != list(V10_MANUAL_UNLOAD_FAILURES):
        raise ComparisonError("retail browser report failures are not the documented manual-unload set")

    saved_downloads = wrapper.get("saved_downloads")
    if not isinstance(saved_downloads, list) or len(saved_downloads) != len(expected_download_names):
        raise ComparisonError("browser capture did not save exactly the bounded trace and report")
    observed_downloads: dict[str, dict[str, Any]] = {}
    for item in saved_downloads:
        if not isinstance(item, dict) or set(item) != {"name", "bytes", "sha256"}:
            raise ComparisonError("browser capture saved-download identity is malformed")
        name = item["name"]
        if not isinstance(name, str) or name in observed_downloads:
            raise ComparisonError("browser capture has a duplicate or unknown saved download")
        size = _int(item["bytes"], f"saved download {name} bytes")
        digest = item["sha256"]
        if not isinstance(digest, str) or re.fullmatch(r"[0-9a-f]{64}", digest) is None:
            raise ComparisonError(f"saved download {name} hash is malformed")
        observed_downloads[name] = {"bytes": size, "sha256": digest}
    if observed_downloads != {name: dict(download_identities[name])
                             for name in expected_download_names}:
        raise ComparisonError("saved browser downloads disagree with the validated trace/report files")

    snapshots = wrapper.get("snapshots")
    if not isinstance(snapshots, list):
        raise ComparisonError("browser capture report lacks replay snapshots")
    target_rows: list[dict[str, Any]] = []
    last_time = -math.inf
    cursor_available = False
    previous_cursor = 0
    target_time: float | None = None
    unload_time: float | None = None
    unload_started = False
    for snapshot_index, row in enumerate(snapshots):
        if not isinstance(row, dict):
            raise ComparisonError("browser capture snapshot list contains a malformed row")
        if "source_cursor" not in row or "runtime_error" not in row:
            raise ComparisonError("browser capture snapshot lacks its cursor or runtime-error field")
        at_ms = row.get("at_ms")
        if type(at_ms) not in (int, float) or not math.isfinite(at_ms):
            raise ComparisonError("browser capture snapshots lack finite ordered observation times")
        if at_ms < last_time:
            # phase() snapshots before page.goto and after it returns. The
            # initial document and the loaded document have separate origins
            # for snapshot()'s performance.now(); all later times stay ordered.
            phases = wrapper.get("phases")
            navigation_pair = snapshots[:2]
            if (snapshot_index != 1 or
                    navigation_pair[0].get("reason") != "phase-start:http-load" or
                    row.get("reason") != "phase-end:http-load" or
                    not isinstance(phases, list) or not phases or
                    not isinstance(phases[0], dict) or
                    phases[0].get("name") != "http-load" or
                    phases[0].get("result") != "pass" or
                    any(any(item.get(field, object()) is not None for field in
                            ("source_cursor", "phase", "running", "replay_report", "runtime_error")) or
                        item.get("replay_downloads") != [] for item in navigation_pair)):
                raise ComparisonError("browser capture snapshots lack finite ordered observation times")
        last_time = at_ms
        cursor = row["source_cursor"]
        if cursor is None:
            if cursor_available:
                raise ComparisonError("browser capture has a null cursor after replay observation began")
            if (row.get("phase", object()) is not None or
                    row.get("running", object()) is not None or
                    row.get("replay_report", object()) is not None or
                    row.get("replay_downloads") != [] or row["runtime_error"] is not None):
                raise ComparisonError("browser pre-replay snapshot has inconsistent unavailable fields")
            continue
        if row["runtime_error"] is not None:
            raise ComparisonError("browser capture recorded a runtime error during manual unload")
        cursor = _int(cursor, "browser snapshot source cursor", 0, recipe.frame_count)
        cursor_available = True
        running = row.get("running")
        phase = row.get("phase")
        if type(running) is not int or running not in (0, 1) or type(phase) is not int:
            raise ComparisonError("browser replay snapshot lacks integer phase/running state")
        downloads = row.get("replay_downloads")
        replay_report = row.get("replay_report")
        if downloads not in ([], expected_download_names):
            raise ComparisonError("browser snapshot contains unexpected replay downloads")
        if replay_report is not None and replay_report != browser_report:
            raise ComparisonError("browser snapshot carries an unrelated replay report")
        has_export = downloads == expected_download_names or replay_report is not None
        if has_export:
            if (cursor != 0 or running != 0 or phase != 0 or
                    downloads != expected_download_names or replay_report != browser_report):
                raise ComparisonError("browser export is not bound to the unloaded native snapshot")
            unload_started = True
            if unload_time is None:
                unload_time = float(at_ms)
        elif unload_started:
            raise ComparisonError("browser snapshot followed unload without the validated report")
        elif target_time is not None and at_ms > target_time:
            if cursor != 0 or running != 0 or phase != 0:
                raise ComparisonError("browser replay continued running after the observed stop")
            unload_started = True
        else:
            if cursor < previous_cursor or cursor > observed_cursor:
                raise ComparisonError("browser live snapshot cursor is nonmonotonic or exceeds stop observation")
            previous_cursor = cursor
            if cursor == observed_cursor and running == 1:
                target_rows.append(row)
                target_time = max(float(at_ms), target_time or -math.inf)
    if (requested_cursor < required_cursor or observed_cursor < requested_cursor or
            not target_rows or target_time is None or unload_time is None or
            unload_time <= target_time):
        raise ComparisonError("browser capture chronology does not reach the requested manual-unload stop")
    if target_rows[-1].get("runtime_error") is not None:
        raise ComparisonError("browser observed stop snapshot contains a runtime error")

    final_snapshot = wrapper.get("final_snapshot")
    def is_export_snapshot(row: Any) -> bool:
        return (isinstance(row, dict) and type(row.get("source_cursor")) is int and
                row.get("source_cursor") == 0 and
                type(row.get("phase")) is int and
                row.get("phase") == 0 and row.get("running") == 0 and
                type(row.get("running")) is int and
                row.get("runtime_error") is None and
                row.get("replay_downloads") == expected_download_names and
                row.get("replay_report") == browser_report)

    if not is_export_snapshot(final_snapshot):
        raise ComparisonError("browser final snapshot does not show the unloaded exported prefix")
    first_mismatch = wrapper.get("first_mismatch")
    if (not isinstance(first_mismatch, dict) or
            first_mismatch.get("phase") != "whole-session-replay" or
            first_mismatch.get("failure") != V10_MANUAL_UNLOAD_FAILURES[0] or
            not is_export_snapshot(first_mismatch.get("snapshot"))):
        raise ComparisonError("browser first mismatch is not bound to the unloaded manual-stop report")
    message = "Browser replay report failed: " + json.dumps(
        list(V10_MANUAL_UNLOAD_FAILURES), separators=(",", ":"))
    first_error = wrapper.get("first_error")
    if not isinstance(first_error, dict) or not isinstance(first_error.get("details"), dict):
        raise ComparisonError("browser capture lacks the expected post-unload replay failure")
    details = first_error["details"]
    first_error_time = details.get("at_ms")
    details_cursor = details.get("source_cursor")
    details_phase = details.get("phase")
    details_running = details.get("running")
    if (first_error.get("kind") != "whole-session-replay" or
            first_error.get("phase") != "whole-session-replay" or
            first_error.get("message") != message or
            type(first_error_time) not in (int, float) or
            not math.isfinite(first_error_time) or first_error_time <= unload_time or
            type(details_cursor) is not int or details_cursor != 0 or
            type(details_phase) is not int or details_phase != 0 or
            type(details_running) is not int or details_running != 0 or
            details.get("runtime_error") is not None or
            details.get("replay_downloads") != expected_download_names or
            details.get("replay_report") != browser_report):
        raise ComparisonError("browser capture first error is not the expected post-unload report failure")
    failure = wrapper.get("failure")
    if (not isinstance(failure, str) or not failure.splitlines() or
            failure.splitlines()[0] != f"Error: {message}"):
        raise ComparisonError("browser capture failure text does not bind to the manual-unload report")
    return target_time


def _validate_v10_browser_provenance(capture_report_path: Path,
                                     producer_manifest_path: Path,
                                     browser_report_path: Path,
                                     port_trace_path: Path,
                                     recipe_path: Path,
                                     recipe_sha: str,
                                     recipe: Recipe,
                                     packet: Mapping[str, Any], *,
                                     required_cursor: int | None = None
                                     ) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any],
                                                dict[str, Any]]:
    expected_files = _expectation_files(packet)
    expected_browser = packet["browser"]
    wrapper, capture_sha, _ = _read_json_sidecar(capture_report_path, "browser capture report")
    producer, producer_sha, _ = _read_json_sidecar(producer_manifest_path,
                                                   "browser producer manifest")
    browser_report, report_sha, _ = _read_json_sidecar(browser_report_path,
                                                       "retail browser report")
    _verify_expected_file(expected_files["browser_capture_report"], capture_report_path,
                          capture_sha, capture_report_path.stat().st_size,
                          "browser capture report")
    _verify_expected_file(expected_files["browser_producer_manifest"], producer_manifest_path,
                          producer_sha, producer_manifest_path.stat().st_size,
                          "browser producer manifest")
    _verify_expected_file(expected_files["browser_report"], browser_report_path,
                          report_sha, browser_report_path.stat().st_size,
                          "retail browser report")
    if (wrapper.get("schema") != V10_BROWSER_CAPTURE_SCHEMA or
            wrapper.get("mode") != "state" or wrapper.get("result") != "incomplete"):
        raise ComparisonError("browser capture report is not the bounded v10 state capture")
    inputs = wrapper.get("inputs")
    if not isinstance(inputs, dict):
        raise ComparisonError("browser capture report lacks bound input identities")
    for name, expected_path in (("recipe", recipe_path), ("manifest", producer_manifest_path)):
        value = inputs.get(name)
        if not isinstance(value, dict):
            raise ComparisonError(f"browser capture report lacks its {name} input")
        _path_identity(value.get("path"), expected_path, f"browser capture {name}")
        if value.get("bytes") != expected_path.stat().st_size:
            raise ComparisonError(f"browser capture {name} byte size disagrees with selected file")
        expected_sha = recipe_sha if name == "recipe" else producer_sha
        if value.get("sha256") != expected_sha:
            raise ComparisonError(f"browser capture {name} hash disagrees with selected file")
    runtime = inputs.get("runtime_data")
    producer_source = producer.get("source")
    build = producer.get("build")
    if not isinstance(producer_source, dict) or not isinstance(build, dict):
        raise ComparisonError("browser producer manifest lacks source or build identity")
    expected_producer = expected_browser.get("producer")
    if not isinstance(expected_producer, dict):
        raise ComparisonError("expectations lack browser producer identity")
    _validate_browser_producer_source(producer, expected_browser, scope=recipe.scope)

    disc = inputs.get("disc")
    expected_disc = expected_browser["disc"]
    if not isinstance(disc, dict):
        raise ComparisonError("browser capture lacks its disc identity")
    _path_identity(disc.get("path"), Path(expected_disc["path"]), "browser capture disc")
    if (disc.get("bytes") != expected_disc["bytes"] or
            disc.get("sha256") != expected_disc["sha256"]):
        raise ComparisonError("browser capture disc differs from frozen expectations")

    if (not isinstance(build, dict) or build.get("configuration") != "Release" or
            build.get("target") != "runtime" or
            type(build.get("artifact_count")) is not int or
            not isinstance(build.get("artifacts"), dict) or
            build["artifact_count"] != len(build["artifacts"])):
        raise ComparisonError("browser producer manifest does not identify the reviewed Release runtime")
    for name, artifact in build["artifacts"].items():
        if (not isinstance(name, str) or not isinstance(artifact, dict) or
                type(artifact.get("bytes")) is not int or artifact["bytes"] < 0 or
                not isinstance(artifact.get("sha256"), str) or
                re.fullmatch(r"[0-9a-f]{64}", artifact["sha256"]) is None):
            raise ComparisonError("browser producer artifact inventory is malformed")
    gates = build.get("default_off_gates")
    if gates != V10_DEFAULT_OFF_GATES:
        raise ComparisonError("browser producer manifest has an enabled diagnostic or non-default gate")
    runtime_artifact = build["artifacts"].get("gameplay_menu_browser.data")
    if (not isinstance(runtime, dict) or not isinstance(runtime_artifact, dict) or
            runtime.get("bytes") != runtime_artifact["bytes"] or
            runtime.get("sha256") != runtime_artifact["sha256"] or
            Path(runtime.get("path", "")).resolve() !=
            (Path(build.get("directory", "")) / "gameplay_menu_browser.data").resolve()):
        raise ComparisonError("browser capture runtime data disagrees with its producer artifact inventory")
    expected_runtime = expected_browser["runtime_data"]
    _path_identity(runtime.get("path"), Path(expected_runtime["path"]),
                   "browser capture runtime data")
    if (runtime.get("bytes") != expected_runtime["bytes"] or
            runtime.get("sha256") != expected_runtime["sha256"]):
        raise ComparisonError("browser runtime data differs from frozen recorded expectations")
    if (type(runtime.get("bytes")) is not int or runtime["bytes"] < 0 or
            not isinstance(runtime.get("sha256"), str) or
            re.fullmatch(r"[0-9a-f]{64}", runtime["sha256"]) is None):
        raise ComparisonError("browser runtime artifact recorded identity is malformed")

    stop = wrapper.get("deliberate_prefix_stop")
    required_cursor = (_first_match_required_cursor(recipe) if required_cursor is None
                       else _int(required_cursor, "frozen browser boundary cursor", 1,
                                 recipe.frame_count))
    if not isinstance(stop, dict):
        raise ComparisonError("browser capture lacks structured deliberate-prefix-stop provenance")
    requested_cursor = _int(stop.get("requested_cursor"), "browser requested stop cursor", 1,
                             recipe.frame_count)
    observed_cursor = _int(stop.get("observed_cursor"), "browser observed stop cursor", 1,
                           recipe.frame_count)
    if requested_cursor < required_cursor or observed_cursor < requested_cursor:
        raise ComparisonError("browser requested/observed cursors do not reach the frozen stop boundary")

    if wrapper.get("browser_report") != browser_report:
        raise ComparisonError("embedded and sidecar retail browser reports disagree")
    if (browser_report.get("schema") != "melee-web-browser-retail-replay" or
            browser_report.get("version") != 1 or
            browser_report.get("recipe_sha256") != recipe_sha or
            browser_report.get("frames") != recipe.frame_count or
            browser_report.get("mode") != "state_capture" or
            browser_report.get("complete") is not False or
            browser_report.get("pass") is not False or
            browser_report.get("final_scene") is not None):
        raise ComparisonError("retail browser report is not the incomplete v10 state capture")
    metrics = browser_report.get("metrics")
    if not isinstance(metrics, dict):
        raise ComparisonError("retail browser report is missing exported source metrics")
    exported_values = [
        _int(metrics.get(name), f"retail browser report metrics.{name}", 1,
             recipe.frame_count)
        for name in ("sourceFrames", "sourceSteps", "sourceDraws")
    ]
    if len(set(exported_values)) != 1:
        raise ComparisonError("retail browser report source export metrics disagree")
    exported_cursor = exported_values[0]
    if not required_cursor <= requested_cursor <= observed_cursor <= exported_cursor:
        raise ComparisonError("browser requested, observed, and exported cursors are inconsistent")
    trace_size = port_trace_path.stat().st_size
    if trace_size > V10_PREFIX_BYTE_CAP:
        raise ComparisonError("browser trace exceeds its bounded diagnostic size")
    trace_stat_before = _file_stat_identity(port_trace_path)
    trace_sha = _sha256(port_trace_path)
    trace_bytes = port_trace_path.stat().st_size
    _verify_expected_file(expected_files["port_trace"], port_trace_path,
                          trace_sha, trace_bytes, "browser port trace")
    if (browser_report.get("trace_sha256") != trace_sha or
            inputs.get("recipe", {}).get("sha256") != browser_report.get("recipe_sha256")):
        raise ComparisonError("retail browser report hashes disagree with selected trace/recipe")
    target_time = _validate_v10_manual_unload(
        wrapper, browser_report, required_cursor=required_cursor,
        requested_cursor=requested_cursor, observed_cursor=observed_cursor,
        recipe=recipe,
        download_identities={
            "retail-port.jsonl": {"bytes": trace_bytes, "sha256": trace_sha},
            "retail-browser-report.json": {
                "bytes": browser_report_path.stat().st_size, "sha256": report_sha,
            },
        })
    browser_records = _validate_v10_browser_export(
        port_trace_path, recipe, exported_cursor, packet)
    trace_stat_after = _file_stat_identity(port_trace_path)
    if trace_stat_before != trace_stat_after:
        raise ComparisonError("browser trace stat identity changed during bounded export validation")
    return producer, wrapper, browser_report, {
        "capture_report_sha256": capture_sha,
        "producer_manifest_sha256": producer_sha,
        "producer_manifest_schema": producer["schema"],
        "browser_report_sha256": report_sha,
        "port_trace_sha256": trace_sha,
        "port_trace_bytes": trace_bytes,
        "port_trace_stat_before": trace_stat_before,
        "port_trace_stat_after": trace_stat_after,
        "browser_records_validated": browser_records,
        "target_cursor": observed_cursor,
        "observed_cursor": observed_cursor,
        "requested_cursor": requested_cursor,
        "exported_cursor": exported_cursor,
        "required_cursor": required_cursor,
        "target_snapshot_ms": target_time,
        "browser_errors_before_stop": 0,
        "runtime_data_recorded_identity": {
            "path": runtime["path"],
            "bytes": runtime["bytes"],
            "sha256": runtime["sha256"],
            "freshly_rehashed": False,
        },
    }


def _validate_browser_report(report: Mapping[str, Any], *, recipe_sha: str,
                             trace_sha: str, frame_count: int) -> None:
    """Bind the browser completion report to this exact recipe and trace."""
    if report.get("schema") != "melee-web-browser-retail-replay" or report.get("version") != 1:
        raise ComparisonError("browser report schema/version is unsupported")
    if report.get("recipe_sha256") != recipe_sha:
        raise ComparisonError("browser report recipe_sha256 disagrees with MWRC")
    if report.get("trace_sha256") != trace_sha:
        raise ComparisonError("browser report trace_sha256 disagrees with browser trace")
    if report.get("frames") != frame_count:
        raise ComparisonError("browser report frames disagrees with MWRC recipe")
    metrics = report.get("metrics")
    if not isinstance(metrics, dict):
        raise ComparisonError("browser report is missing metrics")
    for key in ("sourceFrames", "sourceSteps", "sourceDraws"):
        if metrics.get(key) != frame_count:
            raise ComparisonError(f"browser report metrics.{key} disagrees with MWRC recipe")


def _browser_completion_ok(report: Mapping[str, Any]) -> bool:
    return (report.get("final_scene") == SCENES["css"] and
            report.get("complete") is True and report.get("pass") is True and
            not report.get("failures") and not report.get("errors"))


def _compare_v10_bounded_prefix(reference_path: Path, recipe_path: Path,
                                port_path: Path, *, scope: str,
                                source_manifest_path: Path,
                                   source_report_path: Path, source_audit_path: Path,
                                   browser_capture_report_path: Path,
                                   browser_producer_manifest_path: Path,
                                   browser_report_path: Path,
                                   expectations_path: Path,
                                   positive_boundary_audit_path: Path | None = None,
                                   clock60_boundary_audit_path: Path | None = None,
                                   match_clock_boundary_audit_path: Path | None = None) -> dict[str, Any]:
    positive_scope = scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE
    clock60_scope = scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE
    match_clock_scope = scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE
    ordered_lineage_scope = scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE
    clock_lineage_scope = clock60_scope or match_clock_scope or ordered_lineage_scope
    if not _is_v10_prefix_scope(scope):
        raise ComparisonError(f"unsupported bounded v10 prefix scope {scope!r}")
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "scope": scope,
        "boundary_result": None,
        "result": "invalid",
        "complete": False,
        "whole_session_equivalent": False,
        "nonmatch_fields_compared": [],
        "checks": {
            "source_provenance": "not_checked",
            "browser_capture_provenance": "not_checked",
            "source_first_css_and_setup_binding": "not_checked",
            "source_pad_to_tick0_join": "not_checked",
            "setup_state": "not_checked",
            "tick0_state": "not_checked",
            "whole_session": "incomplete",
        },
        "limitations": {
            "boundary": ("Checks continuous CSS/SSS consumed-input order and compares first match setup plus every contiguous match tick through the externally bound ordered clock-lineage target, freshly rejoining each earlier packet checkpoint before continuing."
                         if ordered_lineage_scope else
                         "Checks CSS/SSS consumed-input order and compares first match setup plus every contiguous match tick through the externally bound match-clock boundary, rejoining the earlier clock-1 and clock-60 prefixes before continuing."
                         if match_clock_scope else
                         "Checks CSS/SSS consumed-input order and compares first match setup plus every contiguous match tick through the externally bound match_frame 60 boundary, rejoining the earlier clock-1 prefix before continuing."
                         if clock60_scope else
                         "Checks CSS/SSS consumed-input order and compares first match setup plus every contiguous match tick through the externally bound first positive match frame."
                         if positive_scope else
                         "Checks CSS/SSS consumed-input order and compares first match setup plus source tick 0 state only; no later match frame is read by the comparator."),
            "nonmatch_state": "CSS/SSS consumed-input ordering is not scalar state equivalence; nonmatch_fields_compared is empty.",
            "browser_export_tail": "All exported rows are checked for schema, typed shape, recipe input/order and exact EOF. Match rows beyond the semantic comparison boundary receive shape checks only; their state is not compared with the original.",
            "whole_session": "This is incomplete prefix evidence and never establishes whole-session equivalence.",
            "timing_rendering_audio": "No timing, draw cadence, pixels, PCM, performance, or tournament-admission claim.",
        },
    }
    if positive_scope:
        result["checks"]["source_ticks_through_first_positive"] = "not_checked"
    if clock60_scope:
        result["checks"]["source_ticks_through_clock60"] = "not_checked"
    if match_clock_scope:
        result["checks"]["source_ticks_through_match_clock_boundary"] = "not_checked"
    if ordered_lineage_scope:
        result["checks"]["source_ticks_through_ordered_clock_lineage"] = "not_checked"
    comparator: Comparator | None = None
    source: SourceCollector | None = None
    browser: BrowserReader | None = None
    records = None
    stats: ObserverStreamStats | None = None
    packet_sha: str | None = None
    source_identity: dict[str, Any] | None = None
    browser_identity: dict[str, Any] | None = None
    source_stat_before: dict[str, int] | None = None
    last_source_sequence: int | None = None
    try:
        needs_positive_audit = positive_scope or clock_lineage_scope
        if needs_positive_audit != (positive_boundary_audit_path is not None):
            raise ComparisonError(
                "bounded prefix scope and positive-boundary audit selection disagree")
        if clock_lineage_scope != (clock60_boundary_audit_path is not None):
            raise ComparisonError(
                "bounded prefix scope and clock-60 audit selection disagree")
        if (match_clock_scope or ordered_lineage_scope) != (
                match_clock_boundary_audit_path is not None):
            raise ComparisonError(
                "bounded prefix scope and match-clock audit selection disagree")
        selected = {
            "reference": reference_path,
            "source_manifest": source_manifest_path,
            "source_report": source_report_path,
            "source_audit": source_audit_path,
            "recipe": recipe_path,
            "browser_capture_report": browser_capture_report_path,
            "browser_producer_manifest": browser_producer_manifest_path,
            "browser_report": browser_report_path,
            "port_trace": port_path,
        }
        if needs_positive_audit:
            selected["positive_boundary_audit"] = positive_boundary_audit_path
        if clock_lineage_scope:
            selected["clock60_boundary_audit"] = clock60_boundary_audit_path
        if match_clock_scope or ordered_lineage_scope:
            selected["match_clock_boundary_audit"] = match_clock_boundary_audit_path
        packet, packet_sha = _load_expectations(expectations_path, selected, scope=scope)
        result["expectations"] = {"path": str(expectations_path), "sha256": packet_sha}
        result["selected_input_identities"] = {
            "source": packet["source"],
            "recipe": packet["recipe"],
            "browser": packet["browser"],
        }
        source_trace_expectation = packet["source"]["trace"]
        source_stat_before = _file_stat_identity(reference_path)
        result["source_full_trace"] = {
            "path": str(reference_path),
            "trace_bytes": source_stat_before["bytes"],
            "recorded_full_trace_sha256": source_trace_expectation["recorded_full_sha256"],
            "full_trace_rehashed": False,
            "stat_before": source_stat_before,
        }
        if source_stat_before["bytes"] != source_trace_expectation["bytes"]:
            raise ComparisonError("source trace size differs from its recorded full-trace identity")
        recipe_size = recipe_path.stat().st_size
        if recipe_size > 16 * 1024 * 1024:
            raise ComparisonError("MWRC recipe exceeds its bounded input size")
        recipe_raw = recipe_path.read_bytes()
        if len(recipe_raw) != recipe_size:
            raise ComparisonError("MWRC recipe changed size while being read")
        recipe_sha = hashlib.sha256(recipe_raw).hexdigest()
        _verify_expected_file(packet["recipe"], recipe_path,
                              recipe_sha, recipe_size, "MWRC recipe")
        recipe_obj = Recipe(recipe_path, recipe_raw, scope=scope)
        if (recipe_obj.version != packet["recipe"]["version"] or
                recipe_obj.frame_count != packet["recipe"]["frame_count"] or
                recipe_obj.seed != packet["recipe"]["seed"]):
            raise ComparisonError("v10 recipe fields differ from frozen expectations")

        manifest, source_report, audit, source_identity = _validate_v10_source_provenance(
            reference_path, recipe_path, recipe_obj, recipe_sha,
            source_manifest_path, source_report_path, source_audit_path, packet)
        result["source_full_trace"].update(source_identity)
        result["checks"]["source_provenance"] = "pass"
        positive_audit: dict[str, Any] | None = None
        positive_audit_sha: str | None = None
        positive_target: Mapping[str, int] | None = None
        clock60_target: Mapping[str, int] | None = None
        match_clock_target: Mapping[str, int] | None = None
        clock60_audit: dict[str, Any] | None = None
        clock60_audit_sha: str | None = None
        match_clock_audit: dict[str, Any] | None = None
        match_clock_audit_sha: str | None = None
        ordered_checkpoints: list[dict[str, Any]] | None = None
        target: Mapping[str, int] | None = None
        if needs_positive_audit:
            assert positive_boundary_audit_path is not None
            positive_audit, positive_audit_sha = _validate_first_positive_audit(
                positive_boundary_audit_path, packet, recipe_obj)
            positive_target = packet["source"]["first_positive_boundary"]
            result["positive_boundary_audit"] = {
                "path": str(positive_boundary_audit_path),
                "sha256": positive_audit_sha,
                "target": positive_target,
                "scope": "source-only audit target; browser comparison uses fresh packet identities",
            }
        if clock_lineage_scope:
            assert clock60_boundary_audit_path is not None and positive_audit_sha is not None
            clock60_audit, clock60_audit_sha = _validate_clock60_audit(
                clock60_boundary_audit_path, packet, recipe_obj, positive_audit_sha)
            clock60_target = packet["source"]["clock60_boundary"]
            result["clock60_boundary_audit"] = {
                "path": str(clock60_boundary_audit_path),
                "sha256": clock60_audit_sha,
                "target": clock60_target,
                "scope": "source-only audit target; browser comparison uses fresh packet identities",
            }
        if match_clock_scope or ordered_lineage_scope:
            assert (match_clock_boundary_audit_path is not None and
                    clock60_audit_sha is not None)
            match_clock_audit, match_clock_audit_sha = _validate_match_clock_boundary_audit(
                match_clock_boundary_audit_path, packet, recipe_obj,
                ordered_lineage=ordered_lineage_scope,
                positive_audit=positive_audit if ordered_lineage_scope else None,
                clock60_audit=clock60_audit if ordered_lineage_scope else None)
            match_clock_target = packet["source"]["match_clock_boundary"]
            result["match_clock_boundary_audit"] = {
                "path": str(match_clock_boundary_audit_path),
                "sha256": match_clock_audit_sha,
                "target": match_clock_target,
                "scope": "source-only audit target; browser comparison uses fresh packet identities",
            }
            if "report_write_failed" not in match_clock_audit:
                result["match_clock_boundary_audit"]["report_write_failed_observation"] = (
                    "not_recorded_in_historical_clock1000_v1")
        if ordered_lineage_scope:
            ordered_checkpoints = packet["source"]["ordered_clock_lineage"]["checkpoints"]
        if positive_scope:
            target = positive_target
        elif clock60_scope:
            target = clock60_target
        elif match_clock_scope or ordered_lineage_scope:
            target = match_clock_target
        producer, capture_report, browser_report, browser_identity = \
            _validate_v10_browser_provenance(
                browser_capture_report_path, browser_producer_manifest_path,
                browser_report_path, port_path, recipe_path, recipe_sha, recipe_obj, packet,
                required_cursor=(target["browser_cursor"] if target is not None else None))
        result["browser_capture"] = browser_identity
        result["checks"]["browser_capture_provenance"] = "pass"
        required_cursor = browser_identity["required_cursor"]
        browser = BrowserReader(port_path)
        comparator = Comparator(
            recipe_obj, browser,
            positive_boundary=(positive_target if needs_positive_audit else None),
            clock60_boundary=clock60_target,
            match_clock_boundary=match_clock_target,
            ordered_clock_checkpoints=ordered_checkpoints)
        source = SourceCollector(comparator, recipe_obj, manifest, audit, packet["source"])
        stats = ObserverStreamStats()
        records = iter_records(
            reference_path,
            max_bytes=(V10_ORDERED_LINEAGE_BYTE_CAP if ordered_lineage_scope
                       else V10_PREFIX_BYTE_CAP),
            max_records=(V10_ORDERED_LINEAGE_RECORD_CAP if ordered_lineage_scope else
                         V10_MATCH_CLOCK_RECORD_CAP if clock_lineage_scope else
                         V10_FIRST_POSITIVE_RECORD_CAP if positive_scope
                         else V10_PREFIX_RECORD_CAP), stats=stats)
        prefix_complete = False
        positive_prefix_rejoined = not clock_lineage_scope
        clock60_prefix_rejoined = not match_clock_scope
        last_source_sequence = None
        if ordered_lineage_scope:
            assert ordered_checkpoints is not None
            prefix_complete, last_source_sequence = _consume_ordered_clock_lineage(
                records, source, comparator, stats, ordered_checkpoints)
        else:
            for row in records:
                last_source_sequence = row["seq"]
                source.consume(row)
                if match_clock_scope:
                    assert (positive_target is not None and clock60_target is not None and
                            match_clock_target is not None and positive_audit is not None and
                            clock60_audit is not None and match_clock_audit is not None)
                    if (not positive_prefix_rejoined and
                            _match_boundary_join_complete(row, source, comparator, positive_target)):
                        audited_prefix = positive_audit["observed"]["source_prefix"]
                        if (stats.records_read != audited_prefix["records_read"] or
                                stats.bytes_read != audited_prefix["bytes_read"] or
                                stats.prefix_sha256 != audited_prefix["sha256"]):
                            raise ComparisonError(
                                "fresh source prefix differs from the accepted first-positive audit before continuation")
                        positive_prefix_rejoined = True
                        continue
                    if (positive_prefix_rejoined and not clock60_prefix_rejoined and
                            _match_boundary_join_complete(row, source, comparator, clock60_target)):
                        audited_prefix = clock60_audit["observed"]["source_prefix"]
                        if (stats.records_read != audited_prefix["records_read"] or
                                stats.bytes_read != audited_prefix["bytes_read"] or
                                stats.prefix_sha256 != audited_prefix["sha256"]):
                            raise ComparisonError(
                                "fresh source prefix differs from the accepted clock-60 audit before continuation")
                        clock60_prefix_rejoined = True
                        continue
                    if (clock60_prefix_rejoined and
                            _match_boundary_join_complete(row, source, comparator, match_clock_target)):
                        audited_prefix = match_clock_audit["observed"]["source_prefix"]
                        if (stats.records_read != audited_prefix["records_read"] or
                                stats.bytes_read != audited_prefix["bytes_read"] or
                                stats.prefix_sha256 != audited_prefix["sha256"]):
                            raise ComparisonError(
                                "fresh source prefix differs from the accepted terminal match-clock audit")
                        prefix_complete = positive_prefix_rejoined and clock60_prefix_rejoined
                        break
                elif clock60_scope:
                    assert positive_target is not None and clock60_target is not None
                    if (not positive_prefix_rejoined and
                            _match_boundary_join_complete(row, source, comparator, positive_target)):
                        audited_prefix = positive_audit["observed"]["source_prefix"]
                        if (stats.records_read != audited_prefix["records_read"] or
                                stats.bytes_read != audited_prefix["bytes_read"] or
                                stats.prefix_sha256 != audited_prefix["sha256"]):
                            raise ComparisonError(
                                "fresh source prefix differs from the accepted first-positive audit before continuation")
                        positive_prefix_rejoined = True
                        continue
                    if _match_boundary_join_complete(row, source, comparator, clock60_target):
                        terminal_prefix = clock60_audit["observed"]["source_prefix"]
                        if (stats.records_read != terminal_prefix["records_read"] or
                                stats.bytes_read != terminal_prefix["bytes_read"] or
                                stats.prefix_sha256 != terminal_prefix["sha256"]):
                            raise ComparisonError(
                                "fresh source prefix differs from the clock-60 audit at the terminal boundary")
                        prefix_complete = positive_prefix_rejoined
                        break
                else:
                    joined = (_first_positive_match_join_complete(
                        row, source, comparator, target)
                        if positive_scope and target is not None
                        else _first_match_tick_join_complete(row, source, comparator))
                    if joined:
                        prefix_complete = True
                        break
        result["last_source_sequence"] = last_source_sequence
        if not prefix_complete:
            boundary = ("frozen ordered match-clock target after rejoining all earlier checkpoints"
                        if ordered_lineage_scope else
                        "frozen match-clock boundary after rejoining the first-positive and clock-60 prefixes"
                        if match_clock_scope else
                        "frozen clock-60 boundary after rejoining the first-positive prefix"
                        if clock60_scope else
                        "frozen first-positive match-frame join" if positive_scope
                        else "completed match-0 source_tick 0 join")
            raise ComparisonError(f"source prefix ended before the {boundary}")
        source_stat_after = _file_stat_identity(reference_path)
        result["source_full_trace"]["stat_after"] = source_stat_after
        _require_stable_mwro_stat(source_stat_before, source_stat_after)
        result["source_full_trace"]["stat_stable_during_attempt"] = True
        if ordered_lineage_scope:
            assert ordered_checkpoints is not None
            audited_prefix = ordered_checkpoints[-1]["prefix"]
            expected_prefix_records = audited_prefix["records_read"]
            expected_prefix_bytes = audited_prefix["bytes_read"]
            expected_prefix_sha = audited_prefix["sha256"]
        elif match_clock_scope:
            assert match_clock_audit is not None
            audited_prefix = match_clock_audit["observed"]["source_prefix"]
            expected_prefix_records = audited_prefix["records_read"]
            expected_prefix_bytes = audited_prefix["bytes_read"]
            expected_prefix_sha = audited_prefix["sha256"]
        elif clock60_scope:
            assert clock60_audit is not None
            audited_prefix = clock60_audit["observed"]["source_prefix"]
            expected_prefix_records = audited_prefix["records_read"]
            expected_prefix_bytes = audited_prefix["bytes_read"]
            expected_prefix_sha = audited_prefix["sha256"]
        elif positive_scope:
            assert positive_audit is not None
            audited_prefix = positive_audit["observed"]["source_prefix"]
            expected_prefix_records = audited_prefix["records_read"]
            expected_prefix_bytes = audited_prefix["bytes_read"]
            expected_prefix_sha = audited_prefix["sha256"]
        else:
            expected_prefix_records = source_identity["audit_records_decoded"]
            expected_prefix_bytes = source_identity["audit_bytes_read"]
            expected_prefix_sha = None
        if (source.record_count != stats.records_read or
                stats.records_read != expected_prefix_records or
                stats.bytes_read != expected_prefix_bytes or
                (expected_prefix_sha is not None and
                 stats.prefix_sha256 != expected_prefix_sha)):
            message = ("freshly consumed source prefix differs from its bounded ordered clock-lineage audit"
                       if ordered_lineage_scope else
                       "freshly consumed source prefix differs from its bounded match-clock audit"
                       if match_clock_scope else
                       "freshly consumed source prefix differs from its bounded clock-60 audit"
                       if clock60_scope else
                       "freshly consumed source prefix differs from its bounded source audit"
                       if positive_scope else
                       "freshly consumed source prefix differs from its bounded identity audit")
            raise ComparisonError(message)
        if (comparator.frame_index != required_cursor or comparator.compared != required_cursor):
            message = ("comparison did not stop immediately after its frozen ordered clock-lineage target"
                       if ordered_lineage_scope else
                       "comparison did not stop immediately after its frozen match-clock boundary"
                       if match_clock_scope else
                       "comparison did not stop immediately after its frozen clock-60 boundary"
                       if clock60_scope else
                       "comparison did not stop immediately after its frozen match boundary"
                       if positive_scope else
                       "comparison did not stop immediately after the first match tick join")
            raise ComparisonError(message)
        if source.setup_bytes != [recipe_obj.match_setups[0]]:
            raise ComparisonError("bounded source prefix consumed an unexpected match setup count")
        result["checks"]["source_first_css_and_setup_binding"] = "pass"
        result["checks"]["setup_state"] = "pass"
        if ordered_lineage_scope:
            result["checks"]["source_ticks_through_ordered_clock_lineage"] = "pass"
            result["checks"]["source_ticks_through_prior_ordered_checkpoints"] = "pass"
            result["checks"]["source_pad_to_tick0_join"] = "pass"
            result["checks"]["tick0_state"] = "pass"
            result["checks"]["first_positive_match_frame_state"] = "pass"
            result["checks"]["clock60_match_frame_state"] = "pass"
            result["checks"]["ordered_clock_checkpoint_states"] = "pass"
        elif match_clock_scope:
            result["checks"]["source_ticks_through_match_clock_boundary"] = "pass"
            result["checks"]["source_ticks_through_clock60"] = "pass"
            result["checks"]["source_pad_to_tick0_join"] = "pass"
            result["checks"]["tick0_state"] = "pass"
            result["checks"]["first_positive_match_frame_state"] = "pass"
            result["checks"]["clock60_match_frame_state"] = "pass"
            result["checks"]["match_clock_boundary_state"] = "pass"
        elif clock60_scope:
            result["checks"]["source_ticks_through_clock60"] = "pass"
            result["checks"]["source_pad_to_tick0_join"] = "pass"
            result["checks"]["tick0_state"] = "pass"
            result["checks"]["first_positive_match_frame_state"] = "pass"
            result["checks"]["clock60_match_frame_state"] = "pass"
        elif positive_scope:
            result["checks"]["source_ticks_through_first_positive"] = "pass"
            result["checks"]["source_pad_to_tick0_join"] = "pass"
            result["checks"]["tick0_state"] = "pass"
            result["checks"]["first_positive_match_frame_state"] = "pass"
        else:
            result["checks"]["source_pad_to_tick0_join"] = "pass"
            result["checks"]["tick0_state"] = "pass"
        result["source_full_trace"].update(source_identity)
        result["source_full_trace"]["stat_before"] = source_stat_before
        result["source_full_trace"]["stat_after"] = source_stat_after
        result["source_full_trace"]["stat_stable_during_attempt"] = True
        result.update({
            "boundary_result": "equivalent",
            "result": "incomplete",
            "complete": False,
            "whole_session_equivalent": False,
            "match_state_fields_compared": list(comparator.compare_fields),
            "setup_state_fields_compared": [*comparator.compare_fields, "declared_setup"],
            "frames_requested": recipe_obj.frame_count,
            "timeline_frames_consumed": comparator.compared,
            "nonmatch_frames_input_ordered": comparator.nonmatch_compared,
            "browser_frames_captured": browser_identity["exported_cursor"],
            "match_state_frames_compared": comparator.match_compared,
            "source_prefix": {
                "records_read": stats.records_read,
                "bytes_read": stats.bytes_read,
                "sha256": stats.prefix_sha256,
                "hash_basis": (f"fresh SHA-256 over exactly the raw bytes consumed through the ordered first match_frame {match_clock_target['target_match_frame_at_least']} boundary"
                               if ordered_lineage_scope and match_clock_target is not None else
                               f"fresh SHA-256 over exactly the raw bytes consumed through the first match_frame {match_clock_target['target_match_frame_at_least']} boundary"
                               if match_clock_scope and match_clock_target is not None else
                               "fresh SHA-256 over exactly the raw bytes consumed through the first match_frame 60 boundary"
                               if clock60_scope else
                               "fresh SHA-256 over exactly the raw bytes consumed through the first positive match frame"
                               if positive_scope else
                               "fresh SHA-256 over exactly the raw bytes consumed through source_tick 0"),
                "last_source_sequence": last_source_sequence,
                "first_entry_seq": source.first_entry_seq,
                "first_setup_seq": source.first_setup_seq,
                "first_setup_source_tick": source.first_setup_source_tick,
                "first_source_tick_seq": source.first_source_tick_seq,
                "first_source_tick": source.first_source_tick,
            },
            "setup_records_compared": comparator.setup_count,
            **({"first_match_boundary": {
                "match_index": 0,
                "setup_source_seq": source.first_setup_seq,
                "source_tick_seq": source.first_source_tick_seq,
                "source_tick": source.first_source_tick,
                "timeline_frame_index": comparator.frame_index - 1,
                "browser_target_cursor": browser_identity["target_cursor"],
            }} if not needs_positive_audit else {}),
            "source_scene_spans_observed": comparator.source_spans,
            "capture_status": ("incomplete bounded capture; semantic comparison stopped after the frozen ordered match-clock source boundary"
                               if ordered_lineage_scope else
                               "incomplete bounded capture; semantic comparison stopped after the frozen match-clock source boundary"
                               if match_clock_scope else
                               "incomplete bounded capture; semantic comparison stopped after the frozen clock-60 source boundary"
                               if clock60_scope else
                               "incomplete bounded capture; semantic comparison stopped after the frozen first-positive match-frame join"
                               if positive_scope else
                               "incomplete bounded capture; semantic comparison stopped after the first source tick join"),
            "original_prefix_audit": {
                "path": str(source_audit_path),
                "sha256": source_identity["audit_sha256"],
                "records_and_bytes_agree": True,
                "full_source_trace_was_rehashed": False,
            },
            "producer_manifest_sha256": browser_identity["producer_manifest_sha256"],
            "source_manifest_sha256": source_identity["manifest_sha256"],
            "source_report_sha256": source_identity["source_report_sha256"],
            "browser_capture_report_sha256": browser_identity["capture_report_sha256"],
            "browser_report_sha256": browser_identity["browser_report_sha256"],
            "browser_trace_sha256": browser_identity["port_trace_sha256"],
        })
        if positive_scope and target is not None and positive_audit_sha is not None:
            result["first_positive_match_frame_boundary"] = {
                **target,
                "setup_source_sequence": source.first_setup_seq,
                "browser_observed_cursor": browser_identity["target_cursor"],
                "browser_exported_cursor": browser_identity["exported_cursor"],
                "source_prefix_sha256": stats.prefix_sha256,
                "source_prefix_records": stats.records_read,
                "source_prefix_bytes": stats.bytes_read,
                "audit_sha256": positive_audit_sha,
            }
        if clock60_scope and clock60_target is not None and positive_audit_sha is not None:
            assert clock60_boundary_audit_path is not None
            result["clock60_match_frame_boundary"] = {
                **clock60_target,
                "setup_source_sequence": source.first_setup_seq,
                "browser_observed_cursor": browser_identity["target_cursor"],
                "browser_exported_cursor": browser_identity["exported_cursor"],
                "source_prefix_sha256": stats.prefix_sha256,
                "source_prefix_records": stats.records_read,
                "source_prefix_bytes": stats.bytes_read,
                "first_positive_audit_sha256": positive_audit_sha,
                "clock60_audit_sha256": result["clock60_boundary_audit"]["sha256"],
            }
            prior_prefix = positive_audit["observed"]["source_prefix"]
            result["first_positive_prefix_rejoin"] = {
                "source_sequence": positive_target["source_sequence"],
                "source_prefix_sha256": prior_prefix["sha256"],
                "source_prefix_records": prior_prefix["records_read"],
                "source_prefix_bytes": prior_prefix["bytes_read"],
                "rejoined_before_continuing": True,
            }
        if (ordered_lineage_scope and ordered_checkpoints is not None and
                match_clock_target is not None and match_clock_audit_sha is not None):
            result["ordered_match_clock_boundary"] = {
                **match_clock_target,
                "setup_source_sequence": source.first_setup_seq,
                "browser_observed_cursor": browser_identity["target_cursor"],
                "browser_exported_cursor": browser_identity["exported_cursor"],
                "source_prefix_sha256": stats.prefix_sha256,
                "source_prefix_records": stats.records_read,
                "source_prefix_bytes": stats.bytes_read,
                "terminal_audit_sha256": match_clock_audit_sha,
                "runner_packet_sha256": packet["source"]["ordered_clock_lineage"]
                ["runner_packet"]["sha256"],
                "fresh_prefix_checkpoints": [
                    {"label": checkpoint["label"], **checkpoint["tuple"],
                     "bytes_read": checkpoint["prefix"]["bytes_read"],
                     "records_read": checkpoint["prefix"]["records_read"],
                     "source_prefix_sha256": checkpoint["prefix"]["sha256"],
                     "audit_sha256": checkpoint["audit"]["sha256"],
                     "rejoined_before_continuing": True}
                    for checkpoint in ordered_checkpoints],
            }
        if (match_clock_scope and match_clock_target is not None and
                positive_audit_sha is not None and clock60_audit_sha is not None and
                match_clock_audit_sha is not None):
            result["match_clock_boundary"] = {
                **match_clock_target,
                "setup_source_sequence": source.first_setup_seq,
                "browser_observed_cursor": browser_identity["target_cursor"],
                "browser_exported_cursor": browser_identity["exported_cursor"],
                "source_prefix_sha256": stats.prefix_sha256,
                "source_prefix_records": stats.records_read,
                "source_prefix_bytes": stats.bytes_read,
                "first_positive_audit_sha256": positive_audit_sha,
                "clock60_audit_sha256": clock60_audit_sha,
                "terminal_audit_sha256": match_clock_audit_sha,
            }
            for name, checkpoint, audited_prefix, audit_sha in (
                    ("first_positive", positive_target,
                     positive_audit["observed"]["source_prefix"], positive_audit_sha),
                    ("clock60", clock60_target,
                     clock60_audit["observed"]["source_prefix"], clock60_audit_sha)):
                result[f"{name}_prefix_rejoin"] = {
                    "source_sequence": checkpoint["source_sequence"],
                    "source_prefix_sha256": audited_prefix["sha256"],
                    "source_prefix_records": audited_prefix["records_read"],
                    "source_prefix_bytes": audited_prefix["bytes_read"],
                    "audit_sha256": audit_sha,
                    "rejoined_before_continuing": True,
                }
    except (ComparisonError, ObserverStreamError, OSError, ValueError, KeyError,
            TypeError, struct.error) as error:
        first_difference = comparator.first_difference if comparator is not None else None
        is_browser_divergence = first_difference is not None
        result["boundary_result"] = "divergent" if is_browser_divergence else None
        result["result"] = "incomplete" if is_browser_divergence else "invalid"
        result["complete"] = False
        result["whole_session_equivalent"] = False
        result["error"] = str(error)
        if last_source_sequence is not None:
            result["last_source_sequence"] = last_source_sequence
        if source_stat_before is not None:
            try:
                source_stat_after = _file_stat_identity(reference_path)
                result.setdefault("source_full_trace", {})["stat_after"] = source_stat_after
                try:
                    _require_stable_mwro_stat(source_stat_before, source_stat_after)
                    result["source_full_trace"]["stat_stable_during_attempt"] = True
                except ComparisonError:
                    result["source_full_trace"]["stat_stable_during_attempt"] = False
                    result["result"] = "invalid"
                    result["boundary_result"] = None
                    result["error"] = (
                        f"{error}; source MWRO stat identity changed during bounded prefix read")
            except OSError as stat_error:
                result.setdefault("source_full_trace", {})["stat_after_error"] = str(stat_error)
        if stats is not None:
            result["source_prefix"] = {
                "records_read": stats.records_read,
                "bytes_read": stats.bytes_read,
                "sha256": stats.prefix_sha256,
                "hash_basis": "fresh SHA-256 over exactly the raw bytes consumed before stop/failure",
                "last_source_sequence": last_source_sequence,
            }
        if comparator is not None:
            result.update({
                "frames_requested": comparator.recipe.frame_count,
                "timeline_frames_consumed": comparator.compared,
                "nonmatch_frames_input_ordered": comparator.nonmatch_compared,
                "setup_records_compared": comparator.setup_count,
                "match_state_frames_compared": comparator.match_compared,
            })
            if comparator.setup_count:
                result["setup_state_fields_compared"] = [*comparator.compare_fields,
                                                         "declared_setup"]
            if comparator.match_compared:
                result["match_state_fields_compared"] = list(comparator.compare_fields)
            if first_difference is not None:
                result["first_difference"] = first_difference
    finally:
        if records is not None:
            records.close()
        if browser is not None:
            browser.close()
    return result


def compare_paths(reference: str | Path, recipe: str | Path, port_trace: str | Path,
                  *, browser_report: str | Path | None = None,
                  scope: str = WHOLE_SESSION_SCOPE,
                  expectations: str | Path | None = None,
                  source_manifest: str | Path | None = None,
                  source_report: str | Path | None = None,
                  source_audit: str | Path | None = None,
                  browser_capture_report: str | Path | None = None,
                  browser_producer_manifest: str | Path | None = None,
                  positive_boundary_audit: str | Path | None = None,
                  clock60_boundary_audit: str | Path | None = None,
                  match_clock_boundary_audit: str | Path | None = None) -> dict[str, Any]:
    """Compare one source capture, its MWRC recipe, and one browser trace."""
    reference_path = Path(reference)
    recipe_path = Path(recipe)
    port_path = Path(port_trace)
    if _is_v10_prefix_scope(scope):
        required = (source_manifest, source_report, source_audit, browser_capture_report,
                    browser_producer_manifest, browser_report, expectations)
        if scope in {V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE,
                     V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                     V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                     V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
            required += (positive_boundary_audit,)
        if scope in {V10_FIRST_MATCH_CLOCK_GE60_SCOPE,
                     V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                     V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
            required += (clock60_boundary_audit,)
        if scope in {V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE,
                     V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE}:
            required += (match_clock_boundary_audit,)
        if any(value is None for value in required):
            label = ("v10 ordered match-clock lineage" if scope == V10_FIRST_MATCH_CLOCK_ORDERED_LINEAGE_SCOPE else
                     "v10 first-match clock-boundary" if scope == V10_FIRST_MATCH_CLOCK_BOUNDARY_SCOPE else
                     "v10 first-match clock-60" if scope == V10_FIRST_MATCH_CLOCK_GE60_SCOPE else
                     "v10 first-positive match-frame" if scope == V10_FIRST_POSITIVE_MATCH_FRAME_SCOPE
                     else "v10 first-setup/tick-0")
            return {
                "schema": SCHEMA, "scope": scope,
                "boundary_result": None, "result": "invalid", "complete": False,
                "whole_session_equivalent": False,
                "error": f"{label} scope requires frozen expectations and all provenance sidecars",
            }
        return _compare_v10_bounded_prefix(
            reference_path, recipe_path, port_path,
            scope=scope,
            source_manifest_path=Path(source_manifest),
            source_report_path=Path(source_report), source_audit_path=Path(source_audit),
            browser_capture_report_path=Path(browser_capture_report),
            browser_producer_manifest_path=Path(browser_producer_manifest),
            browser_report_path=Path(browser_report),
            expectations_path=Path(expectations),
            positive_boundary_audit_path=(Path(positive_boundary_audit)
                                          if positive_boundary_audit is not None else None),
            clock60_boundary_audit_path=(Path(clock60_boundary_audit)
                                         if clock60_boundary_audit is not None else None),
            match_clock_boundary_audit_path=(Path(match_clock_boundary_audit)
                                             if match_clock_boundary_audit is not None else None))
    if scope != WHOLE_SESSION_SCOPE:
        return {"schema": SCHEMA, "scope": scope, "boundary_result": None,
                "result": "invalid", "complete": False,
                "whole_session_equivalent": False,
                "error": f"unsupported whole-session comparison scope {scope!r}"}
    result: dict[str, Any] = {
        "schema": SCHEMA,
        "scope": "Exact source-consumed scene/input state comparison; no timing, draw cadence, pixels, PCM, or admission claim",
        "fields_compared": list(COMPARE_FIELDS),
        "nonmatch_fields_compared": list(NONMATCH_FIELDS),
        "match_fields_compared": list(COMPARE_FIELDS),
        "limitations": {
            "nonmatch_state": "MWRO CSS/SSS/Results boundary snapshots carry RNG, PAD, and scene-frame fields but do not carry match-frame/FighterHead state; pad_poll rows carry PAD without RNG and can be VI-interrupted. The comparator does not infer a boundary-to-browser per-tick join, so non-match scalar state is not claimed equivalent",
            "final_css": "The browser completion report must bind the recipe/trace hashes and report final_scene=1, complete=true, pass=true with no failures/errors; CSS scalar state is not claimed equivalent",
            "menu_lifecycle": "Menu asset/audio/process fields are outside the browser trace and are not fabricated from scene numbers",
        },
        "inputs": {},
    }
    comparator: Comparator | None = None
    browser: BrowserReader | None = None
    try:
        recipe_obj = Recipe(recipe_path, recipe_path.read_bytes())
        compared_fields = comparison_fields(recipe_obj.version)
        result["fields_compared"] = list(compared_fields)
        result["match_fields_compared"] = list(compared_fields)
        result["inputs"] = {
            "reference": {"path": str(reference_path), "bytes": reference_path.stat().st_size,
                          "sha256": _sha256(reference_path)},
            "recipe": {"path": str(recipe_path), "bytes": recipe_path.stat().st_size,
                       "sha256": _sha256(recipe_path)},
            "port_trace": {"path": str(port_path), "bytes": port_path.stat().st_size,
                           "sha256": _sha256(port_path)},
        }
        if browser_report is not None:
            report_path = Path(browser_report)
            result["inputs"]["browser_report"] = {
                "path": str(report_path), "bytes": report_path.stat().st_size,
                "sha256": _sha256(report_path),
            }
        browser = BrowserReader(port_path)
        comparator = Comparator(recipe_obj, browser)
        source = SourceCollector(comparator, recipe_obj)
        for row in iter_records(reference_path):
            source.consume(row)
        source.finish()
        report_sidecar = _browser_report(Path(browser_report) if browser_report else None)
        final_check = "missing"
        if report_sidecar:
            _validate_browser_report(
                report_sidecar,
                recipe_sha=result["inputs"]["recipe"]["sha256"],
                trace_sha=result["inputs"]["port_trace"]["sha256"],
                frame_count=recipe_obj.frame_count,
            )
            endpoint_scene = report_sidecar.get("final_scene") == SCENES["css"]
            endpoint_ok = _browser_completion_ok(report_sidecar)
            if endpoint_ok:
                final_check = "report_pass"
            elif endpoint_scene:
                final_check = "scene_only"
        nonmatch_pad_check = "uncovered"
        complete = final_check in {"pass", "report_pass"}
        result.update({
            "result": "equivalent" if complete else "incomplete",
            "complete": complete,
            "checks": {
                "source_stream": "pass", "recipe_binding": "pass", "scene_order": "pass",
                "input_order": "pass", "nonmatch_pad": nonmatch_pad_check,
                "initial_css_rng": "pass",
                "match_state": "pass", "final_css": final_check,
            },
            "frames_requested": recipe_obj.frame_count,
            "frames_compared": comparator.compared,
            "nonmatch_frames_compared": comparator.nonmatch_compared,
            "match_frames_compared": comparator.match_compared,
            "scene_spans": recipe_obj.spans,
            "boundary_checkpoints": comparator.boundary_checkpoints,
            "match_setups_compared": comparator.setup_count,
            "source_final_css": {"source_seq": source.final_css["source_seq"],
                                 "source_tick": source.final_css["source_tick"]},
        })
        browser.close()
        return result
    except (ComparisonError, OSError, ValueError, KeyError, struct.error) as error:
        source_error = str(error).startswith(("source ", "MWRC", "this comparator",
                                               "observer", "first CSS", "MWRC"))
        result.update({"result": "invalid" if browser is None or source_error else "diverged",
                       "complete": False, "error": str(error),
                       "checks": result.get("checks", {})})
        if comparator is not None:
            checks = {
                "source_stream": "prefix_only",
                "recipe_binding": "pass_prefix",
                "scene_order": "pass_prefix",
                "input_order": "pass_prefix",
                "initial_css_rng": "pass_prefix",
                "match_state": "pass_prefix",
                "final_css": "unverified",
            }
            result.update({"frames_requested": comparator.recipe.frame_count,
                           "frames_compared": comparator.compared,
                           "nonmatch_frames_compared": comparator.nonmatch_compared,
                           "match_frames_compared": comparator.match_compared,
                           "match_setups_compared": comparator.setup_count,
                           "boundary_checkpoints": comparator.boundary_checkpoints,
                           "checks": checks})
            if comparator.first_difference is not None:
                result["first_difference"] = comparator.first_difference
                if _is_match_field(comparator.first_difference.get("field")) or \
                        comparator.first_difference.get("record") == "match_enter_complete":
                    checks["match_state"] = "fail"
        if browser is not None:
            browser.close()
        return result
