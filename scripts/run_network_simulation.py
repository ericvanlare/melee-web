#!/usr/bin/env python3
"""Run reviewed simulated-network scenarios serially under the capture owner.

Invoke through agent_workspace.py run. This does not build a runtime, retry a
failure, start public services, or promote simulated traffic to Internet evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
import re
from pathlib import Path
import subprocess
import sys

from check_gameplay import node_runtime
from a3_expected_fault_auditor import audit_expected_cut

ROOT = Path(__file__).resolve().parents[1]
SCENARIOS = {
    "css-moderate": ("moderate", False, False, False, False, False, False),
    "full-moderate": ("moderate", True, False, False, False, False, False),
    "full-moderate-action-probe": ("moderate", True, False, False, True, False, False),
    "full-moderate-combat-probe": ("moderate", True, False, False, False, False, True),
    "full-moderate-combat-exchange": ("moderate", True, False, False, False, False, True, True),
    "full-moderate-combat-ko": ("moderate", True, False, False, False, False, True, True, True),
    "full-moderate-combat-ko-charged": ("moderate", True, False, False, False, False, True, True, False, True),
    "full-moderate-combat-ko-prepared": ("moderate", True, False, False, False, False, True, True, False, False, True),
    "full-moderate-combat-ko-prepared-platform-exit": ("moderate", True, False, False, False, False, True, True, False, False, True, True),
    "full-moderate-repeat": ("moderate", True, False, False, False, True, False),
    "full-asymmetric": ("asymmetric", True, False, False, False, False, False),
    "css-constrained": ("constrained", False, False, False, False, False, False),
    "css-constrained-stall": ("constrained", False, True, False, False, False, False),
    "css-moderate-cut": ("moderate", False, False, True, False, False, False),
    "css-moderate-fresh": ("moderate", False, False, False, False, False, False),
}


def scenario_route_flags(name):
    """Return only the route flags owned by one named capture scenario."""
    scenario = SCENARIOS[name]
    _, full_route, controlled_stall, controlled_cut, action_probe, repeat_route, combat_probe = scenario[:7]
    combat_exchange = len(scenario) > 7 and scenario[7]
    combat_ko = len(scenario) > 8 and scenario[8]
    combat_ko_charged = len(scenario) > 9 and scenario[9]
    combat_ko_prepared = len(scenario) > 10 and scenario[10]
    combat_ko_prepared_platform_exit = len(scenario) > 11 and scenario[11]
    flags = []
    if full_route:
        flags.append("--full-route")
    if action_probe:
        flags.append("--gameplay-action-probe")
    if combat_probe:
        flags.append("--gameplay-combat-probe")
    if combat_exchange:
        flags.append("--gameplay-combat-exchange")
    if combat_ko:
        flags.append("--gameplay-combat-ko")
    if combat_ko_charged:
        flags.append("--gameplay-combat-ko-charged")
    if combat_ko_prepared:
        flags.append("--gameplay-combat-ko-prepared")
    if combat_ko_prepared_platform_exit:
        flags.append("--gameplay-combat-ko-prepared-platform-exit")
    if repeat_route:
        flags.append("--repeat-full-route")
    if controlled_stall:
        flags.append("--post-css-stall-probe")
    if controlled_cut:
        flags.append("--controlled-cut-after-css")
    return flags


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n")
    temporary.replace(path)


def file_identity(path):
    return {"bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def runtime_identity(directory):
    required = ("runtime.html", "gameplay_menu_browser.wasm",
                "net_lockstep_browser_peer.mjs", "net_lockstep_core.mjs",
                "net_lockstep_websocket_relay.mjs")
    for name in required:
        if not (directory / name).is_file():
            raise ValueError(f"served runtime is missing {name}")
    return {str(path.relative_to(directory)): file_identity(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def validate_incident_recording(browser, *, controlled_stall=False):
    recording = browser.get("runtime_incident_recording") or {}
    if recording.get("requested") is not True:
        raise ValueError("runtime incident recording was not requested")
    for role in ("alpha", "beta"):
        evidence = (recording.get("profiles") or {}).get(role) or {}
        readiness = evidence.get("readiness") or {}
        selection = (readiness.get("ring_status") or {}).get("selection") or {}
        current = (evidence.get("export") or {}).get("current") or {}
        retained = (evidence.get("export") or {}).get("retained") or {}
        if (readiness.get("requested") is not True or readiness.get("export_available") is not True or
                selection.get("staging_diagnostics_requested") is not True or
                selection.get("staging_diagnostics_enabled") is not True or
                selection.get("staging_diagnostics_reason") != "explicit_loopback_opt_in" or
                current.get("schema") != "melee-web-runtime-diagnostics" or
                not isinstance(current.get("session_id"), str) or
                not isinstance(current.get("incidents"), list) or
                not isinstance(retained.get("records"), list) or evidence.get("export_error") or
                evidence.get("before_leave_error")):
            raise ValueError(f"{role} runtime incident recording evidence is incomplete")
        timing = [row for row in current["incidents"] if row.get("reason") in
                  ("simulation_debt", "audio_debt", "nonfinite_clock", "clock_regression")]
        if any(row.get("staging") is None for row in timing):
            raise ValueError(f"{role} H1 timing incident lacks staging evidence")
        stopped = evidence.get("before_leave") or {}
        message = stopped.get("native_message")
        timing_stop = (stopped.get("native_running") == 0 and isinstance(message, str) and
                       message.startswith("Paused after a timing disruption"))
        if (timing or timing_stop) and not controlled_stall:
            raise ValueError(f"{role} H1 timing pause retained; clean route is unproven")


def validate_result(returncode, owner, browser, full_route, gameplay_action_probe=False,
                    repeat_full_route=False, *, controlled_stall=False,
                    gameplay_combat_probe=False, gameplay_combat_exchange=False,
                    gameplay_combat_ko=False, gameplay_combat_ko_charged=False,
                    gameplay_combat_ko_prepared=False,
                    gameplay_combat_ko_prepared_platform_exit=False):
    """Require both independent owners; each opt-in route has its own policy."""
    if gameplay_action_probe and gameplay_combat_probe:
        raise ValueError("gameplay action and combat probes are separate scenarios")
    if gameplay_combat_exchange and not gameplay_combat_probe:
        raise ValueError("gameplay combat exchange requires the existing combat probe prefix")
    if gameplay_combat_ko and not gameplay_combat_exchange:
        raise ValueError("combat KO requires verified combat exchange mode")
    if gameplay_combat_ko_charged and not gameplay_combat_exchange:
        raise ValueError("charged combat KO requires verified combat exchange mode")
    if gameplay_combat_ko_prepared and not gameplay_combat_exchange:
        raise ValueError("prepared combat KO requires verified combat exchange mode")
    if gameplay_combat_ko_prepared_platform_exit and not gameplay_combat_ko_prepared:
        raise ValueError("prepared platform-exit KO requires prepared combat KO mode")
    if sum((gameplay_combat_ko, gameplay_combat_ko_charged,
            gameplay_combat_ko_prepared)) > 1:
        raise ValueError("C-stick, charged, and prepared combat KO are separate scenarios")
    if returncode != 0:
        raise ValueError(f"capture owner exited {returncode}")
    if (owner.get("schema") != "melee-web-capture-process-owner-v3" or
            owner.get("result") != "pass" or owner.get("capture_exit_code") != 0 or
            owner.get("cleanup_passed") is not True or
            owner.get("process_observation_passed") is not True):
        raise ValueError("capture owner did not verify successful cleanup and observation")
    expected_decision = "PASS_POST_CSS_STALL_PROBE" if controlled_stall else "PASS"
    if (browser.get("schema") != "melee-web-a3-local-room-keyboard-v1" or
            browser.get("decision") != expected_decision or browser.get("first_error") is not None or
            browser.get("relay_mode") != "simulated-network"):
        raise ValueError("browser did not pass the requested simulated-network result policy")
    validate_incident_recording(browser, controlled_stall=controlled_stall)
    for mode_report in [browser, browser.get("full_route") or {}]:
        if gameplay_combat_ko and mode_report.get("gameplay_combat_ko") is not True:
            raise ValueError("requested combat KO mode is missing")
        if not gameplay_combat_ko and mode_report.get("gameplay_combat_ko", False) is not False:
            raise ValueError("unexpected combat KO mode")
        if gameplay_combat_ko_charged and mode_report.get("gameplay_combat_ko_charged") is not True:
            raise ValueError("requested charged combat KO mode is missing")
        if not gameplay_combat_ko_charged and mode_report.get("gameplay_combat_ko_charged", False) is not False:
            raise ValueError("unexpected charged combat KO mode")
        if gameplay_combat_ko_prepared and mode_report.get("gameplay_combat_ko_prepared") is not True:
            raise ValueError("requested prepared combat KO mode is missing")
        if not gameplay_combat_ko_prepared and mode_report.get("gameplay_combat_ko_prepared", False) is not False:
            raise ValueError("unexpected prepared combat KO mode")
        if (mode_report.get("gameplay_combat_ko_prepared_platform_exit", False) is not
                gameplay_combat_ko_prepared_platform_exit):
            raise ValueError("prepared platform-exit mode differs from the requested scenario")
    network = browser.get("network_simulation") or {}
    if (network.get("proxy_cleanup", {}).get("verified") is not True or
            network.get("proxy_cleanup", {}).get("final") is not True):
        raise ValueError("browser did not verify final proxy integrity and retirement")
    if full_route and (browser.get("full_route") or {}).get("result") != "pass":
        raise ValueError("full route is missing or incomplete")
    if browser.get("gameplay_action_probe") is not gameplay_action_probe:
        raise ValueError("browser gameplay action mode differs from the requested scenario")
    if browser.get("gameplay_combat_probe") is not gameplay_combat_probe:
        raise ValueError("browser gameplay combat mode differs from the requested scenario")
    if gameplay_combat_exchange and browser.get("gameplay_combat_exchange") is not True:
        raise ValueError("browser gameplay combat exchange mode must be explicitly true")
    if (not gameplay_combat_exchange and "gameplay_combat_exchange" in browser and
            browser.get("gameplay_combat_exchange") is not False):
        raise ValueError("browser gameplay combat exchange mode differs from the requested scenario")
    if full_route:
        route = browser["full_route"]
        if route.get("gameplay_action_probe") is not gameplay_action_probe:
            raise ValueError("full route gameplay action mode differs from the requested scenario")
        if route.get("gameplay_combat_probe") is not gameplay_combat_probe:
            raise ValueError("full route gameplay combat mode differs from the requested scenario")
        if gameplay_combat_exchange and route.get("gameplay_combat_exchange") is not True:
            raise ValueError("full route gameplay combat exchange mode must be explicitly true")
        if (not gameplay_combat_exchange and "gameplay_combat_exchange" in route and
                route.get("gameplay_combat_exchange") is not False):
            raise ValueError("full route combat exchange mode differs from the requested scenario")
        if gameplay_action_probe:
            if (route.get("action_prefix") or {}).get("result") != "pass":
                raise ValueError("requested gameplay action probe is incomplete")
            if route.get("combat_probe") is not None:
                raise ValueError("action scenario unexpectedly contains a combat probe")
        elif route.get("action_prefix") is not None:
            raise ValueError("baseline unexpectedly contains a gameplay action probe")
        if gameplay_combat_probe:
            if (route.get("combat_probe") or {}).get("result") != "pass":
                raise ValueError("requested gameplay combat probe is incomplete")
            if gameplay_combat_exchange:
                if ((route.get("combat_probe") or {}).get("combat_exchange") or {}).get("result") != "pass":
                    raise ValueError("requested gameplay combat exchange is incomplete")
            elif (route.get("combat_probe") or {}).get("combat_exchange") is not None:
                raise ValueError("combat-probe scenario unexpectedly contains a shield exchange")
            if route.get("action_prefix") is not None:
                raise ValueError("combat scenario unexpectedly contains an action probe")
        elif route.get("combat_probe") is not None:
            raise ValueError("scenario unexpectedly contains a gameplay combat probe")
        if gameplay_combat_ko:
            ko = (route.get("combat_probe") or {}).get("combat_ko") or {}
            if ko.get("result") != "pass" or (ko.get("handoff") or {}).get("starting_stocks") != 3:
                raise ValueError("requested combat KO handoff is incomplete")
        elif gameplay_combat_ko_charged:
            ko = (route.get("combat_probe") or {}).get("combat_ko") or {}
            if (ko.get("result") != "pass" or (ko.get("handoff") or {}).get("starting_stocks") != 3 or
                    (ko.get("charge_animation_evidence") or {}).get("kind") !=
                    "observed-s4s60-charge-animation-plateau-and-release-advance-v1"):
                raise ValueError("requested charged combat KO handoff or animation evidence is incomplete")
        elif gameplay_combat_ko_prepared:
            ko = (route.get("combat_probe") or {}).get("combat_ko") or {}
            prep = ko.get("preparation") or {}
            prepared_baseline = prep.get("prepared_ko_baseline")
            ko_baseline = ko.get("baseline")
            if (ko.get("result") != "pass" or ko.get("input_mode") != "cstick-left" or
                    (ko.get("handoff") or {}).get("starting_stocks") != 3 or
                    not isinstance(prepared_baseline, dict) or
                    ko_baseline != prepared_baseline or
                    ko.get("source_cursor_limit") != prepared_baseline.get("source_tick", -1) + 480 or
                    prep.get("result") != "pass" or
                    prep.get("literal_input_window", {}).get("status") != "validated" or
                    not _nonnegative_int(prep.get("jab_count")) or prep["jab_count"] > 8 or
                    isinstance(prep.get("final_p1_damage_percent"), bool) or
                    not isinstance(prep.get("final_p1_damage_percent"), (int, float)) or
                    not math.isfinite(prep["final_p1_damage_percent"]) or
                    prep["final_p1_damage_percent"] < 18 or
                    prep.get("max_jabs") != 8 or prep.get("actual_damage_goal_percent") != 18 or
                    prep.get("max_approach_taps") != 16 or
                    not isinstance((prep.get("approach") or {}).get("steps"), list) or
                    len(prep["approach"]["steps"]) > 16 or
                    prep.get("max_source_cursors") != 480 or
                    not _nonnegative_int(prep.get("source_cursor_start")) or
                    not _nonnegative_int(prep.get("source_cursor_limit")) or
                    (prep.get("baseline") or {}).get("source_tick") != prep.get("source_cursor_start") or
                    prep["source_cursor_limit"] != prep["source_cursor_start"] + 480 or
                    not _nonnegative_int(prep.get("source_cursor_span")) or
                    prep["source_cursor_span"] > 480 or
                    prep.get("source_cursor_end") != (prep.get("prepared_ko_baseline") or {}).get("source_tick") or
                    not _prepared_ko_baseline_ok(prep) or
                    prep["source_cursor_span"] !=
                    prep["prepared_ko_baseline"]["source_tick"] - prep["source_cursor_start"] or
                    prep["prepared_ko_baseline"]["source_tick"] > prep["source_cursor_limit"]):
                raise ValueError("prepared damage proof or unchanged uncharged C-stick KO handoff is incomplete")
            if gameplay_combat_ko_prepared_platform_exit:
                if not _prepared_platform_exit_receipt_ok(ko):
                    raise ValueError("prepared post-credit platform-exit policy, actual Fall, or grounded PAD handoff is incomplete")
            elif ko.get("platform_exit") is not None or (ko.get("handoff") or {}).get("input_policy") is not None:
                raise ValueError("legacy neutral prepared KO cannot be relabeled as platform-exit evidence")
        elif (route.get("combat_probe") or {}).get("combat_ko") is not None:
            raise ValueError("unexpected combat KO receipt")
        if repeat_full_route:
            validate_repeat_route(route, browser.get("checksum_comparison"))
        elif route.get("repeat_mode") is not None or route.get("repeated_matches") is not None:
            raise ValueError("baseline unexpectedly contains repeated full routes")
    elif (repeat_full_route or gameplay_action_probe or gameplay_combat_probe or
          gameplay_combat_exchange or gameplay_combat_ko or gameplay_combat_ko_charged or
          gameplay_combat_ko_prepared or gameplay_combat_ko_prepared_platform_exit):
        raise ValueError("opt-in route scenario did not request a full route")

    if controlled_stall:
        probe = network.get("post_css_stall_probe") or {}
        if (probe.get("outcome") != "EXERCISED_AND_RESUMED" or
                probe.get("policy_result", {}).get("policy") != "controlled-post-css-stall-exercised" or
                probe.get("policy_result", {}).get("result_is_not_clean_route_pass") is not True):
            raise ValueError("controlled-stall probe did not satisfy its separate result policy")


def _nonnegative_int(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


def _prepared_platform_exit_receipt_ok(ko):
    """Bind the opt-in route label to its actual gate, PAD episode, Fall and landing."""
    if not isinstance(ko, dict):
        return False
    exit_receipt = ko.get("platform_exit")
    if not isinstance(exit_receipt, dict):
        return False
    gate = exit_receipt.get("gate")
    prefix = exit_receipt.get("pre_keydown_prefix")
    prefix_readbacks = exit_receipt.get("pre_keydown_readbacks")
    action = exit_receipt.get("action")
    action_record = exit_receipt.get("action_record")
    fall = exit_receipt.get("fall")
    handoff = ko.get("handoff")
    sequence = ko.get("sequence")
    if not isinstance(sequence, dict):
        return False
    loss = sequence.get("loss")
    if not all(isinstance(value, dict) for value in
               (gate, prefix, prefix_readbacks, action, action_record, fall, handoff, loss)):
        return False
    sample = action_record.get("observed_pad_sample")
    window = handoff.get("input_window")
    delay = ko.get("input_delay")
    baseline = ko.get("baseline")
    if not isinstance(sample, dict) or not isinstance(window, dict) or not isinstance(baseline, dict):
        return False
    if (ko.get("input_policy") != "post-credit-platform-exit" or
            exit_receipt.get("result") != "pass" or
            prefix_readbacks.get("accepted") is not True or
            prefix.get("kind") != "validated-pre-keydown-platform-exit-neutral-prefix-v1" or
            handoff.get("kind") != "validated-post-credit-platform-exit-combat-ko-handoff-v1" or
            handoff.get("input_policy") != "post-credit-platform-exit" or
            handoff.get("starting_stocks") != 3 or
            gate.get("kind") != "validated-source-credited-rebirth-wait-platform-gate-v1" or
            loss.get("kind") != "validated-source-credited-combat-ko-loss-v1" or
            gate.get("loss_source_tick") != loss.get("source_tick") or
            not _nonnegative_int(delay) or
            not _nonnegative_int(baseline.get("source_tick")) or
            ko.get("source_cursor_limit") != baseline["source_tick"] + 480 or
            not _nonnegative_int(gate.get("source_tick")) or
            not _nonnegative_int(loss.get("source_tick")) or
            not (loss["source_tick"] < gate["source_tick"] <= ko["source_cursor_limit"])):
        return False

    facing = gate.get("facing_direction")
    expected_key = "a" if facing == -1 else "d" if facing == 1 else None
    position_policy = gate.get("position_admission_policy") or {}
    positions = gate.get("position_by_peer")
    if (expected_key is None or gate.get("direction_key") != expected_key or
            position_policy.get("min_x") != -64 or position_policy.get("max_x") != 64 or
            position_policy.get("min_y") != 32 or position_policy.get("max_y") != 64 or
            position_policy.get("policy") !=
            "conservative scenario admission only; not authored Final Destination geometry or landing proof" or
            not isinstance(positions, list) or len(positions) != 2 or positions[0] != positions[1]):
        return False
    position = positions[0]
    if not isinstance(position, dict):
        return False
    x, y = position.get("x"), position.get("y")
    if (isinstance(x, bool) or isinstance(y, bool) or
            not isinstance(x, (int, float)) or not isinstance(y, (int, float)) or
            not math.isfinite(x) or not math.isfinite(y) or
            not (-64 <= x <= 64 and 32 <= y <= 64)):
        return False

    rise = window.get("p1_axis_rise_input_tick")
    last_held = window.get("p1_axis_last_held_input_tick")
    release = window.get("p1_neutral_release_input_tick")
    held_count = window.get("p1_axis_held_row_count")
    start = window.get("start_input_tick")
    gate_input = gate["source_tick"] - delay - 1
    if (window.get("p1_axis") != "left_x" or window.get("facing_direction") != facing or
            window.get("direction_key") != expected_key or window.get("gate_source_tick") != gate["source_tick"] or
            window.get("gate_input_tick") != gate_input or not _nonnegative_int(start) or
            not _nonnegative_int(rise) or
            not _nonnegative_int(last_held) or not _nonnegative_int(release) or
            not isinstance(held_count, int) or isinstance(held_count, bool) or
            not (1 <= held_count <= 30) or last_held - rise + 1 != held_count or
            release != last_held + 1 or rise <= gate_input or
            window.get("p1_neutral_prefix_row_count") != gate_input - start + 1):
        return False
    end = window.get("through_grounded_wait_input_tick")
    respawn_tick = handoff.get("respawn_source_tick")
    fall_tick = fall.get("source_tick")
    if (not _nonnegative_int(start) or not _nonnegative_int(end) or
            not _nonnegative_int(respawn_tick) or not _nonnegative_int(fall_tick) or
            start != baseline["source_tick"] - delay - 1 or
            not (baseline["source_tick"] < loss["source_tick"] < gate["source_tick"] <=
                 ko["source_cursor_limit"]) or
            end + delay + 1 != respawn_tick or
            not (gate["source_tick"] < fall_tick < respawn_tick) or
            fall.get("kind") != "validated-post-platform-exit-source-fall-v1" or
            fall.get("action_input_tick") != rise or
            fall.get("action_consumed_source_tick") != rise + delay + 1 or
            fall.get("input_delay") != delay or
            rise + delay + 1 > fall_tick or
            window.get("input_delay") != delay):
        return False
    bytes_sample = sample.get("bytes")
    if (action_record.get("source_input_tick") != rise or
            action.get("policy") != "one-contiguous-horizontal-only-actual-input-episode" or
            action.get("hold_ms") != 0 or action.get("observe_press") is not True or
            action.get("callback_limit_ms") != 500 or
            action.get("maximum_held_pad_rows") != 30 or
            sample.get("input_tick") != rise or sample.get("local_port") != 0 or
            not isinstance(bytes_sample, list) or len(bytes_sample) != 11 or
            any(not isinstance(value, int) or isinstance(value, bool) or not 0 <= value <= 255
                for value in bytes_sample) or
            any(value != 0 for index, value in enumerate(bytes_sample) if index != 2)):
        return False
    signed_axis = bytes_sample[2] - 256 if bytes_sample[2] > 127 else bytes_sample[2]
    if signed_axis == 0 or (signed_axis > 0) != (facing > 0):
        return False

    p1_rows = window.get("p1_rows")
    p2_rows = window.get("p2_rows")
    if (not isinstance(p1_rows, list) or not isinstance(p2_rows, list) or
            not all(isinstance(row, dict) for row in p1_rows + p2_rows)):
        return False
    for rows, port in ((p1_rows, 0), (p2_rows, 1)):
        if len(rows) != end - start + 1:
            return False
        if any(row.get("input_tick") != start + offset or row.get("source_cursor") != start + offset or
               row.get("local_port") != port or not isinstance(row.get("bytes"), list) or
               len(row["bytes"]) != 11 or any(not isinstance(value, int) or isinstance(value, bool) or
               not 0 <= value <= 255 for value in row["bytes"])
               for offset, row in enumerate(rows)):
            return False
    p1_episode = [row for row in p1_rows if row["input_tick"] >= rise and
                  any(value != 0 for value in row["bytes"])]
    if (len(p1_episode) != held_count or
            any(row["input_tick"] <= gate_input or row["bytes"] != bytes_sample
                for row in p1_episode) or
            any(any(value != 0 for value in row["bytes"])
                for row in p1_rows if row["input_tick"] < rise or
                row["input_tick"] > last_held)):
        return False
    p2_rise = window.get("p2_cstick_left_rise_input_tick")
    p2_release = window.get("p2_cstick_left_neutral_release_input_tick")
    p2_held = window.get("p2_cstick_left_held_row_count")
    if (not _nonnegative_int(p2_rise) or not _nonnegative_int(p2_release) or
            not isinstance(p2_held, int) or isinstance(p2_held, bool) or
            p2_release - p2_rise != p2_held or p2_held < 1):
        return False
    for row in p2_rows:
        tick, values = row["input_tick"], row["bytes"]
        expected_cstick = tick >= p2_rise and tick < p2_release
        if expected_cstick:
            signed_cstick = values[4] - 256 if values[4] > 127 else values[4]
            if signed_cstick >= 0 or any(value != 0 for index, value in enumerate(values) if index != 4):
                return False
        elif any(values):
            return False
    return True


def _prepared_ko_baseline_ok(preparation):
    baseline = preparation.get("prepared_ko_baseline") or {}
    expected_damage = preparation.get("final_p1_damage_percent")
    if (not _nonnegative_int(baseline.get("source_tick")) or
            isinstance(expected_damage, bool) or
            not isinstance(expected_damage, (int, float)) or not math.isfinite(expected_damage) or
            baseline.get("stocks_by_port") != [4, 4] or
            not isinstance(baseline.get("source_player_index_by_port"), list) or
            len(baseline["source_player_index_by_port"]) != 2 or
            not all(_nonnegative_int(slot) and slot < 6
                    for slot in baseline["source_player_index_by_port"]) or
            len(set(baseline["source_player_index_by_port"])) != 2):
        return False
    peers = baseline.get("actual_matches")
    if not isinstance(peers, list) or len(peers) != 2:
        return False
    projections = []
    for match in peers:
        if not isinstance(match, dict) or not _nonnegative_int(match.get("frame")):
            return False
        players = match.get("players")
        if not isinstance(players, list) or len(players) != 2:
            return False
        by_port = {player.get("source_port"): player for player in players
                   if isinstance(player, dict)}
        if set(by_port) != {0, 1}:
            return False
        p1, p2 = by_port[0], by_port[1]
        numeric = (p1.get("damage_percent"), p1.get("x"), p2.get("x"),
                   p2.get("damage_percent"))
        if (not all(isinstance(value, (int, float)) and not isinstance(value, bool) and
                    math.isfinite(value) for value in numeric) or
                p1.get("damage_percent") != expected_damage or
                p1.get("source_player_index") != baseline["source_player_index_by_port"][0] or
                p2.get("source_player_index") != baseline["source_player_index_by_port"][1] or
                p1.get("motion") != 14 or p1.get("groundAir") != 0 or p1.get("stocks") != 4 or
                p1.get("facing_direction") != 1 or p2.get("motion") != 14 or
                p2.get("groundAir") != 0 or p2.get("stocks") != 4 or
                p2.get("facing_direction") != -1 or not (p1["x"] < p2["x"]) or
                not (0 < p2["x"] - p1["x"] <= 20) or p1["damage_percent"] < 18):
            return False
        projections.append((match.get("frame"), tuple(
            (player.get("source_port"), player.get("motion"), player.get("groundAir"),
             player.get("source_player_index"), player.get("stocks"),
             player.get("damage_percent"), player.get("x"),
             player.get("facing_direction")) for player in players)))
    return projections[0] == projections[1]


def _sha256_string(value):
    return isinstance(value, str) and re.fullmatch(r"[a-f0-9]{64}", value) is not None


def validate_repeat_checkpoint_input(input_row, port):
    """Validate the existing melee_web_input_message schema, not its history entry."""
    def neutral_pair(value):
        return isinstance(value, list) and len(value) == 2 and all(type(v) is int and v == 0 for v in value)
    def require(condition):
        if not condition:
            raise ValueError("repeated full route source input snapshot lacks the active neutral keyboard port or sample counter")
    require(isinstance(input_row, dict))
    for field in ("ready", "focused", "visible", "active"):
        require(type(input_row.get(field)) is int and input_row[field] == 1)
    require(input_row.get("error") == "" and type(input_row.get("keyboard_layout")) is int and
            input_row["keyboard_layout"] == 0 and _nonnegative_int(input_row.get("samples")) and
            input_row["samples"] > 0)
    for field in ("keyboard_requested_mask", "keyboard_active_mask", "physical_mask"):
        require(_nonnegative_int(input_row.get(field)))
    require(input_row["keyboard_requested_mask"] & (1 << port) != 0 and
            input_row["keyboard_active_mask"] & (1 << port) != 0 and
            input_row["physical_mask"] & (1 << port) == 0)
    pads = input_row.get("pads")
    require(isinstance(pads, list) and len(pads) == 4 and isinstance(pads[port], dict))
    pad = pads[port]
    require(pad.get("source") == "keyboard" and type(pad.get("err")) is int and pad["err"] == 0 and
            type(pad.get("buttons")) is int and pad["buttons"] == 0 and
            all(neutral_pair(pad.get(field)) for field in ("stick", "cstick", "triggers", "analog")) and
            isinstance(pad.get("clamped"), dict) and
            all(neutral_pair(pad["clamped"].get(field)) for field in ("stick", "cstick", "triggers")))


def validate_repeat_route(route, checksum_comparison):
    """Require two retained same-room routes and a common raw checksum prefix."""
    def require(condition, message):
        if not condition:
            raise ValueError("repeated full route " + message)

    require(route.get("repeat_mode") == "same-room-two-match",
            "mode is missing or unknown")
    matches = route.get("repeated_matches")
    require(isinstance(matches, list) and len(matches) == 2,
            "must retain exactly two route reports")
    first, second = matches
    screenshot_paths = set()
    for expected, entry in enumerate(matches, 1):
        require(entry.get("match_number") == expected and entry.get("result") == "pass",
                f"match {expected} did not pass")
        require((entry.get("route") or {}).get("result") == "pass",
                f"match {expected} route report is incomplete")
        artifact = entry.get("report_artifact") or {}
        require(artifact.get("file") == f"match-{expected}/report.json" and
                _nonnegative_int(artifact.get("bytes")) and artifact.get("bytes", 0) > 0 and
                _sha256_string(artifact.get("sha256")),
                f"match {expected} report artifact identity is missing")
        found_screenshots = []
        def visit_screenshots(value):
            if isinstance(value, dict):
                if {"file", "bytes", "sha256"}.issubset(value):
                    found_screenshots.append(value)
                for child in value.values():
                    visit_screenshots(child)
            elif isinstance(value, list):
                for child in value:
                    visit_screenshots(child)
        visit_screenshots((entry.get("route") or {}).get("screenshots"))
        require(found_screenshots, f"match {expected} has no retained screenshots")
        for screenshot in found_screenshots:
            filename = screenshot.get("file")
            require(isinstance(filename, str) and filename.startswith(f"match-{expected}/") and
                    filename not in screenshot_paths and _nonnegative_int(screenshot.get("bytes")) and
                    screenshot.get("bytes", 0) > 0 and _sha256_string(screenshot.get("sha256")),
                    f"match {expected} screenshot is not unique and bound")
            screenshot_paths.add(filename)
    first_route = first["route"]
    second_route = second["route"]
    first_boundary = first_route.get("required_checksum_boundary")
    second_boundary = second_route.get("required_checksum_boundary")
    require(_nonnegative_int(first_boundary) and first_boundary > 0 and
            _nonnegative_int(second_boundary) and second_boundary > first_boundary,
            "route checksum boundaries are invalid or nonmonotonic")
    require(route.get("required_checksum_boundary") == second_boundary,
            "final route boundary does not bind match two")

    first_return = first.get("first_return")
    second_entry = second.get("entry")
    second_return = second.get("second_return")
    require(isinstance(first_return, dict) and first_return.get("result") == "pass",
            "first-return checkpoint was not validated before match two")
    require(isinstance(second_entry, dict) and second_entry.get("result") == "pass" and
            isinstance(second_return, dict) and second_return.get("result") == "pass",
            "second entry/return checkpoints are incomplete")
    identity = first_return.get("room_identity")
    require(isinstance(identity, dict) and identity and
            second_entry.get("room_identity") == identity and
            second_return.get("room_identity") == identity,
            "owner/page/room identity changed across routes")
    for role, port in (("alpha", 0), ("beta", 1)):
        observations = [((checkpoint.get("peers") or {}).get(role) or {}).get("peer_identity")
                        for checkpoint in (first_return, second_entry, second_return)]
        require(all(isinstance(item, dict) and item.get("role") == role and
                    item.get("local_port") == port for item in observations),
                f"{role} owner role/port changed across routes")
        for checkpoint in (first_return, second_entry, second_return):
            input_row = ((checkpoint.get("peers") or {}).get(role) or {}).get("input_diagnostics") or {}
            validate_repeat_checkpoint_input(input_row, port)
        samples = [checkpoint["peers"][role]["input_diagnostics"]["samples"]
                   for checkpoint in (first_return, second_entry, second_return)]
        require(samples[0] <= samples[1] <= samples[2], f"{role} input sample counter regressed")

    prefix = first_return.get("common_raw_prefix")
    require(isinstance(prefix, dict) and prefix.get("result") == "pass" and
            prefix.get("source_tick_range") == [0, first_boundary - 1] and
            prefix.get("record_count") == first_boundary and
            prefix.get("bytes_per_record") == 64 and prefix.get("alpha_equals_beta") is True and
            isinstance(prefix.get("sha256_concatenated_records"), str) and
            _sha256_string(prefix["sha256_concatenated_records"]),
            "first match raw checksum prefix was not compared before match two")

    def peer_value(checkpoint, role, key):
        peer = (checkpoint.get("peers") or {}).get(role) or {}
        return peer.get(key)

    for role in ("alpha", "beta"):
        first_cursor = peer_value(first_return, role, "source_cursor")
        entry_cursor = peer_value(second_entry, role, "source_cursor")
        return_cursor = peer_value(second_return, role, "source_cursor")
        route_return_cursor = (second_route.get("returned_css", {}).get("source_cursor_by_peer", {})
                               .get(role))
        require(_nonnegative_int(first_cursor) and _nonnegative_int(entry_cursor) and
                _nonnegative_int(return_cursor) and _nonnegative_int(route_return_cursor) and
                entry_cursor >= first_cursor and return_cursor > entry_cursor and
                return_cursor >= route_return_cursor,
                f"{role} native source cursors did not advance monotonically through returned CSS")
        first_counts = peer_value(first_return, role, "checksum_counts") or {}
        entry_counts = peer_value(second_entry, role, "checksum_counts") or {}
        return_counts = peer_value(second_return, role, "checksum_counts") or {}
        for counter in ("retained", "local", "remote", "compared"):
            before = first_counts.get(counter)
            at_entry = entry_counts.get(counter)
            after = return_counts.get(counter)
            require(_nonnegative_int(before) and _nonnegative_int(at_entry) and
                    _nonnegative_int(after) and at_entry >= before and after >= at_entry,
                    f"{role} {counter} checksum counts did not advance monotonically")

    admission = route.get("admission") or {}
    duration = admission.get("first_route_duration_ms")
    remaining = admission.get("remaining_before_second_ms")
    subdeadline = admission.get("second_route_subdeadline_ms")
    elapsed = admission.get("second_route_elapsed_ms")
    final_reserve = admission.get("final_reserve_ms_at_second_return")
    require(_nonnegative_int(duration) and duration > 0 and
            first.get("route_elapsed_ms") == duration and second.get("route_elapsed_ms") == elapsed and
            _nonnegative_int(remaining) and remaining >= duration + 30000 and
            admission.get("required_remaining_ms") == duration + 30000 and
            _nonnegative_int(subdeadline) and subdeadline <= duration + 10000 and
            _nonnegative_int(elapsed) and elapsed <= subdeadline and
            _nonnegative_int(final_reserve) and final_reserve >= 20000,
            "measured route admission/subdeadline/final reserve failed")

    final_prefix = route.get("final_prefix") or {}
    require(final_prefix.get("result") == "pass" and
            final_prefix.get("raw_records_present_in_final_checksum_comparison") is True and
            _nonnegative_int(final_prefix.get("compared_source_ticks")) and
            final_prefix["compared_source_ticks"] >= second_boundary and
            final_prefix.get("first_boundary") == first_boundary and
            final_prefix.get("first_prefix_sha256") == prefix["sha256_concatenated_records"] and
            final_prefix.get("second_boundary") == second_boundary and
            _sha256_string(final_prefix.get("second_prefix_sha256")),
            "final exact checksum prefix does not bind both route boundaries")

    records = (checksum_comparison or {}).get("records")
    compared = (checksum_comparison or {}).get("compared_source_ticks")
    require(isinstance(records, list) and _nonnegative_int(compared) and
            compared >= second_boundary and len(records) >= second_boundary,
            "final raw checksum comparison does not cover match two")
    digest = hashlib.sha256()
    for tick in range(second_boundary):
        row = records[tick]
        left = row.get("alpha_raw_hex") if isinstance(row, dict) else None
        right = row.get("beta_raw_hex") if isinstance(row, dict) else None
        require(isinstance(row, dict) and _nonnegative_int(row.get("source_tick")) and
                row.get("source_tick") == tick and row.get("bytes") == 64 and
                isinstance(left, str) and re.fullmatch(r"[a-f0-9]{128}", left) is not None and
                left == right,
                f"final raw checksum record {tick} is missing, malformed, or differs by peer")
        digest.update(bytes.fromhex(left))
        if tick + 1 == first_boundary:
            require(digest.copy().hexdigest() == prefix["sha256_concatenated_records"] and
                    digest.copy().hexdigest() == final_prefix["first_prefix_sha256"],
                    "final raw records do not match the pre-second-match prefix")
    require(digest.hexdigest() == final_prefix["second_prefix_sha256"],
            "final raw records do not match the second-match prefix")


def validate_repeat_artifacts(browser_directory, route):
    """Bind per-match report and screenshot receipts to files written by the browser."""
    root = Path(browser_directory).resolve(strict=True)

    def verify_file(relative, expected):
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            raise ValueError("repeated full route artifact path escapes the browser output")
        candidate = (root / path).resolve(strict=True)
        try:
            candidate.relative_to(root)
        except ValueError as error:
            raise ValueError("repeated full route artifact path escapes the browser output") from error
        actual = file_identity(candidate)
        if actual != {"bytes": expected.get("bytes"), "sha256": expected.get("sha256")}:
            raise ValueError("repeated full route artifact hash differs from its receipt")

    for entry in route["repeated_matches"]:
        verify_file(entry["report_artifact"]["file"], entry["report_artifact"])
        def visit(value):
            if isinstance(value, dict):
                if {"file", "bytes", "sha256"}.issubset(value):
                    verify_file(value["file"], value)
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)
        visit(entry["route"].get("screenshots"))


def process_identities(owner):
    result = set()
    for row in owner.get("browser_process_inventory", []):
        identity = row.get("identity") if isinstance(row, dict) else None
        if not isinstance(identity, dict):
            continue
        result.add((identity.get("pid"), identity.get("start"), identity.get("command")))
    return result


def validate_fresh_session(previous_cut, current_owner, current_browser, current_folder):
    """Require a distinct ordinary CSS run after the cut owner has fully retired."""
    if previous_cut is None:
        raise ValueError("fresh CSS scenario requires an earlier audited cut in this invocation")
    prior_browser = previous_cut["browser"]
    prior_owner = previous_cut["owner"]
    prior_folder = previous_cut["folder"]
    if (prior_owner.get("cleanup_passed") is not True or
            prior_owner.get("capture_process_group_absent") is not True or
            prior_owner.get("server_process_group_absent") is not True):
        raise ValueError("cut session did not retire before fresh CSS")
    old_invite = prior_browser.get("invitation") or {}
    new_invite = current_browser.get("invitation") or {}
    if (not old_invite.get("room_id") or not new_invite.get("room_id") or
            old_invite.get("room_id") == new_invite.get("room_id") or
            not old_invite.get("invite_sha256") or
            old_invite.get("invite_sha256") == new_invite.get("invite_sha256")):
        raise ValueError("fresh CSS run reused the cut room identity or Host invite")
    if Path(current_folder).resolve() == Path(prior_folder).resolve():
        raise ValueError("fresh CSS run reused the cut scenario output/profile owner")
    old_processes = process_identities(prior_owner)
    new_processes = process_identities(current_owner)
    if not old_processes or not new_processes or old_processes & new_processes:
        raise ValueError("fresh CSS run did not establish a distinct observed Chrome process identity set")
    if current_browser.get("source", {}).get("head") != prior_browser.get("source", {}).get("head"):
        raise ValueError("fresh CSS run changed source identity after the cut")
    return {"decision": "FRESH_SESSION_CSS_PASS", "separate_capture_owner_invocation": True,
            "old_room_id": old_invite["room_id"], "new_room_id": new_invite["room_id"],
            "old_invite_sha256": old_invite["invite_sha256"],
            "new_invite_sha256": new_invite["invite_sha256"],
            "old_chrome_process_identities": len(old_processes),
            "new_chrome_process_identities": len(new_processes),
            "same_page_reconnect": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True,
                        help="new evidence directory on the verified external volume")
    parser.add_argument("--playwright", type=Path, required=True)
    parser.add_argument("--runtime-directory", type=Path,
                        default=ROOT / "build/browser-release")
    parser.add_argument("--browser-executable", type=Path,
                        default=Path("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"))
    parser.add_argument("--port", type=int, choices=[8787], default=8787,
                        help="the existing keyboard harness requires port 8787")
    parser.add_argument("--scenarios", nargs="+", choices=SCENARIOS,
                        default=["css-moderate", "full-moderate", "full-asymmetric", "css-constrained"])
    parser.add_argument("--plan-only", action="store_true",
                        help="write commands and identities without starting a server or browser")
    args = parser.parse_args()
    if len(set(args.scenarios)) != len(args.scenarios):
        parser.error("duplicate scenarios would repeat captures without a new hypothesis")
    combat_names = [name for name in args.scenarios
                    if name in {"full-moderate-combat-probe", "full-moderate-combat-exchange",
                                "full-moderate-combat-ko", "full-moderate-combat-ko-charged",
                                "full-moderate-combat-ko-prepared",
                                "full-moderate-combat-ko-prepared-platform-exit"}]
    if combat_names and len(args.scenarios) != 1:
        parser.error("each bounded combat scenario must run as its own single moderate scenario")
    task13_names = [name for name in args.scenarios
                    if name in {"css-moderate-cut", "css-moderate-fresh"}]
    if task13_names and task13_names not in (["css-moderate-cut"],
                                              ["css-moderate-cut", "css-moderate-fresh"]):
        parser.error("Task13 permits one moderate cut, optionally followed by one fresh CSS scenario")
    if task13_names and len(args.scenarios) != len(task13_names):
        parser.error("Task13 cut/recovery runs cannot be mixed with other scenarios")
    for path in (args.disc, args.playwright, args.browser_executable):
        if not path.exists():
            parser.error(f"required local input is unavailable: {path}")
    runtime = args.runtime_directory.resolve(strict=True)
    baseline = runtime_identity(runtime)
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=False)
    temporary = output / "tmp"
    temporary.mkdir()
    environment = dict(os.environ, TMPDIR=str(temporary),
                       MELEE_BROWSER_PATH=str(args.browser_executable.resolve()))
    head = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    report = {"schema": "melee-web-network-simulation-suite-v1",
              "scope": "same-machine simulated network; no public or acceptance claim",
              "source": head, "runtime_directory": str(runtime), "runtime_files": baseline,
              "runner": file_identity(Path(__file__)),
              "execution_environment": {key: environment[key]
                                        for key in ("TMPDIR", "MELEE_BROWSER_PATH")},
              "started_at": datetime.now(timezone.utc).isoformat(),
              "decision": "PLANNED" if args.plan_only else "RUNNING", "scenarios": []}
    report_path = output / "suite.json"
    node = str(node_runtime(ROOT))
    server_spec = output / "server-spec.json"
    audited_cut = None
    write_json(server_spec, {
        "command": [sys.executable, str(ROOT / "scripts/serve.py"),
                    "--directory", str(runtime), "--port", str(args.port)],
        "ready_url": f"http://127.0.0.1:{args.port}/runtime.html", "ready_timeout_ms": 10000})
    try:
        for name in args.scenarios:
            scenario = SCENARIOS[name]
            (profile, full_route, controlled_stall, controlled_cut,
             gameplay_action_probe, repeat_full_route, gameplay_combat_probe) = scenario[:7]
            gameplay_combat_exchange = len(scenario) > 7 and scenario[7]
            gameplay_combat_ko = len(scenario) > 8 and scenario[8]
            gameplay_combat_ko_charged = len(scenario) > 9 and scenario[9]
            gameplay_combat_ko_prepared = len(scenario) > 10 and scenario[10]
            gameplay_combat_ko_prepared_platform_exit = len(scenario) > 11 and scenario[11]
            folder = output / name
            folder.mkdir()
            command = [sys.executable, str(ROOT / "scripts/capture_owner_deadline.py"),
                       "--overall-timeout-ms", "300000", "--cleanup-reserve-ms", "30000",
                       "--report", str(folder / "owner.json"), "--log", str(folder / "capture.log"),
                       "--process-trace", str(folder / "browser-processes.jsonl"),
                       "--browser-executable", str(args.browser_executable.resolve()),
                       "--browser-report", str(folder / "browser/report.json"),
                       "--require-browser-process", "--require-cdp-attribution",
                       "--server-spec", str(server_spec), "--server-log", str(folder / "server.log"),
                       "--", node, str(ROOT / "scripts/a3_local_room_keyboard_browser.mjs"),
                       "--url", f"http://127.0.0.1:{args.port}/runtime.html",
                       "--disc", str(args.disc.resolve()), "--out", str(folder / "browser"),
                       "--playwright", str(args.playwright.resolve()), "--min-checksums", "120",
                       "--timeout-ms", "260000", "--network-profile", profile]
            command.extend(scenario_route_flags(name))
            row = {"name": name, "profile": profile, "full_route": full_route,
                   "gameplay_action_probe": gameplay_action_probe,
                   "gameplay_combat_probe": gameplay_combat_probe,
                   "gameplay_combat_exchange": gameplay_combat_exchange,
                   "gameplay_combat_ko": gameplay_combat_ko,
                   "gameplay_combat_ko_charged": gameplay_combat_ko_charged,
                   "gameplay_combat_ko_prepared": gameplay_combat_ko_prepared,
                   "gameplay_combat_ko_prepared_platform_exit": gameplay_combat_ko_prepared_platform_exit,
                   "controlled_stall_probe": controlled_stall,
                   "controlled_cut_probe": controlled_cut,
                   "result_policy": "expected-fault-cut" if controlled_cut else
                       "controlled-stall-probe" if controlled_stall else
                       "two-match-repeat" if repeat_full_route else
                       "bounded-charged-combat-ko" if gameplay_combat_ko_charged else
                       "bounded-prepared-platform-exit-combat-ko" if gameplay_combat_ko_prepared_platform_exit else
                       "bounded-prepared-combat-ko" if gameplay_combat_ko_prepared else
                       "bounded-combat-ko" if gameplay_combat_ko else
                       "bounded-combat-exchange" if gameplay_combat_exchange else
                       "bounded-combat-probe" if gameplay_combat_probe else "clean-route",
                   "command": command, "decision": "PLANNED"}
            if repeat_full_route:
                row["repeat_full_route"] = True
            report["scenarios"].append(row)
            write_json(report_path, report)
            if args.plan_only:
                continue
            if runtime_identity(runtime) != baseline:
                raise ValueError("served runtime changed between scenarios")
            row["decision"] = "RUNNING"
            write_json(report_path, report)
            with (folder / "supervisor.log").open("x") as log:
                result = subprocess.run(command, cwd=ROOT, env=environment,
                                        stdout=log, stderr=subprocess.STDOUT)
            row["exit_code"] = result.returncode
            for key, relative in (("owner", "owner.json"), ("browser", "browser/report.json")):
                path = folder / relative
                if path.is_file():
                    row[key + "_report"] = {"file": str(path.relative_to(output)), **file_identity(path)}
            owner = json.loads((folder / "owner.json").read_text())
            browser = json.loads((folder / "browser/report.json").read_text())
            row["failure_classification"] = browser.get("failure_classification")
            write_json(report_path, report)
            if controlled_cut:
                validate_incident_recording(browser)
                if result.returncode == 0 or owner.get("capture_exit_code") != result.returncode:
                    raise ValueError("expected-cut route did not retain the matching nonzero capture exit")
                audit = audit_expected_cut(owner, browser, expected_source_head=head,
                    expected_runtime_files=baseline,
                    expected_runtime_url=f"http://127.0.0.1:{args.port}/runtime.html")
                audit["trusted_runtime_files"] = baseline
                audit["input_reports"] = {
                    "owner": {"file": "owner.json", **file_identity(folder / "owner.json")},
                    "browser": {"file": "browser/report.json",
                                **file_identity(folder / "browser/report.json")},
                }
                audit["auditor"] = {"file": "scripts/a3_expected_fault_auditor.py",
                                     **file_identity(Path(__file__).with_name("a3_expected_fault_auditor.py"))}
                audit["runner"] = report["runner"]
                audit_path = folder / "expected-fault-audit.json"
                write_json(audit_path, audit)
                row["route_decision"] = "FAIL"
                row["expected_fault_audit"] = {"file": str(audit_path.relative_to(output)),
                                                **file_identity(audit_path)}
                row["decision"] = "EXPECTED_TRANSPORT_CUT_CONFIRMED"
                audited_cut = {"owner": owner, "browser": browser, "folder": folder}
            else:
                validate_result(result.returncode, owner, browser, full_route, gameplay_action_probe,
                                repeat_full_route, controlled_stall=controlled_stall,
                                gameplay_combat_probe=gameplay_combat_probe,
                                gameplay_combat_exchange=gameplay_combat_exchange,
                                gameplay_combat_ko=gameplay_combat_ko,
                                gameplay_combat_ko_charged=gameplay_combat_ko_charged,
                                gameplay_combat_ko_prepared=gameplay_combat_ko_prepared,
                                gameplay_combat_ko_prepared_platform_exit=
                                    gameplay_combat_ko_prepared_platform_exit)
                if repeat_full_route:
                    validate_repeat_artifacts(folder / "browser", browser["full_route"])
                if name == "css-moderate-fresh":
                    row["fresh_session_evidence"] = validate_fresh_session(
                        audited_cut, owner, browser, folder)
                    row["decision"] = "FRESH_SESSION_CSS_PASS"
                else:
                    row["decision"] = "PASS_POST_CSS_STALL_PROBE" if controlled_stall else "PASS"
            if runtime_identity(runtime) != baseline:
                raise ValueError("served runtime changed between scenarios")
            write_json(report_path, report)
        if args.plan_only:
            report["decision"] = "PLANNED"
        elif args.scenarios == ["css-moderate-cut"]:
            report["decision"] = "EXPECTED_TRANSPORT_CUT_CONFIRMED"
        elif args.scenarios == ["css-moderate-cut", "css-moderate-fresh"]:
            report["decision"] = "EXPECTED_TRANSPORT_CUT_CONFIRMED_FRESH_SESSION_CSS_PASS"
        else:
            report["decision"] = "PLANNED" if args.plan_only else (
                "PASS_WITH_CONTROLLED_STALL_PROBE" if any(
                    SCENARIOS[name][2] for name in args.scenarios) else "PASS")
    except Exception as error:
        report["decision"] = "FAIL"
        report["first_error"] = str(error)
        if report["scenarios"]:
            report["scenarios"][-1]["decision"] = "FAIL"
        return 1
    finally:
        report["finished_at"] = datetime.now(timezone.utc).isoformat()
        write_json(report_path, report)
        print(json.dumps({"decision": report["decision"], "report": str(report_path)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
