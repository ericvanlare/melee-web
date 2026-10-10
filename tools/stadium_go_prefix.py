"""Validate a named, passive original Stadium GO raw prefix.

This validator admits only the bounded CSS -> SSS -> ordinary VS setup -> GO
prefix. It preserves source boundaries and makes no port RNG-equality claim.
"""

from __future__ import annotations

import hashlib
import math
import struct
import re
from pathlib import Path
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
DIAGNOSTIC = "stadium_go_prefix_v3"
SCHEMA = "melee-web-original-stadium-go-prefix"
VERSION = 3
EXPECTED_OBSERVER_SCHEMA = "melee-web-passive-dolphin-observer"
EXPECTED_DOLPHIN_COMMIT = "c77bbaa0f372c3f72281602a8b087206706542cb"
EXPECTED_DOL_SHA1 = "08e0bf20134dfcb260699671004527b2d6bb1a45"
EXPECTED_DOL_SHA256 = "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646"
MAX_STREAM_BYTES = 128 * 1024 * 1024
MAX_STREAM_RECORDS = 250_000
MAX_SOURCE_TICK_BATCH = 5
MAX_TAIL_AFTER_F = MAX_SOURCE_TICK_BATCH - 1
EXPECTED_SETUP_RECEIPT_SHA256 = "e6b15cececf103efeb9b7df2dd18908e9a66d37ebde68622ecd304f8eabcfcda"

# One diagnostic-only, first-CSS source context from the retained original
# v6 stream. This is not a replay recipe or whole-session admission boundary.
FIRST_CSS_CONTEXT_ENV = "MELEE_WEB_STADIUM_FIRST_CSS_RAW"
FIRST_CSS_STREAM_BYTES = 4_397_889
FIRST_CSS_STREAM_SHA256 = "361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778"
FIRST_CSS_ENTRY_SEQUENCE = 704
FIRST_CSS_RETURN_SEQUENCE = 833
FIRST_CSS_ENTRY_PC = 0x8026688C
FIRST_CSS_RETURN_PC = 0x802669F0
FIRST_CSS_DATA_ADDRESS = 0x804807B0
FIRST_CSS_KO_ADDRESS = 0x804D6730
FIRST_CSS_PAD_ADDRESS = 0x804C1F84
FIRST_CSS_RNG_POINTER_ADDRESS = 0x804D5F94
FIRST_CSS_RNG_VALUE_ADDRESS = 0x804D5F90
FIRST_CSS_RULES_ADDRESS = 0x8045BF10
FIRST_CSS_SAVE_ADDRESS = 0x8045BF28
FIRST_CSS_SCENE_ADDRESS = 0x803DD9AC
FIRST_CSS_CONTEXT_MAGIC = b"STC1INPT"
FIRST_CSS_CONTEXT_VERSION = 1
FIRST_CSS_CONTEXT_HEADER_BYTES = 8 + 4 + 32 + 4 + 4
FIRST_CSS_CONTEXT_PAYLOAD_BYTES = 4 + 822 + 0x148 + 6 + 0x18 + 0x55E8
FIRST_CSS_CONTEXT_BYTES = (FIRST_CSS_CONTEXT_HEADER_BYTES +
                           FIRST_CSS_CONTEXT_PAYLOAD_BYTES)

CSS_ENTRY_PC = 0x8026688C
CSS_ENTRY_WORD = 0x7C0802A6
CSS_RETURN_PC = 0x802669F0
SSS_ENTRY_PC = 0x8025A998
SSS_ENTRY_WORD = 0x7C0802A6
SSS_RETURN_PC = 0x8025B84C
SSS_EXIT_PC = 0x8025BBD0
SSS_EXIT_WORD = 0x4E800020
VS_ENTRY_PC = 0x8016E934
VS_SETUP_PC = 0x8016E9C4
GO_CALL_PC = 0x8016B820
GO_CALL_WORD = 0x48068821
GO_AFTER_PC = 0x8016B824
GO_AFTER_WORD = 0x881F24C9
SOURCE_TICK_PC = 0x80390EB4
DRAW_RETURN_PC = 0x80391040
MATCH_SETUP_TAG = 4
PAD_QUEUE_TAG = 2
SCENE_ROUTING_TAG = 17
SCENE_ROUTING_ADDRESS = 0x80479D30
MENU_SSS_ROUTE_TAG = 35
RNG_POINTER_TAG = 19
RNG_VALUE_TAG = 20
STAGE_SELECT_INDEX_TAG = 41
STAGE_SELECT_KIND_TAG = 42
MENU_CSS_DOORS_TAG = 44
MENU_CSS_LIVE_STATE_TAG = 48
CSS_DOORS_ADDRESS = 0x803F0DFC
STAGE_SELECT_INDEX_ADDRESS = 0x804D6CAE
STAGE_SELECT_TABLE = 0x803F06D0
STAGE_SELECT_STRIDE = 0x1C
STAGE_SELECT_KIND_OFFSET = 0x0B
STADIUM_KIND = 3
START_MELEE_RULES_STKIND_OFFSET = 0x0E


class StadiumGoPrefixError(ValueError):
    """The bounded stream cannot prove the declared source prefix."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise StadiumGoPrefixError(message)


def _hex_address(value: Any, context: str) -> int:
    _require(isinstance(value, str) and re.fullmatch(r"0x[0-9a-f]{8}", value) is not None,
             f"{context} must be an eight-digit lowercase source address")
    return int(value, 16)


def _slice(record: dict[str, Any], tag: int, context: str,
           size: int | None = None, flags: int = 0) -> dict[str, Any]:
    slices = record.get("slices")
    _require(isinstance(slices, list), f"{context}: missing bounded slices")
    matches = [item for item in slices
               if isinstance(item, dict) and item.get("tag") == tag and
               item.get("flags") == flags]
    _require(len(matches) == 1, f"{context}: expected one tag={tag} flags={flags:#x}")
    item = matches[0]
    raw_hex = item.get("hex")
    _require(isinstance(raw_hex, str) and re.fullmatch(r"(?:[0-9a-f]{2})+", raw_hex),
             f"{context}: tag={tag} has malformed source bytes")
    raw = bytes.fromhex(raw_hex)
    _require(item.get("size") == len(raw) and (size is None or len(raw) == size),
             f"{context}: tag={tag} has an unexpected byte length")
    address = item.get("address")
    _require(type(address) is int and 0x80000000 <= address < 0x81800000 and
             address + len(raw) <= 0x81800000,
             f"{context}: tag={tag} escaped pinned source MEM1")
    return {**item, "raw": raw}


SSS_POSITION_PHASES = {"sss_position_cursor", "sss_position_target", "sss_position_end"}


class StadiumSssPositionObservations:
    """One original callback's observations; no cross-call geometry cache."""
    def __init__(self):
        self.active = None
        self.last_call = 0

    def accept(self, row, *, route_ready, expected_receipt=EXPECTED_SETUP_RECEIPT_SHA256):
        p = row["payload"]
        phase = p.get("phase")
        _require(route_ready and phase in SSS_POSITION_PHASES,
                 "SSS position is outside its accepted live source route")
        _require(p.get("diagnostic") == DIAGNOSTIC and
                 p.get("setup_receipt_sha256") == expected_receipt and
                 p.get("setup_profile_verified_by_observer") is False,
                 "SSS position has a foreign diagnostic/receipt")
        pcs = {"sss_position_cursor": (0x8025A4C0, 0x3C60803F),
               "sss_position_target": (0x8025A4E8, 0xC0410010),
               "sss_position_end": (0x8025A548, 0x8001003C)}
        pc, word = pcs[phase]
        _require(row.get("event") == "progress" and
                 _hex_address(p.get("pc"), "SSS position PC") == pc and
                 _hex_address(p.get("word"), "SSS position word") == word and
                 p.get("source_tick") == row.get("source_tick") and
                 p.get("draw_ordinal") == row.get("draw_ordinal"),
                 "SSS position header/PC/word differs")
        if phase != "sss_position_end":
            _require(_hex_address(p.get("lr"), "SSS position LR") == pc,
                     "SSS position is not the original transform return")
        keys = ("cursor_gobj", "cursor_jobj", "cursor_proc", "frame_sp")
        gobj, jobj, proc, sp = [_hex_address(p.get(k), "SSS position " + k) for k in keys]
        _require(all(0x80000000 <= v < 0x81800000 and v % 4 == 0 for v in (gobj,jobj,proc,sp)) and
                 _hex_address(p.get("argument"), "SSS position argument") == gobj,
                 "SSS position owner is outside source memory")
        call = p.get("position_call")
        cursor = p.get("cursor_observed")
        target = p.get("target_observed")
        _require(type(call) is int and 0 < call <= 0xFFFFFFFF and
                 type(cursor) is bool and type(target) is bool and (not target or cursor),
                 "SSS position call/observation flags differ")
        expected = {(17,0),(40,0),(61,0),(62,0),(63,0),(64,0),(64,1),(65,0),(66,0)}
        if cursor:
            expected.add((59,0))
        if phase == "sss_position_target":
            expected.add((60,0))
        items = p.get("slices")
        _require(isinstance(items,list) and len(items) == len(expected) and
                 {(x.get("tag"),x.get("flags")) for x in items if isinstance(x,dict)} == expected,
                 "SSS position has missing, duplicate or unauthored operands")
        def read(tag,size,address,flags=0):
            item = _slice(p,tag,"SSS position",size,flags)
            _require(item["address"] == address,"SSS position operand address differs")
            return item["raw"]
        route = read(17,6,SCENE_ROUTING_ADDRESS)
        scene = _slice(p,40,"SSS position scene",1)
        _require(route[0] == 2 and scene["raw"] == b"\x09", "SSS position lost ordinary SSS scene")
        owner = read(61,0x38,gobj)
        process = read(62,0x18,proc)
        target_row = read(63,0x1C,0x803F08C8)
        u32 = lambda raw,off=0: int.from_bytes(raw[off:off+4],"big")
        _require(u32(owner,0x28) == jobj and u32(process,0x10) == gobj and
                 u32(process,0x14) == 0x8025A310 and
                 u32(read(64,4,0x804D781C)) == gobj and
                 u32(read(64,4,0x804D7838,1)) == proc,
                 "SSS position lacks its exact current scheduler/cursor owner")
        target_jobj = u32(target_row)
        _require(0x80000000 <= target_jobj <= 0x817FFFBC and target_jobj % 4 == 0 and
                 target_row[8] in (1,2) and target_row[11] == 3 and
                 target_row[12:20] == bytes.fromhex("40466666402ccccd"),
                 "SSS position target18 owner/kind/authored extents differ")
        axes = read(65,4,0x804D6CAC)
        entry_state = p.get("entry_cursor_state")
        end_index = p.get("end_row_index")
        _require(type(entry_state) is int and entry_state in (0,1,2) and
                 axes[2] <= 30 and axes[3] == entry_state and
                 (not cursor or entry_state == 0),
                 "SSS position cursor control state differs from its entry")
        if phase == "sss_position_end":
            if cursor:
                _require(type(end_index) is int and 0 <= end_index <= 30 and
                         _hex_address(p.get("lr"),"SSS position epilogue LR") == 0x8025A4E8 and
                         (end_index == 30 or axes[2] == end_index) and
                         ((target and end_index >= 18) or (not target and end_index < 18)),
                         "SSS position target absence lacks its source earlier-row hit")
            else:
                _require(entry_state != 0 and end_index is None and
                         _hex_address(p.get("lr"),"SSS hidden cursor LR") == 0x8025A340,
                         "SSS normal-path end missed the cursor transform")
        else:
            _require(end_index is None,"SSS non-end observation has an epilogue row")
        local = read(66,12,jobj+0x38)
        world = read(59,12,sp+0x1C) if cursor else None
        target_world = read(60,12,sp+0x10) if phase == "sss_position_target" else None
        for raw in (local,world,target_world):
            _require(raw is None or all(math.isfinite(v) for v in struct.unpack(">3f",raw)),
                     "SSS position contains non-finite source float bits")
        identity = (call,gobj,jobj,proc,sp,target_jobj,scene["address"],target_row,entry_state)
        completed = None
        if phase == "sss_position_cursor":
            _require(self.active is None and call == self.last_call + 1 and cursor and not target,
                     "SSS cursor observation is duplicate/reordered")
            self.active = (identity,world,False,None)
        elif phase == "sss_position_target":
            _require(self.active is not None and self.active[0] == identity and
                     self.active[1] == world and not self.active[2] and cursor and target,
                     "SSS target sample is stale, foreign or not from this cursor call")
            self.active = (identity,world,True,target_world)
        else:
            if cursor:
                _require(self.active is not None and self.active[0] == identity and
                         self.active[1] == world and self.active[2] == target,
                         "SSS position end disagrees with the bound call")
            else:
                _require(self.active is None and call == self.last_call + 1 and not target,
                         "SSS hidden cursor end overlaps another call")
            if cursor and target:
                completed = {"call":call, "identity":identity,
                             "cursor_world_hex":world.hex(),
                             "target_world_hex":self.active[3].hex(),
                             "extent_hex":target_row[12:20].hex()}
            self.last_call = call
            self.active = None
        return {"call":call,"complete_tuple":phase == "sss_position_target",
                "cursor_world_hex":world.hex() if world is not None else None,
                "target_world_hex":target_world.hex() if target_world is not None else None,
                "completed_tuple":completed}

    def require_closed(self):
        _require(self.active is None,"SSS position callback did not reach its original epilogue")


def _check_boundary_contract(payload: dict[str, Any], *,
                             allow_missing_scene: bool = False) -> None:
    boundary = payload.get("boundary")
    expected: dict[str, tuple[int, ...]] = {
        "pad_poll": (1, 2, 17, 21, 27, 40),
        "pad_consume": (2, 3),
        "fighter_create": (24, 5),
        "entry": (MATCH_SETUP_TAG,),
        "setup": (MATCH_SETUP_TAG,),
        "source_tick": (PAD_QUEUE_TAG,),
        "draw_enter": (PAD_QUEUE_TAG,),
        "draw_return": (PAD_QUEUE_TAG,),
    }
    required_sizes = {
        1: 0x30, 2: 0xC, 3: 0x30, 4: 0x138, 5: 0x100, 17: 6,
        21: 0x358, 24: 0x30, 27: 4, 40: 1,
    }
    _require(boundary in expected, f"unexpected source boundary {boundary!r}")
    for tag in expected[boundary]:
        matches = [item for item in payload.get("slices", [])
                   if isinstance(item, dict) and item.get("tag") == tag]
        if (boundary == "pad_poll" and tag == 40 and allow_missing_scene and
                not matches):
            continue
        _require(matches, f"{boundary}: required source tag={tag} is missing")
        for item in matches:
            _slice(payload, tag, f"{boundary} boundary", required_sizes[tag],
                   item.get("flags"))


def _classify_stadium_sss_owner(payload: dict[str, Any], *,
                                allow_missing: bool = False) -> str | None:
    """Validate authored SSS stage rows, including Random's absent kind slice."""
    scene = _slice(payload, 40, "SSS owner PadPoll", 1)
    routing = _slice(payload, SCENE_ROUTING_TAG, "SSS owner PadPoll", 6)
    _require(routing["address"] == SCENE_ROUTING_ADDRESS,
             "SSS owner PadPoll routing escaped its pinned source address")
    if scene["raw"][0] != 9 or routing["raw"][0] != 0x02:
        return None

    slices = payload.get("slices")
    _require(isinstance(slices, list), "SSS owner PadPoll: missing bounded slices")
    index_rows = [item for item in slices
                  if isinstance(item, dict) and item.get("tag") == STAGE_SELECT_INDEX_TAG]
    kind_rows = [item for item in slices
                 if isinstance(item, dict) and item.get("tag") == STAGE_SELECT_KIND_TAG]
    _require(len(index_rows) <= 1 and len(kind_rows) <= 1,
             "SSS PadPoll contains duplicate or ambiguous owner slices")
    _require(not kind_rows or index_rows,
             "SSS PadPoll exposes a stage kind without its owner index")
    if not index_rows:
        _require(allow_missing, "SSS live-owner PadPoll: expected one tag=41 flags=0")
        return None

    index = _slice(payload, STAGE_SELECT_INDEX_TAG, "SSS live-owner PadPoll", 1)
    stage_index = index["raw"][0]
    _require(index["address"] == STAGE_SELECT_INDEX_ADDRESS and stage_index <= 30,
             "SSS stage owner index escaped its authored table")
    if stage_index == 30:
        _require(not kind_rows,
                 "SSS random row unexpectedly has a stage owner")
        return "sss_navigation"

    kind = _slice(payload, STAGE_SELECT_KIND_TAG, "SSS live-owner PadPoll", 1)
    expected_address = (STAGE_SELECT_TABLE + stage_index * STAGE_SELECT_STRIDE +
                        STAGE_SELECT_KIND_OFFSET)
    _require(kind["address"] == expected_address,
             "SSS selected stage kind escaped its authored table row")
    return ("sss" if stage_index == 18 and kind["raw"][0] == STADIUM_KIND
            else "sss_navigation")


def _validate_menu_pad_poll(payload: dict[str, Any], sequence: int,
                            css_return_sequence: int | None,
                            sss_return_sequence: int | None,
                            css_owner_sequence: int | None) -> tuple[str | None, int | None]:
    scene = _slice(payload, 40, "menu PadPoll", 1)
    routing = _slice(payload, SCENE_ROUTING_TAG, "menu PadPoll", 6)
    if routing["raw"][0] != 0x02:
        return None, css_owner_sequence
    if scene["raw"][0] == 8:
        # OnEnter can poll PAD before publishing CSS globals. Preserve those
        # ordinary rows, but only call later polls live CSS-owner evidence.
        if css_return_sequence is None or sequence < css_return_sequence:
            _require(not any(isinstance(item, dict) and item.get("tag") in
                             (MENU_CSS_LIVE_STATE_TAG, MENU_CSS_DOORS_TAG)
                             for item in payload.get("slices", [])),
                     "CSS owner slices appeared before the verified OnEnter return")
            return None, css_owner_sequence
        live = _slice(payload, MENU_CSS_LIVE_STATE_TAG, "CSS live-owner PadPoll", 0x148)
        doors = _slice(payload, MENU_CSS_DOORS_TAG, "CSS live-owner PadPoll", 0x90)
        _require(doors["address"] == CSS_DOORS_ADDRESS and
                 0x80000000 <= live["address"] < 0x81800000,
                 "CSS menu owner escaped its pinned source objects")
        return "css", sequence if css_owner_sequence is None else css_owner_sequence
    if scene["raw"][0] == 9:
        # As with CSS, SSS globals are not accepted until its original
        # OnEnter has returned. Earlier PAD rows stay in the raw stream.
        if sss_return_sequence is None or sequence < sss_return_sequence:
            _require(not any(isinstance(item, dict) and item.get("tag") in
                             (STAGE_SELECT_INDEX_TAG, STAGE_SELECT_KIND_TAG)
                             for item in payload.get("slices", [])),
                     "SSS owner slices appeared before the verified OnEnter return")
            return None, css_owner_sequence
        _require(css_owner_sequence is not None and css_owner_sequence < sequence,
                 "SSS live owner preceded verified live CSS ownership")
        # Random (index 30) is an authored navigation row without a kind byte.
        # Only the exact Stadium row may qualify SSS exit and GO.
        return _classify_stadium_sss_owner(payload), css_owner_sequence
    return None, css_owner_sequence


def _validate_progress(row: dict[str, Any], phase: str,
                       receipt_sha256: str) -> dict[str, Any]:
    payload = row["payload"]
    _require(payload.get("diagnostic") == DIAGNOSTIC and
             payload.get("phase") == phase and
             payload.get("setup_receipt_sha256") == receipt_sha256 and
             payload.get("setup_profile_verified_by_observer") is False,
             f"{phase}: named Progress identity or phase changed")
    pc = _hex_address(payload.get("pc"), f"{phase}.pc")
    word = payload.get("word")
    _require(isinstance(word, str) and re.fullmatch(r"0x[0-9a-f]{8}", word),
             f"{phase}: instruction word is malformed")
    _require(type(payload.get("source_tick")) is int and
             row["source_tick"] == payload["source_tick"],
             f"{phase}: event header source tick disagrees with its named payload")
    argument = _hex_address(payload.get("argument"), f"{phase}.argument")
    if phase in ("css_entry", "css_return", "sss_entry", "sss_return", "sss_exit"):
        routing = _slice(payload, SCENE_ROUTING_TAG, phase, 6)
        _require(routing["raw"][0] == 0x02,
                 f"{phase}: original current mode is not ordinary VS")
    if phase == "css_entry":
        _require(pc == CSS_ENTRY_PC and word == "0x7c0802a6",
                 "CSS entry is not the pinned GALE01r2 source entry")
        for tag, length in ((31, 0xF0), (33, 0x40), (34, 4), (38, 0x18),
                            (39, 0x55E8), (50, 0x148), (51, 6), (21, 0x358),
                            (RNG_POINTER_TAG, 4), (RNG_VALUE_TAG, 4)):
            item = _slice(payload, tag, phase, length)
            if tag == 50:
                _require(argument != 0 and item["address"] == argument,
                         "CSS entry context does not point at the source GPR3 owner")
    elif phase == "css_return":
        _require(pc == CSS_RETURN_PC and word == "0x4e800020",
                 "CSS readiness is not the verified OnEnter return")
        _slice(payload, RNG_POINTER_TAG, phase, 4)
        _slice(payload, RNG_VALUE_TAG, phase, 4)
    elif phase == "sss_entry":
        _require(pc == SSS_ENTRY_PC and word == "0x7c0802a6",
                 "SSS entry is not the pinned GALE01r2 source entry")
        state = _slice(payload, 32, phase, 0xF0)
        route = _slice(payload, MENU_SSS_ROUTE_TAG, phase, 1)
        _require(argument != 0 and state["address"] == argument + 0x10 and
                 route["address"] == argument + 4,
                 "SSS entry slices do not point at the source GPR3 owner")
    elif phase == "sss_return":
        _require(pc == SSS_RETURN_PC and word == "0x4e800020",
                 "SSS readiness is not the verified OnEnter return")
        _slice(payload, 32, phase, 0xF0)
        _slice(payload, MENU_SSS_ROUTE_TAG, phase, 1)
    elif phase == "sss_exit":
        _require(pc == SSS_EXIT_PC and word == "0x4e800020",
                 "SSS exit is not the pinned original source boundary")
        route = _slice(payload, MENU_SSS_ROUTE_TAG, phase, 1)
        _require(route["raw"][0] != 0,
                 "SSS route did not select the original match")
    elif phase == "go_after":
        _require(pc == GO_AFTER_PC and word == "0x881f24c9" and
                 payload.get("callsite_pc") == "0x8016b820" and
                 payload.get("callsite_word") == "0x48068821" and
                 payload.get("lr") == "0x8016b824",
                 "GO marker is not the pinned post-call return")
        match_setup = _slice(payload, MATCH_SETUP_TAG, phase, 0x138)
        _validate_mario_roster(match_setup["raw"], phase)
        _slice(payload, PAD_QUEUE_TAG, phase, 0xC)
        routing = _slice(payload, SCENE_ROUTING_TAG, phase, 6)
        _require(routing["raw"][0] == 0x02,
                 "GO marker is outside the active ordinary VS routing state")
        _slice(payload, RNG_POINTER_TAG, phase, 4)
        _slice(payload, RNG_VALUE_TAG, phase, 4)
    return payload


def _decode_setup(payload: dict[str, Any]) -> dict[str, Any]:
    match_setup = _slice(payload, MATCH_SETUP_TAG, "VS setup", 0x138)
    _validate_mario_roster(match_setup["raw"], "VS setup")
    return {"setup_sha256": hashlib.sha256(match_setup["raw"]).hexdigest(),
            "raw_setup_bytes": len(match_setup["raw"]),
            "phase_mapping": "pending_original_setup_observation"}


def _validate_mario_roster(raw: bytes, context: str) -> None:
    _require(len(raw) == 0x138, f"{context}: source StartMeleeData has the wrong size")
    stage_kind = int.from_bytes(raw[START_MELEE_RULES_STKIND_OFFSET:
                                    START_MELEE_RULES_STKIND_OFFSET + 2], "big")
    _require(stage_kind == STADIUM_KIND,
             f"{context}: StartMeleeRules.stkind is not source Stadium kind 3")
    for slot in range(2):
        row = 0x60 + slot * 0x24
        _require((raw[row], raw[row + 1], raw[row + 2]) == (8, 0, 4),
                 f"{context}: port {slot} is not the observed human Mario/four-stock row")
    for slot in range(2, 6):
        row = 0x60 + slot * 0x24
        _require(raw[row + 1] == 3,
                 f"{context}: source port {slot} is unexpectedly active")


def validate_stadium_go_prefix(stream_path: str | Path,
                               expected_setup_receipt_sha256: str = EXPECTED_SETUP_RECEIPT_SHA256,
                               status_path: str | Path | None = None) -> dict[str, Any]:
    """Admit one natural C→F→DrawReturn raw prefix and retain source identities."""

    _require(isinstance(expected_setup_receipt_sha256, str) and
             re.fullmatch(r"[0-9a-f]{64}", expected_setup_receipt_sha256) is not None and
             expected_setup_receipt_sha256 != "0" * 64,
             "expected setup receipt identity must be a lowercase SHA-256")
    source = Path(stream_path)
    status_file = Path(status_path) if status_path is not None else Path(str(source) + ".status.json")
    try:
        status = read_status(status_file)
    except ObserverStreamError as error:
        raise StadiumGoPrefixError(f"observer status is malformed: {error}") from error

    stats = ObserverStreamStats()
    header = start = end = None
    phases = []
    phase_payloads: dict[str, dict[str, Any]] = {}
    entry_setup_sha256 = None
    setup_result = None
    go_seen = False
    css_owner_sequence = None
    sss_owner_sequence = None
    sss_selected_stadium = False
    sss_navigation_poll_count = 0
    menu_pad_poll_count = 0
    menu_pad_consume_count = 0
    c_tick = None
    f_tick = None
    final_draw_sequence = None
    source_ticks_since_draw = 0
    batch_at_go = None
    tail_after_f = 0
    last_source_tick = None
    previous_draw_sequence = None
    final_tail_after_f = None
    setup_count = 0
    vs_entry_count = 0
    css_return_sequence = None
    sss_return_sequence = None
    raw_rows = 0
    last_sequence = -1
    sss_positions = StadiumSssPositionObservations()
    expected_phases = ["css_entry", "css_return", "sss_entry", "sss_return",
                       "sss_exit", "go_after"]

    try:
        for row in iter_records(source, max_bytes=MAX_STREAM_BYTES,
                                max_records=MAX_STREAM_RECORDS, stats=stats):
            last_sequence = row["seq"]
            if final_draw_sequence is not None:
                _require(row["event"] == "end",
                         "observer emitted rows after the first qualifying DrawReturn")
            if row["event"] == "handshake":
                _require(header is None and start is None,
                         "observer handshake is duplicated or reordered")
                header = row["payload"]
                _require(header.get("schema") == EXPECTED_OBSERVER_SCHEMA and
                         header.get("version") == 1 and
                         header.get("writes_guest_memory") is False and
                         header.get("dolphin_commit") == EXPECTED_DOLPHIN_COMMIT and
                         header.get("dol_sha1") == EXPECTED_DOL_SHA1 and
                         header.get("dol_sha256") == EXPECTED_DOL_SHA256 and
                         header.get("cpu") == "JITARM64" and
                         header.get("diagnostic") == DIAGNOSTIC and
                         header.get("setup_receipt_sha256") == expected_setup_receipt_sha256 and
                         header.get("setup_profile_verified_by_observer") is False and
                         not header.get("whole_session"),
                         "observer handshake does not name the pinned passive Stadium prefix")
            elif row["event"] == "start":
                _require(header is not None and start is None,
                         "observer start is missing its handshake or duplicated")
                start = row["payload"]
                _require(start.get("status") == "recording" and
                         start.get("source_revision") == "GALE01r2" and
                         start.get("diagnostic") == DIAGNOSTIC and
                         start.get("setup_receipt_sha256") == expected_setup_receipt_sha256 and
                         start.get("setup_profile_verified_by_observer") is False and
                         not start.get("whole_session"),
                         "observer start does not name the bounded Stadium prefix scope")
            elif row["event"] == "error":
                raise StadiumGoPrefixError("observer stream contains an error: " +
                                           str(row["payload"].get("error")))
            elif row["event"] == "progress":
                _require(header is not None and start is not None and not go_seen,
                         "named setup Progress is outside the pre-GO source setup")
                payload = row["payload"]
                phase = payload.get("phase")
                if phase in SSS_POSITION_PHASES:
                    sss_positions.accept(row, route_ready=phases == expected_phases[:4],
                                         expected_receipt=expected_setup_receipt_sha256)
                    continue
                if phase == "sss_exit":
                    sss_positions.require_closed()
                if phase == "go_after":
                    _require(phases == expected_phases[:-1] and setup_result is not None and
                             vs_entry_count == 1 and setup_count == 1 and
                             css_owner_sequence is not None and sss_selected_stadium and
                             sss_owner_sequence is not None and menu_pad_poll_count > 0 and
                             menu_pad_consume_count > 0,
                             "GO marker preceded checked CSS/SSS and ordinary VS setup")
                    phase_payloads["go_after"] = _validate_progress(
                        row, "go_after", expected_setup_receipt_sha256)
                    _require(batch_at_go is None and previous_draw_sequence is not None and
                             source_ticks_since_draw < MAX_SOURCE_TICK_BATCH,
                             "GO batch did not begin at an observed DrawReturn or lacks capacity")
                    batch_at_go = source_ticks_since_draw
                    go_seen = True
                    last_source_tick = None
                    phases.append("go_after")
                else:
                    _require(phase in expected_phases[:-1] and
                             len(phases) < 5 and phase == expected_phases[len(phases)],
                             "CSS/SSS setup Progress phase is missing, duplicated, or reordered")
                    if phase == "sss_entry":
                        _require(css_owner_sequence is not None and
                                 css_owner_sequence < row["seq"],
                                 "SSS entry preceded the verified live CSS owner")
                    if phase == "sss_exit":
                        _require(sss_selected_stadium and sss_owner_sequence is not None and
                                 sss_owner_sequence < row["seq"],
                                 "SSS exit did not retain Stadium as the current selected owner")
                    phase_payloads[phase] = _validate_progress(
                        row, phase, expected_setup_receipt_sha256)
                    phases.append(phase)
                    if phase == "css_return":
                        css_return_sequence = row["seq"]
                    elif phase == "sss_return":
                        sss_return_sequence = row["seq"]
            elif row["event"] == "boundary":
                _require(header is not None and start is not None,
                         "source boundary preceded observer handshake/start")
                payload = row["payload"]
                _require(not payload.get("whole_session"),
                         "GO-prefix stream unexpectedly entered whole-session mode")
                boundary = payload.get("boundary")
                # Before the accepted first CSS entry only original boot PAD
                # observations are admitted; none qualify a match or menu owner.
                if not phases:
                    _require(boundary in ("pad_poll", "pad_consume"),
                             "pre-CSS source boundary is not boot PAD")
                    _require(not any(item.get("tag") in
                                     (MATCH_SETUP_TAG, 41, 42, 43, 44, 47, 48, 50, 51)
                                     for item in payload.get("slices", [])),
                             "pre-CSS PAD exposed a premature live CSS/SSS owner")
                raw_rows += 1
                _check_boundary_contract(
                    payload, allow_missing_scene=(not phases and boundary == "pad_poll"))
                if boundary == "pad_poll" and not go_seen:
                    _require(payload.get("pc") == 0x8034DD8C,
                             "menu PAD polling is not at the pinned original HSD poll")
                    scene_observed = any(
                        isinstance(item, dict) and item.get("tag") == 40
                        for item in payload.get("slices", []))
                    if not phases and not scene_observed:
                        _slice(payload, SCENE_ROUTING_TAG, "boot PAD routing", 6)
                        owner = None
                    else:
                        owner, css_owner_sequence = _validate_menu_pad_poll(
                            payload, row["seq"], css_return_sequence,
                            sss_return_sequence, css_owner_sequence)
                    menu_pad_poll_count += 1
                    if owner in ("sss", "sss_navigation"):
                        sss_selected_stadium = owner == "sss"
                        sss_owner_sequence = row["seq"] if sss_selected_stadium else None
                        if owner == "sss_navigation":
                            sss_navigation_poll_count += 1
                elif boundary == "pad_consume" and not go_seen:
                    _require(payload.get("pc") == 0x80377584,
                             "menu PAD consumption is not at the pinned original queue consumer")
                    menu_pad_consume_count += 1
                if boundary == "entry":
                    vs_entry_count += 1
                    _require(vs_entry_count == 1 and phases[-1] == "sss_exit" and
                             sss_selected_stadium and payload.get("pc") == VS_ENTRY_PC,
                             "ordinary VS Entry is missing, duplicated, or out of route order")
                    entry_setup = _slice(payload, MATCH_SETUP_TAG, "VS Entry", 0x138)
                    entry_setup_sha256 = hashlib.sha256(entry_setup["raw"]).hexdigest()
                elif boundary == "setup":
                    setup_count += 1
                    _require(setup_count == 1 and vs_entry_count == 1 and
                             payload.get("pc") == VS_SETUP_PC,
                             "ordinary VS Setup is missing, duplicated, or out of route order")
                    setup_result = _decode_setup(payload)
                elif boundary == "source_tick":
                    _require(payload.get("pc") == SOURCE_TICK_PC,
                             "source tick does not use the pinned post-scheduler PC")
                    _slice(payload, PAD_QUEUE_TAG, "SourceTick", 0xC)
                    tick = payload.get("source_tick")
                    _require(type(tick) is int, "source tick counter is not an integer")
                    source_ticks_since_draw += 1
                    _require(source_ticks_since_draw <= MAX_SOURCE_TICK_BATCH,
                             "one natural DrawReturn batch exceeded five source ticks")
                    if go_seen and c_tick is None:
                        _require(previous_draw_sequence is not None,
                                 "GO-containing batch lacks its preceding natural DrawReturn")
                        _require(tick == phase_payloads["go_after"]["source_tick"],
                                 "containing tick C does not match the checked GO counter")
                        c_tick = tick
                        last_source_tick = tick
                    elif go_seen and c_tick is not None and f_tick is None:
                        _require(tick == c_tick + 1,
                                 "full post-GO tick F is not the next contiguous source tick")
                        f_tick = tick
                        last_source_tick = tick
                    elif go_seen and f_tick is not None:
                        _require(last_source_tick is not None and tick == last_source_tick + 1,
                                 "source tick after F is not contiguous")
                        last_source_tick = tick
                        tail_after_f += 1
                        _require(tail_after_f <= MAX_TAIL_AFTER_F,
                                 "first qualifying DrawReturn exceeded the four-tick tail bound")
                elif boundary == "draw_return":
                    _require(payload.get("pc") == DRAW_RETURN_PC,
                             "DrawReturn does not use the pinned natural queue close")
                    _slice(payload, PAD_QUEUE_TAG, "DrawReturn", 0xC)
                    _require(not (go_seen and c_tick is None),
                             "DrawReturn preceded containing tick C")
                    previous_draw_sequence = row["seq"]
                    if go_seen and f_tick is not None:
                        _require(source_ticks_since_draw <= MAX_SOURCE_TICK_BATCH and
                                 tail_after_f <= MAX_TAIL_AFTER_F,
                                 "qualifying DrawReturn exceeded the authored batch bound")
                        final_draw_sequence = row["seq"]
                        final_tail_after_f = tail_after_f
                    source_ticks_since_draw = 0
                    if final_draw_sequence is None:
                        tail_after_f = 0
                elif boundary in ("pad_poll", "pad_consume", "fighter_create", "draw_enter"):
                    # Preserve these real source boundaries; their original
                    # event kind and descriptor remain decoded by MWRO v1.
                    pass
                else:
                    raise StadiumGoPrefixError(f"unexpected source boundary {boundary!r}")
            elif row["event"] == "end":
                _require(end is None, "observer end record is duplicated")
                end = row["payload"]

    except ObserverStreamError as error:
        raise StadiumGoPrefixError(f"observer stream is malformed: {error}") from error

    _require(header is not None and start is not None and end is not None,
             "observer stream is missing handshake, start, or natural end")
    _require(phases == expected_phases and go_seen and setup_result is not None,
             "observer stream did not complete the exact CSS/SSS/VS/GO phase chain")
    _require(c_tick is not None and f_tick == c_tick + 1 and final_draw_sequence is not None,
             "observer stream lacks containing C, next full F, or finishing DrawReturn")
    _require(end.get("status") == "completed" and end.get("natural") is True and
             end.get("diagnostic") == DIAGNOSTIC and
             end.get("setup_receipt_sha256") == expected_setup_receipt_sha256 and
             end.get("setup_profile_verified_by_observer") is False and
             end.get("whole_session_equivalent") is False and
             end.get("finishing_draw_return_seen") is True,
             "observer end is not the natural bounded Stadium prefix completion")
    _require(type(end.get("tail_ticks_before_draw_return")) is int and
             end["tail_ticks_before_draw_return"] == final_tail_after_f and
             0 <= final_tail_after_f <= MAX_TAIL_AFTER_F,
             "observer end tail accounting disagrees with the retained batch rows")
    _require(status.get("state") == "completed" and status.get("completed") is True and
             status.get("invalid") is False and status.get("error") is None and
             status.get("last_seq") == last_sequence and
             status.get("event_count") == stats.records_read,
             "observer status does not account for the complete checked stream")
    go_setup = _slice(phase_payloads["go_after"], MATCH_SETUP_TAG, "GO marker", 0x138)
    go_setup_sha256 = hashlib.sha256(go_setup["raw"]).hexdigest()
    rng_snapshots = {}
    for phase in ("css_entry", "css_return", "sss_entry", "sss_return", "sss_exit"):
        if phase not in phase_payloads:
            continue
        rng_pointer = _slice(phase_payloads[phase], RNG_POINTER_TAG, phase, 4)
        rng_value = _slice(phase_payloads[phase], RNG_VALUE_TAG, phase, 4)
        rng_snapshots[phase] = {
            "pointer_hex": rng_pointer["raw"].hex(),
            "value_hex": rng_value["raw"].hex(),
        }
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "decision": "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY",
        "source": {"dolphin_commit": EXPECTED_DOLPHIN_COMMIT,
                   "dol_sha1": EXPECTED_DOL_SHA1,
                   "dol_sha256": EXPECTED_DOL_SHA256},
        "setup_receipt_sha256": expected_setup_receipt_sha256,
        "setup_profile_verified_by_observer": False,
        "source_setup_phase_raw_sha256": {
            "vs_entry": entry_setup_sha256,
            "vs_setup": setup_result["setup_sha256"],
            "go_after": go_setup_sha256,
        },
        "setup_phase_mapping": setup_result["phase_mapping"],
        "rng_pointer_value_snapshots": rng_snapshots,
        "go_source_tick_C": c_tick,
        "first_full_post_go_source_tick_F": f_tick,
        "tail_ticks_after_F": final_tail_after_f,
        "source_rows_retained": raw_rows,
        "menu_pad_poll_rows": menu_pad_poll_count,
        "menu_pad_consume_rows": menu_pad_consume_count,
        "css_owner_sequence": css_owner_sequence,
        "sss_owner_sequence": sss_owner_sequence,
        "sss_navigation_poll_rows": sss_navigation_poll_count,
        "sss_selected_stadium_at_exit": sss_selected_stadium,
        "draw_return_sequence": final_draw_sequence,
        "batch_ticks_observed_before_GO": batch_at_go,
        "whole_session_equivalent": False,
        "rng_equality": "not_compared",
        "stream_sha256": stats.prefix_sha256,
        "stream_bytes": stats.bytes_read,
        "stream_records": stats.records_read,
    }


_FIRST_CSS_ENTRY_SLICES = {
    (31, 0), (33, 0), (34, 0), (38, 0), (39, 0), (36, 0), (37, 0),
    (21, 0), (17, 0), (30, 0), (40, 0), (19, 0), (20, 0), (50, 0),
    (51, 0),
}
_FIRST_CSS_RETURN_SLICES = {
    (31, 0), (33, 0), (34, 0), (36, 0), (37, 0), (21, 0), (17, 0),
    (30, 0), (40, 0), (19, 0), (20, 0), (50, 0), (51, 0),
}


def _first_css_slice_inventory(payload: dict[str, Any], phase: str) -> None:
    items = payload.get("slices")
    expected = (_FIRST_CSS_ENTRY_SLICES if phase == "css_entry"
                else _FIRST_CSS_RETURN_SLICES)
    _require(isinstance(items, list) and
             all(isinstance(item, dict) for item in items) and
             len(items) == len(expected) and
             {(item.get("tag"), item.get("flags")) for item in items} == expected,
             f"{phase}: first-CSS witness has an unexpected slice inventory")


def _first_css_semantic_start(start: bytes) -> dict[str, Any]:
    """Decode defined source StartMeleeData fields, omitting ABI padding."""
    _require(len(start) == 0x138, "first-CSS StartMeleeData must be exactly 0x138 bytes")
    rules = start[:0x60]
    _require(not any(rules[0x38:0x5C]),
             "first-CSS StartMeleeRules contains an unsupported callback/data pointer")

    def bits(byte: int, index: int) -> int:
        return (byte >> (7 - index)) & 1

    rule_values: dict[str, Any] = {
        "match_kind": rules[0] >> 5,
        "x0_3": (rules[0] >> 2) & 7,
        "timer_enabled": bits(rules[0], 6),
        "timer_counts_up": bits(rules[0], 7),
    }
    for index in range(6):
        rule_values[f"x1_{index}"] = bits(rules[1], index)
    rule_values.update(timer_shows_hours=bits(rules[1], 6),
                       friendly_fire=bits(rules[1], 7))
    for name, index in (("is_stock", 0), ("x2_1", 1), ("x2_2", 2),
                        ("single_button", 3), ("disable_pausing", 4),
                        ("x2_5", 5), ("x2_6", 6), ("x2_7", 7)):
        rule_values[name] = bits(rules[2], index)
    for index in range(8):
        rule_values[f"x3_{index}"] = bits(rules[3], index)
    for name, index in (("x4_0", 0), ("is_vs", 1), ("x4_2", 2),
                        ("x4_3", 3), ("x4_4", 4), ("x4_5", 5),
                        ("x4_6", 6), ("x4_7", 7)):
        rule_values[name] = bits(rules[4], index)
    for index in range(8):
        rule_values[f"x5_{index}"] = bits(rules[5], index)
    rule_values.update({
        "x6": rules[6], "x7": rules[7], "is_teams": rules[8],
        "x9": rules[9], "xA": rules[10],
        "xB": int.from_bytes(rules[11:12], "big", signed=True),
        "xC": int.from_bytes(rules[12:13], "big", signed=True),
        "xD": rules[13], "stage_kind": int.from_bytes(rules[14:16], "big"),
        "time_limit": int.from_bytes(rules[16:20], "big"), "x14": rules[20],
        "x18": int.from_bytes(rules[24:28], "big"),
        "x1C_pad": [int.from_bytes(rules[28:32], "big")],
        "item_mask": f"{int.from_bytes(rules[32:40], 'big'):016x}",
        "x28": int.from_bytes(rules[40:44], "big", signed=True),
        "x2C_bits": rules[44:48].hex(), "damage_ratio_bits": rules[48:52].hex(),
        "game_speed_bits": rules[52:56].hex(),
        "on_unpause_override": None, "on_pause_override": None,
        "check_for_pauser_override": None, "on_match_start": None,
        "on_frame_start": None, "on_frame_end": None,
        "on_match_end": None, "x54_pointer": None, "x58_pointer": None,
    })

    from transition_trace_format import _player
    players = [_player(start, 0x60 + index * 0x24) for index in range(6)]
    # The established transition-trace decoder omits this defined player byte.
    # Retain it in this diagnostic-only schema without changing that shared
    # decoder's long-standing output contract.
    for index, player in enumerate(players):
        player["xB"] = start[0x60 + index * 0x24 + 0x0B]
    return {"rules": rule_values, "players": players}


def _first_css_return_summary(payload: dict[str, Any]) -> dict[str, Any]:
    from reference_capture_semantics import pad_snapshot_bytes

    css_item = _slice(payload, 50, "first-CSS return", 0x148)
    ko_item = _slice(payload, 51, "first-CSS return", 6)
    _require(css_item["address"] == FIRST_CSS_DATA_ADDRESS and
             ko_item["address"] == FIRST_CSS_KO_ADDRESS and
             int.from_bytes(css_item["raw"][4:8], "big") == ko_item["address"],
             "first-CSS return KO pointer does not name its source slice")
    raw = css_item["raw"]
    vs = raw[8:16]
    start = raw[16:]
    _require(raw[2] == 0 and raw[3] == 0,
             "first-CSS return left the original VS CSS phase")
    try:
        pad_hex = pad_snapshot_bytes(
            _slice(payload, 21, "first-CSS return", 0x358)["raw"])
    except ValueError as error:
        raise StadiumGoPrefixError(f"first-CSS return PAD is invalid: {error}") from error
    semantic = _first_css_semantic_start(start)
    return {
        "scene_kind": _slice(payload, 40, "first-CSS return", 1)["raw"][0],
        "random_seed_hex": _slice(payload, RNG_VALUE_TAG,
                                   "first-CSS return", 4)["raw"].hex(),
        "pad_state_hex": pad_hex,
        "ko_counts_hex": ko_item["raw"].hex(),
        "css": {
            "unk_0x0": int.from_bytes(raw[0:2], "big"),
            "match_type": raw[2], "pending_scene_change": raw[3],
            "ko_counts_owner": "source_vs_owned",
            "vs": {
                "loser": int.from_bytes(vs[0:1], "big", signed=True),
                "ordered_stage_index": int.from_bytes(vs[1:2], "big", signed=True),
                "winner": int.from_bytes(vs[2:3], "big", signed=True),
                "unk_0x3": vs[3], "unk_0x4": vs[4], "unk_0x5": vs[5],
                "unk_0x6": vs[6], "unk_0x7": vs[7],
                "start": semantic,
            },
        },
    }


def _extract_first_css_context_rows(entry_row: dict[str, Any],
                                   return_row: dict[str, Any],
                                   stream_sha256: str) -> dict[str, Any]:
    _require(stream_sha256 == FIRST_CSS_STREAM_SHA256,
             "first-CSS source identity is not the retained v6 observer")
    _require(entry_row.get("event") == "progress" and
             entry_row.get("seq") == FIRST_CSS_ENTRY_SEQUENCE and
             return_row.get("event") == "progress" and
             return_row.get("seq") == FIRST_CSS_RETURN_SEQUENCE,
             "first-CSS source rows have unexpected event or sequence identities")
    entry = _validate_progress(entry_row, "css_entry", EXPECTED_SETUP_RECEIPT_SHA256)
    returned = _validate_progress(return_row, "css_return", EXPECTED_SETUP_RECEIPT_SHA256)
    _first_css_slice_inventory(entry, "css_entry")
    _first_css_slice_inventory(returned, "css_return")
    _require(entry_row.get("source_tick") == return_row.get("source_tick") and
             entry.get("source_tick") == returned.get("source_tick"),
             "first-CSS entry and return are not from the same source tick")

    def read(payload, tag, phase, size, address):
        item = _slice(payload, tag, phase, size)
        _require(item["address"] == address,
                 f"{phase}: tag={tag} source address differs")
        return item

    entry_css = read(entry, 50, "css_entry", 0x148, FIRST_CSS_DATA_ADDRESS)
    entry_ko = read(entry, 51, "css_entry", 6, FIRST_CSS_KO_ADDRESS)
    entry_pad = read(entry, 21, "css_entry", 0x358, FIRST_CSS_PAD_ADDRESS)
    entry_rng_pointer = read(entry, RNG_POINTER_TAG, "css_entry", 4,
                             FIRST_CSS_RNG_POINTER_ADDRESS)
    entry_rng_value = read(entry, RNG_VALUE_TAG, "css_entry", 4,
                           FIRST_CSS_RNG_VALUE_ADDRESS)
    entry_rules = read(entry, 38, "css_entry", 0x18, FIRST_CSS_RULES_ADDRESS)
    entry_save = read(entry, 39, "css_entry", 0x55E8, FIRST_CSS_SAVE_ADDRESS)
    entry_characters = read(entry, 36, "css_entry", 2, FIRST_CSS_SAVE_ADDRESS)
    entry_stages = read(entry, 37, "css_entry", 2, FIRST_CSS_SAVE_ADDRESS + 2)
    entry_route = read(entry, SCENE_ROUTING_TAG, "css_entry", 6,
                       SCENE_ROUTING_ADDRESS)
    entry_scene = read(entry, 40, "css_entry", 1, FIRST_CSS_SCENE_ADDRESS)
    _require(_hex_address(entry.get("argument"), "css_entry.argument") ==
             FIRST_CSS_DATA_ADDRESS and
             int.from_bytes(entry_css["raw"][4:8], "big") == entry_ko["address"] and
             entry_rng_pointer["raw"] == entry_rng_value["address"].to_bytes(4, "big") and
             entry_characters["raw"] == entry_save["raw"][:2] and
             entry_stages["raw"] == entry_save["raw"][2:4] and
             entry_rules["address"] + entry_rules["size"] == entry_save["address"] and
             entry_route["raw"][0] == 0x02 and entry_scene["raw"] == b"\x08",
             "first-CSS entry context has inconsistent source owners")
    _require(entry_css["raw"][2:4] == b"\x00\x00" and
             not any(entry_css["raw"][0x10 + 0x38:0x10 + 0x5C]),
             "first-CSS entry is not a pointer-safe initial ordinary VS context")

    returned_css = read(returned, 50, "css_return", 0x148, FIRST_CSS_DATA_ADDRESS)
    returned_ko = read(returned, 51, "css_return", 6, FIRST_CSS_KO_ADDRESS)
    returned_pad = read(returned, 21, "css_return", 0x358, FIRST_CSS_PAD_ADDRESS)
    returned_rng_pointer = read(returned, RNG_POINTER_TAG, "css_return", 4,
                                FIRST_CSS_RNG_POINTER_ADDRESS)
    returned_rng_value = read(returned, RNG_VALUE_TAG, "css_return", 4,
                              FIRST_CSS_RNG_VALUE_ADDRESS)
    returned_route = read(returned, SCENE_ROUTING_TAG, "css_return", 6,
                          SCENE_ROUTING_ADDRESS)
    returned_scene = read(returned, 40, "css_return", 1, FIRST_CSS_SCENE_ADDRESS)
    _require(int.from_bytes(returned_css["raw"][4:8], "big") == returned_ko["address"] and
             returned_rng_pointer["raw"] == returned_rng_value["address"].to_bytes(4, "big") and
             returned_route["raw"] == entry_route["raw"] and
             returned_scene["raw"] == entry_scene["raw"] == b"\x08" and
             returned_rng_value["raw"] == entry_rng_value["raw"],
             "first-CSS entry/return owner, route, scene or RNG changed")

    # Reuse the existing typed profile and semantic PAD decoders. Add official
    # slice labels only to this short-lived decoder view; MWRO Progress rows
    # themselves remain unchanged and are not relabeled as session records.
    from reference_observer_stream import SLICE_NAMES
    from reference_capture_semantics import _whole_profile_context, _whole_profile_masks
    named_payload = dict(entry)
    named_payload["boundary"] = "css_entry"
    named_payload["slices"] = [
        {**item, "name": SLICE_NAMES[item["tag"]]} for item in entry["slices"]
    ]
    named_entry = {"payload": named_payload}
    profile = _whole_profile_context(named_entry)
    masks = _whole_profile_masks(named_entry)
    _require(profile is not None and
             masks["characters"] == int.from_bytes(entry_characters["raw"], "big") and
             masks["stages"] == int.from_bytes(entry_stages["raw"], "big"),
             "first-CSS source profile masks disagree with their typed save owner")
    from reference_capture_semantics import pad_snapshot_bytes
    try:
        input_pad = bytes.fromhex(pad_snapshot_bytes(entry_pad["raw"]))
    except ValueError as error:
        raise StadiumGoPrefixError(f"first-CSS entry PAD is invalid: {error}") from error
    seed = entry_rng_value["raw"]
    _require(len(seed) == 4, "first-CSS entry seed has the wrong byte width")
    bundle = b"".join((
        FIRST_CSS_CONTEXT_MAGIC,
        FIRST_CSS_CONTEXT_VERSION.to_bytes(4, "big"),
        bytes.fromhex(stream_sha256),
        FIRST_CSS_ENTRY_SEQUENCE.to_bytes(4, "big"),
        FIRST_CSS_RETURN_SEQUENCE.to_bytes(4, "big"),
        seed, input_pad, entry_css["raw"], entry_ko["raw"],
        entry_rules["raw"], entry_save["raw"],
    ))
    _require(len(bundle) == FIRST_CSS_CONTEXT_BYTES,
             "first-CSS diagnostic input bundle has an unexpected length")
    expected = _first_css_return_summary(returned)
    return {
        "schema": "melee-web-stadium-first-css-context-diagnostic",
        "version": FIRST_CSS_CONTEXT_VERSION,
        "scope": "captured original first-CSS context and immediate CSS OnEnter return only",
        "provenance": {
            "stream_bytes": FIRST_CSS_STREAM_BYTES,
            "stream_sha256": stream_sha256,
            "css_entry_sequence": FIRST_CSS_ENTRY_SEQUENCE,
            "css_return_sequence": FIRST_CSS_RETURN_SEQUENCE,
            "source_tick": entry_row["source_tick"],
        },
        "input_bundle": {
            "magic_hex": FIRST_CSS_CONTEXT_MAGIC.hex(),
            "version": FIRST_CSS_CONTEXT_VERSION,
            "bytes": len(bundle),
            "sha256": hashlib.sha256(bundle).hexdigest(),
            "has_return_expectations": False,
        },
        "expected_return": expected,
        "input_bundle_bytes": bundle,
    }


def extract_stadium_first_css_context(stream_path: str | Path,
                                      status_path: str | Path) -> dict[str, Any]:
    """Extract only the exact original v6 CSS entry and return observations."""
    source = Path(stream_path)
    _require(source.is_file(), "configured first-CSS observer stream is missing")
    _require(source.stat().st_size == FIRST_CSS_STREAM_BYTES,
             "first-CSS observer stream byte length differs from the retained v6 source")
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    _require(digest == FIRST_CSS_STREAM_SHA256,
             "first-CSS observer stream hash differs from the retained v6 source")
    try:
        summary = validate_stadium_go_prefix(source, status_path=status_path)
    except StadiumGoPrefixError:
        raise
    except (OSError, ObserverStreamError) as error:
        raise StadiumGoPrefixError(f"cannot validate first-CSS source stream: {error}") from error
    _require(summary.get("decision") == "PASS_ORIGINAL_RAW_GO_PREFIX_ONLY" and
             summary.get("stream_bytes") == FIRST_CSS_STREAM_BYTES and
             summary.get("stream_sha256") == digest,
             "first-CSS source stream no longer passes its existing full GO-prefix validation")
    entry_rows = []
    return_rows = []
    try:
        for row in iter_records(source, max_bytes=MAX_STREAM_BYTES,
                                max_records=MAX_STREAM_RECORDS):
            payload = row.get("payload", {})
            if row.get("event") == "progress" and payload.get("phase") == "css_entry":
                entry_rows.append(row)
            elif row.get("event") == "progress" and payload.get("phase") == "css_return":
                return_rows.append(row)
    except ObserverStreamError as error:
        raise StadiumGoPrefixError(f"cannot read first-CSS source rows: {error}") from error
    _require(len(entry_rows) == 1 and len(return_rows) == 1,
             "first-CSS source stream must have exactly one entry and return row")
    result = _extract_first_css_context_rows(entry_rows[0], return_rows[0], digest)
    _require(source.stat().st_size == FIRST_CSS_STREAM_BYTES and
             hashlib.sha256(source.read_bytes()).hexdigest() == digest,
             "first-CSS source stream changed during extraction")
    return result


def decode_first_css_context_bundle(data: bytes) -> dict[str, Any]:
    """Validate and decode the input-only fixed-layout diagnostic bundle."""
    _require(isinstance(data, bytes) and len(data) == FIRST_CSS_CONTEXT_BYTES,
             "first-CSS diagnostic bundle has an invalid exact length")
    _require(data[:8] == FIRST_CSS_CONTEXT_MAGIC and
             int.from_bytes(data[8:12], "big") == FIRST_CSS_CONTEXT_VERSION,
             "first-CSS diagnostic bundle magic/version differs")
    stream_sha = data[12:44].hex()
    _require(stream_sha == FIRST_CSS_STREAM_SHA256,
             "first-CSS diagnostic bundle source identity is not the retained v6 observer")
    entry_seq = int.from_bytes(data[44:48], "big")
    return_seq = int.from_bytes(data[48:52], "big")
    _require((entry_seq, return_seq) ==
             (FIRST_CSS_ENTRY_SEQUENCE, FIRST_CSS_RETURN_SEQUENCE),
             "first-CSS diagnostic bundle sequence identities differ")
    offset = FIRST_CSS_CONTEXT_HEADER_BYTES
    return {
        "source_stream_sha256": stream_sha,
        "entry_sequence": entry_seq,
        "return_sequence": return_seq,
        "seed_hex": data[offset:offset + 4].hex(),
        "pad_state_hex": data[offset + 4:offset + 4 + 822].hex(),
        "css_data_hex": data[offset + 4 + 822:offset + 4 + 822 + 0x148].hex(),
        "ko_counts_hex": data[offset + 4 + 822 + 0x148:
                               offset + 4 + 822 + 0x148 + 6].hex(),
        "game_rules_hex": data[offset + 4 + 822 + 0x148 + 6:
                               offset + 4 + 822 + 0x148 + 6 + 0x18].hex(),
        "save_data_hex": data[-0x55E8:].hex(),
        "contains_expected_return": False,
    }
