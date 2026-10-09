#!/usr/bin/env python3
"""Owned cold-boot SD prefix runner; opt-in diagnostic, never legacy admission.

Menu packets and polling counts must be authored before launch. This runner
cannot synthesize native MWRI input, recover missed samples, or force a result.
No default menu recipe is guessed. The separate original experiment must first
verify that its final menu release reaches the neutral source boundary.
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
from reference_versus_sequence_capture import prepare_dual_pipe, DualPipeController, ObserverTail
from sd_reference_diagnostic import Receiver, SdDiagnosticError, require
from capture_retail_replay import dolphin_command, _copy_tree
from capture_allocation_history import validate_reference_build_manifest
from reference_observer_stream import read_status
from reference_input_stream import validate_status


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


def run(*, dolphin, disc, profile, input_plan, menu_recipe, output, build_manifest, timeout=180):
    """Not called by legacy capture paths; caller supplies reviewed original inputs."""
    build = validate_reference_build_manifest(Path(build_manifest), Path(dolphin))
    plan, plan_hash = load_plan(input_plan, allow_authored=True)
    receiver = Receiver(plan)
    menus, menu_hash = menu_actions(menu_recipe)
    require(type(timeout) in (int, float) and 0 < timeout <= 600, "SD deadline is unbounded")
    output = Path(output)
    output.mkdir()  # A collision never overwrites another run.
    profile = Path(profile)
    require(profile.is_dir(), "SD cold boot requires an existing original profile")
    source_inventory = {}
    for item in profile.rglob("*"):
        require(not item.is_symlink(), "SD profile must not redirect files")
        if item.is_file():
            source_inventory[item.relative_to(profile).as_posix()] = hashlib.sha256(item.read_bytes()).hexdigest()
    user = output / "user"
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
    with config.open("w") as stream:
        ini.write(stream)
    raw, status = output / "observer.bin", output / "observer-status.json"
    native, native_status = output / "inputs.mwri", output / "input-status.json"
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(("MWRC_", "DOLPHIN_", "SDL_"))}
    environment.update(MWRC_ENABLE="1", MWRC_CPU="JITARM64", MWRC_SOURCE_REV="GALE01r2",
                       MWRC_DOL_SHA256="dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                       MWRC_OUTPUT=str(raw), MWRC_STATUS=str(status), MWRC_SD_INIT="1",
                       MWRC_SD_RECIPE_SHA256=plan["authored_recipe_sha256"],
                       MWRC_INPUT_RECORD=str(native), MWRC_INPUT_STATUS=str(native_status))
    command = dolphin_command(Path(dolphin), user, Path("unused"), Path(disc),
                              cpu="JITARM64", cold_boot=True, audible=False)
    command += ["-C", "Session.Core.SaveDataWritable=False", "-C", "Dolphin.Interface.ConfirmStop=False"]
    (output / "input-plan.json").write_bytes(canonical(plan))
    (output / "menu-recipe.json").write_bytes(canonical(menus))
    (output / "launch.json").write_bytes(canonical({"scope": "sd_initialization_prefix",
        "input_plan_sha256": plan_hash, "menu_recipe_sha256": menu_hash,
        "profile_sha256": source_inventory, "build": build, "command": command}))
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
                def next_menu(scene=None):
                    while True:
                        row = next_row()
                        require(receiver.order == 0, "SD match entered before menu recipe/release completed")
                        payload = row["payload"]
                        if row["event"] == "progress" and payload["name"] == "menu":
                            if scene is None or any(s["tag"] == 40 and s["hex"] == f"{scene:02x}"
                                                   for s in payload["slices"]):
                                return row
                for action in menus["actions"]:
                    next_menu(action["scene"])
                    controller.set_both(action["p1"], action["p2"], action=action["label"])
                    for _ in range(action["polls"]):
                        next_menu()
                    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=action["label"] + ":release")
                    # The final SSS confirmation can transition as soon as the
                    # release is sent. Its first source sample must verify neutral.
                    if action is not menus["actions"][-1]:
                        for _ in range(action["settle_polls"]):
                            next_menu()
                while not receiver.ended:
                    next_row()
                # MWRO End is flushed before the writer publishes final status.
                # Do not treat that publication race as native completion.
                wait_terminal_statuses(status, native_status, deadline)
                report = receiver.finish(status, native, native_status)
                (output / "report.json").write_bytes(canonical(report))
                return report
        except Exception as error:
            (output / "failure.json").write_bytes(canonical({"scope": "sd_initialization_prefix", "error": str(error)}))
            raise
        finally:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "profile", "input-plan", "menu-recipe", "output", "build-manifest"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180)
    args = parser.parse_args(argv)
    try:
        report = run(**vars(args))
    except (OSError, ValueError) as error:
        parser.exit(1, f"SD prefix diagnostic failed: {error}\n")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
