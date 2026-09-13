#!/usr/bin/env python3
"""Recipes and authored input workloads for the original CPU corpus.

This module emits the native ``melee-web-retail-input-plan`` v3 contract.  Its
four PAD vectors cover the original two, three, and four player CSS paths;
inactive ports are explicitly disconnected.  The scenario metadata remains
separate from this runner-facing plan.

The only values generated for a CPU port are the original neutral PAD bytes.
CPU decisions and fighter input are observed from the original game; this
module never writes game state, RNG, fighter state, rules, or registers.
"""
from __future__ import annotations

from copy import deepcopy
import hashlib
import json
import re
import struct
from typing import Any, Iterable


SCHEMA = "melee-web-cpu-reference-scenario"
VERSION = 1
# Corpus plans use the native collector's existing multiplayer contract.  The
# collector owns this schema; keep the scenario schema separate so no private
# plan format can accidentally be handed to the capture runner.
PLAN_SCHEMA = "melee-web-retail-input-plan"
PLAN_VERSION = 3
RAW_POLICY = "dolphin-pipe-raw-v2"
FIRST_SOURCE_FRAME = -123
STARTUP_NEUTRAL_TICKS = 123
MATCH_TICKS = 60 * 60
CAPTURE_TICKS = 4200
# The ordinary VS menu remains a one-minute match.  The longer 3P variant
# only gives the authored workload a two-minute observation horizon so a
# natural timeout/ending can be observed without changing the menu rules.
LONG_MATCH_TICKS = 120 * 60
LONG_CAPTURE_TICKS = 7800
MAX_CAPTURE_TICKS = LONG_CAPTURE_TICKS
MAX_PLAN_FRAMES = STARTUP_NEUTRAL_TICKS + MAX_CAPTURE_TICKS

NEUTRAL_PAD = "00" * 11
DISCONNECTED_PAD = "00" * 10 + "ff"
PAD = struct.Struct(">HbbbbBBBBb")
PAD_HEX = re.compile(r"[0-9a-f]{22}\Z")

BUTTONS = {
    "D_LEFT": 1,
    "D_RIGHT": 2,
    "D_DOWN": 4,
    "D_UP": 8,
    "Z": 16,
    "R": 32,
    "L": 64,
    "A": 256,
    "B": 512,
    "X": 1024,
    "Y": 2048,
    "START": 4096,
}
BUTTON_MASK = sum(BUTTONS.values())

CHARACTERS = {
    "Fox": 2,
    "Mario": 8,
    "Marth": 9,
    "Falco": 20,
}
STAGES = {
    "Yoshis Story": 8,
    "Dream Land": 28,
    "Final Destination": 32,
}


def _player(port: int, character: str, costume: int, player_type: str,
            level: int | None = None) -> dict[str, Any]:
    result: dict[str, Any] = {
        "port": port,
        # Ordinary VS is free-for-all in these recipes. Retain the source
        # StartMelee team byte so CSS proof evidence binds it explicitly.
        "team": 0,
        "character": character,
        "character_kind": CHARACTERS[character],
        "costume": costume,
        "player_type": player_type,
        "player_type_code": 0 if player_type == "human" else 1,
        "cpu_kind": None if player_type == "human" else 4,
        "cpu_level": level,
        "cpu_pad_mode": None if player_type == "human" else "neutral",
        "rumble_enabled": False,
        "stocks": 2,
    }
    return result


def _scenario(name: str, stage: str, players: list[dict[str, Any]],
              offset: int, *, match_ticks: int = MATCH_TICKS,
              capture_ticks: int = CAPTURE_TICKS,
              timer_seconds: int = 60) -> dict[str, Any]:
    timer_label = f"{timer_seconds // 60}:{timer_seconds % 60:02d}"
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "scenario_id": name,
        "stage": stage,
        "stage_kind": STAGES[stage],
        "match_kind": 1,
        "rules": {
            "is_stock": True,
            "stocks": 2,
            "timer_enabled": True,
            "timer_counts_up": False,
            "time_limit_seconds": timer_seconds,
            "disable_pausing": False,
            "is_teams": False,
            # The source stores this as byte FF; setup validation exposes its
            # signed source value, -1, for the ordinary items-off setting.
            "item_frequency": -1,
            "item_mask_hex": "ffffffffffffffff",
            "damage_ratio_bits": "3f800000",
            "game_speed_bits": "3f800000",
        },
        "players": players,
        "human_workload": {
            "schema": "melee-web-authored-development-workload",
            "version": 1,
            "label": "opening edge probe plus repeated CPU combat cycle",
            "purpose": (
                "Purposeful raw P1 input to expose engagement, damage, stock loss, "
                "respawn, camera movement and magnifier-visible drawing."
            ),
            "source_kind": "authored_development_workload",
            "human_port": 1,
            "startup_neutral_ticks": STARTUP_NEUTRAL_TICKS,
            "match_ticks": match_ticks,
            "capture_cap_ticks": capture_ticks,
            "phase_offset": offset,
            "phases": [
                {
                    "name": "opening_edge_probe",
                    "ticks": 90,
                    "main_x": 80,
                    "buttons": [],
                    "purpose": "predeclared approach toward the stage boundary",
                },
                {
                    "name": "opening_recovery_jump",
                    "ticks": 14,
                    "main_x": 80,
                    "buttons": ["X"],
                    "purpose": "attempt a normal jump recovery after the approach",
                },
                {
                    "name": "opening_retreat",
                    "ticks": 36,
                    "main_x": -80,
                    "buttons": [],
                    "purpose": "return toward center under source physics",
                },
                {
                    "name": "combat_cycle",
                    "ticks": 240,
                    "purpose": "repeat raw approach, attacks, shield and recovery",
                    "subphases": [
                        {"name": "approach_right", "ticks": 32,
                         "main_x": 80, "buttons": []},
                        {"name": "attack_a_right", "ticks": 4,
                         "main_x": 80, "buttons": ["A"]},
                        {"name": "shield_reset", "ticks": 24,
                         "main_x": 0, "buttons": ["L"]},
                        {"name": "jump_forward", "ticks": 8,
                         "main_x": 80, "buttons": ["X"]},
                        {"name": "retreat_left", "ticks": 29,
                         "main_x": -80, "buttons": []},
                        {"name": "attack_a_left", "ticks": 4,
                         "main_x": -80, "buttons": ["A"]},
                        {"name": "special_b", "ticks": 10,
                         "main_x": 0, "buttons": ["B"]},
                        {"name": "recovery_jump", "ticks": 10,
                         "main_x": 80, "buttons": ["X"]},
                        {"name": "spacing", "ticks": 119,
                         "main_x": 0, "buttons": []},
                    ],
                },
            ],
            "physical_state_writes": [],
            "game_state_writes": [],
            "outcome_policy": {
                "natural_source_end_required": True,
                "allow_timeout": True,
                "allow_tie": True,
                "cap_exhausted_is_incomplete": True,
                "append_neutral_after_failure": False,
                "force_winner": False,
            },
        },
        "source_menu_route": [
            f"ordinary VS rules menu: Stock, 2; Time, {timer_label}; Pause, On; items off",
            "ordinary CSS: select each character and costume",
            "ordinary CSS: toggle each CPU door to CPU kind 4",
            "ordinary CSS: move each CPU slider to its declared level 1..9",
            "ordinary CSS Start",
            "ordinary SSS: select the declared stage kind",
        ],
        "availability_policy": {
            "source_verified_only": True,
            "persistent_changes": "none; the owned baseline must already expose every requested original menu item",
            "allowed_memory_writes": [],
            "forbidden_memory_writes": [
                "rules", "RNG", "fighter state", "CPU state", "active match state", "registers",
            ],
        },
        "source_evidence": {
            "character_enum": ".deps/melee/src/melee/ft/forward.h:128-153",
            "stage_rows": ".deps/melee/src/melee/mn/mnstagesel.static.h:15-45",
            "cpu_toggle": ".deps/melee/src/melee/mn/mncharsel.c:2979-3074",
            "cpu_slider": ".deps/melee/src/melee/mn/mncharsel.c:2730-2783",
        },
    }


HISTORICAL_THREE_PLAYER_SCENARIO_ID = "marth-human-vs-falco5-mario9-yoshis-story"
ACTIVE_THREE_PLAYER_SCENARIO_ID = (
    "marth-human-vs-falco5-mario9-yoshis-story-120s"
)

# Keep the original 60-second 3P recipe addressable for investigation of its
# retained tie.  It is deliberately outside the active catalog: callers that
# enumerate current scenarios must receive only the long-horizon replacement.
HISTORICAL_SCENARIOS: dict[str, dict[str, Any]] = {
    HISTORICAL_THREE_PLAYER_SCENARIO_ID: _scenario(
        HISTORICAL_THREE_PLAYER_SCENARIO_ID,
        "Yoshis Story",
        [
            _player(1, "Marth", 0, "human"),
            _player(2, "Falco", 1, "cpu", 5),
            _player(3, "Mario", 1, "cpu", 9),
        ],
        37,
    ),
}


SCENARIOS: dict[str, dict[str, Any]] = {
    "mario-human-vs-fox-cpu1-final-destination": _scenario(
        "mario-human-vs-fox-cpu1-final-destination",
        "Final Destination",
        [_player(1, "Mario", 0, "human"), _player(2, "Fox", 1, "cpu", 1)],
        0,
    ),
    ACTIVE_THREE_PLAYER_SCENARIO_ID: _scenario(
        ACTIVE_THREE_PLAYER_SCENARIO_ID,
        "Yoshis Story",
        [
            _player(1, "Marth", 0, "human"),
            _player(2, "Falco", 1, "cpu", 5),
            _player(3, "Mario", 1, "cpu", 9),
        ],
        37,
        match_ticks=LONG_MATCH_TICKS,
        capture_ticks=LONG_CAPTURE_TICKS,
        timer_seconds=120,
    ),
    "fox-human-vs-mario3-marth6-falco8-dream-land": _scenario(
        "fox-human-vs-mario3-marth6-falco8-dream-land",
        "Dream Land",
        [
            _player(1, "Fox", 0, "human"),
            _player(2, "Mario", 1, "cpu", 3),
            _player(3, "Marth", 2, "cpu", 6),
            _player(4, "Falco", 3, "cpu", 8),
        ],
        83,
    ),
}


def canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def scenario_ids() -> list[str]:
    return sorted(SCENARIOS)


def get_scenario(scenario_id: str) -> dict[str, Any]:
    scenario = SCENARIOS.get(scenario_id)
    if scenario is None:
        scenario = HISTORICAL_SCENARIOS.get(scenario_id)
    if scenario is None:
        raise ValueError(f"unknown CPU corpus scenario: {scenario_id}")
    return deepcopy(scenario)


def catalog_summary() -> dict[str, Any]:
    all_players = [p for s in SCENARIOS.values() for p in s["players"]]
    cpu_levels = sorted(p["cpu_level"] for p in all_players if p["player_type"] == "cpu")
    return {
        "scenario_ids": scenario_ids(),
        "cpu_levels": cpu_levels,
        "distinct_cpu_levels": sorted(set(cpu_levels)),
        "characters": sorted({p["character"] for p in all_players}),
        "stages": sorted({s["stage"] for s in SCENARIOS.values()}),
        "player_counts": sorted({len(s["players"]) for s in SCENARIOS.values()}),
    }


def validate_scenario(scenario: dict[str, Any]) -> None:
    if scenario.get("schema") != SCHEMA or scenario.get("version") != VERSION:
        raise ValueError("unsupported CPU reference scenario schema")
    players = scenario.get("players")
    if not isinstance(players, list) or not 2 <= len(players) <= 4:
        raise ValueError("a CPU reference scenario requires 2..4 players")
    if [p.get("port") for p in players] != list(range(1, len(players) + 1)):
        raise ValueError("scenario ports must be contiguous P1..P4")
    if players[0].get("player_type") != "human" or players[0].get("player_type_code") != 0:
        raise ValueError("P1 must be the human player")
    for p in players[1:]:
        if p.get("player_type") != "cpu" or p.get("player_type_code") != 1:
            raise ValueError("every player after P1 must be an ordinary CPU")
        if p.get("cpu_kind") != 4 or not 1 <= p.get("cpu_level", 0) <= 9:
            raise ValueError("CPU players require ordinary CPU kind 4 and level 1..9")
        if p.get("cpu_pad_mode") not in ("neutral", "disconnected"):
            raise ValueError("CPU players require neutral or disconnected PAD mode")
    for p in players:
        if p.get("team") != 0:
            raise ValueError("scenario players must use source free-for-all team 0")
        if p.get("character") not in CHARACTERS or p.get("character_kind") != CHARACTERS[p["character"]]:
            raise ValueError("scenario character kind does not match the source enum")
        if not 0 <= p.get("costume", -1) <= 5:
            raise ValueError("costume is outside the original CSS range")
        if p.get("rumble_enabled") is not False or p.get("stocks") != 2:
            raise ValueError("scenario player rules must be two stocks with rumble disabled")
    if scenario.get("stage") not in STAGES or scenario.get("stage_kind") != STAGES[scenario["stage"]]:
        raise ValueError("scenario stage kind does not match the source stage table")
    rules = scenario.get("rules")
    timer_seconds = rules.get("time_limit_seconds") if isinstance(rules, dict) else None
    if timer_seconds not in (60, 120):
        raise ValueError("scenario timer must use an authored 60- or 120-second rule")
    expected_rules = {
        "is_stock": True,
        "stocks": 2,
        "timer_enabled": True,
        "timer_counts_up": False,
        "time_limit_seconds": timer_seconds,
        "disable_pausing": False,
        "is_teams": False,
        "item_frequency": -1,
        "item_mask_hex": "ffffffffffffffff",
        "damage_ratio_bits": "3f800000",
        "game_speed_bits": "3f800000",
    }
    if rules != expected_rules:
        raise ValueError("scenario rules differ from the bounded original-menu proposal")
    workload = scenario.get("human_workload", {})
    if workload.get("human_port") != 1 or workload.get("startup_neutral_ticks") != STARTUP_NEUTRAL_TICKS:
        raise ValueError("workload must begin with the pinned 123-tick source startup segment")
    match_ticks = workload.get("match_ticks")
    capture_cap_ticks = workload.get("capture_cap_ticks")
    if (type(match_ticks) is not int or type(capture_cap_ticks) is not int or
            not 1 <= match_ticks <= capture_cap_ticks <= MAX_CAPTURE_TICKS):
        raise ValueError("workload match and capture lengths are outside the authored bounds")
    expected_workload = {
        60: (MATCH_TICKS, CAPTURE_TICKS),
        120: (LONG_MATCH_TICKS, LONG_CAPTURE_TICKS),
    }[timer_seconds]
    if (match_ticks, capture_cap_ticks) != expected_workload:
        raise ValueError("workload length does not match the authored menu timer")
    outcome = workload.get("outcome_policy", {})
    for key in ("natural_source_end_required", "allow_timeout", "allow_tie"):
        if outcome.get(key) is not True:
            raise ValueError(f"workload outcome policy must preserve {key}")
    if outcome.get("force_winner") or outcome.get("append_neutral_after_failure"):
        raise ValueError("workload cannot force an outcome or append neutral input")


def validate_catalog() -> None:
    for scenario in SCENARIOS.values():
        validate_scenario(scenario)
    summary = catalog_summary()
    if summary["distinct_cpu_levels"] != [1, 3, 5, 6, 8, 9]:
        raise ValueError("catalog must cover the six requested distinct CPU levels")
    if summary["characters"] != ["Falco", "Fox", "Mario", "Marth"]:
        raise ValueError("catalog must cover all four requested characters")
    if summary["stages"] != ["Dream Land", "Final Destination", "Yoshis Story"]:
        raise ValueError("catalog must cover the three requested stages")


def pack_pad(buttons: Iterable[str] = (), main_x: int = 0, main_y: int = 0,
             cstick_x: int = 0, cstick_y: int = 0, left: int = 0,
             right: int = 0) -> str:
    names = tuple(buttons)
    unknown = set(names) - BUTTONS.keys()
    if unknown:
        raise ValueError(f"unknown authored PAD button(s): {sorted(unknown)}")
    mask = 0
    for name in names:
        mask |= BUTTONS[name]
    if not all(isinstance(v, int) and -127 <= v <= 127 for v in
               (main_x, main_y, cstick_x, cstick_y)):
        raise ValueError("authored PAD axes must be signed bytes")
    for value in (left, right):
        if not isinstance(value, int) or not 0 <= value <= 255:
            raise ValueError("authored PAD triggers must be bytes")
    if mask & BUTTONS["L"]:
        left = 255
    if mask & BUTTONS["R"]:
        right = 255
    if mask & BUTTONS["L"]:
        left = 255
    if mask & BUTTONS["R"]:
        right = 255
    return PAD.pack(mask, main_x, main_y, cstick_x, cstick_y, left, right, 0, 0, 0).hex()


def _combat_subphase(phase: int) -> dict[str, Any]:
    subphases = SCENARIOS[next(iter(SCENARIOS))]["human_workload"]["phases"][-1]["subphases"]
    cursor = 0
    for subphase in subphases:
        if phase < cursor + subphase["ticks"]:
            return subphase
        cursor += subphase["ticks"]
    raise ValueError("combat cycle phases do not total 240 ticks")


def authored_human_pad(scenario: dict[str, Any], match_tick: int) -> str:
    """Return one raw-v2 P1 PAD sample for a source match tick.

    The first 140 match ticks are a single declared edge/recovery probe.  The
    rest repeats the declared combat cycle.  The function remains deterministic
    and keeps generating authored actions through the cap; it never changes to
    neutral because a previous outcome was inconvenient.
    """
    offset = int(scenario["human_workload"]["phase_offset"])
    if match_tick < 0:
        return NEUTRAL_PAD
    if match_tick < 90:
        return pack_pad(main_x=80)
    if match_tick < 104:
        return pack_pad(("X",), main_x=80)
    if match_tick < 140:
        return pack_pad(main_x=-80)
    phase = (match_tick - 140 + offset) % 240
    subphase = _combat_subphase(phase)
    return pack_pad(subphase.get("buttons", ()), main_x=subphase.get("main_x", 0))


def make_input_plan(scenario: dict[str, Any], ticks: int | None = None) -> dict[str, Any]:
    validate_scenario(scenario)
    capture_cap_ticks = scenario["human_workload"]["capture_cap_ticks"]
    if ticks is None:
        ticks = capture_cap_ticks
    if type(ticks) is not int or not 1 <= ticks <= capture_cap_ticks:
        raise ValueError(f"input plan ticks must be in 1..{capture_cap_ticks}")
    players = scenario["players"]
    source_sha256 = hashlib.sha256(canonical(scenario)).hexdigest()
    frames: list[list[str]] = []
    for source_tick in range(-STARTUP_NEUTRAL_TICKS, ticks):
        human = NEUTRAL_PAD if source_tick < 0 else authored_human_pad(scenario, source_tick)
        row = [human]
        row.extend(NEUTRAL_PAD if p["cpu_pad_mode"] == "neutral" else DISCONNECTED_PAD
                   for p in players[1:])
        # v3 always carries all four source ports. Ports outside the active
        # match are explicitly disconnected.
        row.extend([DISCONNECTED_PAD] * (4 - len(players)))
        frames.append(row)
    return {
        "schema": PLAN_SCHEMA,
        "version": PLAN_VERSION,
        "policy": RAW_POLICY,
        "source_sha256": source_sha256,
        "first_frame": FIRST_SOURCE_FRAME,
        "source_stage": scenario["stage_kind"],
        "source_characters": [p["character_kind"] for p in players],
        "source_player_types": [p["player_type_code"] for p in players],
        "source_cpu_kinds": [p["cpu_kind"] for p in players],
        "source_cpu_levels": [p["cpu_level"] for p in players],
        "source_cpu_pad_modes": [p["cpu_pad_mode"] for p in players],
        "controlled_ports": [1],
        "active_player_count": len(players),
        "frames": frames,
    }


def _plan_capture_cap(plan: dict[str, Any]) -> int:
    """Resolve the authored cap from the plan's immutable scenario digest."""
    source_sha256 = plan.get("source_sha256")
    for scenario in (*SCENARIOS.values(), *HISTORICAL_SCENARIOS.values()):
        if source_sha256 == hashlib.sha256(canonical(scenario)).hexdigest():
            return scenario["human_workload"]["capture_cap_ticks"]
    # Unknown plans still receive the global safety bound.  Preparation binds
    # known scenarios explicitly, while this keeps the v3 validator useful for
    # a plan whose source recipe is supplied out of band.
    return MAX_CAPTURE_TICKS


def validate_input_plan(plan: dict[str, Any],
                        max_capture_ticks: int | None = None) -> None:
    if plan.get("schema") != PLAN_SCHEMA or plan.get("version") != PLAN_VERSION:
        raise ValueError("unsupported corpus input plan schema")
    frames = plan.get("frames")
    char_count = plan.get("active_player_count")
    if type(char_count) is not int:
        raise ValueError("v3 plan requires an active_player_count")
    if not isinstance(frames, list) or not frames or not 2 <= char_count <= 4:
        raise ValueError("corpus plan requires a nonempty 2..4 player timeline")
    if max_capture_ticks is None:
        max_capture_ticks = _plan_capture_cap(plan)
    if (type(max_capture_ticks) is not int or
            not 1 <= max_capture_ticks <= MAX_CAPTURE_TICKS or
            len(frames) > STARTUP_NEUTRAL_TICKS + max_capture_ticks):
        raise ValueError("corpus input plan exceeds its bounded capture cap")
    if plan.get("first_frame") != FIRST_SOURCE_FRAME:
        raise ValueError("corpus plan must begin at source frame -123")
    for row in frames:
        if not isinstance(row, list) or len(row) != 4:
            raise ValueError("each v3 input tick must contain all four source ports")
        for port, pad in enumerate(row):
            if not isinstance(pad, str) or not PAD_HEX.fullmatch(pad):
                raise ValueError("corpus plan contains a noncanonical PAD sample")
            if port >= char_count:
                if pad != DISCONNECTED_PAD:
                    raise ValueError("inactive source ports must be disconnected")
                continue
            if port and pad not in (NEUTRAL_PAD, DISCONNECTED_PAD):
                raise ValueError("CPU source ports must use their declared neutral/disconnected PAD")
            fields = PAD.unpack(bytes.fromhex(pad))
            # PAD.err is signed in the packed boundary, so the canonical FF
            # disconnected marker decodes as -1.
            expected_error = -1 if port and pad == DISCONNECTED_PAD else 0
            if fields[-1] != expected_error or fields[7] != 0 or fields[8] != 0:
                raise ValueError("corpus PAD samples must use declared error mode and zero A/B pressure")
    if plan.get("controlled_ports") != [1]:
        raise ValueError("only human P1 is controlled by the authored workload")
    # Apply the collector's strict field and PAD checks as part of preparation.
    import sys
    from pathlib import Path
    tools_dir = Path(__file__).resolve().parent
    if str(tools_dir) not in sys.path:
        sys.path.insert(0, str(tools_dir))
    from retail_input_plan import validate_plan as validate_native_plan
    validate_native_plan(plan)


def scenario_target(scenario: dict[str, Any]) -> dict[str, Any]:
    """Build the expected match-entry declaration consumed by setup tools."""
    validate_scenario(scenario)
    rules = scenario["rules"]
    players = []
    for p in scenario["players"]:
        target_player = {
            "port": p["port"],
            "character_kind": p["character_kind"],
            "costume": p["costume"],
            "player_type": p["player_type_code"],
            "stocks": p["stocks"],
            "rumble_enabled": p["rumble_enabled"],
        }
        if p["player_type_code"] == 1:
            target_player.update({
                "cpu_kind": p["cpu_kind"],
                "cpu_level": p["cpu_level"],
            })
        players.append(target_player)
    return {
        "schema": "melee-web-cpu-reference-corpus-target",
        "version": 1,
        "role": "development",
        "name": scenario["scenario_id"],
        "setup_policy": "original CSS/SSS menus; no code/RNG/fighter/match/CPU-state writes",
        "expected_setup": {
            "stage": scenario["stage_kind"],
            "match_kind": scenario["match_kind"],
            "is_stock": rules["is_stock"],
            "timer_enabled": rules["timer_enabled"],
            "timer_counts_up": rules["timer_counts_up"],
            "time_limit_seconds": rules["time_limit_seconds"],
            "disable_pausing": rules["disable_pausing"],
            "is_teams": rules["is_teams"],
            "item_frequency": rules["item_frequency"],
            "item_mask_hex": rules["item_mask_hex"],
            "damage_ratio_bits": rules["damage_ratio_bits"],
            "game_speed_bits": rules["game_speed_bits"],
            "players": players,
        },
    }


validate_catalog()
