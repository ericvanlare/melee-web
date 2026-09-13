#!/usr/bin/env python3
"""Strict, non-admitting comparison of an ended CPU-register source prefix."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any

from retail_input_plan import load_plan
from retail_replay_validation import (
    CaptureError, MAX_FRAMES, MULTIPLAYER_VERSION, _DuplicateKey,
    _first_difference, _lower_hex_strings, _reject_duplicate_keys,
    compare_validated, load_capture,
)

SCHEMA = "melee-web-retail-cpu-register-diagnostic"
MAX_BYTES = 128 * 1024 * 1024
SCOPE = "ended bounded source-state prefix diagnostic; no candidate or gold admission"
IDENTITY_FIELDS = (
    "game_revision", "cpu", "dol_sha1", "source_revision",
    "dolphin_binary_sha256", "dolphin_version", "dolphin_commit",
    "disc_dol_sha1", "disc_image_sha256",
)


class PrefixError(CaptureError):
    pass


class _Prefix:
    __slots__ = ("capture", "sha256")
    def __init__(self, capture: Any):
        self.capture, self.sha256 = capture, capture.sha256 or ""


def _need(value: bool, message: str) -> None:
    if not value:
        raise PrefixError(message)


def _sha(value: Any, context: str) -> str:
    _need(isinstance(value, str) and len(value) == 64, f"{context}: expected SHA-256 hex")
    try:
        int(value, 16)
    except ValueError as error:
        raise PrefixError(f"{context}: expected SHA-256 hex") from error
    return value.lower()


def _read(path: Path, context: str) -> bytes:
    try:
        return path.read_bytes()
    except OSError as error:
        raise PrefixError(f"{context} cannot be read: {error}") from error


def _read_metadata(path: Path) -> tuple[dict[str, Any], str]:
    raw = _read(path, "diagnostic metadata")
    _need(bool(raw) and len(raw) <= MAX_BYTES, "diagnostic metadata is empty or too large")
    try:
        value = json.loads(raw.decode(), object_pairs_hook=_reject_duplicate_keys)
    except (UnicodeDecodeError, json.JSONDecodeError, _DuplicateKey) as error:
        raise PrefixError(f"diagnostic metadata is invalid JSON: {error}") from error
    _need(isinstance(value, dict), "diagnostic metadata must be a JSON object")
    return value, hashlib.sha256(raw).hexdigest()


def _plan(path: Any, declared: Any, context: str) -> tuple[Path, dict[str, Any], str]:
    _need(isinstance(path, str) and bool(path), f"{context}.path is required")
    target, expected = Path(path).expanduser().resolve(), _sha(declared, f"{context}.sha256")
    try:
        value, actual = load_plan(target)
    except (OSError, ValueError) as error:
        raise PrefixError(f"{context} failed strict input-plan validation: {error}") from error
    _need(actual == expected, f"{context} bytes differ from declared SHA-256")
    return target, value, expected


def _load_ended_prefix(path: Path, cpu: str) -> _Prefix:
    try:
        capture = load_capture(path, cpu=cpu)
    except CaptureError as error:
        raise PrefixError(f"ended prefix failed strict retail validation: {error}") from error
    _need(capture.end.get("record") == "end" and capture.end.get("status") == "captured", "ended prefix requires the existing captured end record")
    return _Prefix(capture)


def _collector_bundle(collector: dict[str, Any]) -> tuple[str, str]:
    source_value = collector.get("input_collector")
    _need(isinstance(source_value, str) and bool(source_value), "diagnostic.collector.input_collector is required")
    source = Path(source_value).expanduser().resolve()
    _need(source.is_file(), "diagnostic input collector source is unavailable")
    source_hash = _sha(collector.get("input_collector_sha256"), "diagnostic.collector.input_collector_sha256")
    source_bytes = _read(source, "diagnostic input collector")
    _need(hashlib.sha256(source_bytes).hexdigest() == source_hash,
          "diagnostic input collector hash does not match its source")
    helpers = collector.get("helpers")
    names = ("reference_replay_boundary.py", "retail_input_plan.py", "retail_input_bootstrap.py")
    _need(isinstance(helpers, list) and len(helpers) == len(names), "diagnostic collector helper list is incomplete")
    parts = [source_bytes]
    for item, name in zip(helpers, names):
        _need(isinstance(item, dict) and item.get("name") == name, "diagnostic collector helper identity is invalid")
        helper = source.with_name(name)
        _need(helper.is_file(), f"diagnostic collector helper is unavailable: {name}")
        helper_bytes = _read(helper, f"diagnostic collector helper {name}")
        _need(_sha(item.get("sha256"), f"diagnostic collector helper {name}") == hashlib.sha256(helper_bytes).hexdigest(), f"diagnostic collector helper hash differs: {name}")
        if "bytes" in item:
            _need(type(item["bytes"]) is int and item["bytes"] == len(helper_bytes), f"diagnostic collector helper byte count differs: {name}")
        parts.extend((b"\0", helper_bytes))
    optional = source.with_name("retail_cpu_observation.py")
    if optional.is_file():
        parts.extend((b"\0", _read(optional, "diagnostic CPU observer")))
    return source_hash, hashlib.sha256(b"".join(parts)).hexdigest()


def _identity(cap: Any) -> dict[str, Any]:
    provenance = cap.header["provenance"]
    return {key: cap.header.get(key) if key == "game_revision" else provenance.get(key) for key in IDENTITY_FIELDS}


def _identity_binding(metadata: dict[str, Any], prefix: _Prefix, gold_a: Any, gold_b: Any) -> dict[str, Any]:
    captures, declared = [_identity(cap) for cap in (gold_a, gold_b, prefix.capture)], metadata.get("identity")
    _need(declared is None or isinstance(declared, dict), "metadata.identity must be an object")
    result = {}
    for key in IDENTITY_FIELDS:
        values = [item[key] for item in captures]
        present = [value for value in values if value is not None]
        _need(not present or len(present) == 3 and all(value == present[0] for value in present),
              f"capture identity differs for {key}")
        value = declared.get(key) if declared is not None else None
        if value is not None:
            _need(not present or value == present[0], f"metadata.identity differs for {key}")
            result[key] = {"capture": present[0] if present else None, "metadata": value}
    return result


def _metadata_binding(path: Path, prefix_path: Path, prefix: _Prefix,
                      gold_a: Any, gold_b: Any) -> dict[str, Any]:
    metadata, metadata_sha = _read_metadata(path)
    _need(metadata.get("schema") == SCHEMA and type(metadata.get("version")) is int and metadata.get("version") == 1,
          "diagnostic metadata schema/version is unsupported")
    status = metadata.get("status")
    _need(status in ("diagnostic_only", "diagnostic_failed"),
          "diagnostic metadata status must be diagnostic_only")
    _need(metadata.get("evidence_status") == "diagnostic_only",
          "diagnostic metadata evidence_status must be diagnostic_only")
    _need(metadata.get("candidate_admission") == "forbidden",
          "diagnostic metadata must forbid candidate admission")
    diagnostic, owned = metadata.get("diagnostic"), metadata.get("owned")
    _need(isinstance(diagnostic, dict) and isinstance(owned, dict),
          "diagnostic and owned metadata are required")
    window, collector = diagnostic.get("window"), diagnostic.get("collector")
    _need(isinstance(window, dict) and isinstance(collector, dict),
          "diagnostic.window and diagnostic.collector are required")
    start, end = window.get("start_tick"), window.get("end_tick")
    _need(type(start) is int and type(end) is int and 0 <= start <= end < MAX_FRAMES, "diagnostic window is invalid")
    capture_path = owned.get("capture_prefix")
    _need(isinstance(capture_path, str) and bool(capture_path), "owned.capture_prefix is required")
    _need(Path(capture_path).expanduser().resolve() == prefix_path.resolve(),
          "owned.capture_prefix does not identify the supplied prefix")
    diagnostic_collector = _sha(owned.get("collector_sha256"), "owned.collector_sha256")
    _need(diagnostic_collector == prefix.capture.header["collector_sha256"].lower(),
          "owned.collector_sha256 differs from prefix header")
    requested = metadata.get("frames_requested")
    _need(type(requested) is int and 1 <= requested <= MAX_FRAMES,
          "frames_requested is outside its supported range")
    _need(requested == prefix.capture.header["frames_requested"] == len(prefix.capture.frames),
          "ended prefix count is not exactly its declared bound")
    _need(end + 1 == requested, "diagnostic.window.end_tick does not match ended prefix bound")
    full_path, full_plan, full_hash = _plan(
        diagnostic.get("full_input_plan_path"), diagnostic.get("full_input_plan_sha256"),
        "diagnostic.full_input_plan")
    prefix_plan_path, prefix_plan, prefix_hash = _plan(
        diagnostic.get("executed_input_prefix_path"),
        diagnostic.get("executed_input_prefix_sha256"), "diagnostic.executed_input_prefix")
    _need(len(full_plan["frames"]) == len(gold_a.frames) == len(gold_b.frames),
          "full input plan is not the complete gold timeline")
    _need(len(prefix_plan["frames"]) == len(prefix.capture.frames),
          "executed input prefix length differs from ended prefix")
    without = lambda value: {key: item for key, item in value.items() if key != "frames"}
    _need(prefix_plan["frames"] == full_plan["frames"][:len(prefix_plan["frames"])]
          and without(prefix_plan) == without(full_plan),
          "executed input prefix changed frames or input-plan configuration")
    gold_plan_a = _sha(gold_a.header["provenance"].get("input_plan_sha256"),
                       "gold_a input_plan_sha256")
    gold_plan_b = _sha(gold_b.header["provenance"].get("input_plan_sha256"),
                       "gold_b input_plan_sha256")
    _need(gold_plan_a == gold_plan_b == full_hash,
          "gold captures are not bound to the full input plan")
    _need(_sha(prefix.capture.header["provenance"].get("input_plan_sha256"),
               "prefix input_plan_sha256") == prefix_hash,
          "prefix is not bound to its executed input-plan prefix")
    original, gold_bundle = _collector_bundle(collector)
    _need({gold_a.header["collector_sha256"].lower(), gold_b.header["collector_sha256"].lower()} == {gold_bundle},
          "diagnostic input collector bundle does not pin both gold captures")
    _need(prefix.capture.header["capture_id"].lower() not in {
          gold_a.header["capture_id"].lower(), gold_b.header["capture_id"].lower()},
          "ended diagnostic prefix capture_id must differ from both gold IDs")
    return {"status": status, "evidence_status": metadata["evidence_status"],
            "metadata_sha256": metadata_sha, "prefix_sha256": prefix.sha256,
            "prefix_path": str(prefix_path), "capture_id": prefix.capture.header["capture_id"].lower(),
            "frames_requested": requested, "full_input_plan_path": str(full_path),
            "full_input_plan_sha256": full_hash, "executed_input_prefix_path": str(prefix_plan_path),
            "executed_input_prefix_sha256": prefix_hash,
            "original_collector_sha256": original, "gold_collector_bundle_sha256": gold_bundle,
            "diagnostic_collector_sha256": diagnostic_collector,
            "identity": _identity_binding(metadata, prefix, gold_a, gold_b),
            "failure_reason": metadata.get("error") if status == "diagnostic_failed" else None}


def _source_compare(gold: Any, prefix: _Prefix, label: str) -> dict[str, Any]:
    result = {"gold": label, "status": "exact", "frames_compared": len(prefix.capture.frames), "first_divergence": None}
    def check(record: str, frame: int | None, expected: Any, actual: Any) -> bool:
        difference = _first_difference(_lower_hex_strings(expected), _lower_hex_strings(actual), record)
        if difference is None:
            return True
        field, before, after = difference
        result.update(status="diverged", first_divergence={
            "record": record, "frame": frame, "field": field,
            "expected": before, "actual": after})
        return False
    if not check("match_enter", None, gold.match_enter, prefix.capture.match_enter):
        return result
    if not check("initial", None, gold.initial, prefix.capture.initial):
        return result
    for index, pair in enumerate(zip(gold.frames, prefix.capture.frames)):
        if not check(f"frame[{index}]", index, *pair):
            return result
    return result


def compare_prefix_paths(gold_a_path: str | Path, gold_b_path: str | Path,
                         prefix_path: str | Path, metadata_path: str | Path, *,
                         cpu: str = "Interpreter64", allow_failed: bool = False) -> dict[str, Any]:
    report = {"status": "invalid_capture", "diagnostic_only": True,
              "candidate_admitted": False, "gold_admitted": False,
              "scope": SCOPE, "first_divergence": None}
    try:
        gold_a, gold_b = load_capture(gold_a_path, cpu=cpu), load_capture(gold_b_path, cpu=cpu)
        report["full_gold_repeatability"] = compare_validated(gold_a, gold_b)
        _need(report["full_gold_repeatability"].get("status") == "repeatable",
              "full gold A/B pair is not repeatable; prefix was not checked")
        prefix = _load_ended_prefix(Path(prefix_path).expanduser().resolve(), cpu)
        metadata = _metadata_binding(Path(metadata_path).expanduser().resolve(), Path(prefix_path).expanduser().resolve(), prefix, gold_a, gold_b)
        _need(prefix.capture.header["version"] == gold_a.header["version"],
              "prefix and gold schema versions differ")
        if prefix.capture.header["version"] == MULTIPLAYER_VERSION:
            _need(prefix.capture.active_player_count == gold_a.active_player_count,
                  "prefix and gold active player counts differ")
        _need(len(prefix.capture.frames) <= len(gold_a.frames),
              "prefix contains more frames than complete gold")
        comparisons = [_source_compare(gold_a, prefix, "a"), _source_compare(gold_b, prefix, "b")]
        report.update(metadata=metadata, source_comparisons=comparisons,
                      gold_frames=len(gold_a.frames), prefix_frames=len(prefix.capture.frames),
                      prefix_is_shorter_than_gold=len(prefix.capture.frames) < len(gold_a.frames),
                      bindings={label: {"capture_sha256": cap.sha256, "prefix_sha256": prefix.sha256,
                                        "full_input_plan_sha256": metadata["full_input_plan_sha256"],
                                        "original_collector_sha256": metadata["original_collector_sha256"]}
                                for label, cap in (("gold_a", gold_a), ("gold_b", gold_b))},
                      metadata_differences={
                          "collector_sha256": {"gold_a": gold_a.header["collector_sha256"].lower(),
                                               "gold_b": gold_b.header["collector_sha256"].lower(),
                                               "prefix": prefix.capture.header["collector_sha256"].lower(),
                                               "differs": prefix.capture.header["collector_sha256"].lower() != gold_a.header["collector_sha256"].lower()},
                          "frames_requested": {"gold_a": gold_a.header["frames_requested"],
                                               "gold_b": gold_b.header["frames_requested"],
                                               "prefix": prefix.capture.header["frames_requested"],
                                               "differs": prefix.capture.header["frames_requested"] != gold_a.header["frames_requested"]},
                          "capture_id": {"gold_a": gold_a.header["capture_id"].lower(),
                                         "gold_b": gold_b.header["capture_id"].lower(),
                                         "prefix": prefix.capture.header["capture_id"].lower(), "distinct": True},
                          "configuration_socket": {"available": False,
                                                    "reason": "current run metadata does not expose a socket field"},
                      })
        report["first_divergence"] = next((item["first_divergence"] for item in comparisons
                                            if item["first_divergence"] is not None), None)
        failed = metadata["status"] == "diagnostic_failed"
        if failed:
            _need(allow_failed, "diagnostic metadata status is diagnostic_failed; pass allow_failed explicitly")
            report["status"] = "diagnostic_failed"
            report["conclusion"] = {"status": "diagnostic_failed",
                                     "reason": metadata["failure_reason"] or "diagnostic failed without a reason",
                                     "source_state": "diverged" if report["first_divergence"] else "exact_through_prefix",
                                     "evidence_admission": "forbidden"}
        else:
            report["status"] = "diagnostic_diverged" if report["first_divergence"] else "diagnostic_prefix"
        return report
    except (PrefixError, CaptureError, OSError, TypeError, ValueError) as error:
        report["error"] = str(error)
        return report


def status_exit_code(status: str) -> int:
    return {"diagnostic_prefix": 0, "diagnostic_diverged": 1, "diagnostic_failed": 1, "invalid_capture": 2}.get(status, 2)
__all__ = ["PrefixError", "compare_prefix_paths", "status_exit_code"]
