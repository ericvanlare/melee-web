# SPDX-License-Identifier: MIT
"""Private profile, controller-pipe, and memory-watch support for local runs."""

from __future__ import annotations

from dataclasses import dataclass
import configparser
import hashlib
import json
import math
import os
from pathlib import Path
import socket
import stat
import struct
import time
from typing import Callable


_WATCHED_SOURCE_WORDS = (
    "80479d30",
    "80479d58",
    "804a04f0",
    "804a04f4",
    "804a0500",
    "804a0bc0 c",
    "804a0bc0 10",
    "804a0bc4 c",
    "804a0bc4 10",
    "804d6720 0",
    "804d6bc8",
    "804d6ca4",
    "804d6cac",
    "804d6cb0 70",
    "804d6cb0 94",
    "804d6cf0",
    "804d3ee0 1850",
    "80453f9c",
    "804a0bc0 c",
    "804a0bc0 10",
    "804a0bc4 c",
    "804a0bc4 10",
    "804d6cae",
)
_WATCHED_STAGE_KIND_WORDS = tuple(
    f"{0x803F06D0 + index * 0x1C + 0x0B:08x}" for index in range(30)
)
_WATCHED_CSS_ICON_WORDS = tuple(
    f"{0x803F0B24 + index * 0x1C + offset:08x}"
    for index in range(25)
    for offset in (1, 2, 12, 16, 20, 24)
)
_WATCHED_CSS_CURSOR_WORDS = (
    "804a0bc0 4",
    "804a0bc0 5",
    "804a0bc0 6",
    "804a0bc4 4",
    "804a0bc4 5",
    "804a0bc4 6",
    "804a0bd0 8",
    "804a0bd0 c",
    "804a0bd4 8",
    "804a0bd4 c",
    "803f0e0a",
    "803f0e2e",
)
WATCHED_WORDS = (
    _WATCHED_SOURCE_WORDS
    + _WATCHED_STAGE_KIND_WORDS
    + _WATCHED_CSS_ICON_WORDS
    + _WATCHED_CSS_CURSOR_WORDS
)

BUTTONS = (
    "A", "B", "X", "Y", "Z", "START", "L", "R",
    "D_UP", "D_DOWN", "D_LEFT", "D_RIGHT",
)
_STICK_AXES = {
    "MAIN": ("Main Stick",),
    "C": ("C-Stick",),
}
CSS_MARIO_CHARACTER_KIND = 8
MEMORY_WATCHER_SNAPSHOT_MARKER = "__local_initial_scan_complete__"


@dataclass(frozen=True)
class ClientProfile:
    root: Path
    user_root: Path
    replay_root: Path
    pads: tuple[Path, Path]
    memory_socket: Path
    memory_locations: Path
    identity: str
    connect_code: str
    peer_port: int


def _write_private(path: Path, content: str) -> None:
    with path.open("x", encoding="utf-8") as stream:
        stream.write(content)
        stream.flush()
        os.fsync(stream.fileno())
    path.chmod(0o600)


def _config_text(sections: dict[str, dict[str, str]]) -> str:
    config = configparser.ConfigParser(interpolation=None)
    config.optionxform = str
    for section, values in sections.items():
        config[section] = values
    from io import StringIO

    stream = StringIO()
    config.write(stream, space_around_delimiters=True)
    return stream.getvalue()


def _controller_config() -> str:
    sections: dict[str, dict[str, str]] = {}
    for port in (1, 2):
        section: dict[str, str] = {"Device": f"Pipe/0/pad{port}"}
        for button in BUTTONS:
            name = "Start" if button == "START" else button
            key = f"D-Pad/{button[2:].title()}" if button.startswith("D_") else f"Buttons/{name}"
            section[key] = f"`Button {button}`"
        for axis_name, (display_name,) in _STICK_AXES.items():
            for direction, component in (
                ("Up", "Y +"), ("Down", "Y -"), ("Left", "X -"), ("Right", "X +")
            ):
                section[f"{display_name}/{direction}"] = f"`Axis {axis_name} {component}`"
            for setting in ("Calibration", "Center", "Modifier"):
                section[f"{display_name}/{setting}"] = ""
            section[f"{display_name}/Dead Zone"] = "0"
            section[f"{display_name}/Virtual Notches"] = "0"
        for trigger in ("L", "R"):
            section[f"Triggers/{trigger}"] = f"`Button {trigger}`"
            section[f"Triggers/{trigger}-Analog"] = f"`Axis {trigger} +`"
        section["Triggers/Dead Zone"] = "0"
        section["Triggers/Threshold"] = "90"
        sections[f"GCPad{port}"] = section
    return _config_text(sections)


def _logger_config() -> str:
    return _config_text({
        "Logs": {"SLIPPI_ONLINE": "True"},
        "Options": {
            # Dolphin levels are ordered NOTICE=1, ERROR=2, WARNING=3, INFO=4.
            # The integration also requires the INFO-level peer-disconnect receipt.
            "Verbosity": "4",
            "WriteToConsole": "True",
            "WriteToFile": "True",
            "WriteToWindow": "False",
        },
    })


def create_client_profile(
    *,
    root: Path,
    replay_root: Path,
    identity: str,
    play_key: str,
    display_name: str,
    connect_code: str,
    opponent_code: str,
    peer_port: int,
    spectator_port: int,
    game_modification: Path,
    expected_modification_sha256: str,
    latest_version: str,
) -> ClientProfile:
    """Create one fresh, isolated local-test profile without linking shared state."""
    root = Path(root).expanduser().resolve()
    replay_root = Path(replay_root).expanduser().resolve()
    game_modification = Path(game_modification).expanduser().resolve(strict=True)
    if (
        not identity.startswith("local-")
        or len(identity.encode("utf-8")) > 64
        or not play_key.startswith("local-")
        or len(play_key.encode("utf-8")) > 128
    ):
        raise ValueError("local test identity or play key has an invalid prefix or length")
    for value, label in ((display_name, "display name"), (connect_code, "connect code"),
                         (opponent_code, "opponent code")):
        if not value or len(value.encode("utf-8")) > 32:
            raise ValueError(f"{label} must be nonempty and at most 32 UTF-8 bytes")
    try:
        connect_code.encode("ascii")
        opponent_bytes = opponent_code.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("direct connect codes must use ASCII") from error
    if not 1 <= len(opponent_bytes) <= 18:
        raise ValueError("direct target connect code must be 1 to 18 ASCII bytes")
    if (
        not 41000 <= peer_port <= 51999
        or not 41000 <= spectator_port <= 51999
        or peer_port in (43113, 43114)
        or spectator_port in (43113, 43114)
    ):
        raise ValueError("Slippi peer and spectator ports must use the reserved range")
    if peer_port == spectator_port:
        raise ValueError("Slippi peer and spectator ports must be different")
    if root == replay_root or root in replay_root.parents or replay_root in root.parents:
        raise ValueError("client profile and replay output must be separate trees")
    if len(str(root / "User" / "MemoryWatcher" / "MemoryWatcher").encode()) >= 104:
        raise ValueError("private profile path is too long for Dolphin's Unix memory socket")
    raw_mod = game_modification.read_bytes()
    if hashlib.sha256(raw_mod).hexdigest() != expected_modification_sha256:
        raise ValueError("generated Slippi game modification hash differs from the pinned lock")
    root.mkdir(parents=True, mode=0o700, exist_ok=False)
    root.chmod(0o700)
    user_root = root / "User"
    user_root.mkdir(mode=0o700)
    config_root = user_root / "Config"
    slippi_root = user_root / "Slippi"
    memory_root = user_root / "MemoryWatcher"
    pipes_root = user_root / "Pipes"
    game_settings_root = user_root / "GameSettings"
    for directory in (config_root, slippi_root, memory_root, pipes_root, game_settings_root):
        directory.mkdir(mode=0o700)
    replay_root.mkdir(parents=True, mode=0o700, exist_ok=False)
    replay_root.chmod(0o700)

    config = {
        "Core": {
            "EnableCheats": "True",
            "GFXBackend": "Null",
            "SIDevice0": "6",
            "SIDevice1": "6",
        },
        "DSP": {"Backend": "No Audio Output", "DumpAudio": "False"},
        "Analytics": {"Enabled": "False"},
        "Slippi": {
            "ForceNetplayPort": "True",
            "NetplayPort": str(peer_port),
            "EnableSpectator": "False",
            "SpectatorLocalPort": str(spectator_port),
            "SaveReplays": "True",
            "ReplayDir": str(replay_root.resolve()),
            "ReplayMonthlyFolders": "False",
            "ShowLocalRankInfo": "False",
            "ShowOpponentRankInfo": "False",
            "EnableJukebox": "False",
        },
    }
    _write_private(config_root / "Dolphin.ini", _config_text(config))
    _write_private(config_root / "GCPadNew.ini", _controller_config())
    _write_private(config_root / "Logger.ini", _logger_config())
    _write_private(memory_root / "Locations.txt", "\n".join(WATCHED_WORDS) + "\n")
    for port in (1, 2):
        os.mkfifo(pipes_root / f"pad{port}", 0o600)

    if not latest_version or len(latest_version.encode("utf-8")) > 64:
        raise ValueError("local test profile latest version is invalid")
    user_data = {
        "uid": identity,
        "playKey": play_key,
        "displayName": display_name,
        "connectCode": connect_code,
        # The pinned Rust UserInfo schema requires this field even though its
        # local API refresh intentionally fails closed with HTTP 503.
        "latestVersion": latest_version,
    }
    _write_private(slippi_root / "user.json", json.dumps(user_data, sort_keys=True) + "\n")
    direct_code = [{"connectCode": opponent_code, "lastPlayed": int(time.time())}]
    _write_private(slippi_root / "direct-codes.json", json.dumps(direct_code) + "\n")
    _write_private(slippi_root / "teams-codes.json", "[]\n")

    _write_private(game_settings_root / "GALE01r2.ini", raw_mod.decode("utf-8"))

    return ClientProfile(
        root=root,
        user_root=user_root,
        replay_root=replay_root,
        pads=(pipes_root / "pad1", pipes_root / "pad2"),
        memory_socket=memory_root / "MemoryWatcher",
        memory_locations=memory_root / "Locations.txt",
        identity=identity,
        connect_code=connect_code,
        peer_port=peer_port,
    )


class MemoryWatcher:
    """Receive changed source words from Dolphin's pinned MemoryWatcher."""

    def __init__(self, profile: ClientProfile):
        self.path = profile.memory_socket
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.socket.bind(str(self.path))
        self.path.chmod(0o600)
        self.values: dict[str, int] = {}
        self._received_initial_snapshot = False
        self._pending_initial_values: dict[str, int] = {}
        self.last_event_ns: int | None = None

    def receive(self, timeout: float) -> dict[str, int]:
        self.socket.settimeout(timeout)
        packet = self.socket.recv(8192).split(b"\0", 1)[0]
        if not packet:
            return {}
        fields = packet.decode("ascii").splitlines()
        if len(fields) % 2:
            raise ValueError("MemoryWatcher datagram has an incomplete address/value pair")
        changed: dict[str, int] = {}
        marker_seen = False
        for index in range(0, len(fields), 2):
            key = fields[index].strip().lower()
            try:
                value = int(fields[index + 1], 16)
            except ValueError as error:
                raise ValueError("MemoryWatcher returned a non-hex value") from error
            if not 0 <= value <= 0xFFFFFFFF:
                raise ValueError("MemoryWatcher word exceeds uint32")
            if key == MEMORY_WATCHER_SNAPSHOT_MARKER:
                if self._received_initial_snapshot or marker_seen or value != 1 \
                        or index != len(fields) - 2:
                    raise ValueError("MemoryWatcher returned an invalid initial-scan marker")
                marker_seen = True
                continue
            if key not in WATCHED_WORDS:
                raise ValueError("MemoryWatcher returned an unconfigured address")
            if key in changed:
                raise ValueError("MemoryWatcher returned a duplicate address in one datagram")
            changed[key] = value

        if not self._received_initial_snapshot:
            overlap = set(changed) & set(self._pending_initial_values)
            if overlap:
                raise ValueError("MemoryWatcher initial scan repeated a source word")
            self._pending_initial_values.update(changed)
            if not marker_seen:
                self.last_event_ns = time.monotonic_ns()
                return {}
            expected = set(WATCHED_WORDS)
            if set(self._pending_initial_values) != expected:
                missing = sorted(expected - set(self._pending_initial_values))
                self._pending_initial_values.clear()
                raise ValueError(
                    "MemoryWatcher initial scan omitted configured source words: "
                    + ", ".join(missing[:8])
                )
            changed = self._pending_initial_values
            self._pending_initial_values = {}
            self._received_initial_snapshot = True
        self.values.update(changed)
        self.last_event_ns = time.monotonic_ns()
        return changed

    def wait_for(
        self,
        predicate: Callable[[dict[str, int]], bool],
        *,
        timeout: float,
        description: str,
    ) -> dict[str, int]:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            remaining = max(0.001, min(0.1, deadline - time.monotonic()))
            try:
                self.receive(remaining)
            except socket.timeout:
                continue
            if predicate(self.values):
                return dict(self.values)
        raise TimeoutError(f"timed out waiting for MemoryWatcher: {description}")

    def wait_source_frames(self, count: int, *, timeout: float) -> int:
        if count <= 0:
            raise ValueError("source frame count must be positive")
        key = "80479d58"
        initial = self.values.get(key)
        if initial is None:
            self.wait_for(lambda values: key in values, timeout=timeout,
                         description="first source frame counter")
            initial = self.values[key]
        result = self.wait_for(
            lambda values: key in values and ((values[key] - initial) & 0xFFFFFFFF) >= count,
            timeout=timeout,
            description=f"{count} source frames",
        )
        return result[key]

    def scene_kind(self) -> int | None:
        value = self.values.get("804d6720 0")
        return None if value is None else (value >> 24) & 0xFF

    def online_scene_code(self) -> int | None:
        value = self.values.get("80479d30")
        if value is None:
            return None
        rotated = ((value << 8) | (value >> 24)) & 0xFFFFFFFF
        return rotated & 0xFFFF

    def selected_stage(self) -> tuple[int, int] | None:
        value = self.values.get("804d6cae")
        if value is None:
            return None
        index = (value >> 24) & 0xFF
        if index >= 30:
            return index, -1
        kind = self.values.get(_WATCHED_STAGE_KIND_WORDS[index])
        return None if kind is None else (index, (kind >> 24) & 0xFF)

    def css_player(self, port: int) -> dict[str, int] | None:
        if port not in (1, 2):
            raise ValueError("CSS player port must be 1 or 2")
        offset = "70" if port == 1 else "94"
        value = self.values.get(f"804d6cb0 {offset}")
        if value is None:
            return None
        return {
            "character_kind": (value >> 24) & 0xFF,
            "slot_type": (value >> 16) & 0xFF,
            "stocks": (value >> 8) & 0xFF,
            "costume": value & 0xFF,
        }

    def css_cursor(self, port: int) -> dict[str, object] | None:
        """Decode source CSS cursor/model positions and the authored icon bounds."""
        if port not in (1, 2):
            raise ValueError("CSS cursor port must be 1 or 2")
        suffix = "0" if port == 1 else "4"
        keys = {
            "cursor_x": f"804a0bc{suffix} c",
            "cursor_y": f"804a0bc{suffix} 10",
            "cursor_port": f"804a0bc{suffix} 4",
            "cursor_mode": f"804a0bc{suffix} 5",
            "cursor_target": f"804a0bc{suffix} 6",
            "model_x": f"804a0bd{suffix} 8",
            "model_y": f"804a0bd{suffix} c",
            "selected_index": "803f0e0a" if port == 1 else "803f0e2e",
        }
        if any(key not in self.values for key in keys.values()):
            return None
        icons = []
        for index in range(25):
            base = 0x803F0B24 + index * 0x1C
            row_keys = tuple(f"{base + offset:08x}" for offset in (1, 2, 12, 16, 20, 24))
            if any(key not in self.values for key in row_keys):
                return None
            kind = self.values[row_keys[0]] >> 24
            available = (self.values[row_keys[1]] >> 24) & 0xFF
            bounds = tuple(
                struct.unpack(">f", struct.pack(">I", self.values[key]))[0]
                for key in row_keys[2:]
            )
            icons.append({"index": index, "kind": kind, "available": available,
                          "bounds": bounds})
        selected_index = (self.values[keys["selected_index"]] >> 24) & 0xFF
        selected_kind = -1
        if selected_index < len(icons):
            selected_kind = int(icons[selected_index]["kind"])
        # The CSS CharacterKind is not the same enum as the project's internal
        # FighterKind. Mario is CSS kind 8; the SLP character ID is also 8,
        # while this repository's runtime FighterKind for Mario is 0.
        targets = [icon for icon in icons
                   if icon["kind"] == CSS_MARIO_CHARACTER_KIND and icon["available"] >= 1]
        if len(targets) != 1:
            raise ValueError("pinned CSS character table has no unique unlocked Mario row (kind 8)")
        left, right, top, bottom = targets[0]["bounds"]
        mode = (self.values[keys["cursor_mode"]] >> 24) & 0xFF
        cursor_target = (self.values[keys["cursor_target"]] >> 24) & 0xFF
        held = cursor_target if mode == 1 and cursor_target < 4 else -1
        return {
            "cursor": (
                struct.unpack(">f", struct.pack(">I", self.values[keys["cursor_x"]]))[0],
                struct.unpack(">f", struct.pack(">I", self.values[keys["cursor_y"]]))[0],
            ),
            "cursor_port": (self.values[keys["cursor_port"]] >> 24) & 0xFF,
            "cursor_mode": mode,
            "cursor_target": cursor_target,
            "model": (
                struct.unpack(">f", struct.pack(">I", self.values[keys["model_x"]]))[0],
                struct.unpack(">f", struct.pack(">I", self.values[keys["model_y"]]))[0],
            ),
            "selected": selected_kind,
            "held": held,
            "bounds": (left, right, top, bottom),
            "target": int(targets[0]["index"]),
            "available_icons": [
                {"index": int(icon["index"]), "kind": int(icon["kind"]),
                 "bounds": icon["bounds"]}
                for icon in icons if icon["available"] >= 1
            ],
        }

    def close(self) -> None:
        self.socket.close()

    def __enter__(self) -> MemoryWatcher:
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.close()
        return False


class ControllerPipe:
    """Send ordinary Pipe/0 controller state and retain intended actions."""

    def __init__(self, fifo: Path, log_path: Path, client: str):
        self.fifo = Path(fifo)
        self.log_path = Path(log_path)
        self.client = client
        self.buttons: set[str] = set()
        self.axes = {"MAIN": (0.5, 0.5), "C": (0.5, 0.5)}
        self.triggers = {"L": 0.0, "R": 0.0}

    def set_button(self, name: str, pressed: bool) -> None:
        if name not in BUTTONS:
            raise ValueError("unknown Pipe controller button")
        if pressed:
            self.buttons.add(name)
        else:
            self.buttons.discard(name)
        self._write()

    def set_axis(self, stick: str, x: float, y: float) -> None:
        if stick not in self.axes or not math.isfinite(x) or not math.isfinite(y):
            raise ValueError("invalid Pipe controller stick")
        if not (0 <= x <= 1 and 0 <= y <= 1):
            raise ValueError("Pipe controller axes must be normalized to [0,1]")
        self.axes[stick] = (x, y)
        self._write()

    def neutral(self) -> None:
        self.buttons.clear()
        self.axes = {"MAIN": (0.5, 0.5), "C": (0.5, 0.5)}
        self.triggers = {"L": 0.0, "R": 0.0}
        self._write()

    def _write(self) -> None:
        lines = [
            ("PRESS " if name in self.buttons else "RELEASE ") + name
            for name in BUTTONS
        ]
        for stick, (x, y) in self.axes.items():
            lines.append(f"SET {stick} {x:.6g} {y:.6g}")
        for trigger, value in self.triggers.items():
            lines.append(f"SET {trigger} {value:.6g}")
        payload = ("\n".join(lines) + "\n").encode("ascii")
        descriptor = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            written = os.write(descriptor, payload)
            if written != len(payload):
                raise OSError("incomplete controller Pipe packet")
        finally:
            os.close(descriptor)
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        record = {
            "host_monotonic_ns": time.monotonic_ns(),
            "client": self.client,
            "buttons": sorted(self.buttons),
            "axes": {key: list(value) for key, value in self.axes.items()},
            "triggers": self.triggers.copy(),
        }
        with self.log_path.open("a", encoding="utf-8") as stream:
            stream.write(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")
