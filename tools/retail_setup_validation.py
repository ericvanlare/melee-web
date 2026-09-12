"""Validate a named development execution plan against a retail setup."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any

from retail_input_plan import load_plan, verify_entry, verify_tick
from retail_replay_validation import load_capture


SCHEMA = "melee-web-retail-setup-validation"
VERSION = 1
EXECUTION_SCHEMA = "melee-web-ucf-off-development-execution"
EXECUTION_VERSION = 1
SETUP_BYTES = 0x138
MAX_EXECUTION_PLAN_BYTES = 1024 * 1024
HEX64 = re.compile(r"[0-9a-f]{64}\Z")
HEX8 = re.compile(r"[0-9a-f]{8}\Z")
HEX16 = re.compile(r"[0-9a-f]{16}\Z")

EXPECTED_SETUP_KEYS = {
    "players", "stage", "match_kind", "timer_enabled", "timer_counts_up",
    "time_limit_seconds", "is_stock", "disable_pausing", "is_teams",
    "item_frequency", "item_mask_hex", "damage_ratio_bits", "game_speed_bits",
}
PLAYER_KEYS = {
    "port", "character_kind", "costume", "stocks", "player_type",
    "rumble_enabled",
}
CPU_PLAYER_KEYS = {"cpu_kind", "cpu_level"}

SCOPE = (
    "exact declared donor settings plus supported ordinary-VS profile checks; "
    "this is not a freeze of every opaque StartMeleeData byte and does not "
    "establish gameplay, port, rendering, performance, or gold admission"
)

SUPPORTED_CAMERA_BITS = "3f800000"


class SetupValidationError(ValueError):
    """The execution plan, source identity, capture, or setup is invalid."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise SetupValidationError(message)


def _unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        _require(key not in result, f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def _read_json(path: Path) -> tuple[dict[str, Any], str]:
    try:
        if path.stat().st_size > MAX_EXECUTION_PLAN_BYTES:
            raise SetupValidationError(
                f"execution plan exceeds the {MAX_EXECUTION_PLAN_BYTES}-byte limit")
        with path.open("rb") as stream:
            raw = stream.read(MAX_EXECUTION_PLAN_BYTES + 1)
    except OSError as error:
        raise SetupValidationError(f"cannot read execution plan {path}: {error}") from error
    _require(len(raw) <= MAX_EXECUTION_PLAN_BYTES,
             f"execution plan exceeds the {MAX_EXECUTION_PLAN_BYTES}-byte limit")
    _require(raw, f"execution plan {path} is empty")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_unique)
    except (UnicodeDecodeError, json.JSONDecodeError, SetupValidationError) as error:
        raise SetupValidationError(f"invalid execution plan {path}: {error}") from error
    _require(isinstance(value, dict), "execution plan must be a JSON object")
    return value, hashlib.sha256(raw).hexdigest()


def _sha256(path: Path, context: str) -> str:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError as error:
        raise SetupValidationError(f"cannot read {context} {path}: {error}") from error


def _resolve(path: Any, repo_root: Path | None, context: str) -> Path:
    _require(isinstance(path, str) and path, f"{context} must be a path")
    candidate = Path(path)
    if not candidate.is_absolute() and repo_root is not None:
        candidate = repo_root / candidate
    return candidate.resolve()


def _hash(value: Any, context: str) -> str:
    _require(isinstance(value, str) and HEX64.fullmatch(value) is not None,
             f"{context} must be a lowercase SHA-256")
    return value


def _integer(value: Any, context: str, low: int, high: int) -> int:
    _require(type(value) is int and low <= value <= high,
             f"{context} must be an integer in [{low}, {high}]")
    return value


def _boolean(value: Any, context: str) -> bool:
    _require(type(value) is bool, f"{context} must be boolean")
    return value


def _validate_expected_setup(value: Any) -> dict[str, Any]:
    _require(isinstance(value, dict), "expected_setup must be an object")
    _require(set(value) == EXPECTED_SETUP_KEYS,
             "expected_setup has missing or unrecognized fields")
    players = value["players"]
    _require(isinstance(players, list) and len(players) == 2,
             "expected_setup.players must contain exactly P1 and P2")
    seen_ports: set[int] = set()
    normalized_players = []
    for index, player in enumerate(players):
        context = f"expected_setup.players[{index}]"
        _require(isinstance(player, dict), f"{context} must be an object")
        player_type = _integer(player.get("player_type"), f"{context}.player_type", 0, 1)
        expected_keys = PLAYER_KEYS | (CPU_PLAYER_KEYS if player_type == 1 else set())
        _require(set(player) == expected_keys,
                 f"{context} has missing or unrecognized fields")
        port = _integer(player["port"], f"{context}.port", 1, 2)
        _require(port not in seen_ports, f"{context}.port is duplicated")
        seen_ports.add(port)
        normalized_players.append({
            "port": port,
            "character_kind": _integer(player["character_kind"], f"{context}.character_kind", -128, 127),
            "costume": _integer(player["costume"], f"{context}.costume", 0, 255),
            "stocks": _integer(player["stocks"], f"{context}.stocks", -128, 127),
            "player_type": player_type,
            "rumble_enabled": _boolean(player["rumble_enabled"], f"{context}.rumble_enabled"),
        })
        if player_type == 1:
            _require(not normalized_players[-1]["rumble_enabled"],
                     f"{context}.rumble_enabled must be false for a CPU")
            normalized_players[-1].update({
                "cpu_kind": _integer(player["cpu_kind"], f"{context}.cpu_kind", 4, 4),
                "cpu_level": _integer(player["cpu_level"], f"{context}.cpu_level", 1, 9),
            })
    _require(seen_ports == {1, 2}, "expected_setup.players must name ports 1 and 2")
    normalized = {
        "players": normalized_players,
        "stage": _integer(value["stage"], "expected_setup.stage", 0, 0xffff),
        "match_kind": _integer(value["match_kind"], "expected_setup.match_kind", 0, 7),
        "timer_enabled": _boolean(value["timer_enabled"], "expected_setup.timer_enabled"),
        "timer_counts_up": _boolean(value["timer_counts_up"], "expected_setup.timer_counts_up"),
        "time_limit_seconds": _integer(value["time_limit_seconds"], "expected_setup.time_limit_seconds", 0, 0xffffffff),
        "is_stock": _boolean(value["is_stock"], "expected_setup.is_stock"),
        "disable_pausing": _boolean(value["disable_pausing"], "expected_setup.disable_pausing"),
        "is_teams": _boolean(value["is_teams"], "expected_setup.is_teams"),
        "item_frequency": _integer(value["item_frequency"], "expected_setup.item_frequency", -128, 127),
        "item_mask_hex": value["item_mask_hex"],
        "damage_ratio_bits": value["damage_ratio_bits"],
        "game_speed_bits": value["game_speed_bits"],
    }
    _require(isinstance(normalized["item_mask_hex"], str) and
             HEX16.fullmatch(normalized["item_mask_hex"]) is not None,
             "expected_setup.item_mask_hex must be 16 lowercase hex digits")
    for field in ("damage_ratio_bits", "game_speed_bits"):
        _require(isinstance(normalized[field], str) and HEX8.fullmatch(normalized[field]) is not None,
                 f"expected_setup.{field} must be 8 lowercase hex digits")
    return normalized


def _u16(raw: bytes, offset: int) -> int:
    return int.from_bytes(raw[offset:offset + 2], "big")


def _u32(raw: bytes, offset: int) -> int:
    return int.from_bytes(raw[offset:offset + 4], "big")


def _s8(value: int) -> int:
    return value - 256 if value >= 128 else value


def _decode_setup(start_melee_hex: Any) -> dict[str, Any]:
    _require(isinstance(start_melee_hex, str) and
             len(start_melee_hex) == SETUP_BYTES * 2 and
             re.fullmatch(r"[0-9a-fA-F]+", start_melee_hex) is not None,
             "capture start_melee_hex is not a complete 0x138-byte setup")
    raw = bytes.fromhex(start_melee_hex)
    # These fields are part of the source-owned ordinary VS profile, but are
    # deliberately separate from the donor-declared settings below.  The
    # defaults come from gm_SetupRulesDefaults (x2C=1.0, timer hours off,
    # x14=0, friendly fire off), gmVsMelee_EnterVs (is_vs=true), and the
    # native setup decoder's pointer rejection.  Do not accept a capture that
    # would enter a different source profile merely because the declared
    # donor fields happen to match.
    _require(raw[4] & 0x40,
             "capture setup requires the ordinary VS is_vs profile")
    _require((raw[1] & 0x02) == 0,
             "capture setup requires timer_shows_hours to be false")
    _require((raw[1] & 0x01) == 0,
             "capture setup requires friendly_fire to be false")
    _require(raw[0x14] == 0,
             "capture setup requires the ordinary zero timer subframe")
    _require(raw[0x2C:0x30].hex() == SUPPORTED_CAMERA_BITS,
             "capture setup requires the ordinary camera scale 1.0")
    _require(not any(raw[0x38:0x5C]),
             "capture setup contains an unsupported callback/data pointer")
    actual = {
        "stage": _u16(raw, 0x0E),
        "match_kind": raw[0] >> 5,
        "timer_enabled": bool((raw[0] >> 1) & 1),
        "timer_counts_up": bool(raw[0] & 1),
        "time_limit_seconds": _u32(raw, 0x10),
        "is_stock": bool((raw[2] >> 7) & 1),
        "disable_pausing": bool((raw[2] >> 3) & 1),
        "is_teams": bool(raw[8]),
        "item_frequency": _s8(raw[0x0B]),
        "item_mask_hex": raw[0x20:0x28].hex(),
        "damage_ratio_bits": raw[0x30:0x34].hex(),
        "game_speed_bits": raw[0x34:0x38].hex(),
        "players": [],
    }
    _require(raw[8] in (0, 1), "capture setup has an invalid is_teams byte")
    for index in range(6):
        base = 0x60 + index * 0x24
        slot = raw[base + 4]
        port = index + 1
        if index < 2 and raw[base + 1] in (0, 1):
            _require(slot in (0, port),
                     f"capture setup has an invalid slot byte at player index {index}")
            player = {
                "port": port,
                "character_kind": _s8(raw[base]),
                "costume": raw[base + 3],
                "stocks": _s8(raw[base + 2]),
                "player_type": raw[base + 1],
                "rumble_enabled": bool((raw[base + 12] >> 7) & 1),
            }
            if raw[base + 1] == 1:
                _require(not player["rumble_enabled"],
                         f"capture setup requires CPU rumble disabled at player index {index}")
                _require(raw[base + 14] == 4 and 1 <= raw[base + 15] <= 9,
                         f"capture setup has an unsupported ordinary-VS CPU at player index {index}")
                player.update({"cpu_kind": raw[base + 14], "cpu_level": raw[base + 15]})
            actual["players"].append(player)
        elif index >= 2:
            _require(raw[base + 1] == 3,
                     f"capture setup has an unsupported active player record at index {index}")
    actual["players"].sort(key=lambda player: player["port"])
    _require(len(actual["players"]) == 2 and
             [player["port"] for player in actual["players"]] == [1, 2],
             "capture setup does not contain exactly supported ports 1 and 2")
    return actual


def _first_mismatch(expected: Any, actual: Any, path: str = "") -> tuple[str, Any, Any] | None:
    if type(expected) is not type(actual):
        return path or "$", expected, actual
    if isinstance(expected, dict):
        for key in expected:
            child = f"{path}.{key}" if path else key
            if key not in actual:
                return child, expected[key], None
            mismatch = _first_mismatch(expected[key], actual[key], child)
            if mismatch:
                return mismatch
        for key in actual:
            child = f"{path}.{key}" if path else key
            if key not in expected:
                return child, None, actual[key]
        return None
    if isinstance(expected, list):
        if len(expected) != len(actual):
            return f"{path}.length", len(expected), len(actual)
        for index, (left, right) in enumerate(zip(expected, actual)):
            mismatch = _first_mismatch(left, right, f"{path}[{index}]")
            if mismatch:
                return mismatch
        return None
    return None if expected == actual else (path or "$", expected, actual)


def _canonical_plan_hash(plan: dict[str, Any]) -> str:
    payload = json.dumps(plan, sort_keys=True, separators=(",", ":")) + "\n"
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _verify_capture_prefix(capture: Any, full_plan: dict[str, Any]) -> str:
    frames = getattr(capture, "frames", None)
    _require(isinstance(frames, (list, tuple)) and 1 <= len(frames) <= len(full_plan["frames"]),
             "capture frame timeline is not a nonempty prefix of the full input plan")
    try:
        verify_entry(full_plan, capture.match_enter["start_melee_hex"])
    except (KeyError, IndexError, TypeError, ValueError) as error:
        raise SetupValidationError(
            f"capture setup metadata does not match the frozen input plan: {error}") from error
    for index, frame in enumerate(frames):
        _require(isinstance(frame, dict), f"capture frame {index} is not an object")
        try:
            consumed = frame["consumed_inputs"]
        except (KeyError, TypeError) as error:
            raise SetupValidationError(f"capture frame {index} is missing consumed_inputs") from error
        _require(isinstance(consumed, list) and len(consumed) == 1 and
                 isinstance(consumed[0], list) and len(consumed[0]) == 4,
                 f"capture frame {index} has an invalid consumed input vector")
        try:
            verify_tick(full_plan, index, consumed[0])
        except (IndexError, KeyError, TypeError, ValueError) as error:
            raise SetupValidationError(
                f"capture input prefix mismatch at tick {index}: {error}") from error
    prefix = dict(full_plan)
    prefix["frames"] = list(full_plan["frames"][:len(frames)])
    prefix_sha256 = _canonical_plan_hash(prefix)
    provenance = capture.header.get("provenance", {})
    capture_plan_sha256 = provenance.get("input_plan_sha256")
    _require(capture_plan_sha256 == prefix_sha256,
             "capture input_plan_sha256 does not match the exact frozen-plan prefix")
    return prefix_sha256


def validate_setup(plan_path: str | Path, name: str, capture_path: str | Path,
                   *, cpu: str = "Interpreter64", repo_root: str | Path | None = None) -> dict[str, Any]:
    """Validate one named development entry against a complete retail capture."""

    plan_file = Path(plan_path).resolve()
    root = Path(repo_root).resolve() if repo_root is not None else None
    execution, execution_sha256 = _read_json(plan_file)
    _require(execution.get("schema") == EXECUTION_SCHEMA and
             type(execution.get("version")) is int and
             execution["version"] == EXECUTION_VERSION,
             "unsupported execution plan schema or version")
    entries = execution.get("selected_before_reference_execution")
    _require(isinstance(entries, list), "execution plan is missing development entries")
    matches = [entry for entry in entries if isinstance(entry, dict) and entry.get("name") == name]
    _require(len(matches) == 1, f"execution plan must contain exactly one entry named {name!r}")
    entry = matches[0]
    _require(entry.get("role") == "development", f"entry {name!r} is not a development workload")
    expected = _validate_expected_setup(entry.get("expected_setup"))
    source_sha256 = _hash(entry.get("source_sha256"), f"entry {name}.source_sha256")
    declared_plan_sha256 = _hash(entry.get("plan_sha256"), f"entry {name}.plan_sha256")
    candidate_manifest_sha256 = _hash(
        execution.get("candidate_manifest_sha256"), "execution_plan.candidate_manifest_sha256")

    source_file = _resolve(entry.get("source"), root, f"entry {name}.source")
    input_plan_file = _resolve(entry.get("plan"), root, f"entry {name}.plan")
    actual_source_sha256 = _sha256(source_file, "donor source")
    _require(actual_source_sha256 == source_sha256,
             f"donor source SHA-256 mismatch for {name}")
    input_plan, input_plan_sha256 = load_plan(input_plan_file)
    _require(input_plan_sha256 == declared_plan_sha256,
             f"input plan SHA-256 mismatch for {name}")
    _require(input_plan.get("source_sha256") == source_sha256,
             f"input plan source SHA-256 disagrees for {name}")

    capture = load_capture(capture_path, cpu=cpu)
    capture_plan_sha256 = _verify_capture_prefix(capture, input_plan)
    actual = _decode_setup(capture.match_enter.get("start_melee_hex"))
    mismatch = _first_mismatch(expected, actual, "setup")
    if mismatch is not None:
        raise SetupValidationError(
            f"retail setup mismatch at {mismatch[0]}: expected {mismatch[1]!r}, got {mismatch[2]!r}")
    return {
        "schema": SCHEMA,
        "version": VERSION,
        "status": "pass",
        "scope": SCOPE,
        "entry": {"name": name, "role": entry["role"]},
        "manifest_sha256": execution_sha256,
        "execution_plan_sha256": execution_sha256,
        "candidate_manifest_sha256": candidate_manifest_sha256,
        "source_sha256": source_sha256,
        "input_plan_sha256": input_plan_sha256,
        "capture_input_plan_sha256": capture_plan_sha256,
        "capture_sha256": capture.sha256,
        "setup": {"status": "match", "expected": expected, "actual": actual},
    }


check_setup = validate_setup
