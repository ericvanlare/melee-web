"""Validate a named, passive original Stadium GO raw prefix.

This validator admits only the bounded CSS -> SSS -> ordinary VS setup -> GO
prefix. It preserves source boundaries and makes no port RNG-equality claim.
"""

from __future__ import annotations

import hashlib
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
