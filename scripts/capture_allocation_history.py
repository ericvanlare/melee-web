#!/usr/bin/env python3
"""Capture original allocation history from a fresh DOL boot, without a state load.

Reuses the retail runner's owned Dolphin/config/controller transport and the
original menu/input collectors. This diagnostic stream cannot be admitted as a
replacement gold capture. Every attempted process retains its logs and hashes.
"""
from __future__ import annotations

import argparse
import base64
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
VS_RULES_ITEMS_ROUTE_SOURCE_FILES = (
    *MENU_ROUTE_SOURCE_FILES,
    ".deps/melee/src/melee/mn/mnmainrule.c",
    ".deps/melee/src/melee/mn/mnruleplus.c",
    ".deps/melee/src/melee/mn/mnitemsw.c",
    ".deps/melee/src/melee/mn/mnstagesel.c",
    ".deps/melee/src/melee/gm/gmvsmelee.c",
    ".deps/melee/src/melee/gm/gmvsmode.c",
    ".deps/melee/src/melee/gm/gm_16AE.c",
    ".deps/melee/src/melee/gm/gmresult.c",
    ".deps/melee/src/melee/gm/gmresultplayer.c",
    "tools/retail_cpu_menu_prepare.py",
    "tools/retail_allocation_menu.py",
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
    initial_gci = getattr(args, "initial_gci", None)
    if initial_gci is None:
        if (receipt.get("initial_card", {}).get("file_count") != 0 or
                receipt.get("initial_card", {}).get("format") != "empty GCI folder"):
            raise ValueError("owned-input setup receipt does not declare the empty GCI folder baseline")
        if not card.is_dir() or any(card.iterdir()):
            raise ValueError("the declared empty GCI folder baseline is missing or contains save files")
        card_baseline = "empty GCI folder"
        initial_profile_hash = None
    else:
        initial_gci = Path(initial_gci).expanduser().resolve()
        if not initial_gci.is_file() or initial_gci.suffix.lower() != ".gci":
            raise ValueError("initial Everything unlocked profile must be a readable .gci file")
        initial_profile_hash = retail._sha256(initial_gci)
        expected_card = {
            "file_count": 1,
            "format": "single imported Everything unlocked GCI",
            "gci_filename": initial_gci.name,
            "gci_sha256": initial_profile_hash,
            "save_mode": "Everything unlocked",
        }
        if receipt.get("initial_card") != expected_card:
            raise ValueError("owned-input setup receipt does not bind the imported Everything unlocked GCI")
        source_export = receipt.get("source_profile_export")
        if not isinstance(source_export, dict):
            raise ValueError("setup receipt lacks the source profile export receipt")
        report_path = Path(source_export.get("source_run_report", "")).expanduser().resolve()
        manifest_path = Path(source_export.get("source_run_manifest", "")).expanduser().resolve()
        try:
            source_report = json.loads(report_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read the Everything unlocked export report: {error}") from error
        report_hash = retail._sha256(report_path)
        try:
            source_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read the Everything unlocked runtime manifest: {error}") from error
        manifest_hash = retail._sha256(manifest_path)
        source_check = source_export.get("source_run_check")
        manifest_runtime = source_manifest.get("runtime")
        source_build_identity = source_report.get("build_identity")
        if (source_export.get("source_run_report_sha256") != report_hash or
                source_export.get("source_run_manifest_sha256") != manifest_hash or
                source_manifest.get("schema") != "melee-web-public-release-v1" or
                source_manifest.get("profile") != "player" or
                not isinstance(manifest_runtime, dict) or
                not manifest_runtime.get("identity_sha256") or
                source_report.get("schema") != "webmelee-public-player-browser-v1" or
                not isinstance(source_build_identity, dict) or
                source_build_identity.get("schema") != "melee-web-public-release-v1" or
                source_build_identity.get("profile") != "player" or
                source_export.get("source_run_result") != "pass" or
                source_export.get("source_run_result") != source_report.get("result") or
                source_export.get("source_run_scope") != source_report.get("scope") or
                source_check not in source_report.get("checks", []) or
                source_export.get("profile_mode") != "Everything unlocked" or
                source_export.get("exported_gci_sha256") != initial_profile_hash or
                source_export.get("exported_gci_bytes") != initial_gci.stat().st_size):
            raise ValueError("source profile export report, manifest, GCI hash, or save mode disagrees with the setup receipt")
        card_files = sorted(card.iterdir()) if card.is_dir() else []
        if (len(card_files) != 1 or card_files[0].name != initial_gci.name or
                not card_files[0].is_file() or retail._sha256(card_files[0]) != initial_profile_hash):
            raise ValueError("the isolated GCI folder does not contain the declared Everything unlocked profile")
        card_baseline = "single imported Everything unlocked GCI"
    external_hashes = tree_hashes(args.checkpoint_gc)
    if external_hashes != receipt.get("external_save_hashes"):
        raise ValueError("checkpoint memory-card files differ from the setup receipt")
    provenance_path = getattr(args, "provenance", None)
    provenance_hash = None
    if initial_profile_hash is not None and provenance_path is not None:
        provenance_path = Path(provenance_path).expanduser().resolve()
        source_provenance_receipt = receipt.get("retail_provenance")
        try:
            source_provenance = json.loads(provenance_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ValueError(f"cannot read the imported profile provenance: {error}") from error
        provenance_hash = retail._sha256(provenance_path)
        if (not isinstance(source_provenance_receipt, dict) or
                source_provenance_receipt.get("path") != str(provenance_path) or
                source_provenance_receipt.get("sha256") != provenance_hash or
                source_provenance_receipt.get("external_save_hashes") != external_hashes or
                source_provenance.get("external_save_hashes") != external_hashes or
                source_provenance.get("source_profile_mode") != "Everything unlocked" or
                source_provenance.get("source_profile_gci_sha256") != initial_profile_hash):
            raise ValueError("retail provenance does not bind the imported Everything unlocked GCI")
    configured_ini = args.template_user / "Config/Dolphin.ini"
    if receipt.get("template_source", {}).get("capture_dolphin_ini_sha256") != retail._sha256(configured_ini):
        raise ValueError("capture template Dolphin.ini differs from the setup receipt")
    configured_pad = args.template_user / "Config/GCPadNew.ini"
    if receipt.get("template_source", {}).get("capture_gcpad_ini_sha256") != retail._sha256(configured_pad):
        raise ValueError("capture template GCPadNew.ini differs from the setup receipt")
    return {"path": str(path.resolve()), "sha256": retail._sha256(path),
            "card_baseline": card_baseline, "initial_profile_sha256": initial_profile_hash,
            "external_save_hashes": external_hashes,
            "retail_provenance_sha256": provenance_hash,
            "source_profile_export": (None if initial_profile_hash is None else {
                "source_run_report": str(report_path), "source_run_report_sha256": report_hash,
                "source_run_manifest": str(manifest_path), "source_run_manifest_sha256": manifest_hash,
                "source_run_check": source_check})}


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


def retain_route_screenshots(frames_dir, trace_path, output, *, route="menu"):
    """Keep one PNG beside each verified source route marker, with its mapping."""
    if route not in ("menu", "vs_rules_items"):
        raise ValueError("unknown original route screenshot mapping")
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
    marker_names = ((
        "cold_css_ready", "css_before_b_back_probe", "css_b_back_probe_remained_css",
        "versus_submenu_ready_after_css", "root_main_menu_ready", "title_ready",
        "root_main_menu_ready_after_title", "versus_submenu_ready_after_title",
        "round_trip_css_ready") if route == "menu" else (
        "cold_css_ready", "versus_submenu_after_css_parent", "root_menu_after_vs_back",
        "versus_submenu_for_rules", "vs_rules_first_entry", "vs_items_entry",
        "vs_items_one_bit_toggled", "vs_items_frequency_none", "vs_items_back_committed",
        "vs_rules_back_to_versus", "versus_back_to_main",
        "vs_rules_reentry_retained_items", "vs_rules_stock_three_selected",
        "css_after_rules_start_retained", "sss_after_rules_start",
        "sss_final_destination_selected", "vs_match_entered",
        "vs_match_after_180_ticks", "vs_no_contest_chord_sent",
        "results_no_contest", "css_after_results_retained"))
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
        ("first_scheduler_return", 0x2A, 0),
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


def verify_vs_rules_items_route(path):
    """Require the cold-DOL original Rules/Items path and CSS settings retention."""
    rows = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if not rows:
        raise RuntimeError("VS Rules/Items route trace is empty")
    expected = [
        ("first_scheduler_return", 0x2A, 0),
        ("cold_css_ready", 8, 2),
        ("versus_submenu_after_css_parent", 1, 1),
        ("root_menu_after_vs_back", 1, 1),
        ("versus_submenu_for_rules", 1, 1),
        ("vs_rules_first_entry", 1, 1),
        ("vs_items_entry", 1, 1),
        ("vs_items_one_bit_toggled", 1, 1),
        ("vs_items_frequency_none", 1, 1),
        ("vs_items_back_committed", 1, 1),
        ("vs_rules_back_to_versus", 1, 1),
        ("versus_back_to_main", 1, 1),
        ("vs_rules_reentry_retained_items", 1, 1),
        ("vs_rules_stock_three_selected", 1, 1),
        ("css_after_rules_start_retained", 8, 2),
        ("sss_after_rules_start", 9, 2),
        ("sss_final_destination_selected", 9, 2),
        ("vs_match_entered", 2, 2),
        ("vs_match_after_180_ticks", 2, 2),
        ("vs_no_contest_chord_sent", 2, 2),
        ("results_no_contest", 5, 2),
        ("css_after_results_retained", 8, 2),
    ]
    markers = [row for row in rows if row.get("event") != "scheduler_return"]
    if len(markers) != len(expected):
        raise RuntimeError("VS Rules/Items route trace has a missing or unexpected source marker")
    observed = [(row.get("event"), row.get("scene_kind"), row.get("game_mode"))
                for row in markers]
    for (name, scene, mode), row in zip(expected, markers):
        if row.get("event") != name:
            raise RuntimeError(f"VS Rules/Items marker order differs: expected {name}, got {row.get('event')}")
        if scene is not None and (row.get("scene_kind"), row.get("game_mode")) != (scene, mode):
            raise RuntimeError(f"VS Rules/Items marker {name} reached scene/mode "
                               f"{row.get('scene_kind')}/{row.get('game_mode')}, expected {scene}/{mode}")
    expected_menu_states = {
        "versus_submenu_after_css_parent": (2, 0),
        "root_menu_after_vs_back": (0, 1),
        "versus_submenu_for_rules": (2, 0),
        "vs_rules_first_entry": (13, 0),
        "vs_items_entry": (16, 0),
        "vs_items_one_bit_toggled": (16, 0),
        "vs_items_frequency_none": (16, 31),
        "vs_items_back_committed": (13, 5),
        "vs_rules_back_to_versus": (2, 3),
        "versus_back_to_main": (0, 1),
        "vs_rules_reentry_retained_items": (13, 0),
        "vs_rules_stock_three_selected": (13, 1),
    }
    for row in markers:
        expected_state = expected_menu_states.get(row.get("event"))
        if expected_state is not None:
            state = row.get("menu_state")
            if not isinstance(state, dict) or (state.get("cur"), state.get("hovered")) != expected_state:
                raise RuntimeError(f"VS Rules/Items marker {row.get('event')} has unexpected source menu state: {state}")
    frequency_state = next(row for row in markers
                           if row["event"] == "vs_items_frequency_none").get("menu_state", {})
    if frequency_state.get("confirmed") != 0:
        raise RuntimeError("original Items None frequency was not selected in source menu state")
    entry = next(row for row in markers if row["event"] == "vs_items_entry")
    toggled = next(row for row in markers if row["event"] == "vs_items_one_bit_toggled")
    committed = next(row for row in markers if row["event"] == "vs_items_back_committed")
    retained = next(row for row in markers if row["event"] == "vs_rules_reentry_retained_items")
    stock = next(row for row in markers if row["event"] == "vs_rules_stock_three_selected")
    final_css = markers[-1]
    before_mask = entry.get("rules_state", {}).get("item_mask")
    toggled_mask = toggled.get("rules_state", {}).get("item_mask")
    delta = before_mask ^ toggled_mask if isinstance(before_mask, int) and isinstance(toggled_mask, int) else 0
    if not delta or delta & (delta - 1):
        raise RuntimeError("original Items A did not change exactly one source item-mask bit")
    if committed.get("rules_state", {}).get("item_mask") != toggled_mask:
        raise RuntimeError("Items B did not commit the one-bit mask change into original SaveData")
    if committed.get("rules_state", {}).get("item_frequency") != -1:
        raise RuntimeError("Items B did not commit the source None frequency value")
    if retained.get("rules_state") != committed.get("rules_state"):
        raise RuntimeError("VS Rules re-entry did not retain source item preferences")
    if stock.get("menu_state", {}).get("confirmed") != 3:
        raise RuntimeError("Rules source menu did not select three stocks")
    final_rules = final_css.get("rules_state", {})
    if (final_rules.get("stock_count") != 3 or
            final_rules.get("item_mask") != toggled_mask or
            final_rules.get("item_frequency") != -1):
        raise RuntimeError("Results/CSS return did not retain original source rules and item preferences")
    if markers[16].get("selected_stage_kind") != 0x20:
        raise RuntimeError("original SSS route did not retain source St_Kind_Last selection")
    match = markers[17].get("match_start_data", {})
    players = match.get("players", [])
    if (match.get("stage") != 0x20 or match.get("item_frequency") != -1 or
            match.get("item_mask_hex") != f"{toggled_mask:016x}" or
            len(players) != 6 or
            [(p.get("character_kind"), p.get("slot_type"), p.get("stocks"),
              p.get("color"), p.get("team")) for p in players[:2]] !=
            [(8, 0, 3, 0, 0), (8, 0, 3, 0, 0)] or
            any(p.get("slot_type") != 3 for p in players[2:])):
        raise RuntimeError("GM_VS StartMeleeData did not contain the selected source Rules/Items values")
    result = markers[20]
    if result.get("results_outcome") != 7:
        raise RuntimeError("source Results did not observe OUTCOME_NO_CONTEST")
    if markers[21].get("scene_kind") != 8:
        raise RuntimeError("source Results did not return to original CSS")
    frames = [row for row in rows if row.get("event") == "scheduler_return"]
    sequences = [row.get("sequence") for row in frames]
    if not sequences or sequences != list(range(len(sequences))):
        raise RuntimeError("VS Rules/Items source-frame sequence is missing or discontinuous")
    if not all("pad_copy_status_hex" in row and "current_hps_hex" in row and
               "hps_voice_word" in row for row in frames):
        raise RuntimeError("VS Rules/Items trace omitted source PAD or audio-owner state")
    if not any(row.get("scene_kind") == 1 and
               row.get("menu_state", {}).get("cur") in (13, 16) and
               "rules_state" in row for row in frames):
        raise RuntimeError("VS Rules/Items trace omitted live original Rules/Items state")
    if not any(row.get("scene_kind") == 2 and "match_start_data" in row
               for row in frames):
        raise RuntimeError("VS Rules/Items trace omitted original live StartMeleeData")
    if not any(row.get("scene_kind") == 5 and row.get("results_outcome") == 7
               for row in frames):
        raise RuntimeError("VS Rules/Items trace omitted original No Contest Results state")
    return {"source_scheduler_frames": len(frames), "markers": observed,
            "initial_item_mask": f"{before_mask:016x}",
            "committed_item_mask": f"{toggled_mask:016x}",
            "committed_item_frequency": final_rules["item_frequency"],
            "stock_count_after_css_handoff": final_rules["stock_count"],
            "live_match_stage": match["stage"],
            "live_match_player_one_stocks": match["players"][0]["stocks"],
            "live_match_start_players": [
                {key: player.get(key) for key in
                 ("character_kind", "slot_type", "stocks", "color", "team",
                  "rumble_enabled", "cpu_kind", "cpu_level")}
                for player in players],
            "results_outcome": result["results_outcome"],
            "stock_count_after_results_css_return": markers[21]["rules_state"]["stock_count"],
            "trace_sha256": retail._sha256(path)}


def parse_melee_gci_profiles(paths):
    """Read original SaveData through the same checked GCI parser as the web app."""
    node = shutil.which("node")
    if node is None:
        raise RuntimeError("Node.js is required to verify the original Melee GCI save effect")
    script = (
        "import fs from 'node:fs';\n"
        "import {parseMeleeGCI} from './web/gamecube-save.mjs';\n"
        "const profiles = process.argv.slice(1).map(path => {\n"
        "  const data = new Uint8Array(fs.readFileSync(path));\n"
        "  return Buffer.from(parseMeleeGCI(data)).toString('base64');\n"
        "});\n"
        "console.log(JSON.stringify(profiles));\n"
    )
    result = subprocess.run([node, "--input-type=module", "-e", script,
                             *(str(Path(path).resolve()) for path in paths)],
                            cwd=ROOT, check=False, capture_output=True, text=True,
                            timeout=30)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip() or "unknown parser failure"
        raise RuntimeError(f"original Melee GCI parser rejected a retained source card: {detail}")
    try:
        profiles = [base64.b64decode(value, validate=True)
                    for value in json.loads(result.stdout)]
    except (ValueError, json.JSONDecodeError) as error:
        raise RuntimeError(f"original Melee GCI parser returned invalid profile data: {error}") from error
    if len(profiles) != len(paths) or any(len(profile) != 0xF1C4 for profile in profiles):
        raise RuntimeError("original Melee GCI parser returned an unexpected SaveData extent")
    return profiles


def verify_vs_rules_items_save_effect(initial_gci, final_gc, item_frequency,
                                      item_mask):
    """Verify that source Items B persisted the selected values into the card."""
    initial_gci = Path(initial_gci).expanduser().resolve()
    final_gc = Path(final_gc).expanduser().resolve()
    expected_relative = f"USA/Card A/{initial_gci.name}"
    final_hashes = tree_hashes(final_gc)
    if set(final_hashes) != {expected_relative}:
        raise RuntimeError("original menu route changed the owned card file set")
    final_gci = final_gc / expected_relative
    initial_profile, final_profile = parse_melee_gci_profiles((initial_gci, final_gci))
    initial_frequency, final_frequency = initial_profile[0x448], final_profile[0x448]
    initial_mask = int.from_bytes(initial_profile[0x450:0x458], "big")
    final_mask = int.from_bytes(final_profile[0x450:0x458], "big")
    # Retail menu observers expose this signed byte as -1; the card stores 0xff.
    if (initial_frequency == final_frequency or initial_mask == final_mask or
            final_frequency != (item_frequency & 0xFF) or final_mask != item_mask):
        raise RuntimeError("retail memory-card SaveData does not contain the committed source Items values")
    changed = [offset for offset, (before, after) in enumerate(zip(initial_profile, final_profile))
               if before != after]
    ranges = []
    for offset in changed:
        if not ranges or offset != ranges[-1][1]:
            ranges.append([offset, offset + 1])
        else:
            ranges[-1][1] = offset + 1
    item_bytes = {0x448, *range(0x450, 0x458)}
    node = shutil.which("node")
    node_version = subprocess.run([node, "--version"], check=True,
                                  capture_output=True, text=True,
                                  timeout=15).stdout.strip()
    return {"status": "verified_source_save_effect",
            "card_relative_path": expected_relative,
            "initial_gci_sha256": retail._sha256(initial_gci),
            "final_gci_sha256": final_hashes[expected_relative],
            "item_frequency_before": initial_frequency,
            "item_frequency_after": final_frequency,
            "item_frequency_after_menu_value": item_frequency,
            "item_mask_before": f"{initial_mask:016x}",
            "item_mask_after": f"{final_mask:016x}",
            "profile_changed_byte_count": len(changed),
            "profile_changed_ranges": ranges,
            "other_profile_changed_byte_count": sum(offset not in item_bytes for offset in changed),
            "gci_parser_source_sha256": retail._sha256(ROOT / "web/gamecube-save.mjs"),
            "gci_parser_runtime": {"node_path": node, "node_version": node_version},
            "final_card_hashes": final_hashes}


def verify_vs_rules_items_route_commands(path):
    """Bind each Rules/Items choice to its original source menu and PAD command."""
    commands = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
    if not commands or any(row.get("event") != "pad_command" for row in commands):
        raise RuntimeError("VS Rules/Items input command log is empty or contains unknown rows")

    def index(scene, mode, command, menu_kind=None, after=-1):
        for i, row in enumerate(commands):
            if (i > after and row.get("scene_kind") == scene and
                    row.get("game_mode") == mode and row.get("command") == command and
                    (menu_kind is None or row.get("menu_state", {}).get("cur") == menu_kind)):
                return i
        return -1

    css_lr = index(8, 2, "PRESS L")
    css_r = index(8, 2, "PRESS R", after=css_lr)
    css_start = index(8, 2, "PRESS START", after=css_r)
    root_back = index(1, 1, "PRESS B", 2, css_start)
    main_confirm = index(1, 1, "PRESS A", 0, root_back)
    versus_move = next((i for i, row in enumerate(commands)
                        if i > main_confirm and row.get("scene_kind") == 1 and
                        row.get("game_mode") == 1 and row.get("command") in
                        ("PRESS D_UP", "PRESS D_DOWN") and
                        row.get("menu_state", {}).get("cur") == 2), -1)
    rules_confirm = index(1, 1, "PRESS A", 2, versus_move)
    items_entry = index(1, 1, "PRESS A", 13, rules_confirm)
    item_toggle = index(1, 1, "PRESS A", 16, items_entry)
    cursor_frequency = index(1, 1, "PRESS D_LEFT", 16, item_toggle)
    items_back = index(1, 1, "PRESS B", 16, cursor_frequency)
    frequency_changes = [i for i, row in enumerate(commands)
                         if item_toggle < i < items_back and row.get("scene_kind") == 1 and
                         row.get("game_mode") == 1 and row.get("menu_state", {}).get("cur") == 16 and
                         row.get("menu_state", {}).get("hovered") == 31 and
                         row.get("command") in ("PRESS D_UP", "PRESS D_DOWN")]
    rules_back = index(1, 1, "PRESS B", 13, items_back)
    versus_back = index(1, 1, "PRESS B", 2, rules_back)
    main_confirm_again = index(1, 1, "PRESS A", 0, versus_back)
    versus_move_again = next((i for i, row in enumerate(commands)
                              if i > main_confirm_again and row.get("scene_kind") == 1 and
                              row.get("game_mode") == 1 and row.get("command") in
                              ("PRESS D_UP", "PRESS D_DOWN") and
                              row.get("menu_state", {}).get("cur") == 2), -1)
    rules_confirm_again = index(1, 1, "PRESS A", 2, versus_move_again)
    stock_move = index(1, 1, "PRESS D_DOWN", 13, rules_confirm_again)
    start_rules = index(1, 1, "PRESS START", 13, stock_move)
    css_start_after_rules = index(8, 2, "PRESS START", after=start_rules)
    sss_start_match = index(9, 2, "PRESS A", after=css_start_after_rules)
    no_contest = [index(2, 2, "PRESS " + button, after=sss_start_match)
                  for button in ("L", "R", "A", "START")]
    if min(css_lr, css_r, css_start, root_back, main_confirm,
           versus_move, rules_confirm, items_entry, item_toggle,
           cursor_frequency, items_back, rules_back, versus_back,
           main_confirm_again, versus_move_again, rules_confirm_again, stock_move,
           start_rules, css_start_after_rules, sss_start_match, *no_contest) < 0:
        raise RuntimeError("VS Rules/Items PAD log lacks an ordered original route input")
    if commands[root_back].get("menu_state", {}).get("hovered") != 0:
        raise RuntimeError("first VS Back after CSS was not at the VS submenu")
    if commands[main_confirm].get("menu_state", {}).get("hovered") != 1:
        raise RuntimeError("main menu A was not SEL_MAIN_VS")
    if commands[rules_confirm].get("menu_state", {}).get("hovered") != 3:
        raise RuntimeError("VS submenu A was not SEL_VS_RULES")
    if commands[items_entry].get("menu_state", {}).get("hovered") != 5:
        raise RuntimeError("Rules submenu A was not the original Items entry")
    if commands[cursor_frequency].get("menu_state", {}).get("hovered") != 0:
        raise RuntimeError("Items frequency row was not reached from source item row zero")
    if (commands[items_back].get("menu_state", {}).get("hovered") != 31 or
            commands[items_back].get("menu_state", {}).get("confirmed") != 0):
        raise RuntimeError("Items B was not issued after the source frequency row")
    if any(commands[i].get("command") != "PRESS D_UP" for i in frequency_changes):
        raise RuntimeError("Items None frequency must be reached through original D-up inputs")
    if commands[versus_back].get("menu_state", {}).get("hovered") != 3:
        raise RuntimeError("Rules route did not Back from the original VS submenu")
    if commands[main_confirm_again].get("menu_state", {}).get("hovered") != 1:
        raise RuntimeError("Rules re-entry main menu A was not SEL_MAIN_VS")
    if commands[rules_confirm_again].get("menu_state", {}).get("hovered") != 3:
        raise RuntimeError("Rules re-entry VS menu A was not SEL_VS_RULES")
    if commands[start_rules].get("menu_state", {}).get("hovered") != 1:
        raise RuntimeError("Rules Start was not issued with the source stock row selected")
    no_contest_rows = [commands[i] for i in no_contest]
    if any(row.get("port") != 1 or row.get("source_sequence") is None
           for row in no_contest_rows):
        raise RuntimeError("No Contest chord was not delivered through observed source P1 PAD")
    if len({row.get("source_sequence") for row in no_contest_rows}) != 1:
        raise RuntimeError("No Contest L+R+A+Start inputs were not simultaneous in one source sample")
    results_start = index(5, 2, "PRESS START", after=no_contest[-1])
    if results_start < 0:
        raise RuntimeError("Results did not exit through an observed original P1 Start input")
    if commands[results_start].get("port") != 1:
        raise RuntimeError("Results confirmation was not routed through original P1 Start")
    return {"commands": len(commands), "sha256": retail._sha256(path)}


def gdb_script(paths, boot_only=False, menu_round_trip=False,
               vs_rules_items_round_trip=False):
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
        if menu_round_trip or vs_rules_items_round_trip:
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
               "vs_rules_items_round_trip_complete" if vs_rules_items_round_trip else
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
                "vs_rules_items_round_trip": args.vs_rules_items_round_trip,
                "route_mode": "cold_boot_css_menu_round_trip" if args.menu_round_trip else
                              "cold_boot_vs_rules_items_route" if args.vs_rules_items_round_trip else
                              "first_scheduler_return" if args.boot_only else "fixed_allocation_replay",
                "candidate_admission": "forbidden",
                "scope": "original allocation diagnostic; no gameplay or performance admission"}
    paths = None
    processes = []
    streams = []
    inputs = {}
    try:
        route_flags = sum((args.boot_only, args.menu_round_trip,
                           args.vs_rules_items_round_trip))
        if route_flags > 1:
            raise ValueError("boot-only and original menu routes are separate diagnostic captures")
        if args.vs_rules_items_round_trip and getattr(args, "initial_gci", None) is None:
            raise ValueError("VS Rules/Items retail route requires its declared Everything unlocked GCI baseline")
        plan = scenario = plan_hash = None
        if not args.boot_only and not args.menu_round_trip and not args.vs_rules_items_round_trip:
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
                     getattr(args, "initial_gci", None),
                     ROOT / "web/gamecube-save.mjs",
                     ROOT / ".deps/melee/config/GALE01/symbols.txt",
                     Path(__file__), ROOT / "tools/original_boot_context.py",
                     ROOT / "tools/reference_allocation_capture.py",
                     ROOT / "tools/retail_allocation_profile.py",
                     ROOT / "tools/retail_allocation_menu.py"):
            if path is not None:
                inputs[str(path.resolve())] = retail._sha256(path)
        route_source_identities = {}
        if args.menu_round_trip or args.vs_rules_items_round_trip:
            source_files = (VS_RULES_ITEMS_ROUTE_SOURCE_FILES
                            if args.vs_rules_items_round_trip else MENU_ROUTE_SOURCE_FILES)
            for relative in source_files:
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
            source_export = metadata["owned_inputs"].get("source_profile_export")
            if source_export is not None:
                for key in ("source_run_report", "source_run_manifest"):
                    artifact = Path(source_export[key])
                    inputs[str(artifact)] = source_export[key + "_sha256"]
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
        if args.menu_round_trip or args.vs_rules_items_round_trip:
            route_identity_name = ("vs-rules-items-route-source-identities.json"
                                   if args.vs_rules_items_round_trip else
                                   "route-source-identities.json")
            write_json(evidence / route_identity_name, {
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
                "players": [
                    {"character_kind": 8, "costume": 0, "player_type": 0,
                     "cpu_level": 0, "team": 0, "stocks": 4,
                     "rumble_enabled": True},
                    {"character_kind": 8, "costume": 0, "player_type": 0,
                     "cpu_level": 0, "team": 0, "stocks": 4,
                     "rumble_enabled": True}],
                "disable_pausing": False, "stage": 32}}
        write_json(evidence / "menu-target.json", target)
        paths["menu_driver"] = evidence / "menu-driver.py"
        if args.menu_round_trip:
            from retail_allocation_menu import render_menu_round_trip_driver
            paths["menu_driver"].write_text(render_menu_round_trip_driver())
        elif args.vs_rules_items_round_trip:
            from retail_allocation_menu import render_vs_rules_items_round_trip_driver
            paths["menu_driver"].write_text(render_vs_rules_items_round_trip_driver())
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
        pipe_count = 4 if (args.menu_round_trip or args.vs_rules_items_round_trip) else (plan["active_player_count"] if plan else 0)
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
        commands.write_text(gdb_script(paths, args.boot_only, args.menu_round_trip,
                                       args.vs_rules_items_round_trip))
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
        elif args.vs_rules_items_round_trip:
            route_path = evidence / "cold-boot-vs-rules-items-route.jsonl"
            metadata["menu_route"] = verify_vs_rules_items_route(route_path)
            metadata["menu_route"]["input_commands"] = verify_vs_rules_items_route_commands(
                evidence / "cold-boot-input-commands.jsonl")
            metadata["menu_route"]["save_effect"] = verify_vs_rules_items_save_effect(
                args.initial_gci, paths["user"] / "GC",
                metadata["menu_route"]["committed_item_frequency"],
                int(metadata["menu_route"]["committed_item_mask"], 16))
            if args.capture_images:
                screenshots = retain_route_screenshots(
                    paths["user"] / "Dump/Frames", route_path, output,
                    route="vs_rules_items")
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
    parser.add_argument("--initial-gci", type=Path,
                        help="isolated source-generated Everything unlocked GCI matching the single file in USA/Card A")
    parser.add_argument("--capture-images", action="store_true",
                        help="use headless Dolphin OpenGL frame dumping and retain route screenshots")
    parser.add_argument("--boot-only", action="store_true", help="Stop at first scheduler return; diagnostic smoke only")
    parser.add_argument("--menu-round-trip", action="store_true",
                        help="Capture a cold-DOL CSS -> original menus -> title -> CSS route; diagnostic only")
    parser.add_argument("--vs-rules-items-round-trip", action="store_true",
                        help="Capture cold-DOL VS Rules/Items -> SSS -> live match -> Results -> retained CSS; diagnostic only")
    return capture(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
