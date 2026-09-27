#!/usr/bin/env python3
"""Capture original allocation history from a fresh DOL boot, without a state load.

Reuses the retail runner's owned Dolphin/config/controller transport and the
original menu/input collectors. This diagnostic stream cannot be admitted as a
replacement gold capture. Every attempted process retains its logs and hashes.
"""
from __future__ import annotations

import argparse
import datetime
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
MENU_ROUTE_SOURCE_FILES = (
    ".deps/melee/src/melee/gm/gmboot.c",
    ".deps/melee/src/melee/gm/gmopeningmode.c",
    ".deps/melee/src/melee/gm/gmtitlemode.c",
    ".deps/melee/src/melee/gm/gm_1A3F.c",
    ".deps/melee/src/melee/gm/gmvsmelee.c",
    ".deps/melee/src/melee/mn/mnmain.c",
    ".deps/melee/src/melee/mn/mncharsel.c",
)
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tools"))
import capture_retail_replay as retail
from retail_allocation_profile import build_profile
from original_boot_context import derive_boot_context
from retail_input_plan import load_plan
from retail_replay_validation import load_capture
from retail_input_plan import verify_capture
from cpu_reference_scenarios import scenario_target, validate_scenario, validate_input_plan


def write_json(path, value):
    with path.open("x") as stream:
        json.dump(value, stream, indent=2, sort_keys=True)
        stream.write("\n")


def tree_hashes(root):
    return {str(path.relative_to(root)): retail._sha256(path)
            for path in sorted(root.rglob("*")) if path.is_file()}


def validate_reference_build_manifest(path, dolphin):
    """Bind this run to the exact private Dolphin build receipt when supplied."""
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read Dolphin build manifest: {error}") from error
    executable = Path(dolphin).expanduser().resolve()
    manifest_binary = Path(manifest.get("binary", "")).expanduser().resolve()
    if (manifest.get("schema") != "melee-web-reference-dolphin-build" or
            manifest.get("version") != 1 or manifest.get("target") != "dolphin-nogui" or
            manifest.get("dolphin_commit") != retail.EXPECTED_PROVENANCE["dolphin_commit"] or
            manifest.get("writes_guest_memory") is not False or
            manifest_binary != executable):
        raise ValueError("Dolphin build manifest does not identify the pinned no-GUI executable")
    actual = retail._sha256(executable)
    if actual.lower() != str(manifest.get("binary_sha256", "")).lower():
        raise ValueError("Dolphin executable hash disagrees with the supplied build manifest")
    identity = manifest.get("observer_identity")
    if (not isinstance(identity, dict) or identity.get("game_revision") != "GALE01r2" or
            identity.get("dol_sha1") != retail.EXPECTED_PROVENANCE["dol_sha1"] or
            identity.get("dol_sha256") != "dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646" or
            identity.get("writes_guest_memory") is not False):
        raise ValueError("Dolphin build manifest has an incompatible observer identity")
    return {"path": str(path.resolve()), "sha256": retail._sha256(path),
            "binary_sha256": actual, "dolphin_commit": manifest["dolphin_commit"],
            "target": manifest["target"], "build_cpu": identity.get("cpu")}


def validate_setup_receipt(path, args, manifest):
    """Bind the private run inputs to the receipt made during local setup."""
    try:
        receipt = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read owned-input setup receipt: {error}") from error
    expected = {
        "schema": "melee-web-original-menu-reference-inputs",
        "version": 1,
        "disc.sha256": retail._sha256(args.disc),
        "dol.sha1": retail._sha1(args.dol),
        "dol.sha256": retail._sha256(args.dol),
        "dolphin.binary_sha256": retail._sha256(args.dolphin),
        "dolphin.dolphin_commit": retail.EXPECTED_PROVENANCE["dolphin_commit"],
        "dolphin.target": "dolphin-nogui",
        "template_source.selected_language": 0,
        "template_source.cpu_thread": False,
        "template_source.cheats": False,
        "template_source.custom_rtc": 1704067200,
        "initial_card.file_count": 0,
        "initial_card.format": "empty GCI folder",
        "disc.game_id": "GALE01r2",
    }
    for dotted, value in expected.items():
        actual = receipt
        for component in dotted.split("."):
            actual = actual.get(component) if isinstance(actual, dict) else None
        if actual != value:
            raise ValueError(f"owned-input setup receipt disagrees at {dotted}: {actual!r}")
    if manifest is None or receipt.get("dolphin", {}).get("sha256") != manifest["sha256"]:
        raise ValueError("owned-input setup receipt is not bound to the pinned Dolphin manifest")
    if receipt.get("disc", {}).get("dol_sha1_verified") != expected["dol.sha1"]:
        raise ValueError("owned-input setup receipt does not bind the DOL extracted from this disc")
    card = args.checkpoint_gc / "USA/Card A"
    if not card.is_dir() or any(card.iterdir()):
        raise ValueError("the declared empty GCI folder baseline is missing or contains save files")
    if tree_hashes(args.checkpoint_gc) != receipt.get("external_save_hashes"):
        raise ValueError("checkpoint memory-card files differ from the setup receipt")
    configured_ini = args.template_user / "Config/Dolphin.ini"
    if receipt.get("template_source", {}).get("capture_dolphin_ini_sha256") != retail._sha256(configured_ini):
        raise ValueError("capture template Dolphin.ini differs from the setup receipt")
    configured_pad = args.template_user / "Config/GCPadNew.ini"
    if receipt.get("template_source", {}).get("capture_gcpad_ini_sha256") != retail._sha256(configured_pad):
        raise ValueError("capture template GCPadNew.ini differs from the setup receipt")
    return {"path": str(path.resolve()), "sha256": retail._sha256(path),
            "card_baseline": "empty GCI folder", "external_save_hashes": tree_hashes(args.checkpoint_gc)}


def visual_capture_options(command):
    """Enable the pinned Dolphin frame dumper in a headless OpenGL session."""
    return command + ["-p", "headless", "-v", "OGL",
                      "-C", "Dolphin.Movie.DumpFrames=True",
                      "-C", "Dolphin.Movie.DumpFramesSilent=True",
                      "-C", "Dolphin.GFX.Settings.DumpFramesAsImages=True"]


def resolve_gdb_executable(path=None):
    """Use an explicit private debugger binary or the ordinary PATH entry."""
    candidate = Path(path).expanduser() if path is not None else Path(shutil.which("gdb") or "")
    candidate = candidate.resolve()
    if not candidate.is_file() or not os.access(candidate, os.X_OK):
        raise ValueError("GDB executable is unavailable; pass --gdb PATH or install gdb on PATH")
    return candidate


def retain_route_screenshots(frames_dir, trace_path, output):
    """Keep one PNG beside each verified source route marker, with its mapping."""
    images = sorted(frames_dir.glob("framedump_*.png"),
                    key=lambda path: int(path.stem.rsplit("_", 1)[1]))
    rows = [json.loads(line) for line in trace_path.read_text(encoding="utf-8").splitlines()]
    scheduler = [row for row in rows if row.get("event") == "scheduler_return"]
    if not images:
        return {"status": "missing", "frame_dump_count": 0,
                "reason": "Dolphin's configured headless frame dumper produced no PNG files"}
    if not scheduler:
        raise RuntimeError("cannot align original screenshots without source scheduler rows")
    # FrameDumper names output frames from 1 while the source route trace starts
    # at scheduler sequence 0. Preserve the observed count and offset so a
    # reviewer can reject any non-one-to-one relationship rather than assuming
    # this is pixel or frame-timing equivalence.
    offset = len(images) - len(scheduler)
    if abs(offset) > 2:
        return {"status": "unmapped", "frame_dump_count": len(images),
                "scheduler_frame_count": len(scheduler), "frame_offset": offset,
                "reason": "PNG count differs from source scheduler rows by more than two"}
    marker_names = (
        "cold_css_ready", "css_before_b_back_probe", "css_b_back_probe_remained_css",
        "versus_submenu_ready_after_css", "root_main_menu_ready", "title_ready",
        "root_main_menu_ready_after_title", "versus_submenu_ready_after_title",
        "round_trip_css_ready")
    markers = [row for row in rows if row.get("event") in marker_names]
    if [row.get("event") for row in markers] != list(marker_names):
        raise RuntimeError("cannot retain route screenshots: expected source markers are missing")
    screenshot_dir = output / "screenshots"
    screenshot_dir.mkdir()
    manifest_rows = []
    for marker in markers:
        image_index = int(marker["sequence"]) + 1 + offset
        if image_index < 1 or image_index > len(images):
            raise RuntimeError(f"no original PNG maps to route marker {marker['event']}")
        source = images[image_index - 1]
        name = marker["event"] + ".png"
        target = screenshot_dir / name
        shutil.copy2(source, target)
        manifest_rows.append({"event": marker["event"], "scene_kind": marker["scene_kind"],
            "game_mode": marker["game_mode"], "source_scheduler_sequence": marker["sequence"],
            "frame_dump_index": image_index, "file": name, "sha256": retail._sha256(target)})
    write_json(screenshot_dir / "mapping.json", {
        "status": "retained", "frame_dump_count": len(images),
        "scheduler_frame_count": len(scheduler), "frame_offset": offset,
        "alignment_scope": "one PNG per source scheduler row by count and ordinal only; not pixel or timing equivalence",
        "screenshots": manifest_rows})
    return {"status": "retained", "frame_dump_count": len(images),
            "scheduler_frame_count": len(scheduler), "frame_offset": offset,
            "screenshots": len(manifest_rows),
            "mapping_sha256": retail._sha256(screenshot_dir / "mapping.json")}


def verify_menu_route(path):
    """Check that a source-frame diagnostic completed every declared route."""
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if not rows:
        raise RuntimeError("menu route trace is empty")
    markers = [row for row in rows if row.get("event") != "scheduler_return"]
    expected = [
        ("first_scheduler_return", None, None),
        ("cold_css_ready", 8, 2),
        ("css_before_b_back_probe", 8, 2),
        ("css_b_back_probe_remained_css", 8, 2),
        ("versus_submenu_ready_after_css", 1, 1),
        ("root_main_menu_ready", 1, 1),
        ("title_ready", 0, 0),
        ("root_main_menu_ready_after_title", 1, 1),
        ("versus_submenu_ready_after_title", 1, 1),
        ("round_trip_css_ready", 8, 2),
    ]
    observed = [(row.get("event"), row.get("scene_kind"), row.get("game_mode"))
                for row in markers]
    if len(observed) != len(expected):
        raise RuntimeError("menu route trace has a missing or unexpected scene marker")
    for (name, scene, mode), row in zip(expected, markers):
        if row.get("event") != name:
            raise RuntimeError(f"menu route marker order differs: expected {name}, got {row.get('event')}")
        if scene is not None and (row.get("scene_kind"), row.get("game_mode")) != (scene, mode):
            raise RuntimeError(f"menu route marker {name} reached scene/mode "
                               f"{row.get('scene_kind')}/{row.get('game_mode')}, expected {scene}/{mode}")
    expected_menu_states = {
        "versus_submenu_ready_after_css": (2, 0),
        "root_main_menu_ready": (0, 1),
        "root_main_menu_ready_after_title": (0, 0),
        "versus_submenu_ready_after_title": (2, 0),
    }
    for row in markers:
        expected_state = expected_menu_states.get(row.get("event"))
        if expected_state is not None:
            state = row.get("menu_state")
            if not isinstance(state, dict) or (state.get("cur"), state.get("hovered")) != expected_state:
                raise RuntimeError(f"menu route marker {row.get('event')} has unexpected source menu state: {state}")
    frame_rows = [row for row in rows if row.get("event") == "scheduler_return"]
    sequences = [row.get("sequence") for row in frame_rows]
    if not sequences or sequences != list(range(len(sequences))):
        raise RuntimeError("menu route source-frame sequence is missing or discontinuous")
    if not any("pad_copy_status_hex" in row for row in rows):
        raise RuntimeError("menu route trace omitted source PAD state")
    if not all("current_hps_hex" in row and "hps_voice_word" in row for row in frame_rows):
        raise RuntimeError("menu route trace omitted the bounded source audio owner snapshot")
    if not any(row.get("scene_kind") == 1 and "menu_state" in row for row in frame_rows):
        raise RuntimeError("menu route trace omitted decoded original main-menu state")
    if not any(row.get("scene_kind") == 8 and "css_cursors" in row and
               "css_data_hex" in row for row in frame_rows):
        raise RuntimeError("menu route trace omitted source CSS selection/cursor state")
    return {"source_scheduler_frames": len(frame_rows),
            "markers": observed[:len(expected)],
            "trace_sha256": retail._sha256(path)}


def verify_menu_route_commands(path):
    """Require ordinary PAD inputs at the original CSS/menu/title boundaries."""
    commands = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if not commands:
        raise RuntimeError("menu route input command log is empty")
    if any(row.get("event") != "pad_command" for row in commands):
        raise RuntimeError("menu route input log contains an unknown record")
    def index(scene, mode, command, after=-1):
        return next((i for i, row in enumerate(commands)
                     if i > after and row.get("scene_kind") == scene and
                     row.get("game_mode") == mode and row.get("command") == command), -1)
    css_b = index(8, 2, "PRESS B")
    css_l = index(8, 2, "PRESS L", css_b)
    css_r = index(8, 2, "PRESS R", css_l)
    css_start = index(8, 2, "PRESS START", css_r)
    if min(css_b, css_l, css_r, css_start) < 0:
        raise RuntimeError("menu route lacks the CSS B probe followed by L+R+Start parent-menu chord")
    submenu_back = index(1, 1, "PRESS B", css_start)
    root_back = index(1, 1, "PRESS B", submenu_back)
    if submenu_back < 0 or root_back < 0:
        raise RuntimeError("menu route lacks ordered VS-submenu and root-menu Back inputs")
    if commands[submenu_back].get("menu_state", {}).get("cur") != 2:
        raise RuntimeError("first post-CSS Back input was not in the original VS submenu")
    if commands[root_back].get("menu_state", {}).get("cur") != 0:
        raise RuntimeError("second post-CSS Back input was not in the original root menu")
    title_start = index(0, 0, "PRESS START", root_back)
    if title_start < 0:
        raise RuntimeError("menu route lacks original title Start input")
    root_down = index(1, 1, "PRESS DOWN", title_start)
    first_confirm = index(1, 1, "PRESS A", root_down)
    second_confirm = index(1, 1, "PRESS A", first_confirm)
    if root_down < 0 or first_confirm < 0 or second_confirm < 0:
        raise RuntimeError("menu route lacks ordered original main/VS menu confirmations")
    if commands[first_confirm].get("menu_state", {}).get("cur") != 0 or commands[first_confirm].get("menu_state", {}).get("hovered") != 1:
        raise RuntimeError("first post-title confirm was not SEL_MAIN_VS in the root menu")
    if commands[second_confirm].get("menu_state", {}).get("cur") != 2 or commands[second_confirm].get("menu_state", {}).get("hovered") != 0:
        raise RuntimeError("second post-title confirm was not SEL_VS_MELEE in the VS submenu")
    return {"commands": len(commands), "sha256": retail._sha256(path)}


def gdb_script(paths, boot_only=False, menu_round_trip=False):
    q = retail._gdb_quote
    lines = ["set architecture powerpc:common", "set endian big", "set pagination off",
             "set confirm off", "set breakpoint pending on",
             f"target remote {paths['socket']}",
             "hbreak *0x80390eb4", "commands", "silent", "end",
             f"source {paths['allocation_collector']}", "continue",
             "python",
             "if _allocation_failed: raise RuntimeError(_allocation_failed)",
             "if _allocation_reg('pc') != 0x80390eb4: raise RuntimeError('boot did not reach first scheduler return')",
             "end"]
    if not boot_only:
        if menu_round_trip:
            lines += [f"source {paths['menu_driver']}", "enable 1",
                      "python", "MENU_ROUTE_TRACE.close()", "end"]
        else:
            lines += [f"source {paths['menu_driver']}",
                      f"source {paths['helper']}",
                      f"source {paths['collector']}",
                      "enable 1",
                      f"retail-replay-arm {q(paths['trace'])} {paths['frames']}",
                      "retail-step 8 1 PRESS A", "retail-step 8 1 RELEASE A",
                      "disable 1", "continue", "python",
                      "if _allocation_failed: raise RuntimeError(_allocation_failed)",
                      "end"]
    lines += ["python", "allocation_finish('captured', '" +
              ("first_scheduler_return" if boot_only else
               "menu_round_trip_complete" if menu_round_trip else
               "requested_match_capture_boundary") + "')",
              "end", "quit", ""]
    return "\n".join(lines)


def capture(args):
    output = args.output.expanduser().resolve()
    output.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    metadata = {"schema": "melee-web-allocation-capture-run", "version": 1,
                "run_id": uuid.uuid4().hex, "status": "preparing",
                "started_at": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "start": "original_dol_entry", "savestate_loaded": False,
                "boot_only": args.boot_only, "menu_round_trip": args.menu_round_trip,
                "route_mode": "cold_boot_css_menu_round_trip" if args.menu_round_trip else
                              "first_scheduler_return" if args.boot_only else "fixed_allocation_replay",
                "candidate_admission": "forbidden",
                "scope": "original allocation diagnostic; no gameplay or performance admission"}
    paths = None
    processes = []
    streams = []
    inputs = {}
    try:
        if args.boot_only and args.menu_round_trip:
            raise ValueError("--boot-only and --menu-round-trip are separate diagnostic routes")
        plan = scenario = plan_hash = None
        if not args.boot_only and not args.menu_round_trip:
            if args.input_plan is None or args.scenario is None:
                raise ValueError("fixed allocation replay requires --input-plan and --scenario")
            plan, plan_hash = load_plan(args.input_plan)
            scenario = json.loads(args.scenario.read_text())
            validate_scenario(scenario)
            validate_input_plan(plan)
            if plan.get("version") != 3:
                raise ValueError("allocation experiment requires an existing v3 CPU input plan")
        for path in (args.disc, args.dol, args.provenance, args.input_plan, args.scenario,
                     args.dolphin_manifest,
                     args.setup_receipt,
                     ROOT / ".deps/melee/config/GALE01/symbols.txt",
                     Path(__file__), ROOT / "tools/original_boot_context.py",
                     ROOT / "tools/reference_allocation_capture.py",
                     ROOT / "tools/retail_allocation_profile.py",
                     ROOT / "tools/retail_allocation_menu.py"):
            if path is not None:
                inputs[str(path.resolve())] = retail._sha256(path)
        route_source_identities = {}
        if args.menu_round_trip:
            for relative in MENU_ROUTE_SOURCE_FILES:
                source_path = ROOT / relative
                route_source_identities[relative] = retail._sha256(source_path)
                inputs[str(source_path.resolve())] = route_source_identities[relative]
            lock = ROOT / "dependencies.lock.json"
            inputs[str(lock.resolve())] = retail._sha256(lock)
        for parent in (args.template_user, args.checkpoint_gc):
            for path in parent.rglob("*"):
                if path.is_file(): inputs[str(path.resolve())] = retail._sha256(path)
        # Use the repository collector, never an inherited custom capture hook.
        previous_collector = os.environ.pop("MELEE_REPLAY_COLLECTOR", None)
        try:
            paths = retail.prepare_run(args.template_user, args.checkpoint_gc, args.provenance,
                                       args.dol, args.dolphin, output / "trace.jsonl", cpu=args.cpu)
        finally:
            if previous_collector is not None:
                os.environ["MELEE_REPLAY_COLLECTOR"] = previous_collector
        evidence = paths["evidence"]
        evidence.mkdir()
        paths["trace"] = output / "trace.jsonl"
        build_receipt = None
        if args.dolphin_manifest is not None:
            build_receipt = validate_reference_build_manifest(
                args.dolphin_manifest.expanduser().resolve(), args.dolphin)
            shutil.copy2(args.dolphin_manifest, evidence / "reference-dolphin-build.json")
            metadata["dolphin_build"] = build_receipt
        if args.setup_receipt is not None:
            metadata["owned_inputs"] = validate_setup_receipt(
                args.setup_receipt.expanduser().resolve(), args, build_receipt)
            shutil.copy2(args.setup_receipt, evidence / "owned-inputs-setup-receipt.json")
        if args.capture_images:
            (paths["user"] / "Dump/Frames").mkdir(parents=True, exist_ok=True)
        if plan is not None:
            paths["frames"] = len(plan["frames"])
        inputs[str(paths["source_dolphin"])] = paths["identity"]["dolphin_binary_sha256"]
        paths["allocation_collector"] = paths["collector"].with_name("reference_allocation_capture.py")
        shutil.copy2(ROOT / "tools/reference_allocation_capture.py", paths["allocation_collector"])
        profile_path = evidence / "allocation-profile.json"
        write_json(profile_path, build_profile(args.dol, ROOT / ".deps/melee/config/GALE01/symbols.txt"))
        boot_context = derive_boot_context(args.dol, args.disc,
            ROOT / ".deps/melee/config/GALE01/symbols.txt", ROOT / ".deps/melee")
        write_json(evidence / "independent-boot-context.json", boot_context)
        if args.menu_round_trip:
            write_json(evidence / "route-source-identities.json", {
                "melee_checkout": str((ROOT / ".deps/melee").resolve()),
                "melee_revision": paths["provenance"].get("source_revision"),
                "dependencies_lock_sha256": retail._sha256(ROOT / "dependencies.lock.json"),
                "files_sha256": route_source_identities})
        if plan is not None:
            shutil.copy2(args.input_plan, evidence / "input-plan.json")
            if retail._sha256(evidence / "input-plan.json") != plan_hash:
                raise ValueError("owned input-plan copy differs from its frozen source")
            shutil.copy2(args.scenario, evidence / "scenario.json")
            target = scenario_target(scenario)
        else:
            # This target declares the availability query at first CSS. The
            # route driver never applies it to source game state.
            target = {"expected_setup": {"time_limit_seconds": 60,
                "players": [{"character_kind": 8, "costume": 0, "player_type": 0,
                             "stocks": 4, "rumble_enabled": True}],
                "disable_pausing": False, "stage": 32}}
        write_json(evidence / "menu-target.json", target)
        paths["menu_driver"] = evidence / "menu-driver.py"
        if args.menu_round_trip:
            from retail_allocation_menu import render_menu_round_trip_driver
            paths["menu_driver"].write_text(render_menu_round_trip_driver())
        elif not args.boot_only:
            from retail_allocation_menu import render_driver
            paths["menu_driver"].write_text(render_driver())
        paths["helper"] = evidence / "gdb-control.py"
        retail.write_control_helper(paths["helper"], paths["user"] / "Pipes", paths["trace"])
        # Any allocation failure must stop every controller-driven continue loop.
        helper = paths["helper"].read_text().replace(
            'gdb.execute("continue", to_string=True)',
            'gdb.execute("continue", to_string=True)\n'
            '                if globals().get("_allocation_failed"): raise RuntimeError(_allocation_failed)')
        paths["helper"].write_text(helper)
        original_provenance = dict(paths["provenance"])
        original_provenance.pop("setup_snapshot_sha256", None)
        original_provenance["application_context"] = "fresh_original_dol_boot_no_savestate"
        original_provenance["allocation_profile_sha256"] = retail._sha256(profile_path)
        original_provenance["independent_boot_context_sha256"] = retail._sha256(evidence / "independent-boot-context.json")
        write_json(evidence / "provenance.json", original_provenance)
        external = tree_hashes(paths["user"] / "GC")
        if external != paths["provenance"].get("external_save_hashes"):
            raise ValueError("external card context differs from pinned provenance")
        pipe_count = 4 if args.menu_round_trip else (plan["active_player_count"] if plan else 0)
        if pipe_count:
            retail.require_raw_pipe_config(paths["pad_config"], range(1, pipe_count + 1))
        identity = {**paths["identity"], "disc_dol_sha1": retail.verify_disc_dol(args.disc, args.dol),
                    "disc_image_sha256": inputs[str(args.disc.resolve())],
                    "dolphin_ini_canonical_sha256": retail._canonical_dolphin_ini_sha256(paths["config"], paths["socket"]),
                    "gcpad_ini_sha256": retail._sha256(paths["pad_config"]),
                    "external_save_hashes": external}
        command = retail.dolphin_command(paths["source_dolphin"], paths["user"], paths["snapshot"], args.disc, cpu=args.cpu)
        at = command.index("-s")
        del command[at:at + 2]
        if args.capture_images:
            command = visual_capture_options(command)
        commands = evidence / "gdb-commands.txt"
        commands.write_text(gdb_script(paths, args.boot_only, args.menu_round_trip))
        # These owned files are execution inputs even though they live beside
        # the diagnostic outputs. Freeze them before any original process runs.
        for path in evidence.iterdir():
            if path.is_file():
                inputs[str(path.resolve())] = retail._sha256(path)
        debugger_path = resolve_gdb_executable(args.gdb)
        inputs[str(debugger_path)] = retail._sha256(debugger_path)
        debugger_version = subprocess.run([str(debugger_path), "--version"], check=True,
                                          capture_output=True, text=True).stdout.splitlines()[0]
        environment = {k: v for k, v in os.environ.items()
                       if not k.startswith(("MELEE_REPLAY_", "MELEE_CPU_", "MELEE_ALLOCATION_"))}
        environment.update({"MELEE_ALLOCATION_PROFILE": str(profile_path),
            "MELEE_ALLOCATION_OUTPUT": str(output / "allocations.jsonl"),
            "MELEE_REPLAY_REFERENCE_WORK": str(evidence),
            "MELEE_REPLAY_COLLECTOR": str(paths["collector"]),
            "MELEE_REPLAY_DRAW_AUDIT": "1",
            "MELEE_CHECKPOINT_TARGET_JSON": str(evidence / "menu-target.json")})
        if plan is not None:
            environment["MELEE_REPLAY_INPUT_PLAN"] = str(evidence / "input-plan.json")
        metadata.update({"status": "running", "identity": identity, "owned_run": str(paths["run_root"]),
                         "gdb": {"path": str(debugger_path),
                                 "sha256": inputs[str(debugger_path)],
                                 "version": debugger_version},
                         "input_plan_sha256": plan_hash,
                         "frames_requested": len(plan["frames"]) if plan is not None else None,
                         "launch": command, "inputs": inputs,
                         "collector_files": tree_hashes(paths["collector"].parent)})
        write_json(output / "launch.json", metadata)
        dolphin_log = (output / "dolphin.log").open("x"); streams.append(dolphin_log)
        dolphin = subprocess.Popen(command, stdout=dolphin_log, stderr=subprocess.STDOUT,
                                   env=environment, start_new_session=True)
        processes.append((dolphin, "Dolphin"))
        metadata["dolphin_pid"] = dolphin.pid
        retail.wait_for_socket(paths["socket"], dolphin, 30)
        gdb_log = (output / "gdb.log").open("x"); streams.append(gdb_log)
        debugger = subprocess.Popen([str(debugger_path), "--quiet", "--nx", "--batch", "-x", str(commands)],
                                    stdout=gdb_log, stderr=subprocess.STDOUT, env=environment,
                                    start_new_session=True)
        processes.append((debugger, "GDB"))
        metadata["gdb_pid"] = debugger.pid
        write_json(output / "processes.json", {"run_id": metadata["run_id"],
                   "started_at": metadata["started_at"], "dolphin_pid": dolphin.pid,
                   "gdb_pid": debugger.pid, "owned_run": str(paths["run_root"])})
        debugger.wait(timeout=args.timeout)
        if debugger.returncode:
            raise RuntimeError("GDB capture failed; preserved gdb.log")
        allocation_path = output / "allocations.jsonl"
        with allocation_path.open() as stream:
            last = None
            first = json.loads(next(stream))
            for key in ("arena_lo", "arena_hi", "bi2", "memory_size"):
                observed = first["observed_boot_context"][key]
                if boot_context["boot"].get(key) != observed:
                    raise RuntimeError("observed boot context differs from independently derived " + key)
            for line in stream: last = json.loads(line)
        if last is None or last.get("record") != "end" or last.get("status") != "captured":
            raise RuntimeError("allocation capture lacks a successful terminal record")
        if args.menu_round_trip:
            route_path = evidence / "cold-boot-menu-route.jsonl"
            metadata["menu_route"] = verify_menu_route(route_path)
            metadata["menu_route"]["input_commands"] = verify_menu_route_commands(
                evidence / "cold-boot-input-commands.jsonl")
            if args.capture_images:
                screenshots = retain_route_screenshots(
                    paths["user"] / "Dump/Frames", route_path, output)
                metadata["menu_route"]["screenshots"] = screenshots
                if screenshots.get("status") != "retained":
                    raise RuntimeError("original visual evidence was not mapped to every route marker: " +
                                       str(screenshots.get("reason", screenshots.get("status"))))
        if plan is not None:
            trace = load_capture(paths["trace"], cpu=args.cpu)
            verify_capture(plan, trace)
            if len(trace.frames) != len(plan["frames"]):
                raise RuntimeError("original trace does not contain the complete frozen input timeline")
            metadata["frames_captured"] = len(trace.frames)
        metadata["status"] = "captured_diagnostic"
    except Exception as error:
        metadata.update({"status": "failed", "error": str(error)})
    finally:
        for process, label in reversed(processes):
            retail._terminate(process, label)
        for stream in streams: stream.close()
        if paths: paths["socket"].unlink(missing_ok=True)
        metadata["wall_seconds"] = time.monotonic() - started
        metadata["inputs"] = inputs
        metadata["inputs_unchanged"] = all(Path(path).is_file() and retail._sha256(Path(path)) == digest
                                           for path, digest in inputs.items())
        if not metadata["inputs_unchanged"]:
            metadata.update({"status": "failed", "error": "frozen capture input changed"})
        metadata["outputs"] = {path.name: retail._sha256(path) for path in output.iterdir() if path.is_file()}
        if paths:
            metadata["owned_evidence_outputs"] = tree_hashes(paths["evidence"])
            metadata["owned_final_gc_hashes"] = tree_hashes(paths["user"] / "GC")
            metadata["collector_files_after"] = tree_hashes(paths["collector"].parent)
            if metadata.get("collector_files") is not None and metadata["collector_files_after"] != metadata["collector_files"]:
                metadata.update({"status": "failed", "error": "owned collector changed during capture"})
        write_json(output / "result.json", metadata)
    print(json.dumps({"status": metadata["status"], "output": str(output), "error": metadata.get("error")}))
    return 0 if metadata["status"] == "captured_diagnostic" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "dol", "template-user", "checkpoint-gc", "provenance", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--input-plan", type=Path)
    parser.add_argument("--scenario", type=Path)
    parser.add_argument("--cpu", choices=("Interpreter64", "JITARM64"), default="Interpreter64")
    parser.add_argument("--timeout", type=float, default=7200)
    parser.add_argument("--gdb", type=Path,
                        help="GDB executable; defaults to the executable named gdb on PATH")
    parser.add_argument("--dolphin-manifest", type=Path,
                        help="private pinned build receipt to hash and copy into evidence")
    parser.add_argument("--setup-receipt", type=Path,
                        help="private disc, profile, and memory-card setup receipt to hash and copy")
    parser.add_argument("--capture-images", action="store_true",
                        help="use headless Dolphin OpenGL frame dumping and retain route screenshots")
    parser.add_argument("--boot-only", action="store_true", help="Stop at first scheduler return; diagnostic smoke only")
    parser.add_argument("--menu-round-trip", action="store_true",
                        help="Capture a cold-DOL CSS -> original menus -> title -> CSS route; diagnostic only")
    return capture(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
