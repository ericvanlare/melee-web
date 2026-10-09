#!/usr/bin/env python3
"""Owned cold-boot Rules gate or authored SD initialization prefix diagnostic.

Menu intents and bounded source predicates must be authored before launch. This runner
cannot synthesize native MWRI input, recover missed samples, or force a result.
No default menu recipe is guessed. The separate original experiment must first
verify its consumed neutral release and original Rules-ready source owner.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from authored_sd_reference_plan import canonical
from retail_input_plan import load_plan, NEUTRAL_PAD, pipe_commands
from reference_versus_sequence_capture import _atomic_ini, prepare_dual_pipe, DualPipeController, ObserverTail
from sd_reference_diagnostic import RulesMenuReceiver, GciRulesMenuReceiver, SdDiagnosticError, require
from sd_original_menu_plan import validate_packet, matches
from capture_retail_replay import dolphin_command, _copy_tree
from capture_allocation_history import validate_reference_build_manifest
from reference_observer_stream import read_status
from reference_input_stream import validate_status


def rules_dolphin_command(dolphin, user, disc):
    command = dolphin_command(Path(dolphin), Path(user), Path("unused"), Path(disc),
                              cpu="JITARM64", cold_boot=True, audible=False)
    return command + ["-p", "headless", "-C", "Session.Core.SaveDataWritable=False",
                      "-C", "Dolphin.Interface.ConfirmStop=False"]


def cleanup_process(process, output, scope="rules_ready"):
    """Stop and reap only this runner's direct Popen; always retain the outcome."""
    receipt = {"scope": scope, "pid": process.pid, "ownership": "direct-Popen",
               "terminate_sent": False, "kill_sent": False, "returncode": None, "error": None}
    try:
        if process.poll() is None:
            process.terminate()
            receipt["terminate_sent"] = True
        try:
            receipt["returncode"] = process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            receipt["kill_sent"] = True
            receipt["returncode"] = process.wait(timeout=5)
    except (OSError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    finally:
        (Path(output) / "cleanup.json").write_bytes(canonical(receipt))
    require(receipt["error"] is None and receipt["returncode"] is not None,
            "Rules probe direct process cleanup failed")
    return receipt


def wait_terminal_statuses(observer, native, deadline):
    """Two independently published writers; neither status stands in for the other."""
    while time.monotonic() < deadline:
        primary = read_status(observer) if Path(observer).is_file() else None
        inputs = validate_status(native, mode="record", require_complete=False) if Path(native).is_file() else None
        if primary:
            require(not primary["invalid"] and not primary["error"], "SD observer failed during finalization")
            require(primary["state"] != "completed", "SD observer unexpectedly completed a legacy capture")
        if primary and primary["state"] == "interrupted" and inputs and inputs["complete"]:
            return
        time.sleep(0.02)
    raise SdDiagnosticError("SD independent writer finalization deadline expired")


def menu_actions(path):
    raw = Path(path).read_bytes()
    require(len(raw) <= 1024 * 1024, "SD menu recipe exceeds its bound")
    value = json.loads(raw)
    if isinstance(value, dict) and value.get("version") in (2, 3, 4, 5, 6, 7):
        validate_packet(value)
        return value, hashlib.sha256(raw).hexdigest()
    require(isinstance(value, dict) and set(value) == {"schema", "version", "actions"} and
            value["schema"] == "melee-web-sd-original-menu-inputs" and value["version"] == 1,
            "SD original menu recipe schema differs")
    actions = value["actions"]
    require(isinstance(actions, list) and 1 <= len(actions) <= 256, "SD menu actions are unbounded")
    for action in actions:
        require(isinstance(action, dict) and set(action) == {"label", "scene", "p1", "p2", "polls", "settle_polls"},
                "SD menu action fields differ")
        require(isinstance(action["label"], str) and action["label"], "SD menu action has no label")
        require(type(action["scene"]) is int and 0 <= action["scene"] <= 255 and
                all(type(action[k]) is int and 1 <= action[k] <= 120 for k in ("polls", "settle_polls")),
                "SD menu polling bounds differ")
        pipe_commands(action["p1"])
        pipe_commands(action["p2"])
    require(actions[-1]["scene"] == 9, "SD final confirmation must originate in original SSS")
    return value, hashlib.sha256(raw).hexdigest()


def prepare_rules_profile(profile, user):
    """Customize only a fresh owned copy; retain the shared atomic writer's 0400 freeze."""
    profile = Path(profile)
    require(profile.is_dir(), "SD cold boot requires an existing original profile")
    source_inventory = {}
    for item in profile.rglob("*"):
        require(not item.is_symlink(), "SD profile must not redirect files")
        if item.is_file():
            source_inventory[item.relative_to(profile).as_posix()] = hashlib.sha256(item.read_bytes()).hexdigest()
    user = Path(user)
    require(not user.exists(), "Rules probe profile copy already exists")
    _copy_tree(profile, user, skip={"Pipes"})
    config = user / "Config" / "Dolphin.ini"
    require(config.is_file(), "SD cold boot requires Dolphin.ini")
    p1, p2 = prepare_dual_pipe(user)
    # Inactive source PAD ports must remain disconnected, not extra Pipe devices.
    import configparser
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(config)
    require(not ini.get("General", "GDBSocket", fallback="").strip(),
            "SD cold boot must not attach a debugger")
    for port in (2, 3):
        ini.set("Core", "SIDevice" + str(port), "0")
    _atomic_ini(config, ini)
    return p1, p2, source_inventory


def run(*, dolphin, disc, profile, input_plan, menu_recipe, output, build_manifest, timeout=180, gci=None):
    """Own fresh output before preparation so failures cannot vanish before launch."""
    output = Path(output)
    output.mkdir()  # A collision never overwrites another run or its evidence.
    scope = "rules_ready"
    try:
        scope = menu_actions(menu_recipe)[0].get("scope", scope)
        return _run(dolphin=dolphin, disc=disc, profile=profile, input_plan=input_plan,
                    menu_recipe=menu_recipe, output=output, build_manifest=build_manifest, timeout=timeout, gci=gci)
    except Exception as error:
        failure = output / "failure.json"
        if not failure.exists():
            failure.write_bytes(canonical({"scope": scope, "stage": "prelaunch",
                                           "native_launched": False, "error": str(error)}))
        raise


def _run(*, dolphin, disc, profile, input_plan, menu_recipe, output, build_manifest, timeout, gci=None):
    build = validate_reference_build_manifest(Path(build_manifest), Path(dolphin))
    plan, plan_hash = load_plan(input_plan, allow_authored=True)
    menus, menu_hash = menu_actions(menu_recipe)
    items_probe = menus["scope"] == "items_row_gci"
    full_route = menus["scope"] == "sd_prefix_gci" or items_probe
    guarded_items = menus["scope"] == "sd_prefix_gci" and menus["version"] == 7
    campaign = menus["scope"] in ("rules_ready_gci", "sd_prefix_gci", "items_row_gci")
    scope = menus["scope"]
    require(plan["authored_recipe"]["version"] == (5 if full_route else 4 if campaign else 3) and
            menus["version"] == (6 if items_probe else 7 if guarded_items else 5 if full_route else 4 if campaign else 2) and (gci is not None) == campaign and
            menus["authored_recipe_sha256"] == plan["authored_recipe_sha256"],
            "Runnable original diagnostic requires the exact current scoped recipe/menu versions")
    loaded_profile = None
    if campaign:
        from sd_gci_profile import prepare_gci_folder
        manifest_raw = Path(build_manifest).read_bytes()
        overlay = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"
        require(hashlib.sha256(manifest_raw).hexdigest() == build["sha256"] and
                json.loads(manifest_raw).get("observer_source_overlay_sha256", {}).get(
                    "Core/PowerPC/ReferenceCaptureObserver.cpp") == hashlib.sha256(overlay.read_bytes()).hexdigest(),
                "Loaded-profile observer producer is stale or unbound")
        loaded_profile, owned_gci = prepare_gci_folder(gci, output / "gci-folder")
    receiver = GciRulesMenuReceiver(plan, loaded_profile, full_route=full_route, items_probe=items_probe,
                                   guarded_items=guarded_items) if campaign else RulesMenuReceiver(plan)
    require(type(timeout) in (int, float) and 0 < timeout <= (180 if full_route else 600),
            "Original diagnostic deadline is unbounded")
    user = output / "user"
    p1, p2, source_inventory = prepare_rules_profile(profile, user)
    raw, status = output / "observer.bin", output / "observer-status.json"
    native, native_status = output / "inputs.mwri", output / "input-status.json"
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(("MWRC_", "DOLPHIN_", "SDL_"))}
    environment.update(MWRC_ENABLE="1", MWRC_CPU="JITARM64", MWRC_SOURCE_REV="GALE01r2",
                       MWRC_DOL_SHA256="dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                       MWRC_OUTPUT=str(raw), MWRC_STATUS=str(status), MWRC_SD_INIT="1",
                       MWRC_SD_RECIPE_SHA256=plan["authored_recipe_sha256"],
                       MWRC_SD_MENU_PROBE="items_row" if items_probe else "sd_prefix" if full_route else "rules_ready",
                       MWRC_INPUT_RECORD=str(native), MWRC_INPUT_STATUS=str(native_status))
    command = rules_dolphin_command(dolphin, user, disc)
    if campaign:
        environment["MWRC_SD_PROFILE_GCI_SHA256"] = loaded_profile["sha256"]
        command += ["-C", "Dolphin.Core.SlotA=8", "-C",
                    "Dolphin.Core.GCIFolderAPath=" + str(output / "gci-folder")]
    (output / "input-plan.json").write_bytes(canonical(plan))
    (output / "menu-recipe.json").write_bytes(canonical(menus))
    launch = {"scope": scope,
        "input_plan_sha256": plan_hash, "menu_recipe_sha256": menu_hash,
        "profile_sha256": source_inventory, "build": build, "command": command}
    if campaign:
        launch.update(profile_gci_sha256=loaded_profile["sha256"], owned_gci=str(owned_gci),
                      observed_prelaunch_config_modes={name: oct((user / "Config" / name).stat().st_mode & 0o777)
                         for name in ("Dolphin.ini", "GCPadNew.ini")})
    (output / "launch.json").write_bytes(canonical(launch))
    controller = DualPipeController(p1, p2, output / "input-intentions.jsonl")
    deadline = time.monotonic() + timeout
    with (output / "dolphin.log").open("xb") as log:
        process = subprocess.Popen(command, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            # The dedicated receiver expects an interrupted primary ending, so
            # do not use the whole-session Tail's completion-status policy.
            with ObserverTail(raw, None) as tail:
                def next_row():
                    row = tail.next(deadline)
                    receiver.accept(row)
                    return row
                def wait_source(predicate, label, max_polls):
                    first = receiver.menu_polls
                    while not predicate():
                        require(not receiver.ended and receiver.menu_polls - first < max_polls,
                                "Rules menu source predicate/cap failed: " + label)
                        next_row()
                def tap(action, label, max_polls):
                    before = receiver.menu_consumed
                    controller.set_both(action["p1"], action["p2"], action=label)
                    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                                [action["p1"], action["p2"]], label + ":consumed", max_polls)
                    before = receiver.menu_consumed
                    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label + ":release")
                    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                                [NEUTRAL_PAD] * 2, label + ":neutral-consumed", max_polls)
                boot_actions = 0
                while not matches(receiver.latest_menu, menus["actions"][0]["before"]):
                    require(receiver.menu_polls < menus["boot_max_polls"], "Rules cold startup polling cap")
                    next_row()
                    scene = (receiver.latest_menu or {}).get("scene")
                    for action in menus["boot"]:
                        if scene == action["scene"]:
                            boot_actions += 1
                            require(boot_actions <= menus["boot_max_actions"], "Rules cold startup action cap")
                            tap(action, "cold-scene-" + str(scene), 600)
                            # Consume a new poll before considering another boot action.
                            first = receiver.menu_polls
                            wait_source(lambda: receiver.menu_polls > first, "cold-source-poll", 600)
                            break
                for action in menus["actions"]:
                    wait_source(lambda: matches(receiver.latest_menu, action["before"]),
                                action["label"] + ":ready", action["max_polls"])
                    tap(action, action["label"], action["max_polls"])
                    wait_source(lambda: matches(receiver.latest_menu, action["after"]),
                                action["label"] + ":observed", action["max_polls"])
                if full_route and not items_probe:
                    drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap)
                while not receiver.ended:
                    next_row()
                # MWRO End is flushed before the writer publishes final status.
                # Do not treat that publication race as native completion.
                wait_terminal_statuses(status, native_status, deadline)
                report = receiver.finish(status, native, native_status)
                (output / "report.json").write_bytes(canonical(report))
                return report
        except Exception as error:
            (output / "failure.json").write_bytes(canonical({"scope": scope, "stage": "native",
                "native_launched": True, "pid": process.pid, "error": str(error)}))
            raise
        finally:
            try:
                cleanup_process(process, output, scope=scope)
            except SdDiagnosticError as error:
                failure = output / "failure.json"
                if not failure.exists():
                    failure.write_bytes(canonical({"scope": scope, "stage": "cleanup",
                        "native_launched": True, "pid": process.pid, "error": str(error)}))
                raise


def drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap):
    """Reuse the original Pipe driver's bounded cursor/door/highlight policy.

    All axes, targets and limits are frozen in the canonical packet. Observer
    input events prove actual consumption; host sleep never proves readiness.
    This is an initialization prefix, with no Results/resolution continuation.
    """
    from reference_versus_sequence_capture import raw_pad
    policy = menus["css"]
    def pair(port, pad):
        pads = [NEUTRAL_PAD] * 2
        pads[port] = pad
        return {"p1": pads[0], "p2": pads[1]}
    def neutral(label):
        before = receiver.menu_consumed
        controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label)
        wait_source(lambda: receiver.menu_consumed > before and
                    receiver.last_pad[:2] == [NEUTRAL_PAD]*2, label, 600)
    def move(port, point, label):
        first = receiver.menu_polls
        while True:
            require(receiver.css is not None and receiver.menu_polls-first < policy["max_move_polls"],
                    "CSS cursor owner/movement cap: " + label)
            cursor = receiver.css["cursors"][port]
            dx, dy = point[0]-cursor["x"], point[1]-cursor["y"]
            if abs(dx) < policy["tolerance"] and abs(dy) < policy["tolerance"]:
                neutral(label + ":neutral")
                stable, previous = 0, receiver.css["cursors"][port].copy()
                while stable < policy["stable_cursor_polls"]:
                    before = receiver.menu_polls
                    wait_source(lambda: receiver.menu_polls > before, label+":settle", 600)
                    require(receiver.css is not None, "CSS cursor lost while settling")
                    current = receiver.css["cursors"][port]
                    stable = stable+1 if all(abs(current[k]-previous[k]) < 0.02 for k in ("x", "y")) else 0
                    previous = current.copy()
                    require(receiver.menu_polls-first < policy["max_move_polls"], "CSS settle cap")
                if all(abs(point[i]-previous[k]) < policy["tolerance"] for i,k in enumerate(("x","y"))):
                    return
                continue
            def axis(delta):
                return 0 if abs(delta) < 0.5 else (70 if abs(delta)>5 else 35)*(1 if delta>0 else -1)
            intent = pair(port, raw_pad(x=axis(dx), y=axis(dy)))
            before = receiver.menu_polls
            controller.set_both(intent["p1"], intent["p2"], action=label)
            wait_source(lambda: receiver.menu_polls > before, label+":cursor", 600)
    wait_source(lambda: receiver.css is not None, "CSS constructor-owned inventory", 600)
    require([p["kind"] for p in receiver.css["players"]] == [0, 0], "CSS requires two original humans")
    for port, costume in enumerate(policy["costumes"]):
        move(port, policy["point"], f"Mario-P{port+1}")
        tap(pair(port, raw_pad(buttons=["A"])), f"Mario-place-P{port+1}", 600)
        wait_source(lambda: receiver.css["players"][port]["character"] == policy["character"], "Mario selected", 600)
        if receiver.css["doors"][port]["costume"] != costume:
            model = receiver.css["models"][port]
            move(port, (model["x"]-2, model["y"]+1.6), "pickup-human-puck")
            tap(pair(port, raw_pad(buttons=["A"])), "pickup-human-puck", 600)
            wait_source(lambda: receiver.css["cursors"][port]["state"] == 1 and
                        receiver.css["cursors"][port]["held"] == port, "held human puck", 600)
            move(port, policy["point"], "Mario-costume-hover")
            wait_source(lambda: receiver.css["doors"][port]["icon"] == policy["icon"], "Mario icon", 600)
            for attempt in range(policy["max_costume_taps"]):
                if receiver.css["doors"][port]["costume"] == costume:
                    break
                before = receiver.css["doors"][port]["costume"]
                tap(pair(port, raw_pad(buttons=["X"])), "Mario-costume", 600)
                wait_source(lambda: receiver.css["doors"][port]["costume"] != before, "costume changed", 600)
            require(receiver.css["doors"][port]["costume"] == costume, "Mario costume cap")
            tap(pair(port, raw_pad(buttons=["A"])), "place-colored-Mario", 600)
        wait_source(lambda: receiver.css["cursors"][port]["state"] != 1, "human puck placed", 600)
    require([p["character"] for p in receiver.css["players"]] == [8,8] and
            [d["costume"] for d in receiver.css["doors"]] == policy["costumes"], "CSS final lineup differs")
    before = receiver.menu_polls
    wait_source(lambda: receiver.menu_polls >= before+policy["idle_polls_before_start"], "CSS source idle", 600)
    tap(pair(0, raw_pad(buttons=["START"])), "CSS-start-SSS", 600)
    wait_source(lambda: receiver.stage is not None, "SSS constructor-owned readiness", 600)
    stage = menus["sss"]
    before = receiver.menu_polls
    wait_source(lambda: receiver.menu_polls >= before+stage["initial_idle_polls"], "SSS source idle", 600)
    if receiver.stage["kind"] != stage["stage_kind"]:
        before = receiver.menu_polls
        controller.set_both(raw_pad(x=stage["column_x"]), NEUTRAL_PAD, action="FD-column")
        wait_source(lambda: receiver.menu_polls >= before+stage["column_polls"], "FD column", 600)
        neutral("FD-column-neutral")
        controller.set_both(raw_pad(y=stage["scan_y"]), NEUTRAL_PAD, action="FD-scan-up")
        wait_source(lambda: receiver.stage["kind"] == stage["stage_kind"], "FD highlight", stage["max_scan_polls"])
        neutral("FD-highlight-neutral")
    wait_source(lambda: receiver.stage["kind"] == 32 and receiver.stage["cooldown"] == 0,
                "FD original confirmation predicate", 600)
    # Release immediately after observed menu consumption. If an A sample
    # reaches VS instead, the unchanged native/receiver neutral checks fail.
    before = receiver.menu_consumed
    controller.set_both(raw_pad(buttons=["A"]), NEUTRAL_PAD, action="choose-FD")
    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                [raw_pad(buttons=["A"]), NEUTRAL_PAD], "choose-FD:consumed", 600)
    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action="choose-FD:release")


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "profile", "input-plan", "menu-recipe", "output", "build-manifest"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--gci", type=Path, help="Exact retained re-export; required only by the separate GCI campaign")
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(argv)
    try:
        report = run(**vars(args))
    except (OSError, ValueError) as error:
        parser.exit(1, f"Original menu/SD diagnostic failed: {error}\n")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
