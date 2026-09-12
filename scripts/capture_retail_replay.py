#!/usr/bin/env python3
"""Run one bounded, read-only GALE01r2 retail replay capture.

The runner owns a fresh user directory, controller pipes, GDB session, and
Dolphin process for one invocation.  It copies template/configuration and
external-GC bytes into that run directory, points the collector at a local
provenance root, and preserves launch/config/input metadata plus process logs.
It never edits the supplied disc, DOL, snapshot, template user, checkpoint, or
provenance inputs.

The fixed procedure is intentionally narrow: stop on the first scheduler
return, advance 12 neutral ticks, arm the collector, issue eight ``PRESS A``
and eight ``RELEASE A`` ticks, disable the temporary scheduler breakpoint, then
let the collector stop at the requested frame count.  With
``--until-match-end``, the same setup uses a full input plan and a hard frame
cap, writing a distinct source-match discovery artifact that stops only after
the original exit and final source draw.  Fixed output is validated by
``retail_replay_validation`` before success is reported.
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import math
import os
import platform
from pathlib import Path
import shutil
import signal
import stat
import struct
import subprocess
import tempfile
import time
import uuid


REPO_ROOT = Path(__file__).resolve().parents[1]
import sys
TOOLS_ROOT = REPO_ROOT / "tools"
sys.path.insert(0, str(TOOLS_ROOT))
sys.path.insert(0, str(REPO_ROOT / "scripts"))

from retail_replay_validation import EXPECTED_PROVENANCE, CPU_PROFILES, MAX_FRAMES, load_capture
from retail_draw_audit import load_draw_audit
from retail_match_completion import load_match_completion
from retail_input_plan import load_plan, verify_capture
from retail_input_bootstrap import (BootstrapCalibrationError, load_calibration,
                                     load_runtime_binding, runtime_binding_record,
                                     write_runtime_binding)
from retail_match_discovery import load_discovery
from extract_disc_file import DiscImage, DiscFormatError


DEFAULT_FRAMES = 240
DEFAULT_TIMEOUT = 120.0
SCHEDULER_RETURN = "0x80390eb4"
GDB_ARCHITECTURE = "powerpc:common"
RTC = EXPECTED_PROVENANCE["fixed_rtc"]
CANONICAL_GDB_SOCKET = "<owned-gdb-socket>"


class CaptureRunnerError(RuntimeError):
    """A prerequisite, child process, timeout, or output validation failed."""


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
    except OSError as error:
        raise CaptureRunnerError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def _sha1(path: Path) -> str:
    digest = hashlib.sha1()
    try:
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                digest.update(block)
    except OSError as error:
        raise CaptureRunnerError(f"cannot hash {path}: {error}") from error
    return digest.hexdigest()


def _sha256_bytes(*parts: bytes) -> str:
    digest = hashlib.sha256()
    for part in parts:
        digest.update(part)
    return digest.hexdigest()


def _canonical_dolphin_ini_sha256(path: Path, owned_socket: Path) -> str:
    """Hash Dolphin.ini with only its verified per-run GDB socket normalized."""

    try:
        raw = path.read_bytes()
        text = raw.decode("utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise CaptureRunnerError(f"cannot read Dolphin.ini for canonical hash: {error}") from error
    expected_socket = str(owned_socket)
    lines: list[str] = []
    in_general = False
    socket_count = 0
    for line in text.splitlines(keepends=True):
        body = line.rstrip("\r\n")
        stripped = body.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_general = stripped[1:-1].strip().lower() == "general"
        if in_general:
            key, separator, value = body.partition("=")
            if separator and key.strip().lower() == "gdbsocket":
                socket_count += 1
                if value.strip() != expected_socket:
                    raise CaptureRunnerError(
                        "Dolphin.ini General/GDBSocket is not the owned runtime socket")
                leading = value[:len(value) - len(value.lstrip())]
                trailing = value[len(value.rstrip()):]
                line = (key + separator + leading + CANONICAL_GDB_SOCKET + trailing
                        + line[len(body):])
        lines.append(line)
    if socket_count != 1:
        raise CaptureRunnerError(
            "Dolphin.ini must contain exactly one General/GDBSocket for canonical binding")
    return hashlib.sha256("".join(lines).encode("utf-8")).hexdigest()


def verify_disc_dol(disc: Path, dol: Path) -> str:
    """Bind the launched image to the independently pinned executable bytes."""
    try:
        size = dol.stat().st_size
        if not 0x100 <= size <= 8 * 1024 * 1024:
            raise CaptureRunnerError("reference DOL exceeds bounded executable size")
        with DiscImage(disc) as image:
            offset = struct.unpack(">I", image.read(0x420, 4))[0]
            if offset < 0x440:
                raise CaptureRunnerError("disc DOL offset precedes disc header")
            digest = hashlib.sha1(image.read(offset, size)).hexdigest()
        if digest != _sha1(dol):
            raise CaptureRunnerError("launched disc DOL does not match pinned reference DOL")
        return digest
    except (OSError, DiscFormatError) as error:
        raise CaptureRunnerError(f"cannot verify launched disc executable: {error}") from error


def _regular_file(path: Path, label: str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_file():
        raise CaptureRunnerError(f"{label} must be a regular file: {resolved}")
    return resolved


def _directory(path: Path, label: str) -> Path:
    resolved = Path(path).expanduser().resolve()
    if not resolved.is_dir():
        raise CaptureRunnerError(f"{label} must be a directory: {resolved}")
    return resolved


def dolphin_executable(path: str | Path) -> Path:
    """Resolve a Dolphin executable or macOS application bundle."""

    candidate = Path(path).expanduser().resolve()
    if candidate.is_dir() and candidate.name.endswith(".app"):
        candidate /= "Contents/MacOS/Dolphin"
    return _regular_file(candidate, "Dolphin executable")


def _strict_provenance(provenance: dict, dol: Path, dolphin: Path, *, cpu: str = "Interpreter64") -> dict:
    if not isinstance(provenance, dict):
        raise CaptureRunnerError("provenance must be a JSON object")
    for key, expected in dict(EXPECTED_PROVENANCE, cpu=cpu).items():
        actual = provenance.get(key)
        if type(actual) is not type(expected) or actual != expected:
            raise CaptureRunnerError(f"pinned provenance mismatch for {key}")
    if provenance.get("background_input") is not True:
        raise CaptureRunnerError("provenance must record BackgroundInput=True")
    binary_provenance = provenance.get("dolphin_binary_sha256")
    if (not isinstance(binary_provenance, str)
            or len(binary_provenance) != 64
            or any(char not in "0123456789abcdefABCDEF" for char in binary_provenance)):
        raise CaptureRunnerError(
            "provenance must include a 64-character Dolphin binary SHA-256")
    dol_sha1 = _sha1(dol)
    if dol_sha1 != provenance["dol_sha1"]:
        raise CaptureRunnerError(
            f"DOL SHA-1 does not match provenance (expected {provenance['dol_sha1']}, "
            f"got {dol_sha1})")
    binary_sha256 = _sha256(dolphin)
    if binary_sha256.lower() != provenance["dolphin_binary_sha256"].lower():
        raise CaptureRunnerError(
            f"Dolphin binary SHA-256 does not match provenance (expected "
            f"{provenance['dolphin_binary_sha256']}, got {binary_sha256})")
    return {
        "dol_sha1": dol_sha1,
        "dolphin_binary_sha256": binary_sha256,
        "source_revision": provenance["source_revision"],
        "dolphin_version": provenance["dolphin_version"],
        "cpu": provenance["cpu"],
        "cpu_thread": provenance["cpu_thread"],
        "cheats": provenance["cheats"],
        "fixed_rtc": provenance["fixed_rtc"],
        "background_input": provenance["background_input"],
    }


def _load_provenance_source(source: Path, dol: Path,
                            dolphin: Path, *, cpu: str = "Interpreter64") -> tuple[dict, dict, bytes]:
    try:
        raw = source.read_bytes()
        provenance = json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CaptureRunnerError(f"cannot read provenance {source}: {error}") from error
    return provenance, _strict_provenance(provenance, dol, dolphin, cpu=cpu), raw


def load_provenance(path: str | Path, dol: Path, dolphin: Path) -> tuple[dict, dict]:
    source = _regular_file(Path(path), "provenance")
    provenance, identity, _ = _load_provenance_source(source, dol, dolphin)
    return provenance, identity


def _copy_tree(source: Path, destination: Path, *, skip: set[str] | None = None) -> None:
    """Copy only regular bytes into an owned tree; reject symlink/special files."""

    source = _directory(source, "copy source")
    skip = skip or set()
    destination.mkdir(parents=True, exist_ok=True)
    for current, directories, files in os.walk(source, followlinks=False):
        current_path = Path(current)
        relative = current_path.relative_to(source)
        if relative.parts and relative.parts[0] in skip:
            directories[:] = []
            continue
        for name in list(directories):
            if not relative.parts and name in skip:
                directories.remove(name)
                continue
            child = current_path / name
            if child.is_symlink():
                raise CaptureRunnerError(f"refusing symlink in copied tree: {child}")
        for name in files:
            child = current_path / name
            if child.is_symlink() or not child.is_file():
                mode = child.lstat().st_mode
                raise CaptureRunnerError(
                    f"refusing non-regular file in copied tree: {child} "
                    f"(mode {stat.S_IFMT(mode):#o})")
        target_dir = destination / relative
        target_dir.mkdir(parents=True, exist_ok=True)
        for name in files:
            shutil.copy2(current_path / name, target_dir / name)


def _copy_checkpoint(source: Path, destination: Path) -> None:
    source = Path(source).expanduser().resolve()
    if not source.exists():
        raise CaptureRunnerError(f"checkpoint-GC path does not exist: {source}")
    if source.is_dir():
        _copy_tree(source, destination)
    elif source.is_file():
        destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination / source.name)
    else:
        raise CaptureRunnerError(f"checkpoint-GC path is not regular: {source}")


def _patch_gdb_socket(config: Path, socket_path: Path) -> None:
    try:
        text = config.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise CaptureRunnerError(f"cannot read copied Dolphin.ini: {error}") from error
    lines = text.splitlines()
    in_general = False
    replaced = False
    output: list[str] = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            in_general = stripped[1:-1].strip().lower() == "general"
        key, separator, _ = stripped.partition("=")
        if in_general and separator and key.strip().lower() == "gdbsocket":
            if replaced:
                raise CaptureRunnerError(
                    f"copied Dolphin.ini contains duplicate General/GDBSocket keys: {config}")
            output.append(f"GDBSocket = {socket_path}")
            replaced = True
        else:
            output.append(line)
    if not replaced:
        if output and output[-1].strip():
            output.append("")
        output.extend(["[General]", f"GDBSocket = {socket_path}"])
    try:
        config.write_text("\n".join(output) + "\n", encoding="utf-8")
    except OSError as error:
        raise CaptureRunnerError(f"cannot write copied Dolphin.ini: {error}") from error


def _require_pipe_config(config: Path) -> None:
    try:
        text = config.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as error:
        raise CaptureRunnerError(f"cannot read copied GCPadNew.ini: {error}") from error
    for port in (1, 2):
        if f"Device = Pipe/0/pad{port}" not in text:
            raise CaptureRunnerError(
                f"copied GCPadNew.ini does not configure Pipe/0/pad{port}")


def require_raw_pipe_config(path: Path) -> None:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    try:
        config.read_string(path.read_text())
        for port in (1, 2):
            section = config[f'GCPad{port}']
            if section.get('Device') != f'Pipe/0/pad{port}':
                raise ValueError('unexpected controller device')
            for stick in ('Main Stick', 'C-Stick'):
                for key in ('Calibration', 'Center', 'Modifier'):
                    if section.get(stick + '/' + key, '').strip():
                        raise ValueError('stick calibration/center/modifier must be empty')
                for key in ('Dead Zone', 'Virtual Notches'):
                    if float(section.get(stick + '/' + key, '0')) != 0:
                        raise ValueError('stick dead zone/notches must be zero')
            if float(section.get('Triggers/Dead Zone', '0')) != 0 or not 0 < float(section.get('Triggers/Threshold', '90')) <= 100:
                raise ValueError('unsupported trigger processing configuration')
    except (OSError, ValueError, KeyError, configparser.Error) as error:
        raise CaptureRunnerError(f'raw pipe input configuration: {error}') from error


def _make_fifo(path: Path) -> None:
    try:
        os.mkfifo(path, 0o600)
    except OSError as error:
        raise CaptureRunnerError(f"cannot create owned controller pipe {path}: {error}") from error


def prepare_run(template_user: str | Path, checkpoint_gc: str | Path,
                provenance_path: str | Path, dol: str | Path,
                dolphin: str | Path, output: str | Path, *, cpu: str = "Interpreter64") -> dict:
    """Prepare an owned run directory and return its paths/metadata."""

    template = _directory(Path(template_user), "template user")
    checkpoint = Path(checkpoint_gc).expanduser().resolve()
    dol_path = _regular_file(Path(dol), "DOL")
    dolphin_path = dolphin_executable(dolphin)
    output_path = Path(output).expanduser().resolve()
    if output_path.exists():
        raise CaptureRunnerError(f"capture output already exists: {output_path}")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    provenance_source = _regular_file(Path(provenance_path), "provenance")
    provenance, identity, provenance_bytes = _load_provenance_source(
        provenance_source, dol_path, dolphin_path, cpu=cpu)

    try:
        run_root = Path(tempfile.mkdtemp(prefix=".retail-replay-run-",
                                         dir=str(output_path.parent)))
    except OSError as error:
        raise CaptureRunnerError(f"cannot create owned run directory: {error}") from error
    user = run_root / "user"
    evidence = run_root / "evidence"
    # Darwin's Unix-domain socket path limit is 104 bytes. Evidence directories
    # may be much longer; use a unique short endpoint and remove it in finally.
    socket_path = Path("/tmp") / ("mwr-" + uuid.uuid4().hex + ".sock")
    snapshot_copy = run_root / "snapshot.sav"
    collector = REPO_ROOT / "tools/reference_replay_capture.py"
    collector_env = os.environ.get("MELEE_REPLAY_COLLECTOR")
    collector = _regular_file(
        Path(collector_env) if collector_env else collector, "retail collector")
    collector_boundary = _regular_file(
        collector.with_name("reference_replay_boundary.py"),
        "retail collector boundary helper")
    collector_input = _regular_file(collector.with_name('retail_input_plan.py'),
                                    'retail input plan helper')
    collector_bootstrap = _regular_file(
        collector.with_name('retail_input_bootstrap.py'),
        'retail input bootstrap helper')
    try:
        collector_sha256 = _sha256_bytes(
            collector.read_bytes(), b"\0", collector_boundary.read_bytes(),
            b"\0", collector_input.read_bytes(), b"\0", collector_bootstrap.read_bytes())
    except OSError as error:
        raise CaptureRunnerError(
            f"cannot hash retail collector sources: {error}") from error
    try:
        pinned_collector = run_root / "collector"
        pinned_collector.mkdir()
        shutil.copy2(collector, pinned_collector / "reference_replay_capture.py")
        shutil.copy2(collector_boundary, pinned_collector / "reference_replay_boundary.py")
        shutil.copy2(collector_input, pinned_collector / 'retail_input_plan.py')
        shutil.copy2(collector_bootstrap, pinned_collector / 'retail_input_bootstrap.py')
        collector = pinned_collector / "reference_replay_capture.py"
        collector_boundary = pinned_collector / "reference_replay_boundary.py"
        _copy_tree(template, user, skip={"Pipes"})
        config = _regular_file(user / "Config/Dolphin.ini", "copied Dolphin.ini")
        pad_config = _regular_file(user / "Config/GCPadNew.ini", "copied GCPadNew.ini")
        _patch_gdb_socket(config, socket_path)
        _require_pipe_config(pad_config)
        pipes = user / "Pipes"
        pipes.mkdir(parents=True, exist_ok=True)
        for port in (1, 2):
            _make_fifo(pipes / f"pad{port}")
        gc = user / "GC"
        if gc.exists():
            shutil.rmtree(gc)
        _copy_checkpoint(checkpoint, gc)
    except CaptureRunnerError:
        raise
    except OSError as error:
        raise CaptureRunnerError(f"cannot prepare owned run directory: {error}") from error
    return {
        "run_root": run_root,
        "user": user,
        "evidence": evidence,
        "socket": socket_path,
        "snapshot": snapshot_copy,
        "output": output_path,
        "collector": collector,
        "collector_boundary": collector_boundary,
        "collector_sha256": collector_sha256,
        "config": config,
        "pad_config": pad_config,
        "provenance": provenance,
        "provenance_bytes": provenance_bytes,
        "source_provenance": provenance_source,
        "identity": identity,
        "source_dol": dol_path,
        "source_dolphin": dolphin_path,
    }


def _gdb_quote(value: str | Path) -> str:
    text = str(value)
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_control_helper(path: Path, pipes: Path, output: Path | None = None) -> None:
    content = f'''"""Owned GDB control helper for one retail capture."""
import gdb
import os
import struct

PIPES = {str(pipes)!r}
SCENE_FRAME = 0x80479D58
MAX_TRAPS_FACTOR = 8
MAX_TRAPS_SLOP = 64

def source_frame():
    raw = bytes(gdb.selected_inferior().read_memory(SCENE_FRAME, 4))
    return struct.unpack(">I", raw)[0]

class RetailStep(gdb.Command):
    def __init__(self):
        super().__init__("retail-step", gdb.COMMAND_USER)

    def invoke(self, args, from_tty):
        words = gdb.string_to_argv(args)
        if not words:
            raise gdb.GdbError("retail-step COUNT [PORT] [COMMAND]")
        count = int(words[0])
        port = int(words[1]) if len(words) > 1 else 1
        command = " ".join(words[2:])
        if count < 0 or port not in (1, 2):
            raise gdb.GdbError("retail-step has invalid count or port")
        if command:
            pipe = os.path.join(PIPES, "pad%d" % port)
            try:
                fd = os.open(pipe, os.O_WRONLY | os.O_NONBLOCK)
            except OSError as error:
                raise gdb.GdbError("cannot write owned controller pipe: %s" % error)
            try:
                os.write(fd, (command + "\\n").encode("ascii"))
            finally:
                os.close(fd)
        trap_budget = MAX_TRAPS_FACTOR * count + MAX_TRAPS_SLOP
        traps = 0
        for tick in range(count):
            before = source_frame()
            while traps < trap_budget:
                traps += 1
                gdb.execute("continue", to_string=True)
                after = source_frame()
                if after == before:
                    continue
                expected = (before + 1) & 0xffffffff
                if after != expected:
                    raise gdb.GdbError(
                        "source scene_frame jumped during owned tick %d: "
                        "expected 0x%08x, got 0x%08x" % (tick, expected, after))
                break
            else:
                raise gdb.GdbError(
                    "source scene_frame did not advance within %d GDB traps" % trap_budget)
        print("Completed", count, "owned retail scheduler ticks; trap budget", trap_budget)

RetailStep()
'''
    try:
        path.write_text(content, encoding="utf-8")
    except OSError as error:
        raise CaptureRunnerError(f"cannot write GDB control helper: {error}") from error


def write_gdb_script(path: Path, socket: Path, helper: Path, collector: Path,
                     output: Path, frames: int) -> None:
    # Calibration uses the same neutral/press/release and scheduler-disable
    # sequence as ordinary captures. Its collector stops at VS entry during
    # the final continue, after which this script's quit command runs.
    lines = [
        "set architecture powerpc:common",
        "set endian big",
        "set pagination off",
        "set confirm off",
        "set breakpoint pending on",
        f"target remote {socket}",
        f"hbreak *{SCHEDULER_RETURN}",
        "commands",
        "  silent",
        "end",
        # Stop at the first ordinary SSS scheduler return.
        "continue",
        f"source {helper}",
        "retail-step 12 1 SET MAIN 0.5 0.5",
        f"source {collector}",
        f"retail-replay-arm {_gdb_quote(output)} {frames}",
        "retail-step 8 1 PRESS A",
        "retail-step 8 1 RELEASE A",
        # GDB starts this fresh session with the user hbreak as breakpoint 1.
        "disable 1",
        "continue",
        "quit",
    ]
    lines.append("")
    content = "\n".join(lines)
    try:
        path.write_text(content, encoding="utf-8")
    except OSError as error:
        raise CaptureRunnerError(f"cannot write GDB command script: {error}") from error


def dolphin_command(dolphin: Path, user: Path, snapshot: Path, disc: Path,
                    *, cpu: str = "Interpreter64") -> list[str]:
    if cpu not in CPU_PROFILES:
        raise CaptureRunnerError("Unsupported reference CPU profile")
    return [
        str(dolphin), "-u", str(user), "-b", "-d", "-s", str(snapshot), "-e", str(disc),
        "-C", "Dolphin.Input.BackgroundInput=True",
        "-C", "Dolphin.Display.Fullscreen=False",
        "-C", "Dolphin.DSP.Backend=No Audio Output",
        "-C", f"Dolphin.Core.CPUCore={CPU_PROFILES[cpu]}",
        "-C", "Dolphin.Core.CPUThread=False",
        "-C", "Dolphin.Core.EnableCheats=False",
        "-C", "Dolphin.Core.EnableCustomRTC=True",
        "-C", f"Dolphin.Core.CustomRTCValue={RTC}",
    ]


def _write_json(path: Path, value: dict) -> None:
    try:
        path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                        encoding="utf-8")
    except OSError as error:
        raise CaptureRunnerError(f"cannot write run metadata {path}: {error}") from error


def _terminate(process: subprocess.Popen | None, label: str) -> None:
    if process is None or process.poll() is not None:
        return
    try:
        # Both children are launched with ``start_new_session=True``.  Signal
        # only that owned process group so a Dolphin helper cannot outlive a
        # failed capture, while never touching an unrelated process group.
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            return
        except OSError:
            # Keep cleanup useful for a test double or a platform without
            # process groups; the process itself is still owned by this run.
            process.terminate()
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except OSError:
            process.kill()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired as error:
            raise CaptureRunnerError(f"could not terminate owned {label}") from error
    except OSError as error:
        raise CaptureRunnerError(f"could not terminate owned {label}: {error}") from error


def wait_for_socket(socket: Path, process: subprocess.Popen, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise CaptureRunnerError(
                f"Dolphin exited before GDB socket appeared (status {process.returncode})")
        try:
            if stat.S_ISSOCK(socket.stat().st_mode):
                return
        except FileNotFoundError:
            pass
        except OSError as error:
            raise CaptureRunnerError(f"cannot inspect GDB socket {socket}: {error}") from error
        time.sleep(0.05)
    raise CaptureRunnerError(f"timed out waiting for GDB socket: {socket}")


def capture_replay(*, dolphin: str | Path, disc: str | Path, dol: str | Path,
                   template_user: str | Path, snapshot: str | Path,
                   checkpoint_gc: str | Path, provenance: str | Path,
                   output: str | Path, frames: int = DEFAULT_FRAMES,
                   timeout: float = DEFAULT_TIMEOUT, draw_audit: bool = False,
                   input_plan: str | Path | None = None, cpu: str = "Interpreter64",
                   require_match_complete: bool = False,
                   until_match_end: bool = False,
                   input_bootstrap: str | Path | None = None,
                   input_bootstrap_calibrate: bool = False) -> dict:
    """Run the bounded capture and return preserved run metadata."""

    if isinstance(frames, bool) or not isinstance(frames, int) or not 1 <= frames <= MAX_FRAMES:
        raise CaptureRunnerError(f"frames must be an integer in 1..{MAX_FRAMES}")
    if (isinstance(timeout, bool) or not isinstance(timeout, (int, float))
            or not math.isfinite(timeout) or timeout <= 0):
        raise CaptureRunnerError("timeout must be positive")
    if not isinstance(draw_audit, bool):
        raise CaptureRunnerError("draw_audit must be boolean")
    if not isinstance(require_match_complete, bool):
        raise CaptureRunnerError("require_match_complete must be boolean")
    if not isinstance(until_match_end, bool):
        raise CaptureRunnerError("until_match_end must be boolean")
    if not isinstance(input_bootstrap_calibrate, bool):
        raise CaptureRunnerError("input_bootstrap_calibrate must be boolean")
    if input_bootstrap is not None and input_bootstrap_calibrate:
        raise CaptureRunnerError("Choose input bootstrap apply or calibration, not both")
    if input_bootstrap_calibrate and (until_match_end or require_match_complete or draw_audit):
        raise CaptureRunnerError("Bootstrap calibration cannot request capture completion or draw evidence")
    if require_match_complete and not draw_audit:
        raise CaptureRunnerError("Complete-match capture requires --draw-audit")
    if until_match_end and require_match_complete:
        raise CaptureRunnerError("Match-length discovery cannot be a fixed candidate completion")
    if cpu not in CPU_PROFILES:
        raise CaptureRunnerError("Unsupported reference CPU profile")
    if cpu == "JITARM64" and platform.machine() not in ("arm64", "aarch64"):
        raise CaptureRunnerError("JITARM64 capture requires an ARM64 host")
    plan = None
    if input_plan is not None:
        try:
            plan, plan_hash = load_plan(input_plan)
            if (until_match_end and frames > len(plan['frames'])):
                raise ValueError('Discovery cap exceeds the declared input plan')
            if not until_match_end and frames != len(plan['frames']):
                raise ValueError('Capture must consume the entire declared input plan')
        except (OSError, ValueError) as error:
            raise CaptureRunnerError(str(error)) from error
    elif until_match_end:
        raise CaptureRunnerError("Match-length discovery requires --input-plan")
    if input_bootstrap_calibrate and plan is None:
        raise CaptureRunnerError("Bootstrap calibration requires --input-plan")
    if input_bootstrap is not None and plan is None:
        raise CaptureRunnerError("Calibrated input requires --input-plan")
    disc_path = _regular_file(Path(disc), "disc image")
    snapshot_path = _regular_file(Path(snapshot), "snapshot")
    started = time.monotonic()
    paths = prepare_run(template_user, checkpoint_gc, provenance, dol, dolphin, output, cpu=cpu)
    run_root = paths["run_root"]
    paths["identity"]["disc_dol_sha1"] = verify_disc_dol(disc_path, paths["source_dol"])
    # Copy the snapshot into the owned run evidence area. Dolphin only reads
    # it, but this makes the launch wholly independent of a mutable source.
    try:
        shutil.copy2(snapshot_path, paths["snapshot"])
        paths["evidence"].mkdir(parents=True, exist_ok=True)
        (paths["evidence"] / "provenance.json").write_bytes(paths["provenance_bytes"])
    except OSError as error:
        raise CaptureRunnerError(f"cannot copy owned snapshot/provenance: {error}") from error
    if _sha256(paths["snapshot"]) != paths["provenance"].get("setup_snapshot_sha256"):
        raise CaptureRunnerError("owned snapshot does not match pinned provenance")
    gc = paths["user"] / "GC"
    actual_gc = {str(p.relative_to(gc)): _sha256(p)
                 for p in gc.rglob("*") if p.is_file()}
    if actual_gc != paths["provenance"].get("external_save_hashes"):
        raise CaptureRunnerError("owned external GC state does not match pinned provenance")
    bootstrap_runtime = None
    bootstrap_runtime_path = None
    bootstrap_runtime_hash = None
    if input_bootstrap is not None or input_bootstrap_calibrate:
        bootstrap_runtime = runtime_binding_record(
            dolphin_ini_canonical_sha256=_canonical_dolphin_ini_sha256(
                paths["config"], paths["socket"]),
            gcpad_ini_sha256=_sha256(paths["pad_config"]),
            external_save_hashes=actual_gc)
        bootstrap_runtime_path = paths["evidence"] / "input-bootstrap-runtime.json"
        try:
            write_runtime_binding(bootstrap_runtime_path, bootstrap_runtime)
            bootstrap_runtime_hash = _sha256(bootstrap_runtime_path)
        except (OSError, BootstrapCalibrationError) as error:
            raise CaptureRunnerError(str(error)) from error
    bootstrap_runtime_for_run = dict(paths["provenance"])
    bootstrap_runtime_for_run.update(bootstrap_runtime or {})
    helper = run_root / "gdb-control.py"
    if plan is not None:
        require_raw_pipe_config(paths['pad_config'])
        plan_copy = paths['evidence'] / 'input-plan.json'
        shutil.copy2(input_plan, plan_copy)
        if _sha256(plan_copy) != plan_hash:
            raise CaptureRunnerError('Input plan changed during preparation')
    bootstrap_copy = None
    bootstrap_hash = None
    if input_bootstrap is not None:
        bootstrap_source = _regular_file(Path(input_bootstrap), "input bootstrap calibration")
        bootstrap_copy = paths["evidence"] / "input-bootstrap.json"
        shutil.copy2(bootstrap_source, bootstrap_copy)
        bootstrap_hash = _sha256(bootstrap_copy)
        try:
            load_calibration(bootstrap_copy, plan=plan, plan_sha256=plan_hash,
                             runtime=bootstrap_runtime_for_run,
                             collector_sha256=paths["collector_sha256"])
        except (OSError, BootstrapCalibrationError) as error:
            raise CaptureRunnerError(str(error)) from error
    gdb_commands = run_root / "gdb-commands.txt"
    collector_output = (paths["evidence"] / "bootstrap-capture.jsonl"
                        if input_bootstrap_calibrate else paths["output"])
    write_control_helper(helper, paths["user"] / "Pipes", collector_output)
    write_gdb_script(gdb_commands, paths["socket"], helper, paths["collector"],
                     collector_output, frames)
    command = dolphin_command(paths["source_dolphin"], paths["user"],
                              paths["snapshot"], disc_path, cpu=cpu)
    gdb_command = ["gdb", "--quiet", "--nx", "--batch", "-x", str(gdb_commands)]
    metadata_path = run_root / "run-metadata.json"
    input_procedure = {
        "first_scheduler_breakpoint": SCHEDULER_RETURN,
        "neutral_ticks_before_arm": 12,
        "press_ticks": 8,
        "release_ticks": 8,
        "port": 1,
        "commands": ["PRESS A", "RELEASE A"],
        "tick_clock": "scene_frame at 0x80479D58",
        "duplicate_trap_budget": "8 * requested_ticks + 64",
        "tick_transition": "exactly previous scene_frame + 1 modulo u32",
        "collector_phase": "HSD_GObj_80390CFC_return",
        "input_phase": "HSD_PadRenewMasterStatus_dequeued_slot",
        "bootstrap_mode": (
            "calibrate" if input_bootstrap_calibrate else
            "apply" if input_bootstrap is not None else "default"),
    }
    metadata = {
        "status": "prepared",
        "evidence_status": "provisional",
        "scope": (
            "source retail match-length discovery only; exact input prefix and "
            "original exit/final-draw evidence; no reference, port equivalence, "
            "performance, or gold admission claim"
            if until_match_end else
            "bounded retail candidate capture procedure only; repeatability must "
            "be established by the separate comparator; no port equivalence, "
            "performance acceptance, or gold admission claim"
        ),
        "run_id": uuid.uuid4().hex,
        "run_root": str(run_root),
        "output": str(paths["output"]),
        "frames_requested": frames,
        "source_draw_audit": draw_audit or until_match_end,
        "require_match_complete": require_match_complete,
        "until_match_end": until_match_end,
        "input_bootstrap_calibrate": input_bootstrap_calibrate,
        "timeout_seconds": timeout,
        "identity": paths["identity"],
        "source": {
            "disc": str(disc_path),
            "dol": str(paths["source_dol"]),
            "snapshot": str(snapshot_path),
            "template_user": str(Path(template_user).expanduser().resolve()),
            "checkpoint_gc": str(Path(checkpoint_gc).expanduser().resolve()),
            "provenance": str(paths["source_provenance"]),
        },
        "owned": {
            "user": str(paths["user"]),
            "socket": str(paths["socket"]),
            "evidence": str(paths["evidence"]),
            "snapshot": str(paths["snapshot"]),
            "collector": str(paths["collector"]),
            "collector_sha256": paths["collector_sha256"],
            "collector_boundary": str(paths["collector_boundary"]),
            "collector_boundary_sha256": _sha256(paths["collector_boundary"]),
            "dolphin_ini_sha256": _sha256(paths["config"]),
            "dolphin_ini_canonical_sha256": (
                bootstrap_runtime["dolphin_ini_canonical_sha256"]
                if bootstrap_runtime is not None else None),
            "gcpad_ini_sha256": _sha256(paths["pad_config"]),
            "input_bootstrap_runtime": (
                str(bootstrap_runtime_path) if bootstrap_runtime_path is not None else None),
            "input_bootstrap_runtime_sha256": bootstrap_runtime_hash,
            "snapshot_sha256": _sha256(paths["snapshot"]),
            "provenance_sha256": _sha256(paths["evidence"] / "provenance.json"),
        },
        "launch": {"dolphin": command, "gdb": gdb_command},
        "configuration": {
            "architecture": GDB_ARCHITECTURE,
            "endian": "big",
            "background_input_cli": "Dolphin.Input.BackgroundInput=True",
            "cpu_core_cli": f"Dolphin.Core.CPUCore={CPU_PROFILES[cpu]} ({cpu})",
            "debugger_ui": "hidden by batch mode; hardware observers retained",
            "host_audio_sink": "No Audio Output; DSP/game audio execution retained",
            "cpu_thread_cli": "Dolphin.Core.CPUThread=False",
            "cheats_cli": "Dolphin.Core.EnableCheats=False",
            "rtc_cli": f"Dolphin.Core.EnableCustomRTC=True, Dolphin.Core.CustomRTCValue={RTC}",
        },
        "input_procedure": input_procedure,
        "gdb_commands": str(gdb_commands),
        "logs": {
            "dolphin": str(run_root / "dolphin.log"),
            "gdb": str(run_root / "gdb.log"),
        },
    }
    _write_json(metadata_path, metadata)
    if plan is not None:
        metadata['input_plan'] = {'path': str(plan_copy), 'sha256': plan_hash,
            'source_sha256': plan['source_sha256'], 'policy': plan['policy'],
            'source_first_frame': plan['first_frame'], 'frames': len(plan['frames']),
            'consumption': 'not_yet_verified'}
        _write_json(metadata_path, metadata)
    if bootstrap_copy is not None:
        metadata["input_bootstrap"] = {
            "path": str(bootstrap_copy), "sha256": bootstrap_hash,
            "source": str(Path(input_bootstrap).expanduser().resolve()),
            "runtime_path": str(bootstrap_runtime_path),
            "runtime_sha256": bootstrap_runtime_hash,
        }
        _write_json(metadata_path, metadata)
    dolphin_process: subprocess.Popen | None = None
    gdb_process: subprocess.Popen | None = None
    dolphin_log = None
    gdb_log = None
    try:
        environment = os.environ.copy()
        environment["MELEE_REPLAY_REFERENCE_WORK"] = str(paths["evidence"])
        environment["MELEE_REPLAY_COLLECTOR"] = str(paths["collector"])
        environment["MELEE_REPLAY_DRAW_AUDIT"] = "1" if (draw_audit or until_match_end) else "0"
        environment["MELEE_REPLAY_UNTIL_MATCH_END"] = "1" if until_match_end else "0"
        environment["MELEE_REPLAY_INPUT_BOOTSTRAP_MODE"] = (
            "calibrate" if input_bootstrap_calibrate else
            "apply" if input_bootstrap is not None else "default")
        environment.pop("MELEE_REPLAY_INPUT_BOOTSTRAP_CALIBRATION", None)
        environment.pop("MELEE_REPLAY_INPUT_BOOTSTRAP_RUNTIME", None)
        environment.pop("MELEE_REPLAY_INPUT_BOOTSTRAP_OUTPUT", None)
        if bootstrap_copy is not None:
            environment["MELEE_REPLAY_INPUT_BOOTSTRAP_CALIBRATION"] = str(bootstrap_copy)
        if bootstrap_runtime_path is not None:
            environment["MELEE_REPLAY_INPUT_BOOTSTRAP_RUNTIME"] = str(bootstrap_runtime_path)
        if input_bootstrap_calibrate:
            environment["MELEE_REPLAY_INPUT_BOOTSTRAP_OUTPUT"] = str(
                paths["evidence"] / "bootstrap-calibration.json")
        environment.pop('MELEE_REPLAY_INPUT_PLAN', None)
        if plan is not None:
            environment['MELEE_REPLAY_INPUT_PLAN'] = str(plan_copy)
        dolphin_log = (run_root / "dolphin.log").open("w", encoding="utf-8")
        dolphin_process = subprocess.Popen(
            command, stdout=dolphin_log, stderr=subprocess.STDOUT, env=environment,
            start_new_session=True)
        wait_for_socket(paths["socket"], dolphin_process,
                        min(float(timeout), 30.0))
        remaining = float(timeout) - (time.monotonic() - started)
        if remaining <= 0:
            raise CaptureRunnerError(
                f"capture timed out before GDB could start after {timeout} seconds")
        gdb_log = (run_root / "gdb.log").open("w", encoding="utf-8")
        gdb_process = subprocess.Popen(
            gdb_command, stdout=gdb_log, stderr=subprocess.STDOUT, env=environment,
            start_new_session=True)
        try:
            gdb_process.wait(timeout=remaining)
        except subprocess.TimeoutExpired as error:
            raise CaptureRunnerError(
                f"timed out waiting for GDB capture after {timeout} seconds") from error
        if gdb_process.returncode != 0:
            raise CaptureRunnerError(
                f"GDB capture failed with status {gdb_process.returncode}; see {run_root / 'gdb.log'}")
        if input_bootstrap_calibrate:
            calibration_path = paths["evidence"] / "bootstrap-calibration.json"
            if not calibration_path.exists():
                raise CaptureRunnerError(
                    f"collector did not produce bootstrap calibration: {calibration_path}")
            try:
                calibration, calibration_hash = load_calibration(
                    calibration_path, plan=plan, plan_sha256=plan_hash,
                    runtime=bootstrap_runtime_for_run, collector_sha256=paths["collector_sha256"])
                shutil.copy2(calibration_path, paths["output"])
            except (OSError, BootstrapCalibrationError) as error:
                raise CaptureRunnerError(str(error)) from error
            metadata["input_bootstrap"] = {
                "path": str(paths["output"]), "sha256": _sha256(paths["output"]),
                "construction_pad_reads": calibration["construction_pad_reads"],
                "calibration_sha256": calibration_hash,
                "runtime_path": str(bootstrap_runtime_path),
                "runtime_sha256": bootstrap_runtime_hash,
            }
            metadata.update({
                "status": "bootstrap_calibrated",
                "output_sha256": _sha256(paths["output"]),
                "frames_actual": 0,
                "capture_wall_seconds": time.monotonic() - started,
            })
            _write_json(metadata_path, metadata)
            return metadata
        if not paths["output"].exists():
            raise CaptureRunnerError(
                f"collector did not produce requested output: {paths['output']}")
        try:
            if until_match_end:
                discovery = load_discovery(paths["output"], cpu=cpu, plan=plan,
                                           plan_sha256=plan_hash)
                if discovery.header['collector_sha256'] != paths['collector_sha256']:
                    raise ValueError('Discovery collector hash differs from owned source files')
                metadata['discovery'] = dict(discovery.report)
                metadata['discovery']['path'] = str(paths['output'])
                metadata['input_plan']['consumption'] = 'verified_prefix'
                if not discovery.complete:
                    raise ValueError(
                        'original match did not end before the discovery frame cap')
                loaded = discovery
            else:
                loaded = load_capture(paths["output"], cpu=cpu)
                if loaded.header['collector_sha256'] != paths['collector_sha256']:
                    raise ValueError('Captured collector hash differs from owned source files')
                if len(loaded.frames) != frames:
                    raise ValueError("captured frame count differs from requested bound")
                if plan is not None:
                    if loaded.header['provenance'].get('input_plan_sha256') != plan_hash:
                        raise ValueError('Captured input plan hash differs from owned plan')
                    verify_capture(plan, loaded)
                    metadata['input_plan']['consumption'] = 'verified_all_ticks'
                if draw_audit:
                    audit_path = paths["evidence"] / "draw-audit.jsonl"
                    metadata["draw_audit"] = load_draw_audit(loaded, audit_path)
                    metadata["draw_audit"]["path"] = str(audit_path)
                completion_path = paths["evidence"] / "match-completion.json"
                if completion_path.exists() or require_match_complete:
                    metadata["match_completion"] = load_match_completion(
                        loaded, completion_path, require_complete=require_match_complete)
        except ValueError as error:
            raise CaptureRunnerError(f"captured JSONL failed strict validation: {error}") from error
        metadata.update({
            "status": "discovered" if until_match_end else "captured",
            "output_sha256": loaded.sha256,
            "frames_actual": len(loaded.frames),
            "capture_id": loaded.header["capture_id"],
            "capture_wall_seconds": time.monotonic() - started,
        })
        _write_json(metadata_path, metadata)
        return metadata
    except (CaptureRunnerError, OSError, subprocess.SubprocessError) as error:
        metadata.update({"status": "failed", "error": str(error)})
        try:
            _write_json(metadata_path, metadata)
        except CaptureRunnerError:
            pass
        raise CaptureRunnerError(str(error)) from error
    finally:
        if gdb_log is not None:
            gdb_log.close()
        if dolphin_log is not None:
            dolphin_log.close()
        try:
            _terminate(gdb_process, "GDB")
        finally:
            _terminate(dolphin_process, "Dolphin")
            paths["socket"].unlink(missing_ok=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dolphin", required=True, type=Path)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--dol", required=True, type=Path)
    parser.add_argument("--template-user", required=True, type=Path)
    parser.add_argument("--snapshot", required=True, type=Path)
    parser.add_argument("--checkpoint-gc", required=True, type=Path)
    parser.add_argument("--provenance", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=DEFAULT_FRAMES)
    parser.add_argument('--input-plan', type=Path, help='Input-only donor plan; every declared tick must be consumed')
    parser.add_argument("--draw-audit", action="store_true", help="Observe every source camera traversal and its scheduler-tick index")
    parser.add_argument("--require-match-complete", action="store_true",
                        help="Require the original elimination exit request and final source draw")
    parser.add_argument("--until-match-end", action="store_true",
                        help="Discover the original match length under --frames using a full input plan")
    bootstrap = parser.add_mutually_exclusive_group()
    bootstrap.add_argument(
        "--input-bootstrap", type=Path,
        help="Apply a previously calibrated construction PADRead bootstrap sidecar")
    bootstrap.add_argument(
        "--input-bootstrap-calibrate", action="store_true",
        help="Record a construction PADRead bootstrap sidecar at --output")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--cpu", choices=CPU_PROFILES, default="Interpreter64",
                        help="Explicit reference backend; JIT needs interpreter calibration")
    args = parser.parse_args(argv)
    try:
        metadata = capture_replay(
            dolphin=args.dolphin, disc=args.disc, dol=args.dol,
            template_user=args.template_user, snapshot=args.snapshot,
            checkpoint_gc=args.checkpoint_gc, provenance=args.provenance,
            output=args.output, frames=args.frames, timeout=args.timeout,
            draw_audit=args.draw_audit, input_plan=args.input_plan, cpu=args.cpu,
            require_match_complete=args.require_match_complete,
            until_match_end=args.until_match_end,
            input_bootstrap=args.input_bootstrap,
            input_bootstrap_calibrate=args.input_bootstrap_calibrate)
    except CaptureRunnerError as error:
        parser.exit(2, f"retail capture failed: {error}\n")
    print(json.dumps({
        "status": metadata["status"],
        "output": metadata["output"],
        "output_sha256": metadata.get("output_sha256"),
        "frames": metadata.get("frames_actual"),
        "capture_id": metadata.get("capture_id"),
        "run_root": metadata["run_root"],
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
