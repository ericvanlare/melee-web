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


def recipe():
    """Return a fresh copy of the single fixed, wholly authored experiment."""
    return {
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


def validate_recipe(value):
    # Byte comparison also rejects bool-for-int and int-for-float substitutions.
    if canonical(value) != canonical(recipe()):
        raise ValueError("Unsupported or changed authored SD reference recipe")
    return value


def recipe_sha256(value):
    validate_recipe(value)
    return hashlib.sha256(canonical(value)).hexdigest()


def make_input_plan():
    """Build all samples before execution; there is no length/extension option."""
    from retail_input_plan import (AUTHORED_PLAN_VERSION, SCHEMA, POLICY,
                                   NEUTRAL_PAD, DISCONNECTED_PAD, validate_plan)
    declaration = recipe()
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
