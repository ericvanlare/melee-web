#!/usr/bin/env python3
"""Drive a bounded ordinary two-port Dolphin Pipe sequence.

This helper attaches to an already launched reference Dolphin.  It only writes
the same ordinary controller Pipe packets that a human controller would send
and reads the passive MWRO observer.  The observer stream is the authority for
scene boundaries; this program never reads or writes guest memory and never
retries a failed input write or a failed sequence.

The launch environment must set ``MWRC_WHOLE_SESSION_MATCHES`` to the selected
inventory length and must point ``MWRC_OUTPUT`` and ``MWRC_STATUS`` at fresh
paths.  A private Dolphin profile can be prepared with ``--prepare-pipes``.
Every run must supply ``--recipe``: CSS/SSS selections are ordinary inputs and
cannot be inferred from the inventory without weakening the evidence boundary.
"""
from __future__ import annotations

import argparse
import configparser
from dataclasses import dataclass
import errno
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
from typing import Any, Callable, Iterable

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "reference-capture" / "dolphin"))
from reference_observer_stream import (  # noqa: E402
    BOUNDARY_NAMES,
    HEADER,
    MAX_PAYLOAD,
    _decode_record,
    read_status,
)

sys.path.insert(0, str(ROOT / "tools"))
from retail_input_plan import BUTTONS, NEUTRAL_PAD, PAD, pipe_commands  # noqa: E402


class CaptureError(RuntimeError):
    """A sequence cannot be admitted as the declared ordinary-input run."""


BUTTON_BITS = {name: bit for bit, name in BUTTONS}
BUTTON_BITS.update({"START": BUTTON_BITS["START"], "Start": BUTTON_BITS["START"]})
WHOLE_BOUNDARIES = set(BOUNDARY_NAMES.values())
IGNORE_BOUNDARIES = {"pad_poll", "pad_consume", "fighter_create", "source_tick",
                     "draw_enter", "draw_return", "entry", "setup", "result_enter",
                     "result_return", "scene_exit"}
STARTUP_PRIZE_ORDER = ("prize_mode_enter", "prize_scene_enter",
                       "prize_scene_exit", "startup_prize_mode_exit")
SOURCE_STATE_IGNORED = {"pad_poll", "pad_consume", "fighter_create", "source_tick",
                        "draw_enter", "draw_return"}

SEQUENCE_DOC = ROOT / "docs" / "evidence" / "versus-sequences-v1.json"


def _json_object(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CaptureError(f"cannot read JSON recipe {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise CaptureError(f"JSON recipe {path} must contain an object")
    return value


def load_inventory(name: str, path: Path = SEQUENCE_DOC) -> list[dict[str, Any]]:
    """Load and validate one of the predeclared whole-session inventories."""

    document = _json_object(path)
    if document.get("schema") != "melee-web-versus-sequences-v1":
        raise CaptureError("unsupported versus sequence evidence schema")
    if document.get("status") != "predeclared":
        raise CaptureError("versus sequence evidence is not predeclared")
    inventories = document.get("inventories")
    if not isinstance(inventories, list):
        raise CaptureError("versus sequence evidence has no inventory list")
    selected = next((row for row in inventories
                     if isinstance(row, dict) and row.get("name") == name), None)
    if selected is None or not isinstance(selected.get("matches"), list):
        raise CaptureError(f"unknown versus sequence inventory: {name}")
    matches = selected["matches"]
    if not 3 <= len(matches) <= 64:
        raise CaptureError("whole-session inventory must contain 3..64 matches")
    checked: list[dict[str, Any]] = []
    for index, match in enumerate(matches):
        if not isinstance(match, dict) or set(match) != {"p1", "p2", "stage", "label"}:
            raise CaptureError(f"inventory {name} match {index} has an invalid shape")
        for key in ("p1", "p2", "stage"):
            value = match[key]
            if type(value) is not int or not 0 <= value <= 0xFF:
                raise CaptureError(f"inventory {name} match {index} has invalid {key}")
        if not isinstance(match["label"], str) or not match["label"]:
            raise CaptureError(f"inventory {name} match {index} has no label")
        checked.append(dict(match))
    return checked


def raw_pad(*, buttons: Iterable[str] = (), x: int = 0, y: int = 0,
            cx: int = 0, cy: int = 0, left: int = 0, right: int = 0) -> str:
    """Encode one representable raw PAD sample for the Pipe translator."""

    mask = 0
    for name in buttons:
        if name not in BUTTON_BITS:
            raise CaptureError(f"unknown controller button {name}")
        mask |= BUTTON_BITS[name]
    values = (x, y, cx, cy)
    if any(type(value) is not int or not -127 <= value <= 127 for value in values):
        raise CaptureError("stick axes must be integer bytes in [-127,127]")
    if type(left) is not int or type(right) is not int or not 0 <= left <= 255 or not 0 <= right <= 255:
        raise CaptureError("trigger axes must be integer bytes in [0,255]")
    if mask & BUTTON_BITS["L"] and left != 255:
        left = 255
    if mask & BUTTON_BITS["R"] and right != 255:
        right = 255
    return PAD.pack(mask, x, y, cx, cy, left, right, 0, 0, 0).hex()


def _action_pad(value: Any, *, port: int) -> str:
    if not isinstance(value, dict):
        raise CaptureError(f"recipe port {port} action must be an object")
    allowed = {"buttons", "x", "y", "cx", "cy", "left", "right"}
    if set(value) - allowed:
        raise CaptureError(f"recipe port {port} has unknown fields")
    buttons = value.get("buttons", [])
    if not isinstance(buttons, list) or any(not isinstance(button, str) for button in buttons):
        raise CaptureError(f"recipe port {port} buttons must be a string list")
    return raw_pad(buttons=buttons, x=value.get("x", 0), y=value.get("y", 0),
                   cx=value.get("cx", 0), cy=value.get("cy", 0),
                   left=value.get("left", 0), right=value.get("right", 0))


@dataclass(frozen=True)
class InputAction:
    p1: str
    p2: str
    seconds: float
    label: str


def _decode_actions(value: Any, *, context: str) -> list[InputAction]:
    if not isinstance(value, list) or not value:
        raise CaptureError(f"{context} must contain at least one ordinary input action")
    actions: list[InputAction] = []
    for index, row in enumerate(value):
        if not isinstance(row, dict) or set(row) - {"p1", "p2", "seconds", "label"}:
            raise CaptureError(f"{context} action {index} has an invalid shape")
        seconds = row.get("seconds", 0.12)
        if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or not 0.01 <= seconds <= 5.0:
            raise CaptureError(f"{context} action {index} has an invalid duration")
        label = row.get("label", f"{context}-{index}")
        if not isinstance(label, str) or not label:
            raise CaptureError(f"{context} action {index} has an invalid label")
        actions.append(InputAction(_action_pad(row.get("p1", {}), port=1),
                                   _action_pad(row.get("p2", {}), port=2),
                                   float(seconds), label))
    return actions


def load_recipe(path: Path | None, sequence: str, match_count: int) -> list[dict[str, list[InputAction]]]:
    """Decode ordinary CSS/SSS actions; no source state is inferred."""

    if path is None:
        raise CaptureError(
            "an explicit ordinary CSS/SSS --recipe is required; scheduler cannot infer "
            "Mario/Final Destination selection")
    document = _json_object(path)
    if document.get("sequence") not in (None, sequence):
        raise CaptureError("ordinary input recipe sequence does not match --sequence")
    if "matches" in document:
        rows = document["matches"]
        if not isinstance(rows, list) or len(rows) != match_count:
            raise CaptureError("recipe match count does not match the selected inventory")
    else:
        rows = [document] * match_count
    decoded: list[dict[str, list[InputAction]]] = []
    for index, row in enumerate(rows):
        if not isinstance(row, dict) or set(row) - {"sequence", "matches", "css", "sss"}:
            raise CaptureError(f"recipe match {index} has an invalid shape")
        decoded.append({
            "css": _decode_actions(row.get("css"), context=f"recipe match {index} css"),
            "sss": _decode_actions(row.get("sss"), context=f"recipe match {index} sss"),
        })
    return decoded


def _controller_section(device: str) -> dict[str, str]:
    section = {"Device": device}
    for _, name in BUTTONS:
        key = ("D-Pad/" + name[2:].title() if name.startswith("D_") else
               "Triggers/" + name if name in ("L", "R") else
               "Buttons/" + ("Start" if name == "START" else name))
        section[key] = "`Button " + name + "`"
    for stick, axis in (("Main Stick", "MAIN"), ("C-Stick", "C")):
        for direction, component in (("Up", "Y +"), ("Down", "Y -"),
                                     ("Left", "X -"), ("Right", "X +")):
            section[stick + "/" + direction] = "`Axis " + axis + " " + component + "`"
        for setting in ("Calibration", "Center", "Modifier"):
            section[stick + "/" + setting] = ""
        section[stick + "/Dead Zone"] = "0"
        section[stick + "/Virtual Notches"] = "0"
    for trigger in ("L", "R"):
        section["Triggers/" + trigger + "-Analog"] = "`Axis " + trigger + " +`"
    section["Triggers/Dead Zone"] = "0"
    section["Triggers/Threshold"] = "90"
    return section


def _atomic_ini(path: Path, parser: configparser.ConfigParser) -> None:
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    if temporary.exists():
        raise CaptureError(f"refusing to overwrite temporary profile file {temporary}")
    try:
        descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
            parser.write(stream)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        path.chmod(0o400)
    except OSError as exc:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
        raise CaptureError(f"cannot update private Dolphin profile {path}: {exc}") from exc


def _ensure_fifo(path: Path) -> None:
    if path.exists():
        if not path.is_fifo():
            raise CaptureError(f"Pipe path exists but is not a FIFO: {path}")
        return
    try:
        os.mkfifo(path, 0o600)
    except OSError as exc:
        raise CaptureError(f"cannot create controller FIFO {path}: {exc}") from exc


def prepare_dual_pipe(user: Path) -> tuple[Path, Path]:
    """Prepare two ordinary Dolphin Pipe devices without deleting profile data."""

    user = Path(user)
    user.mkdir(mode=0o700, parents=True, exist_ok=True)
    pipes = user / "Pipes"
    pipes.mkdir(mode=0o700, exist_ok=True)
    p1, p2 = pipes / "pad1", pipes / "pad2"
    _ensure_fifo(p1)
    _ensure_fifo(p2)

    pad = configparser.ConfigParser(interpolation=None)
    pad.optionxform = str
    pad_path = user / "Config" / "GCPadNew.ini"
    if pad_path.exists():
        pad.read(pad_path)
    # Dolphin assigns Pipe device IDs per (source, name), not globally.  Both
    # named FIFOs therefore receive ControllerInterface ID 0; the port is
    # selected by the distinct Pipe name.  Binding P2 to Pipe/1 leaves the
    # second controller disconnected in the source observer.
    for section, device in (("GCPad1", "Pipe/0/pad1"), ("GCPad2", "Pipe/0/pad2")):
        if not pad.has_section(section):
            pad.add_section(section)
        for key, value in _controller_section(device).items():
            pad.set(section, key, value)
    _atomic_ini(pad_path, pad)

    core = configparser.ConfigParser(interpolation=None)
    core.optionxform = str
    core_path = user / "Config" / "Dolphin.ini"
    if core_path.exists():
        core.read(core_path)
    if not core.has_section("Core"):
        core.add_section("Core")
    core.set("Core", "SIDevice0", "6")
    core.set("Core", "SIDevice1", "6")
    _atomic_ini(core_path, core)
    return p1, p2


class DualPipeController:
    """Write exact ordinary Pipe packets; a failed write is terminal."""

    def __init__(self, p1: Path, p2: Path, log: Path,
                 *, sleep: Callable[[float], None] = time.sleep) -> None:
        self.pipes = (Path(p1), Path(p2))
        self.log = Path(log)
        self.log.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        self.sleep = sleep

    def write(self, port: int, pad: str, *, action: str) -> None:
        if port not in (1, 2):
            raise CaptureError(f"invalid controller port {port}")
        try:
            payload = pipe_commands(pad)
        except (TypeError, ValueError) as exc:
            raise CaptureError(f"invalid PAD for port {port}: {exc}") from exc
        fifo = self.pipes[port - 1]
        try:
            fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as exc:
            raise CaptureError(f"ordinary Pipe port {port} is not connected: {exc}") from exc
        try:
            written = os.write(fd, payload)
            if written != len(payload):
                raise CaptureError(f"short ordinary Pipe write on port {port}")
        except OSError as exc:
            if exc.errno == errno.EAGAIN:
                raise CaptureError(f"ordinary Pipe port {port} is full") from exc
            raise CaptureError(f"ordinary Pipe write failed on port {port}: {exc}") from exc
        finally:
            os.close(fd)
        with self.log.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps({"host_monotonic_ns": time.monotonic_ns(),
                                     "port": port, "action": action,
                                     "intended_pad": pad}, sort_keys=True) + "\n")

    def set_both(self, p1: str, p2: str, *, action: str) -> None:
        self.write(1, p1, action=action)
        self.write(2, p2, action=action)

    def hold(self, p1: str, p2: str, seconds: float, *, action: str) -> None:
        self.set_both(p1, p2, action=action)
        self.sleep(seconds)
        self.set_both(NEUTRAL_PAD, NEUTRAL_PAD, action=action + ":release")


class ObserverTail:
    """Fail-closed live reader for the immutable MWRO file."""

    def __init__(self, stream_path: Path, status_path: Path | None,
                 *, poll: float = 0.02, sink: Callable[[dict[str, Any]], None] | None = None) -> None:
        self.stream_path = Path(stream_path)
        self.status_path = Path(status_path) if status_path else None
        self.poll = poll
        self.sink = sink
        self.stream = None
        self.offset = 0
        self.expected_seq = 0
        self.ended = False
        self.counts: dict[str, int] = {}
        self.identity: dict[str, str] | None = None

    def close(self) -> None:
        if self.stream is not None:
            self.stream.close()
            self.stream = None

    def __enter__(self) -> "ObserverTail":
        return self

    def __exit__(self, _type, _value, _traceback) -> None:
        self.close()

    def _status_error(self) -> str | None:
        if self.status_path is None or not self.status_path.exists():
            return None
        status = read_status(self.status_path)
        if status["invalid"] or status["state"] == "invalid" or status["error"]:
            return "observer status invalid: " + str(status["error"] or "invalid")
        if status["state"] == "interrupted":
            return "observer was interrupted before the declared sequence ended"
        return None

    def require_completed_status(self) -> None:
        if self.status_path is None:
            return
        status = read_status(self.status_path)
        if not status["completed"] or status["state"] != "completed":
            raise CaptureError("observer ended without a completed status sidecar")

    def _open(self, deadline: float) -> None:
        while self.stream is None:
            if self.stream_path.exists():
                try:
                    self.stream = self.stream_path.open("rb")
                    return
                except OSError as exc:
                    raise CaptureError(f"cannot open observer stream: {exc}") from exc
            error = self._status_error()
            if error:
                raise CaptureError(error)
            if time.monotonic() >= deadline:
                raise CaptureError("observer stream did not appear before deadline")
            time.sleep(self.poll)

    def next(self, deadline: float) -> dict[str, Any]:
        if self.ended:
            raise CaptureError("observer stream ended before the requested boundary")
        self._open(deadline)
        while True:
            self.stream.seek(self.offset)
            raw_header = self.stream.read(HEADER.size)
            if len(raw_header) != HEADER.size:
                error = self._status_error()
                if error:
                    raise CaptureError(error)
                if time.monotonic() >= deadline:
                    raise CaptureError("observer record did not arrive before deadline")
                time.sleep(self.poll)
                continue
            header = HEADER.unpack(raw_header)
            if header[8] > MAX_PAYLOAD:
                raise CaptureError("observer payload exceeds pinned bound")
            payload = self.stream.read(header[8])
            if len(payload) != header[8]:
                error = self._status_error()
                if error:
                    raise CaptureError(error)
                if time.monotonic() >= deadline:
                    raise CaptureError("observer payload remained incomplete before deadline")
                time.sleep(self.poll)
                continue
            try:
                row = _decode_record(header, payload, f"observer record {self.expected_seq}")
            except ValueError as exc:
                raise CaptureError(str(exc)) from exc
            if row["seq"] != self.expected_seq:
                raise CaptureError(f"observer sequence gap/reorder ({self.expected_seq} expected, {row['seq']})")
            self.expected_seq += 1
            self.offset += HEADER.size + len(payload)
            self.counts[row["event"]] = self.counts.get(row["event"], 0) + 1
            if self.sink is not None:
                self.sink(row)
            if row["event"] == "error":
                raise CaptureError("observer error: " + str(row["payload"]))
            if row["event"] == "end":
                self.ended = True
            return row

    @staticmethod
    def boundary(row: dict[str, Any]) -> tuple[str, int] | None:
        if row.get("event") != "boundary":
            return None
        payload = row.get("payload", {})
        if not payload.get("whole_session"):
            raise CaptureError("whole-session scheduler received an unmarked boundary")
        name = payload.get("boundary")
        index = payload.get("match_index")
        if name not in WHOLE_BOUNDARIES or type(index) is not int:
            raise CaptureError("observer whole-session boundary metadata is invalid")
        return name, index

    def require_announcements(self, match_count: int, deadline: float, *,
                              capture_id: str | None = None,
                              sequence_id: str | None = None) -> None:
        """Admit only a whole-session stream with one source identity pair.

        The producer validates these IDs before writing the stream.  Requiring
        the same bounded values in both announcements prevents attaching this
        run to a stale stream or combining captures from different sequences.
        """
        def identity_value(payload: dict[str, Any], name: str, context: str) -> str:
            value = payload.get(name)
            if (not isinstance(value, str) or
                    re.fullmatch(r"[A-Za-z0-9_.-]{1,128}", value) is None):
                raise CaptureError(f"observer {context} has an invalid {name}")
            return value

        handshake = self.next(deadline)
        if handshake["event"] != "handshake" or handshake["payload"].get("whole_session") is not True:
            raise CaptureError("observer stream is missing its whole-session handshake")
        if handshake["payload"].get("match_count") != match_count:
            raise CaptureError("observer handshake match count differs from selected inventory")
        handshake_capture = identity_value(handshake["payload"], "capture_id", "handshake")
        handshake_sequence = identity_value(handshake["payload"], "sequence_id", "handshake")
        start = self.next(deadline)
        if start["event"] != "start" or start["payload"].get("whole_session") is not True:
            raise CaptureError("observer stream is missing its whole-session start")
        if start["payload"].get("match_count") != match_count:
            raise CaptureError("observer start match count differs from selected inventory")
        start_capture = identity_value(start["payload"], "capture_id", "start")
        start_sequence = identity_value(start["payload"], "sequence_id", "start")
        if (handshake_capture, handshake_sequence) != (start_capture, start_sequence):
            raise CaptureError("observer handshake and start identities disagree")
        if capture_id is not None and handshake_capture != capture_id:
            raise CaptureError("observer capture_id differs from the requested capture")
        if sequence_id is not None and handshake_sequence != sequence_id:
            raise CaptureError("observer sequence_id differs from the requested sequence")
        self.identity = {"capture_id": handshake_capture, "sequence_id": handshake_sequence}

    def wait_source_state(self, match_index: int, deadline: float,
                           predicate: Callable[[dict[str, Any]], bool],
                           *, description: str) -> dict[str, Any]:
        """Wait for an observed source tick satisfying a fighter-state predicate.

        Entry/setup metadata does not prove that the match is playable.  This
        consumes the typed FighterHead/FighterStocks slices from source_tick
        records and lets callers gate input on the actual four-stock, grounded
        source state and on each observed stock transition.
        """
        while True:
            row = self.next(deadline)
            value = self.boundary(row)
            if value is None:
                continue
            name, index = value
            if index != match_index:
                raise CaptureError(
                    f"observer boundary {name} has match index {index}, expected {match_index}")
            if name == "source_tick":
                state = _source_fighter_state(row)
                if predicate(state):
                    return {"row": row, "state": state}
                continue
            if name in SOURCE_STATE_IGNORED:
                continue
            raise CaptureError(f"observer boundary {name} arrived before {description}")

    def wait_boundary(self, expected: str, match_index: int, deadline: float,
                      *, allowed: set[str] | None = None) -> dict[str, Any]:
        allowed = allowed or {expected}
        while True:
            row = self.next(deadline)
            value = self.boundary(row)
            if value is None:
                continue
            name, index = value
            if index != match_index:
                raise CaptureError(f"observer boundary {name} has match index {index}, expected {match_index}")
            if name == expected:
                return row
            if name in IGNORE_BOUNDARIES:
                continue
            if name not in allowed:
                raise CaptureError(f"observer boundary {name} arrived before expected {expected}")

    def wait_initial_css(self, deadline: float) -> dict[str, Any]:
        """Wait for the first CSS enter, admitting only the authored Prize prelude.

        Some source profiles enter Prize during startup before the first CSS
        route.  The passive observer permits that path only as the complete
        four-boundary sequence, in order, at match index zero.  Prize events
        elsewhere remain handled by Results confirmation and are never placed
        in ``IGNORE_BOUNDARIES``.
        """
        prelude: list[dict[str, Any]] = []
        next_prize = 0
        prelude_complete = False
        while True:
            row = self.next(deadline)
            value = self.boundary(row)
            if value is None:
                continue
            name, index = value
            if index != 0:
                raise CaptureError(
                    f"initial CSS boundary {name} has match index {index}, expected 0")
            if name in IGNORE_BOUNDARIES:
                continue
            if name == "css_enter":
                if next_prize:
                    raise CaptureError("startup Prize prelude was incomplete before CSS enter")
                return {"css": row, "startup_prize": prelude}
            if name in STARTUP_PRIZE_ORDER and prelude_complete:
                raise CaptureError("duplicate startup Prize prelude before CSS enter")
            if name == STARTUP_PRIZE_ORDER[next_prize]:
                prelude.append(row)
                next_prize += 1
                if next_prize == len(STARTUP_PRIZE_ORDER):
                    prelude_complete = True
                    next_prize = 0
                continue
            raise CaptureError(
                f"observer boundary {name} arrived before the initial CSS enter")


def _setup_values(row: dict[str, Any]) -> tuple[int, int, int]:
    if row.get("event") != "boundary" or row.get("payload", {}).get("boundary") != "setup":
        raise CaptureError("VS setup validation received the wrong observer record")
    spans = [slice_row for slice_row in row["payload"].get("slices", [])
             if slice_row.get("name") == "match_setup"]
    if len(spans) != 1 or spans[0].get("size") != 0x138:
        raise CaptureError("VS setup boundary has no complete authored setup slice")
    try:
        raw = bytes.fromhex(spans[0]["hex"])
    except (KeyError, ValueError) as exc:
        raise CaptureError("VS setup slice is not canonical bytes") from exc
    if len(raw) != 0x138:
        raise CaptureError("VS setup slice has an unexpected size")
    return int.from_bytes(raw[0x60:0x61], "big"), int.from_bytes(raw[0x84:0x85], "big"), int.from_bytes(raw[14:16], "big")


def _source_fighter_state(row: dict[str, Any]) -> dict[str, Any]:
    """Decode the exact two-player state slices emitted at source_tick.

    ``ReferenceCaptureObserver::AddFighterSlices`` pins FighterHead at 0x100
    bytes and FighterStocks at one signed byte.  The offsets below are the
    authored source fields used by the passive semantic decoder, rather than a
    browser approximation of readiness or respawn state.
    """
    payload = row.get("payload", {})
    if (row.get("event") != "boundary" or
            payload.get("boundary") != "source_tick"):
        raise CaptureError("fighter state validation received a non-source-tick record")
    heads: dict[int, bytes] = {}
    stocks: dict[int, int] = {}
    for item in payload.get("slices", []):
        if not isinstance(item, dict):
            raise CaptureError("source_tick contains a malformed slice descriptor")
        name = item.get("name")
        slot = item.get("flags")
        size = item.get("size")
        if type(slot) is not int or slot not in (0, 1):
            raise CaptureError("source_tick fighter slice has an invalid player flag")
        if type(size) is not int:
            raise CaptureError("source_tick fighter slice has an invalid size")
        try:
            raw = bytes.fromhex(item["hex"])
        except (KeyError, TypeError, ValueError) as exc:
            raise CaptureError("source_tick fighter slice is not canonical bytes") from exc
        if len(raw) != size:
            raise CaptureError("source_tick fighter slice size disagrees with its bytes")
        if name == "fighter_head":
            if size != 0x100 or slot in heads:
                raise CaptureError("source_tick must expose one 0x100-byte FighterHead per player")
            heads[slot] = raw
        elif name == "fighter_stocks":
            if size != 1 or slot in stocks:
                raise CaptureError("source_tick must expose one stock byte per player")
            stocks[slot] = struct.unpack(">b", raw)[0]
    if set(heads) != {0, 1} or set(stocks) != {0, 1}:
        raise CaptureError("source_tick lacks complete two-player FighterHead/FighterStocks slices")
    return {
        "stocks": (stocks[0], stocks[1]),
        "motions": (int.from_bytes(heads[0][0x10:0x14], "big"),
                    int.from_bytes(heads[1][0x10:0x14], "big")),
        "ground_air": (int.from_bytes(heads[0][0xe0:0xe4], "big"),
                       int.from_bytes(heads[1][0xe0:0xe4], "big")),
    }


class SequenceRunner:
    """Boundary-driven ordinary input state machine."""

    def __init__(self, inventory: list[dict[str, Any]], recipe: list[dict[str, list[InputAction]]],
                 observer: ObserverTail, controller: DualPipeController, *,
                 deadline: float = 1800.0, per_match: float = 480.0,
                 hold_seconds: float = 0.12, capture_id: str | None = None,
                 sequence_id: str | None = None) -> None:
        self.inventory = inventory
        self.recipe = recipe
        self.observer = observer
        self.controller = controller
        self.deadline_seconds = deadline
        self.per_match_seconds = per_match
        self.hold_seconds = hold_seconds
        self.capture_id = capture_id
        self.sequence_id = sequence_id
        self.actions: list[dict[str, Any]] = []
        self.boundaries: list[dict[str, Any]] = []

    def _deadline(self, run_deadline: float, match_started: float | None = None) -> float:
        value = run_deadline
        if match_started is not None:
            value = min(value, match_started + self.per_match_seconds)
        return value

    def _input_actions(self, phase: str, match_index: int) -> None:
        for action in self.recipe[match_index][phase]:
            self.controller.hold(action.p1, action.p2, action.seconds, action=action.label)
            self.actions.append({"match_index": match_index, "phase": phase,
                                 "label": action.label, "seconds": action.seconds,
                                 "p1": action.p1, "p2": action.p2})

    def _boundary(self, name: str, index: int, deadline: float,
                  *, allowed: set[str] | None = None) -> dict[str, Any]:
        row = self.observer.wait_boundary(name, index, deadline, allowed=allowed)
        self._record_boundary(row, name, index)
        return row

    def _record_boundary(self, row: dict[str, Any], name: str, index: int,
                         *, state: dict[str, Any] | None = None) -> None:
        payload = row["payload"]
        record = {"name": name, "match_index": index,
                  "seq": row["seq"], "source_tick": row["source_tick"],
                  "draw_ordinal": row["draw_ordinal"],
                  "audio_owner_epoch": payload.get("audio_owner_epoch")}
        if state is not None:
            record["fighter_state"] = state
        self.boundaries.append(record)

    def _confirm_results(self, index: int, deadline: float) -> None:
        # Source Results can remain in its statistics page after the first
        # Start.  Pulses are sent only while passive boundaries show that the
        # source is still in the Results/Prize chain.  A missing return-CSS is
        # a failed run, never a reason to keep pressing indefinitely.
        allowed = {"results_gobj", "results_exit", "results_mode_exit", "scene_teardown",
                   "prize_mode_enter", "prize_scene_enter", "prize_scene_exit",
                   "prize_mode_exit", "return_css"}
        observed: set[str] = set()
        pulse_count = 0
        while pulse_count < 8:
            self.controller.hold(raw_pad(buttons=["START"]), raw_pad(buttons=["START"]),
                                 self.hold_seconds, action=f"results-start-{pulse_count + 1}")
            self.actions.append({"match_index": index, "phase": "results",
                                 "label": f"results-start-{pulse_count + 1}",
                                 "seconds": self.hold_seconds})
            pulse_count += 1
            phase_deadline = min(deadline, time.monotonic() + 1.5)
            while time.monotonic() < phase_deadline:
                try:
                    row = self.observer.next(phase_deadline)
                except CaptureError as exc:
                    if "did not arrive before deadline" in str(exc):
                        break
                    raise
                value = self.observer.boundary(row)
                if value is None:
                    continue
                name, row_index = value
                if row_index != index:
                    raise CaptureError(f"Results boundary {name} has match index {row_index}, expected {index}")
                if name in IGNORE_BOUNDARIES:
                    continue
                if name not in allowed:
                    raise CaptureError(f"observer boundary {name} arrived during Results confirmation")
                observed.add(name)
                self.boundaries.append({"name": name, "match_index": index,
                                        "seq": row["seq"], "source_tick": row["source_tick"],
                                        "draw_ordinal": row["draw_ordinal"],
                                        "audio_owner_epoch": row["payload"].get("audio_owner_epoch")})
                if name == "return_css":
                    required = {"results_gobj", "results_exit", "results_mode_exit",
                                "scene_teardown"}
                    missing = sorted(required - observed)
                    if missing:
                        raise CaptureError(
                            "Results returned to CSS before required source boundaries: " +
                            ", ".join(missing))
                    if "prize_mode_enter" in observed:
                        prize_required = {"prize_mode_enter", "prize_scene_enter",
                                          "prize_scene_exit", "prize_mode_exit"}
                        missing = sorted(prize_required - observed)
                        if missing:
                            raise CaptureError(
                                "Prize returned to CSS before required source boundaries: " +
                                ", ".join(missing))
                    return
        missing = "return_css"
        raise CaptureError(f"Results confirmation exhausted {pulse_count} ordinary Start pulses before {missing}")

    def run(self) -> dict[str, Any]:
        started = time.monotonic()
        deadline = started + self.deadline_seconds
        self.observer.require_announcements(
            len(self.inventory), deadline, capture_id=self.capture_id,
            sequence_id=self.sequence_id)
        initial = self.observer.wait_initial_css(deadline)
        for row in initial["startup_prize"]:
            self._record_boundary(row, row["payload"]["boundary"], 0)
        self._record_boundary(initial["css"], "css_enter", 0)
        for index, expected in enumerate(self.inventory):
            match_started = time.monotonic()
            match_deadline = self._deadline(deadline, match_started)
            self._input_actions("css", index)
            self._boundary("css_exit", index, match_deadline)
            self._boundary("sss_enter", index, match_deadline)
            self._input_actions("sss", index)
            self._boundary("sss_exit", index, match_deadline)
            self._boundary("entry", index, match_deadline)
            setup = self._boundary("setup", index, match_deadline)
            p1, p2, stage = _setup_values(setup)
            if (p1, p2, stage) != (expected["p1"], expected["p2"], expected["stage"]):
                raise CaptureError(
                    f"match {index} authored setup {(p1, p2, stage)} differs from inventory "
                    f"{expected['p1'], expected['p2'], expected['stage']}")
            # Setup is not the playable-ready signal.  Wait for a complete
            # source tick with both authored stock bytes at four and both
            # fighter heads grounded in their idle motion before writing any
            # movement input.
            ready = self.observer.wait_source_state(
                index, match_deadline,
                lambda state: state["stocks"] == (4, 4) and
                state["motions"] == (14, 14) and state["ground_air"] == (0, 0),
                description="the grounded four-stock ready state")
            self._record_boundary(ready["row"], "source_tick", index, state=ready["state"])

            # Four-stock ordinary edge movement.  Each action is followed by a
            # source-observed stock transition; fixed sleeps cannot establish a
            # respawn or completion and are therefore not used here.
            current_p1 = 4
            while current_p1 > 0:
                edge = raw_pad(x=80)
                jump = raw_pad(buttons=["X"], x=80)
                self.controller.hold(edge, NEUTRAL_PAD, 1.2,
                                     action=f"match-{index}-stock-{current_p1}-edge")
                self.controller.hold(jump, NEUTRAL_PAD, 0.65,
                                     action=f"match-{index}-stock-{current_p1}-jump")
                self.actions.extend([
                    {"match_index": index, "phase": "match", "stock": current_p1,
                     "label": "edge", "seconds": 1.2, "p1": edge, "p2": NEUTRAL_PAD},
                    {"match_index": index, "phase": "match", "stock": current_p1,
                     "label": "jump", "seconds": 0.65, "p1": jump, "p2": NEUTRAL_PAD},
                ])
                previous_p1 = current_p1
                loss = self.observer.wait_source_state(
                    index, match_deadline,
                    lambda state, previous_p1=previous_p1:
                    state["stocks"][0] == previous_p1 - 1 and state["stocks"][1] == 4,
                    description=f"P1 stock transition {previous_p1}->{previous_p1 - 1}")
                current_p1 = loss["state"]["stocks"][0]
                self._record_boundary(loss["row"], "source_tick", index, state=loss["state"])
                if current_p1 > 0:
                    respawn = self.observer.wait_source_state(
                        index, match_deadline,
                        lambda state, current_p1=current_p1:
                        state["stocks"] == (current_p1, 4) and
                        state["motions"] == (14, 14) and state["ground_air"] == (0, 0),
                        description=f"the grounded P1 respawn at {current_p1} stocks")
                    self._record_boundary(respawn["row"], "source_tick", index,
                                          state=respawn["state"])
            self._boundary("vs_exit", index, match_deadline)
            self._boundary("vs_exit_return", index, match_deadline)
            self._boundary("vs_mode_exit", index, match_deadline)
            self._boundary("results_enter", index, match_deadline)
            self._confirm_results(index, match_deadline)
        # The last confirmation consumes the final ReturnCss boundary.  The
        # producer must then finish naturally; waiting for end catches a
        # truncated or stopped capture instead of declaring browser-like input
        # success from a scene boundary alone.
        while True:
            row = self.observer.next(deadline)
            if row["event"] == "end":
                self.observer.require_completed_status()
                break
            value = self.observer.boundary(row)
            if value is not None and value[0] not in IGNORE_BOUNDARIES:
                raise CaptureError(f"observer boundary {value[0]} followed final ReturnCss")
        return {"complete": True, "elapsed_seconds": time.monotonic() - started,
                "actions": self.actions, "boundaries": self.boundaries,
                "observer_counts": self.observer.counts}


def _new_output(path: Path) -> Path:
    if path.exists():
        raise CaptureError(f"refusing to overwrite existing evidence directory {path}")
    path.mkdir(mode=0o700, parents=True)
    return path


def _write_report(path: Path, report: dict[str, Any]) -> None:
    target = path / "report.json"
    temporary = path / "report.json.tmp"
    if temporary.exists() or target.exists():
        raise CaptureError(f"refusing to overwrite report in {path}")
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, sort_keys=True, indent=2)
        stream.write("\n")
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(target)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--observer", type=Path, help="fresh MWRO observer stream")
    parser.add_argument("--status", type=Path, help="observer status sidecar")
    parser.add_argument("--user", type=Path, required=True, help="private Dolphin user profile")
    parser.add_argument("--out", type=Path, help="new evidence directory")
    parser.add_argument("--sequence", choices=("repeatMario", "rotate"), default="repeatMario")
    parser.add_argument("--recipe", type=Path,
                        help="ordinary CSS/SSS input recipe (required for a capture; selections are not inferred)")
    parser.add_argument("--capture-id",
                        help="expected MWRC_CAPTURE_ID; rejects a different observer stream")
    parser.add_argument("--sequence-id",
                        help="expected MWRC_SEQUENCE_ID; rejects a different observer stream")
    parser.add_argument("--prepare-pipes", action="store_true",
                        help="create/check pad1/pad2 FIFOs and bind both Dolphin GC ports")
    parser.add_argument("--prepare-only", action="store_true",
                        help="prepare the private profile and FIFOs, then exit before reading MWRO")
    parser.add_argument("--deadline", type=float, default=1800.0)
    parser.add_argument("--per-match-deadline", type=float, default=480.0)
    args = parser.parse_args(argv)
    try:
        if args.prepare_only:
            prepare_dual_pipe(args.user)
            return 0
        if args.observer is None or args.out is None:
            raise CaptureError("--observer and --out are required unless --prepare-only is used")
        if args.deadline <= 0 or args.per_match_deadline <= 0:
            raise CaptureError("deadlines must be positive")
        inventory = load_inventory(args.sequence)
        recipe = load_recipe(args.recipe, args.sequence, len(inventory))
        evidence = _new_output(args.out)
        if args.prepare_pipes:
            p1, p2 = prepare_dual_pipe(args.user)
        else:
            p1, p2 = args.user / "Pipes/pad1", args.user / "Pipes/pad2"
        ledger = evidence / "boundaries.jsonl"
        def sink(row: dict[str, Any]) -> None:
            if row["event"] != "boundary" or row["payload"].get("boundary") not in IGNORE_BOUNDARIES:
                with ledger.open("a", encoding="utf-8") as stream:
                    stream.write(json.dumps(row, sort_keys=True) + "\n")
        tail = ObserverTail(args.observer, args.status, sink=sink)
        controller = DualPipeController(p1, p2, evidence / "input-intentions.jsonl")
        try:
            # The source announces these identities in both the handshake and
            # start records.  Optional expected values bind this attach to the
            # freshly launched Dolphin session without guessing from paths.
            result = SequenceRunner(inventory, recipe, tail, controller,
                                    deadline=args.deadline,
                                    per_match=args.per_match_deadline,
                                    capture_id=args.capture_id,
                                    sequence_id=args.sequence_id).run()
        finally:
            tail.close()
        report = {"schema": "melee-web-versus-sequences-capture-v1", "status": "complete",
                  "sequence": args.sequence, "inventory": inventory,
                  "observer": str(args.observer), "observer_identity": tail.identity,
                  "result": result}
        _write_report(evidence, report)
        return 0
    except Exception as exc:
        try:
            if "evidence" in locals():
                _write_report(evidence, {"schema": "melee-web-versus-sequences-capture-v1",
                                         "status": "failed", "sequence": args.sequence,
                                         "observer_identity": getattr(locals().get("tail"), "identity", None),
                                         "error": str(exc), "first_failure": str(exc)})
        except Exception:
            pass
        print(f"reference versus sequence failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
