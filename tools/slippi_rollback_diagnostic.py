#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate and emit the bounded desktop rollback diagnostic configuration.

The native diagnostic patch consumes the JSON written by :func:`write_config`.
The schema supports the optional whole-forward ``mario-fd-rollback-v1`` input
profile, one input overlay, and one bounded transport action; it cannot express
an unbounded impairment schedule.
"""

from __future__ import annotations

from dataclasses import dataclass
import argparse
import json
from pathlib import Path
from typing import Any


SCHEMA = "melee-web-slippi-rollback-diagnostic-v1"
MAX_FRAME = 1_000_000
MAX_HOLD_FRAMES = 8
PAD_DATA_BYTES = 8
FINAL_DESTINATION_STAGE_ID = 0x20
PROFILE_NAME = "mario-fd-rollback-v1"


class DiagnosticConfigError(ValueError):
    """Raised when a diagnostic configuration is malformed or unsafe."""


def _strict_object(value: Any, name: str, keys: set[str]) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise DiagnosticConfigError(f"{name} must be an object")
    unknown = set(value) - keys
    missing = keys - set(value)
    if unknown:
        raise DiagnosticConfigError(f"{name} has unknown keys: {sorted(unknown)}")
    if missing:
        raise DiagnosticConfigError(f"{name} is missing keys: {sorted(missing)}")
    return value


def _bool(value: Any, name: str) -> bool:
    if not isinstance(value, bool):
        raise DiagnosticConfigError(f"{name} must be a boolean")
    return value


def _integer(value: Any, name: str, *, minimum: int, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise DiagnosticConfigError(f"{name} must be an integer")
    if not minimum <= value <= maximum:
        raise DiagnosticConfigError(f"{name} must be between {minimum} and {maximum}")
    return value


def _hex_pad(value: Any, name: str) -> str:
    if not isinstance(value, str) or len(value) != PAD_DATA_BYTES * 2:
        raise DiagnosticConfigError(f"{name} must contain exactly {PAD_DATA_BYTES * 2} hex characters")
    if any(character not in "0123456789abcdefABCDEF" for character in value):
        raise DiagnosticConfigError(f"{name} must contain hexadecimal bytes")
    return value.lower()


@dataclass(frozen=True)
class InputOverlay:
    frame: int
    pad_hex: str


@dataclass(frozen=True)
class TransportAction:
    action: str
    frame: int | None
    release_frame: int | None


@dataclass(frozen=True)
class InputProfile:
    name: str
    role: int


@dataclass(frozen=True)
class DiagnosticConfig:
    enabled: bool
    rng_offset: int
    log_path: str
    stage_id: int | None
    input_profile: InputProfile | None
    overlay: InputOverlay | None
    transport: TransportAction

    def as_dict(self) -> dict[str, Any]:
        return {
            "schema": SCHEMA,
            "enabled": self.enabled,
            "rng_offset": self.rng_offset,
            "log_path": self.log_path,
            "stage_id": self.stage_id,
            "input_profile": None if self.input_profile is None else {
                "name": self.input_profile.name,
                "role": self.input_profile.role,
            },
            "overlay": None if self.overlay is None else {
                "frame": self.overlay.frame,
                "pad_hex": self.overlay.pad_hex,
            },
            "transport": {
                "action": self.transport.action,
                "frame": self.transport.frame,
                "release_frame": self.transport.release_frame,
            },
        }


def parse_config(value: Any) -> DiagnosticConfig:
    root = _strict_object(value, "config", {
        "schema", "enabled", "rng_offset", "log_path", "stage_id", "input_profile",
        "overlay", "transport",
    })
    if root["schema"] != SCHEMA:
        raise DiagnosticConfigError(f"schema must be {SCHEMA!r}")
    enabled = _bool(root["enabled"], "enabled")
    rng_offset = _integer(root["rng_offset"], "rng_offset", minimum=0, maximum=0xFFFF)
    log_path = root["log_path"]
    if not isinstance(log_path, str) or not log_path or "\x00" in log_path:
        raise DiagnosticConfigError("log_path must be a nonempty string without NUL")
    stage_value = root["stage_id"]
    if stage_value is None:
        stage_id = None
    else:
        stage_id = _integer(
            stage_value, "stage_id", minimum=FINAL_DESTINATION_STAGE_ID,
            maximum=FINAL_DESTINATION_STAGE_ID,
        )

    profile_value = root["input_profile"]
    input_profile = None
    if profile_value is not None:
        profile_object = _strict_object(profile_value, "input_profile", {"name", "role"})
        if profile_object["name"] != PROFILE_NAME:
            raise DiagnosticConfigError(f"input_profile.name must be {PROFILE_NAME!r}")
        input_profile = InputProfile(
            name=PROFILE_NAME,
            role=_integer(profile_object["role"], "input_profile.role", minimum=1, maximum=2),
        )

    overlay_value = root["overlay"]
    overlay = None
    if overlay_value is not None:
        overlay_object = _strict_object(overlay_value, "overlay", {"frame", "pad_hex"})
        overlay = InputOverlay(
            frame=_integer(overlay_object["frame"], "overlay.frame", minimum=1, maximum=MAX_FRAME),
            pad_hex=_hex_pad(overlay_object["pad_hex"], "overlay.pad_hex"),
        )

    transport_object = _strict_object(
        root["transport"], "transport", {"action", "frame", "release_frame"}
    )
    action = transport_object["action"]
    if not isinstance(action, str):
        raise DiagnosticConfigError("transport.action must be a string")
    if action not in {"none", "drop", "hold"}:
        raise DiagnosticConfigError("transport.action must be one of none, drop, hold")
    frame_value = transport_object["frame"]
    release_value = transport_object["release_frame"]
    if action == "none":
        if frame_value is not None or release_value is not None:
            raise DiagnosticConfigError("transport none requires null frame and release_frame")
        frame = release_frame = None
    else:
        frame = _integer(frame_value, "transport.frame", minimum=1, maximum=MAX_FRAME)
        if action == "drop":
            if release_value is not None:
                raise DiagnosticConfigError("transport drop requires null release_frame")
            release_frame = None
        else:
            release_frame = _integer(
                release_value, "transport.release_frame", minimum=frame + 1,
                maximum=min(MAX_FRAME, frame + MAX_HOLD_FRAMES),
            )

    if not enabled:
        # Disabled files remain structurally valid but carry no active controls.
        if stage_id is not None or input_profile is not None or overlay is not None or action != "none":
            raise DiagnosticConfigError("disabled config cannot contain active diagnostics")

    return DiagnosticConfig(
        enabled=enabled,
        rng_offset=rng_offset,
        log_path=log_path,
        stage_id=stage_id,
        input_profile=input_profile,
        overlay=overlay,
        transport=TransportAction(action, frame, release_frame),
    )


def load_config(path: str | Path) -> DiagnosticConfig:
    config_path = Path(path)
    try:
        value = json.loads(config_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise DiagnosticConfigError(f"could not read diagnostic config {config_path}: {error}") from error
    return parse_config(value)


def write_config(path: str | Path, config: DiagnosticConfig) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    try:
        with output.open("x", encoding="utf-8") as stream:
            stream.write(json.dumps(config.as_dict(), indent=2, sort_keys=True) + "\n")
    except FileExistsError as error:
        raise DiagnosticConfigError(f"refusing to overwrite diagnostic config {output}") from error
    except OSError as error:
        raise DiagnosticConfigError(f"could not write diagnostic config {output}: {error}") from error


def _cli() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", type=Path, help="input JSON configuration")
    parser.add_argument("--canonical-output", type=Path, help="write normalized JSON after validation")
    args = parser.parse_args()
    config = load_config(args.config)
    if args.canonical_output:
        write_config(args.canonical_output, config)
    else:
        print(json.dumps(config.as_dict(), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(_cli())
