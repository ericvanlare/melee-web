"""Validate one bounded, passive Zelda-to-Sheik original ownership prefix.

This is source-owner evidence only. It does not compare port state, menus,
pixels, audio, timing, or a complete match.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference-capture" / "dolphin"))

from reference_observer_stream import (  # noqa: E402
    ObserverStreamError,
    ObserverStreamStats,
    iter_records,
    read_status,
)
from retail_setup_validation import SetupValidationError, _decode_setup  # noqa: E402
from whole_session_replay import (  # noqa: E402
    EXPECTED_DOLPHIN_COMMIT,
    EXPECTED_DOL_SHA1,
    EXPECTED_DOL_SHA256,
    EXPECTED_OBSERVER_SCHEMA,
)


SCHEMA = "melee-web-sheik-transform-prefix"
VERSION = 1
MAX_ACTIVE_SOURCE_TICKS = 600
MAX_STREAM_BYTES = 128 * 1024 * 1024
MAX_STREAM_RECORDS = 250_000
MEM1_BASE = 0x80000000
MEM1_END = 0x81800000
PLAYER_TABLE = 0x80453080
PLAYER_STRIDE = 0xE90
PLAYER_ENTITIES_OFFSET = 0xB0
PLAYER_TRANSFORMED_OFFSET = 0x0C
GOBJ_USER_DATA_OFFSET = 0x2C
PLAYER_ENTITIES_TAG = 52
PLAYER_ENTITY_USER_DATA_TAG = 53
PLAYER_TRANSFORMED_TAG = 58
FIGHTER_HEAD_TAG = 5
PAD_QUEUE_TAG = 2
PAD_SLOT_TAG = 3
SCENE_ROUTING_TAG = 17
SCENE_KIND_TAG = 40
STAGE_SELECT_INDEX_TAG = 41
STAGE_SELECT_KIND_TAG = 42
MENU_CSS_DOORS_TAG = 44
MENU_CSS_LIVE_STATE_TAG = 48
CSS_DOORS_ADDRESS = 0x803F0DFC
CSS_LIVE_STATE_SIZE = 0x148
STAGE_SELECT_TABLE = 0x803F06D0
STAGE_SELECT_STRIDE = 0x1C
STAGE_SELECT_COUNT = 30
STAGE_SELECT_KIND_OFFSET = 0x0B
ZELDA_KIND = 19
SHEIK_KIND = 7
MARIO_KIND = 0
ZELDA_DOWN_B_MOTION = 355
NEUTRAL_MOTION = 14
BUTTON_B = 0x0200
COMPLETION_BOUNDARY = "active_sheik_grounded_neutral_source_tick_after_owner_change"
SETUP_PC = 0x8016E9C4
PAD_CONSUME_PC = 0x80377584
SOURCE_TICK_PC = 0x80390EB4
PAD_POLL_PC = 0x8034DD8C


class TransformPrefixError(ValueError):
    """The bounded stream cannot prove the declared ownership transition."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise TransformPrefixError(message)


def _be32(raw: bytes, offset: int, context: str) -> int:
    _require(offset >= 0 and offset + 4 <= len(raw), f"{context}: missing source word")
    return struct.unpack_from(">I", raw, offset)[0]


def _be16(raw: bytes, offset: int, context: str) -> int:
    _require(offset >= 0 and offset + 2 <= len(raw), f"{context}: missing source halfword")
    return struct.unpack_from(">H", raw, offset)[0]


def _raw_slice(record: dict[str, Any], tag: int, flags: int, context: str,
               size: int | None = None) -> dict[str, Any]:
    matches = [item for item in record["slices"]
               if item["tag"] == tag and item["flags"] == flags]
    _require(len(matches) == 1, f"{context}: expected one tag={tag} flags={flags:#x}")
    item = matches[0]
    raw = bytes.fromhex(item["hex"])
    _require(size is None or len(raw) == size,
             f"{context}: tag={tag} has size {len(raw)}, expected {size}")
    _require(item["size"] == len(raw), f"{context}: decoded slice size disagrees with bytes")
    return {**item, "raw": raw}


def _expected_slice_order(payload: dict[str, Any], *, setup: bool) -> None:
    slices = payload.get("slices")
    expected = []
    if setup:
        expected.append((4, 0, 0x138))
    expected.extend([
        (PLAYER_ENTITIES_TAG, 0, 8),
        (PLAYER_TRANSFORMED_TAG, 0, 2),
        (PLAYER_ENTITY_USER_DATA_TAG, 0, 4),
        (FIGHTER_HEAD_TAG, 0, 0x100),
        (PLAYER_ENTITY_USER_DATA_TAG, 0x100, 4),
        (FIGHTER_HEAD_TAG, 0x100, 0x100),
        (PLAYER_ENTITIES_TAG, 1, 8),
        (PLAYER_ENTITY_USER_DATA_TAG, 1, 4),
        (FIGHTER_HEAD_TAG, 1, 0x100),
    ])
    _require(isinstance(slices, list) and len(slices) == len(expected),
             "transform owner boundary has missing or extra source slices")
    actual = [(item.get("tag"), item.get("flags"), item.get("size"))
              for item in slices]
    _require(actual == expected, "transform owner slices changed authored field/order contract")


def _menu_owner_pad_poll(payload: dict[str, Any]) -> str | None:
    """Identify one live mode-2 CSS or SSS owner in an existing PadPoll row."""

    scene_matches = [item for item in payload.get("slices", [])
                     if item.get("tag") == SCENE_KIND_TAG]
    if not scene_matches:
        return None
    _require(len(scene_matches) == 1 and scene_matches[0].get("flags") == 0,
             "menu PadPoll has a duplicate or ambiguous SceneKind slice")
    scene = _raw_slice(payload, SCENE_KIND_TAG, 0, "menu PadPoll", 1)
    scene_kind = scene["raw"][0]
    if scene_kind not in (8, 9):
        return None

    routing = _raw_slice(payload, SCENE_ROUTING_TAG, 0, "menu PadPoll", 6)
    _require(routing["address"] == 0x80479D30,
             "menu PadPoll routing slice escaped its pinned source address")
    if routing["raw"][0] != 0x02:
        return None

    slices_by_tag: dict[int, list[dict[str, Any]]] = {}
    for item in payload.get("slices", []):
        slices_by_tag.setdefault(item.get("tag"), []).append(item)
    if scene_kind == 8:
        live_count = len(slices_by_tag.get(MENU_CSS_LIVE_STATE_TAG, []))
        doors_count = len(slices_by_tag.get(MENU_CSS_DOORS_TAG, []))
        _require(live_count <= 1 and doors_count <= 1,
                 "CSS PadPoll contains duplicate or ambiguous owner slices")
        if live_count != 1 or doors_count != 1:
            return None
        live = _raw_slice(payload, MENU_CSS_LIVE_STATE_TAG, 0,
                          "CSS live-owner PadPoll", CSS_LIVE_STATE_SIZE)
        doors = _raw_slice(payload, MENU_CSS_DOORS_TAG, 0,
                           "CSS live-owner PadPoll", 0x90)
        _require(MEM1_BASE <= live["address"] and
                 live["address"] + CSS_LIVE_STATE_SIZE <= MEM1_END and
                 doors["address"] == CSS_DOORS_ADDRESS,
                 "CSS PadPoll owner slices escaped their pinned source owners")
        return "css"

    index_count = len(slices_by_tag.get(STAGE_SELECT_INDEX_TAG, []))
    kind_count = len(slices_by_tag.get(STAGE_SELECT_KIND_TAG, []))
    _require(index_count <= 1 and kind_count <= 1,
             "SSS PadPoll contains duplicate or ambiguous owner slices")
    if kind_count and not index_count:
        raise TransformPrefixError("SSS PadPoll exposes a stage kind without its owner index")
    if index_count != 1 or kind_count != 1:
        return None
    index = _raw_slice(payload, STAGE_SELECT_INDEX_TAG, 0,
                       "SSS live-owner PadPoll", 1)
    stage_index = index["raw"][0]
    _require(index["address"] == 0x804D6CAE and stage_index < STAGE_SELECT_COUNT,
             "SSS PadPoll stage owner index is outside its authored table")
    kind = _raw_slice(payload, STAGE_SELECT_KIND_TAG, 0,
                      "SSS live-owner PadPoll", 1)
    expected_kind_address = (STAGE_SELECT_TABLE + stage_index * STAGE_SELECT_STRIDE +
                             STAGE_SELECT_KIND_OFFSET)
    _require(kind["address"] == expected_kind_address,
             "SSS PadPoll stage owner kind escaped its authored table entry")
    return "sss"


def _decode_owner_sample(payload: dict[str, Any], *, setup: bool,
                         sequence: int, source_tick: int) -> dict[str, Any]:
    context = "setup owner inventory" if setup else f"source_tick[{source_tick}] owner inventory"
    _expected_slice_order(payload, setup=setup)
    slices = payload["slices"]
    if setup:
        match_setup = _raw_slice(payload, 4, 0, context, 0x138)
        _require(match_setup["address"] >= 0x80000000,
                 f"{context}: setup pointer escaped source MEM1")
        try:
            declared = _decode_setup(match_setup["hex"])
        except (SetupValidationError, TypeError, ValueError) as error:
            raise TransformPrefixError(f"{context}: invalid original VS setup: {error}") from error
        players = declared["players"]
        _require(len(players) == 2 and
                 [(player["port"], player["character_kind"], player["player_type"])
                  for player in players] == [(1, 18, 0), (2, 8, 0)] and
                 declared["stage"] == 32 and declared["is_stock"] and
                 [player["stocks"] for player in players] == [4, 4],
                 f"{context}: expected two-human Zelda/Mario four-stock Final Destination setup")
        setup_sha256 = hashlib.sha256(match_setup["raw"]).hexdigest()
    else:
        setup_sha256 = None

    player_entities: dict[int, tuple[int, int]] = {}
    player_entities_raw: dict[int, str] = {}
    transformed = b""
    fighters: dict[tuple[int, int], dict[str, Any]] = {}
    links: dict[tuple[int, int], dict[str, int]] = {}
    for item in slices:
        raw = bytes.fromhex(item["hex"])
        tag, flags, address = item["tag"], item["flags"], item["address"]
        _require(MEM1_BASE <= address and address + len(raw) <= MEM1_END,
                 f"{context}: source slice escaped the pinned MEM1 range")
        if tag == PLAYER_ENTITIES_TAG:
            _require(len(raw) == 8, f"{context}: StaticPlayer entity pair is not eight bytes")
            expected_address = PLAYER_TABLE + flags * PLAYER_STRIDE + PLAYER_ENTITIES_OFFSET
            _require(address == expected_address,
                     f"{context}: entity pointers do not name the authored StaticPlayer slot")
            player_entities[flags] = (_be32(raw, 0, context), _be32(raw, 4, context))
            player_entities_raw[flags] = raw.hex()
        elif tag == PLAYER_TRANSFORMED_TAG:
            transformed = raw
            _require(address == PLAYER_TABLE + PLAYER_TRANSFORMED_OFFSET,
                     f"{context}: transformed bytes do not name P1 StaticPlayer")
        elif tag == PLAYER_ENTITY_USER_DATA_TAG:
            slot, entity_index = flags & 0xFF, flags >> 8
            _require(slot in (0, 1) and entity_index in (0, 1),
                     f"{context}: user_data slice has invalid slot/entity identity")
            links[(slot, entity_index)] = {
                "user_data_address": address,
                "fighter_pointer": _be32(raw, 0, context),
            }
        elif tag == FIGHTER_HEAD_TAG:
            slot, entity_index = flags & 0xFF, flags >> 8
            _require(slot in (0, 1) and entity_index in (0, 1),
                     f"{context}: FighterHead slice has invalid slot/entity identity")
            _require(len(raw) == 0x100, f"{context}: FighterHead size changed")
            fighters[(slot, entity_index)] = {
                "fighter_pointer": address,
                "raw": raw,
                "backlink": _be32(raw, 0, context),
                "kind": _be32(raw, 4, context),
                "player_id": raw[0x0C],
                "motion": _be32(raw, 0x10, context),
                "ground_air": _be32(raw, 0xE0, context),
            }

    _require(set(player_entities) == {0, 1} and len(transformed) == 2,
             f"{context}: missing source entity indexes")
    _require(set(transformed) == {0, 1},
             f"{context}: transformed indexes are not a permutation of entity ordinals 0 and 1")
    _require(player_entities[0][0] != 0 and player_entities[0][1] != 0 and
             player_entities[0][0] != player_entities[0][1] and player_entities[1][0] != 0 and
             player_entities[1][1] == 0,
             f"{context}: StaticPlayer entity pointers are missing or aliased")
    _require(set(fighters) == {(0, 0), (0, 1), (1, 0)} and set(links) == set(fighters),
             f"{context}: missing or extra fighter headers/GObj user_data links")
    expected_kinds = {(0, 0): ZELDA_KIND, (0, 1): SHEIK_KIND, (1, 0): MARIO_KIND}
    for key, entity in fighters.items():
        slot, entity_index = key
        gobj = player_entities[slot][entity_index]
        link = links[key]
        _require(link["user_data_address"] == gobj + GOBJ_USER_DATA_OFFSET and
                 link["fighter_pointer"] == entity["fighter_pointer"] and
                 entity["backlink"] == gobj and entity["player_id"] == slot and
                 entity["kind"] == expected_kinds[key],
                 f"{context}: GObj/Fighter/header/player ownership link differs for {key}")
    active_index = transformed[0]
    active_kind = fighters[(0, active_index)]["kind"]
    if setup:
        _require(active_index == 0 and active_kind == ZELDA_KIND,
                 f"{context}: initial active entity is not Zelda kind 19")
    active_fighter = fighters[(0, active_index)]
    portable = {
        "entity_ordinals": [0, 1],
        "fighter_kinds": [fighters[(0, 0)]["kind"], fighters[(0, 1)]["kind"]],
        "active_entity_index": active_index,
        "active_kind": active_kind,
        "active_motion": active_fighter["motion"],
        "active_ground_air": active_fighter["ground_air"],
    }
    local_pointer_checks = {
        "player_entities_hex_by_slot": [player_entities_raw[0], player_entities_raw[1]],
        "transformed_hex": transformed.hex(),
        "gobj_pointers": [player_entities[0][0], player_entities[0][1],
                          player_entities[1][0]],
        "fighter_pointers": [fighters[key]["fighter_pointer"]
                             for key in ((0, 0), (0, 1), (1, 0))],
        "gobj_user_data_hex": [links[key]["fighter_pointer"].to_bytes(4, "big").hex()
                               for key in ((0, 0), (0, 1), (1, 0))],
        "fighter_backlink_hex": [fighters[key]["raw"][:4].hex()
                                 for key in ((0, 0), (0, 1), (1, 0))],
    }
    slice_order = [(item["tag"], item["flags"], item["address"], item["size"])
                   for item in slices]
    slice_contract = b"".join(
        struct.pack(">HHII", item["tag"], item["flags"], item["address"], item["size"]) +
        bytes.fromhex(item["hex"]) for item in slices)
    return {
        "boundary": "setup" if setup else "source_tick",
        "sequence": sequence,
        "source_tick": source_tick,
        "setup_sha256": setup_sha256,
        "slice_order": slice_order,
        "slice_contract_sha256": hashlib.sha256(slice_contract).hexdigest(),
        "portable": portable,
        "local_pointer_checks": local_pointer_checks,
        "source_fields": {
            "fighter_kind_hex": [fighters[(0, 0)]["raw"][4:8].hex(),
                                 fighters[(0, 1)]["raw"][4:8].hex()],
            "player_id_hex": [fighters[(0, 0)]["raw"][0x0C:0x0D].hex(),
                              fighters[(0, 1)]["raw"][0x0C:0x0D].hex()],
            "motion_hex": [fighters[(0, 0)]["raw"][0x10:0x14].hex(),
                           fighters[(0, 1)]["raw"][0x10:0x14].hex()],
            "ground_air_hex": [fighters[(0, 0)]["raw"][0xE0:0xE4].hex(),
                               fighters[(0, 1)]["raw"][0xE0:0xE4].hex()],
        },
    }


def _validate_consumed_pad(payload: dict[str, Any], sequence: int) -> bool:
    slices = payload.get("slices")
    _require(isinstance(slices, list) and
             [(item.get("tag"), item.get("flags"), item.get("size")) for item in slices] ==
             [(PAD_QUEUE_TAG, 0, 0x0C), (PAD_SLOT_TAG, 0, 0x30)],
             f"PAD consume record {sequence} lacks exact queue/slot slices")
    queue = bytes.fromhex(slices[0]["hex"])
    slot = bytes.fromhex(slices[1]["hex"])
    _require(slices[0]["address"] == 0x804C1F78 and
             MEM1_BASE <= slices[1]["address"] and
             slices[1]["address"] + len(slot) <= MEM1_END,
             f"PAD consume record {sequence} queue/slot pointer escaped checked source memory")
    registers = payload.get("gprs")
    _require(isinstance(registers, list) and len(registers) == 32,
             f"PAD consume record {sequence} lacks its source registers")
    qread, slot_pointer = registers[6] & 0xFF, registers[25]
    _require(queue[0] > 0 and qread < queue[0] and
             _be32(queue, 8, f"PAD consume record {sequence}") + qread * 0x30 == slot_pointer and
             slices[1]["address"] == slot_pointer,
             f"PAD consume record {sequence} queue pointer/read index disagree")
    ports = [slot[index * 12:index * 12 + 11] for index in range(4)]
    p1, p2, p3, p4 = ports
    p1_neutral = not any(p1)
    buttons = _be16(p1, 0, f"PAD consume record {sequence}")
    p1_down_b = (buttons == BUTTON_B and p1[2] == 0 and p1[3] >= 0x80 and
                 not any(p1[4:]))
    inactive_neutral = all(not any(port[:10]) and port[10] == 0xFF for port in (p3, p4))
    _require((p1_down_b if p1_down_b else p1_neutral) and not any(p2) and inactive_neutral,
             f"PAD consume record {sequence} differs from its declared four-port source statuses")
    return p1_down_b


def validate_transform_prefix(stream_path: str | Path,
                              status_path: str | Path | None = None) -> dict[str, Any]:
    """Validate one completed 19→7 ownership prefix and return scoped evidence."""

    source = Path(stream_path)
    status_file = Path(status_path) if status_path is not None else Path(str(source) + ".status.json")
    try:
        status = read_status(status_file)
    except ObserverStreamError as error:
        raise TransformPrefixError(f"observer status sidecar is malformed: {error}") from error
    stats = ObserverStreamStats()
    header = start = end = None
    setup = None
    css_owner_sequence = None
    sss_owner_sequence = None
    samples: list[dict[str, Any]] = []
    active_ticks = 0
    down_b_edges = 0
    down_b_held = False
    down_b_consumed = False
    down_b_released = False
    action_seen = False
    grounded_neutral_ready = False
    neutral_pad_consume_sequence = None
    grounded_neutral_source_sequence = None
    down_b_consume_sequence = None
    previous_source_tick = None
    last_sequence = -1
    after_swap = None
    post_swap_neutral = None

    try:
        for row in iter_records(source, max_bytes=MAX_STREAM_BYTES,
                                max_records=MAX_STREAM_RECORDS, stats=stats):
            last_sequence = row["seq"]
            if post_swap_neutral is not None:
                _require(row["event"] == "end",
                         "observer continued emitting events after the completed Sheik neutral prefix")
            if row["event"] == "handshake":
                _require(header is None and start is None and setup is None,
                         "observer handshake is duplicated or reordered")
                header = row["payload"]
                _require(header.get("schema") == EXPECTED_OBSERVER_SCHEMA and
                         header.get("version") == 1 and header.get("writes_guest_memory") is False and
                         header.get("dolphin_commit") == EXPECTED_DOLPHIN_COMMIT and
                         header.get("dol_sha1") == EXPECTED_DOL_SHA1 and
                         header.get("dol_sha256") == EXPECTED_DOL_SHA256 and
                         header.get("cpu") == "JITARM64" and
                         header.get("diagnostic") == "sheik_transform_prefix" and
                         header.get("max_active_source_ticks") == MAX_ACTIVE_SOURCE_TICKS and
                         header.get("completion_boundary") == COMPLETION_BOUNDARY and
                         not header.get("whole_session"),
                         "observer handshake does not name the pinned passive Sheik-prefix source scope")
            elif row["event"] == "start":
                _require(header is not None and start is None,
                         "observer start is missing its handshake or duplicated")
                start = row["payload"]
                _require(start.get("status") == "recording" and
                         start.get("source_revision") == "GALE01r2" and
                         not start.get("whole_session") and
                         start.get("diagnostic") == "sheik_transform_prefix" and
                         start.get("max_active_source_ticks") == MAX_ACTIVE_SOURCE_TICKS and
                         start.get("completion_boundary") == COMPLETION_BOUNDARY,
                         "observer start does not name the bounded Sheik-prefix scope")
            elif row["event"] == "error":
                raise TransformPrefixError("observer stream contains an error: " +
                                           str(row["payload"].get("error")))
            elif row["event"] == "end":
                _require(end is None, "observer end record is duplicated")
                end = row["payload"]
            elif row["event"] == "boundary":
                payload = row["payload"]
                _require(header is not None and start is not None,
                         "observer boundary precedes diagnostic handshake/start")
                _require(not payload.get("whole_session"),
                         "transform-prefix stream unexpectedly entered whole-session mode")
                if payload["boundary"] == "pad_poll" and setup is None:
                    _require(payload["pc"] == PAD_POLL_PC,
                             "menu owner sample is not at the pinned original PadPoll boundary")
                    menu_owner = _menu_owner_pad_poll(payload)
                    if menu_owner == "css" and css_owner_sequence is None:
                        css_owner_sequence = row["seq"]
                    elif menu_owner == "sss":
                        _require(css_owner_sequence is not None and
                                 css_owner_sequence < row["seq"],
                                 "SSS live owner preceded the ordered CSS live-owner PadPoll")
                        if sss_owner_sequence is None:
                            sss_owner_sequence = row["seq"]
                if payload["boundary"] == "setup":
                    _require(payload["pc"] == SETUP_PC and setup is None and not samples,
                             "match setup is missing, duplicated, or not the first owner boundary")
                    _require(css_owner_sequence is not None and
                             sss_owner_sequence is not None and
                             css_owner_sequence < sss_owner_sequence < row["seq"],
                             "match setup preceded ordered live CSS and SSS owner PadPolls")
                    setup = _decode_owner_sample(payload, setup=True,
                                                 sequence=row["seq"], source_tick=row["source_tick"])
                    samples.append(setup)
                elif payload["boundary"] == "pad_consume" and setup is not None:
                    _require(payload["pc"] == PAD_CONSUME_PC,
                             "down+B sample is not at the pinned source PAD-consume boundary")
                    down_b = _validate_consumed_pad(payload, row["seq"])
                    if (not down_b and not down_b_held and not down_b_consumed and
                            neutral_pad_consume_sequence is None):
                        neutral_pad_consume_sequence = row["seq"]
                    if down_b and not down_b_held:
                        _require(grounded_neutral_ready and
                                 neutral_pad_consume_sequence is not None and
                                 grounded_neutral_source_sequence is not None and
                                 neutral_pad_consume_sequence < grounded_neutral_source_sequence <
                                 row["seq"],
                                 "source down+B preceded a consumed neutral PAD record and later grounded neutral Zelda source tick")
                        down_b_edges += 1
                        _require(down_b_edges == 1,
                                 "source input contains more than one consumed down+B episode")
                        down_b_consumed = True
                        down_b_consume_sequence = row["seq"]
                    if not down_b and down_b_held:
                        down_b_released = True
                    down_b_held = down_b
                elif payload["boundary"] == "source_tick" and setup is not None:
                    _require(post_swap_neutral is None,
                             "observer continued sampling after the completed Sheik neutral prefix")
                    _require(payload["pc"] == SOURCE_TICK_PC,
                             "owner sample is not at the pinned source-tick boundary")
                    _require(active_ticks < MAX_ACTIVE_SOURCE_TICKS,
                             "observer exceeded the 600 active source-tick cap")
                    if previous_source_tick is None:
                        _require(row["source_tick"] == 0,
                                 "first source-tick ownership sample is missing original tick zero")
                    else:
                        _require(row["source_tick"] == previous_source_tick + 1,
                                 "source-tick ownership samples contain a gap or reordering")
                    previous_source_tick = row["source_tick"]
                    active_ticks += 1
                    sample = _decode_owner_sample(payload, setup=False,
                                                  sequence=row["seq"],
                                                  source_tick=row["source_tick"])
                    initial_pointers = setup["local_pointer_checks"]
                    current_pointers = sample["local_pointer_checks"]
                    for identity_field in ("player_entities_hex_by_slot", "gobj_pointers",
                                           "fighter_pointers", "gobj_user_data_hex",
                                           "fighter_backlink_hex"):
                        _require(current_pointers[identity_field] == initial_pointers[identity_field],
                                 "StaticPlayer entity identity changed during the transform prefix")
                    active_kind = sample["portable"]["active_kind"]
                    if active_kind == ZELDA_KIND:
                        _require(sample["portable"]["active_entity_index"] == 0 and
                                 after_swap is None,
                                 "Zelda regained active ownership after the Sheik transition")
                        if (not down_b_consumed and not down_b_held and
                                neutral_pad_consume_sequence is not None and
                                neutral_pad_consume_sequence < row["seq"] and
                                not grounded_neutral_ready and
                                sample["portable"]["active_motion"] == NEUTRAL_MOTION and
                                sample["portable"]["active_ground_air"] == 0):
                            grounded_neutral_ready = True
                            grounded_neutral_source_sequence = row["seq"]
                        if sample["source_fields"]["motion_hex"][0] == f"{ZELDA_DOWN_B_MOTION:08x}":
                            _require(down_b_consumed,
                                     "Zelda down-B motion preceded consumed P1 down+B")
                            action_seen = True
                    elif active_kind == SHEIK_KIND:
                        _require(sample["portable"]["active_entity_index"] == 1 and action_seen and
                                 down_b_released,
                                 "dormant Sheik was not proven as the active post-down-B owner after neutral release")
                        if after_swap is None:
                            after_swap = sample
                        elif (sample["portable"]["active_motion"] == NEUTRAL_MOTION and
                              sample["portable"]["active_ground_air"] == 0):
                            post_swap_neutral = sample
                    else:
                        raise TransformPrefixError("active owner kind left Zelda/Sheik identities")
                    samples.append(sample)
                elif setup is not None and payload["boundary"] not in (
                        "pad_poll", "pad_consume", "source_tick"):
                    raise TransformPrefixError(
                        f"unexpected {payload['boundary']} boundary inside transform prefix")
            if end is not None:
                _require(row["event"] == "end", "observer record follows terminal end event")
    except ObserverStreamError as error:
        raise TransformPrefixError(f"observer stream is malformed: {error}") from error

    _require(header is not None and start is not None and end is not None,
             "observer stream lacks handshake/start/end records")
    _require(end.get("status") == "completed" and end.get("natural") is True and
             end.get("completion_boundary") == COMPLETION_BOUNDARY and
             end.get("match_complete") is False,
             "observer end record lacks the declared bounded prefix completion boundary")
    readiness_sequences = {
        "neutral_pad_consume": neutral_pad_consume_sequence,
        "grounded_neutral_source_tick": grounded_neutral_source_sequence,
        "down_b_consume": down_b_consume_sequence,
    }
    _require(all(value is not None for value in readiness_sequences.values()) and
             neutral_pad_consume_sequence < grounded_neutral_source_sequence <
             down_b_consume_sequence and
             end.get("readiness_source_sequence") == readiness_sequences,
             "observer end record does not preserve the ordered neutral-readiness source sequences")
    _require(status["state"] == "completed" and status["completed"] and
             not status["invalid"] and status["error"] is None,
             "observer status sidecar is not a clean completed capture")
    _require(status["event_count"] == stats.records_read and
             status["last_seq"] == last_sequence and
             status["source_tick"] == (previous_source_tick if previous_source_tick is not None
                                       else status["source_tick"]),
             "observer stream and terminal status disagree")
    _require(setup is not None and after_swap is not None and post_swap_neutral is not None and
             grounded_neutral_ready and action_seen and
             down_b_edges == 1 and down_b_released,
             "capture lacks grounded readiness, one down+B action/release, active 19→7 transition, or post-swap neutral tick")
    _require(active_ticks <= MAX_ACTIVE_SOURCE_TICKS and active_ticks >= 4,
             "transform prefix has an invalid source-tick count")
    _require(samples[0]["portable"]["active_kind"] == ZELDA_KIND and
             samples[0]["portable"]["fighter_kinds"] == [ZELDA_KIND, SHEIK_KIND] and
             after_swap["portable"]["active_kind"] == SHEIK_KIND and
             after_swap["portable"]["active_entity_index"] == 1,
             "dormant Sheik inventory was mistaken for an active-owner transition")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "status": "pass",
        "scope": "passive original Zelda/Sheik entity ownership prefix; no browser or port comparison; match completion not observed",
        "menu_owner_readiness": {
            "css_live_owner_pad_poll_sequence": css_owner_sequence,
            "sss_live_owner_pad_poll_sequence": sss_owner_sequence,
            "ordered_before_setup": True,
            "meaning": "mode-2 live source-owner slices after the observer's checked menu-return gates; does not compare CSS/SSS scalar state or prove consumed input",
        },
        "completion": {"boundary": COMPLETION_BOUNDARY,
                       "match_complete": False,
                       "post_swap_neutral_source_tick": post_swap_neutral},
        "source_stream": {"path": str(source), "bytes": stats.bytes_read,
                          "sha256": stats.prefix_sha256, "records": stats.records_read},
        "terminal_status": status,
        "source_provenance": {
            "observer_schema": EXPECTED_OBSERVER_SCHEMA,
            "observer_version": 1,
            "source_revision": start["source_revision"],
            "dolphin_commit": header["dolphin_commit"],
            "dol_sha1": header["dol_sha1"],
            "dol_sha256": header["dol_sha256"],
            "cpu": header["cpu"],
        },
        "setup": {"sha256": setup["setup_sha256"],
                  "portable_players": [{"port": 1, "character_kind": 18, "fighter_kinds": [19, 7]},
                                       {"port": 2, "character_kind": 8, "fighter_kinds": [0]}]},
        "input": {"p1_down_b_consumed": down_b_consumed,
                  "down_b_episodes": down_b_edges,
                  "neutral_release_consumed": down_b_released,
                  "readiness_source_sequence": readiness_sequences,
                  "p2_neutral": True},
        "active_source_ticks": active_ticks,
        "initial_owner": samples[0],
        "active_transition": {"before": next(sample for sample in samples
                                                   if sample["boundary"] == "source_tick" and
                                                   sample["portable"]["active_kind"] == ZELDA_KIND),
                              "after": after_swap},
        "portable_comparison_fields": {
            "entity_ordinals": [0, 1],
            "before_kinds": [19, 7],
            "before_active_index": 0,
            "after_kinds": [19, 7],
            "after_active_index": 1,
            "after_active_kind": 7,
        },
        "checks": {
            "ordered_live_css_sss_owner_polls_before_setup": True,
            "distinct_created_entities": True,
            "gobj_user_data_and_fighter_backlinks": True,
            "fighter_player_ids_and_kinds": True,
            "transformed_indexes_are_permutation": True,
            "consumed_down_b_precedes_motion_355": True,
            "neutral_release_precedes_active_sheik": True,
            "dormant_sheik_is_not_pass": True,
            "source_tick_cap": MAX_ACTIVE_SOURCE_TICKS,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("observer_stream", type=Path)
    parser.add_argument("--status", type=Path)
    args = parser.parse_args()
    try:
        report = validate_transform_prefix(args.observer_stream, args.status)
    except (OSError, TransformPrefixError) as error:
        parser.exit(2, f"Sheik transform prefix failed: {error}\n")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
