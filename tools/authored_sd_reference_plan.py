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
    if type(version) is int and version == 7:
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD
        declaration = recipe(5)
        declaration.update(version=7, id="sparse-p1-p3-mario-fd-setup-input-witness",
                           purpose="observe original sparse P1/P3 Mario/FD setup and a distinct consumed PAD witness")
        declaration["source_slots"] = [0, 2]
        declaration["cold_original_context"].update(
            human_source_ports_zero_based=[0, 2],
            preparation="unchanged original loaded profile; only source ports 0 and 2 join")
        declaration["expected_setup"].update(
            players=[
                {"port": 1, "character_kind": 8, "costume": 1, "stocks": 4,
                 "player_type": 0, "rumble_enabled": True,
                 "source_slot": 0, "source_port": 0},
                {"port": 3, "character_kind": 8, "costume": 0, "stocks": 4,
                 "player_type": 0, "rumble_enabled": True,
                 "source_slot": 2, "source_port": 2},
            ],
            stage=32, match_kind=1, timer_enabled=False, timer_counts_up=False,
            time_limit_seconds=0, is_stock=True, disable_pausing=False,
            is_teams=False, item_frequency=-1, item_mask_hex="ffffffffffffffff",
            damage_ratio_bits="3f800000", game_speed_bits="3f800000",
            friendly_fire=False)
        declaration["expected_game_rules"] = {"mode": 1, "time_limit": 2, "stock_count": 4,
            "handicap": 0, "damage_ratio": 10, "stock_time_limit": 0,
            "friendly_fire": 0, "pause": 1}
        press = [raw_pad(buttons=["A"], x=35), DISCONNECTED_PAD,
                 raw_pad(buttons=["B"], y=-35), DISCONNECTED_PAD]
        release = [NEUTRAL_PAD, DISCONNECTED_PAD, NEUTRAL_PAD, DISCONNECTED_PAD]
        declaration["input_witness"] = {
            "source_slots": [0, 2], "inactive_source_slots": [1, 3],
            "press": press, "release": release,
            "max_prepress_neutral_samples": 6, "max_source_samples": 8,
            "expected_pad_errors": [0, -1, 0, -1],
        }
        declaration["expected_item_preference_mask_hex"] = "ffffffffffffffff"
        declaration["original_profile"]["first_gate"] = (
            "original sparse P1/P3 CSS/SSS setup and separate actual input witness")
        declaration["stop"] = "observed exact VS setup, distinct PAD0/PAD2 consume and release; interrupt before terminal"
        declaration["exclusions"] = ["natural match terminal", "elimination", "Results/CSS return",
            "physical-device path equality", "RNG", "pixels", "PCM", "live timing"]
        return declaration
    if type(version) is int and version == 8:
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD, NEUTRAL_PAD
        declaration = recipe(7)
        declaration.update(version=8, id="original-sheik-transform-prefix-p1-p2",
                           purpose="observe Zelda/Mario original setup and one consumed down-B transform prefix")
        declaration["source_slots"] = [0, 1]
        declaration["cold_original_context"] = {
            "human_source_ports_zero_based": [0, 1],
            "preparation": "unchanged supplied original profile; only source ports 0 and 1 join",
            "profile_binding": "launch inventory hashes every supplied source-profile file before the owned Pipe copy is prepared",
        }
        declaration["timeline"] = {
            "first_source_tick": 0,
            "max_source_tick_observations_after_setup": 600,
            "input": "wait for consumed neutral and grounded neutral Zelda; consume one B/Y=-80 episode; consume neutral release; retain actual source queue samples",
            "exhaustion": "fail at 600 observations; never append, repeat or synthesize samples",
        }
        declaration["original_profile"] = {
            "id": "supplied-original-dolphin-profile",
            "provenance": "unchanged user-supplied original profile directory, bound by per-file SHA-256 inventory in launch.json; no GCI injected and no source-profile bytes rewritten",
            "first_gate": "original two-human Zelda/Mario CSS/SSS setup, then consumed down-B and neutral release",
        }
        declaration["expected_setup"]["players"] = [
            {"port": 1, "character_kind": 18, "costume": 1, "stocks": 4,
             "player_type": 0, "rumble_enabled": True},
            {"port": 2, "character_kind": 8, "costume": 0, "stocks": 4,
             "player_type": 0, "rumble_enabled": True},
        ]
        press = [raw_pad(buttons=["B"], y=-80), NEUTRAL_PAD,
                 DISCONNECTED_PAD, DISCONNECTED_PAD]
        release = [NEUTRAL_PAD, NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
        declaration["input_witness"] = {
            "source_slots": [0, 1], "inactive_source_slots": [2, 3],
            "press": press, "release": release,
            "max_prepress_neutral_samples": 600, "max_source_samples": 600,
            "expected_pad_errors": [0, 0, -1, -1],
        }
        declaration["stop"] = (
            "observed exact VS setup, consumed P1 B/Y=-80, neutral release, active Sheik owner, "
            "and grounded neutral Sheik source tick; no terminal match claim")
        declaration["exclusions"] = ["natural match terminal", "elimination", "Results/CSS return",
            "physical-device path equality", "pixels", "PCM", "draw boundaries", "live timing"]
        return declaration
    if type(version) is int and version == 6:
        declaration = recipe(5)
        declaration.update(version=6, id="two-human-mario-fd-competitive-profile-entry",
                           purpose="observe exact competitive original menu/profile and normalized VS setup",
                           stop="observed VS setup return; no active match or timeout admission")
        declaration["expected_setup"].update(time_limit_seconds=480, disable_pausing=True,
            friendly_fire=True, item_mask_hex="fffffff80000000f")
        declaration["expected_game_rules"] = {"mode": 1, "stock_count": 4, "handicap": 0,
            "damage_ratio": 10, "stock_time_limit": 8, "friendly_fire": 1, "pause": 0}
        declaration["expected_item_preference_mask_hex"] = "0000000010000000"
        declaration["original_profile"]["first_gate"] = "exact competitive original menu through VS setup only"
        declaration["exclusions"] = ["active gameplay", "natural eight-minute outcome", "Results/CSS return",
                                     "pixels", "PCM", "live timing"]
        return declaration
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
    if version in (2, 3, 4, 5) and type(version) is int:
        declaration["version"] = version
        declaration["cold_original_context"] = {
            "port_rumble_preferences": [0, 0, 1, 1],
            "human_source_slots": [1, 2],
            "human_nametags": [120, 120],
            "port_mapping": "slot zero uses player index; otherwise slot minus one",
            "preparation": "original Options Rumble: P1/P2 off; P3/P4 untouched defaults",
        }
        if version in (3, 4, 5):
            del declaration["cold_original_context"]["human_source_slots"]
            declaration["cold_original_context"]["human_source_ports_zero_based"] = [0, 1]
        if version in (4, 5):
            from sd_gci_profile import GCI_SHA256, GAME_WRITTEN_SHA256
            declaration["original_profile"] = {
                "id": "game-written-save-browser-reexport-original-load",
                "gci_sha256": GCI_SHA256, "game_written_predecessor_sha256": GAME_WRITTEN_SHA256,
                "provenance": "browser re-export of original game-written save; historical fresh original load",
                "initial_port_rumble_preferences": [1, 1, 1, 1],
                "initial_characters_mask": "07ff", "initial_stages_mask": "07ff",
                "first_gate": "reduced Rules readiness and loaded context only; later settings preparation separate",
            }
        if version == 5:
            declaration["cold_original_context"].update(
                port_rumble_preferences=[1, 1, 1, 1],
                preparation="unchanged original loaded profile; unnamed human ports retain rumble on")
            declaration["original_profile"]["first_gate"] = "loaded Rules context, then original menus through neutral tied timeout and SD setup"
            declaration["expected_setup"]["disable_pausing"] = False
            for player, costume in zip(declaration["expected_setup"]["players"], (1, 0)):
                player.update(costume=costume, rumble_enabled=True)
    elif version != 1 or type(version) is not int:
        raise ValueError("Unsupported authored SD recipe version")
    return declaration


def validate_recipe(value):
    # Byte comparison also rejects bool-for-int and int-for-float substitutions.
    version = value.get("version") if isinstance(value, dict) else None
    if version not in (1, 2, 3, 4, 5, 6, 7, 8) or canonical(value) != canonical(recipe(version)):
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
    if recipe_version in (7, 8):
        witness = declaration["input_witness"]
        frames = [witness["press"], witness["release"]]
        controlled_ports = [port + 1 for port in witness["source_slots"]]
    else:
        frames = [[NEUTRAL_PAD, NEUTRAL_PAD, DISCONNECTED_PAD, DISCONNECTED_PAD]
                  for _ in range(STARTUP_TICKS + MATCH_TICKS)]
        controlled_ports = [1, 2]
    return validate_plan({
        "schema": SCHEMA, "version": AUTHORED_PLAN_VERSION, "policy": POLICY,
        "provenance": PROVENANCE, "authored_recipe": declaration,
        "authored_recipe_sha256": recipe_sha256(declaration),
        # Legacy plan-format coordinate; this is not an observed Ready duration.
        "first_frame": -123, "source_stage": 32,
        "source_characters": [18, 8] if recipe_version == 8 else [8, 8],
        "active_player_count": 2, "source_player_types": [0, 0],
        "controlled_ports": controlled_ports,
        "frames": frames,
    })
