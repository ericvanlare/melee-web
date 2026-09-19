#!/usr/bin/env python3
"""Bounded, receipt-gated macOS System Trace capture.

The command has three deliberately separate phases:

``preflight``
    Attach System Trace to a short-lived process owned by this command.  The
    run proves recorder startup, bounded finalization, XML export, and the
    requested rolling-window setting before a gameplay capture is attempted.
``validate``
    Validate an immutable preflight receipt against a proposed capture
    configuration.  This command is safe to run before launching a target.
``record``
    Attach to an explicitly supplied PID after validating the receipt.  Only
    the recorder and readiness observer are cleaned up by this command; the
    target process is never killed.

The output directory is immutable by construction: it must not exist when a
command starts, and every file written by this module is created with
exclusive creation.  A failed run leaves its diagnostics in the directory.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET
from typing import Any, Callable, Iterable, Mapping, Sequence


SCHEMA = "melee-web-native-capture-preflight-v1"
CAPTURE_SCHEMA = "melee-web-native-capture-v1"
RECEIPT_VERSION = 1
DEFAULT_TEMPLATE = "System Trace"
DEFAULT_RETENTION_SECONDS = 90.0
DEFAULT_PROBE_SECONDS = 2.0
DEFAULT_MIN_RETAINED_SECONDS = 0.5
DEFAULT_READINESS_TIMEOUT = 30.0
DEFAULT_STOP_TIMEOUT = 30.0
DEFAULT_KILL_TIMEOUT = 5.0
DEFAULT_EXPORT_TIMEOUT = 60.0
DEFAULT_VALID_FOR_SECONDS = 24 * 60 * 60.0
DEFAULT_SCOPE = "single-target-system-trace"
MAX_LOG_BYTES = 8 * 1024 * 1024


class NativeCaptureError(RuntimeError):
    """A preflight, recorder, export, or coverage validation failure."""

    def __init__(self, phase: str, message: str):
        self.phase = phase
        super().__init__(f"{phase}: {message}")


def _canonical(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"),
                       ensure_ascii=True) + "\n").encode("utf-8")


def _sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def _artifact_identity(path: Path) -> dict[str, Any]:
    """Hash a regular file or an xctrace directory bundle without paths."""
    if path.is_symlink():
        raise NativeCaptureError("receipt", f"evidence symlinks are not allowed: {path.name}")
    if path.is_file():
        return {"kind": "file", "bytes": path.stat().st_size, "sha256": _sha256_file(path)}
    if not path.is_dir():
        raise NativeCaptureError("receipt", f"evidence is not a regular file or directory bundle: {path.name}")
    entries = []
    for child in sorted(path.rglob("*")):
        if child.is_symlink():
            raise NativeCaptureError("receipt", f"trace bundle contains unsupported entry: {child.name}")
        if not child.is_file():
            continue
        entries.append({"path": child.relative_to(path).as_posix(),
                        "bytes": child.stat().st_size, "sha256": _sha256_file(child)})
    return {"kind": "directory", "bytes": sum(item["bytes"] for item in entries),
            "files": len(entries), "sha256": _sha256_bytes(_canonical(entries))}


def _artifact_matches(path: Path, identity: Mapping[str, Any]) -> bool:
    try:
        return _artifact_identity(path) == dict(identity)
    except NativeCaptureError:
        return False


def _seal(value: Mapping[str, Any], field: str) -> dict[str, Any]:
    body = dict(value)
    body.pop(field, None)
    body[field] = _sha256_bytes(_canonical(body))
    return body


def _verify_seal(value: Mapping[str, Any], field: str) -> None:
    supplied = value.get(field)
    if not isinstance(supplied, str) or len(supplied) != 64:
        raise NativeCaptureError("receipt", f"missing or malformed {field}")
    body = dict(value)
    del body[field]
    if _sha256_bytes(_canonical(body)) != supplied:
        raise NativeCaptureError("receipt", f"{field} does not match immutable bytes")


def _new_evidence_dir(path: str | os.PathLike[str]) -> Path:
    directory = Path(path)
    if directory.exists() or directory.is_symlink():
        raise NativeCaptureError("output", f"evidence directory already exists: {directory}")
    try:
        directory.mkdir(parents=True, exist_ok=False)
    except FileExistsError as exc:
        raise NativeCaptureError("output", f"evidence directory already exists: {directory}") from exc
    return directory


def _write_new(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as exc:
        raise NativeCaptureError("output", f"refusing to overwrite evidence: {path}") from exc
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
    except BaseException:
        try:
            path.unlink()
        except OSError:
            pass
        raise


def _write_json_new(path: Path, value: Mapping[str, Any]) -> None:
    _write_new(path, _canonical(value))


def _read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as exc:
        raise NativeCaptureError("receipt", f"cannot read JSON {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise NativeCaptureError("receipt", f"JSON object required: {path}")
    return value


def _duration_arg(seconds: float) -> str:
    if seconds <= 0 or not (seconds < float("inf")):
        raise ValueError("duration must be finite and positive")
    if float(seconds).is_integer():
        return f"{int(seconds)}s"
    return f"{seconds:.3f}s"


def _positive_finite(value: float, label: str) -> float:
    try:
        number = float(value)
    except (TypeError, ValueError) as exc:
        raise NativeCaptureError("startup", f"{label} must be a finite positive number") from exc
    if number <= 0 or not (number < float("inf")):
        raise NativeCaptureError("startup", f"{label} must be a finite positive number")
    return number


def _parse_duration_text(text: str) -> float | None:
    """Parse xctrace's human-readable duration strings.

    xctrace 16 emits values such as ``1 minute, 30 seconds`` in the TOC.  The
    parser also accepts its abbreviated forms so fixtures can use ``90s``.
    """
    if not isinstance(text, str):
        return None
    units = {"millisecond": 1e-3, "milliseconds": 1e-3,
             "ms": 1e-3, "second": 1.0, "seconds": 1.0, "s": 1.0,
             "minute": 60.0, "minutes": 60.0, "m": 60.0,
             "hour": 3600.0, "hours": 3600.0, "h": 3600.0}
    total = 0.0
    found = False
    for number, unit in re.findall(r"(\d+(?:\.\d+)?)\s*([A-Za-z]+)", text):
        scale = units.get(unit.lower())
        if scale is not None:
            total += float(number) * scale
            found = True
    return total if found else None


def _duration_label(seconds: float) -> str:
    whole = int(round(seconds))
    minutes, rest = divmod(whole, 60)
    hours, minutes = divmod(minutes, 60)
    parts: list[str] = []
    if hours:
        parts.append(f"{hours} hour" + ("s" if hours != 1 else ""))
    if minutes:
        parts.append(f"{minutes} minute" + ("s" if minutes != 1 else ""))
    if rest or not parts:
        parts.append(f"{rest} second" + ("s" if rest != 1 else ""))
    return ", ".join(parts)


def _tool_command(*args: str) -> list[str]:
    return ["/usr/bin/xcrun", "xctrace", *args]


def _run_command(command: Sequence[str], timeout: float) -> subprocess.CompletedProcess[bytes]:
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                timeout=timeout, check=False)
    except subprocess.TimeoutExpired as exc:
        raise NativeCaptureError("startup", f"command timed out after {timeout:g}s: {' '.join(command)}") from exc
    except OSError as exc:
        raise NativeCaptureError("startup", f"cannot execute {' '.join(command)}: {exc}") from exc
    if result.returncode != 0:
        combined = ((result.stdout or b"") + (result.stderr or b"")).decode("utf-8", "replace")
        # xctrace prints usage and exits 48 when record/export is invoked with
        # no subcommand arguments.  We use those invocations only to capture
        # the installed option set for the configuration receipt.
        usage_probe = (len(command) == 3 and command[:2] == ["/usr/bin/xcrun", "xctrace"]
                       and command[2] in {"record", "export"}
                       and "usage:" in combined.lower())
        if not usage_probe:
            raise NativeCaptureError("startup", f"command exited {result.returncode}: {' '.join(command)}; {combined[-2048:].strip()}")
    return result


def _tool_identity(timeout: float = 20.0,
                   command_runner: Callable[[Sequence[str], float], subprocess.CompletedProcess[bytes]] = _run_command,
                   ) -> dict[str, Any]:
    if platform.system() != "Darwin":
        raise NativeCaptureError("startup", "native xctrace capture requires macOS")
    outputs: dict[str, str] = {}
    for name, args in (
        ("version", ("version",)),
        ("record_help", ("record",)),
        ("export_help", ("export",)),
        ("templates", ("list", "templates")),
    ):
        try:
            result = command_runner(_tool_command(*args), timeout)
        except NativeCaptureError:
            raise
        except (OSError, subprocess.SubprocessError) as exc:
            raise NativeCaptureError("startup", f"xctrace identity command failed: {exc}") from exc
        outputs[name] = ((result.stdout or b"") + (result.stderr or b"")).decode("utf-8", "replace")
    version = outputs["version"].strip().splitlines()[0] if outputs["version"].strip() else ""
    if not version.startswith("xctrace version "):
        raise NativeCaptureError("startup", "xctrace version output is not recognized")
    if "System Trace" not in outputs["templates"]:
        raise NativeCaptureError("startup", "System Trace template is unavailable")
    host = {"os": platform.system(), "os_release": platform.mac_ver()[0],
            "machine": platform.machine(),
            "hostname_sha256": _sha256_bytes(platform.node().encode("utf-8"))}
    helper_sha256 = _sha256_file(Path(__file__))
    fingerprint = {"version": version,
                   "outputs_sha256": {key: _sha256_bytes(value.encode("utf-8"))
                                      for key, value in outputs.items()},
                   "template": DEFAULT_TEMPLATE, "host": host,
                   "helper_sha256": helper_sha256}
    return {
        "version": version,
        "outputs_sha256": fingerprint["outputs_sha256"],
        "host": host,
        "helper_sha256": helper_sha256,
        "tool_identity_sha256": _sha256_bytes(_canonical(fingerprint)),
        "record_help_contains": ["--window", "--time-limit", "--attach", "--notify-tracing-started"],
        "export_help_contains": ["--input", "--output", "--toc", "--xpath"],
        "template": DEFAULT_TEMPLATE,
        "raw_outputs": outputs,
    }


def _configuration(*, template: str, retention_seconds: float,
                   min_retained_seconds: float, scope: str,
                   tool: Mapping[str, Any]) -> dict[str, Any]:
    return {
        "platform": "macOS",
        "template": template,
        "scope": scope,
        "retention_window_seconds": retention_seconds,
        "minimum_retained_seconds": min_retained_seconds,
        "xctrace_version": str(tool.get("version", "")),
        "xctrace_identity_sha256": str(tool.get("tool_identity_sha256", "")),
        "host_identity": tool.get("host"),
        "xctrace_template": template,
        "record_mode": "attach-one-explicit-pid",
        "export_mode": "toc-and-thread-state-xpath",
    }


def _target_alive(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _open_log(path: Path):
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    return os.fdopen(fd, "wb")


def _spawn_notify(token: str, directory: Path):
    stdout = _open_log(directory / "notify.stdout")
    stderr = _open_log(directory / "notify.stderr")
    try:
        proc = subprocess.Popen(["/usr/bin/notifyutil", "-1", token], stdout=stdout, stderr=stderr)
    except OSError:
        stdout.close()
        stderr.close()
        raise
    return proc, stdout, stderr


def _wait_for_ready(profiler, notify, timeout: float, *, sleep: Callable[[float], None] = time.sleep) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if profiler.poll() is not None:
            raise NativeCaptureError("startup", "xctrace exited before readiness notification")
        notify_code = notify.poll()
        if notify_code is not None:
            if notify_code == 0:
                return
            raise NativeCaptureError("startup", f"readiness observer exited {notify_code}")
        sleep(0.05)
    raise NativeCaptureError("startup", f"xctrace readiness exceeded {timeout:g}s")


def _wait_process(proc, timeout: float) -> bool:
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        return False
    return proc.poll() is not None


def _stop_owned_process(proc, *, stop_timeout: float, kill_timeout: float) -> dict[str, Any]:
    """Stop only a Popen object created by this module.

    The returned escalation record is retained in the capture receipt.  The
    caller must never pass the target PID here for an explicit ``record``.
    """
    initial = proc.poll()
    if initial is not None:
        return {"initial_returncode": initial, "signal": None, "escalated": False,
                "terminated": True, "returncode": initial}
    used: str | None = None
    try:
        proc.send_signal(signal.SIGINT)
        used = "SIGINT"
    except (OSError, AttributeError) as exc:
        raise NativeCaptureError("finalization", f"cannot request recorder stop: {exc}") from exc
    if not _wait_process(proc, stop_timeout):
        try:
            proc.terminate()
            used = "SIGTERM"
        except (OSError, AttributeError) as exc:
            raise NativeCaptureError("finalization", f"cannot terminate recorder: {exc}") from exc
        if not _wait_process(proc, kill_timeout):
            try:
                proc.kill()
                used = "SIGKILL"
            except (OSError, AttributeError) as exc:
                raise NativeCaptureError("finalization", f"cannot kill recorder: {exc}") from exc
            if not _wait_process(proc, kill_timeout):
                raise NativeCaptureError("finalization", "recorder remained alive after SIGKILL")
            return {"initial_returncode": None, "signal": used, "escalated": True,
                    "terminated": True, "returncode": proc.poll()}
        return {"initial_returncode": None, "signal": used, "escalated": True,
                "terminated": True, "returncode": proc.poll()}
    return {"initial_returncode": None, "signal": used, "escalated": False,
            "terminated": True, "returncode": proc.poll()}


def _stop_after(proc, seconds: float) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise NativeCaptureError("finalization", "recorder exited before the bounded stop")
        time.sleep(min(0.05, max(0.0, deadline - time.monotonic())))


def _resolve_ref(element: ET.Element | None, refs: Mapping[str, ET.Element]) -> ET.Element | None:
    seen: set[str] = set()
    while element is not None and element.get("ref") is not None:
        ref = element.get("ref")
        if ref in seen or ref not in refs:
            raise NativeCaptureError("coverage", "invalid xctrace XML reference")
        seen.add(ref)
        element = refs[ref]
    return element


def _element_text(element: ET.Element | None, refs: Mapping[str, ET.Element]) -> str | None:
    element = _resolve_ref(element, refs)
    return element.text if element is not None else None


def _xml_rows(states: Path, expected_pid: int) -> list[tuple[int, int]]:
    try:
        root = ET.parse(states).getroot()
    except (OSError, ET.ParseError) as exc:
        raise NativeCaptureError("export", f"cannot parse exported thread-state XML: {exc}") from exc
    refs = {element.get("id"): element for element in root.iter()
            if element.get("id") is not None}
    rows: list[tuple[int, int]] = []
    for row in root.iter("row"):
        process = _resolve_ref(row.find("process"), refs)
        pid_text = _element_text(process.find("pid") if process is not None else None, refs)
        if pid_text is None:
            continue
        try:
            pid = int(pid_text)
        except ValueError:
            continue
        if pid != expected_pid:
            continue
        start_text = _element_text(row.find("start-time"), refs)
        duration_text = _element_text(row.find("duration"), refs)
        if start_text is None or duration_text is None:
            continue
        try:
            start = int(start_text)
            duration = int(duration_text)
        except ValueError:
            continue
        if start < 0 or duration <= 0:
            continue
        rows.append((start, start + duration))
    if not rows:
        raise NativeCaptureError("coverage", f"export contains no thread-state rows for owned PID {expected_pid}")
    return rows


def _toc_info(toc: Path, expected_pid: int) -> dict[str, Any]:
    try:
        root = ET.parse(toc).getroot()
    except (OSError, ET.ParseError) as exc:
        raise NativeCaptureError("export", f"cannot parse xctrace TOC XML: {exc}") from exc
    run = root.find("./run")
    if run is None:
        raise NativeCaptureError("coverage", "xctrace TOC has no run")
    process = run.find("./info/target/process")
    if process is None or process.get("pid") is None:
        raise NativeCaptureError("coverage", "xctrace TOC has no attached target PID")
    try:
        attached_pid = int(process.get("pid"))
    except ValueError as exc:
        raise NativeCaptureError("coverage", "xctrace TOC target PID is malformed") from exc
    if attached_pid != expected_pid:
        raise NativeCaptureError("coverage", f"TOC target PID {attached_pid} does not match {expected_pid}")
    summary = run.find("./info/summary")
    if summary is None:
        raise NativeCaptureError("coverage", "xctrace TOC has no recording summary")
    duration_text = summary.findtext("duration")
    duration = float(duration_text) if duration_text else 0.0
    mode = summary.findtext("recording-mode") or ""
    time_limit = summary.findtext("time-limit") or ""
    mode_duration = _parse_duration_text(mode)
    limit_duration = _parse_duration_text(time_limit)
    return {"pid": attached_pid, "duration_seconds": duration,
            "recording_mode": mode, "recording_window_seconds": mode_duration,
            "time_limit": time_limit, "time_limit_seconds": limit_duration,
            "start_date": summary.findtext("start-date"),
            "end_date": summary.findtext("end-date"),
            "end_reason": summary.findtext("end-reason")}


def _union_range(intervals: Iterable[tuple[int, int]]) -> tuple[int, int, float]:
    ordered = sorted(intervals)
    first, last = ordered[0]
    covered = 0
    for start, end in ordered[1:]:
        if start <= last:
            last = max(last, end)
        else:
            covered += last - first
            first, last = start, end
    covered += last - first
    return min(start for start, _ in ordered), max(end for _, end in ordered), covered / 1e9


def validate_exported_coverage(toc: str | os.PathLike[str], states: str | os.PathLike[str], *,
                               expected_pid: int, expected_window_seconds: float,
                               min_retained_seconds: float = DEFAULT_MIN_RETAINED_SECONDS,
                               required_range_seconds: tuple[float, float] | None = None,
                               ) -> dict[str, Any]:
    """Validate target identity, explicit rolling window, and retained rows."""
    info = _toc_info(Path(toc), expected_pid)
    if info["recording_window_seconds"] is None:
        raise NativeCaptureError("coverage", "TOC does not report a parseable recording window")
    if abs(info["recording_window_seconds"] - expected_window_seconds) > 0.01:
        raise NativeCaptureError("coverage", f"TOC retained window {info['recording_window_seconds']:g}s does not match requested {expected_window_seconds:g}s")
    if info["duration_seconds"] <= 0:
        raise NativeCaptureError("coverage", "TOC reports an empty recording")
    rows = _xml_rows(Path(states), expected_pid)
    start_ns, end_ns, covered_seconds = _union_range(rows)
    retained_seconds = (end_ns - start_ns) / 1e9
    if retained_seconds < min_retained_seconds:
        raise NativeCaptureError("coverage", f"owned target range is {retained_seconds:g}s, below minimum {min_retained_seconds:g}s")
    required = None
    if required_range_seconds is not None:
        required = tuple(float(x) for x in required_range_seconds)
        if len(required) != 2 or not (0 <= required[0] < required[1] < float("inf")):
            raise NativeCaptureError("coverage", "required range must be positive start:end seconds")
        required_ns = (int(required[0] * 1e9), int(required[1] * 1e9))
        cursor = required_ns[0]
        for start, end in sorted(rows):
            if end < cursor:
                continue
            if start > cursor:
                break
            cursor = max(cursor, end)
            if cursor >= required_ns[1]:
                break
        if cursor < required_ns[1]:
            raise NativeCaptureError("coverage", f"owned target rows do not cover required range {required[0]:g}:{required[1]:g}s")
    return {"target_pid": expected_pid, "toc_duration_seconds": info["duration_seconds"],
            "recording_window_seconds": info["recording_window_seconds"],
            "retained_start_ns": start_ns, "retained_end_ns": end_ns,
            "retained_seconds": retained_seconds, "covered_seconds": covered_seconds,
            "row_count": len(rows), "required_range_seconds": list(required) if required is not None else None,
            "toc_start_date": info["start_date"], "toc_end_date": info["end_date"],
            "toc_end_reason": info["end_reason"]}


def _export_recording(trace: Path, directory: Path, *, timeout: float,
                      command_runner: Callable[[Sequence[str], float], subprocess.CompletedProcess[bytes]] = _run_command,
                      ) -> tuple[Path, Path]:
    toc = directory / "native-toc.xml"
    states = directory / "native-thread-state.xml"
    for path, args in (
        (toc, ("export", "--input", str(trace), "--toc", "--output", str(toc))),
        (states, ("export", "--input", str(trace), "--xpath",
                  '/trace-toc/run[@number="1"]/data/table[@schema="thread-state"]',
                  "--output", str(states))),
    ):
        try:
            result = command_runner(_tool_command(*args), timeout)
        except NativeCaptureError as exc:
            raise NativeCaptureError("export", str(exc)) from exc
        if not path.exists() or path.stat().st_size == 0:
            raise NativeCaptureError("export", f"xctrace export produced no bytes: {path.name}")
        # Keep small command diagnostics without replacing xctrace's XML.
        if result.stderr:
            diagnostic = directory / f"{path.stem}.stderr"
            _write_new(diagnostic, result.stderr[-MAX_LOG_BYTES:])
    return toc, states


def _owned_probe_command(duration: float) -> list[str]:
    code = (
        "import math,sys,time\n"
        "end=time.monotonic()+float(sys.argv[1])\n"
        "x=0.0\n"
        "while time.monotonic()<end:\n"
        "    x=math.sin(x+0.001)\n"
        "    time.sleep(0.001)\n"
    )
    return [sys.executable, "-c", code, f"{duration:.3f}"]


def _write_failure(directory: Path, *, phase: str, error: BaseException, context: Mapping[str, Any]) -> None:
    failure = {"schema": "melee-web-native-capture-failure-v1", "phase": phase,
               "error": str(error), "context": dict(context), "failed_utc_ms": int(time.time() * 1000)}
    try:
        _write_json_new(directory / "failure.json", failure)
    except NativeCaptureError:
        pass


def _run_session(*, directory: Path, command: Sequence[str], target_pid: int,
                 time_limit_seconds: float, readiness_timeout: float,
                 stop_timeout: float, kill_timeout: float,
                 stop_after_seconds: float | None,
                 own_target: Any = None,
                 ) -> dict[str, Any]:
    """Run xctrace, using only handles created here for cleanup."""
    trace = directory / "native.trace"
    stdout_file = _open_log(directory / "xctrace.stdout")
    stderr_file = _open_log(directory / "xctrace.stderr")
    profiler = None
    notify = None
    notify_stdout = notify_stderr = None
    token = f"com.melee-web.native-capture.{uuid.uuid4()}"
    full_command = list(command) + ["--notify-tracing-started", token, "--output", str(trace)]
    session: dict[str, Any] = {"target_pid": target_pid, "command": full_command,
                               "readiness_timeout_seconds": readiness_timeout,
                               "time_limit_seconds": time_limit_seconds,
                               "stop_after_seconds": stop_after_seconds,
                               "started_utc_ms": int(time.time() * 1000),
                               "ready_utc_ms": None, "stop_requested_utc_ms": None,
                               "finished_utc_ms": None, "escalated": False}
    try:
        try:
            notify, notify_stdout, notify_stderr = _spawn_notify(token, directory)
            # notifyutil must be registered before xctrace starts.
            time.sleep(0.1)
            profiler = subprocess.Popen(full_command, stdout=stdout_file, stderr=stderr_file)
        except OSError as exc:
            raise NativeCaptureError("startup", f"cannot start native recorder: {exc}") from exc
        try:
            _wait_for_ready(profiler, notify, readiness_timeout)
            session["ready_utc_ms"] = int(time.time() * 1000)
            if stop_after_seconds is not None:
                _stop_after(profiler, stop_after_seconds)
                session["stop_requested_utc_ms"] = int(time.time() * 1000)
                finalization = _stop_owned_process(profiler, stop_timeout=stop_timeout,
                                                  kill_timeout=kill_timeout)
            else:
                if not _wait_process(profiler, time_limit_seconds + stop_timeout):
                    session["stop_requested_utc_ms"] = int(time.time() * 1000)
                    finalization = _stop_owned_process(profiler, stop_timeout=stop_timeout,
                                                      kill_timeout=kill_timeout)
                else:
                    finalization = {"initial_returncode": profiler.returncode,
                                    "signal": None, "escalated": False,
                                    "terminated": True, "returncode": profiler.returncode}
            session["finalization"] = finalization
            session["escalated"] = bool(finalization.get("escalated"))
            session["returncode"] = finalization.get("returncode")
            # xctrace 16 returns 2 for a clean user-stop (SIGINT) while the
            # TOC records end-reason "User pressed Stop".  SIGTERM/SIGKILL
            # escalation remains a hard finalization failure.
            clean_user_stop = (finalization.get("signal") == "SIGINT"
                               and session["returncode"] == 2)
            session["clean_user_stop"] = clean_user_stop
            if session["escalated"] or (session["returncode"] not in (0, None) and not clean_user_stop):
                raise NativeCaptureError("finalization", f"recorder finalization failed: {finalization}")
        except BaseException:
            if profiler is not None and profiler.poll() is None:
                try:
                    finalization = _stop_owned_process(profiler, stop_timeout=stop_timeout,
                                                      kill_timeout=kill_timeout)
                    session["finalization"] = finalization
                    session["escalated"] = bool(finalization.get("escalated"))
                    session["returncode"] = finalization.get("returncode")
                except NativeCaptureError as stop_error:
                    session["finalization_error"] = str(stop_error)
            raise
    finally:
        if profiler is not None and profiler.poll() is None:
            try:
                _stop_owned_process(profiler, stop_timeout=stop_timeout, kill_timeout=kill_timeout)
            except NativeCaptureError as error:
                session["finalization_error"] = str(error)
        if notify is not None and notify.poll() is None:
            try:
                _stop_owned_process(notify, stop_timeout=min(stop_timeout, 2.0),
                                    kill_timeout=min(kill_timeout, 2.0))
            except (NativeCaptureError, OSError):
                pass
        for stream in (notify_stdout, notify_stderr, stdout_file, stderr_file):
            if stream is not None:
                stream.close()
        # The preflight probe is ours.  Explicit --pid targets never arrive in
        # own_target and therefore cannot be signalled here.
        if own_target is not None and own_target.poll() is None:
            try:
                _stop_owned_process(own_target, stop_timeout=min(stop_timeout, 2.0),
                                    kill_timeout=min(kill_timeout, 2.0))
            except NativeCaptureError:
                pass
        session["finished_utc_ms"] = int(time.time() * 1000)
    if not trace.exists() or trace.stat().st_size == 0:
        raise NativeCaptureError("finalization", "xctrace did not produce a trace file")
    return session


def _receipt_files(directory: Path) -> dict[str, Any]:
    files: dict[str, Any] = {}
    for path in sorted(directory.iterdir()):
        if path.name in {"preflight.json", "capture.json"} or not (path.is_file() or path.is_dir()):
            continue
        files[path.name] = _artifact_identity(path)
    return files


def run_preflight(*, output: str | os.PathLike[str], template: str = DEFAULT_TEMPLATE,
                  retention_seconds: float = DEFAULT_RETENTION_SECONDS,
                  probe_seconds: float = DEFAULT_PROBE_SECONDS,
                  min_retained_seconds: float = DEFAULT_MIN_RETAINED_SECONDS,
                  readiness_timeout: float = DEFAULT_READINESS_TIMEOUT,
                  stop_timeout: float = DEFAULT_STOP_TIMEOUT,
                  kill_timeout: float = DEFAULT_KILL_TIMEOUT,
                  export_timeout: float = DEFAULT_EXPORT_TIMEOUT,
                  valid_for_seconds: float = DEFAULT_VALID_FOR_SECONDS,
                  ) -> dict[str, Any]:
    if template != DEFAULT_TEMPLATE:
        raise NativeCaptureError("startup", f"this preflight requires the {DEFAULT_TEMPLATE!r} template")
    retention_seconds = _positive_finite(retention_seconds, "retention window")
    probe_seconds = _positive_finite(probe_seconds, "probe duration")
    min_retained_seconds = _positive_finite(min_retained_seconds, "minimum retained duration")
    valid_for_seconds = _positive_finite(valid_for_seconds, "receipt validity")
    directory = _new_evidence_dir(output)
    context: dict[str, Any] = {"template": template, "retention_window_seconds": retention_seconds,
                               "probe_seconds": probe_seconds, "scope": DEFAULT_SCOPE}
    target = None
    probe_stdout = probe_stderr = None
    try:
        tool = _tool_identity()
        config = _configuration(template=template, retention_seconds=retention_seconds,
                                min_retained_seconds=min_retained_seconds,
                                scope=DEFAULT_SCOPE, tool=tool)
        config_record = {"schema": "melee-web-native-capture-configuration-v1",
                         "configuration": config,
                         "configuration_sha256": _sha256_bytes(_canonical(config))}
        _write_json_new(directory / "configuration.json", config_record)
        for name, value in tool["raw_outputs"].items():
            _write_new(directory / f"xctrace-{name}.txt", value.encode("utf-8"))
        workload_duration = max(probe_seconds + readiness_timeout + 3.0, 5.0)
        probe_command = _owned_probe_command(workload_duration)
        probe_stdout = _open_log(directory / "probe.stdout")
        probe_stderr = _open_log(directory / "probe.stderr")
        try:
            target = subprocess.Popen(probe_command, stdout=probe_stdout, stderr=probe_stderr)
        except OSError as exc:
            probe_stdout.close()
            probe_stderr.close()
            raise NativeCaptureError("startup", f"cannot start owned probe: {exc}") from exc
        try:
            if not _target_alive(target.pid):
                raise NativeCaptureError("startup", "owned probe exited before recorder attach")
            base_command = _tool_command("record", "--no-prompt", "--template", template, "--attach", str(target.pid),
                                         "--time-limit", _duration_arg(max(retention_seconds, probe_seconds + 3)),
                                         "--window", _duration_arg(retention_seconds))
            session = _run_session(directory=directory, command=base_command, target_pid=target.pid,
                                   time_limit_seconds=max(retention_seconds, probe_seconds + 3),
                                   readiness_timeout=readiness_timeout, stop_timeout=stop_timeout,
                                   kill_timeout=kill_timeout, stop_after_seconds=probe_seconds,
                                   own_target=target)
        finally:
            probe_stdout.close()
            probe_stderr.close()
        trace = directory / "native.trace"
        toc, states = _export_recording(trace, directory, timeout=export_timeout)
        coverage = validate_exported_coverage(toc, states, expected_pid=target.pid,
                                              expected_window_seconds=retention_seconds,
                                              min_retained_seconds=min_retained_seconds)
        receipt = {
            "schema": SCHEMA, "version": RECEIPT_VERSION, "status": "passed",
            "created_utc_ms": int(time.time() * 1000),
            "expires_utc_ms": int((time.time() + valid_for_seconds) * 1000),
            "scope": {"id": DEFAULT_SCOPE, "kind": "one-attached-process",
                      "target": "owned non-game probe for preflight; later capture may attach one explicit PID",
                      "evidence": "System Trace TOC and target thread-state rows",
                      "excludes": ["gameplay correctness", "GPU execution time", "browser timing", "all-process attribution"]},
            "configuration": config,
            "configuration_sha256": config_record["configuration_sha256"],
            "tool": {key: value for key, value in tool.items() if key != "raw_outputs"},
            "probe": {"role": "owned-non-game-process", "command_identity_sha256": _sha256_bytes(_canonical(probe_command[1:])),
                      "target_pid": target.pid, "workload_seconds": workload_duration},
            "session": session,
            "coverage": coverage,
            "evidence_files": _receipt_files(directory),
        }
        receipt = _seal(receipt, "receipt_sha256")
        _write_json_new(directory / "preflight.json", receipt)
        return receipt
    except NativeCaptureError as exc:
        _write_failure(directory, phase=exc.phase, error=exc, context=context)
        raise
    except BaseException as exc:
        _write_failure(directory, phase="startup", error=exc, context=context)
        raise NativeCaptureError("startup", str(exc)) from exc
    finally:
        # Cover errors before _run_session receives the owned probe. An
        # explicit --pid capture never passes a target here.
        if target is not None and target.poll() is None:
            try:
                _stop_owned_process(target, stop_timeout=min(stop_timeout, 2.0),
                                    kill_timeout=min(kill_timeout, 2.0))
            except NativeCaptureError:
                pass
        for stream in (probe_stdout, probe_stderr):
            if stream is not None and not stream.closed:
                stream.close()


def validate_preflight_receipt(path: str | os.PathLike[str], *, template: str = DEFAULT_TEMPLATE,
                               retention_seconds: float = DEFAULT_RETENTION_SECONDS,
                               scope: str = DEFAULT_SCOPE,
                               now_ms: int | None = None,
                               current_tool: Mapping[str, Any] | None = None,
                               ) -> dict[str, Any]:
    retention_seconds = _positive_finite(retention_seconds, "retention window")
    receipt_path = Path(path)
    receipt = _read_json(receipt_path)
    if receipt.get("schema") != SCHEMA or receipt.get("version") != RECEIPT_VERSION:
        raise NativeCaptureError("receipt", "unsupported preflight receipt schema")
    _verify_seal(receipt, "receipt_sha256")
    if receipt.get("status") != "passed":
        raise NativeCaptureError("receipt", "preflight receipt is not passed")
    expiry = receipt.get("expires_utc_ms")
    if not isinstance(expiry, int):
        raise NativeCaptureError("receipt", "preflight receipt has no expiry")
    if (int(time.time() * 1000) if now_ms is None else now_ms) >= expiry:
        raise NativeCaptureError("receipt", "preflight receipt has expired")
    scope_record = receipt.get("scope")
    if not isinstance(scope_record, dict) or scope_record.get("id") != scope:
        raise NativeCaptureError("receipt", "preflight scope does not match requested capture scope")
    config = receipt.get("configuration")
    if not isinstance(config, dict):
        raise NativeCaptureError("receipt", "preflight configuration is missing")
    if config.get("template") != template:
        raise NativeCaptureError("receipt", "capture template does not match preflight")
    try:
        receipt_retention = float(config.get("retention_window_seconds", -1))
        minimum_retained = float(config.get("minimum_retained_seconds", -1))
    except (TypeError, ValueError) as exc:
        raise NativeCaptureError("receipt", "preflight duration identity is malformed") from exc
    if not (receipt_retention > 0 and receipt_retention < float("inf")
            and minimum_retained > 0 and minimum_retained < float("inf")):
        raise NativeCaptureError("receipt", "preflight duration identity is not finite and positive")
    if abs(receipt_retention - retention_seconds) > 0.01:
        raise NativeCaptureError("receipt", "capture retention window does not match preflight")
    expected_config_hash = _sha256_bytes(_canonical(config))
    if receipt.get("configuration_sha256") != expected_config_hash:
        raise NativeCaptureError("receipt", "preflight configuration identity is invalid")
    if current_tool is not None:
        recorded_tool = receipt.get("tool")
        if not isinstance(recorded_tool, dict):
            raise NativeCaptureError("receipt", "preflight tool identity is missing")
        for key in ("version", "outputs_sha256", "host", "helper_sha256", "tool_identity_sha256"):
            if recorded_tool.get(key) != current_tool.get(key):
                raise NativeCaptureError("receipt", f"current xctrace {key} does not match preflight")
    if not isinstance(config.get("xctrace_identity_sha256"), str) or not config["xctrace_identity_sha256"]:
        raise NativeCaptureError("receipt", "preflight xctrace identity is missing")
    if not isinstance(config.get("host_identity"), dict):
        raise NativeCaptureError("receipt", "preflight host identity is missing")
    coverage = receipt.get("coverage")
    try:
        retained = float(coverage.get("retained_seconds", 0)) if isinstance(coverage, dict) else 0.0
    except (TypeError, ValueError) as exc:
        raise NativeCaptureError("receipt", "preflight retained range is malformed") from exc
    if not (retained >= minimum_retained and retained < float("inf")):
        raise NativeCaptureError("receipt", "preflight has no retained target coverage")
    evidence_dir = receipt_path.parent
    inventory = receipt.get("evidence_files")
    if not isinstance(inventory, dict):
        raise NativeCaptureError("receipt", "preflight evidence inventory is missing")
    required = {"configuration.json", "native.trace", "native-toc.xml", "native-thread-state.xml"}
    if not required.issubset(inventory):
        raise NativeCaptureError("receipt", "preflight evidence inventory is incomplete")
    for name, identity in inventory.items():
        if not isinstance(name, str) or name in {"", ".", ".."} or Path(name).name != name:
            raise NativeCaptureError("receipt", "preflight evidence names must be plain basenames")
        file_path = evidence_dir / name
        if not isinstance(identity, dict) or not _artifact_matches(file_path, identity):
            raise NativeCaptureError("receipt", f"preflight evidence changed or is missing: {name}")
    return receipt


def run_capture(*, pid: int, output: str | os.PathLike[str], receipt: str | os.PathLike[str],
                template: str = DEFAULT_TEMPLATE, retention_seconds: float = DEFAULT_RETENTION_SECONDS,
                time_limit_seconds: float = 300.0, stop_after_seconds: float | None = None,
                min_retained_seconds: float | None = None,
                required_range_seconds: tuple[float, float] | None = None,
                readiness_timeout: float = DEFAULT_READINESS_TIMEOUT,
                stop_timeout: float = DEFAULT_STOP_TIMEOUT,
                kill_timeout: float = DEFAULT_KILL_TIMEOUT,
                export_timeout: float = DEFAULT_EXPORT_TIMEOUT,
                ) -> dict[str, Any]:
    if template != DEFAULT_TEMPLATE:
        raise NativeCaptureError("startup", f"this capture requires the {DEFAULT_TEMPLATE!r} template")
    if pid <= 0 or not _target_alive(pid):
        raise NativeCaptureError("startup", f"explicit target PID is not alive: {pid}")
    preflight = validate_preflight_receipt(receipt, template=template,
                                           retention_seconds=retention_seconds)
    current_tool = _tool_identity()
    preflight = validate_preflight_receipt(receipt, template=template,
                                           retention_seconds=retention_seconds,
                                           current_tool=current_tool)
    time_limit_seconds = _positive_finite(time_limit_seconds, "time limit")
    if time_limit_seconds > 24 * 60 * 60:
        raise NativeCaptureError("startup", "time limit must be positive and at most 24 hours")
    if stop_after_seconds is not None:
        stop_after_seconds = _positive_finite(stop_after_seconds, "stop-after")
    if stop_after_seconds is not None and stop_after_seconds > time_limit_seconds:
        raise NativeCaptureError("startup", "stop-after must be positive and no greater than time-limit")
    declared_minimum = float(preflight["configuration"]["minimum_retained_seconds"])
    if min_retained_seconds is None:
        min_retained_seconds = declared_minimum
    else:
        min_retained_seconds = _positive_finite(min_retained_seconds, "minimum retained duration")
        if min_retained_seconds < declared_minimum:
            raise NativeCaptureError("startup", "capture minimum retained duration is below preflight declaration")
    directory = _new_evidence_dir(output)
    context = {"template": template, "retention_window_seconds": retention_seconds,
               "target_pid": pid, "scope": DEFAULT_SCOPE}
    try:
        base_command = _tool_command("record", "--no-prompt", "--template", template, "--attach", str(pid),
                                     "--time-limit", _duration_arg(time_limit_seconds),
                                     "--window", _duration_arg(retention_seconds))
        session = _run_session(directory=directory, command=base_command, target_pid=pid,
                               time_limit_seconds=time_limit_seconds, readiness_timeout=readiness_timeout,
                               stop_timeout=stop_timeout, kill_timeout=kill_timeout,
                               stop_after_seconds=stop_after_seconds)
        toc, states = _export_recording(directory / "native.trace", directory, timeout=export_timeout)
        coverage = validate_exported_coverage(toc, states, expected_pid=pid,
                                              expected_window_seconds=retention_seconds,
                                              min_retained_seconds=min_retained_seconds,
                                              required_range_seconds=required_range_seconds)
        result = {"schema": CAPTURE_SCHEMA, "version": 1, "status": "passed",
                  "created_utc_ms": int(time.time() * 1000), "scope": DEFAULT_SCOPE,
                  "preflight_receipt": Path(receipt).name,
                  "preflight_receipt_sha256": _sha256_file(Path(receipt)),
                  "configuration_sha256": preflight["configuration_sha256"],
                  "target_pid": pid, "session": session, "coverage": coverage,
                  "evidence_files": _receipt_files(directory)}
        result = _seal(result, "capture_sha256")
        _write_json_new(directory / "capture.json", result)
        return result
    except NativeCaptureError as exc:
        _write_failure(directory, phase=exc.phase, error=exc, context=context)
        raise

    except BaseException as exc:
        _write_failure(directory, phase="capture", error=exc, context=context)
        raise NativeCaptureError("capture", str(exc)) from exc


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    pre = sub.add_parser("preflight", help="run an owned non-game retention preflight")
    pre.add_argument("--output", required=True, type=Path)
    pre.add_argument("--template", default=DEFAULT_TEMPLATE)
    pre.add_argument("--retention-window", type=float, default=DEFAULT_RETENTION_SECONDS, dest="retention")
    pre.add_argument("--probe-seconds", type=float, default=DEFAULT_PROBE_SECONDS, dest="probe")
    pre.add_argument("--min-retained-seconds", type=float, default=DEFAULT_MIN_RETAINED_SECONDS, dest="minimum")
    pre.add_argument("--valid-for-seconds", type=float, default=DEFAULT_VALID_FOR_SECONDS, dest="valid_for")
    val = sub.add_parser("validate", help="validate a preflight receipt for a capture configuration")
    val.add_argument("--receipt", required=True, type=Path)
    val.add_argument("--template", default=DEFAULT_TEMPLATE)
    val.add_argument("--retention-window", type=float, default=DEFAULT_RETENTION_SECONDS, dest="retention")
    val.add_argument("--scope", default=DEFAULT_SCOPE)
    rec = sub.add_parser("record", help="capture one explicit PID after receipt validation")
    rec.add_argument("--pid", required=True, type=int)
    rec.add_argument("--output", required=True, type=Path)
    rec.add_argument("--preflight-receipt", required=True, type=Path, dest="receipt")
    rec.add_argument("--template", default=DEFAULT_TEMPLATE)
    rec.add_argument("--retention-window", type=float, default=DEFAULT_RETENTION_SECONDS, dest="retention")
    rec.add_argument("--time-limit", type=float, default=300.0, dest="time_limit")
    rec.add_argument("--min-retained-seconds", type=float, default=None, dest="minimum")
    rec.add_argument("--stop-after", type=float, default=None, dest="stop_after")
    rec.add_argument("--coverage-range", type=str, default=None, dest="coverage_range")
    return parser


def _range_arg(value: str | None) -> tuple[float, float] | None:
    if value is None:
        return None
    try:
        start, end = (float(part) for part in value.split(":", 1))
    except (ValueError, TypeError) as exc:
        raise argparse.ArgumentTypeError("coverage range must be START:END seconds") from exc
    if not (math.isfinite(start) and math.isfinite(end)) or start < 0 or end <= start:
        raise argparse.ArgumentTypeError("coverage range must be START:END with END > START")
    return start, end


def main(argv: Sequence[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.command == "preflight":
            result = run_preflight(output=args.output, template=args.template,
                                   retention_seconds=args.retention, probe_seconds=args.probe,
                                   min_retained_seconds=args.minimum, valid_for_seconds=args.valid_for)
        elif args.command == "validate":
            tool = _tool_identity()
            result = validate_preflight_receipt(args.receipt, template=args.template,
                                                retention_seconds=args.retention, scope=args.scope,
                                                current_tool=tool)
        else:
            result = run_capture(pid=args.pid, output=args.output, receipt=args.receipt,
                                 template=args.template, retention_seconds=args.retention,
                                 time_limit_seconds=args.time_limit, stop_after_seconds=args.stop_after,
                                 min_retained_seconds=args.minimum,
                                 required_range_seconds=_range_arg(args.coverage_range))
    except (NativeCaptureError, argparse.ArgumentTypeError) as exc:
        print(json.dumps({"status": "failed", "error": str(exc)}, sort_keys=True), file=sys.stderr)
        return 2
    print(json.dumps({"status": result.get("status", "passed"),
                      "schema": result.get("schema"),
                      "configuration_sha256": result.get("configuration_sha256"),
                      "coverage": result.get("coverage")}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
