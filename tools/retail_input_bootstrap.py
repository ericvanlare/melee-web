"""Strict two-pass calibration for the retail Pipe input bootstrap.

The calibration records the final construction PADRead that precedes the
first source queue consume.  It is input timing evidence only; it contains no
game state and cannot be used to alter a source vector.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from typing import Any


SCHEMA = "melee-web-retail-input-bootstrap-calibration"
VERSION = 1
RUNTIME_SCHEMA = "melee-web-retail-input-bootstrap-runtime"
RUNTIME_VERSION = 1
MAX_BYTES = 1024 * 1024
MAX_CONSTRUCTION_PAD_READS = 4096
RETRACE_COUNT_ADDRESS = "0x804d7420"
INPUT_BUFFER_VCOUNT_ADDRESS = "0x804a7f98"
VI_GET_RETRACE_COUNT_ADDRESS = "0x8035017c"
VI_GET_RETRACE_COUNT_WORDS = [0x806DBD80, 0x4E800020]


class BootstrapCalibrationError(ValueError):
    """A calibration is malformed or does not bind to this run."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise BootstrapCalibrationError(message)


def _hex(value: Any, length: int, label: str) -> str:
    _require(isinstance(value, str) and re.fullmatch(rf"[0-9a-f]{{{length}}}", value) is not None,
             f"{label} must be lowercase hexadecimal")
    return value


def _read_json(path: str | Path) -> tuple[dict[str, Any], str]:
    path = Path(path)
    try:
        size = path.stat().st_size
        _require(size <= MAX_BYTES, "bootstrap calibration exceeds byte limit")
        with path.open("rb") as stream:
            raw = stream.read(MAX_BYTES + 1)
    except OSError as error:
        raise BootstrapCalibrationError(f"cannot read bootstrap calibration: {error}") from error
    _require(len(raw) <= MAX_BYTES, "bootstrap calibration exceeds byte limit")
    try:
        value = json.loads(raw.decode("utf-8"), object_pairs_hook=_reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BootstrapCalibrationError(f"invalid bootstrap calibration JSON: {error}") from error
    _require(isinstance(value, dict), "bootstrap calibration must be a JSON object")
    return value, hashlib.sha256(raw).hexdigest()


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, item in pairs:
        if key in result:
            raise BootstrapCalibrationError(f"duplicate JSON key {key!r}")
        result[key] = item
    return result


def validate_runtime_binding(value: dict[str, Any]) -> dict[str, Any]:
    """Validate the per-run files excluded from immutable provenance.json."""

    expected_keys = {
        "schema", "version", "dolphin_ini_canonical_sha256", "gcpad_ini_sha256",
        "external_save_hashes",
    }
    _require(set(value) == expected_keys,
             "bootstrap runtime binding has missing or unexpected fields")
    _require(type(value["schema"]) is str and value["schema"] == RUNTIME_SCHEMA
             and type(value["version"]) is int
             and not isinstance(value["version"], bool)
             and value["version"] == RUNTIME_VERSION,
             "unsupported bootstrap runtime binding schema")
    for key in ("dolphin_ini_canonical_sha256", "gcpad_ini_sha256"):
        _hex(value[key], 64, key)
    saves = value["external_save_hashes"]
    _require(isinstance(saves, dict), "bootstrap runtime external_save_hashes is invalid")
    for save_path, save_hash in saves.items():
        _require(isinstance(save_path, str),
                 "bootstrap runtime external save path is invalid")
        _hex(save_hash, 64, f"external_save_hashes[{save_path!r}]")
    return value


def runtime_binding_record(*, dolphin_ini_canonical_sha256: str,
                           gcpad_ini_sha256: str,
                           external_save_hashes: dict[str, str]) -> dict[str, Any]:
    value = {
        "schema": RUNTIME_SCHEMA,
        "version": RUNTIME_VERSION,
        "dolphin_ini_canonical_sha256": dolphin_ini_canonical_sha256,
        "gcpad_ini_sha256": gcpad_ini_sha256,
        "external_save_hashes": external_save_hashes,
    }
    return validate_runtime_binding(value)


def load_runtime_binding(path: str | Path) -> tuple[dict[str, Any], str]:
    value, digest = _read_json(path)
    return validate_runtime_binding(value), digest


def write_runtime_binding(path: str | Path, value: dict[str, Any]) -> None:
    validate_runtime_binding(value)
    path = Path(path)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
    except OSError as error:
        raise BootstrapCalibrationError(
            f"cannot write bootstrap runtime binding: {error}") from error


def validate_calibration(value: dict[str, Any], *, plan: dict[str, Any],
                         plan_sha256: str, runtime: dict[str, Any],
                         collector_sha256: str | None = None) -> dict[str, Any]:
    """Validate a calibration against the exact input plan and run identity."""

    expected_keys = {
        "schema", "version", "mode", "input_plan_sha256", "input_plan_frames",
        "input_plan_source_sha256", "input_plan_policy", "input_plan_first_frame",
        "input_plan_source_stage", "input_plan_source_characters", "first_input",
        "construction_pad_reads", "last_construction_pad_read", "retrace_count_address",
        "source_vi_count_address", "vi_get_retrace_count_address",
        "vi_get_retrace_count_words", "provenance", "collector_sha256",
    }
    _require(set(value) == expected_keys, "bootstrap calibration has missing or unexpected fields")
    _require(type(value["schema"]) is str and value["schema"] == SCHEMA
             and type(value["version"]) is int and not isinstance(value["version"], bool)
             and value["version"] == VERSION,
             "unsupported bootstrap calibration schema")
    _require(value["mode"] == "construction-last-padread",
             "unsupported bootstrap calibration mode")
    _hex(value["input_plan_sha256"], 64, "input_plan_sha256")
    _require(value["input_plan_sha256"] == plan_sha256,
             "bootstrap calibration input plan hash differs from this plan")
    _hex(plan_sha256, 64, "plan_sha256")
    _require(value["input_plan_source_sha256"] == plan["source_sha256"],
             "bootstrap calibration source hash differs from this plan")
    _require(type(value["input_plan_frames"]) is int
             and not isinstance(value["input_plan_frames"], bool)
             and value["input_plan_frames"] == len(plan["frames"])
             and len(plan["frames"]) > 1,
             "bootstrap calibration plan length differs from this plan")
    for key in ("input_plan_policy", "input_plan_first_frame", "input_plan_source_stage",
                "input_plan_source_characters"):
        _require(value[key] == {
            "input_plan_policy": plan["policy"],
            "input_plan_first_frame": plan["first_frame"],
            "input_plan_source_stage": plan["source_stage"],
            "input_plan_source_characters": plan["source_characters"],
        }[key], f"bootstrap calibration {key} differs from this plan")
    _require(isinstance(value["first_input"], list) and len(value["first_input"]) == 2,
             "bootstrap calibration first_input is invalid")
    _require(value["first_input"] == plan["frames"][0],
             "bootstrap calibration first input differs from this plan")
    _require(isinstance(value["construction_pad_reads"], int)
             and not isinstance(value["construction_pad_reads"], bool)
             and 0 < value["construction_pad_reads"] <= MAX_CONSTRUCTION_PAD_READS,
             "bootstrap calibration construction PADRead count is outside the bounded range")
    identity = value["last_construction_pad_read"]
    _require(isinstance(identity, dict), "bootstrap calibration last PADRead identity is invalid")
    identity_keys = {"ordinal", "scene_frame", "retrace_count", "source_vi_count",
                     "caller", "stack", "queue_hex", "raw_hex"}
    _require(set(identity) == identity_keys, "bootstrap calibration PADRead identity is incomplete")
    _require(identity["ordinal"] == value["construction_pad_reads"] - 1,
             "bootstrap calibration PADRead ordinal is not the final observation")
    for key in ("ordinal", "scene_frame", "retrace_count", "source_vi_count", "caller", "stack"):
        _require(isinstance(identity[key], int) and not isinstance(identity[key], bool)
                 and 0 <= identity[key] <= 0xffffffff,
                 f"bootstrap calibration {key} is invalid")
    _require(identity["caller"] == 0x80376A28,
             "bootstrap calibration PADRead caller is not HSD_PadRenewRawStatus")
    _hex(identity["queue_hex"], 24, "bootstrap calibration queue_hex")
    _hex(identity["raw_hex"], 96, "bootstrap calibration raw_hex")
    _require(value["retrace_count_address"] == RETRACE_COUNT_ADDRESS,
             "bootstrap calibration retrace count address is not pinned")
    _require(value["source_vi_count_address"] == INPUT_BUFFER_VCOUNT_ADDRESS,
             "bootstrap calibration InputBufferVcount address is not pinned")
    _require(value["vi_get_retrace_count_address"] == VI_GET_RETRACE_COUNT_ADDRESS
             and value["vi_get_retrace_count_words"] == VI_GET_RETRACE_COUNT_WORDS,
             "bootstrap calibration VI getter identity is not pinned")
    provenance = value["provenance"]
    _require(isinstance(provenance, dict), "bootstrap calibration provenance is invalid")
    for key in ("dol_sha1", "dolphin_binary_sha256", "source_revision", "cpu",
                "cpu_thread", "cheats", "background_input", "fixed_rtc",
                "setup_snapshot_sha256", "dolphin_ini_canonical_sha256",
                "gcpad_ini_sha256"):
        _require(provenance.get(key) == runtime.get(key),
                 f"bootstrap calibration provenance mismatch for {key}")
    _require(provenance.get("external_save_hashes") == runtime.get("external_save_hashes"),
             "bootstrap calibration provenance mismatch for external_save_hashes")
    for key in ("dolphin_ini_canonical_sha256", "gcpad_ini_sha256"):
        _hex(provenance.get(key), 64, key)
    saves = provenance.get("external_save_hashes")
    _require(isinstance(saves, dict), "bootstrap calibration external_save_hashes is invalid")
    for save_path, save_hash in saves.items():
        _require(isinstance(save_path, str), "bootstrap calibration external save path is invalid")
        _hex(save_hash, 64, f"external_save_hashes[{save_path!r}]")
    _hex(value["collector_sha256"], 64, "collector_sha256")
    if collector_sha256 is not None:
        _require(value["collector_sha256"] == collector_sha256,
                 "bootstrap calibration collector hash differs from current collector")
    return value


def load_calibration(path: str | Path, *, plan: dict[str, Any], plan_sha256: str,
                     runtime: dict[str, Any], collector_sha256: str | None = None) -> tuple[dict[str, Any], str]:
    value, digest = _read_json(path)
    return validate_calibration(value, plan=plan, plan_sha256=plan_sha256,
                                runtime=runtime, collector_sha256=collector_sha256), digest


def calibration_record(*, plan: dict[str, Any], plan_sha256: str, provenance: dict[str, Any],
                       collector_sha256: str, construction_pad_reads: int,
                       last_construction_pad_read: dict[str, Any]) -> dict[str, Any]:
    """Create the sidecar emitted by the read-only collector."""

    return {
        "schema": SCHEMA,
        "version": VERSION,
        "mode": "construction-last-padread",
        "input_plan_sha256": plan_sha256,
        "input_plan_frames": len(plan["frames"]),
        "input_plan_source_sha256": plan["source_sha256"],
        "input_plan_policy": plan["policy"],
        "input_plan_first_frame": plan["first_frame"],
        "input_plan_source_stage": plan["source_stage"],
        "input_plan_source_characters": plan["source_characters"],
        "first_input": plan["frames"][0],
        "construction_pad_reads": construction_pad_reads,
        "last_construction_pad_read": last_construction_pad_read,
        "retrace_count_address": RETRACE_COUNT_ADDRESS,
        "source_vi_count_address": INPUT_BUFFER_VCOUNT_ADDRESS,
        "vi_get_retrace_count_address": VI_GET_RETRACE_COUNT_ADDRESS,
        "vi_get_retrace_count_words": VI_GET_RETRACE_COUNT_WORDS,
        "provenance": {
            key: provenance.get(key) for key in (
                "dol_sha1", "dolphin_binary_sha256", "source_revision", "cpu",
                "cpu_thread", "cheats", "background_input", "fixed_rtc",
                "setup_snapshot_sha256", "dolphin_ini_canonical_sha256", "gcpad_ini_sha256",
                "external_save_hashes")
        },
        "collector_sha256": collector_sha256,
    }


def write_calibration(path: str | Path, value: dict[str, Any]) -> None:
    path = Path(path)
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("x", encoding="utf-8") as stream:
            json.dump(value, stream, sort_keys=True, separators=(",", ":"))
            stream.write("\n")
    except OSError as error:
        raise BootstrapCalibrationError(f"cannot write bootstrap calibration: {error}") from error
