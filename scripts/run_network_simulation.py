#!/usr/bin/env python3
"""Run reviewed simulated-network scenarios serially under the capture owner.

Invoke through agent_workspace.py run. This does not build a runtime, retry a
failure, start public services, or promote simulated traffic to Internet evidence.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
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
    flags = []
    if full_route:
        flags.append("--full-route")
    if action_probe:
        flags.append("--gameplay-action-probe")
    if combat_probe:
        flags.append("--gameplay-combat-probe")
    if combat_exchange:
        flags.append("--gameplay-combat-exchange")
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


def validate_result(returncode, owner, browser, full_route, gameplay_action_probe=False,
                    repeat_full_route=False, *, controlled_stall=False,
                    gameplay_combat_probe=False, gameplay_combat_exchange=False):
    """Require both independent owners; each opt-in route has its own policy."""
    if gameplay_action_probe and gameplay_combat_probe:
        raise ValueError("gameplay action and combat probes are separate scenarios")
    if gameplay_combat_exchange and not gameplay_combat_probe:
        raise ValueError("gameplay combat exchange requires the existing combat probe prefix")
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
        if repeat_full_route:
            validate_repeat_route(route, browser.get("checksum_comparison"))
        elif route.get("repeat_mode") is not None or route.get("repeated_matches") is not None:
            raise ValueError("baseline unexpectedly contains repeated full routes")
    elif repeat_full_route or gameplay_action_probe or gameplay_combat_probe or gameplay_combat_exchange:
        raise ValueError("opt-in route scenario did not request a full route")

    if controlled_stall:
        probe = network.get("post_css_stall_probe") or {}
        if (probe.get("outcome") != "EXERCISED_AND_RESUMED" or
                probe.get("policy_result", {}).get("policy") != "controlled-post-css-stall-exercised" or
                probe.get("policy_result", {}).get("result_is_not_clean_route_pass") is not True):
            raise ValueError("controlled-stall probe did not satisfy its separate result policy")


def _nonnegative_int(value):
    return isinstance(value, int) and not isinstance(value, bool) and value >= 0


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
                    if name in {"full-moderate-combat-probe", "full-moderate-combat-exchange"}]
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
                   "controlled_stall_probe": controlled_stall,
                   "controlled_cut_probe": controlled_cut,
                   "result_policy": "expected-fault-cut" if controlled_cut else
                       "controlled-stall-probe" if controlled_stall else
                       "two-match-repeat" if repeat_full_route else
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
            if controlled_cut:
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
                                gameplay_combat_exchange=gameplay_combat_exchange)
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
