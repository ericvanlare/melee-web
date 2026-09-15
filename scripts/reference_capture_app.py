#!/usr/bin/env python3
"""Private macOS capture supervisor. JSONL is the native window's control API."""
from __future__ import annotations

import argparse
import configparser
from datetime import datetime, timezone
import fcntl
from itertools import chain
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "reference-capture/dolphin"))
from reference_capture_environment import (EnvironmentError, read_settings, verify_environment,
    configured_controller, physical_devices, support_root, file_inventory, sha256)
from reference_capture_semantics import SemanticSession
from reference_input_stream import validate_stream, validate_status as validate_input_status
from reference_dolphin_replay import load_source, verify_replay_environment, snapshot_profile
from reference_session_bundle import ReferenceSessionBundle, ReferenceCaptureInbox


def write_json(path, value):
    temporary = path.with_name(path.name + ".tmp")
    try:
        temporary.unlink()
    except FileNotFoundError:
        pass
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
        json.dump(value, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def prepare_user(root, identifier, profile, fixture_gc):
    """Only ordinary configuration is copied; disc/save bytes stay in place."""
    parent = root / "Sessions"
    parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    user = parent / identifier
    user.mkdir(mode=0o700)
    config = user / "Config"
    if isinstance(profile, dict):
        config.mkdir()
        for name, data in profile.items():
            target = config / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    else:
        shutil.copytree(profile, config, symlinks=False)
    (user / "GC").mkdir()
    # The observer build disables backing-store writes; this is a read-only
    # reference to the owned fixture, not a second copy of the memory card.
    (user / "GC/SRAM.raw").symlink_to(fixture_gc / "SRAM.raw")
    for path in config.rglob("*"):
        if path.is_file(): path.chmod(0o400)
    config.chmod(0o500)
    return user


def dolphin_command(settings, user):
    paths = settings["paths"]
    command = [paths["dolphin"], "-u", str(user), "-b", "-e", paths["disc"]]
    options = {
        # The supervisor owns stop/finalization; a modal confirmation can hold
        # the process open until its timeout escalates to a forced termination.
        "Dolphin.Interface.ConfirmStop": "False",
        "Dolphin.Input.BackgroundInput": "True",
        "Dolphin.Display.Fullscreen": "False",
        "Dolphin.Core.CPUCore": "4", "Dolphin.Core.CPUThread": "False",
        "Dolphin.Core.EnableCheats": "False", "Dolphin.Core.EnableCustomRTC": "True",
        "Dolphin.Core.CustomRTCValue": "1704067200", "Dolphin.Core.EmulationSpeed": "1",
        "Session.Core.SaveDataWritable": "False",
        "Dolphin.Core.GCIFolderAPathOverride": str(Path(paths["fixture_gc"]) / "USA/Card A"),
    }
    for key, value in options.items(): command.extend(("-C", key + "=" + value))
    return command


def validate_observer_status(status, previous=None):
    """Reject impossible status counters before displaying or accepting them."""
    if not isinstance(status, dict):
        raise EnvironmentError("Observer status is not an object")
    event_count = status.get("event_count")
    last_seq = status.get("last_seq")
    if (isinstance(event_count, bool) or not isinstance(event_count, int) or event_count < 0 or
            isinstance(last_seq, bool) or not isinstance(last_seq, int) or last_seq < -1):
        raise EnvironmentError("Observer status counters are invalid")
    if last_seq != event_count - 1:
        raise EnvironmentError("Observer status event count does not match its last sequence")
    if previous is not None:
        if event_count < previous["event_count"] or last_seq < previous["last_seq"]:
            raise EnvironmentError("Observer status counters moved backwards")
    return status


def validate_observer_handshake(records, identity):
    """Bind the first decoded observer record to the verified environment."""
    try:
        first = next(iter(records))
    except StopIteration:
        first = None
    if not isinstance(first, dict) or first.get("event") != "handshake":
        raise EnvironmentError("Observer stream is missing its handshake")
    if first.get("seq") != 0:
        raise EnvironmentError("Observer stream must start at sequence 0")
    payload = first.get("payload")
    if not isinstance(payload, dict):
        raise EnvironmentError("Observer handshake payload is invalid")
    disc = identity.get("disc", {})
    dolphin = identity.get("dolphin", {})
    expected = {
        "schema": "melee-web-passive-dolphin-observer",
        "version": 1,
        "dolphin_commit": dolphin.get("source_revision"),
        "dol_sha1": disc.get("dol_sha1"),
        "dol_sha256": disc.get("dol_sha256"),
        "cpu": "JITARM64",
        "writes_guest_memory": False,
    }
    for field, value in expected.items():
        if value is None or payload.get(field) != value:
            raise EnvironmentError(f"Observer handshake identity mismatch: {field}")
    ring_capacity = payload.get("ring_capacity")
    if isinstance(ring_capacity, bool) or not isinstance(ring_capacity, int) or ring_capacity <= 0:
        raise EnvironmentError("Observer handshake ring capacity is invalid")
    return payload


def isolated_dolphin_environment():
    # Controller discovery and Dolphin must see the same profile-owned SDL
    # hints, without an inherited mapping or device-filter override.
    return {key: value for key, value in os.environ.items()
            if not key.startswith(("SDL_", "MWRC_")) and key != "DOLPHIN_EMU_USERPATH"}


class CaptureCancelled(Exception):
    """Cancellation before Dolphin has consumed any game input."""


class Supervisor:
    def __init__(self, settings_path, *, root=None, emit=None, automated=False):
        self.root = root or support_root()
        self.root.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.settings_path = settings_path
        self.automated = automated
        self.replay_source = None
        self.emit_callback = emit or (lambda row: print(json.dumps(row), flush=True))
        self.output_lock = threading.Lock()
        self.state_lock = threading.Lock()
        self.worker = None
        self.process = None
        self.stop_requested = threading.Event()
        self.identity = None
        self.settings = None
        self.status = {"type": "status", "state": "stopped", "accepted_disc": None,
            "physical_controller": "unknown", "dolphin": "stopped", "capture": "idle",
            "source_frames": 0, "events": 0, "message": "Checking the capture environment",
            "bundle_path": None, "capture_destination": str(self.root / "Captures"),
            "partial_captures": []}
        self.lock_stream = (self.root / "application.lock").open("a")
        try:
            fcntl.flock(self.lock_stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise EnvironmentError("The capture application is already running") from error
        self._refresh_partial_discovery()

    def discover_partial_captures(self):
        """Return inspectable active partials left by an earlier run."""
        return ReferenceCaptureInbox(self.root / "Captures").list_runs(state="activepartials")

    def _refresh_partial_discovery(self):
        self.status["partial_captures"] = self.discover_partial_captures()

    def _capture_prerequisites_verified(self):
        if self.status["state"] == "ready":
            return True
        # Automation is allowed to replace only the physical-controller gate
        # with its owned port-1 pipe. Disc, profile, fixture, timing, and
        # binary checks have already completed in verify().
        return (self.automated and self.status["state"] == "controller_required" and
                isinstance(self.identity, dict) and
                isinstance(self.identity.get("controller"), dict) and
                self.identity["controller"].get("state") == "unavailable")

    def _check_controller_connection(self):
        if self.automated or self.replay_source is not None:
            return
        selected = (self.identity or {}).get("controller", {}).get("selected")
        devices = physical_devices()
        if selected and selected.get("source") == "SDL":
            # Do not open a second SDL client while Dolphin owns the device.
            # The readiness probe already bound the SDL qualifier to these
            # physical USB/HID IDs; monitor their presence without polling PAD.
            hardware = selected["hardware"]
            connected = any(all(device.get(key) == hardware.get(key)
                                for key in ("vendor_id", "product_id", "transport"))
                            for device in devices)
            state = "connected" if connected else "unavailable"
        else:
            state = configured_controller(self.settings, devices)["state"]
        self.publish(physical_controller=state)
        if state != "connected":
            raise EnvironmentError("Physical controller disconnected during capture")

    def publish(self, **fields):
        with self.output_lock:
            self.status.update(fields)
            self.emit_callback(dict(self.status))

    def verify(self):
        self._refresh_partial_discovery()
        self.publish(state="verifying", accepted_disc=None, message="Verifying the capture environment")
        self.settings = read_settings(self.settings_path)
        if (self.replay_source is not None and
                self.replay_source["header"].get("tooling_sha256") != self.tooling_identity()):
            raise EnvironmentError("This recording requires the capture build that recorded it. "
                                   "Replay it with the matching preserved tools; the recording is unchanged.")
        self.identity = verify_environment(self.settings, self.root,
            devices=[] if self.replay_source else None,
            progress=lambda state, message: self.publish(state=state, message=message))
        controller = self.identity["controller"]
        if self.settings.get("input_recording_version") != 1:
            raise EnvironmentError("The installed Dolphin lacks replay recording. Update the reference environment before capturing.")
        if self.replay_source:
            verify_replay_environment(self.replay_source, self.identity)
        ready = self.replay_source is not None or controller["state"] == "connected"
        device_name = controller.get("configured", {}).get("device", "controller")
        self.publish(state="ready" if ready else "controller_required", accepted_disc=True,
                     physical_controller="replay" if self.replay_source else controller["state"], dolphin="stopped", capture="idle",
                     message=f"Ready: {device_name}. Start Capture boots ordinary retail Melee." if ready else
                     f"Configured: {device_name}. Connect it, or choose Configure Controller to select another device.")

    def _terminate_owned(self):
        process = self.process
        if process is not None and process.poll() is None:
            os.killpg(process.pid, signal.SIGTERM)
            try: process.wait(timeout=8)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=5)
            return True
        return False

    def capture(self):
        from reference_observer_stream import iter_records, read_status
        self.verify()
        if self.stop_requested.is_set():
            self.publish(state="stopped", dolphin="stopped", capture="idle",
                         message="Capture cancelled during environment verification")
            return
        if not self._capture_prerequisites_verified():
            return
        settings_hash = sha256(self.settings_path)
        tooling_before = self.tooling_identity()
        identifier = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + uuid.uuid4().hex[:12]
        input_source = ("dolphin_input_replay" if self.replay_source else
                        "automated_human_pipe" if self.automated else "physical_controller")
        replay_binding = self.replay_source["manifest_sha256"] if self.replay_source else None
        bundle = ReferenceSessionBundle.begin(self.root / "Captures", identifier, "GALE01r2",
            uuid.uuid4().hex, metadata={"environment": self.identity,
                                      "input_source": input_source,
                                      "replay_source_manifest_sha256": replay_binding,
                                      "private_settings_sha256": settings_hash,
                                      "tooling_sha256": tooling_before}, sequence_start=0)
        log = None
        user = None
        try:
            self.publish(state="starting", dolphin="starting", capture="armed", source_frames=0,
                         events=0, bundle_path=str(bundle.path), message="Capture armed. Starting retail Melee.")
            user = prepare_user(self.root, identifier, self.replay_source["profile"] if self.replay_source else Path(self.settings["paths"]["profile"]),
                                Path(self.settings["paths"]["fixture_gc"]))
            if self.automated and not self.replay_source:
                from reference_capture_automation import prepare_pipe
                prepare_pipe(user)
            config_before = file_inventory(user / "Config")
            write_json(bundle.path / "configuration-snapshot.json", snapshot_profile(user / "Config"))
            write_json(bundle.path / "configuration.json", {
                "profile_files": config_before, "launch_command": dolphin_command(self.settings, user),
                "input_source": input_source,
                "save_backing_writes": False})
            write_json(bundle.path / "environment.json", self.identity)
            raw = bundle.path / "observer.bin"
            status_path = bundle.path / "observer-status.json"
            environment = dict(isolated_dolphin_environment(), MWRC_ENABLE="1", MWRC_CPU="JITARM64",
                               MWRC_SOURCE_REV="GALE01r2", MWRC_OUTPUT=str(raw), MWRC_STATUS=str(status_path),
                               MWRC_DOL_SHA256=self.identity["disc"]["dol_sha256"],
                               MWRC_OBSERVER_ID=self.settings["observer_identity"],
                               LANG="en_US.UTF-8", LC_ALL="en_US.UTF-8")
            if self.replay_source:
                environment["MWRC_INPUT_REPLAY"] = str(self.replay_source["input_path"])
                write_json(bundle.path / "input-source.json", {
                    "manifest_sha256": replay_binding, "stream": self.replay_source["input"]})
            else:
                environment["MWRC_INPUT_RECORD"] = str(bundle.path / "inputs.mwri")
            environment["MWRC_INPUT_STATUS"] = str(bundle.path / "input-status.json")
            # No inherited debugger, observer or user-path override may redirect
            # this explicit invocation. Only this process group is ever stopped.
            environment.pop("DOLPHIN_EMU_USERPATH", None)
            if self.stop_requested.is_set():
                raise CaptureCancelled("Capture cancelled before Dolphin started")
            log = (bundle.path / "dolphin.log").open("xb")
            self.process = subprocess.Popen(dolphin_command(self.settings, user), env=environment,
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            started = time.monotonic()
            completed = False
            observer_status = None
            last_observer_status = None
            last_controller_check = started
            while self.process.poll() is None:
                if self.stop_requested.wait(0.25): break
                if status_path.exists():
                    observer_status = read_status(status_path)
                    validate_observer_status(observer_status, last_observer_status)
                    last_observer_status = observer_status
                    if observer_status.get("error") or observer_status.get("invalid"):
                        raise EnvironmentError("Observer failure: " + str(observer_status.get("error", "invalid stream")))
                    self.publish(state="running", dolphin="running", capture="recording",
                        events=observer_status["event_count"],
                        source_frames=observer_status["source_tick"],
                        elapsed_seconds=int(time.monotonic() - started),
                        message=("Replaying recorded Dolphin controller input. No live controller is used." if self.replay_source else
                                 "Recording retail play. Finish the match and continue through the results."))
                    if observer_status.get("completed") or observer_status.get("complete"):
                        completed = True
                        break
                elif time.monotonic() - started > 45:
                    raise EnvironmentError("Dolphin started without the required observer handshake")
                if not self.automated and not self.replay_source and time.monotonic() - last_controller_check >= 1.0:
                    self._check_controller_connection()
                    last_controller_check = time.monotonic()
                if file_inventory(user / "Config") != config_before:
                    raise EnvironmentError("Dolphin configuration changed during recording")
            terminated_by_supervisor = self._terminate_owned()
            if not status_path.exists():
                raise EnvironmentError("Observer did not publish a final status")
            final_observer_status = read_status(status_path)
            validate_observer_status(final_observer_status, last_observer_status)
            if final_observer_status.get("error") or final_observer_status.get("invalid"):
                raise EnvironmentError("Observer failure: " + str(final_observer_status.get("error", "invalid stream")))
            completed = completed or final_observer_status.get("completed", False)
            if completed and not terminated_by_supervisor and self.process.returncode != 0:
                raise EnvironmentError("Dolphin crashed after reporting the original teardown")
            log.close()
            log = None
            # The identity recheck below deliberately uses the startup device
            # snapshot for reproducibility.  Also require a live physical
            # controller at teardown so a disconnect immediately before the
            # final observer status cannot be accepted silently.
            self._check_controller_connection()
            self.publish(state="finalizing", dolphin="stopped", capture="finalizing",
                         message="Validating the recorded sequence and original match teardown")
            # Reverify all frozen inputs before accepting any recorded bytes.
            after = verify_environment(self.settings, self.root, devices=self.identity["controller"]["devices"])
            if (after != self.identity or file_inventory(user / "Config") != config_before or
                    sha256(self.settings_path) != settings_hash or
                    self.tooling_identity() != tooling_before):
                raise EnvironmentError("Capture input or configuration drift was detected")
            semantic = SemanticSession()
            records = iter_records(raw)
            try:
                first_record = next(records)
            except StopIteration as error:
                raise EnvironmentError("Observer stream is missing its handshake") from error
            validate_observer_handshake((first_record,), self.identity)
            record_count = 0
            final_sequence = -1
            final_source_tick = 0
            final_draw_ordinal = 0
            for record in chain((first_record,), records):
                record_count += 1
                final_sequence = record["seq"]
                final_source_tick = record["source_tick"]
                final_draw_ordinal = record["draw_ordinal"]
                converted = semantic.consume(record)
                if record["event"] in ("handshake", "start", "progress", "error", "end"):
                    converted["payload"] = record["payload"]
                if record["event"] == "end": converted["event"] = "observer_end"
                bundle.append(converted)
            if final_observer_status["event_count"] != record_count:
                raise EnvironmentError("Observer status event count does not match the raw stream")
            if final_observer_status["last_seq"] != final_sequence:
                raise EnvironmentError("Observer status sequence does not match the raw stream")
            if (final_observer_status["source_tick"] != final_source_tick or
                    final_observer_status["draw_ordinal"] != final_draw_ordinal):
                raise EnvironmentError("Observer status position does not match the raw stream")
            report = semantic.completion()
            if not completed or not report["complete"]:
                reason = "Capture stopped before the original match, results and teardown completed" if self.stop_requested.is_set() else "Dolphin stopped before capture completion"
                write_json(bundle.path / "semantic-validation.json", report)
                bundle.fail(reason, error_type="incomplete")
                self._refresh_partial_discovery()
                self.publish(state="incomplete", capture="idle", message=reason)
                return
            if self.replay_source:
                source_after = load_source(self.replay_source["path"])
                if source_after["manifest_sha256"] != replay_binding:
                    raise EnvironmentError("The original capture changed during replay")
                input_summary = source_after["input"]
            else:
                input_summary = validate_stream(bundle.path / "inputs.mwri")
            validate_input_status(bundle.path / "input-status.json", mode="replay" if self.replay_source else "record",
                                  events=input_summary["events"])
            write_json(bundle.path / "input-validation.json", input_summary)
            final = bundle.complete(semantic_report=report)
            self._refresh_partial_discovery()
            self.publish(state="accepted", capture="idle", bundle_path=str(final),
                         source_frames=report["source_ticks"], events=record_count,
                         message="Capture accepted. The reference bundle is ready for replay comparison.")
            if self.replay_source:
                self._finish_replay_comparison(identifier, final)
        except BaseException as error:
            self._terminate_owned()
            if log is not None:
                log.close()
                log = None
            reason = str(error) if isinstance(error, Exception) else "Capture interrupted during shutdown"
            cancelled = isinstance(error, CaptureCancelled) or not isinstance(error, Exception)
            try: bundle.fail(reason, error_type="incomplete" if cancelled else "capture")
            except Exception: pass
            self._refresh_partial_discovery()
            self.publish(state="incomplete" if cancelled else "failed", dolphin="stopped", capture="idle",
                         message=reason, bundle_path=str(bundle.path))
            if not isinstance(error, Exception):
                raise
        finally:
            # Retain ownership until the child has actually stopped, including
            # synchronous CLI interruption by SystemExit or KeyboardInterrupt.
            self._terminate_owned()
            if log is not None:
                log.close()
            stopped = self.process is None or self.process.poll() is not None
            if stopped:
                self.process = None
            if user is not None:
                write_json(self.root / "Sessions" / (identifier + ".json"),
                           {"capture_id": identifier, "capture_path": str(bundle.path),
                            "dolphin_stopped": stopped})

    def _finish_replay_comparison(self, identifier, final):
        # Raw finalization is already durable. A failed derived report must
        # never attempt to fail/reopen the closed, immutable raw bundle.
        try:
            from reference_session_comparison import compare_bundles
            comparison = compare_bundles(self.replay_source["path"], final)
            saved = ReferenceCaptureInbox(self.root / "Captures").store_derived(
                identifier, "dolphin-replay-comparison", comparison)
        except Exception as error:
            self.publish(state="replay_comparison_failed", capture="idle", bundle_path=str(final),
                         message="The replay recording is preserved, but its comparison report failed: " + str(error))
            return
        matched = comparison["status"] == "matched"
        self.publish(state="replay_matched" if matched else "replay_diverged", capture="idle",
                     comparison_path=str(saved),
                     message=("Dolphin replay matches the original recorded observations." if matched else
                              "Dolphin replay finished with a comparison difference. The report and both recordings are preserved."))

    def tooling_identity(self):
        names = ("scripts/reference_capture_app.py", "tools/reference_capture_environment.py",
                 "tools/reference_controller_probe.py",
                 "tools/reference_input_stream.py", "tools/reference_dolphin_replay.py",
                 "tools/reference_session_comparison.py",
                 "tools/reference_capture_semantics.py", "tools/reference_session_bundle.py",
                 "tools/reference_capture_automation.py",
                 "tools/retail_cpu_observation.py", "tools/retail_replay_validation.py",
                 "tools/retail_setup_validation.py", "tools/retail_input_plan.py",
                 "scripts/build_reference_dolphin.py", "scripts/extract_disc_file.py",
                 "reference-capture/dolphin/reference_observer_stream.py")
        result = {name: sha256(ROOT / name) for name in names}
        installed_identity = ROOT.parent / "identity.json"
        if installed_identity.is_file():
            result["installed_application_identity"] = sha256(installed_identity)
        else:
            for name in ("reference-capture/app/ReferenceCaptureApp.swift",
                         "scripts/install_reference_capture.py"):
                result[name] = sha256(ROOT / name)
        return result

    def configure_controller(self):
        settings = read_settings(self.settings_path)
        verify_environment(settings, self.root,
            progress=lambda state, message: self.publish(state=state, message=message))
        if self.stop_requested.is_set():
            self.publish(state="stopped", dolphin="stopped", capture="idle",
                         message="Controller setup cancelled during environment verification")
            return
        user = self.root / "ControllerSetup"
        user.mkdir(mode=0o700, exist_ok=True)
        config = user / "Config"
        if config.exists(): shutil.rmtree(config)
        shutil.copytree(settings["paths"]["profile"], config)
        if self.stop_requested.is_set():
            self.publish(state="stopped", dolphin="stopped", capture="idle",
                         message="Controller setup cancelled before Dolphin started")
            return
        self.publish(state="configuring", message="In Dolphin, open Controllers and configure port 1. Close Dolphin when done.")
        self.process = subprocess.Popen([settings["paths"]["dolphin"], "-u", str(user)],
                                       env=isolated_dolphin_environment(), start_new_session=True,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        while self.process.poll() is None:
            if self.stop_requested.wait(0.25):
                self._terminate_owned()
                return
        self.process = None
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(config / "Dolphin.ini")
        kind = parser.getint("Core", "SIDevice0", fallback=6)
        pad = configparser.ConfigParser(interpolation=None)
        pad.read(config / "GCPadNew.ini")
        device = pad.get("GCPad1", "Device", fallback="")
        if kind == 12:
            controller = {"backend": "adapter", "device": "GameCube adapter", "port": 1}
        elif kind == 6 and device.startswith("SDL/") and len(device.split("/", 2)) == 3:
            _, index, name = device.split("/", 2)
            if not index.isdecimal():
                raise EnvironmentError("Choose a numbered physical SDL gamepad on port 1")
            controller = {"backend": "SDL", "device": name, "index": int(index), "port": 1}
        else:
            raise EnvironmentError("Choose a physical SDL gamepad or GameCube adapter on port 1")
        # Import only the controller mapping and selected SI device. Other Qt
        # configuration edits never become part of the pinned capture profile.
        profile = Path(settings["paths"]["profile"])
        shutil.copyfile(config / "GCPadNew.ini", profile / "GCPadNew.ini")
        core = configparser.ConfigParser(interpolation=None)
        core.optionxform = str
        core.read(profile / "Dolphin.ini")
        core.set("Core", "SIDevice0", str(kind))
        with (profile / "Dolphin.ini").open("w") as stream: core.write(stream)
        settings["controller"] = controller
        settings["hashes"]["profile"] = file_inventory(profile)
        write_json(self.settings_path, settings)
        self.verify()

    def replay(self, bundle_path):
        self.replay_source = load_source(bundle_path)
        try:
            self.capture()
        finally:
            self.replay_source = None

    def command(self, command, bundle_path=None):
        if command == "stop":
            self.stop_requested.set()
            self.publish(state="stopping", message="Stopping capture; partial evidence will be preserved")
            return
        if command == "open_inbox":
            inbox = self.root / "Captures"
            inbox.mkdir(mode=0o700, exist_ok=True)
            subprocess.Popen(["/usr/bin/open", str(inbox)])
            return
        with self.state_lock:
            if self.worker and self.worker.is_alive(): return
            if command not in ("verify", "start", "configure_controller", "replay"):
                self.publish(state="failed", message="Unknown capture action")
                return
            self.stop_requested.clear()
            def run():
                try:
                    {"verify": self.verify, "start": self.capture,
                     "configure_controller": self.configure_controller,
                     "replay": lambda: self.replay(bundle_path)}[command]()
                except Exception as error:
                    self.publish(state="failed", dolphin="stopped",
                                 capture="idle", message=str(error))
            self.worker = threading.Thread(target=run, daemon=False)
            self.worker.start()

    def close(self):
        self.stop_requested.set()
        if self.worker: self.worker.join()
        self._terminate_owned()
        self.lock_stream.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gui", action="store_true")
    parser.add_argument("--automated", action="store_true",
                        help="Developer validation only: ordinary human port 1 Pipe inputs")
    parser.add_argument("--replay-bundle", type=Path, help="Replay one finalized original input recording, then exit")
    parser.add_argument("--settings", type=Path,
        default=Path(os.environ.get("WEBMELEE_REFERENCE_CAPTURE_SETTINGS", support_root() / "environment.json")))
    args = parser.parse_args()
    app = Supervisor(args.settings, automated=args.automated)
    def interrupted(_signal, _frame):
        app.stop_requested.set()
        raise SystemExit(128 + _signal)
    signal.signal(signal.SIGTERM, interrupted)
    signal.signal(signal.SIGINT, interrupted)
    try:
        if args.replay_bundle:
            app.replay(args.replay_bundle)
            return 0 if app.status["state"] == "replay_matched" else 2
        for line in sys.stdin:
            try:
                value = json.loads(line)
                if not isinstance(value, dict) or set(value) not in ({"command"}, {"command", "bundle_path"}):
                    raise ValueError("Invalid command envelope")
                if "bundle_path" in value and (value.get("command") != "replay" or not isinstance(value["bundle_path"], str)):
                    raise ValueError("Invalid replay request")
                if value.get("command") == "replay" and not value.get("bundle_path"):
                    raise ValueError("Choose a finalized capture to replay")
                app.command(value["command"], value.get("bundle_path"))
            except (ValueError, TypeError) as error:
                app.publish(state="failed", message=str(error))
    finally:
        app.close()


if __name__ == "__main__":
    raise SystemExit(main())
