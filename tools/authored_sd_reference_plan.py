"""Predeclared neutral original-game experiment; no recorded input or oracle state.

This recipe is a bounded Sudden Death initialization prerequisite, not a
capture-ready workload. Menu preparation, DOL instruction verification and an
SD observer remain required. No samples may be appended after observing a run.
"""
import hashlib
import json

RECIPE_SCHEMA = "melee-web-authored-sd-reference-recipe"
PROVENANCE = "authored-development-workload"
STARTUP_TICKS = 123
MATCH_TICKS = 4200


def canonical(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       allow_nan=False) + "\n").encode("utf-8")


def recipe(version=1):
    """Return a fresh copy of the single fixed, wholly authored experiment."""
    declaration = {
        "schema": RECIPE_SCHEMA,
        "version": 1,
        "id": "two-human-mario-fd-neutral-timeout-sd-prefix",
        "provenance": PROVENANCE,
        "purpose": "observe natural tied timeout through Sudden Death initialization",
        "timeline": {
            "first_frame": -123,
            "startup_ticks": STARTUP_TICKS,
            "match_ticks": MATCH_TICKS,
            "input": "neutral on P1/P2; P3/P4 disconnected for every declared sample",
            "exhaustion": "fail; never append, repeat or synthesize samples",
        },
        "expected_setup": {
            "players": [
                {"port": 1, "character_kind": 8, "costume": 0, "stocks": 4,
                 "player_type": 0, "rumble_enabled": False},
                {"port": 2, "character_kind": 8, "costume": 1, "stocks": 4,
                 "player_type": 0, "rumble_enabled": False},
            ],
            "stage": 32, "match_kind": 1, "timer_enabled": True,
            "timer_counts_up": False, "time_limit_seconds": 60,
            "is_stock": True, "disable_pausing": True, "is_teams": False,
            "item_frequency": -1, "item_mask_hex": "ffffffffffffffff",
            "damage_ratio_bits": "3f800000", "game_speed_bits": "3f800000",
        },
        "stop": "completed SD initialization or declared sample cap, whichever comes first",
        "exclusions": ["SD resolution", "Results/CSS return", "pixels", "PCM", "live timing"],
        "setup_policy": "original menus only; no game, RNG, fighter or winner writes",
    }
    if version in (2, 3, 4) and type(version) is int:
        declaration["version"] = version
        declaration["cold_original_context"] = {
            "port_rumble_preferences": [0, 0, 1, 1],
            "human_source_slots": [1, 2],
            "human_nametags": [120, 120],
            "port_mapping": "slot zero uses player index; otherwise slot minus one",
            "preparation": "original Options Rumble: P1/P2 off; P3/P4 untouched defaults",
        }
        if version in (3, 4):
            del declaration["cold_original_context"]["human_source_slots"]
            declaration["cold_original_context"]["human_source_ports_zero_based"] = [0, 1]
        if version == 4:
            from sd_gci_profile import GCI_SHA256, GAME_WRITTEN_SHA256
            declaration["original_profile"] = {
                "id": "game-written-save-browser-reexport-original-load",
                "gci_sha256": GCI_SHA256, "game_written_predecessor_sha256": GAME_WRITTEN_SHA256,
                "provenance": "browser re-export of original game-written save; historical fresh original load",
                "initial_port_rumble_preferences": [1, 1, 1, 1],
                "initial_characters_mask": "07ff", "initial_stages_mask": "07ff",
                "first_gate": "reduced Rules readiness and loaded context only; later settings preparation separate",
            }
    elif version != 1 or type(version) is not int:
        raise ValueError("Unsupported authored SD recipe version")
    return declaration


def validate_recipe(value):
    # Byte comparison also rejects bool-for-int and int-for-float substitutions.
    version = value.get("version") if isinstance(value, dict) else None
    if version not in (1, 2, 3, 4) or canonical(value) != canonical(recipe(version)):
        raise ValueError("Unsupported or changed authored SD reference recipe")
    return value


def recipe_sha256(value):
    validate_recipe(value)
    return hashlib.sha256(canonical(value)).hexdigest()


def make_input_plan(recipe_version=1):
    """Build all samples before execution; there is no length/extension option."""
    from retail_input_plan import (AUTHORED_PLAN_VERSION, SCHEMA, POLICY,
                                   NEUTRAL_PAD, DISCONNECTED_PAD, validate_plan)
    declaration = recipe(recipe_version)
    return validate_plan({
        "schema": SCHEMA, "version": AUTHORED_PLAN_VERSION, "policy": POLICY,
        "provenance": PROVENANCE, "authored_recipe": declaration,
        "authored_recipe_sha256": recipe_sha256(declaration),
        "first_frame": -123, "source_stage": 32, "source_characters": [8, 8],
        "active_player_count": 2, "source_player_types": [0, 0],
        "controlled_ports": [1, 2],
        "frames": [[NEUTRAL_PAD, NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
                   for _ in range(STARTUP_TICKS + MATCH_TICKS)],
    })
