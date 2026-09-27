#!/usr/bin/env python3
"""Run a bounded, ordinary-input Melee GCI save round trip in isolated Dolphin.

The Dolphin executable must provide the passive reference observer and Pipe
controller support used by this repository. The harness never writes guest
memory. Dolphin's GCI folder, user settings, controller pipe, observer stream,
and logs are all created beneath a new output directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "reference-capture/dolphin"))
from reference_capture_automation import PipeController, prepare_pipe  # noqa: E402
from reference_observer_stream import HEADER, MAX_PAYLOAD, _decode_record  # noqa: E402


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dolphin", required=True, type=Path, help="Headless Dolphin binary with reference observer and Pipe support")
    parser.add_argument("--disc", required=True, type=Path, help="Owned GALE01 revision 2 disc image")
    parser.add_argument("--gci", required=True, type=Path, help="WebMelee-exported GCI to load in USA Slot A")
    parser.add_argument("--out", required=True, type=Path, help="New ignored work directory for this isolated run")
    parser.add_argument("--dol-sha256", required=True, help="SHA-256 of the selected disc's main.dol")
    parser.add_argument("--source-revision", default="GALE01r2", help="Observer source revision label (default: GALE01r2)")
    parser.add_argument("--capture-id", default="save-profile-interop", help="ASCII observer capture identifier")
    parser.add_argument("--timeout-sec", type=int, default=180, help="Hard source-observation deadline, 30..300 seconds")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-fA-F]{64}", args.dol_sha256):
        parser.error("--dol-sha256 must contain 64 hexadecimal characters")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", args.capture_id):
        parser.error("--capture-id must contain only ASCII letters, numbers, dot, underscore, or hyphen")
    if not 30 <= args.timeout_sec <= 300:
        parser.error("--timeout-sec must be between 30 and 300")
    for label, path in (("Dolphin", args.dolphin), ("disc", args.disc), ("GCI", args.gci)):
        if not path.expanduser().is_file():
            parser.error(f"{label} file does not exist: {path}")
    args.dolphin = args.dolphin.expanduser().resolve()
    args.disc = args.disc.expanduser().resolve()
    args.gci = args.gci.expanduser().resolve()
    args.out = args.out.expanduser().resolve()
    if args.out.exists():
        parser.error(f"Refusing to overwrite output directory: {args.out}")
    return args


def main() -> int:
    args = parse_args()
    run = args.out
    user = run / "user"
    card_root = run / "gci-folder"
    usa = card_root / "USA"
    config = user / "Config"
    for directory in (config, usa, user / "GC"):
        directory.mkdir(parents=True)

    gci_copy = usa / args.gci.name
    gci_copy.write_bytes(args.gci.read_bytes())
    before_hash = sha256(gci_copy)
    (config / "Dolphin.ini").write_text("[Core]\nSIDevice0 = 6\n")
    (config / "GCPadNew.ini").write_text("[GCPad1]\nDevice = Pipe/0/pad1\n")
    fifo = prepare_pipe(user)

    stream = run / "observer.mwro"
    status_path = run / "observer-status.json"
    stdout_path = run / "dolphin.log"
    input_log_path = run / "controller-input.jsonl"
    event_log_path = run / "observed-controller-events.json"
    argv = [
        str(args.dolphin), "-u", str(user), "-v", "Null", "-a", "HLE",
        "-C", "Dolphin.Core.SlotA=8",
        "-C", f"Dolphin.Core.GCIFolderAPath={card_root}",
        "-C", "Dolphin.Core.SIDevice0=6",
        "-C", "Session.Core.SaveDataWritable=True",
        "-C", "Dolphin.DSP.Backend=No Audio Output",
        "-C", "Dolphin.Interface.ConfirmStop=False",
        "-e", str(args.disc),
    ]
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("SDL_", "MWRC_")) and key != "DOLPHIN_EMU_USERPATH"}
    env.update(
        MWRC_ENABLE="1", MWRC_CPU="JITARM64", MWRC_SOURCE_REV=args.source_revision,
        MWRC_OUTPUT=str(stream), MWRC_STATUS=str(status_path), MWRC_DOL_SHA256=args.dol_sha256.lower(),
        MWRC_CAPTURE_ID=args.capture_id, MWRC_SEQUENCE_ID=args.capture_id,
        LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8",
    )

    controller = PipeController(fifo, input_log_path)
    offset = sequence = 0
    latest_scene = None
    latest_tick = 0
    latest_flow = None
    first_title_tick = None
    first_main_tick = None
    menu_transition_tick = 0
    sent_progressive = sent_start = False
    last_prize_input_tick = 0
    main_down_count = 0
    settings_enter = rumble_enter = rumble_toggle = False
    rumble_exit = settings_exit = False
    finished = False
    records_seen = 0
    scene_transitions = []
    menu_transitions = []
    input_events = []

    def pulse(name: str, buttons: int, seconds: float = 0.12) -> None:
        event = {
            "name": name,
            "buttons_hex": f"0x{buttons:04x}",
            "issued_at_source_tick": latest_tick,
            "scene": latest_scene,
            "menu": latest_flow,
        }
        controller.pulse(buttons=buttons, seconds=seconds)
        event["input_log_tail"] = input_log_path.read_text().splitlines()[-2:]
        input_events.append(event)
        print(json.dumps(event), flush=True)

    def drain() -> None:
        nonlocal offset, sequence, latest_scene, latest_tick, latest_flow
        nonlocal first_title_tick, menu_transition_tick, records_seen
        if not stream.exists():
            return
        with stream.open("rb") as data:
            data.seek(offset)
            while True:
                header_bytes = data.read(HEADER.size)
                if len(header_bytes) != HEADER.size:
                    break
                fields = HEADER.unpack(header_bytes)
                if fields[8] > MAX_PAYLOAD:
                    raise RuntimeError("Observer payload exceeds the configured size limit")
                payload = data.read(fields[8])
                if len(payload) != fields[8]:
                    break
                row = _decode_record(fields, payload, f"observer record {sequence}")
                if row["seq"] != sequence:
                    raise RuntimeError(f"Observer sequence discontinuity at {sequence}")
                sequence += 1
                offset = data.tell()
                records_seen += 1
                if row["event"] in ("error", "end"):
                    raise RuntimeError(f"Observer emitted {row['event']}: {row['payload']}")
                if row["event"] != "boundary":
                    continue
                tick = row["source_tick"]
                for item in row["payload"]["slices"]:
                    raw = bytes.fromhex(item["hex"])
                    if item["name"] == "scene_kind":
                        scene = raw[0]
                        if scene != latest_scene:
                            scene_transitions.append({"source_tick": tick, "scene": scene})
                            print(json.dumps({"seq": row["seq"], "tick": tick, "scene": scene}), flush=True)
                        latest_scene = scene
                        latest_tick = tick
                        if scene == 0 and first_title_tick is None:
                            first_title_tick = tick
                    elif item["name"] == "menu_main_flow":
                        flow = {
                            "cur_menu": raw[0], "prev_menu": raw[1],
                            "hovered": int.from_bytes(raw[2:4], "big"),
                            "confirmed": raw[4], "buttons": int.from_bytes(raw[8:16], "big"),
                        }
                        if latest_flow is None or flow["cur_menu"] != latest_flow["cur_menu"]:
                            menu_transition_tick = tick
                            menu_transitions.append({"source_tick": tick, **flow})
                        if flow != latest_flow:
                            print(json.dumps({"seq": row["seq"], "tick": tick, "menu": flow}), flush=True)
                        latest_flow = flow
                        latest_tick = tick

    with stdout_path.open("wb") as stdout:
        process = subprocess.Popen(argv, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                   stdout=stdout, stderr=subprocess.STDOUT, start_new_session=True)
        deadline = time.monotonic() + args.timeout_sec
        try:
            while time.monotonic() < deadline and not finished:
                drain()
                if not sent_progressive and latest_scene == 28 and latest_tick >= 60:
                    pulse("Acknowledge Progressive Scan default No", 0x0100)
                    sent_progressive = True
                elif sent_progressive and not sent_start and latest_scene == 0 and first_title_tick is not None and latest_tick >= first_title_tick + 30:
                    pulse("START from title screen", 0x1000)
                    sent_start = True
                elif sent_start and latest_scene == 39 and latest_tick >= last_prize_input_tick + 24:
                    pulse("Acknowledge Melee prize/unlock screen", 0x0100)
                    last_prize_input_tick = latest_tick
                elif sent_start and latest_scene == 1 and latest_flow:
                    flow = latest_flow
                    if flow["cur_menu"] == 0 and first_main_tick is None:
                        first_main_tick = latest_tick
                    if (flow["cur_menu"] == 0 and main_down_count < 3 and flow["hovered"] == main_down_count and
                            latest_tick >= (first_main_tick or 0) + 10):
                        pulse(f"D-pad Down selects main menu item {main_down_count + 1}", 0x0004, 0.08)
                        main_down_count += 1
                    elif flow["cur_menu"] == 0 and main_down_count == 3 and flow["hovered"] == 3 and not settings_enter:
                        pulse("A enters Settings", 0x0100)
                        settings_enter = True
                    elif flow["cur_menu"] == 4 and settings_enter and not rumble_enter and latest_tick >= menu_transition_tick + 8:
                        pulse("A enters Settings Rumble", 0x0100)
                        rumble_enter = True
                    elif flow["cur_menu"] == 19 and rumble_enter and not rumble_toggle and latest_tick >= menu_transition_tick + 32:
                        pulse("A turns Controller 1 Rumble off", 0x0100)
                        rumble_toggle = True
                    elif flow["cur_menu"] == 19 and rumble_toggle and not rumble_exit and latest_tick >= menu_transition_tick + 40:
                        pulse("B exits Rumble settings", 0x0200)
                        rumble_exit = True
                    elif flow["cur_menu"] == 4 and rumble_exit and not settings_exit and latest_tick >= menu_transition_tick + 8:
                        pulse("B exits Settings", 0x0200)
                        settings_exit = True
                    elif flow["cur_menu"] == 0 and settings_exit and latest_tick >= menu_transition_tick + 30:
                        finished = True
                if process.poll() is not None:
                    break
                time.sleep(0.025)
        finally:
            drain()
            if process.poll() is None:
                process.send_signal(signal.SIGINT)
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.send_signal(signal.SIGTERM)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()

    after_hash = sha256(gci_copy)
    required_inputs = {
        "progressive_prompt": sent_progressive,
        "title_start": sent_start,
        "main_menu_navigation": main_down_count == 3,
        "settings_enter": settings_enter,
        "rumble_settings_enter": rumble_enter,
        "rumble_change": rumble_toggle,
        "settings_exit": settings_exit,
        "completed_controller_sequence": finished,
    }
    if process.returncode != 0:
        raise RuntimeError(f"Dolphin returned {process.returncode}; see {stdout_path}")
    if not all(required_inputs.values()):
        raise TimeoutError(f"Dolphin did not complete the expected ordinary-input sequence: {required_inputs}")
    if before_hash == after_hash:
        raise RuntimeError("Melee left the GCI unchanged; no persistent save was observed")
    if not status_path.exists():
        raise RuntimeError("Dolphin did not publish the observer status receipt")
    observer_status = json.loads(status_path.read_text())
    if observer_status.get("invalid") or observer_status.get("error"):
        raise RuntimeError(f"Dolphin observer marked the run invalid: {observer_status}")

    summary = {
        "client": {
            "dolphin_version": subprocess.run([str(args.dolphin), "--version"], capture_output=True, text=True,
                                                timeout=10, check=False).stdout.strip(),
            "binary_sha256": sha256(args.dolphin),
        },
        "disc": {"path": str(args.disc), "sha256": sha256(args.disc), "main_dol_sha256": args.dol_sha256.lower()},
        "source_revision": args.source_revision,
        "argv": argv,
        "save": {
            "input_path": str(args.gci), "input_sha256": before_hash,
            "dolphin_output_path": str(gci_copy), "output_sha256": after_hash,
            "output_bytes": gci_copy.stat().st_size,
        },
        "result": "pass",
        "return_code": process.returncode,
        "records_seen": records_seen,
        "observer_status": observer_status,
        "scene_transitions": scene_transitions,
        "menu_transitions": menu_transitions,
        "required_inputs": required_inputs,
        "controller_events": input_events,
        "controller_input_log": input_log_path.read_text(),
        "logs": {"stdout": str(stdout_path), "observer": str(stream), "observer_status": str(status_path)},
        "policy": {"video_backend": "Null", "audio_backend": "No Audio Output",
                   "save_data_writable": True, "user_directory": str(user),
                   "card_directory": str(card_root)},
    }
    (run / "roundtrip-receipt.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("SUMMARY " + json.dumps(summary, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
