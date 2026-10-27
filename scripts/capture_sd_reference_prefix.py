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
import threading

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
from reference_input_stream import validate_status, validate_stream as validate_input_stream


class BoundedIntentController(DualPipeController):
    """Scope-local pre-write limits; historical controllers remain unchanged."""
    def __init__(self,*args,**kwargs):
        super().__init__(*args,**kwargs)
        self.intent_records=0

    def write(self,port,pad,*,action):
        from original_competitive_timeout import CAPS
        # ASCII labels <=96 guarantee the existing JSON serializer's line is
        # <=512 bytes, including two escaped quotes per character and ns time.
        require(isinstance(action,str) and all(32<=ord(c)<=126 for c in action) and len(action)<=96 and
                len(pad)==22 and self.intent_records<CAPS["intent_records"],
                "Ordinary input intention identity/record cap")
        size=self.log.stat().st_size if self.log.exists() else 0
        require(size+512<=CAPS["intent_bytes"], "Ordinary input intention byte cap")
        self.intent_records+=1
        super().write(port,pad,action=action)


STADIUM_CAPTURE_CAPS = dict(observer_bytes=64*1024*1024, input_bytes=16*1024*1024,
                            observer_records=16384, intent_bytes=1024*1024,
                            intent_records=4096, log_bytes=8*1024*1024)
STADIUM_WHOLE_LONG_IDLE_PROFILE = {
    "profile": "selected_b_3500_v1", "selected_matches": [1],
    "comparison_post_go_ticks": 3500, "natural_tail_max": 4,
    "raw_omission_kinds": ["draw_enter", "draw_return"],
}
STADIUM_WHOLE_PAIRED_IDLE_PROFILE = {
    "profile": "paired_idle_scene_endpoints_v1", "selected_matches": [0, 1],
    "match_count": 2, "comparison_post_go_ticks": 3500, "natural_tail_max": 4,
    "raw_omission_kinds": ["draw_enter", "draw_return"],
}
STADIUM_WHOLE_LONG_IDLE_RECORD_CAP = 77602
STADIUM_WHOLE_STOCKOUT_PROFILE = {
    "profile": "selected_b_natural_zero_stockout_v1", "match_count": 3,
    "selected_match": 1, "initial_stock_pair": [4, 4],
    "p1_transitions": [3, 2, 1, 0], "p2_stocks": 4,
    "required_mode_before_exit": 9, "required_outcome": 2,
}
STADIUM_WHOLE_LONG_IDLE_COMPACT_BASENAME = "stadium-whole-selected-b-compact-rng.mwrl"
STADIUM_WHOLE_PAIRED_IDLE_COMPACT_BASENAME = "stadium-whole-paired-compact-rng.mwrl"


def build_stadium_whole_long_idle_compact_launch(output, *, paired_idle=False):
    """Build a fresh selected-B or explicitly paired compact sidecar config."""
    require(type(paired_idle) is bool, "Whole Stadium paired compact option must be boolean")
    root = Path(output).resolve()
    require(root.is_dir() and not Path(output).is_symlink(),
            "Whole Stadium long-idle compact output root is not an owned directory")
    matches = [0, 1] if paired_idle else [1]
    compact_path = root / (STADIUM_WHOLE_PAIRED_IDLE_COMPACT_BASENAME if paired_idle else
                           STADIUM_WHOLE_LONG_IDLE_COMPACT_BASENAME)
    require(not compact_path.exists() and not compact_path.is_symlink(),
            "Whole Stadium long-idle compact output must be fresh")
    environment = {
        "MWRC_STADIUM_COMPACT_RNG_OUTPUT": str(compact_path),
        "MWRC_STADIUM_COMPACT_RNG_MATCHES": ",".join(map(str, matches)),
    }
    receipt = {
        "schema": "melee-web-stadium-paired-idle-compact-launch-v1" if paired_idle else
                  "melee-web-stadium-whole-long-idle-compact-launch-v1",
        "output_path": str(compact_path),
        "matches": matches,
        "output_absent_at_prepare": True,
        "lifecycle_parity": False,
    }
    return environment, receipt


def validate_stadium_whole_long_idle_outputs(raw, status_path, output_root,
                                             compact_launch, driver_window, dol_sha256):
    """Join the full raw stream, final observer status, and selected-B compact ledger."""
    from reference_observer_stream import iter_records
    from stadium_whole_long_idle import LongIdleBObservations, PROFILE
    from stadium_rng_compact_live import decode_live_compact

    require(PROFILE == STADIUM_WHOLE_LONG_IDLE_PROFILE,
            "Whole Stadium driver and strict source validator profiles differ")
    primary_status = read_status(status_path)
    require(primary_status.get("state") == "completed" and
            primary_status.get("completed") is True and
            primary_status.get("invalid") is False and
            primary_status.get("error") is None and
            primary_status.get("stadium_whole_long_idle") == PROFILE and
            primary_status.get("stadium_whole_long_idle_window_complete") is True,
            "Whole Stadium long-idle final status lacks exact natural completion/profile")
    observations = LongIdleBObservations()
    for row in iter_records(raw, max_bytes=STADIUM_CAPTURE_CAPS["observer_bytes"],
                            max_records=STADIUM_WHOLE_LONG_IDLE_RECORD_CAP):
        observations.accept(row)
    source_window = observations.finish()

    require(isinstance(driver_window, dict) and driver_window.get("profile") == PROFILE["profile"] and
            driver_window.get("match_index") == 1 and
            driver_window.get("comparison_post_go_ticks") == 3500 and
            driver_window.get("comparison_typed_state_count") == 3501 and
            driver_window.get("typed_state_count_including_natural_tail") == source_window["state_samples"] and
            len(driver_window.get("natural_tail_source_ticks", [])) == source_window["natural_tail_ticks"],
            "Whole Stadium driver window differs from the exact full-stream validator")
    go = driver_window.get("go")
    close = driver_window.get("first_later_natural_draw_return")
    require(isinstance(go, dict) and
            (go.get("sequence"), go.get("source_tick")) ==
                (source_window["go_sequence"], source_window["go_tick"]) and
            isinstance(close, dict) and close == source_window["close"],
            "Whole Stadium driver GO/close coordinates differ from the exact full stream")

    output_root = Path(output_root).resolve()
    compact_path = output_root / STADIUM_WHOLE_LONG_IDLE_COMPACT_BASENAME
    require(isinstance(compact_launch, dict) and
            compact_launch.get("schema") == "melee-web-stadium-whole-long-idle-compact-launch-v1" and
            compact_launch.get("matches") == [1] and
            compact_launch.get("output_path") == str(compact_path) and
            compact_launch.get("output_absent_at_prepare") is True and
            compact_launch.get("lifecycle_parity") is False and
            compact_path.is_file() and not compact_path.is_symlink(),
            "Whole Stadium selected-B compact launch/output identity differs")
    compact_status = primary_status.get("compact_rng")
    require(isinstance(compact_status, dict) and
            compact_status.get("schema") == "melee-web-live-compact-rng-status-v1" and
            compact_status.get("go_mask") == 1 and
            compact_status.get("closed_window_mask") == 1 and
            compact_status.get("requested_window_mask") == 1 and
            type(compact_status.get("written_rows")) is int and
            compact_status.get("lifecycle_parity") is False,
            "Whole Stadium selected-B compact status is incomplete or out of scope")
    capture_id = "stadium-whole3"
    sequence_id = "primary3-allocation2-feedback"
    require(isinstance(dol_sha256, str) and len(dol_sha256) == 64 and
            all(ch in "0123456789abcdef" for ch in dol_sha256),
            "Whole Stadium compact DOL identity is malformed")
    ledger = decode_live_compact(compact_path.read_bytes(), capture_id=capture_id,
        sequence_id=sequence_id, dol_sha256=dol_sha256, primary_status=primary_status)
    require(ledger["header"].get("matches") == [1] and
            len(ledger["windows"]) == 1 and ledger["record_count"] > 0 and
            compact_status["written_rows"] == ledger["record_count"],
            "Whole Stadium selected-B compact ledger lacks its exact one-window output")
    compact_window = ledger["windows"][0]
    require(compact_window["match"] == 1 and
            compact_window["go_sequence"] == source_window["go_sequence"] and
            compact_window["go_tick"] == source_window["go_tick"] and
            compact_window["target_source_tick"] == source_window["go_tick"] + 3500 and
            compact_window["target_source_sequence"] == driver_window["target_source_tick"]["sequence"] and
            compact_window["close_sequence"] == source_window["close"]["sequence"] and
            compact_window["close_tick"] == source_window["close"]["source_tick"],
            "Whole Stadium compact RNG window does not join the actual GO/target/DrawReturn")
    compact_summary = {
        "schema": ledger["schema"],
        "output_path": str(compact_path),
        "output_sha256": ledger["output_sha256"],
        "output_bytes": ledger["output_bytes"],
        "record_count": ledger["record_count"],
        "header": ledger["header"],
        "windows": ledger["windows"],
        "unresolved_owner_events": ledger["unresolved_owner_events"],
        "owner_comparison_ready": ledger["owner_comparison_ready"],
        "source_schedule_verified": False,
        "all_rng_writers_observed": False,
        "lifecycle_parity": False,
        "C3": False,
    }
    return source_window, compact_summary


def validate_stadium_paired_idle_window_coordinates(driver, source):
    """Join the live ordinal-bearing window to strict raw source coordinates."""
    go = driver.get("go")
    target = driver.get("target_source_tick")
    close = driver.get("first_later_natural_draw_return")
    source_target = source.get("target_source_tick")
    source_close = source.get("close")
    require(isinstance(go, dict) and
            (go.get("sequence"), go.get("source_tick")) ==
                (source.get("go_sequence"), source.get("go_tick")) and
            isinstance(target, dict) and isinstance(source_target, dict) and
            target == dict(source_target, ordinal=3500) and
            target.get("source_tick") == go.get("source_tick") + 3500 and
            isinstance(close, dict) and close == source_close and
            driver.get("typed_state_count_including_natural_tail") ==
                3501 + source.get("natural_tail_ticks"),
            "paired GO/target/natural DrawReturn coordinates differ from the full stream")


def validate_stadium_paired_idle_outputs(raw, status_path, output_root,
                                         compact_launch, driver_windows, dol_sha256):
    """Join two actual GO windows across the full raw stream and compact ledger."""
    from reference_observer_stream import iter_records
    from stadium_whole_long_idle import LongIdleBObservations, PAIRED_IDLE_PROFILE
    from stadium_rng_compact_live import decode_live_compact

    require(PAIRED_IDLE_PROFILE == STADIUM_WHOLE_PAIRED_IDLE_PROFILE,
            "paired idle driver and strict source validator profiles differ")
    primary_status = read_status(status_path)
    require(primary_status.get("state") == "completed" and
            primary_status.get("completed") is True and
            primary_status.get("invalid") is False and
            primary_status.get("error") is None and
            primary_status.get("stadium_whole_b_state") is True and
            primary_status.get("stadium_observed_matches") == [0, 1] and
            primary_status.get("stadium_b_observations_complete") is True and
            primary_status.get("stadium_whole_long_idle") == PAIRED_IDLE_PROFILE and
            primary_status.get("stadium_whole_long_idle_window_complete") is True and
            isinstance(primary_status.get("paired_go_ticks"), list) and
            len(primary_status["paired_go_ticks"]) == 2 and
            isinstance(primary_status.get("paired_source_ticks"), list) and
            len(primary_status["paired_source_ticks"]) == 2,
            "paired whole-session final status lacks exact two-window natural completion")

    observations = LongIdleBObservations(profile=PAIRED_IDLE_PROFILE)
    for row in iter_records(raw, max_bytes=STADIUM_CAPTURE_CAPS["observer_bytes"],
                            max_records=STADIUM_WHOLE_LONG_IDLE_RECORD_CAP):
        observations.accept(row)
    source_result = observations.finish()
    source_windows = source_result.get("windows")
    require(source_result.get("profile") == PAIRED_IDLE_PROFILE and
            source_result.get("allocation_history_complete") is False and
            source_result.get("registration_lifetime_complete") is False and
            isinstance(source_windows, list) and len(source_windows) == 2 and
            [window.get("match_index") for window in source_windows] == [0, 1],
            "paired full-stream validator lacks exactly the two declared windows")
    require(isinstance(driver_windows, list) and len(driver_windows) == 2,
            "paired driver did not retain exactly two window results")
    for index, (driver, source) in enumerate(zip(driver_windows, source_windows)):
        require(isinstance(driver, dict) and
                driver.get("schema") == "melee-web-stadium-whole-paired-idle-window-v1" and
                driver.get("profile") == PAIRED_IDLE_PROFILE["profile"] and
                driver.get("match_index") == index == source.get("match_index") and
                driver.get("comparison_post_go_ticks") == 3500 and
                driver.get("comparison_typed_state_count") == 3501 and
                driver.get("typed_state_count_including_natural_tail") == source.get("state_samples") and
                driver.get("four_port_consumed_pad_count") == source.get("state_samples") and
                len(driver.get("natural_tail_source_ticks", [])) == source.get("natural_tail_ticks"),
                "paired driver window differs from full-stream count/profile validation")
        validate_stadium_paired_idle_window_coordinates(driver, source)
        require(primary_status["paired_go_ticks"][index] == source["go_tick"] and
                primary_status["paired_source_ticks"][index] == source["state_samples"],
                "paired terminal status counters differ from the validated source window")

    output_root = Path(output_root).resolve()
    compact_path = output_root / STADIUM_WHOLE_PAIRED_IDLE_COMPACT_BASENAME
    require(isinstance(compact_launch, dict) and
            compact_launch.get("schema") == "melee-web-stadium-paired-idle-compact-launch-v1" and
            compact_launch.get("matches") == [0, 1] and
            compact_launch.get("output_path") == str(compact_path) and
            compact_launch.get("output_absent_at_prepare") is True and
            compact_launch.get("lifecycle_parity") is False and
            compact_path.is_file() and not compact_path.is_symlink(),
            "paired compact launch/output identity differs")
    compact_status = primary_status.get("compact_rng")
    require(isinstance(compact_status, dict) and
            compact_status.get("schema") == "melee-web-live-compact-rng-status-v1" and
            compact_status.get("go_mask") == 3 and
            compact_status.get("closed_window_mask") == 3 and
            compact_status.get("requested_window_mask") == 3 and
            type(compact_status.get("written_rows")) is int and
            compact_status.get("lifecycle_parity") is False,
            "paired compact final status lacks both exact windows")
    require(isinstance(dol_sha256, str) and len(dol_sha256) == 64 and
            all(ch in "0123456789abcdef" for ch in dol_sha256),
            "paired compact DOL identity is malformed")
    ledger = decode_live_compact(compact_path.read_bytes(),
        capture_id="stadium-whole-paired-idle",
        sequence_id="primary2-paired-idle-scene-endpoints",
        dol_sha256=dol_sha256, primary_status=primary_status)
    windows = ledger.get("windows")
    require(ledger["header"].get("matches") == [0, 1] and
            isinstance(windows, list) and len(windows) == 2 and
            [window.get("match") for window in windows] == [0, 1] and
            ledger["record_count"] > 0 and
            compact_status["written_rows"] == ledger["record_count"],
            "paired compact ledger lacks exact ordered two-window output")
    compact_windows = []
    for index, (window, source) in enumerate(zip(windows, source_windows)):
        target = driver_windows[index]["target_source_tick"]
        close = driver_windows[index]["first_later_natural_draw_return"]
        require(window["go_sequence"] == source["go_sequence"] and
                window["go_tick"] == source["go_tick"] and
                window["target_source_tick"] == source["go_tick"] + 3500 and
                window["target_source_sequence"] == target["sequence"] and
                window["close_sequence"] == close["sequence"] and
                window["close_tick"] == close["source_tick"],
                "paired compact RNG window does not join the actual GO/target/DrawReturn")
        compact_windows.append(window)
    compact_summary = {
        "schema": ledger["schema"], "output_path": str(compact_path),
        "output_sha256": ledger["output_sha256"], "output_bytes": ledger["output_bytes"],
        "record_count": ledger["record_count"], "header": ledger["header"],
        "windows": compact_windows, "unresolved_owner_events": ledger["unresolved_owner_events"],
        "owner_comparison_ready": ledger["owner_comparison_ready"],
        "source_schedule_verified": False, "all_rng_writers_observed": False,
        "lifecycle_parity": False, "C3": False,
    }
    source_result["scope"] = "two actual GO+3500 windows; scene endpoint scope remains separate"
    return source_result, compact_summary


def check_stadium_capture_bounds(raw, native, deadline, records, *, original_idle_3500=False,
                                 whole_long_idle=False):
    """Bound accepted evidence and stop the owned producer on the next check."""
    require(time.monotonic() < deadline, "Stadium capture wall deadline exhausted")
    require(records <= (77602 if original_idle_3500 else
                        STADIUM_WHOLE_LONG_IDLE_RECORD_CAP if whole_long_idle else
                        STADIUM_CAPTURE_CAPS["observer_records"]),
            "Stadium observer record cap")
    for path, key in ((raw, "observer_bytes"), (native, "input_bytes")):
        require(not path.exists() or path.stat().st_size <= STADIUM_CAPTURE_CAPS[key],
                "Stadium capture byte cap: " + key)


# This first replay experiment accepts exactly the retained v6 source, not an
# arbitrary stream or a host-input substitute. Footer is not a controller poll.
STADIUM_REPLAY_INPUT_SHA256 = "57e91222a280f93c861a849c8af404ae4272767ead1debf62312a5be48c473fe"
STADIUM_REPLAY_OBSERVER_SHA256 = "361e8c1108cc2d02ba94d1f3b3be9612a80d0227672445cd4a2b0e2119917778"
STADIUM_REPLAY_OBSERVER_STATUS_SHA256 = "05a08641404bda9f87ac4c20b1a44a982ff0470ff3a9b04861c03ba542866c3f"


def stadium_replay_preflight(input_replay, output, build_manifest):
    source = Path(input_replay).resolve()
    output = Path(output).resolve()
    manifest = Path(build_manifest).resolve()
    require(not output.exists(), "Stadium replay output already exists")
    require(output not in manifest.parents and manifest != output,
            "Stadium relocated manifest must be prepared outside the absent output")
    require(output not in source.parents and source != output,
            "Stadium replay input must be retained outside the absent output")
    require(not Path(input_replay).is_symlink(), "Stadium replay input must not redirect")
    require(source.stat().st_size <= STADIUM_CAPTURE_CAPS["input_bytes"],
            "Stadium replay source input byte cap")
    summary = validate_input_stream(source)
    require(summary["bytes"] == 188296 and summary["events"] == 4706 and
            summary["sha256"] == STADIUM_REPLAY_INPUT_SHA256 and
            summary["ports"] == {"0": 2353, "1": 2353} and
            summary["operations"] == {"pad_status": 4706, "adapter_connection": 0},
            "Stadium replay source differs from the pinned complete v6 MWRI")
    source_status = source.with_name("input-status.json")
    validate_status(source_status, mode="record", events=4706, require_complete=True)
    original = source.with_name("observer.bin")
    original_status = source.with_name("observer-status.json")
    for path, expected in ((original, STADIUM_REPLAY_OBSERVER_SHA256),
                           (original_status, STADIUM_REPLAY_OBSERVER_STATUS_SHA256)):
        require(path.is_file() and not path.is_symlink() and
                path.stat().st_size <= STADIUM_CAPTURE_CAPS["observer_bytes"] and
                hashlib.sha256(path.read_bytes()).hexdigest() == expected,
                "Stadium replay original observer identity differs")
    return {"source": str(source), "summary": summary,
            "source_status_sha256": hashlib.sha256(source_status.read_bytes()).hexdigest(),
            "original_observer": str(original), "original_status": str(original_status)}


def stadium_replay_complete(receipt, status):
    # Replay has no fresh MWRI writer/footer. Recheck the retained source and
    # independently require the current process's final replay status.
    summary = validate_input_stream(receipt["source"])
    require(summary == receipt["summary"], "Stadium replay input changed during capture")
    return validate_status(status, mode="replay", events=summary["events"], require_complete=True)


class StadiumIntentController(DualPipeController):
    """Pre-write intention bounds for the separate Stadium experiment only."""
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.intent_records = 0

    def write(self, port, pad, *, action):
        require(isinstance(action, str) and len(action) <= 96 and
                all(32 <= ord(c) <= 126 for c in action) and len(pad) == 22 and
                self.intent_records < STADIUM_CAPTURE_CAPS["intent_records"],
                "Stadium intention identity/record cap")
        size = self.log.stat().st_size if self.log.exists() else 0
        require(size + 512 <= STADIUM_CAPTURE_CAPS["intent_bytes"],
                "Stadium intention byte cap")
        self.intent_records += 1
        super().write(port, pad, action=action)


class BoundedLog:
    """Drain only the owned child's stdout; retain a hard bounded failed log."""
    def __init__(self,source,target,cap):
        self.source,self.target,self.cap=source,target,cap
        self.size=0
        self.error=None
        self.thread=threading.Thread(target=self._drain,name="ordinary-owned-log",daemon=True)
        self.thread.start()

    def _drain(self):
        try:
            while True:
                block=self.source.read(4096)
                if not block: break
                remaining=self.cap-self.size
                kept=block[:remaining]
                self.target.write(kept); self.target.flush(); self.size+=len(kept)
                if len(kept)!=len(block): self.error="Ordinary native log byte cap"
        except (OSError,ValueError) as error:
            self.error=str(error)
        finally:
            self.source.close()

    def check(self):
        require(self.error is None,self.error or "Ordinary owned log failed")

    def finish(self):
        self.thread.join(timeout=5)
        require(not self.thread.is_alive(),"Ordinary owned log drain did not finish after child cleanup")
        self.check()


def rules_dolphin_command(dolphin, user, disc):
    command = dolphin_command(Path(dolphin), Path(user), Path("unused"), Path(disc),
                              cpu="JITARM64", cold_boot=True, audible=False)
    return command + ["-p", "headless", "-C", "Session.Core.SaveDataWritable=False",
                      "-C", "Dolphin.Interface.ConfirmStop=False"]


def check_owned_native_wait(process, bounded_log):
    """Check only the direct child while a declared observer record is absent."""
    returncode = process.poll()
    require(returncode is None,
            "Owned original native child exited before observer record: returncode " + str(returncode))
    if bounded_log is not None:
        bounded_log.check()


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


def wait_transform_terminal_statuses(observer, native, deadline, wait_check=None, *,
                                     input_mode="record", items_only=False):
    """Wait for the declared observer terminal and mode-specific MWRI status."""
    require(input_mode in ("record", "replay"), "Native input mode is unknown")
    while time.monotonic() < deadline:
        if wait_check is not None:
            wait_check()
        primary = read_status(observer) if Path(observer).is_file() else None
        inputs = validate_status(native, mode=input_mode, require_complete=False) if Path(native).is_file() else None
        if primary:
            require(not primary["invalid"] and primary["error"] is None,
                    "Sheik observer failed during finalization")
        if inputs:
            require(not inputs["invalid"] and inputs["error"] is None,
                    "Sheik native input writer failed during finalization")
        terminal = "interrupted" if items_only else "completed"
        if primary and primary["state"] == terminal and inputs and inputs["complete"]:
            return primary, inputs
        time.sleep(0.02)
    raise SdDiagnosticError("Sheik independent writer finalization deadline expired")


def _transform_slices(payload):
    """Decode the transform observer's typed source slices without aliasing."""
    from reference_observer_stream import SLICE_NAMES
    values = payload.get("slices")
    require(isinstance(values, list) and len(values) <= 64,
            "Transform boundary slice inventory is invalid")
    data, metadata = {}, {}
    for item in values:
        require(isinstance(item, dict) and
                set(item) == {"name", "tag", "flags", "address", "size", "hex"} and
                all(type(item[key]) is int for key in ("tag", "flags", "address", "size")),
                "Transform boundary slice identity differs")
        require(item["tag"] in SLICE_NAMES and item["name"] == SLICE_NAMES[item["tag"]],
                "Transform boundary slice name differs from its typed observer tag")
        key = (item["tag"], item["flags"])
        require(key not in data, "Transform boundary contains a duplicate source slice")
        try:
            raw = bytes.fromhex(item["hex"])
        except (TypeError, ValueError) as error:
            raise SdDiagnosticError("Transform boundary slice is not hexadecimal") from error
        require(raw.hex() == item["hex"] and len(raw) == item["size"] and len(raw) <= 0x10000 and
                0x80000000 <= item["address"] <= 0x81800000 - len(raw),
                "Transform boundary source slice bounds differ")
        data[key], metadata[key] = raw, item
    return data, metadata


def _transform_consumed_ports(payload, sequence):
    from sheik_transform_prefix import transform_consumed_ports
    return transform_consumed_ports(payload, sequence)


class SheikTransformPrefixReceiver:
    """Small menu/input adapter over the existing passive transform MWRO stream."""
    def __init__(self, plan, menus, *, items_only=False):
        self.items_only = items_only
        self.transform_route = menus["scope"] == "sheik_transform_prefix"
        self.items_progress = None
        if self.transform_route:
            from sheik_transform_prefix import TransformItemsProgress
            self.items_progress = TransformItemsProgress(items_only=items_only)
        from sd_original_menu_plan import route_pads
        self.plan, self.menus = plan, menus
        self.allowed_menu_pads = route_pads(menus)
        self.seq = 0
        self.ended = False
        self.menu_polls = 0
        self.menu_consumed = 0
        self.last_pad = None
        self.latest_menu = None
        self.css = None
        self.stage = None
        self.setup_seen = False
        self.setup_hex = None
        self.setup_sample = None
        self.active_source_ticks = 0
        self.last_source_tick = None
        self.source_samples = []
        self.consumed_samples = []
        self.pad_queue_records = []
        self.neutral_consume_sequence = None
        self.grounded_zelda_sequence = None
        self.down_b_consume_sequence = None
        self.down_b_consumed = False
        self.down_b_held = False
        self.down_b_released = False
        self.action_seen = False
        self.after_swap = None
        self.post_swap_neutral = None
        self._stage_identity = None
        self._stage_stable_polls = 0
        self.css_live_owner_sequence = None
        self.sss_live_owner_sequence = None
        self.items_lock_transitions = []
        self.items_unlock_sequence = None
        self.items_row_up_consume_sequence = None
        self.items_row_up_release_sequence = None
        self.items_row_up_held = False

    def _sync_items_progress(self):
        p = self.items_progress
        self.items_lock_transitions = p.transitions
        self.items_unlock_sequence = p.unlock
        self.items_row_up_consume_sequence = p.up
        self.items_row_up_release_sequence = p.release
        self.items_row_up_held = p.held

    def _menu_owner_pad_poll(self, payload):
        from sheik_transform_prefix import _menu_owner_pad_poll
        return _menu_owner_pad_poll(payload)

    def _pad_poll(self, row):
        from sd_reference_diagnostic import menu_state, css_state
        payload = row["payload"]
        if self.transform_route:
            from sheik_transform_prefix import PAD_POLL_PC, _transform_menu_state
            require(payload.get("pc") == PAD_POLL_PC,
                    "Transform menu owner sample is not at the pinned original PadPoll boundary")
        data, _ = _transform_slices(payload)
        self.menu_polls += 1
        if self.transform_route:
            self.items_progress.observe(row)
            self._sync_items_progress()
            scene_slices = [item for item in payload["slices"] if item["tag"] == 40]
            require(len(scene_slices) <= 1 and
                    (not scene_slices or scene_slices[0]["flags"] == 0),
                    "Transform PadPoll has a duplicate or ambiguous scene owner")
            if not scene_slices:
                require(not any(item["tag"] == 56 for item in payload["slices"]),
                        "Transform PadPoll exposes the Items lock without a scene owner")
        if (40, 0) in data:
            self.latest_menu = (_transform_menu_state(payload) if self.transform_route
                                else menu_state(data))
            menu_owner = self._menu_owner_pad_poll(payload)
            if self.latest_menu.get("scene") == 8:
                if menu_owner == "css":
                    if self.css_live_owner_sequence is None:
                        self.css_live_owner_sequence = row["seq"]
                    css_owners = {(48, 0), (44, 0), (43, 0), (43, 1), (47, 0), (47, 1)}
                    self.css = (css_state(data, source_slots=(0, 1))
                                if css_owners <= data.keys() else None)
                else:
                    self.css = None
            else:
                self.css = None
            if (self.latest_menu.get("scene") == 9 and
                    menu_owner in ("sss", "sss_navigation")):
                index, kind = data.get((41, 0)), data.get((42, 0))
                metadata = { (item["tag"], item["flags"]): item
                             for item in payload["slices"] }
                if index is not None:
                    require(len(index) == 1 and index[0] <= 30 and
                            metadata[(41, 0)]["address"] == 0x804D6CAE,
                            "SSS live source index identity differs")
                    if index[0] == 30:
                        require(kind is None, "SSS random row unexpectedly has a stage owner")
                        stage_kind = None
                    else:
                        require(kind is not None and len(kind) == 1 and
                                metadata[(42, 0)]["address"] == 0x803F06D0 + index[0] * 0x1C + 0x0B,
                                "SSS live source kind identity differs")
                        stage_kind = kind[0]
                    identity = (index[0], stage_kind)
                    self._stage_stable_polls = self._stage_stable_polls + 1 if identity == self._stage_identity else 1
                    self._stage_identity = identity
                    self.stage = {"index": index[0], "kind": stage_kind,
                                  "stable_polls": self._stage_stable_polls}
                    if self.sss_live_owner_sequence is None:
                        require(self.css_live_owner_sequence is not None and
                                self.css_live_owner_sequence < row["seq"],
                                "SSS live owner preceded ordered CSS live-owner poll")
                        self.sss_live_owner_sequence = row["seq"]
                else:
                    self.stage = None
            else:
                self.stage = None

    def _pad_consume(self, row):
        from retail_input_plan import DISCONNECTED_PAD
        from sheik_transform_prefix import _validate_consumed_pad
        ports, errors, queue_record = _transform_consumed_ports(row["payload"], row["seq"])
        self.menu_consumed += 1
        self.pad_queue_records.append({"sequence": row["seq"], **queue_record})
        self.last_pad = ports
        if not self.setup_seen:
            require(ports[2:] == [DISCONNECTED_PAD, DISCONNECTED_PAD] and errors == [0, 0, -1, -1] and
                    (ports[0], ports[1]) in self.allowed_menu_pads,
                    "Original menu consumed PAD escaped its declared P1/P2 route alphabet")
            self.consumed_samples.append({"sequence": row["seq"], "phase": "menu",
                                          "ports": ports, "errors": errors})
            if self.transform_route:
                self.items_progress.observe(row)
                self._sync_items_progress()
            return
        down_b = _validate_consumed_pad(row["payload"], row["seq"])
        witness = self.plan["authored_recipe"]["input_witness"]
        require(errors == witness["expected_pad_errors"],
                "Transform input PAD error statuses differ from the authored four-port declaration")
        expected_press, expected_release = self.plan["frames"]
        require(ports == expected_press or ports == expected_release,
                "Consumed source PAD differs from exact B/Y=-80 or neutral-release witness")
        if ports == expected_press:
            require(not self.down_b_released,
                    "A second down-B episode appeared after the neutral release")
            require(down_b and self.grounded_zelda_sequence is not None and
                    self.neutral_consume_sequence is not None and
                    self.neutral_consume_sequence < self.grounded_zelda_sequence < row["seq"],
                    "Down-B consume preceded ordered neutral readiness")
            if not self.down_b_held:
                require(not self.down_b_consumed, "Down-B source episode repeated after release")
                self.down_b_consumed = True
                self.down_b_consume_sequence = row["seq"]
            self.down_b_held = True
        else:
            require(not down_b, "Neutral witness disagrees with native PAD decoder")
            if self.down_b_held:
                self.down_b_released = True
                self.down_b_held = False
            elif self.neutral_consume_sequence is None:
                self.neutral_consume_sequence = row["seq"]
        self.consumed_samples.append({"sequence": row["seq"],
                                      "phase": "down-b-held" if down_b else "neutral",
                                      "ports": ports, "errors": errors})

    def _source_tick(self, row):
        from sheik_transform_prefix import (NEUTRAL_MOTION, SHEIK_KIND, ZELDA_DOWN_B_MOTION,
                                            ZELDA_KIND, _decode_owner_sample)
        payload = row["payload"]
        tick = payload.get("source_tick")
        require(type(tick) is int and self.active_source_ticks < 600 and
                (self.last_source_tick is None and tick == 0 or
                 self.last_source_tick is not None and tick == self.last_source_tick + 1),
                "Transform source-tick sequence or 600-observation cap differs")
        self.last_source_tick = tick
        self.active_source_ticks += 1
        sample = _decode_owner_sample(payload, setup=False, sequence=row["seq"], source_tick=tick)
        active = sample["portable"]
        stable_identity_fields = ("player_entities_hex_by_slot", "gobj_pointers",
                                  "fighter_pointers", "gobj_user_data_hex",
                                  "fighter_backlink_hex")
        require(self.setup_sample is not None and
                all(sample["local_pointer_checks"][field] ==
                    self.setup_sample["local_pointer_checks"][field]
                    for field in stable_identity_fields),
                "Transform source owner pointers changed after setup")
        if active["active_kind"] == ZELDA_KIND:
            require(active["active_entity_index"] == 0 and self.after_swap is None,
                    "Zelda regained active ownership after Sheik became active")
            if (not self.down_b_consumed and self.neutral_consume_sequence is not None and
                    self.neutral_consume_sequence < row["seq"] and
                    self.grounded_zelda_sequence is None and
                    active["active_motion"] == NEUTRAL_MOTION and active["active_ground_air"] == 0):
                self.grounded_zelda_sequence = row["seq"]
        elif active["active_kind"] == SHEIK_KIND:
            require(self.down_b_consumed and self.down_b_released and self.action_seen and
                    active["active_entity_index"] == 1,
                    "Active Sheik owner preceded consumed down-B or neutral release")
            if self.after_swap is None:
                self.after_swap = sample
            elif active["active_motion"] == NEUTRAL_MOTION and active["active_ground_air"] == 0:
                self.post_swap_neutral = sample
        else:
            raise SdDiagnosticError("Active transform owner left Zelda/Sheik source identities")
        if sample["source_fields"]["motion_hex"][0] == f"{ZELDA_DOWN_B_MOTION:08x}":
            require(self.down_b_consumed,
                    "Original Zelda action 355 preceded the consumed B/Y=-80 source sample")
            self.action_seen = True
        self.source_samples.append(sample)

    def accept(self, row):
        require(not self.ended and row["seq"] == self.seq,
                "Transform observer sequence is repeated, missing, or trailing")
        self.seq += 1
        event, payload = row["event"], row["payload"]
        if event in ("handshake", "start"):
            from sheik_transform_prefix import ITEMS_COMPLETION_BOUNDARY, COMPLETION_BOUNDARY
            require(payload.get("diagnostic") == "sheik_transform_prefix" and
                    payload.get("completion_boundary") ==
                    (ITEMS_COMPLETION_BOUNDARY if self.items_only else COMPLETION_BOUNDARY) and
                    not payload.get("whole_session"),
                    "Observer handshake/start escaped the declared transform-prefix profile")
            return
        if event == "error":
            raise SdDiagnosticError("Transform observer error: " + str(payload.get("error")))
        if event == "end" and self.items_only:
            from sheik_transform_prefix import ITEMS_COMPLETION_BOUNDARY
            proof = self.items_progress.finish()
            require(payload.get("status") == "interrupted" and payload.get("natural") is False and
                    payload.get("items_boundary_complete") is True and
                    payload.get("completion_boundary") == ITEMS_COMPLETION_BOUNDARY and
                    payload.get("transform_complete") is False and payload.get("match_complete") is False,
                    "Reduced Items stream lacks its explicit interrupted Items completion")
            self.ended = True
            return
        if event == "end":
            require(payload.get("status") == "completed" and payload.get("natural") is True and
                    payload.get("match_complete") is False and self.post_swap_neutral is not None,
                    "Transform observer ended before the completed grounded Sheik prefix")
            self.ended = True
            return
        require(event == "boundary" and payload.get("whole_session") is not True,
                "Unexpected event in passive transform-prefix observer stream")
        boundary = payload.get("boundary")
        require(not self.items_only or boundary in ("pad_poll", "pad_consume"),
                "Reduced Items stream continued into setup or transformation")
        if boundary == "pad_poll":
            self._pad_poll(row)
        elif boundary == "setup":
            from sheik_transform_prefix import _decode_owner_sample
            from retail_input_plan import verify_entry
            require(not self.setup_seen and self.css_live_owner_sequence is not None and
                    self.sss_live_owner_sequence is not None and
                    self.css_live_owner_sequence < self.sss_live_owner_sequence < row["seq"],
                    "VS setup preceded ordered live CSS and SSS source owner polls")
            _, metadata = _transform_slices(payload)
            setup_field = metadata.get((4, 0))
            require(setup_field is not None and setup_field["size"] == 0x138,
                    "Transform setup lacks its exact source-owned StartMeleeData")
            self.setup_hex = setup_field["hex"]
            verify_entry(self.plan, self.setup_hex)
            self.setup_sample = _decode_owner_sample(payload, setup=True,
                                                     sequence=row["seq"], source_tick=payload.get("source_tick"))
            require(self.items_unlock_sequence is not None and
                    self.items_row_up_consume_sequence is not None and
                    self.items_row_up_release_sequence is not None and
                    self.items_unlock_sequence < self.items_row_up_consume_sequence <
                    self.items_row_up_release_sequence <
                    self.css_live_owner_sequence < self.sss_live_owner_sequence < row["seq"],
                    "VS setup preceded the observed unlocked Items Up/neutral and ordered CSS/SSS owners")
            self.setup_seen = True
        elif boundary == "pad_consume":
            self._pad_consume(row)
        elif boundary == "source_tick":
            require(self.setup_seen, "Transform source tick preceded VS setup")
            self._source_tick(row)
        elif self.setup_seen:
            raise SdDiagnosticError("Unexpected boundary inside transform-prefix source interval")

    def finish(self):
        if self.items_only:
            require(self.ended and not self.setup_seen, "Reduced Items receiver did not end before setup")
            return {"scope": "sheik_transform_items_readiness",
                    "items": self.items_progress.finish(),
                    "consumed_source_pads": self.consumed_samples,
                    "pad_queue_records": self.pad_queue_records}
        self.items_progress.finish()
        require(self.ended and self.setup_seen and self.down_b_consumed and self.down_b_released and
                self.grounded_zelda_sequence is not None and self.after_swap is not None and
                self.post_swap_neutral is not None and self.active_source_ticks <= 600,
                "Transform receiver lacks its complete setup/input/owner prefix")
        return {
            "scope": "sheik_transform_prefix",
            "setup_sha256": self.setup_sample["setup_sha256"],
            "source_slot_map": {"pipe_lane_0": 0, "pipe_lane_1": 1,
                                "inactive_source_slots": [2, 3]},
            "menu_owner_sequences": {"css": self.css_live_owner_sequence,
                                     "sss": self.sss_live_owner_sequence,
                                     "setup": self.setup_sample["sequence"]},
            "items_lock_readiness": {
                "transitions": self.items_lock_transitions,
                "first_unlocked_pad_poll": self.items_unlock_sequence,
                "frequency_row_up_consume": self.items_row_up_consume_sequence,
                "frequency_row_neutral_release": self.items_row_up_release_sequence,
                "up_followed_unlock": (self.items_unlock_sequence is not None and
                                       self.items_row_up_consume_sequence is not None and
                                       self.items_unlock_sequence < self.items_row_up_consume_sequence),
            },
            "consumed_source_pads": self.consumed_samples,
            "pad_queue_records": self.pad_queue_records,
            "source_samples": self.source_samples,
            "active_source_tick_observations": self.active_source_ticks,
            "ordered_witness_sequences": {
                "neutral_pad_consume": self.neutral_consume_sequence,
                "grounded_neutral_zelda_source_tick": self.grounded_zelda_sequence,
                "down_b_consume": self.down_b_consume_sequence,
                "neutral_release": next((sample["sequence"] for sample in self.consumed_samples
                                          if sample["phase"] == "neutral" and
                                          self.down_b_consume_sequence is not None and
                                          sample["sequence"] > self.down_b_consume_sequence), None),
                "active_sheik": self.after_swap["sequence"],
                "grounded_neutral_sheik_source_tick": self.post_swap_neutral["sequence"],
            },
        }



class StadiumGoPrefixReceiver(SheikTransformPrefixReceiver):
    """Menu/input adapter; the strict v3 validator owns raw Stadium admission."""
    def __init__(self, menus, *, recorded_input=False):
        super().__init__(None, menus)
        self.recorded_input = recorded_input
        self.recorded_target_neutral_release_sequence = None
        self.recorded_confirm_neutral_polls = None
        self.setup_seen = False
        self.setup_sequence = None
        self.source_tick_rows = 0
        self.draw_return_rows = 0
        self.progress_rows = []
        from stadium_go_prefix import StadiumSssPositionObservations
        self.sss_positions = StadiumSssPositionObservations()
        self.sss_position_route_ready = False
        self.sss_feedback = menus["sss"].get("policy") == "same_call_feedback_v1"
        self.sss_geometry_token = None
        self.sss_geometry_pending = None
        self.sss_callback_count = 0
        self.sss_completed_input = None
        self.stadium_target_sequence = None
        self.target_neutral_release_sequence = None
        self.target_neutral_polls = 0
        self.confirm_sequence = None
        self.confirm_release_sequence = None
        self.consumed_samples = []

    def take_sss_geometry(self, after_consume):
        """Consume one completed current-call token, never carry a target across calls."""
        token = self.sss_geometry_token
        self.sss_geometry_token = None
        require(token is not None and token["consumed_sample"]["sequence"] >= after_consume and
                self.consumed_samples and token["consumed_sample"] == self.consumed_samples[-1] and
                token["call"] == self.sss_positions.last_call and self.sss_positions.active is None and
                self.sss_position_route_ready and not self.setup_seen and not self.ended and
                (self.latest_menu or {}).get("scene") == 9 and self.stage is not None,
                "SSS feedback lacks a fresh completed call after exact consumed input")
        return token

    def _feedback_position(self, row, observation):
        phase = row["payload"]["phase"]
        if phase == "sss_position_cursor" or (phase == "sss_position_end" and
                                              not row["payload"]["cursor_observed"]):
            self.sss_geometry_token = None
            require(self.consumed_samples and not self.setup_seen,
                    "SSS feedback callback lacks preceding consumed source PAD")
            sample = self.consumed_samples[-1]
            require(sample["sequence"] < row["seq"] and sample["phase"] == "menu",
                    "SSS feedback callback has a foreign consumed input")
            # Copy arrays: later input observations cannot mutate this witness.
            self.sss_geometry_pending = (dict(sample, ports=list(sample["ports"]),
                                              errors=list(sample["errors"])),
                                         row["source_tick"], row["draw_ordinal"])
        pending = self.sss_geometry_pending
        require(pending is not None and pending[0] == self.consumed_samples[-1] and
                pending[1:] == (row["source_tick"],row["draw_ordinal"]),
                "SSS feedback input/source cursor changed inside callback")
        if phase == "sss_position_end":
            self.sss_callback_count += 1
            self.sss_completed_input = {"call":row["payload"]["position_call"],
                                        "cursor_observed":row["payload"]["cursor_observed"],
                                        "consumed_sample":pending[0]}
            completed = observation["completed_tuple"]
            self.sss_geometry_token = (dict(completed, end_sequence=row["seq"],
                                           consumed_sample=pending[0]) if completed else None)
            self.sss_geometry_pending = None

    def _menu_owner_pad_poll(self, payload):
        scene_rows = [item for item in payload.get("slices", [])
                      if isinstance(item, dict) and item.get("tag") == 40]
        if (len(scene_rows) == 1 and scene_rows[0].get("flags") == 0 and
                scene_rows[0].get("size") == 1 and scene_rows[0].get("hex") == "09"):
            from stadium_go_prefix import _classify_stadium_sss_owner
            return _classify_stadium_sss_owner(payload, allow_missing=True)
        return super()._menu_owner_pad_poll(payload)

    def _pad_poll(self, row):
        super()._pad_poll(row)
        if self.latest_menu is not None:
            from sd_reference_diagnostic import items_lock_state
            data, _ = _transform_slices(row["payload"])
            self.latest_menu = items_lock_state(data, row["payload"], self.latest_menu)
        if (self.latest_menu or {}).get("scene") != 9 or self.stage is None:
            self.sss_geometry_token = None
            self.sss_geometry_pending = None
            self.sss_completed_input = None
            self.target_neutral_polls = 0
            return
        target = (self.stage.get("index") == self.menus["sss"]["target_index"] and
                  self.stage.get("kind") == self.menus["sss"]["target_kind"])
        if self.recorded_input and not target:
            self.target_neutral_release_sequence = None
            self.target_neutral_polls = 0
        if target and self.stage.get("stable_polls", 0) >= 2:
            self.stadium_target_sequence = row["seq"]
        if self.target_neutral_release_sequence is not None and row["seq"] > self.target_neutral_release_sequence:
            self.target_neutral_polls = (self.target_neutral_polls + 1
                if target and self.last_pad is not None and self.last_pad[:2] == [NEUTRAL_PAD]*2 else 0)

    def _pad_consume_stadium(self, row):
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD
        ports, errors, queue_record = _transform_consumed_ports(row["payload"], row["seq"])
        require(ports[2:] == [DISCONNECTED_PAD, DISCONNECTED_PAD] and errors == [0, 0, -1, -1],
                "Original Stadium route changed active or disconnected source ports")
        require((ports[0], ports[1]) in self.allowed_menu_pads,
                "Original Stadium consumed PAD escaped the predeclared route alphabet")
        pair = ports[:2]
        if self.recorded_input and not self.setup_seen:
            target_context = (self.sss_position_route_ready and
                self.sss_live_owner_sequence is not None and
                (self.latest_menu or {}).get("scene") == 9 and self.stage is not None and
                self.stage.get("index") == self.menus["sss"]["target_index"] and
                self.stage.get("kind") == self.menus["sss"]["target_kind"] and
                self.stage.get("stable_polls", 0) >= 2)
            # The release witness is the original's consumed neutral sample,
            # never an intention or an inferred controller write. Subsequent
            # owner polls count only while this exact target remains observed.
            if pair == [NEUTRAL_PAD]*2 and target_context and self.confirm_sequence is None:
                if self.target_neutral_release_sequence is None:
                    self.target_neutral_release_sequence = row["seq"]
                    self.recorded_target_neutral_release_sequence = row["seq"]
                    self.target_neutral_polls = 0
            if self.confirm_release_sequence is not None:
                require(pair != [raw_pad(buttons=["A"]), NEUTRAL_PAD],
                        "Recorded SSS confirm repeated after its consumed neutral release")
        if self.setup_seen:
            require(pair == [NEUTRAL_PAD, NEUTRAL_PAD],
                    "Non-neutral source PAD was consumed after Stadium VS setup")
        self.menu_consumed += 1
        self.last_pad = ports
        sample = {"sequence": row["seq"], "phase": "post-setup-neutral" if self.setup_seen else "menu",
                  "ports": ports, "errors": errors, **queue_record}
        self.consumed_samples.append(sample)
        if self.sss_feedback:
            self.sss_geometry_token = None
        if (not self.setup_seen and (self.latest_menu or {}).get("scene") == 9 and
                self.stage is not None and
                self.stage.get("index") == self.menus["sss"]["target_index"] and
                self.stage.get("kind") == self.menus["sss"]["target_kind"] and
                self.target_neutral_polls >= self.menus["sss"]["target_stable_neutral_polls"] and
                pair == [raw_pad(buttons=["A"]), NEUTRAL_PAD]):
            require(self.confirm_sequence is None,
                    "Original SSS confirm was consumed more than once")
            self.confirm_sequence = row["seq"]
            if self.recorded_input:
                self.recorded_confirm_neutral_polls = self.target_neutral_polls
        elif self.confirm_sequence is not None and self.confirm_release_sequence is None and pair == [NEUTRAL_PAD]*2:
            self.confirm_release_sequence = row["seq"]
        if pair != [NEUTRAL_PAD]*2:
            self.target_neutral_polls = 0
            if self.recorded_input:
                self.target_neutral_release_sequence = None
        return sample

    def accept(self, row):
        require(not self.ended and row["seq"] == self.seq,
                "Stadium observer sequence is repeated, missing, or trailing")
        self.seq += 1
        event, payload = row["event"], row["payload"]
        from stadium_go_prefix import DIAGNOSTIC
        if event in ("handshake", "start"):
            require(payload.get("diagnostic") == DIAGNOSTIC and
                    payload.get("setup_receipt_sha256") == self.menus["setup_receipt_sha256"] and
                    payload.get("setup_profile_verified_by_observer") is False and
                    not payload.get("whole_session"),
                    "Observer handshake/start escaped the declared Stadium-prefix profile")
            return
        if event == "error":
            self.sss_geometry_token = None
            self.sss_geometry_pending = None
            self.sss_completed_input = None
            raise SdDiagnosticError("Stadium observer error: " + str(payload.get("error")))
        if event == "progress":
            from stadium_go_prefix import SSS_POSITION_PHASES
            phase = payload.get("phase")
            if phase in SSS_POSITION_PHASES:
                if self.sss_feedback:
                    self.sss_geometry_token = None  # Any new call/operand precedes reuse.
                try:
                    observation = self.sss_positions.accept(row, route_ready=self.sss_position_route_ready)
                    if self.sss_feedback:
                        self._feedback_position(row, observation)
                except ValueError:
                    self.sss_geometry_token = None
                    self.sss_geometry_pending = None
                    self.sss_completed_input = None
                    raise
            elif phase == "sss_return":
                self.sss_position_route_ready = True
            elif phase == "sss_exit":
                self.sss_positions.require_closed()
                self.sss_position_route_ready = False
                self.sss_geometry_token = None
                self.sss_geometry_pending = None
                self.sss_completed_input = None
            elif self.sss_feedback:
                self.sss_geometry_token = None
                self.sss_geometry_pending = None
                self.sss_completed_input = None
            self.progress_rows.append({"sequence": row["seq"], "phase": payload.get("phase"),
                                       "source_tick": payload.get("source_tick")})
            return
        if event == "end":
            self.sss_geometry_token = None
            self.sss_geometry_pending = None
            self.sss_completed_input = None
            require(payload.get("status") == "completed" and payload.get("natural") is True and
                    payload.get("setup_receipt_sha256") == self.menus["setup_receipt_sha256"] and
                    payload.get("setup_profile_verified_by_observer") is False,
                    "Stadium observer did not end naturally under its declared receipt")
            self.ended = True
            return
        require(event == "boundary" and payload.get("whole_session") is not True,
                "Unexpected event in passive Stadium observer stream")
        boundary = payload.get("boundary")
        if boundary == "pad_poll":
            self._pad_poll(row)
        elif boundary == "pad_consume":
            self._pad_consume_stadium(row)
        elif boundary == "setup":
            require(not self.setup_seen and self.css_live_owner_sequence is not None and
                    self.sss_live_owner_sequence is not None and
                    self.css_live_owner_sequence < self.sss_live_owner_sequence < row["seq"] and
                    self.stadium_target_sequence is not None and
                    self.confirm_sequence is not None and self.confirm_sequence < row["seq"],
                    "Original Stadium setup preceded CSS/SSS, selected Stadium, or consumed confirm")
            self.setup_seen = True
            self.setup_sequence = row["seq"]
        elif boundary == "source_tick":
            self.source_tick_rows += 1
        elif boundary == "draw_return":
            self.draw_return_rows += 1

    def finish(self):
        require(self.ended and self.setup_seen and self.stadium_target_sequence is not None and
                self.confirm_sequence is not None and self.confirm_release_sequence is not None and
                self.draw_return_rows >= 1,
                "Stadium driver lacks consumed setup/selection/confirm or natural DrawReturn")
        return {
            "scope": "stadium_go_prefix",
            "recorded_input_observation": ({
                "target_neutral_release": self.recorded_target_neutral_release_sequence,
                "target_neutral_polls_before_confirm": self.recorded_confirm_neutral_polls,
                "driver_intentions_used": False} if self.recorded_input else None),
            "menu_owner_sequences": {"css": self.css_live_owner_sequence,
                                     "sss": self.sss_live_owner_sequence,
                                     "stadium_target": self.stadium_target_sequence,
                                     "confirm": self.confirm_sequence,
                                     "confirm_neutral_release": self.confirm_release_sequence,
                                     "setup": self.setup_sequence},
            "consumed_source_pads": self.consumed_samples,
            "source_tick_rows": self.source_tick_rows,
            "draw_return_rows": self.draw_return_rows,
            "progress_rows": self.progress_rows,
        }

def menu_actions(path):
    raw = Path(path).read_bytes()
    require(len(raw) <= 1024 * 1024, "SD menu recipe exceeds its bound")
    value = json.loads(raw)
    if isinstance(value, dict) and value.get("version") in (2, 3, 4, 5, 6, 7, 8, 9, 10, 11):
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


def prepare_rules_profile(profile, user, *, source_slots=(0, 1)):
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
    from original_source_ports import declared_source_slots, inactive_source_slots
    source_slots = declared_source_slots(source_slots)
    p1, p2 = prepare_dual_pipe(user, source_slots=source_slots)
    # Inactive source PAD ports must remain disconnected, not extra Pipe devices.
    import configparser
    ini = configparser.ConfigParser(interpolation=None)
    ini.optionxform = str
    ini.read(config)
    require(not ini.get("General", "GDBSocket", fallback="").strip(),
            "SD cold boot must not attach a debugger")
    for port in inactive_source_slots(source_slots):
        ini.set("Core", "SIDevice" + str(port), "0")
    _atomic_ini(config, ini)
    return p1, p2, source_inventory


def run(*, dolphin, disc, profile, input_plan=None, menu_recipe, output, build_manifest, timeout=180, gci=None,
        ordinary_policy=None, input_replay=None, common_wire_comparator=None, stop_after_items=False, stadium_constructor_owner=False, stadium_idle_3500=False, stadium_go_ordinal2=False, stadium_whole_target=None, stadium_whole_setup=None, stadium_whole_gc=None, stadium_whole_dol=None, stadium_whole_provenance=None, stadium_whole_b_state=False, stadium_whole_long_idle=False, stadium_whole_paired_idle=False, stadium_whole_stockout=False, stadium_whole_rng_window=None, stadium_whole_replay_binding=None, stadium_whole_rng_coverage="rng_returns"):
    """Own fresh output before preparation so failures cannot vanish before launch."""
    require(type(stadium_whole_b_state) is bool, "Whole Stadium B observation opt-in must be boolean")
    require(type(stadium_whole_long_idle) is bool and
            (not stadium_whole_long_idle or stadium_whole_b_state),
            'Whole Stadium long-idle profile requires the B-state option')
    require(type(stadium_whole_paired_idle) is bool,
            'Whole Stadium paired-idle profile option must be boolean')
    require(type(stadium_whole_stockout) is bool and
            (not stadium_whole_stockout or stadium_whole_b_state and
             not stadium_whole_long_idle and not stadium_whole_paired_idle and
             stadium_whole_rng_window is None and stadium_whole_replay_binding is None),
            'Whole Stadium natural stockout requires B-state and excludes other adjunct profiles')
    whole_args = (stadium_whole_target, stadium_whole_setup, stadium_whole_gc,
                  stadium_whole_dol, stadium_whole_provenance)
    if any(x is not None for x in whole_args):
        require(all(x is not None for x in whole_args) and input_plan is None and
                input_replay is None and ordinary_policy is None and not stop_after_items and
                not stadium_constructor_owner and not stadium_idle_3500 and
                not stadium_go_ordinal2,
                'Whole Stadium route requires its complete exclusive input tuple')
        require(not stadium_whole_paired_idle or
                stadium_whole_rng_window is None and stadium_whole_replay_binding is None,
                'Whole Stadium paired-idle route excludes RNG sidecar and replay modes')
        return run_stadium_whole_three(dolphin=dolphin, disc=disc, profile=profile,
            menu_recipe=menu_recipe, output=output, build_manifest=build_manifest,
            target_path=stadium_whole_target, setup_path=stadium_whole_setup,
            checkpoint_gc=stadium_whole_gc, dol=stadium_whole_dol,
            provenance=stadium_whole_provenance, gci=gci, timeout=timeout,
            stadium_whole_b_state=stadium_whole_b_state,
            stadium_whole_long_idle=stadium_whole_long_idle,
            stadium_whole_paired_idle=stadium_whole_paired_idle,
            stadium_whole_stockout=stadium_whole_stockout,
            stadium_whole_rng_window=stadium_whole_rng_window,
            stadium_whole_replay_binding=stadium_whole_replay_binding,
            stadium_whole_rng_coverage=stadium_whole_rng_coverage)
    require(stadium_whole_rng_coverage == "rng_returns", "Whole Stadium RNG coverage requires the whole3 window")
    require(stadium_whole_replay_binding is None, "Whole Stadium replay requires the whole3 input tuple")
    require(stadium_whole_rng_window is None, "Whole Stadium RNG window requires the whole3 input tuple")
    require(not stadium_whole_b_state, "Whole Stadium B observation requires the whole3 input tuple")
    require(not stadium_whole_long_idle,
            "Whole Stadium long-idle profile requires the whole3 input tuple")
    require(not stadium_whole_stockout,
            "Whole Stadium natural stockout requires the whole-session input tuple")
    require(not stadium_whole_paired_idle,
            "Whole Stadium paired-idle profile requires the whole-session input tuple")
    require(type(stadium_go_ordinal2) is bool and
            (not stadium_go_ordinal2 or input_replay is None),
            'Stadium GO+2 requires a fresh recording, never replay or hybrid input')
    output = Path(output)
    replay_receipt = (stadium_replay_preflight(input_replay, output, build_manifest)
                      if input_replay is not None else None)
    output.mkdir()  # A collision never overwrites another run or its evidence.
    scope = "rules_ready"
    try:
        scope = menu_actions(menu_recipe)[0].get("scope", scope)
        if ordinary_policy is not None:
            scope="ordinary_timeout_gci"
        return _run(dolphin=dolphin, disc=disc, profile=profile, input_plan=input_plan,
                    menu_recipe=menu_recipe, output=output, build_manifest=build_manifest, timeout=timeout, gci=gci,
                    ordinary_policy=ordinary_policy, replay_receipt=replay_receipt,
                    common_wire_comparator=common_wire_comparator,
                    stop_after_items=stop_after_items,
                    stadium_constructor_owner=stadium_constructor_owner,
                    stadium_idle_3500=stadium_idle_3500,
                    stadium_go_ordinal2=stadium_go_ordinal2)
    except Exception as error:
        failure = output / "failure.json"
        if not failure.exists():
            failure.write_bytes(canonical({"scope": scope, "stage": "prelaunch",
                                           "native_launched": False, "error": str(error),
                                           "comparison_receipt": getattr(error, "receipt", None)}))
        raise


def _run(*, dolphin, disc, profile, input_plan=None, menu_recipe, output, build_manifest, timeout, gci=None,
         ordinary_policy=None, replay_receipt=None, common_wire_comparator=None, stop_after_items=False, stadium_constructor_owner=False, stadium_idle_3500=False, stadium_go_ordinal2=False):
    build = validate_reference_build_manifest(Path(build_manifest), Path(dolphin))
    menus, menu_hash = menu_actions(menu_recipe)
    stadium_prefix = menus["scope"] == "stadium_go_prefix"
    require(type(stadium_idle_3500) is bool and
            (not stadium_idle_3500 or (stadium_prefix and replay_receipt is None)),
            "Stadium idle3500 requires fresh recording, never replay or hybrid input")
    if stadium_idle_3500:
        stadium_constructor_owner = True
    require(type(stadium_go_ordinal2) is bool and
            (not stadium_go_ordinal2 or
             (stadium_prefix and replay_receipt is None and not stadium_idle_3500)),
            "Stadium GO+2 requires its fresh-record Stadium route, exclusive of replay and idle3500")
    if stadium_go_ordinal2:
        stadium_constructor_owner = True
    require(type(stadium_constructor_owner) is bool and
            (not stadium_constructor_owner or
             (stadium_prefix and (replay_receipt is not None or stadium_idle_3500 or stadium_go_ordinal2))),
            "Stadium constructor adjunct requires an explicit checked capture mode")
    require(replay_receipt is None or stadium_prefix,
            "Recorded MWRI replay is exclusive to Stadium prefix")
    if replay_receipt is not None:
        require(callable(common_wire_comparator),
                "Stadium replay common-wire comparator is not integrated; capture refused")
    if stadium_prefix:
        require(input_plan is None and ordinary_policy is None,
                "Stadium GO prefix uses its declared menu recipe, supplied profile and optional exact retained GCI")
        plan, plan_hash = None, None
    else:
        require(input_plan is not None, "This menu route requires its declared input plan")
        plan, plan_hash = load_plan(input_plan, allow_authored=True)
    items_probe = menus["scope"] == "items_row_gci"
    competitive_entry = menus["scope"] == "competitive_entry_gci"
    sparse_pair = menus["scope"] == "sparse_pair_gci"
    transform_prefix = menus["scope"] == "sheik_transform_prefix"
    full_route = menus["scope"] == "sd_prefix_gci" or items_probe or competitive_entry or sparse_pair
    guarded_items = (menus["scope"] == "sd_prefix_gci" and menus["version"] == 7) or competitive_entry
    campaign = menus["scope"] in ("rules_ready_gci", "sd_prefix_gci", "items_row_gci",
                                    "competitive_entry_gci", "sparse_pair_gci")
    scope = menus["scope"]
    require(type(stop_after_items) is bool and (not stop_after_items or transform_prefix),
            "Items early stop requires the exact original transform route")
    if stop_after_items:
        scope = "sheik_transform_items_readiness"
    ordinary = ordinary_policy is not None
    if ordinary:
        from original_competitive_timeout import load_policy, CAPS
        policy, policy_hash = load_policy(Path(ordinary_policy))
        require(competitive_entry and menus["version"]==8 and
                policy["entry_recipe_sha256"]==plan["authored_recipe_sha256"] and
                hashlib.sha256(canonical(menus)).hexdigest()==policy["menu_sha256"],
                "Ordinary policy must retain exact competitive entry/menu provenance")
        scope="ordinary_timeout_gci"
    bounded_log_cap = (CAPS["log_bytes"] if ordinary else
                       8 * 1024 * 1024 if transform_prefix or stadium_prefix else None)
    expected_recipe_version = (8 if transform_prefix else 7 if sparse_pair else 6 if competitive_entry else
                               5 if full_route else 4 if campaign else 3)
    expected_menu_version = (11 if stadium_prefix else 10 if transform_prefix else 9 if sparse_pair else
                             8 if competitive_entry else 6 if items_probe else 7 if guarded_items else
                             5 if full_route else 4 if campaign else 2)
    if stadium_prefix:
        from stadium_go_prefix import EXPECTED_SETUP_RECEIPT_SHA256
        require(menus["version"] == expected_menu_version and
                menus["setup_receipt_sha256"] == EXPECTED_SETUP_RECEIPT_SHA256 and
                "authored_recipe_sha256" not in menus,
                "Stadium menu packet is not bound to the reviewed observer setup receipt")
    else:
        require(plan["authored_recipe"]["version"] == expected_recipe_version and
                menus["version"] == expected_menu_version and (gci is not None) == campaign and
                menus["authored_recipe_sha256"] == plan["authored_recipe_sha256"],
                "Runnable original diagnostic requires the exact current scoped recipe/menu versions")
    loaded_profile = None
    if campaign or transform_prefix or stadium_prefix:
        manifest_raw = Path(build_manifest).read_bytes()
        overlay = ROOT / "reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp"
        require(hashlib.sha256(manifest_raw).hexdigest() == build["sha256"] and
                json.loads(manifest_raw).get("observer_source_overlay_sha256", {}).get(
                    "Core/PowerPC/ReferenceCaptureObserver.cpp") == hashlib.sha256(overlay.read_bytes()).hexdigest(),
                "Loaded-profile observer producer is stale or unbound")
        if campaign or (stadium_prefix and gci is not None):
            from sd_gci_profile import prepare_gci_folder
            loaded_profile, owned_gci = prepare_gci_folder(gci, output / "gci-folder")
    if transform_prefix:
        require(gci is None and ordinary_policy is None,
                "Sheik transform prefix must use the supplied original profile without GCI injection")
        receiver = SheikTransformPrefixReceiver(plan, menus, items_only=stop_after_items)
    elif stadium_prefix:
        receiver = StadiumGoPrefixReceiver(menus, recorded_input=replay_receipt is not None)
    else:
        receiver = GciRulesMenuReceiver(plan, loaded_profile, full_route=full_route, items_probe=items_probe,
                                       guarded_items=guarded_items, competitive_entry=competitive_entry,
                                       sparse_pair=sparse_pair) if campaign else RulesMenuReceiver(plan)
    if ordinary:
        from ordinary_timeout_receiver import OrdinaryTimeoutReceiver
        receiver=OrdinaryTimeoutReceiver(plan,loaded_profile)
    require(type(timeout) in (int, float) and 0 < timeout <= (600 if ordinary else 180 if full_route or stadium_prefix else 600),
            "Original diagnostic deadline is unbounded")
    user = output / "user"
    if sparse_pair:
        p1, p2, source_inventory = prepare_rules_profile(profile, user, source_slots=(0, 2))
    else:
        p1, p2, source_inventory = prepare_rules_profile(profile, user)
    raw, status = output / "observer.bin", output / "observer-status.json"
    native, native_status = output / "inputs.mwri", output / "input-status.json"
    environment = {k: v for k, v in os.environ.items()
                   if not k.startswith(("MWRC_", "DOLPHIN_", "SDL_"))}
    environment.update(MWRC_ENABLE="1", MWRC_CPU="JITARM64", MWRC_SOURCE_REV="GALE01r2",
                       MWRC_DOL_SHA256="dc21504513424350bda17a7c65e82371b45112a5dfc1e9f2749a8b7ab0eff646",
                       MWRC_OUTPUT=str(raw), MWRC_STATUS=str(status),
                       MWRC_INPUT_STATUS=str(native_status))
    if replay_receipt is None:
        environment["MWRC_INPUT_RECORD"] = str(native)
    else:
        environment["MWRC_INPUT_REPLAY"] = replay_receipt["source"]
        native = Path(replay_receipt["source"])
    if transform_prefix:
        environment["MWRC_TRANSFORM_PREFIX"] = "1"
        if stop_after_items:
            environment["MWRC_TRANSFORM_STOP_AT_ITEMS"] = "1"
    elif stadium_prefix:
        environment.update(MWRC_STADIUM_GO_PREFIX="1",
                           MWRC_STADIUM_SETUP_RECEIPT_SHA256=menus["setup_receipt_sha256"])
        if stadium_constructor_owner:
            environment["MWRC_STADIUM_CONSTRUCTOR_OWNER"] = "1"
        if stadium_idle_3500:
            environment["MWRC_STADIUM_IDLE_3500"] = "1"
        if stadium_go_ordinal2:
            environment["MWRC_STADIUM_GO_ORDINAL2"] = "1"
    else:
        environment.update(MWRC_SD_INIT="1",
                           MWRC_SD_RECIPE_SHA256=plan["authored_recipe_sha256"],
                           MWRC_SD_MENU_PROBE="sparse_pair" if sparse_pair else "competitive_entry" if competitive_entry else "items_row" if items_probe else "sd_prefix" if full_route else "rules_ready")
    if ordinary:
        environment.update(MWRC_SD_MENU_PROBE="ordinary_timeout",MWRC_ORDINARY_POLICY_SHA256=policy_hash)
        (output/"ordinary-policy.json").write_bytes(canonical(policy))
    command = rules_dolphin_command(dolphin, user, disc)
    if campaign:
        environment["MWRC_SD_PROFILE_GCI_SHA256"] = loaded_profile["sha256"]
    if loaded_profile is not None:
        command += ["-C", "Dolphin.Core.SlotA=8", "-C",
                    "Dolphin.Core.GCIFolderAPath=" + str(output / "gci-folder")]
    if plan is not None:
        (output / "input-plan.json").write_bytes(canonical(plan))
    (output / "menu-recipe.json").write_bytes(canonical(menus))
    launch = {"scope": scope,
        "input_plan_sha256": plan_hash, "menu_recipe_sha256": menu_hash,
        "profile_sha256": source_inventory, "build": build, "command": command}
    if loaded_profile is not None:
        launch.update(profile_gci_sha256=loaded_profile["sha256"], owned_gci=str(owned_gci),
                      observed_prelaunch_config_modes={name: oct((user / "Config" / name).stat().st_mode & 0o777)
                         for name in ("Dolphin.ini", "GCPadNew.ini")})
    if transform_prefix:
        launch["observer_profile"] = "MWRC_TRANSFORM_PREFIX=1; no SD/GCI probe injection"
        if stop_after_items:
            launch["observer_profile"] += "; MWRC_TRANSFORM_STOP_AT_ITEMS=1"
        launch["stop_after_items"] = stop_after_items
    if stadium_prefix:
        launch["observer_profile"] = ("MWRC_STADIUM_GO_PREFIX=1; exact setup-receipt binding; "
                                       "supplied profile; optional exact retained read-only GCI copy")
        if stadium_go_ordinal2:
            launch.update(stadium_go_ordinal2=True, original_post_go_target_ordinal=2,
                          input_mode="record")
            launch["observer_profile"] += "; fresh MWRI recording; first DrawReturn after GO+2"
    if ordinary:
        launch.update(ordinary_policy_sha256=policy_hash,caps=CAPS)
    elif stadium_prefix:
        launch.update(caps={**STADIUM_CAPTURE_CAPS,
                            **({"observer_records":77602,"pre_C_records":16384}
                               if stadium_idle_3500 else {})}, wall_seconds=timeout)
        if stadium_idle_3500:
            launch['original_idle_post_go_ticks'] = 3500
        if stadium_go_ordinal2:
            launch['original_post_go_target_ordinal'] = 2
    if replay_receipt is not None:
        launch.update(input_mode="replay", input_replay=replay_receipt,
                      adaptive_controller_enabled=False)
    if stadium_constructor_owner:
        launch["stadium_constructor_owner"] = True
    (output / "launch.json").write_bytes(canonical(launch))
    controller = (DualPipeController(p1, p2, output / "input-intentions.jsonl")
                  if replay_receipt is None else None)
    if ordinary:
        controller=BoundedIntentController(p1,p2,output/"input-intentions.jsonl")
    elif stadium_prefix and replay_receipt is None:
        controller=StadiumIntentController(p1,p2,output/"input-intentions.jsonl")
    deadline = time.monotonic() + timeout
    with (output / "dolphin.log").open("xb") as log:
        process = subprocess.Popen(command, env=environment,
                                   stdout=subprocess.PIPE if bounded_log_cap is not None else log,
                                   stderr=subprocess.STDOUT,
                                   start_new_session=True)
        bounded_log=None
        try:
            if bounded_log_cap is not None:
                bounded_log=BoundedLog(process.stdout,log,bounded_log_cap)
            # The dedicated receiver expects an interrupted primary ending, so
            # do not use the whole-session Tail's completion-status policy.
            def stadium_wait_check():
                check_stadium_capture_bounds(raw, native, deadline, receiver.seq, original_idle_3500=stadium_idle_3500)
                check_owned_native_wait(process, bounded_log)
            idle_observations = None
            if stadium_idle_3500:
                from stadium_go_prefix import StadiumIdle3500Observations
                idle_observations = StadiumIdle3500Observations()
            with ObserverTail(raw, None, wait_check=stadium_wait_check if stadium_prefix else
                              (lambda: check_owned_native_wait(process, bounded_log))
                              if ordinary or transform_prefix else None) as tail:
                def next_row():
                    if stadium_prefix:
                        check_stadium_capture_bounds(raw, native, deadline, receiver.seq + 1, original_idle_3500=stadium_idle_3500)
                    if bounded_log is not None:
                        bounded_log.check()
                    if ordinary:
                        require(raw.stat().st_size<=CAPS["observer_bytes"] if raw.exists() else True,
                                "Ordinary observer byte cap")
                        require(native.stat().st_size<=CAPS["input_bytes"] if native.exists() else True,
                                "Ordinary native input byte cap")
                    row = tail.next(deadline)
                    if ordinary:
                        require(tail.offset<=CAPS["observer_bytes"], "Ordinary consumed observer byte cap")
                    if stadium_prefix:
                        check_stadium_capture_bounds(raw, native, deadline, receiver.seq + 1, original_idle_3500=stadium_idle_3500)
                    receiver.accept(row)
                    if idle_observations is not None:
                        idle_observations.accept(row)
                        if idle_observations.last_ordinal < 0:
                            require(receiver.seq <= 16384, "Stadium idle3500 pre-C record cap")
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
                if replay_receipt is None:
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
                    for action in menu_actions_for_capture(menus, stop_after_items):
                        if transform_prefix and action["label"] == "items-frequency-row":
                            drive_transform_items_row(receiver, action, wait_source, tap)
                            continue
                        wait_source(lambda: matches(receiver.latest_menu, action["before"]),
                                    action["label"] + ":ready", action["max_polls"])
                        if "when_value" in action:
                            require(receiver.latest_menu["value"] in (0,1), "Competitive item is not a observed switch")
                        if "when_value" not in action or receiver.latest_menu["value"] == action["when_value"]:
                            tap(action, action["label"], action["max_polls"])
                        wait_source(lambda: matches(receiver.latest_menu, action["after"]),
                                    action["label"] + ":observed", action["max_polls"])
                    if ((full_route and not items_probe) or transform_prefix or stadium_prefix) and not (
                            transform_prefix and stop_after_items):
                        drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap)
                    if stadium_prefix and not receiver.setup_seen:
                        wait_source(lambda: receiver.setup_seen, "original Stadium VS setup transition",
                                    menus["sss"]["max_vs_transition_polls"])
                if transform_prefix and not stop_after_items:
                    drive_sheik_transform_input(receiver, plan, controller, next_row)
                if sparse_pair:
                    while not receiver.setup_seen:
                        next_row()
                    press, release = plan["frames"]
                    controller.set_both(press[0], press[2], action="sparse-source-0-2-press")
                    samples_left = plan["authored_recipe"]["input_witness"]["max_source_samples"]
                    while receiver.witness_phase == 0:
                        require(receiver.sparse_source_samples < samples_left and not receiver.ended,
                                "Sparse source input press witness cap exhausted")
                        next_row()
                    require(receiver.witness_phase == 1,
                            "Sparse source input press witness was not observed")
                    controller.set_both(release[0], release[2], action="sparse-source-0-2-release")
                    while not receiver.ended:
                        require(receiver.sparse_source_samples < samples_left,
                                "Sparse source input release witness cap exhausted")
                        next_row()
                while not receiver.ended:
                    next_row()
                    if ordinary and receiver.order==2:
                        state=receiver.ordinary
                        if state.phase=="first-loss" and not getattr(receiver,"direction_sent",False):
                            from reference_versus_sequence_capture import raw_pad
                            controller.set_both(raw_pad(x=-80),NEUTRAL_PAD,action="ordinary-first-loss")
                            receiver.direction_sent=True
                        elif state.phase=="held-bank-drain" and not getattr(receiver,"release_sent",False):
                            controller.set_both(NEUTRAL_PAD,NEUTRAL_PAD,action="ordinary-first-loss:release")
                            receiver.release_sent=True
                # MWRO End is flushed before the writer publishes final status.
                # Do not treat that publication race as native completion.
                if stadium_prefix:
                    primary_status, _ = wait_transform_terminal_statuses(status, native_status, deadline,
                        wait_check=lambda: check_stadium_capture_bounds(raw, native, deadline, receiver.seq, original_idle_3500=stadium_idle_3500),
                        input_mode="replay" if replay_receipt is not None else "record")
                    from reference_input_stream import validate_stream as validate_input_stream
                    from stadium_go_prefix import validate_stadium_go_prefix
                    constructor_scope = {"constructor_observations": True} if stadium_constructor_owner else {}
                    go_ordinal2_reference = None
                    if stadium_go_ordinal2:
                        from stadium_go_prefix import validate_stadium_go_ordinal2_reference
                        go_ordinal2_reference = validate_stadium_go_ordinal2_reference(
                            raw, input_path=native, input_status_path=native_status,
                            expected_setup_receipt_sha256=menus["setup_receipt_sha256"],
                            status_path=status, include_go2_state=True,
                            producer_identity={
                                "menu_recipe_sha256": menu_hash,
                                "input_plan_sha256": plan_hash,
                                "input_intentions_sha256": hashlib.sha256(
                                    (output / "input-intentions.jsonl").read_bytes()).hexdigest(),
                            })
                        input_summary = go_ordinal2_reference["input_stream"]
                        input_status = go_ordinal2_reference["input_status"]
                        prefix_report = go_ordinal2_reference["route"]
                    else:
                        input_summary = validate_input_stream(native)
                        input_status = (stadium_replay_complete(replay_receipt, native_status)
                                        if replay_receipt is not None else
                                        validate_status(native_status, mode="record",
                                            events=input_summary["events"], require_complete=True))
                        prefix_report = validate_stadium_go_prefix(
                            raw, menus["setup_receipt_sha256"], status_path=status,
                            **constructor_scope, **({"original_idle_3500":True} if stadium_idle_3500 else {}))
                    check_stadium_capture_bounds(raw, native, deadline, receiver.seq, original_idle_3500=stadium_idle_3500)
                    idle_reference = None
                    if stadium_idle_3500:
                        from stadium_go_prefix import validate_stadium_idle_3500_reference
                        idle_reference = validate_stadium_idle_3500_reference(raw, status_path=status,
                                                                            extract_inputs=True)
                        (output / 'idle3500-reference.json').write_bytes(canonical(idle_reference))
                    state_report = common_report = None
                    if replay_receipt is not None:
                        from stadium_go_prefix import validate_stadium_idle_state_snapshot
                        state_report = validate_stadium_idle_state_snapshot(
                            raw, menus["setup_receipt_sha256"], status_path=status, **constructor_scope)
                        common_report = common_wire_comparator(
                            replay_receipt["original_observer"], raw,
                            original_status_path=replay_receipt["original_status"],
                            candidate_status_path=status, **constructor_scope)
                        require(isinstance(common_report, dict) and
                                common_report.get("decision") == ("PASS_SEQUENCE_MAPPED_STADIUM_COMMON_WIRE_WITH_CONSTRUCTOR_OBSERVATIONS"
                                    if stadium_constructor_owner else "PASS_EXACT_STADIUM_COMMON_WIRE"),
                                "Stadium replay common-wire comparison did not pass")
                    bounded_log.check()
                    report = {
                        "schema": ("melee-web-original-stadium-go-ordinal2-capture" if stadium_go_ordinal2 else
                                   "melee-web-original-stadium-go-prefix-capture"),
                        "version": 1,
                        "scope": scope,
                        "status": "pass",
                        "input_plan_sha256": None,
                        "profile_gci_sha256": loaded_profile["sha256"] if loaded_profile else None,
                        "menu_recipe_sha256": menu_hash,
                        "input_intentions_sha256": (hashlib.sha256(
                            (output / "input-intentions.jsonl").read_bytes()).hexdigest()
                            if replay_receipt is None else None),
                        "input_mode": "replay" if replay_receipt is not None else "record",
                        "input_replay": replay_receipt,
                        "stadium_idle_state": state_report,
                        **({"original_idle_3500":{k:v for k,v in idle_reference.items()
                             if k not in ('literal_input_bundles','source_action_schedule')}}
                           if stadium_idle_3500 else {}),
                        "common_wire_comparison": common_report,
                        "profile_sha256": source_inventory,
                        "build": build,
                        "observer_status": primary_status,
                        "input_status": input_status,
                        "input_stream": input_summary,
                        "driver": receiver.finish(),
                        "stadium_go_prefix": prefix_report,
                        **({"stadium_go_ordinal2_reference": go_ordinal2_reference}
                           if stadium_go_ordinal2 else {}),
                        "claims": {"original_css_sss_stadium_setup": True,
                                   "source_consumed_stadium_confirm_and_release": True,
                                   **({"natural_go_ordinal2_draw_return": True}
                                      if stadium_go_ordinal2 else {}),
                                   "natural_go_draw_return_prefix": not stadium_go_ordinal2,
                                   "profile_setup_equivalence": False,
                                   "port_rng_equivalence": False,
                                   "whole_match_or_results": False,
                                   "pixels_or_pcm_equivalence": False},
                    }
                    (output / "report.json").write_bytes(canonical(report))
                    return report
                if transform_prefix:
                    primary_status, _ = wait_transform_terminal_statuses(status, native_status, deadline, items_only=stop_after_items)
                    from reference_input_stream import validate_stream as validate_input_stream
                    from sheik_transform_prefix import validate_transform_prefix
                    input_summary = validate_input_stream(native)
                    input_status = validate_status(native_status, mode="record",
                                                   events=input_summary["events"], require_complete=True)
                    transform_report = validate_transform_prefix(raw, status, items_only=stop_after_items)
                    bounded_log.check()
                    report = {
                        "schema": ("melee-web-original-sheik-items-readiness-capture" if stop_after_items else
                                   "melee-web-original-sheik-transform-prefix-capture"),
                        "version": 1,
                        "scope": scope,
                        "status": "pass",
                        "input_plan_sha256": plan_hash,
                        "menu_recipe_sha256": menu_hash,
                        "input_intentions_sha256": hashlib.sha256(
                            (output / "input-intentions.jsonl").read_bytes()).hexdigest(),
                        "profile_sha256": source_inventory,
                        "build": build,
                        "observer_status": primary_status,
                        "input_status": input_status,
                        "input_stream": input_summary,
                        "driver": receiver.finish(),
                        ("items_readiness" if stop_after_items else "transform_prefix"): transform_report,
                        "claims": {"original_css_sss_setup": not stop_after_items,
                                   "source_consumed_down_b_y_minus_80": not stop_after_items,
                                   "consumed_neutral_release": True,
                                   "active_sheik_grounded_neutral_prefix": not stop_after_items,
                                   "items_readiness_only": stop_after_items,
                                   "match_complete": False,
                                   "draw_or_browser_equivalence": False},
                    }
                    (output / "report.json").write_bytes(canonical(report))
                    return report
                wait_terminal_statuses(status, native_status, deadline)
                if ordinary:
                    bounded_log.check()
                report = receiver.finish(status, native, native_status)
                (output / "report.json").write_bytes(canonical(report))
                return report
        except Exception as error:
            (output / "failure.json").write_bytes(canonical({"scope": scope, "stage": "native",
                "native_launched": True, "pid": process.pid, "error": str(error),
                "comparison_receipt": getattr(error, "receipt", None)}))
            raise
        finally:
            # Cleanup closes the direct child's output; join its owned drain
            # before closing the retained log. No process-name/group cleanup.
            primary_error=sys.exc_info()[1]
            cleanup_error=None
            log_error=None
            try:
                cleanup_process(process, output, scope=scope)
            except Exception as error:
                cleanup_error=error
            # Independently finalize the owned drain even if PID cleanup failed.
            if ordinary or transform_prefix or stadium_prefix:
                try:
                    if bounded_log:
                        bounded_log.finish()
                    elif process.stdout:
                        process.stdout.close()
                except Exception as error:
                    log_error=error
                (output/"owned-log-cleanup.json").write_bytes(canonical(dict(
                    scope=scope,initialized=bounded_log is not None,
                    bytes=bounded_log.size if bounded_log else 0,
                    thread_alive=bounded_log.thread.is_alive() if bounded_log else False,
                    native_cleanup_error=str(cleanup_error) if cleanup_error else None,
                    error=str(log_error) if log_error else None)))
            if cleanup_error or log_error:
                # A diagnostic success candidate is admitted only after owned
                # process and log retirement; retain the primary source failure.
                if (transform_prefix or stadium_prefix) and (output / "report.json").is_file():
                    candidate = json.loads((output / "report.json").read_text())
                    candidate["status"] = "fail"
                    candidate["cleanup_errors"] = [str(value) for value in
                        (cleanup_error, log_error) if value is not None]
                    (output / "report.json").write_bytes(canonical(candidate))
                error=cleanup_error or log_error
                failure = output / "failure.json"
                if not failure.exists():
                    failure.write_bytes(canonical({"scope": scope, "stage": "cleanup",
                        "native_launched": True, "pid": process.pid, "error": str(error),
                        "comparison_receipt": getattr(error, "receipt", None)}))
                if primary_error is None or not (ordinary or transform_prefix or stadium_prefix):
                    raise error


def require_css_join_owner(css, port, *, initial=False):
    """Menu7's original vacant door, or its observed own-Human join.

    mnCharSel_CursorThink joins an NA door when its own cursor enters
    0.2 < y < 22. Movement remains the existing source-owned Mario policy.
    """
    source_slot = css.get("source_slots", [0, 1])[port]
    player, door, cursor = css["players"][port], css["doors"][port], css["cursors"][port]
    model = css["models"][port]
    require(player["slot"] == 0 and cursor["port"] == source_slot and
            cursor["state"] in (0, 1, 2) and
            model["owner"] in (0, source_slot+1) and
            (cursor["state"] != 1 or (cursor["held"] == source_slot and model["owner"] == source_slot+1)),
            "CSS join has foreign slot/cursor ownership")
    if initial and player["kind"] == 3:
        require(player["character"] == 26 and door["kind"] == 3 and door["icon"] == 25 and
                door["costume"] == 0 and cursor["state"] == 0 and cursor["held"] == 0 and
                cursor["x"] == 15.0*source_slot-31.0 and cursor["y"] == -21.5 and
                model["owner"] == 0,
                "CSS vacant door is not the observed initialized owner")
    else:
        require(player["kind"] == door["kind"] == 0, "CSS requires observed own Human join")


def drive_sheik_transform_input(receiver, plan, controller, next_row):
    """Send one exact down-B value only after source-consumed readiness."""
    witness = plan["authored_recipe"]["input_witness"]
    press, release = plan["frames"]
    cap = witness["max_source_samples"]
    while not receiver.grounded_zelda_sequence:
        require(not receiver.ended and receiver.active_source_ticks < cap,
                "Grounded neutral Zelda readiness exhausted the 600-observation cap")
        next_row()
    require(receiver.neutral_consume_sequence is not None and
            receiver.neutral_consume_sequence < receiver.grounded_zelda_sequence,
            "Neutral source consume did not precede grounded Zelda readiness")
    controller.set_both(press[0], press[1], action="sheik-transform-down-b-y-minus-80")
    while not receiver.down_b_consumed:
        require(not receiver.ended,
                "Consumed down-B witness ended before the source PAD record")
        next_row()
    controller.set_both(release[0], release[1], action="sheik-transform-neutral-release")
    while not receiver.down_b_released:
        require(not receiver.ended,
                "Neutral release ended before its source PAD record")
        next_row()
    while not receiver.ended:
        next_row()
    require(receiver.down_b_consumed and receiver.down_b_released and
            receiver.action_seen and receiver.post_swap_neutral is not None,
            "Source transform prefix ended before the consumed input and active Sheik owner")


def menu_actions_for_capture(menus, stop_after_items=False):
    if not stop_after_items:
        return menus["actions"]
    require(menus.get("scope") == "sheik_transform_prefix",
            "Reduced Items actions require the unchanged original transform route")
    indices = [i for i, action in enumerate(menus["actions"])
               if action["label"] == "items-frequency-row"]
    require(len(indices) == 1, "Transform route lacks one authored Items Up action")
    return menus["actions"][:indices[0] + 1]


def drive_transform_items_row(receiver, action, wait_source, tap):
    """Gate the unchanged Items Up pulse on its typed original lock owner."""
    require(action.get("label") == "items-frequency-row" and
            action.get("before", {}).get("scene") == 1 and
            action["before"].get("kind") == 16 and
            action["before"].get("row") == 0 and
            action["before"].get("items_locked") == 0 and
            action.get("after", {}).get("items_locked") == 0,
            "Transform Items row action lacks the authored unlocked-owner guard")
    wait_source(lambda: matches(receiver.latest_menu, action["before"]),
                action["label"] + ":original-items-lock-clear", action["max_polls"])
    require(receiver.latest_menu.get("items_locked") == 0 and
            receiver.items_unlock_sequence is not None and
            receiver.items_unlock_sequence < receiver.seq,
            "Transform Items row action lacks a checked unlocked source poll")
    tap(action, action["label"], action["max_polls"])
    wait_source(lambda: matches(receiver.latest_menu, action["after"]),
                action["label"] + ":observed", action["max_polls"])


def drive_authored_css_sss(receiver, menus, controller, next_row, wait_source, tap):
    """Reuse the original Pipe driver's bounded cursor/door/highlight policy.

    All axes, targets and limits are frozen in the canonical packet. Observer
    input events prove actual consumption; host sleep never proves readiness.
    Menu7 SD and menu8 competitive entry share this source-owned join policy;
    historical menus keep their immediate-Human precondition. There is no
    Results/resolution continuation.
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
    transform_prefix = menus["scope"] == "sheik_transform_prefix"
    stadium_prefix = menus["scope"] == "stadium_go_prefix"
    if menus["version"] in (7, 8, 9, 10, 11):
        for port in policy["ports"]:
            require_css_join_owner(receiver.css, port, initial=True)
    else:
        require([p["kind"] for p in receiver.css["players"]] == [0, 0], "CSS requires two original humans")
    for port, costume in enumerate(policy["costumes"]):
        point = policy["points"][port] if transform_prefix else policy["point"]
        character = policy["characters"][port] if transform_prefix else policy["character"]
        icon = policy["icon_table_indices"][port] if transform_prefix else policy["icon"]
        label = f"original-character-P{port+1}" if transform_prefix else f"Mario-P{port+1}"
        move(port, point, label)
        if menus["version"] in (7, 8, 9, 10, 11):
            wait_source(lambda: receiver.css["players"][port]["kind"] == 0 and
                        receiver.css["doors"][port]["kind"] == 0,
                        f"CSS own Human join P{port+1}", 600)
            require_css_join_owner(receiver.css, port)
        if transform_prefix:
            wait_source(lambda: receiver.css["doors"][port]["icon"] == icon,
                        f"CSS authored icon hover P{port+1}", 600)
        place_label = f"place-P{port+1}" if transform_prefix else f"Mario-place-P{port+1}"
        tap(pair(port, raw_pad(buttons=["A"])), place_label, 600)
        wait_source(lambda: receiver.css["players"][port]["character"] == character,
                    f"original character selected P{port+1}", 600)
        if receiver.css["doors"][port]["costume"] != costume:
            model = receiver.css["models"][port]
            move(port, (model["x"]-2, model["y"]+1.6), "pickup-human-puck")
            tap(pair(port, raw_pad(buttons=["A"])), "pickup-human-puck", 600)
            wait_source(lambda: receiver.css["cursors"][port]["state"] == 1 and
                        receiver.css["cursors"][port]["held"] ==
                        receiver.css.get("source_slots", [0, 1])[port],
                        "held human puck", 600)
            hover_label = ("original-character-costume-hover" if transform_prefix else
                           "Mario-costume-hover")
            move(port, point, hover_label)
            wait_source(lambda: receiver.css["doors"][port]["icon"] == icon,
                        "original character icon", 600)
            for attempt in range(policy["max_costume_taps"]):
                if receiver.css["doors"][port]["costume"] == costume:
                    break
                before = receiver.css["doors"][port]["costume"]
                tap(pair(port, raw_pad(buttons=["X"])), "Mario-costume", 600)
                wait_source(lambda: receiver.css["doors"][port]["costume"] != before, "costume changed", 600)
            require(receiver.css["doors"][port]["costume"] == costume, "Mario costume cap")
            tap(pair(port, raw_pad(buttons=["A"])), "place-colored-Mario", 600)
        wait_source(lambda: receiver.css["cursors"][port]["state"] != 1, "human puck placed", 600)
    expected_lineup = policy["characters"] if transform_prefix else [8, 8]
    require([p["kind"] for p in receiver.css["players"]] == [0,0] and
            [p["character"] for p in receiver.css["players"]] == expected_lineup and
            [d["costume"] for d in receiver.css["doors"]] == policy["costumes"], "CSS final lineup differs")
    before = receiver.menu_polls
    wait_source(lambda: receiver.menu_polls >= before+policy["idle_polls_before_start"], "CSS source idle", 600)
    tap(pair(0, raw_pad(buttons=["START"])), "CSS-start-SSS", 600)
    wait_source(lambda: receiver.stage is not None, "SSS constructor-owned readiness", 600)
    stage = menus["sss"]
    if stadium_prefix:
        drive_stadium_sss(receiver, menus, controller, next_row, wait_source)
        return
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
    if transform_prefix:
        wait_source(lambda: receiver.stage["kind"] == 32 and
                    receiver.stage.get("stable_polls", 0) >= 2,
                    "FD live source highlight stable", 600)
    else:
        wait_source(lambda: receiver.stage["kind"] == 32 and receiver.stage["cooldown"] == 0,
                    "FD original confirmation predicate", 600)
    # Release immediately after observed menu consumption. If an A sample
    # reaches VS instead, the unchanged native/receiver neutral checks fail.
    before = receiver.menu_consumed
    controller.set_both(raw_pad(buttons=["A"]), NEUTRAL_PAD, action="choose-FD")
    wait_source(lambda: receiver.menu_consumed > before and receiver.last_pad[:2] ==
                [raw_pad(buttons=["A"]), NEUTRAL_PAD], "choose-FD:consumed", 600)
    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action="choose-FD:release")



def drive_stadium_sss(receiver, menus, controller, next_row, wait_source):
    """Use only the packet's declared finite policy and observed source owner."""
    if menus["sss"].get("policy") == "same_call_feedback_v1":
        return drive_stadium_sss_feedback(receiver, menus, controller, next_row, wait_source)
    from reference_versus_sequence_capture import raw_pad
    policy = menus["sss"]
    neutral_pair = [NEUTRAL_PAD, NEUTRAL_PAD]
    target = lambda: (receiver.stage is not None and
                      receiver.stage.get("index") == policy["target_index"] and
                      receiver.stage.get("kind") == policy["target_kind"])
    target_stable = lambda: (target() and receiver.stage.get("stable_polls", 0) >= 2)
    movement_poll_total = 0
    owner_start = receiver.menu_polls

    def set_neutral_and_wait(label):
        before = len(receiver.consumed_samples)
        controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=label)
        wait_source(lambda: any(sample["ports"][:2] == neutral_pair
                                for sample in receiver.consumed_samples[before:]),
                    label + ":consumed", policy["max_pad_consume_wait_polls"])
        sample = next(sample for sample in receiver.consumed_samples[before:]
                      if sample["ports"][:2] == neutral_pair)
        return sample["sequence"]

    release_sequence = set_neutral_and_wait("SSS-Stadium-neutral-entry")
    receiver.target_neutral_release_sequence = release_sequence
    if target():
        wait_source(lambda: receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
                    "SSS-Stadium-neutral-stability", policy["max_owner_polls"])

    for pulse_index, pulse in enumerate(policy["pulses"]):
        if receiver.target_neutral_polls >= policy["target_stable_neutral_polls"]:
            break
        pad = raw_pad(x=pulse["x"], y=pulse["y"])
        before_samples = len(receiver.consumed_samples)
        start_polls = receiver.menu_polls
        controller.set_both(pad, NEUTRAL_PAD, action=f"SSS-cardinal-{pulse_index}")
        pulse_consumed = lambda: any(sample["ports"][:2] == [pad, NEUTRAL_PAD]
                                     for sample in receiver.consumed_samples[before_samples:])
        while (receiver.menu_polls - start_polls < pulse["max_source_polls"] and
               not (pulse_consumed() and target_stable())):
            require(receiver.menu_polls - owner_start < policy["max_owner_polls"],
                    "SSS live-owner observation cap exhausted")
            before_poll = receiver.menu_polls
            wait_source(lambda: receiver.menu_polls > before_poll,
                        f"SSS-cardinal-{pulse_index}:source-poll", 1)
            movement_poll_total += receiver.menu_polls - before_poll
            require(movement_poll_total <= policy["max_movement_source_polls"],
                    "SSS cardinal movement budget exceeded")
        require(pulse_consumed(),
                f"SSS cardinal pulse {pulse_index} was not observed in consumed source PAD")
        release_sequence = set_neutral_and_wait(f"SSS-cardinal-{pulse_index}:neutral")
        receiver.target_neutral_release_sequence = release_sequence
        receiver.target_neutral_polls = 0
        if target():
            wait_source(lambda: receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
                        f"SSS-Stadium-neutral-stability-{pulse_index}", policy["max_owner_polls"])

    require(target_stable() and receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
            "Finite SSS cardinal pulse policy did not observe stable Stadium index 18/kind 3")
    before_samples = len(receiver.consumed_samples)
    controller.set_both(raw_pad(buttons=["A"]), NEUTRAL_PAD, action="SSS-Stadium-confirm")
    wait_source(lambda: any(sample["ports"][:2] == [raw_pad(buttons=["A"]), NEUTRAL_PAD]
                            for sample in receiver.consumed_samples[before_samples:]),
                "SSS-Stadium-confirm:consumed", policy["max_pad_consume_wait_polls"])
    release_sequence = set_neutral_and_wait("SSS-Stadium-confirm:neutral-release")
    require(receiver.confirm_sequence is not None and receiver.confirm_release_sequence == release_sequence,
            "SSS Stadium confirm/release did not retain exact consumed PAD rows")


def drive_stadium_sss_feedback(receiver, menus, controller, next_row, wait_source):
    """Choose bounded Pipe inputs from complete original calls; never simulate guest motion."""
    import struct
    from reference_versus_sequence_capture import raw_pad
    policy = menus["sss"]
    neutral_pair = [NEUTRAL_PAD]*2
    owner_start = receiver.menu_polls
    last_polls, last_calls = receiver.menu_polls, receiver.sss_callback_count
    last_consumes = len(receiver.consumed_samples)
    movement_polls = movement_calls = movement_consumes = 0
    moving = False
    release_only = False
    confirmed_sequence = None

    def budget():
        nonlocal last_polls, last_calls, last_consumes, movement_polls, movement_calls, movement_consumes
        if moving:
            movement_polls += receiver.menu_polls-last_polls
            movement_calls += receiver.sss_callback_count-last_calls
            movement_consumes += len(receiver.consumed_samples)-last_consumes
        last_consumes = len(receiver.consumed_samples)
        last_polls, last_calls = receiver.menu_polls, receiver.sss_callback_count
        require(not receiver.ended and
                (receiver.sss_position_route_ready or
                 (release_only and receiver.confirm_sequence == confirmed_sequence)) and
                receiver.menu_polls-owner_start < policy["max_owner_polls"],
                "SSS feedback live-owner observation cap")
        require(movement_polls <= policy["max_movement_source_polls"] and
                movement_calls <= policy["max_movement_callbacks"] and
                movement_consumes <= policy["max_movement_callbacks"],
                "SSS feedback movement cap")

    def wait(predicate, label, cap):
        before_consumes = len(receiver.consumed_samples)
        def checked():
            budget()
            require(len(receiver.consumed_samples)-before_consumes <= policy["max_pad_consume_wait_polls"],
                    "SSS feedback consumed-input wait cap")
            return predicate()
        wait_source(checked, label, cap)
        budget()

    def consumed(pair, after):
        return next((s for s in receiver.consumed_samples[after:]
                     if s["ports"][:2] == pair), None)

    def neutral(label):
        nonlocal moving
        before = len(receiver.consumed_samples)
        controller.set_both(*neutral_pair, action=label)
        wait(lambda: consumed(neutral_pair,before) is not None,
             label+":consumed",policy["max_pad_consume_wait_polls"])
        moving = False  # The original has now consumed the release.
        sequence = consumed(neutral_pair,before)["sequence"]
        receiver.target_neutral_release_sequence = sequence
        receiver.target_neutral_polls = 0
        return sequence

    def completed(after_consume, pair):
        before = receiver.sss_callback_count
        wait(lambda: receiver.sss_callback_count > before,
             "SSS-feedback:completed-call",policy["max_pad_consume_wait_polls"])
        witness = receiver.sss_completed_input
        require(receiver.sss_callback_count == before+1 and witness is not None and
                witness["consumed_sample"]["sequence"] >= after_consume and
                witness["consumed_sample"] == receiver.consumed_samples[-1] and
                witness["consumed_sample"]["ports"][:2] == pair,
                "SSS feedback completed call lacks its exact consumed command")
        return witness

    def target_stable():
        return (receiver.stage is not None and receiver.stage.get("index") == policy["target_index"] and
                receiver.stage.get("kind") == policy["target_kind"] and
                receiver.stage.get("stable_polls",0) >= 2)

    command_sequence = neutral("SSS-feedback:neutral-entry")
    command_pair = neutral_pair
    witness = completed(command_sequence,command_pair)
    reacquire = 0
    while True:
        budget()
        if receiver.sss_geometry_token is None:
            require(movement_polls < policy["max_movement_source_polls"] and
                    movement_calls < policy["max_movement_callbacks"] and
                    movement_consumes < policy["max_movement_callbacks"], "SSS feedback movement cap")
            require(moving and witness["cursor_observed"] and
                    reacquire < policy["max_reacquire_callbacks"],
                    "SSS feedback target not observed within bounded reacquisition")
            # Fixed input exploration only. Do not reuse a prior target or calculate an error.
            reacquire += 1
            witness = completed(command_sequence,command_pair)
            continue
        token = receiver.take_sss_geometry(command_sequence)
        reacquire = 0
        cursor = struct.unpack(">3f",bytes.fromhex(token["cursor_world_hex"]))
        target = struct.unpack(">3f",bytes.fromhex(token["target_world_hex"]))
        extent = struct.unpack(">2f",bytes.fromhex(token["extent_hex"]))
        inside = [target[i]-extent[i] < cursor[i] < target[i]+extent[i] for i in (0,1)]
        if all(inside):
            neutral("SSS-feedback:target-neutral")
            wait(lambda: target_stable() and
                 receiver.target_neutral_polls >= policy["target_stable_neutral_polls"],
                 "SSS-feedback:target-stability",policy["max_owner_polls"])
            break
        axis = 0 if not inside[0] else 1
        delta = target[axis]-cursor[axis]
        magnitude = 35 if abs(delta) <= policy["near_center_distance"] else 70
        value = magnitude if delta>0 else -magnitude
        pad = raw_pad(x=value if axis==0 else 0,y=value if axis==1 else 0)
        command_pair = [pad,NEUTRAL_PAD]
        before = len(receiver.consumed_samples)
        require(movement_polls < policy["max_movement_source_polls"] and
                movement_calls < policy["max_movement_callbacks"] and
                movement_consumes < policy["max_movement_callbacks"], "SSS feedback movement cap")
        moving = True
        controller.set_both(*command_pair,action="SSS-feedback:cardinal")
        wait(lambda: consumed(command_pair,before) is not None,
             "SSS-feedback:cardinal-consumed",policy["max_pad_consume_wait_polls"])
        command_sequence = consumed(command_pair,before)["sequence"]
        witness = completed(command_sequence,command_pair)

    before = len(receiver.consumed_samples)
    controller.set_both(raw_pad(buttons=["A"]),NEUTRAL_PAD,action="SSS-Stadium-confirm")
    wait(lambda: consumed([raw_pad(buttons=["A"]),NEUTRAL_PAD],before) is not None,
         "SSS-Stadium-confirm:consumed",policy["max_pad_consume_wait_polls"])
    confirmed_sequence = consumed([raw_pad(buttons=["A"]),NEUTRAL_PAD],before)["sequence"]
    require(receiver.confirm_sequence == confirmed_sequence,
            "SSS feedback A sample did not qualify as the original confirm")
    # Source confirmation may exit SSS before the Pipe neutral is consumed.
    # From this point the only permitted operation is bounded neutral release.
    release_only = True
    release = neutral("SSS-Stadium-confirm:neutral-release")
    require(receiver.confirm_release_sequence == release,
            "SSS Stadium confirm/release did not retain exact consumed PAD rows")


class StadiumNaturalStockoutWitness:
    """Validate actual two-fighter stock rows and mode-9 ordering."""
    def __init__(self):
        self.expected_start_tick = None
        self.last_tick = None
        self.last_stocks = None
        self.last_fighters = None
        self.transitions = []
        self.mode9 = None
        self.source_ticks = 0

    def begin(self, go_source_tick):
        require(self.expected_start_tick is None and self.last_tick is None and
                type(go_source_tick) is int and go_source_tick >= 0,
                'Natural stockout witness lacks one actual B GO source-tick binding')
        self.expected_start_tick = go_source_tick

    def observe(self, row, fighters, mode):
        require(row.get('event') == 'boundary' and row['payload'].get('boundary') == 'source_tick' and
                type(row.get('source_tick')) is int and type(row.get('seq')) is int and
                isinstance(fighters, dict) and isinstance(fighters.get('stocks'), tuple) and
                len(fighters['stocks']) == 2 and all(type(x) is int and 0 <= x <= 4 for x in fighters['stocks']) and
                type(mode) is int,
                'Natural stockout witness lacks actual source-tick fighters/display')
        tick, stocks = row['source_tick'], fighters['stocks']
        require((self.last_tick is None and tick == self.expected_start_tick and stocks == (4, 4)) or
                (self.last_tick is not None and tick == self.last_tick + 1 and stocks[1] == 4 and
                 stocks[0] <= self.last_stocks[0] and self.last_stocks[0] - stocks[0] <= 1),
                'Natural stockout source rows skipped/reordered a stock or changed neutral P2 stocks')
        if self.last_tick is None:
            require(mode != 9, 'Natural stockout began in mode9 before any observed stock loss')
        elif stocks[0] != self.last_stocks[0]:
            require(stocks[0] == self.last_stocks[0] - 1,
                    'Natural stockout P1 stock transition is not exactly one source-observed life')
            self.transitions.append(dict(sequence=row['seq'], source_tick=tick,
                                         from_stocks=self.last_stocks, to_stocks=stocks))
        if mode == 9:
            require(stocks == (0, 4) and [item['to_stocks'][0] for item in self.transitions] == [3, 2, 1, 0],
                    'Natural stockout mode9 appeared before P1 zero with exact four observed losses')
            if self.mode9 is None:
                self.mode9 = dict(sequence=row['seq'], source_tick=tick, mode=mode,
                                  stocks=list(stocks))
        self.last_tick, self.last_stocks, self.last_fighters = tick, stocks, fighters
        self.source_ticks += 1

    def finish(self, exit_row, result_outcome):
        require(type(result_outcome) is int and result_outcome == 2 and self.mode9 is not None and
                type(exit_row.get('seq')) is int and exit_row['seq'] > self.mode9['sequence'] and
                self.last_stocks == (0, 4) and
                [item['to_stocks'][0] for item in self.transitions] == [3, 2, 1, 0],
                'Natural stockout lacks ordered four actual losses, mode9-before-exit, or elimination outcome')
        return {'schema': 'melee-web-stadium-natural-zero-stockout-v1',
                'profile': STADIUM_WHOLE_STOCKOUT_PROFILE['profile'],
                'go_source_tick': self.expected_start_tick,
                'source_tick_count': self.source_ticks,
                'initial_stocks': [4, 4], 'p1_stock_transitions': list(self.transitions),
                'mode9_source_tick': self.mode9,
                'vs_exit_return_sequence': exit_row['seq'], 'result_outcome': result_outcome,
                'source_stock_write_observed': False, 'standings_observed': False,
                'lifecycle_parity': False, 'C3': False}


def _stockout_source_fighter_state(row):
    """Project the two fighter slices for the existing strict fighter decoder.

    Whole Stadium source ticks also carry map/display slices with flags 2 and
    5. The full Stadium snapshot validator checks those; this projection keeps
    them out of `_source_fighter_state`, whose flags are fighter-only.
    """
    payload = row.get('payload') if isinstance(row, dict) else None
    slices = payload.get('slices') if isinstance(payload, dict) else None
    require(isinstance(slices, list) and all(isinstance(item, dict) for item in slices),
            'Natural stockout source row lacks a complete slice descriptor list')
    fighter_slices = [item for item in slices
                      if item.get('name') in ('fighter_head', 'fighter_stocks')]
    require(len(fighter_slices) == 4 and
            all(sum(item.get('name') == name and item.get('flags') == slot
                    for item in fighter_slices) == 1
                for name in ('fighter_head', 'fighter_stocks') for slot in (0, 1)),
            'Natural stockout source row lacks exactly two FighterHead and FighterStocks slices')
    projected_payload = dict(payload)
    projected_payload['slices'] = fighter_slices
    projected_row = dict(row)
    projected_row['payload'] = projected_payload
    from reference_versus_sequence_capture import _source_fighter_state
    return _source_fighter_state(projected_row)


class StadiumWholeFeedbackReceiver(StadiumGoPrefixReceiver):
    """Strict whole-session rows feeding the existing CSS/SSS input policies."""
    def __init__(self, menus, receipt, expected, *, stadium_whole_b_state=False,
                 stadium_whole_long_idle=False, stadium_whole_paired_idle=False,
                 stadium_whole_stockout=False, replay_binding_sha256=None):
        super().__init__(menus)
        require(type(stadium_whole_b_state) is bool, "Whole Stadium B observation opt-in must be boolean")
        require(type(stadium_whole_long_idle) is bool and
                (not stadium_whole_long_idle or stadium_whole_b_state),
                "Whole Stadium long-idle profile requires the selected B-state observation")
        require(type(stadium_whole_paired_idle) is bool and
                (not stadium_whole_paired_idle or stadium_whole_b_state and stadium_whole_long_idle),
                "Whole Stadium paired idle requires B-state and long-idle observations")
        from stadium_go_prefix import StadiumConstructorObservations
        self.replay_binding_sha256 = replay_binding_sha256
        self.stadium_whole_b_state = stadium_whole_b_state
        self.stadium_whole_long_idle = stadium_whole_long_idle
        self.stadium_whole_paired_idle = stadium_whole_paired_idle
        require(type(stadium_whole_stockout) is bool and
                (not stadium_whole_stockout or stadium_whole_b_state and
                 not stadium_whole_long_idle and not stadium_whole_paired_idle),
                'Whole Stadium natural stockout requires B-state and excludes idle profiles')
        self.stadium_whole_stockout = stadium_whole_stockout
        self.stockout_witness = StadiumNaturalStockoutWitness() if stadium_whole_stockout else None
        self.stockout_summary = None
        self.progress_scope = ('stadium_paired_idle_feedback_v1' if stadium_whole_paired_idle else
                               'stadium_whole3_feedback_v1')
        self.match_count = 2 if stadium_whole_paired_idle else 3
        self.b_state_matches = (0, 1) if stadium_whole_paired_idle else (1,)
        self.b_constructor = StadiumConstructorObservations()
        self.b_entry_seen = False
        self.b_go = None
        self.b_state_samples = []
        self.b_observation = None
        self.b_observations = []
        self.long_idle_window = None
        self.long_idle_window_result = None
        self.long_idle_windows = []
        require(isinstance(receipt, str) and len(receipt) == 64 and
                all(c in '0123456789abcdef' for c in receipt) and receipt != '0'*64,
                'Whole Stadium receipt identity differs')
        self.receipt, self.expected = receipt, expected
        self.match_index = 0
        self.announcements = []
        self.boundaries = []
        self.tick_count = 0
        self.previous_tick = None
        self.pending_neutral_sample = None
        self.chord_requested = False
        self.pause_consumed = self.pause_released = None
        self.pause_positive_clock = self.pause_ready_clock = None
        self.exit_requested = False
        self.chord_consumed = None
        self.chord_released = None
        self.outcome_seen = False
        self.returned_css = False
        self.start_allowed = False

    def selected_b_state(self, index):
        return self.stadium_whole_b_state and index in self.b_state_matches

    def selected_long_idle(self, index):
        return self.stadium_whole_long_idle and index in self.b_state_matches

    def selected_stockout(self, index):
        return self.stadium_whole_stockout and index == STADIUM_WHOLE_STOCKOUT_PROFILE['selected_match']

    def stockout_gameplay_active(self):
        return self.selected_stockout(self.match_index) and not self.outcome_seen

    def next_lifetime(self):
        require(self.returned_css and self.match_index < self.match_count - 1 and not self.ended,
                'Whole Stadium lifetime reset preceded natural ReturnCss')
        self.sss_positions.require_closed()
        # The native observer numbers SSS calls for the entire recording.
        # Carry only its closed counter; all geometry and owner state is fresh.
        last_sss_call = self.sss_positions.last_call
        seq, boundaries, announcements, index = self.seq, self.boundaries, self.announcements, self.match_index
        fresh = type(self)(self.menus, self.receipt, self.expected,
            stadium_whole_b_state=self.stadium_whole_b_state,
            stadium_whole_long_idle=self.stadium_whole_long_idle,
            stadium_whole_paired_idle=self.stadium_whole_paired_idle,
            stadium_whole_stockout=self.stadium_whole_stockout,
            replay_binding_sha256=self.replay_binding_sha256)
        fresh.b_observations = self.b_observations
        fresh.long_idle_windows = self.long_idle_windows
        fresh.stockout_summary = self.stockout_summary
        if not self.stadium_whole_paired_idle:
            fresh.b_observation = self.b_observation
            fresh.long_idle_window = self.long_idle_window
            fresh.long_idle_window_result = self.long_idle_window_result
        fresh.sss_positions.last_call = last_sss_call
        self.__dict__.update(fresh.__dict__)
        self.seq, self.boundaries, self.announcements, self.match_index = seq, boundaries, announcements, index+1

    def accept_b_progress(self, row):
        from stadium_go_prefix import (CONSTRUCTOR_POINTS, _validate_progress,
                                       _validate_stadium_idle_state_record)
        p = row['payload']
        require(self.selected_b_state(self.match_index) and
                p.get('stadium_b_state_observation') is True and self.b_entry_seen,
                'Whole Stadium B Progress lacks explicit selected Entry scope')
        phase = p.get('phase')
        if phase in CONSTRUCTOR_POINTS:
            require(not self.setup_seen and self.b_go is None,
                    'Whole Stadium B constructor escaped Entry-to-Setup')
            self.b_constructor.accept(row, route_ready=True,
                scope=self.progress_scope, expected_receipt=self.receipt,
                match_index=self.match_index)
        else:
            require(phase == 'go_after' and self.setup_seen and self.b_go is None,
                    'Whole Stadium B GO is duplicate or outside completed Setup')
            self.b_constructor.finish()
            _validate_progress(row, phase, self.receipt, scope=self.progress_scope,match_index=self.match_index)
            self.b_go = _validate_stadium_idle_state_record(row, 'GO', row['source_tick'])
            if self.selected_stockout(self.match_index):
                self.stockout_witness.begin(self.b_go['source_tick'])
            if self.selected_long_idle(self.match_index):
                profile = (STADIUM_WHOLE_PAIRED_IDLE_PROFILE if self.stadium_whole_paired_idle else
                           STADIUM_WHOLE_LONG_IDLE_PROFILE)
                self.long_idle_window = {
                    'schema': 'melee-web-stadium-whole-paired-idle-window-v1' if self.stadium_whole_paired_idle else
                              'melee-web-stadium-whole-long-idle-window-v1',
                    'profile': profile['profile'],
                    'match_index': self.match_index,
                    'go': self._idle_row_identity(row, ordinal=0),
                    'expected_draw_ordinal': row['draw_ordinal'],
                    'draw_open': None,
                    'last_ordinal': -1,
                    'state_count': 0,
                    'consumed_pad_count': 0,
                    'state_digest': hashlib.sha256(),
                    'consumed_pad_digest': hashlib.sha256(),
                    'target_source_tick': None,
                    'first_later_draw_return': None,
                    'natural_tail_source_ticks': [],
                }

    @staticmethod
    def _idle_row_identity(row, *, ordinal=None):
        value = dict(sequence=row['seq'], source_tick=row['source_tick'],
                     draw_ordinal=row['draw_ordinal'])
        if ordinal is not None:
            value['ordinal'] = ordinal
        return value

    @property
    def long_idle_window_complete(self):
        return bool(self.stadium_whole_long_idle and isinstance(self.long_idle_window, dict) and
                    self.long_idle_window['first_later_draw_return'] is not None)

    def _observe_long_idle_tick(self, row, snapshot, consumed_sample):
        window = self.long_idle_window
        if not self.selected_long_idle(self.match_index) or window is None or \
                window['first_later_draw_return'] is not None:
            return
        from retail_input_plan import DISCONNECTED_PAD
        ordinal = row['source_tick'] - window['go']['source_tick']
        require(ordinal == window['last_ordinal'] + 1 and 0 <= ordinal <= 3504 and
                row['seq'] > window['go']['sequence'] and window['draw_open'] is None,
                'Whole Stadium long-idle typed SourceTick is missing, reordered or in a draw')
        require(isinstance(consumed_sample, dict) and
                consumed_sample['sequence'] < row['seq'] and
                consumed_sample.get('match_index') == self.match_index and
                consumed_sample['source_tick'] == row['source_tick'] and
                consumed_sample.get('draw_ordinal') == row['draw_ordinal'] and
                consumed_sample['ports'] == [NEUTRAL_PAD, NEUTRAL_PAD,
                                              DISCONNECTED_PAD, DISCONNECTED_PAD] and
                consumed_sample['errors'] == [0, 0, -1, -1],
                'Whole Stadium long-idle SourceTick lacks its exact four-port consumed PAD')
        require(snapshot.get('sequence') == row['seq'] and
                snapshot.get('source_tick') == row['source_tick'] and
                snapshot.get('draw_ordinal') == row['draw_ordinal'],
                'Whole Stadium long-idle typed state differs from its SourceTick')
        require(row['draw_ordinal'] == window['expected_draw_ordinal'],
                'Whole Stadium long-idle SourceTick draw ordinal differs')
        window['state_digest'].update(json.dumps(snapshot, sort_keys=True).encode())
        pad_identity = dict(consume_sequence=consumed_sample['sequence'],
            source_tick=row['source_tick'], source_tick_sequence=row['seq'],
            ports=consumed_sample['ports'], errors=consumed_sample['errors'])
        window['consumed_pad_digest'].update(json.dumps(pad_identity, sort_keys=True).encode())
        window['state_count'] += 1
        window['consumed_pad_count'] += 1
        window['last_ordinal'] = ordinal
        if ordinal == 3500:
            require(window['state_count'] == window['consumed_pad_count'] == 3501,
                    'Whole Stadium long-idle target lacks all 3501 typed states/four-port consumes')
            window['target_source_tick'] = self._idle_row_identity(row, ordinal=ordinal)
        elif ordinal > 3500:
            require(window['target_source_tick'] is not None and ordinal <= 3504,
                    'Whole Stadium long-idle natural tail exceeds four actual SourceTicks')
            window['natural_tail_source_ticks'].append(self._idle_row_identity(row, ordinal=ordinal))

    def _observe_long_idle_boundary(self, row, kind):
        window = self.long_idle_window
        if not self.selected_long_idle(self.match_index) or window is None or \
                window['first_later_draw_return'] is not None:
            return
        require(kind in ('pad_poll', 'pad_consume', 'source_tick', 'draw_enter', 'draw_return'),
                'Whole Stadium long-idle window contains an unexpected boundary')
        require(type(row.get('seq')) is int and row['seq'] >= 0 and
                type(row.get('source_tick')) is int and row['source_tick'] >= 0 and
                type(row.get('draw_ordinal')) is int and row['draw_ordinal'] >= 0,
                'Whole Stadium long-idle boundary lacks actual source coordinates')
        require(row['draw_ordinal'] == window['expected_draw_ordinal'],
                'Whole Stadium long-idle boundary draw ordinal is missing, duplicated or reordered')
        opened = window['draw_open']
        if opened is not None:
            require(kind in ('pad_poll', 'draw_return') and
                    row['draw_ordinal'] == opened['draw_ordinal'] and
                    row['source_tick'] == opened['source_tick'],
                    'Whole Stadium long-idle open draw boundary differs')
        if kind == 'draw_enter':
            require(opened is None,
                    'Whole Stadium long-idle DrawEnter duplicated before DrawReturn')
            window['draw_open'] = self._idle_row_identity(row)
            return
        if kind != 'draw_return':
            return
        require(kind == 'draw_return' and opened is not None,
                'Whole Stadium long-idle DrawReturn lacks its actual DrawEnter')
        window['draw_open'] = None
        window['expected_draw_ordinal'] += 1
        target = window['target_source_tick']
        if target is not None and row['seq'] > target['sequence']:
            require(len(window['natural_tail_source_ticks']) <=
                    (STADIUM_WHOLE_PAIRED_IDLE_PROFILE if self.stadium_whole_paired_idle else
                     STADIUM_WHOLE_LONG_IDLE_PROFILE)['natural_tail_max'],
                    'Whole Stadium long-idle first DrawReturn exceeds its natural tail bound')
            window['first_later_draw_return'] = self._idle_row_identity(row)

    def long_idle_window_summary(self):
        window = self.long_idle_window
        require(self.long_idle_window_complete and window['target_source_tick'] is not None and
                window['state_count'] == window['consumed_pad_count'] ==
                    3501 + len(window['natural_tail_source_ticks']) and
                len(window['natural_tail_source_ticks']) <=
                    (STADIUM_WHOLE_PAIRED_IDLE_PROFILE if self.stadium_whole_paired_idle else
                     STADIUM_WHOLE_LONG_IDLE_PROFILE)['natural_tail_max'],
                'Whole Stadium long-idle profile lacks its actual GO+3500/natural DrawReturn window')
        return {
            'schema': window['schema'], 'profile': window['profile'],
            'match_index': self.match_index,
            'comparison_post_go_ticks': 3500, 'comparison_typed_state_count': 3501,
            'typed_state_count_including_natural_tail': window['state_count'],
            'four_port_consumed_pad_count': window['consumed_pad_count'],
            'go': window['go'], 'target_source_tick': window['target_source_tick'],
            'first_later_natural_draw_return': window['first_later_draw_return'],
            'natural_tail_source_ticks': list(window['natural_tail_source_ticks']),
            'typed_state_digest_sha256': window['state_digest'].hexdigest(),
            'consumed_pad_digest_sha256': window['consumed_pad_digest'].hexdigest(),
            'source_schedule_verified': False, 'lifecycle_parity': False,
        }

    def observe_pause_clock(self, row):
        """Indirect pause evidence from existing source clock, never a pause getter."""
        if not self.chord_requested or self.exit_requested or self.pause_consumed is None:
            return
        require(row.get('event') == 'boundary' and row['payload'].get('boundary') == 'source_tick' and
                row['payload'].get('whole_session') is True and
                type(row['payload'].get('match_index')) is int and
                row['payload']['match_index'] == self.match_index,
                'Whole Stadium pause readiness has a foreign source tick')
        data, metadata = _transform_slices(row['payload'])
        raw = data.get((14, 0), b'')
        require(len(raw) == 0x2e and metadata[(14, 0)]['address'] == 0x8046b6a0,
                'Whole Stadium pause clock source identity differs')
        clock = dict(sequence=row['seq'], source_tick=row['source_tick'],
                     match_index=self.match_index, pauser=int.from_bytes(raw[1:2], 'big', signed=True),
                     pause_timer=raw[2], unk3=raw[3], hud_enabled=raw[5], outcome=raw[8],
                     frame_count=int.from_bytes(raw[0x24:0x28], 'big'),
                     timer_seconds=int.from_bytes(raw[0x28:0x2c], 'big'),
                     timer_frames=int.from_bytes(raw[0x2c:0x2e], 'big'),
                     evidence='indirect_source_pause_timer_and_frozen_clock')
        require(clock['pauser'] == 0 and clock['hud_enabled'] == 1 and
                clock['unk3'] == 0 and clock['outcome'] == 0 and clock['pause_timer'] <= 10 and
                clock['sequence'] > self.pause_consumed['sequence'] and
                self.pause_consumed['match_index'] == self.match_index,
                'Whole Stadium pause clock owner/state differs')
        if self.pause_positive_clock is None:
            if clock['pause_timer'] > 0:
                self.pause_positive_clock = clock
            return
        prior = self.pause_positive_clock
        require(clock['sequence'] > prior['sequence'] and
                all(clock[k] == prior[k] for k in ('frame_count', 'timer_seconds', 'timer_frames')),
                'Whole Stadium pause clock is stale or advanced')
        if clock['pause_timer'] == 0 and self.pause_released is not None:
            require(clock['sequence'] > self.pause_released['sequence'] and
                    self.pause_released['match_index'] == self.match_index,
                    'Whole Stadium pause readiness preceded consumed release')
            if self.pause_ready_clock is None:
                self.pause_ready_clock = clock

    def accept(self, row):
        from stadium_go_prefix import SSS_POSITION_PHASES
        from retail_setup_validation import _decode_setup
        from reference_versus_sequence_capture import raw_pad
        from retail_input_plan import DISCONNECTED_PAD
        require(not self.ended and type(row.get('seq')) is int and row['seq'] == self.seq,
                'Whole Stadium source sequence differs')
        self.seq += 1
        event, payload = row['event'], row['payload']
        if event in ('handshake', 'start'):
            require(self.announcements == ([] if event == 'handshake' else ['handshake']) and
                    payload.get('whole_session') is True and payload.get('match_count') == self.match_count and
                    payload.get('stadium_whole_feedback') is True and
                    payload.get('setup_receipt_sha256') == self.receipt,
                    'Whole Stadium source announcement differs')
            if self.stadium_whole_paired_idle:
                require(payload.get('stadium_whole_b_state') is True and
                        payload.get('stadium_observed_matches') == [0, 1] and
                        payload.get('stadium_observed_match') is None,
                        'Whole Stadium paired selected-match declaration differs')
            else:
                require(payload.get('stadium_whole_b_state', False) is self.stadium_whole_b_state and
                        (not self.stadium_whole_b_state or type(payload.get('stadium_observed_match')) is int and payload['stadium_observed_match'] == 1),
                        'Whole Stadium B announcement opt-in differs')
            require(payload.get('stadium_whole_replay_binding_sha256') == self.replay_binding_sha256,
                    'Whole Stadium replay source announcement differs')
            if self.stadium_whole_long_idle:
                expected_idle = (STADIUM_WHOLE_PAIRED_IDLE_PROFILE if self.stadium_whole_paired_idle else
                                 STADIUM_WHOLE_LONG_IDLE_PROFILE)
                require(payload.get('stadium_whole_long_idle') == expected_idle,
                        'Whole Stadium long-idle source profile announcement differs')
            else:
                require(payload.get('stadium_whole_long_idle') is None,
                        'Whole Stadium default route received an undeclared long-idle profile')
            self.announcements.append(event)
            return
        require(self.announcements == ['handshake', 'start'], 'Whole Stadium row preceded announcements')
        if event == 'error':
            raise SdDiagnosticError('Whole Stadium native error: '+str(payload))
        if event == 'end':
            require(self.match_index == self.match_count - 1 and self.returned_css and
                    payload.get('natural') is True and payload.get('status') == 'completed',
                    'Whole Stadium ended before the declared final natural CSS')
            if self.stadium_whole_paired_idle:
                require(len(self.b_observations) == 2 and len(self.long_idle_windows) == 2 and
                        [item['match_index'] for item in self.long_idle_windows] == [0, 1],
                        'Whole Stadium paired terminal lacks both selected observation windows')
            else:
                require(not self.stadium_whole_stockout or self.stockout_summary is not None,
                        'Whole Stadium terminal lacks completed natural stockout mode9/result witness')
                require(not self.stadium_whole_b_state or self.b_observation is not None,
                        'Whole Stadium terminal lacks completed B observation span')
                require(not self.stadium_whole_long_idle or self.long_idle_window_complete,
                        'Whole Stadium terminal precedes the selected B long-idle window')
            self.ended = True
            return
        require(event in ('progress', 'boundary') and payload.get('whole_session') is True and
                type(payload.get('match_index')) is int and payload['match_index'] == self.match_index,
                'Whole Stadium scope or match index differs')
        if event == 'progress':
            require(payload.get('diagnostic') == self.progress_scope and
                    payload.get('setup_receipt_sha256') == self.receipt,
                    'Whole Stadium Progress identity differs')
            phase = payload.get('phase')
            from stadium_go_prefix import CONSTRUCTOR_POINTS
            if phase in CONSTRUCTOR_POINTS or phase == 'go_after':
                self.accept_b_progress(row)
                return
            if phase == 'sss_return':
                require(not self.sss_position_route_ready and self.sss_live_owner_sequence is None,
                        'Whole Stadium duplicate SSS readiness')
                self.sss_position_route_ready = True
            else:
                require(phase in SSS_POSITION_PHASES, 'Whole Stadium unsupported Progress phase')
                observation = self.sss_positions.accept(row, route_ready=self.sss_position_route_ready,
                    expected_receipt=self.receipt, scope=self.progress_scope,
                    match_index=self.match_index)
                self._feedback_position(row, observation)
            return
        name = payload.get('boundary')
        self.boundaries.append(row)
        self._observe_long_idle_boundary(row, name)
        if name == 'css_enter' and self.match_index == 0:
            from whole_session_replay import _first_css_context
            context = _first_css_context([row])
            rules = bytes.fromhex(context['game_rules_hex'])
            from capture_allocation_history import stadium_three_rule_profile
            if stadium_three_rule_profile(self.expected) == 'stock4_none':
                save=bytes.fromhex(context['save_data_hex'])
                require((rules[2],rules[3],rules[4],rules[5],rules[6],rules[8],rules[9],rules[10],rules[11]) ==
                        (1,2,4,0,10,0,0,1,0) and save[0x448] == 0xff and
                        save[0x450:0x458].hex() == self.expected['item_mask_hex'] and
                        list(save[0x458:0x45a]) == [1,1],
                        'Whole Stadium first CSS stock4/ItemsNone source context differs')
            else:
                require((rules[2],rules[3],rules[4],rules[8],rules[10],rules[6]) == (0,2,3,0,1,10),
                        'Whole Stadium first CSS source TIME120 baseline differs')
        elif name == 'pad_poll':
            self._pad_poll(row)
        elif name == 'pad_consume':
            if not self.setup_seen:
                self._pad_consume_stadium(row)
            else:
                ports, errors, queue = _transform_consumed_ports(payload, row['seq'])
                chord = raw_pad(buttons=['L','R','A','START'])
                start = raw_pad(buttons=['START'])
                allowed = [[NEUTRAL_PAD, NEUTRAL_PAD]]
                if self.stockout_gameplay_active():
                    edge = raw_pad(x=80)
                    jump = raw_pad(buttons=['X'], x=80)
                    allowed.extend([[edge, NEUTRAL_PAD], [jump, NEUTRAL_PAD]])
                if self.chord_requested and ((not self.exit_requested and self.pause_released is None) or
                                             (self.exit_requested and self.chord_released is None)):
                    allowed.append([chord, NEUTRAL_PAD])
                if self.start_allowed:
                    allowed.extend([[start, NEUTRAL_PAD], [NEUTRAL_PAD, start], [start, start]])
                require(ports[2:] == [DISCONNECTED_PAD]*2 and errors == [0,0,-1,-1] and
                        ports[:2] in allowed, 'Whole Stadium consumed PAD escaped current phase')
                sample = dict(sequence=row['seq'], source_tick=row['source_tick'],
                              phase='whole-post-setup', match_index=self.match_index, ports=ports, errors=errors,
                              pc=payload['pc'], lr=payload['lr'], gprs=list(payload['gprs']), **queue)
                if self.selected_long_idle(self.match_index):
                    sample['draw_ordinal'] = row['draw_ordinal']
                if not self.chord_requested and not (self.selected_stockout(self.match_index) and self.outcome_seen):
                    require(self.pending_neutral_sample is None,
                            'Whole Stadium repeated unpaired neutral consumed sample')
                    self.pending_neutral_sample = sample
                self.consumed_samples.append(sample)
                self.menu_consumed += 1
                self.last_pad = ports
                if self.chord_requested and not self.exit_requested:
                    if ports[:2] == [chord, NEUTRAL_PAD] and self.pause_consumed is None:
                        require(self.tick_count >= 180, 'Whole Stadium pause preceded minimum neutral ticks')
                        self.pause_consumed = sample.copy()
                    elif self.pause_consumed is not None and self.pause_released is None and ports[:2] == [NEUTRAL_PAD]*2:
                        self.pause_released = sample.copy()
                elif self.exit_requested:
                    require(self.pause_ready_clock is not None and
                            row['seq'] > self.pause_ready_clock['sequence'],
                            'Whole Stadium exit input preceded pause readiness')
                    if ports[:2] == [chord, NEUTRAL_PAD] and self.chord_consumed is None:
                        self.chord_consumed = sample.copy()
                    elif self.chord_consumed is not None and self.chord_released is None and ports[:2] == [NEUTRAL_PAD]*2:
                        self.chord_released = sample.copy()
        elif name == 'sss_exit':
            self.sss_positions.require_closed()
            self.sss_position_route_ready = False
            self.sss_geometry_token = self.sss_geometry_pending = self.sss_completed_input = None
        elif name in ('entry', 'setup'):
            items = [x for x in payload.get('slices', []) if x.get('name') == 'match_setup']
            require(len(items) == 1 and _decode_setup(items[0]['hex']) == self.expected,
                    'Whole Stadium source Setup differs from its exact declaration')
            if name == 'entry' and self.selected_b_state(self.match_index):
                require(not self.b_entry_seen, 'Whole Stadium duplicate B Entry')
                self.b_entry_seen = True
            if name == 'setup':
                if self.selected_b_state(self.match_index):
                    require(self.b_entry_seen, 'Whole Stadium B Setup lacks Entry')
                    self.b_constructor.finish()
                require(not self.setup_seen and self.confirm_release_sequence is not None,
                        'Whole Stadium Setup preceded consumed menu confirmation/release')
                self.setup_seen = True
        elif name == 'source_tick':
            require(not (self.selected_stockout(self.match_index) and self.outcome_seen),
                    'Whole Stadium natural stockout source tick followed its validated exit')
            require(self.setup_seen and self.last_pad is not None and
                    (row['source_tick'] == 0 if self.previous_tick is None else row['source_tick'] == self.previous_tick+1),
                    'Whole Stadium source tick lacks ordered Setup/PAD')
            sample = None
            if not self.chord_requested:
                sample = self.pending_neutral_sample
                require(sample is not None and sample['sequence'] < row['seq'] and
                        sample['source_tick'] == row['source_tick'] and
                        (sample['ports'][:2] == [NEUTRAL_PAD]*2 or
                         self.selected_stockout(self.match_index) and
                         sample['ports'][1] == NEUTRAL_PAD and sample['ports'][0] in
                         [NEUTRAL_PAD, raw_pad(x=80), raw_pad(buttons=['X'], x=80)]),
                        'Whole Stadium counted tick lacks its fresh neutral consumed owner')
                self.pending_neutral_sample = None
                self.tick_count += 1
            snapshot = None
            if self.selected_b_state(self.match_index) and self.b_go is not None:
                from stadium_go_prefix import _validate_stadium_idle_state_record
                require(self.b_state_samples or row['source_tick'] == self.b_go['source_tick'],
                        'Whole Stadium B first state is not actual GO containing tick')
                snapshot = _validate_stadium_idle_state_record(row, 'IDLE',row['source_tick'])
                self.b_state_samples.append(snapshot)
            if self.selected_long_idle(self.match_index) and self.b_go is not None:
                self._observe_long_idle_tick(row, snapshot, sample)
            if self.selected_stockout(self.match_index) and snapshot is not None:
                self.stockout_witness.observe(row, _stockout_source_fighter_state(row),
                    snapshot['display']['mode_s16'])
            self.previous_tick = row['source_tick']
            self.observe_pause_clock(row)
        elif name == 'vs_exit_return':
            items = [x for x in payload.get('slices', []) if x.get('name') == 'result']
            stockout = self.selected_stockout(self.match_index)
            require((self.stockout_witness is not None if stockout else
                     self.pause_released is not None and self.pause_ready_clock is not None and
                     self.chord_consumed is not None and self.exit_requested and
                     self.chord_consumed['sequence'] < row['seq']) and len(items) == 1,
                    'Whole Stadium exit lacks the exact selected natural-route witness or result header')
            # Native Result is the first0x28 bytes of original MatchEnd at
            # MatchExitInfo0x80479d98 +0xc, not the whole0x227c-byte object.
            # gm/types.h declares timer+0 and outcome+4 in this header prefix.
            result = items[0]
            raw = bytes.fromhex(result['hex'])
            outcome = 2 if stockout else 7
            require(result.get('tag') == 15 and result.get('flags') == 0 and
                    result.get('address') == 0x80479da4 and
                    result.get('size') == len(raw) == 0x28 and raw[4] == outcome,
                    'Whole Stadium MatchEnd header outcome differs from selected route')
            if stockout:
                require(self.pending_neutral_sample is None,
                        'Whole Stadium natural stockout exit has an unpaired consumed sample')
                self.stockout_summary = self.stockout_witness.finish(row, raw[4])
            if self.selected_b_state(self.match_index):
                require(self.b_go is not None and self.b_state_samples,
                        'Whole Stadium selected match exit lacks actual GO/state span')
                if self.selected_long_idle(self.match_index):
                    self.long_idle_window_result = self.long_idle_window_summary()
                    self.long_idle_windows.append(self.long_idle_window_result)
                self.b_observation = dict(constructor=self.b_constructor.finish(include_state_projection=True),
                    go=self.b_go, source_tick_states=self.b_state_samples,
                    registration_event_observed=False, lifetime_pairing_observed=False)
                if self.selected_long_idle(self.match_index):
                    self.b_observation['long_idle_window'] = self.long_idle_window_result
                if self.stadium_whole_paired_idle:
                    self.b_observations.append(self.b_observation)
            self.outcome_seen = True
        elif name == 'return_css':
            require(self.outcome_seen and
                    (self.chord_released is not None or self.selected_stockout(self.match_index)),
                    'Whole Stadium CSS return preceded its outcome or consumed exit release')
            require(not self.selected_long_idle(self.match_index) or self.long_idle_window_complete,
                    'Whole Stadium Results began before the selected long-idle window')
            self.returned_css = True


class StadiumDeadlineController:
    """Bound existing Pipe actions; no alternate input or Results policy."""
    def __init__(self, controller, deadline):
        self.controller, self.deadline = controller, deadline
        self.sleep = controller.sleep

    def set_both(self, p1, p2, *, action):
        require(time.monotonic() < self.deadline, 'Whole Stadium deadline expired before input')
        self.controller.set_both(p1, p2, action=action)

    def hold(self, p1, p2, seconds, *, action):
        require(type(seconds) in (int, float) and seconds > 0 and
                seconds < self.deadline-time.monotonic(),
                'Whole Stadium remaining deadline cannot fit existing input hold')
        # Keep the shared policy's exact press/sleep/release behavior. Its
        # release routes through this checked set_both as well.
        DualPipeController.hold(self, p1, p2, seconds, action=action)


def drive_stadium_cold_css(receiver, menus, next_row, wait_source, tap, *, stock4_none=False):
    """Reuse checked menu inputs; only the explicit stock declaration enters Rules."""
    if receiver.css is not None:
        return
    actions = menus['actions'] if stock4_none else menus['actions'][:2]
    require([a['label'] for a in actions[:2]] == ['main-down-to-VS', 'main-select-VS'],
            'Whole Stadium cold entry lacks the existing Main/VS policy')
    boot_actions = 0
    while receiver.css is None and not matches(receiver.latest_menu, actions[0]['before']):
        require(receiver.menu_polls < menus['boot_max_polls'],
                'Whole Stadium cold startup polling cap')
        next_row()
        scene = (receiver.latest_menu or {}).get('scene')
        for action in menus['boot']:
            if scene == action['scene']:
                boot_actions += 1
                require(boot_actions <= menus['boot_max_actions'],
                        'Whole Stadium cold startup action cap')
                tap(action, 'whole-cold-scene-'+str(scene), 600)
                first = receiver.menu_polls
                wait_source(lambda: receiver.menu_polls > first, 'whole-cold-source-poll', 600)
                break
    if receiver.css is not None:
        return
    for action in actions:
        wait_source(lambda: matches(receiver.latest_menu, action['before']),
                    action['label']+':ready', action['max_polls'])
        tap(action, action['label'], action['max_polls'])
        wait_source(lambda: matches(receiver.latest_menu, action['after']),
                    action['label']+':observed', action['max_polls'])
    if stock4_none:
        # Existing final Rules-start-CSS descriptor already reached the real CSS.
        wait_source(lambda: receiver.css is not None, 'whole-first-CSS', 600)
        return
    # Original MENU_KIND_VS/SEL_VS_MELEE is kind2/row0. The checked
    # second action's after-state includes that exact source owner/cooldown.
    # mnmain.c confirms A here requests GM_VS; no Rules/Items action follows.
    tap(actions[1], 'whole-VS-Melee-to-CSS', actions[1]['max_polls'])
    wait_source(lambda: receiver.css is not None, 'whole-first-CSS', 600)


def drive_stadium_natural_stockout(receiver, next_row, controller, deadline):
    """Reuse the existing ordinary edge/jump sequence, gated by actual source stocks."""
    from reference_versus_sequence_capture import raw_pad
    witness = receiver.stockout_witness
    require(receiver.selected_stockout(1) and witness is not None and
            witness.last_stocks == (4, 4) and witness.last_fighters is not None and
            witness.last_fighters['motions'] == (14, 14) and
            witness.last_fighters['ground_air'] == (0, 0),
            'Natural stockout did not start from the actual grounded four-stock state')
    edge = raw_pad(x=80)
    jump = raw_pad(buttons=['X'], x=80)

    def pump_until(predicate, label, max_rows=1800):
        start = receiver.seq
        while not predicate():
            require(not receiver.ended and receiver.seq - start < max_rows and
                    time.monotonic() < deadline,
                    'Natural stockout source wait/deadline: ' + label)
            next_row()

    for expected_stock in (4, 3, 2, 1):
        require(witness.last_stocks == (expected_stock, 4) and
                witness.last_fighters['motions'] == (14, 14) and
                witness.last_fighters['ground_air'] == (0, 0),
                'Natural stockout life did not begin from a source-observed grounded respawn')
        prior = len(witness.transitions)
        # This is the existing ordinary SequenceRunner recipe, not injected
        # stock state. Every actual source tick and consumed four-port row is
        # still processed by the receiver while the input stream is retained.
        controller.hold(edge, NEUTRAL_PAD, 1.2,
                        action=f'natural-stockout-stock-{expected_stock}-edge')
        controller.hold(jump, NEUTRAL_PAD, 0.65,
                        action=f'natural-stockout-stock-{expected_stock}-jump')
        pump_until(lambda: len(witness.transitions) > prior,
                   f'P1 stock {expected_stock}->{expected_stock - 1}')
        require(len(witness.transitions) == prior + 1 and
                witness.transitions[-1]['from_stocks'] == (expected_stock, 4) and
                witness.transitions[-1]['to_stocks'] == (expected_stock - 1, 4),
                'Natural stockout action skipped or combined actual stock transitions')
        if expected_stock > 1:
            # RebirthWait intentionally holds a neutral player on the platform.
            # Leave it with ordinary input, then observe the original landing;
            # do not spend the bounded source wait on its inactivity timer.
            pump_until(lambda: witness.last_stocks == (expected_stock - 1, 4) and
                       witness.last_fighters['motions'][1] == 14 and
                       (witness.last_fighters['motions'][0] == 13 or
                        witness.last_fighters['motions'] == (14, 14) and
                        witness.last_fighters['ground_air'] == (0, 0)),
                       f'P1 respawn platform at {expected_stock - 1} stocks')
            if witness.last_fighters['motions'][0] == 13:
                requested_after = receiver.seq
                controller.set_both(edge, NEUTRAL_PAD,
                    action=f'natural-stockout-stock-{expected_stock - 1}-leave-platform')
                try:
                    pump_until(lambda: witness.last_tick is not None and
                               witness.last_fighters['motions'][0] != 13,
                               'P1 leave respawn platform', max_rows=150)
                    require(receiver.seq > requested_after and
                            receiver.last_pad[:2] == [edge, NEUTRAL_PAD] and
                            witness.last_stocks == (expected_stock - 1, 4) and
                            witness.last_fighters['motions'] == (29, 14) and
                            witness.last_fighters['ground_air'] == (1, 0),
                            'Natural stockout platform exit lacks consumed stick and source Fall')
                finally:
                    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD,
                        action=f'natural-stockout-stock-{expected_stock - 1}-leave-platform:release')
            pump_until(lambda: witness.last_stocks == (expected_stock - 1, 4) and
                       witness.last_fighters['motions'] == (14, 14) and
                       witness.last_fighters['ground_air'] == (0, 0),
                       f'P1 grounded respawn at {expected_stock - 1} stocks')

    controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action='natural-stockout-zero:neutral')
    pump_until(lambda: receiver.outcome_seen,
               'source-observed zero-stock mode9 and natural elimination Results exit')
    require(witness.mode9 is not None and receiver.stockout_summary is not None,
            'Natural stockout result arrived before a source-observed mode9 row')
    return dict(receiver.stockout_summary)


def drive_stadium_whole_three(receiver, tail, controller, menus, deadline):
    """Existing input policies, three natural completions; no source writes."""
    controller = StadiumDeadlineController(controller, deadline)
    from reference_versus_sequence_capture import raw_pad
    def next_row():
        row = tail.next(deadline)
        receiver.accept(row)
        return row
    def wait_source(predicate, label, max_polls):
        first = receiver.menu_polls
        while not predicate():
            require(not receiver.ended and receiver.menu_polls-first < max_polls and time.monotonic()<deadline,
                    'Whole Stadium source predicate/deadline: '+label)
            next_row()
    def tap(action, label, max_polls):
        before = receiver.menu_consumed
        controller.set_both(action['p1'], action['p2'], action=label)
        wait_source(lambda: receiver.menu_consumed>before and receiver.last_pad[:2]==[action['p1'],action['p2']],label+':consumed',max_polls)
        before = receiver.menu_consumed
        controller.set_both(NEUTRAL_PAD,NEUTRAL_PAD,action=label+':release')
        wait_source(lambda: receiver.menu_consumed>before and receiver.last_pad[:2]==[NEUTRAL_PAD]*2,label+':released',max_polls)
    drive_stadium_cold_css(receiver, menus, next_row, wait_source, tap,
        stock4_none=getattr(receiver,'expected',{}).get('is_stock') is True)
    measurements = []
    for index in range(receiver.match_count):
        drive_authored_css_sss(receiver,menus,controller,next_row,wait_source,tap)
        wait_source(lambda: receiver.setup_seen,'Setup',600)
        while receiver.tick_count < 180:
            next_row()
        stockout_result = None
        if receiver.selected_stockout(index):
            stockout_result = drive_stadium_natural_stockout(receiver, next_row, controller, deadline)
        if receiver.selected_long_idle(index):
            # Continue consuming the same complete MWRO stream. The receiver
            # gates only on its actual GO, contiguous SourceTicks, and first
            # later DrawReturn; no independent row sampler or inferred GO is
            # involved. The original 180-second whole-session deadline remains.
            idle_window = wait_for_stadium_b_long_idle_window(receiver, next_row, deadline)
        else:
            idle_window = None
        requested_after = dict(sequence=receiver.seq-1, source_tick=receiver.previous_tick,
                               minimum_neutral_ticks=receiver.tick_count)
        if stockout_result is None:
            receiver.chord_requested = True
            chord = raw_pad(buttons=['L','R','A','START'])
            tap({'p1':chord,'p2':NEUTRAL_PAD},'whole-pause-request',600)
            wait_source(lambda: receiver.pause_ready_clock is not None,'source-pause-ready',600)
            receiver.exit_requested = True
            controller.set_both(chord, NEUTRAL_PAD, action='whole-no-contest')
            wait_source(lambda: receiver.chord_consumed is not None,'fresh-exit-chord-consumed',600)
            controller.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action='whole-no-contest:release')
            wait_source(lambda: receiver.outcome_seen,'natural-exit-outcome7',600)
        # Use the existing checked exit/Results/Prize protocol intact.
        from reference_versus_sequence_capture import SequenceRunner
        class ObservedResultsTail:
            def next(self, phase_deadline):
                row = tail.next(min(deadline, phase_deadline))
                receiver.accept(row)
                return row
            boundary = staticmethod(ObserverTail.boundary)
            wait_boundary = ObserverTail.wait_boundary
        result_policy = SequenceRunner([], [], ObservedResultsTail(), controller)
        result_policy._boundary('vs_mode_exit', index, deadline)
        result_policy._boundary('results_enter', index, deadline)
        release_wait_start = receiver.menu_polls
        def input_ready():
            require(receiver.menu_polls-release_wait_start < 600,
                    'Whole Stadium exit release consume polling cap')
            if stockout_result is not None:
                receiver.start_allowed = True
                return True
            if receiver.chord_released is None:
                return False
            receiver.start_allowed = True
            return True
        result_policy._confirm_results(index, deadline, input_ready=input_ready)
        require(receiver.returned_css,'Whole Stadium Results lacks checked natural CSS return')
        measurement = (dict(match_index=index, request=requested_after,
            natural_stockout=stockout_result) if stockout_result is not None else
            dict(match_index=index, request=requested_after,
            actual_pause_chord=receiver.pause_consumed, actual_pause_release=receiver.pause_released,
            pause_ready=receiver.pause_ready_clock,
            actual_chord=receiver.chord_consumed, actual_release=receiver.chord_released))
        if idle_window is not None:
            measurement['long_idle_window'] = idle_window
        measurements.append(measurement)
        if index < receiver.match_count - 1:
            receiver.next_lifetime()
    while not receiver.ended:
        next_row()
    remaining = deadline-time.monotonic()
    require(remaining > 0, 'Whole Stadium deadline expired before writer status')
    tail.require_completed_status(timeout=min(5.0, remaining))
    require(time.monotonic() < deadline, 'Whole Stadium deadline expired during writer status')
    return measurements


def _require_stadium_output_disjoint(output, *source_trees):
    """Refuse copy/output overlap before creating any owned destination."""
    resolved=Path(output).resolve()
    for source in source_trees:
        source=Path(source).resolve()
        require(not resolved.is_relative_to(source) and not source.is_relative_to(resolved),
                'Whole Stadium output overlaps a source tree')


def prepare_stadium_setup_gci_folder(checkpoint_gc, initial_gci, destination, setup):
    """Copy the exact card already admitted by validate_setup_receipt; no save decoding."""
    source=Path(initial_gci)
    expected=setup.get('initial_profile_sha256')
    declared={'USA/Card A/'+source.name:expected}
    baseline=setup.get('card_baseline')
    if baseline=='single imported retained Personal progress GCI':
        from sd_gci_profile import load_profile
        require(load_profile(source)['sha256']==expected,
                'Whole Stadium retained Personal source profile differs')
    require(baseline in ('single imported Everything unlocked GCI',
                         'single imported retained Personal progress GCI') and
            isinstance(expected,str) and len(expected)==64 and
            expected.strip('0123456789abcdef')=='' and expected!='0'*64 and
            setup.get('external_save_hashes')==declared,
            'Whole Stadium requires the exact strict-admitted card inventory')
    require(not source.is_symlink() and source.is_file() and
            hashlib.sha256(source.read_bytes()).hexdigest()==expected,
            'Whole Stadium initial GCI differs from the strict-admitted identity')
    card=Path(checkpoint_gc)/'USA/Card A'
    require(not card.is_symlink() and card.is_dir(), 'Whole Stadium source card is not ordinary')
    destination=Path(destination)
    _require_stadium_output_disjoint(destination,card)
    destination.mkdir()  # Fresh owned card root; never reuse/overwrite a destination.
    _copy_tree(card,destination/'USA')  # Existing checked regular-byte copy, no symlink/special files.
    copied=list((destination/'USA').iterdir())
    require(len(copied)==1 and copied[0].name==source.name and copied[0].is_file() and
            not copied[0].is_symlink() and hashlib.sha256(copied[0].read_bytes()).hexdigest()==expected,
            'Whole Stadium owned GCI copy differs from the strict-admitted inventory')
    copied[0].chmod(0o400)
    return copied[0]


def parse_stadium_whole_rng_window(value):
    if value is None:
        return None
    require(isinstance(value,str) and len(value)==3 and value[1]==':' and
            '1'<=value[0]<=value[2]<='9',
            'Whole Stadium RNG window requires FIRST:LAST pair offsets1..9 after actual GO')
    return int(value[0]),int(value[2])


def wait_for_stadium_b_long_idle_window(receiver, next_row, deadline):
    """Read the same contiguous whole3 stream through actual B window close."""
    require(receiver.selected_long_idle(receiver.match_index),
            'Whole Stadium long-idle gate is outside the selected profile window')
    while not receiver.long_idle_window_complete:
        require(not receiver.ended and receiver.seq < STADIUM_WHOLE_LONG_IDLE_RECORD_CAP and
                time.monotonic() < deadline,
                'Whole Stadium B GO+3500/natural DrawReturn gate or deadline was not reached')
        next_row()
    return receiver.long_idle_window_summary()


def validate_stadium_whole_rng_sidecar(path, offsets, go, status, dol_sha256, coverage="rng_returns"):
    # This validates bounded same-run raw capture, not original/native parity.
    from reference_capture_app import validate_cpu_rng_probe
    require(isinstance(go,dict) and type(go.get('source_tick')) is int and
            type(go.get('sequence')) is int, 'Whole Stadium RNG lacks its actual same-run B GO')
    first,last=offsets;tick=go['source_tick']
    raw_first,raw_last=tick+first-1,tick+last+1
    require(0<=raw_first<=raw_last<=0xffffffff and raw_last-raw_first<64,
            'Whole Stadium RNG actual GO raw guard band is out of bounds')
    metadata=dict(window_basis='actual_go_raw_guard_band',requested_first_offset=first,
        requested_last_offset=last,go_tick=tick,go_observer_sequence=go['sequence'],
        first_guard_tick_partial=first==1)
    probe=json.loads(Path(path).read_text())
    close=status.get('cpu_probe_close') if isinstance(status,dict) else None
    require(isinstance(probe,dict) and isinstance(close,dict) and
            all(type(row.get(k)) is type(v) and row.get(k)==v for row in (probe,close) for k,v in metadata.items()),
            'Whole Stadium RNG sidecar/header/status differ from actual GO and requested offsets')
    require(coverage in ('rng_returns','rng_updates'), 'Unknown whole RNG coverage')
    request=dict(site=coverage,match=1,first_tick=raw_first,last_tick=raw_last)
    result=validate_cpu_rng_probe(path,request,'stadium-whole3','primary3-allocation2-feedback',dol_sha256,status)
    result.update(scope='same-run raw guard-band capture only; extras retained; no paired owner parity',
        requested_pair_offsets=[first,last],raw_source_tick_bounds=[raw_first,raw_last],
        actual_go=dict(source_tick=tick,sequence=go['sequence']),
        requires_exact_sequence_projection=True,guard_band_extras_retained=True,C3=False)
    return result


class StadiumReplayIntentObserver:
    """Observe existing policy decisions; this object owns no Pipe or input service.

    Each policy wait still validates newly observed consumed PAD bytes. Requests
    cannot change the prerecorded stream, acknowledge native work, or grant a
    phase transition. A final exact source recipe/seed comparison is mandatory.
    """
    sleep = staticmethod(time.sleep)

    def __init__(self, log):
        self.log = Path(log)
        require(not self.log.exists(), 'Replay policy observation log already exists')
        self.count = 0

    def set_both(self, p1, p2, *, action):
        require(self.count < STADIUM_CAPTURE_CAPS['intent_records'] and
                isinstance(action,str) and 0 < len(action) <= 96 and
                all(32 <= ord(c) <= 126 for c in action), 'Replay policy observation cap')
        pipe_commands(p1); pipe_commands(p2)  # Validate syntax, never dispatch.
        record=(json.dumps(dict(index=self.count,action=action,pads=[p1,p2],
                                input_written=False),sort_keys=True)+'\n').encode()
        require((self.log.stat().st_size if self.log.exists() else 0)+len(record) <=
                STADIUM_CAPTURE_CAPS['intent_bytes'], 'Replay policy byte cap')
        with self.log.open('ab') as stream: stream.write(record)
        self.count += 1


def stadium_whole_replay_preflight(binding_path, *, output, setup_path, expected,
                                    profile_inventory, disc, dol):
    """Admit one complete raw source; never manufacture a finalized bundle.

    Reuse the eight-role raw identity binding, but not the fixed2013 exporter:
    this adapter validates input provenance; browser context admission remains
    a separate unchanged strict comparison after the new capture.
    """
    import stadium_later_b_reference as reference
    import stadium_current_context_packet as context
    from capture_retail_replay import _sha256
    from capture_allocation_history import validate_reference_build_manifest
    raw, _ = reference._read_binding_snapshot(binding_path)
    binding=json.loads(raw)
    require(binding.get('schema')==reference.SCHEMA,
            'Whole replay source must be an original record binding')
    paths=reference.verified_files(binding)
    require(not Path(binding_path).resolve().is_relative_to(Path(output).resolve()),
            'Whole replay binding must be outside fresh output')
    for path in paths.values():
        require(not path.resolve().is_relative_to(Path(output).resolve()),
                'Whole replay source must be outside fresh output')
    source_launch=json.loads(paths['launch'].read_text())
    source_report=json.loads(paths['report'].read_text())
    require(source_report.get('result')=='pass' and not source_report.get('cleanup_errors') and
            source_report.get('scope')=='original-stadium-whole3-feedback-allocation2' and
            source_launch.get('input_mode','record')=='record',
            'Whole replay requires a completed original raw recording')
    for role, launch_key in [('setup_receipt','setup'),('producer_manifest','manifest')]:
        require(source_launch[launch_key]['sha256']==binding['files'][role]['sha256'] and
                Path(source_launch[launch_key]['path'])==paths[role],
                'Whole replay source producer/setup role differs')
    validate_reference_build_manifest(paths['producer_manifest'],Path(source_launch['command'][0]))
    summary=validate_input_stream(paths['si'])
    require(summary['bytes']<=STADIUM_CAPTURE_CAPS['input_bytes'] and
            source_report['input_stream']==summary and
            source_report['native']['primary_sha256']==binding['files']['primary']['sha256'],
            'Whole replay source report/raw identity differs')
    validate_status(paths['si_status'],mode='record',events=summary['events'],require_complete=True)
    capture=context.replay.capture_from_path(paths['primary'],paths['primary_status'])
    require(capture['declared_setups']==[expected]*3 and
            source_launch['expected']==expected and source_launch['profile_inventory']==profile_inventory,
            'Whole replay source declared Setup/profile differs')
    prior=json.loads(paths['setup_receipt'].read_text());current=json.loads(Path(setup_path).read_text())
    # Native observer build changes are explicit; all game/profile/initial-save
    # identities are identical. Reconciliation prose is retained, not authority.
    for key in ('schema','version','disc','dol','external_save_hashes','initial_card',
                'reference_run_configuration','retail_provenance','template_source','source_profile_export'):
        require(key in prior and prior[key]==current.get(key),
                'Whole replay current game/setup identity differs: '+key)
    require(prior['disc']['sha256']==_sha256(Path(disc)) and
            prior['dol']['sha256']==_sha256(Path(dol)) and
            source_launch['environment_delta']['MWRC_DOL_SHA256']==prior['dol']['sha256'],
            'Whole replay actual disc/DOL identity differs')
    reference.verified_files(binding)
    require(reference._read_binding_snapshot(binding_path)[0]==raw,
            'Whole replay binding changed during admission')
    return dict(binding_path=str(Path(binding_path).resolve()),binding_sha256=hashlib.sha256(raw).hexdigest(),
                source=str(paths['si']),summary=summary,source_files=binding['files'],
                source_profile_inventory=profile_inventory,
                scope='complete raw input replay only; no finalized bundle or browser authority')


def stadium_whole_replay_complete(source, native_status, primary, primary_status, expected):
    """Require actual replay completion and same newly consumed source workload."""
    import stadium_later_b_reference as reference
    import stadium_current_context_packet as context
    raw,_=reference._read_binding_snapshot(source['binding_path'])
    require(hashlib.sha256(raw).hexdigest()==source['binding_sha256'], 'Whole replay binding changed')
    binding=json.loads(raw);paths=reference.verified_files(binding)
    require(binding['files']==source['source_files'] and str(paths['si'])==source['source'],
            'Whole replay source roles changed')
    summary=validate_input_stream(paths['si'])
    require(summary==source['summary'], 'Whole replay input changed')
    validate_status(native_status,mode='replay',events=summary['events'],require_complete=True)
    old=context.replay.capture_from_path(paths['primary'],paths['primary_status'])
    new=context.replay.capture_from_path(primary,primary_status)
    require(context.replay.encode_v8(old)[0]==context.replay.encode_v8(new)[0],
            'Whole replay actual consumed recipe differs from recorded source')
    old_packet=context.packet_from_records(list(context.replay.iter_records(paths['primary'])),expected)
    new_packet=context.packet_from_records(list(context.replay.iter_records(primary)),expected)
    for previous,current in zip(old_packet['matches'],new_packet['matches']):
        for phase in ('entry','setup'):
            require(previous[phase]['rng']['value_hex']==current[phase]['rng']['value_hex'],
                    'Whole replay actual Entry/Setup seed differs')
    reference.verified_files(binding)
    return summary


def run_stadium_whole_three(*, dolphin, disc, profile, menu_recipe, output, build_manifest,
                             target_path, setup_path, checkpoint_gc, dol, provenance, gci, timeout,
                             stadium_whole_b_state=False, stadium_whole_long_idle=False,
                             stadium_whole_paired_idle=False, stadium_whole_stockout=False, stadium_whole_rng_window=None,
                             stadium_whole_replay_binding=None,
                             stadium_whole_rng_coverage="rng_returns"):
    """Explicit private whole3/sidecar2 diagnostic using the existing passive route."""
    import types
    require(type(stadium_whole_b_state) is bool, "Whole Stadium B observation opt-in must be boolean")
    require(type(stadium_whole_long_idle) is bool and
            (not stadium_whole_long_idle or stadium_whole_b_state),
            'Whole Stadium long-idle profile requires the B-state observation')
    require(type(stadium_whole_paired_idle) is bool,
            'Whole Stadium paired-idle profile option must be boolean')
    require(type(stadium_whole_stockout) is bool and
            (not stadium_whole_stockout or stadium_whole_b_state and
             not stadium_whole_long_idle and not stadium_whole_paired_idle and
             stadium_whole_rng_window is None and stadium_whole_replay_binding is None),
            'Whole Stadium natural stockout profile is exclusive and requires B-state')
    if stadium_whole_paired_idle:
        require(stadium_whole_rng_window is None and stadium_whole_replay_binding is None,
                'Whole Stadium paired-idle profile excludes RNG sidecar/replay modes')
        stadium_whole_b_state = True
        stadium_whole_long_idle = True
    rng_window=parse_stadium_whole_rng_window(stadium_whole_rng_window)
    require(stadium_whole_rng_coverage in ("rng_returns","rng_updates") and
            (rng_window is not None or stadium_whole_rng_coverage == "rng_returns"),
            "Whole Stadium RNG coverage requires its explicit bounded window")
    require(rng_window is None or stadium_whole_b_state,
            'Whole Stadium RNG window requires the selected match1 B-state observer')
    require(not stadium_whole_long_idle or
            (rng_window is None and stadium_whole_replay_binding is None),
            'Whole Stadium long-idle profile initially excludes short RNG and replay adjuncts')
    from capture_allocation_history import (validate_setup_receipt, stadium_three_target,
                                            validate_stadium_three_native,
                                            validate_stadium_paired_idle_native,
                                            validate_stadium_paired_allocation_environment)
    from retail_allocation_profile import build_profile
    from reference_observer_stream import iter_records
    deadline = time.monotonic()+180
    require(timeout == 180 and all(x is not None for x in
        [target_path,setup_path,checkpoint_gc,dol,provenance,gci]),
        'Whole Stadium requires exact180s and complete owned input tuple')
    require(not any(k.startswith('MWRC_') for k in os.environ),
            'Whole Stadium refuses inherited native observer settings')
    output=Path(output)
    _require_stadium_output_disjoint(output,checkpoint_gc,profile)
    output.mkdir()
    process=bounded_log=log=None
    report=dict(scope=('original-stadium-whole3-natural-stockout-v1' if stadium_whole_stockout else
                       'original-stadium-paired-idle-scene-endpoints-v1' if stadium_whole_paired_idle else
                       'original-stadium-whole3-feedback-allocation2'),result='fail',
                endpoint_scope=('selected B: actual P1 four-stock losses, mode9 before elimination exit; no standings/lifecycle parity' if stadium_whole_stockout else
                                'firsttwo short natural lifetimes only; no C3/per-owner ledger'),
                input_contract=('exact stock4_none two-human setup; ordinary P1 edge/jump and P2 neutral consumed rows; no stock writes' if stadium_whole_stockout else
                                'minimum180 observed neutral ticks before ordinary chord request; actual consume ticks retained'))
    if stadium_whole_paired_idle:
        report.update(endpoint_scope='two natural lifetimes; GO+3500 typed/four-port windows for matches0/1; only exact scene RNG endpoints; no full allocation/registration claim',
                      input_contract='actual four-port neutral consumes retained for both actual GO-derived windows; natural pause/exit/Results/CSS order preserved')
    elif stadium_whole_long_idle:
        report.update(endpoint_scope='three natural lifetimes; selected B GO+3500 and first later natural DrawReturn; no C3/per-owner ledger',
                      input_contract='actual four-port neutral consumes retained; existing pause/exit route follows the selected B natural batch close')
    try:
        build=validate_reference_build_manifest(Path(build_manifest),Path(dolphin))
        actual_manifest=json.loads(Path(build_manifest).read_text())
        overlay=ROOT/'reference-capture/dolphin/source/Core/PowerPC/ReferenceCaptureObserver.cpp'
        require(actual_manifest.get('observer_source_overlay_sha256',{}).get('Core/PowerPC/ReferenceCaptureObserver.cpp') == hashlib.sha256(overlay.read_bytes()).hexdigest(),
                'Whole Stadium linked observer producer is stale')
        inputs=types.SimpleNamespace(disc=Path(disc),dol=Path(dol),dolphin=Path(dolphin),
            template_user=Path(profile),checkpoint_gc=Path(checkpoint_gc),initial_gci=Path(gci),
            provenance=Path(provenance))
        setup=validate_setup_receipt(Path(setup_path),inputs,build)
        expected=stadium_three_target(Path(target_path))['source_expected_setup']
        if stadium_whole_stockout:
            from capture_allocation_history import stadium_three_rule_profile
            require(stadium_three_rule_profile(expected) == 'stock4_none' and
                    len(expected.get('players', [])) == 2,
                    'Natural stockout requires the exact admitted two-human stock4_none setup')
        menus,_=menu_actions(menu_recipe)
        require(menus['scope']=='stadium_go_prefix' and menus['version']==11 and
                menus['sss'].get('policy')=='same_call_feedback_v1',
                'Whole Stadium must reuse the existing reviewed same-call input policy')
        # The historical policy document is input-geometry provenance only.
        # Actual source Setup/receipt are admitted separately, never relabeled GO-prefix.
        menus=json.loads(json.dumps(menus))
        menus['css']['costumes']=[p['costume'] for p in expected['players']]
        user=output/'user'
        p1,p2,profile_inventory=prepare_rules_profile(profile,user)
        owned_gci=prepare_stadium_setup_gci_folder(checkpoint_gc,gci,output/'gci-folder',setup)
        allocation_profile=output/'allocation-profile.json'
        allocation_profile.write_bytes(canonical(build_profile(Path(dol),ROOT/'.deps/melee/config/GALE01/symbols.txt')))
        raw,status=output/'native-primary.mwro',output/'native-primary-status.json'
        native,native_status=output/'native-inputs.mwri',output/'native-input-status.json'
        env={k:v for k,v in os.environ.items() if not k.startswith(('MWRC_','DOLPHIN_','SDL_'))}
        env.update(MWRC_ENABLE='1',MWRC_CPU='JITARM64',MWRC_SOURCE_REV='GALE01r2',
            MWRC_DOL_SHA256=hashlib.sha256(Path(dol).read_bytes()).hexdigest(),
            MWRC_OUTPUT=str(raw),MWRC_STATUS=str(status),MWRC_INPUT_RECORD=str(native),
            MWRC_INPUT_STATUS=str(native_status),
            MWRC_WHOLE_SESSION_MATCHES='2' if stadium_whole_paired_idle else '3',
            MWRC_CAPTURE_ID='stadium-whole-paired-idle' if stadium_whole_paired_idle else 'stadium-whole3',
            MWRC_SEQUENCE_ID='primary2-paired-idle-scene-endpoints' if stadium_whole_paired_idle else 'primary3-allocation2-feedback',
            MWRC_ALLOCATION_OUTPUT=str(output/'native-allocations.jsonl'),
            MWRC_ALLOCATION_VS_TARGET='2',MWRC_ALLOCATION_STOP_AT='exit',
            MWRC_STADIUM_WHOLE_FEEDBACK='1',MWRC_STADIUM_WHOLE_SETUP_RECEIPT_SHA256=setup['sha256'])
        if stadium_whole_b_state:
            env['MWRC_STADIUM_WHOLE_B_STATE']='1'
        long_idle_compact_launch = None
        if stadium_whole_paired_idle:
            env['MWRC_STADIUM_WHOLE_PAIRED_IDLE']=STADIUM_WHOLE_PAIRED_IDLE_PROFILE['profile']
            env['MWRC_STADIUM_WHOLE_LONG_IDLE']=STADIUM_WHOLE_PAIRED_IDLE_PROFILE['profile']
            env['MWRC_ALLOCATION_SCENE_ENDPOINT_PROFILE']=STADIUM_WHOLE_PAIRED_IDLE_PROFILE['profile']
            compact_env, long_idle_compact_launch = build_stadium_whole_long_idle_compact_launch(output, paired_idle=True)
            env.update(compact_env)
        elif stadium_whole_long_idle:
            env['MWRC_STADIUM_WHOLE_LONG_IDLE']=STADIUM_WHOLE_LONG_IDLE_PROFILE['profile']
            compact_env, long_idle_compact_launch = build_stadium_whole_long_idle_compact_launch(output)
            env.update(compact_env)
        if rng_window is not None:
            env.update(MWRC_STADIUM_WHOLE_RNG_RETURNS='1',
                MWRC_CPU_PROBE_OUTPUT=str(output/'rng-returns.json'),MWRC_CPU_PROBE_MATCH='1',
                MWRC_CPU_PROBE_FIRST_TICK=str(rng_window[0]),MWRC_CPU_PROBE_LAST_TICK=str(rng_window[1]),
                MWRC_CPU_PROBE_RNG_RETURN_SITE=stadium_whole_rng_coverage)
        validate_stadium_paired_allocation_environment(env)
        command=rules_dolphin_command(dolphin,user,disc)+['-C','Dolphin.Core.SlotA=8',
            '-C','Dolphin.Core.GCIFolderAPath='+str(output/'gci-folder')]
        receiver=StadiumWholeFeedbackReceiver(menus,setup['sha256'],expected,
            stadium_whole_b_state=stadium_whole_b_state,
            stadium_whole_long_idle=stadium_whole_long_idle,
            stadium_whole_paired_idle=stadium_whole_paired_idle,
            stadium_whole_stockout=stadium_whole_stockout)
        replay_source = None
        if stadium_whole_replay_binding is not None:
            require(rng_window is not None and stadium_whole_b_state and not stadium_whole_long_idle,
                    'Whole Stadium replay requires B state and combined RNG observation')
            replay_source = stadium_whole_replay_preflight(stadium_whole_replay_binding,
                output=output, setup_path=setup_path, expected=expected,
                profile_inventory=profile_inventory, disc=disc, dol=dol)
            env.pop('MWRC_INPUT_RECORD')
            env['MWRC_INPUT_REPLAY']=replay_source['source']
            env['MWRC_STADIUM_WHOLE_REPLAY_BINDING']=replay_source['binding_sha256']
            receiver.replay_binding_sha256=replay_source['binding_sha256']
        controller=(StadiumReplayIntentObserver(output/'replay-policy-intentions.jsonl') if replay_source else
                    StadiumIntentController(p1,p2,output/'input-intentions.jsonl'))
        launch=dict(command=command,environment_delta={k:v for k,v in env.items() if k.startswith('MWRC_')},manifest=build,setup=setup,expected=expected,
                    profile_inventory=profile_inventory,owned_gci=dict(path=str(owned_gci),bytes=owned_gci.stat().st_size,sha256=hashlib.sha256(owned_gci.read_bytes()).hexdigest()),operation_seconds=180,owner_seconds=210,cleanup_seconds=30,
                    stadium_whole_stockout=dict(STADIUM_WHOLE_STOCKOUT_PROFILE) if stadium_whole_stockout else None)
        if stadium_whole_paired_idle:
            launch['stadium_whole_paired_idle'] = dict(STADIUM_WHOLE_PAIRED_IDLE_PROFILE)
            launch['stadium_whole_long_idle_compact_launch'] = dict(long_idle_compact_launch)
            launch['caps'] = dict(observer_bytes=STADIUM_CAPTURE_CAPS['observer_bytes'],
                                  observer_records=STADIUM_WHOLE_LONG_IDLE_RECORD_CAP,
                                  input_bytes=STADIUM_CAPTURE_CAPS['input_bytes'])
        elif stadium_whole_long_idle:
            launch['stadium_whole_long_idle'] = dict(STADIUM_WHOLE_LONG_IDLE_PROFILE)
            launch['stadium_whole_long_idle_compact_launch'] = dict(long_idle_compact_launch)
            launch['caps'] = dict(observer_bytes=STADIUM_CAPTURE_CAPS['observer_bytes'],
                                  observer_records=STADIUM_WHOLE_LONG_IDLE_RECORD_CAP,
                                  input_bytes=STADIUM_CAPTURE_CAPS['input_bytes'])
        if replay_source is not None:
            launch.update(input_mode='replay', input_replay=replay_source)
        (output/'launch.json').write_bytes(canonical(launch))
        require(time.monotonic()<deadline,'Whole Stadium operation expired before native launch')
        log=(output/'dolphin.log').open('xb')
        process=subprocess.Popen(command,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
        bounded_log=BoundedLog(process.stdout,log,STADIUM_CAPTURE_CAPS['log_bytes'])
        def wait_check():
            check_owned_native_wait(process,bounded_log)
            check_stadium_capture_bounds(raw,native,deadline,receiver.seq,
                                         whole_long_idle=stadium_whole_long_idle)
        with ObserverTail(raw,status,wait_check=wait_check) as tail:
            report['actual_inputs']=drive_stadium_whole_three(receiver,tail,controller,menus,deadline)
        primary_status, _ = wait_transform_terminal_statuses(status,native_status,deadline,wait_check=wait_check,
            input_mode="replay" if replay_source is not None else "record")
        if stadium_whole_b_state and not stadium_whole_paired_idle:
            require(receiver.b_observation is not None, 'Whole Stadium B observation incomplete')
            report['stadium_b_observation']=receiver.b_observation
        if stadium_whole_stockout:
            require(receiver.stockout_summary is not None,
                    'Whole Stadium natural stockout profile lacks the source-authored zero-stock/mode9 boundary')
            report['stadium_whole_stockout'] = receiver.stockout_summary
        if stadium_whole_paired_idle:
            driver_windows = list(receiver.long_idle_windows)
            source_windows, compact_summary = validate_stadium_paired_idle_outputs(
                raw, status, output, long_idle_compact_launch, driver_windows,
                env['MWRC_DOL_SHA256'])
            report['stadium_whole_paired_idle_windows'] = driver_windows
            report['stadium_whole_paired_idle_source_validation'] = source_windows
            report['stadium_paired_idle_compact_rng'] = compact_summary
            report['stadium_b_observations'] = receiver.b_observations
        elif stadium_whole_long_idle:
            driver_window = receiver.long_idle_window_summary()
            source_window, compact_summary = validate_stadium_whole_long_idle_outputs(
                raw, status, output, long_idle_compact_launch, driver_window,
                env['MWRC_DOL_SHA256'])
            report['stadium_whole_long_idle_window'] = driver_window
            report['stadium_whole_long_idle_source_validation'] = source_window
            report['stadium_whole_long_idle_compact_rng'] = compact_summary
        if replay_source is None:
            report['input_stream']=validate_input_stream(native)
        else:
            report['input_stream']=stadium_whole_replay_complete(replay_source,native_status,
                raw,status,expected)
            report['input_replay']=replay_source
            report['input_replay_status']=validate_status(native_status,mode='replay',
                events=replay_source['summary']['events'],require_complete=True)
        if stadium_whole_paired_idle:
            report['native']=validate_stadium_paired_idle_native(output,allocation_profile,expected)
        else:
            report['native']=validate_stadium_three_native(output,allocation_profile,expected)
        if rng_window is not None:
            report['rng_returns']=validate_stadium_whole_rng_sidecar(output/'rng-returns.json',rng_window,
                receiver.b_observation['go'],read_status(status),env['MWRC_DOL_SHA256'],
                coverage=stadium_whole_rng_coverage)
        require(time.monotonic()<deadline,'Whole Stadium operation expired during final validation')
        report['result']='pass'
        cleanup_process(process,output,scope=report['scope'])
        process=None
        bounded_log.finish()
        bounded_log=None
    except BaseException as error:
        report['error']=repr(error)
        report['result']='fail'
        raise
    finally:
        primary_error=sys.exc_info()[1]
        errors=[]
        if process is not None:
            try:cleanup_process(process,output,scope=report['scope'])
            except BaseException as error:errors.append(repr(error))
        if bounded_log is not None:
            try:bounded_log.finish()
            except BaseException as error:errors.append(repr(error))
        if log is not None:
            try:log.close()
            except BaseException as error:errors.append(repr(error))
        report['cleanup_errors']=errors
        if errors:report['result']='fail'
        (output/'report.json').write_bytes(canonical(report))
        if errors and primary_error is None:raise SdDiagnosticError('Whole Stadium cleanup failed: '+str(errors))
    return report



def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("dolphin", "disc", "profile", "input-plan", "menu-recipe", "output", "build-manifest"):
        parser.add_argument("--" + name, type=Path, required=name != "input-plan")
    parser.add_argument("--gci", type=Path, help="Exact retained re-export; required by GCI campaign, optional for Stadium GO prefix")
    parser.add_argument("--ordinary-policy",type=Path,
                        help="Exact separate adaptive competitive timeout policy; v4 remains entry provenance")
    parser.add_argument("--input-replay", type=Path,
                        help="Pinned complete v6 MWRI; Stadium-only, no adaptive input writes")
    parser.add_argument("--stadium-constructor-owner", action="store_true",
                        help="Observe map2 constructor within exact recorded Stadium replay; F terminal unchanged")
    parser.add_argument("--stadium-idle-3500", action="store_true",
                        help="Fresh original recording through3500 relative post-GO ticks and natural batch close")
    parser.add_argument("--stadium-go-ordinal2", action="store_true",
                        help="Fresh original recording through GO+2 and its first subsequent natural DrawReturn")
    parser.add_argument("--stop-after-items", action="store_true",
                        help="Stop the same original transform menu route at observed Items readiness; no setup/transform claim")
    parser.add_argument("--timeout", type=float, default=180)
    for name in ('stadium-whole-target','stadium-whole-setup','stadium-whole-gc',
                 'stadium-whole-dol','stadium-whole-provenance'):
        parser.add_argument('--'+name,type=Path,
                            help='Explicit whole3/allocation2 passive source-route input identity')
    parser.add_argument("--stadium-whole-b-state", action="store_true",
                        help="Whole3-only selected match1 constructor/actual-GO/state adjunct; terminal unchanged")
    parser.add_argument("--stadium-whole-long-idle", action="store_true",
                        help="Whole3 selected B: gate its existing pause on actual GO+3500 ticks and first later natural DrawReturn")
    parser.add_argument("--stadium-whole-paired-idle", action="store_true",
                        help="Two natural Stadium lifetimes with actual GO+3500 windows for both and scene RNG endpoints only")
    parser.add_argument("--stadium-whole-stockout", action="store_true",
                        help="Strict selected-B natural P1 zero-stock/mode9 witness on the exact two-human stock4_none route")
    parser.add_argument('--stadium-whole-replay-binding',type=Path,
                        help='Bound complete raw whole3 MWRI diagnostic replay; requires B-state+combined RNG')
    parser.add_argument("--stadium-whole-rng-coverage",choices=("rng_returns","rng_updates"),
                        default="rng_returns",help="Explicit v4 two-site or v5 three-site LCG update coverage")
    parser.add_argument("--stadium-whole-rng-window",
                        help="Whole3+B-state combined RNG adjunct;1–9 post-GO pair offsets, raw guard-band extras retained")
    args = parser.parse_args(argv)
    try:
        comparator = None
        if args.input_replay is not None:
            from stadium_recorded_prefix_compare import compare_stadium_common_wire
            comparator = compare_stadium_common_wire
        report = run(**vars(args), common_wire_comparator=comparator)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Original menu/SD diagnostic failed: {error}\n")
    print(json.dumps(report, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
