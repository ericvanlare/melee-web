"""Durable planning and evidence storage for bounded browser hitch runs.

This module deliberately does not run a browser or interpret a hitch's cause.
It freezes a finite matrix, records one immutable attempt per slot, copies the
evidence supplied by the runner, and keeps independent deadline counters.  A
missing deadline measurement is represented as ``UNKNOWN`` and is never
silently changed to zero.
"""

from __future__ import annotations

import copy
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import uuid
from typing import Any, Iterable, Mapping


PLAN_SCHEMA = "melee-web-hitch-plan"
PLAN_VERSION = 1
START_SCHEMA = "melee-web-hitch-attempt-start"
FINISH_SCHEMA = "melee-web-hitch-attempt-finish"
RECORD_VERSION = 1
UNKNOWN = "UNKNOWN"

DEFAULT_NATIVE_TARGET_MS = 1000.0 / 60.0
DEFAULT_BROWSER_GAP_MS = 1000.0 / 30.0
DEFAULT_NATIVE_HARD_MS = 1000.0 / 30.0
DEFAULT_TIMEOUT_MS = 300000
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_ATTACHMENT_BYTES = 256 * 1024 * 1024
_SLOT_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]*$")
_SHA_RE = re.compile(r"^[0-9a-fA-F]{64}$")
_ATTEMPT_STATUSES = frozenset(
    {"completed", "aborted", "crashed", "interrupted", "timeout"}
)


class HitchCaptureError(ValueError):
    """A malformed plan or an unsafe evidence transition."""


def _require(condition: bool, message: str) -> None:
    if not condition:
        raise HitchCaptureError(message)


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace(
        "+00:00", "Z"
    )


def _json_bytes(value: Any) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")) + "\n").encode(
        "utf-8"
    )


def _json_hash(value: Any) -> str:
    return hashlib.sha256(_json_bytes(value)).hexdigest()


def sha256_file(path: str | os.PathLike[str]) -> str:
    """Hash a file without following a directory or imposing a size limit."""

    target = Path(path)
    _require(target.is_file(), f"Evidence identity is not a regular file: {target}")
    digest = hashlib.sha256()
    try:
        with target.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
    except OSError as error:
        raise HitchCaptureError(f"Cannot read evidence identity {target}: {error}") from error
    return digest.hexdigest()


def _read_bounded(path: Path, limit: int) -> bytes:
    """Read at most ``limit + 1`` bytes, so oversize input stays bounded."""

    chunks: list[bytes] = []
    total = 0
    try:
        with path.open("rb") as stream:
            while total <= limit:
                chunk = stream.read(min(1024 * 1024, limit - total + 1))
                if not chunk:
                    break
                chunks.append(chunk)
                total += len(chunk)
                if total > limit:
                    raise HitchCaptureError(
                        f"Evidence exceeds {limit} byte limit: {path}"
                    )
    except HitchCaptureError:
        raise
    except OSError as error:
        raise HitchCaptureError(f"Cannot read evidence {path}: {error}") from error
    return b"".join(chunks)


def _read_json(path: str | os.PathLike[str]) -> tuple[Any, str]:
    target = Path(path)
    raw = _read_bounded(target, MAX_JSON_BYTES)

    def unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            _require(key not in result, f"Duplicate JSON key in {target}: {key}")
            result[key] = value
        return result

    try:
        value = json.loads(raw, object_pairs_hook=unique)
    except (json.JSONDecodeError, UnicodeDecodeError) as error:
        raise HitchCaptureError(f"Invalid JSON evidence {target}: {error}") from error
    return value, hashlib.sha256(raw).hexdigest()


def _write_exclusive(path: Path, payload: bytes) -> None:
    """Publish one file from fsynced staging without replacing evidence.

    The staging name remains when writing, fsync, or exclusive publication
    fails.  A caller can therefore inspect the exact bytes that failed to
    become a record, while a later recovery attempt never has to overwrite a
    partially written final path.
    """

    path.parent.mkdir(parents=True, exist_ok=True)
    staging = path.with_name(f".{path.name}.staging-{uuid.uuid4().hex}")
    descriptor = None
    published = False
    try:
        descriptor = os.open(
            str(staging), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644
        )
        try:
            with os.fdopen(descriptor, "wb") as stream:
                descriptor = None
                stream.write(payload)
                stream.flush()
                os.fsync(stream.fileno())
        finally:
            if descriptor is not None:
                os.close(descriptor)
    except OSError as error:
        raise HitchCaptureError(f"Cannot write evidence {path}: {error}") from error
    try:
        os.link(str(staging), str(path))
        published = True
    except FileExistsError as error:
        # Leave the staged bytes in place.  The existing final record remains
        # authoritative and no failed publication is silently discarded.
        raise HitchCaptureError(f"Evidence already exists: {path}") from error
    except OSError as error:
        raise HitchCaptureError(f"Cannot publish evidence {path}: {error}") from error
    try:
        descriptor = os.open(str(path.parent), os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)
    except OSError:
        # Directory fsync is not available on every supported filesystem.  The
        # file itself is still fsynced and its creation remains exclusive.
        pass
    finally:
        if published:
            try:
                staging.unlink()
            except OSError:
                pass


def _bounded_file_hash(path: Path, limit: int) -> tuple[str, int]:
    digest = hashlib.sha256()
    total = 0
    try:
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                total += len(block)
                if total > limit:
                    raise HitchCaptureError(
                        f"Evidence exceeds {limit} byte limit: {path}"
                    )
                digest.update(block)
    except HitchCaptureError:
        raise
    except OSError as error:
        raise HitchCaptureError(f"Cannot read attachment {path}: {error}") from error
    return digest.hexdigest(), total


def _copy_exclusive(source: Path, destination: Path) -> str:
    """Publish a bounded copy atomically, recovering identical destinations.

    A failed stream remains as a uniquely named staging file.  It never
    replaces a final attachment and is therefore inspectable evidence rather
    than a half-written attachment that blocks a later finish retry.
    """

    _require(source.is_file(), f"Attachment is not a regular file: {source}")
    source_hash, _ = _bounded_file_hash(source, MAX_ATTACHMENT_BYTES)
    if destination.exists():
        _require(destination.is_file(), f"Attachment destination is not a file: {destination}")
        destination_hash, _ = _bounded_file_hash(destination, MAX_ATTACHMENT_BYTES)
        _require(destination_hash == source_hash,
                 f"Existing attachment differs: {destination}")
        return source_hash

    destination.parent.mkdir(parents=True, exist_ok=True)
    staging = destination.with_name(
        f".{destination.name}.staging-{uuid.uuid4().hex}"
    )
    descriptor = None
    linked = False
    try:
        descriptor = os.open(str(staging), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
        total = 0
        copied_hash = hashlib.sha256()
        try:
            with source.open("rb") as input_stream, os.fdopen(descriptor, "wb") as output_stream:
                descriptor = None
                for block in iter(lambda: input_stream.read(1024 * 1024), b""):
                    total += len(block)
                    if total > MAX_ATTACHMENT_BYTES:
                        raise HitchCaptureError(
                            f"Evidence exceeds {MAX_ATTACHMENT_BYTES} byte limit: {source}"
                        )
                    output_stream.write(block)
                    copied_hash.update(block)
                output_stream.flush()
                os.fsync(output_stream.fileno())
        finally:
            if descriptor is not None:
                os.close(descriptor)
        _require(copied_hash.hexdigest() == source_hash,
                 f"Attachment changed while copying: {source}")
        # A hard link publishes the completed staging inode without replacing
        # an existing final file.  A concurrent identical publisher is safe.
        try:
            os.link(str(staging), str(destination))
            linked = True
        except FileExistsError:
            destination_hash, _ = _bounded_file_hash(destination, MAX_ATTACHMENT_BYTES)
            _require(destination_hash == source_hash,
                     f"Existing attachment differs: {destination}")
            linked = True
        if linked:
            return source_hash
        return source_hash
    except OSError as error:
        raise HitchCaptureError(f"Cannot publish attachment {source}: {error}") from error
    finally:
        # Once linked, the staging name is only a second hard link to the same
        # bytes.  Failed streams remain for inspection and recovery.
        if linked:
            try:
                staging.unlink()
            except OSError:
                pass


def _resolved_path(value: str | os.PathLike[str], base: Path) -> Path:
    target = Path(value).expanduser()
    if not target.is_absolute():
        target = base / target
    return target.resolve(strict=False)


def _validate_sha(value: Any, context: str) -> str:
    _require(isinstance(value, str) and _SHA_RE.fullmatch(value) is not None,
             f"{context} must be a 64-character SHA-256")
    return value.lower()


def _identity(value: Any, *, base: Path, context: str) -> dict[str, str]:
    if isinstance(value, (str, os.PathLike)):
        source_path = _resolved_path(str(value), base)
        return {"path": str(source_path), "sha256": sha256_file(source_path)}
    _require(isinstance(value, Mapping), f"{context} must identify a file")
    raw_path = value.get("path")
    _require(isinstance(raw_path, str) and raw_path, f"{context}.path is required")
    source_path = _resolved_path(raw_path, base)
    actual = sha256_file(source_path)
    supplied = value.get("sha256")
    if supplied is not None:
        _require(_validate_sha(supplied, f"{context}.sha256") == actual,
                 f"{context} hash does not match {source_path}")
    return {"path": str(source_path), "sha256": actual}


def _normalize_artifacts(value: Any, *, base: Path) -> list[dict[str, str]]:
    if isinstance(value, Mapping):
        items: list[Any] = [dict(path=path, sha256=digest) for path, digest in value.items()]
    else:
        _require(isinstance(value, list), "identities.build_artifacts must be a list or path map")
        items = value
    _require(items, "At least one build artifact identity is required")
    result = [_identity(item, base=base, context=f"build_artifacts[{index}]")
              for index, item in enumerate(items)]
    _require(len({item["path"] for item in result}) == len(result),
             "Build artifact identities must have unique paths")
    return result


def _normalize_allowlist(value: Any) -> list[str]:
    _require(isinstance(value, list) and value,
             "development_allowlist must be an explicit non-empty list")
    result: list[str] = []
    for index, item in enumerate(value):
        target_id = item.get("id") if isinstance(item, Mapping) else item
        _require(isinstance(target_id, str) and target_id,
                 f"development_allowlist[{index}] must be a target id")
        _require(target_id not in result,
                 f"development_allowlist contains duplicate target {target_id}")
        result.append(target_id)
    return result


def _normalize_targets(value: Any) -> list[dict[str, Any]]:
    if isinstance(value, Mapping):
        raw_targets: list[Any] = []
        for target_id, details in value.items():
            if isinstance(details, Mapping):
                item = dict(details)
                item.setdefault("id", target_id)
            else:
                item = {"id": target_id, "value": details}
            raw_targets.append(item)
    else:
        _require(isinstance(value, list) and value, "targets must be a non-empty list")
        raw_targets = value
    result: list[dict[str, Any]] = []
    seen: set[str] = set()
    for index, item in enumerate(raw_targets):
        if isinstance(item, str):
            item = {"id": item}
        _require(isinstance(item, Mapping), f"targets[{index}] must be an object or id")
        target_id = item.get("id", item.get("target_id"))
        _require(isinstance(target_id, str) and target_id,
                 f"targets[{index}].id is required")
        _require(target_id not in seen, f"Duplicate target id: {target_id}")
        seen.add(target_id)
        target = copy.deepcopy(dict(item))
        target["id"] = target_id
        result.append(target)
    return result


def _normalize_modes(value: Any) -> list[str]:
    value = ["unprofiled", "profiler"] if value is None else value
    _require(isinstance(value, list) and value, "modes must be a non-empty list")
    result = []
    for item in value:
        _require(isinstance(item, str) and item, "modes must contain names")
        _require(item not in result, f"Duplicate mode: {item}")
        result.append(item)
    return result


def _normalize_caches(value: Any) -> list[str]:
    value = ["cold", "warm"] if value is None else value
    _require(isinstance(value, list) and value, "caches must be a non-empty list")
    result = []
    for item in value:
        _require(isinstance(item, str) and item, "caches must contain names")
        _require(item not in result, f"Duplicate cache profile: {item}")
        result.append(item)
    return result


def _normalize_repetitions(value: Any) -> int:
    if value is None:
        value = 2
    _require(type(value) is int and 1 <= value <= 1000,
             "repetitions must be a bounded integer between 1 and 1000")
    return value


def _slot_id(value: Any, ordinal: int) -> str:
    value = f"slot-{ordinal + 1:04d}" if value is None else str(value)
    _require(_SLOT_RE.fullmatch(value) is not None, f"Invalid slot id: {value}")
    return value


def _build_slots(spec: Mapping[str, Any], targets: list[dict[str, Any]],
                 allowlist: list[str]) -> list[dict[str, Any]]:
    target_ids = [target["id"] for target in targets]
    target_by_id = {target["id"]: target for target in targets}
    allowed_modes = _normalize_modes(spec.get("modes"))
    allowed_caches = _normalize_caches(spec.get("caches"))
    _require(set(target_ids) <= set(allowlist),
             "Every target must be explicitly present in development_allowlist")
    target_set = set(target_ids)
    supplied_slots = spec.get("slots")
    matrix_cells = spec.get("matrix") if supplied_slots is None else None
    raw_slots: list[dict[str, Any]] = []
    if supplied_slots is not None:
        _require(isinstance(supplied_slots, list) and supplied_slots,
                 "slots must be a non-empty list")
        for index, value in enumerate(supplied_slots):
            _require(isinstance(value, Mapping), f"slots[{index}] must be an object")
            slot = copy.deepcopy(dict(value))
            slot["slot_id"] = _slot_id(slot.get("slot_id", slot.get("id")), index)
            target_id = slot.get("target_id", slot.get("target"))
            _require(isinstance(target_id, str) and target_id in target_set,
                     f"slots[{index}] names an unknown target")
            _require(target_id in allowlist,
                     f"slot {slot['slot_id']} target is not in development_allowlist")
            slot["target_id"] = target_id
            if "frames" not in slot:
                if "frames" in target_by_id[target_id]:
                    slot["frames"] = target_by_id[target_id]["frames"]
                elif "frame_count" in target_by_id[target_id]:
                    slot["frames"] = target_by_id[target_id]["frame_count"]
            mode = slot.get("mode")
            cache = slot.get("cache")
            _require(isinstance(mode, str) and mode, f"slot {slot['slot_id']} needs mode")
            _require(isinstance(cache, str) and cache, f"slot {slot['slot_id']} needs cache")
            _require(mode in allowed_modes,
                     f"slot {slot['slot_id']} uses mode outside the frozen mode list")
            _require(cache in allowed_caches,
                     f"slot {slot['slot_id']} uses cache outside the frozen cache list")
            repetition = slot.get("repetition", index + 1)
            _require(type(repetition) is int and repetition >= 1,
                     f"slot {slot['slot_id']} has invalid repetition")
            slot["mode"], slot["cache"], slot["repetition"] = mode, cache, repetition
            raw_slots.append(slot)
    elif matrix_cells is not None:
        _require(isinstance(matrix_cells, list) and matrix_cells,
                 "matrix must be a non-empty list")
        default_repetitions = _normalize_repetitions(spec.get("repetitions"))
        for cell_index, cell_value in enumerate(matrix_cells):
            _require(isinstance(cell_value, Mapping),
                     f"matrix[{cell_index}] must be an object")
            cell = dict(cell_value)
            raw_targets = cell.get("target_ids", cell.get("targets", cell.get("target_id", cell.get("target"))))
            if isinstance(raw_targets, str):
                raw_targets = [raw_targets]
            _require(isinstance(raw_targets, list) and raw_targets,
                     f"matrix[{cell_index}] needs target or target_ids")
            repetitions = _normalize_repetitions(cell.get("repetitions", default_repetitions))
            mode, cache = cell.get("mode"), cell.get("cache")
            _require(mode in allowed_modes, f"matrix[{cell_index}] uses an unfrozen mode")
            _require(cache in allowed_caches, f"matrix[{cell_index}] uses an unfrozen cache")
            for target_id in raw_targets:
                _require(target_id in target_set and target_id in allowlist,
                         f"matrix[{cell_index}] names a target outside the development allowlist")
                for repetition in range(1, repetitions + 1):
                    raw_slots.append({
                        "target_id": target_id,
                        "mode": mode,
                        "cache": cache,
                        "repetition": repetition,
                        **({"frames": cell["frames"]} if "frames" in cell else {}),
                    })
    else:
        modes = _normalize_modes(spec.get("modes"))
        caches = _normalize_caches(spec.get("caches"))
        repetitions = _normalize_repetitions(spec.get("repetitions"))
        profiler_caches = spec.get("profiler_caches", ["warm"])
        _require(isinstance(profiler_caches, list) and profiler_caches,
                 "profiler_caches must be a non-empty list")
        # Mode/cache/target/repetition is the frozen order.  Profiler cold is
        # omitted by default because the performance matrix reserves cold for
        # unprofiled measurements; it can be explicitly requested in slots.
        for mode in modes:
            for cache in caches:
                if mode == "profiler" and cache not in profiler_caches:
                    continue
                for target_id in target_ids:
                    for repetition in range(1, repetitions + 1):
                        raw_slots.append({
                            "target_id": target_id,
                            "mode": mode,
                            "cache": cache,
                            "repetition": repetition,
                            **({"frames": target_by_id[target_id].get("frames", target_by_id[target_id].get("frame_count"))}
                               if ("frames" in target_by_id[target_id]
                                   or "frame_count" in target_by_id[target_id]) else {}),
                        })
    _require(raw_slots, "The matrix contains no slots")
    result: list[dict[str, Any]] = []
    seen_slots: set[str] = set()
    for index, slot in enumerate(raw_slots):
        slot = copy.deepcopy(slot)
        slot["slot_id"] = _slot_id(slot.get("slot_id"), index)
        _require(slot["slot_id"] not in seen_slots, f"Duplicate slot id: {slot['slot_id']}")
        seen_slots.add(slot["slot_id"])
        slot["ordinal"] = index
        # Each row carries the frozen wall-time bound so a resumed runner
        # cannot silently substitute a different timeout.
        slot["timeout_ms"] = int(spec.get("timeout_ms", DEFAULT_TIMEOUT_MS))
        result.append(slot)
    return result


def _normalize_recipe_identities(value: Any, *, base: Path,
                                 allowlist: list[str]) -> dict[str, dict[str, str]]:
    _require(isinstance(value, Mapping),
             "identities.development_recipes must map target ids to files")
    result: dict[str, dict[str, str]] = {}
    for target_id in allowlist:
        raw = value.get(target_id)
        _require(raw is not None,
                 f"Missing development recipe identity for target {target_id}")
        result[target_id] = _identity(raw, base=base,
                                      context=f"development_recipes[{target_id}]")
    return result


def _normalize_plan(spec: Mapping[str, Any], *, spec_path: Path,
                    output_path: Path) -> dict[str, Any]:
    allowlist = _normalize_allowlist(spec.get("development_allowlist"))
    targets = _normalize_targets(spec.get("targets"))
    target_ids = {target["id"] for target in targets}
    _require(set(allowlist) <= target_ids,
             "development_allowlist names a target absent from targets")
    slots = _build_slots(spec, targets, allowlist)

    identities_raw = spec.get("identities")
    if identities_raw is None:
        identities_raw = {
            "build_artifacts": spec.get("build_artifacts"),
            "profile": spec.get("profile"),
            "development_recipes": spec.get("development_recipes"),
            "legacy_red_reports": spec.get("legacy_red_reports", []),
        }
    _require(isinstance(identities_raw, Mapping), "identities must be an object")
    build_artifacts = _normalize_artifacts(
        identities_raw.get("build_artifacts"), base=spec_path.parent
    )
    profile = _identity(
        identities_raw.get("profile"), base=spec_path.parent, context="profile"
    )
    recipes = _normalize_recipe_identities(
        identities_raw.get("development_recipes"),
        base=spec_path.parent,
        allowlist=allowlist,
    )
    raw_reds = identities_raw.get("legacy_red_reports", [])
    _require(isinstance(raw_reds, list), "legacy_red_reports must be a list")
    red_reports = [
        _identity(value, base=spec_path.parent, context=f"legacy_red_reports[{index}]")
        for index, value in enumerate(raw_reds)
    ]

    timeout_ms = spec.get("timeout_ms", DEFAULT_TIMEOUT_MS)
    _require(type(timeout_ms) is int and timeout_ms > 0,
             "timeout_ms must be a positive integer")
    thresholds = dict(spec.get("thresholds", {}))
    expected_thresholds = {
        "native_target_ms": DEFAULT_NATIVE_TARGET_MS,
        "browser_gap_ms": DEFAULT_BROWSER_GAP_MS,
        "native_hard_ms": DEFAULT_NATIVE_HARD_MS,
    }
    for key, expected in expected_thresholds.items():
        thresholds.setdefault(key, expected)
        value = thresholds[key]
        _require(type(value) in (int, float) and not isinstance(value, bool)
                 and math.isfinite(float(value)) and float(value) > 0,
                 f"thresholds.{key} must be a positive finite number")
        _require(float(value) == expected,
                 f"thresholds.{key} is fixed at {expected}")
        thresholds[key] = expected

    attempts_dir = spec.get("attempts_dir", "hitch-attempts")
    _require(isinstance(attempts_dir, str) and attempts_dir,
             "attempts_dir must be a path string")
    attempts_path = Path(attempts_dir).expanduser()
    if attempts_path.is_absolute():
        attempts_dir_value = str(attempts_path.resolve(strict=False))
    else:
        attempts_dir_value = str(Path(attempts_dir))
    plan = {
        "schema": PLAN_SCHEMA,
        "version": PLAN_VERSION,
        "plan_id": str(spec.get("plan_id") or uuid.uuid4()),
        "created_at": _utc_now(),
        "attempts_dir": attempts_dir_value,
        "timeout_ms": timeout_ms,
        "development_allowlist": allowlist,
        "targets": targets,
        "modes": _normalize_modes(spec.get("modes")),
        "caches": _normalize_caches(spec.get("caches")),
        "repetitions": _normalize_repetitions(spec.get("repetitions")),
        "thresholds": thresholds,
        "identities": {
            "build_artifacts": build_artifacts,
            "profile": profile,
            "development_recipes": recipes,
            "legacy_red_reports": red_reports,
        },
        "slots": slots,
    }
    _validate_plan(plan)
    return plan


def _validate_plan(plan: Any) -> None:
    _require(isinstance(plan, Mapping), "Plan must be an object")
    _require(plan.get("schema") == PLAN_SCHEMA and plan.get("version") == PLAN_VERSION,
             "Unsupported hitch plan schema")
    _require(isinstance(plan.get("plan_id"), str) and plan["plan_id"],
             "Plan id is required")
    allowlist = plan.get("development_allowlist")
    _require(isinstance(allowlist, list) and allowlist,
             "Plan has no explicit development allowlist")
    _require(all(isinstance(value, str) and value for value in allowlist)
             and len(set(allowlist)) == len(allowlist),
             "Plan development allowlist is malformed")
    _require(isinstance(plan.get("slots"), list) and plan["slots"],
             "Plan has no frozen slots")
    modes = plan.get("modes")
    caches = plan.get("caches")
    _require(isinstance(modes, list) and modes and
             all(isinstance(value, str) and value for value in modes) and
             len(set(modes)) == len(modes),
             "Plan modes are malformed")
    _require(isinstance(caches, list) and caches and
             all(isinstance(value, str) and value for value in caches) and
             len(set(caches)) == len(caches),
             "Plan caches are malformed")
    thresholds = plan.get("thresholds")
    _require(isinstance(thresholds, Mapping), "Plan thresholds are missing")
    expected_thresholds = {
        "native_target_ms": DEFAULT_NATIVE_TARGET_MS,
        "browser_gap_ms": DEFAULT_BROWSER_GAP_MS,
        "native_hard_ms": DEFAULT_NATIVE_HARD_MS,
    }
    for key, expected in expected_thresholds.items():
        value = thresholds.get(key)
        _require(type(value) in (int, float) and not isinstance(value, bool)
                 and math.isfinite(float(value)) and float(value) == expected,
                 f"Plan thresholds.{key} is fixed at {expected}")
    timeout_ms = plan.get("timeout_ms")
    _require(type(timeout_ms) is int and timeout_ms > 0,
             "Plan timeout is invalid")
    ids = set()
    target_ids = {item.get("id") for item in plan.get("targets", [])
                  if isinstance(item, Mapping)}
    for index, slot in enumerate(plan["slots"]):
        _require(isinstance(slot, Mapping), f"Plan slot {index} is not an object")
        slot_id = slot.get("slot_id")
        _require(isinstance(slot_id, str) and _SLOT_RE.fullmatch(slot_id) is not None,
                 f"Plan slot {index} has invalid id")
        _require(slot_id not in ids, f"Plan has duplicate slot {slot_id}")
        ids.add(slot_id)
        _require(slot.get("ordinal") == index,
                 f"Plan slot {slot_id} ordinal is not sequential")
        target_id = slot.get("target_id")
        _require(target_id in target_ids and target_id in set(allowlist),
                 f"Plan slot {slot_id} is outside the development allowlist")
        _require(slot.get("mode") in modes,
                 f"Plan slot {slot_id} uses an unfrozen mode")
        _require(slot.get("cache") in caches,
                 f"Plan slot {slot_id} uses an unfrozen cache")
        _require(type(slot.get("timeout_ms")) is int
                 and slot.get("timeout_ms") == timeout_ms,
                 f"Plan slot {slot_id} timeout is not the frozen bound")
        _require(type(slot.get("repetition")) is int and slot["repetition"] >= 1,
                 f"Plan slot {slot_id} has invalid repetition")
        if "frames" in slot:
            _require(type(slot["frames"]) is int and slot["frames"] > 0,
                     f"Plan slot {slot_id} has invalid frame count")
    identities = plan.get("identities")
    _require(isinstance(identities, Mapping), "Plan identities are missing")
    _require(isinstance(identities.get("build_artifacts"), list)
             and identities["build_artifacts"], "Plan build artifact identities are missing")
    _require(isinstance(identities.get("profile"), Mapping), "Plan profile identity is missing")
    _require(isinstance(identities.get("development_recipes"), Mapping),
             "Plan development recipe identities are missing")
    _require(isinstance(identities.get("legacy_red_reports"), list),
             "Plan legacy red identities are missing")


def _verify_identity(identity: Mapping[str, Any], context: str) -> None:
    _require(isinstance(identity, Mapping), f"{context} identity is malformed")
    path = identity.get("path")
    expected = identity.get("sha256")
    _require(isinstance(path, str) and path and isinstance(expected, str),
             f"{context} identity is incomplete")
    _require(_validate_sha(expected, f"{context}.sha256") == sha256_file(path),
             f"{context} changed: {path}")


def _verify_plan_identities(plan: Mapping[str, Any]) -> None:
    identities = plan["identities"]
    for index, identity in enumerate(identities["build_artifacts"]):
        _verify_identity(identity, f"build_artifacts[{index}]")
    _verify_identity(identities["profile"], "profile")
    for target_id, identity in identities["development_recipes"].items():
        _verify_identity(identity, f"development_recipes[{target_id}]")
    for index, identity in enumerate(identities["legacy_red_reports"]):
        _verify_identity(identity, f"legacy_red_reports[{index}]")


def load_plan(plan_path: str | os.PathLike[str], *, verify: bool = True) -> tuple[dict[str, Any], str]:
    plan, digest = _read_json(plan_path)
    _validate_plan(plan)
    if verify:
        _verify_plan_identities(plan)
    return dict(plan), digest


def create_plan(spec: Mapping[str, Any] | str | os.PathLike[str],
                output_path: str | os.PathLike[str]) -> dict[str, Any]:
    """Freeze a plan and create it exclusively at ``output_path``."""

    output = Path(output_path).expanduser().resolve(strict=False)
    if isinstance(spec, Mapping):
        spec_value = spec
        spec_path = output
    else:
        spec_path = Path(spec).expanduser().resolve(strict=False)
        spec_value, _ = _read_json(spec_path)
    _require(isinstance(spec_value, Mapping), "Plan spec must be an object")
    plan = _normalize_plan(spec_value, spec_path=spec_path, output_path=output)
    _write_exclusive(output, _json_bytes(plan))
    return plan


def _attempt_root(plan_path: Path, plan: Mapping[str, Any]) -> Path:
    root = Path(str(plan["attempts_dir"])).expanduser()
    if not root.is_absolute():
        root = plan_path.parent / root
    return root.resolve(strict=False)


def _slot(plan: Mapping[str, Any], slot_id: str) -> dict[str, Any]:
    for value in plan["slots"]:
        if value.get("slot_id") == slot_id:
            return dict(value)
    raise HitchCaptureError(f"Unknown plan slot: {slot_id}")


def _attempt_dir(plan_path: Path, plan: Mapping[str, Any], slot_id: str) -> Path:
    _require(isinstance(slot_id, str) and _SLOT_RE.fullmatch(slot_id) is not None,
             f"Invalid slot id: {slot_id}")
    return _attempt_root(plan_path, plan) / slot_id


def _seal(record: dict[str, Any]) -> dict[str, Any]:
    result = copy.deepcopy(record)
    result["record_sha256"] = _json_hash(result)
    return result


def _verify_seal(record: Mapping[str, Any], context: str) -> None:
    supplied = record.get("record_sha256")
    _require(isinstance(supplied, str), f"{context} seal is missing")
    body = dict(record)
    del body["record_sha256"]
    _require(_validate_sha(supplied, f"{context}.record_sha256") == _json_hash(body),
             f"{context} was modified")


def _read_sealed(path: Path, schema: str, context: str) -> tuple[dict[str, Any], str]:
    value, digest = _read_json(path)
    _require(isinstance(value, Mapping) and value.get("schema") == schema
             and value.get("version") == RECORD_VERSION,
             f"{context} has an unsupported schema")
    _verify_seal(value, context)
    return dict(value), digest


def _identity_bundle_hash(plan: Mapping[str, Any]) -> str:
    return _json_hash(plan["identities"])


def begin_attempt(plan_path: str | os.PathLike[str], slot_id: str,
                  *, now: str | None = None) -> dict[str, Any]:
    """Atomically reserve exactly one frozen slot before browser work starts."""

    source = Path(plan_path).expanduser().resolve(strict=False)
    plan, plan_digest = load_plan(source)
    slot = _slot(plan, slot_id)
    # Slots are consumed in their frozen order.  An open or pending earlier
    # row, or any other open row, is an interrupted run that must be closed
    # explicitly before a later row can start.
    selected_index = int(slot["ordinal"])
    root = _attempt_root(source, plan)
    if root.exists():
        _require(root.is_dir(), f"Attempt root is not a directory: {root}")
        expected_slot_ids = {item["slot_id"] for item in plan["slots"]}
        for child in root.iterdir():
            _require(child.name in expected_slot_ids,
                     f"Unexpected attempt outside the frozen matrix: {child.name}")
    for prior in plan["slots"]:
        if int(prior["ordinal"]) > selected_index:
            continue
        prior_dir = root / prior["slot_id"]
        prior_start = prior_dir / "start.json"
        prior_finish = prior_dir / "finish.json"
        if prior["slot_id"] == slot_id:
            continue
        if prior_dir.exists() and not prior_finish.exists() and not prior_start.exists():
            raise HitchCaptureError(
                f"Earlier slot {prior['slot_id']} has an unstarted reservation; close it first"
            )
        if prior_start.exists() and not prior_finish.exists():
            raise HitchCaptureError(
                f"Earlier slot {prior['slot_id']} is still open; record its interruption first"
            )
        if not prior_start.exists():
            raise HitchCaptureError(
                f"Earlier slot {prior['slot_id']} is pending; slots cannot be skipped"
            )
    for other in plan["slots"]:
        if other["slot_id"] == slot_id:
            continue
        other_dir = root / other["slot_id"]
        if other_dir.exists() and not (other_dir / "finish.json").exists():
            raise HitchCaptureError(
                f"Slot {other['slot_id']} is open; record its interruption before continuing"
            )
    directory = _attempt_dir(source, plan, slot_id)
    root = directory.parent
    root.mkdir(parents=True, exist_ok=True)
    try:
        directory.mkdir()
    except FileExistsError as error:
        raise HitchCaptureError(
            f"Slot {slot_id} already has an attempt; a planned slot cannot be retried"
        ) from error
    except OSError as error:
        raise HitchCaptureError(f"Cannot create attempt directory {directory}: {error}") from error
    record = {
        "schema": START_SCHEMA,
        "version": RECORD_VERSION,
        "plan_id": plan["plan_id"],
        "plan_sha256": plan_digest,
        "slot_id": slot_id,
        "ordinal": slot["ordinal"],
        "target_id": slot["target_id"],
        "mode": slot["mode"],
        "cache": slot["cache"],
        "repetition": slot["repetition"],
        "attempt_id": str(uuid.uuid4()),
        "started_at": now or _utc_now(),
        "timeout_ms": plan["timeout_ms"],
        "identity_bundle_sha256": _identity_bundle_hash(plan),
        "identities": copy.deepcopy(plan["identities"]),
    }
    sealed = _seal(record)
    try:
        _write_exclusive(directory / "start.json", _json_bytes(sealed))
    except Exception:
        # A failed publication must never turn into a reusable slot.  Keep the
        # directory as a visible interrupted reservation and do not overwrite it.
        raise
    return {**sealed, "attempt_dir": str(directory), "attempt_directory": str(directory)}


def _safe_name(value: str, fallback: str) -> str:
    name = Path(value).name
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", name)
    return name or fallback


def _report_summary(report: Any, plan: Mapping[str, Any]) -> dict[str, Any]:
    """Extract independent counts while refusing causal attribution."""

    metrics = report.get("metrics") if isinstance(report, Mapping) else None
    if not isinstance(metrics, Mapping):
        metrics = {}
    events: list[Any] = []
    diagnostic = (report.get("diagnostic_capture") if isinstance(report, Mapping)
                  and isinstance(report.get("diagnostic_capture"), Mapping) else
                  metrics.get("diagnostic_capture") if isinstance(metrics, Mapping) else None)
    if isinstance(diagnostic, Mapping):
        # The collector reports a capped diagnostic_capture.events list.  It
        # is useful only as a cross-check; complete counters below remain the
        # source of truth for aggregate counts.
        value = diagnostic.get("events")
        if isinstance(value, list):
            events.extend(value)
    diagnostic_overflowed = bool(diagnostic.get("overflowed")) if isinstance(diagnostic, Mapping) else False
    thresholds = plan["thresholds"]

    def event_kind(event: Mapping[str, Any]) -> str:
        value = event.get("kind", "")
        return value if isinstance(value, str) else ""

    def number(event: Mapping[str, Any], keys: Iterable[str]) -> float | None:
        value = event.get("duration_ms")
        return (float(value) if type(value) in (int, float)
                and not isinstance(value, bool) and math.isfinite(float(value)) else None)

    def summarize(kind: str, value_keys: tuple[str, ...], threshold: float,
                  counter: str, denominator: str, maximum: str) -> dict[str, Any]:
        candidates = [event for event in events
                      if isinstance(event, Mapping) and event_kind(event) == kind]
        measured_count = metrics.get(counter)
        count = measured_count if type(measured_count) is int and measured_count >= 0 else None
        unknown = 0 if count is not None else 1
        denominator_value = metrics.get(denominator)
        denominator_value = (denominator_value if type(denominator_value) is int
                             and denominator_value >= 0 else None)
        maximum_value = metrics.get(maximum)
        maximum_value = (float(maximum_value) if type(maximum_value) in (int, float)
                         and not isinstance(maximum_value, bool)
                         and math.isfinite(float(maximum_value)) and maximum_value >= 0 else None)
        if candidates:
            event_count = 0
            event_unknown = 0
            for event in candidates:
                value = number(event, value_keys)
                if value is None:
                    event_unknown += 1
                elif value > threshold:
                    event_count += 1
            if diagnostic_overflowed:
                crosscheck = "capped"
            elif count is None:
                crosscheck = UNKNOWN
            elif event_unknown:
                crosscheck = "UNKNOWN"
            else:
                crosscheck = "match" if event_count == count else "mismatch"
        else:
            event_count = 0
            event_unknown = 0
            crosscheck = UNKNOWN if count is None else "no_events"
        return {
            "count": count,
            "unknown": unknown,
            "denominator": denominator_value,
            "maximum_ms": maximum_value,
            "source": counter if count is not None else UNKNOWN,
            "threshold_ms": threshold,
            "event_count": event_count,
            "event_unknown": event_unknown,
            "event_crosscheck": crosscheck,
        }

    native_target = summarize(
        "native_deadline",
        ("duration_ms",),
        float(thresholds["native_target_ms"]),
        "nativeCallbacksOverBudget", "nativeCallbacks", "worstNativeCallbackMs",
    )
    browser_gap = summarize(
        "browser_gap",
        ("duration_ms",),
        float(thresholds["browser_gap_ms"]),
        "browserCallbackGaps", "browserCallbacks", "worstBrowserCallbackMs",
    )
    native_hard = summarize(
        "native_hard_deadline",
        ("duration_ms",),
        float(thresholds["native_hard_ms"]),
        "nativeCallbacksOver33ms", "nativeCallbacks", "worstNativeCallbackMs",
    )
    # A native deadline event may be used only to cross-check the independent
    # hard counter.  Capped event capture never supplies a complete count.
    native_events = [event for event in events
                     if isinstance(event, Mapping) and event_kind(event) == "native_deadline"]
    if native_events and native_hard["count"] is not None:
        hard_events = 0
        hard_unknown = 0
        for event in native_events:
            value = number(event, ("duration_ms",))
            if value is None:
                hard_unknown += 1
            elif value > float(thresholds["native_hard_ms"]):
                hard_events += 1
        native_hard["event_count"] = hard_events
        native_hard["event_unknown"] = hard_unknown
        native_hard["event_crosscheck"] = ("capped" if diagnostic_overflowed else
                                            ("UNKNOWN" if hard_unknown else
                                             ("match" if hard_events == native_hard["count"] else "mismatch")))
    pass_value = report.get("pass") if isinstance(report, Mapping) else None
    result = {
        "native_over_1000_60": native_target,
        "browser_over_1000_30": browser_gap,
        "native_hard_over_33ms": native_hard,
        "causal_classification": "NOT_CLASSIFIED",
    }
    if type(pass_value) is bool:
        result["reported_pass"] = pass_value
    return result


def _attachment_spec(value: Any, index: int) -> tuple[Path, str]:
    if isinstance(value, Mapping):
        raw_path = value.get("path")
        label = value.get("name", value.get("kind", f"attachment-{index:03d}"))
    else:
        raw_path, label = value, f"attachment-{index:03d}"
    _require(isinstance(raw_path, (str, os.PathLike)),
             f"Attachment {index} needs a path")
    return Path(raw_path).expanduser().resolve(strict=False), str(label)


def _validate_browser_report(report: Any, plan: Mapping[str, Any],
                            slot: Mapping[str, Any]) -> dict[str, Any]:
    """Validate report identity and the existing browser gate when possible.

    A bad report is still retained as evidence.  ``valid`` only controls the
    derived summary; it never causes finish to overwrite or discard a red.
    """

    errors: list[str] = []
    if not isinstance(report, Mapping):
        return {"valid": False, "errors": ["report is not a JSON object"],
                "validator": "browser_replay_validation.validate_report"}
    target_id = slot["target_id"]
    expected_recipe = plan["identities"]["development_recipes"][target_id]["sha256"]
    if report.get("recipe_sha256") != expected_recipe:
        errors.append("report recipe_sha256 does not match the frozen development recipe")
    expected_frames = slot.get("frames")
    if expected_frames is None:
        target = next((value for value in plan["targets"] if value.get("id") == target_id), {})
        expected_frames = (target.get("frames", target.get("frame_count"))
                           if isinstance(target, Mapping) else None)
    if type(expected_frames) is not int or expected_frames <= 0:
        errors.append("plan slot has no positive expected frame count")
    elif report.get("frames") != expected_frames:
        errors.append("report frames do not match the frozen target frame count")
    if report.get("mode") != "performance":
        errors.append("report mode is not performance")
    capture = report.get("diagnostic_capture")
    if not isinstance(capture, Mapping):
        errors.append("requested diagnostic_capture is missing")
    else:
        if capture.get("enabled") is not True:
            errors.append("requested diagnostic_capture is not enabled")
        if capture.get("valid") is not True or capture.get("invalid") is not False:
            errors.append("diagnostic_capture is invalid")
        if capture.get("overflowed") is not False:
            errors.append("diagnostic_capture is incomplete or overflowed")
    cache = report.get("cache")
    expected_cold = slot.get("cache") == "cold"
    if not isinstance(cache, Mapping):
        errors.append("report cache profile is missing")
    else:
        if cache.get("cleared_on_startup") is not expected_cold:
            errors.append("report cache does not match the frozen cache row")
        expected_state = "cleared" if expected_cold else "ready"
        if cache.get("state") != expected_state:
            errors.append("report cache state does not match the frozen cache row")
    for key in ("target_id", "target", "workload_id"):
        if key in report and report[key] != target_id:
            errors.append(f"report {key} does not match the frozen target")
    for key in ("hitch_mode", "capture_mode"):
        if key in report and report[key] != slot.get("mode"):
            errors.append(f"report {key} does not match the frozen mode")

    if expected_frames is not None and isinstance(expected_frames, int) and expected_frames > 0:
        try:
            # Import lazily so plan/status remain usable in small tooling
            # environments while the actual browser gate stays authoritative.
            from browser_replay_validation import validate_hitch_capture, validate_report
            metrics = report.get("metrics")
            if report.get("diagnostic_capture") is not None:
                validate_hitch_capture(report["diagnostic_capture"], metrics)
            elif isinstance(metrics, Mapping) and metrics.get("diagnostic_capture") is not None:
                # Accept one legacy nested placement while the runtime report
                # transitions to its frozen root placement.
                validate_hitch_capture(metrics["diagnostic_capture"], metrics)
            validate_report(report, expected_recipe, expected_frames, "performance", expected_cold)
        except Exception as error:  # ValueError plus malformed nested data
            errors.append(str(error))
    return {
        "valid": not errors,
        "errors": errors,
        "validator": "browser_replay_validation.validate_report",
        "target_id": target_id,
        "recipe_sha256": expected_recipe,
        "frames": expected_frames,
        "mode": slot.get("mode"),
        "cache": slot.get("cache"),
    }


def _stored_evidence(directory: Path, metadata: Mapping[str, Any], context: str) -> None:
    relative = metadata.get("path")
    expected = metadata.get("sha256")
    _require(isinstance(relative, str) and isinstance(expected, str),
             f"{context} metadata is incomplete")
    stored = (directory / relative).resolve(strict=False)
    _require(directory.resolve(strict=False) in stored.parents,
             f"{context} escapes attempt directory")
    _require(_validate_sha(expected, f"{context}.sha256") == sha256_file(stored),
             f"{context} changed: {stored}")


def _record_evidence_error(directory: Path, *, source: Path, label: str,
                           error: Exception) -> None:
    """Retain a bounded-copy failure without publishing a false finish."""

    record = _seal({
        "schema": "melee-web-hitch-evidence-error",
        "version": RECORD_VERSION,
        "recorded_at": _utc_now(),
        "source_path": str(source),
        "label": label,
        "error": str(error),
        "staging_files": sorted(
            str(path.relative_to(directory))
            for path in directory.rglob("*.staging-*")
            if path.is_file()
        ),
    })
    _write_exclusive(
        directory / f"evidence-error-{uuid.uuid4().hex}.json",
        _json_bytes(record),
    )


def _read_evidence_errors(directory: Path) -> list[dict[str, Any]]:
    errors: list[dict[str, Any]] = []
    for path in sorted(directory.glob("evidence-error-*.json")):
        value, _ = _read_json(path)
        _require(isinstance(value, Mapping)
                 and value.get("schema") == "melee-web-hitch-evidence-error"
                 and value.get("version") == RECORD_VERSION,
                 f"Evidence error has an unsupported schema: {path}")
        _verify_seal(value, f"Evidence error {path}")
        errors.append(dict(value))
    return errors


def finish_attempt(plan_path: str | os.PathLike[str], slot_id: str, *,
                   status: str = "completed", report_path: str | os.PathLike[str] | None = None,
                   attachments: Iterable[Any] | None = None, reason: str | None = None,
                   now: str | None = None, partial_evidence: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Publish one immutable completion, failure, crash or timeout record."""

    _require(status in _ATTEMPT_STATUSES, f"Unsupported attempt status: {status}")
    source = Path(plan_path).expanduser().resolve(strict=False)
    plan, plan_digest = load_plan(source)
    slot = _slot(plan, slot_id)
    directory = _attempt_dir(source, plan, slot_id)
    start_path = directory / "start.json"
    _require(directory.is_dir(), f"Slot {slot_id} has no reserved attempt directory")
    start: dict[str, Any] | None = None
    if start_path.exists():
        _require(start_path.is_file(), f"Slot {slot_id} start record is not a file")
        start, _ = _read_sealed(start_path, START_SCHEMA, f"slot {slot_id} start")
        _require(start.get("plan_id") == plan["plan_id"] and start.get("slot_id") == slot_id,
                 f"slot {slot_id} start does not belong to this plan")
        _require(start.get("plan_sha256") == plan_digest,
                 f"slot {slot_id} started under a different plan file")
        _require(start.get("identity_bundle_sha256") == _identity_bundle_hash(plan),
                 f"slot {slot_id} identity bundle changed")
    else:
        _require(status == "interrupted",
                 f"Slot {slot_id} has no durable start record; only interrupted may close it")
    finish_path = directory / "finish.json"
    _require(not finish_path.exists(),
             f"Slot {slot_id} is already consumed; a planned slot cannot be retried")
    if status == "completed":
        _require(report_path is not None, "Completed attempt requires a report")

    evidence_dir = directory / "evidence"
    evidence_dir.mkdir(exist_ok=True)
    report_metadata: dict[str, Any] | None = None
    report_object: Any = None
    if report_path is not None:
        report_source = Path(report_path).expanduser().resolve(strict=False)
        _require(report_source.is_file(), f"Report is not a regular file: {report_source}")
        stored_name = "report-" + _safe_name(report_source.name, "report.json")
        stored_path = evidence_dir / stored_name
        try:
            if stored_path.resolve(strict=False) == report_source:
                digest, _ = _bounded_file_hash(report_source, MAX_ATTACHMENT_BYTES)
            else:
                digest = _copy_exclusive(report_source, stored_path)
        except HitchCaptureError as error:
            _record_evidence_error(directory, source=report_source, label="report", error=error)
            raise
        try:
            report_object, _ = _read_json(stored_path)
        except HitchCaptureError as error:
            # Raw diagnostic bytes are retained as evidence; their metric
            # fields are unknown rather than invented.  Keep a sealed error
            # record as well so an oversize, truncated, or malformed report
            # cannot look like an ordinary measured row.
            _record_evidence_error(
                directory, source=report_source, label="report-read", error=error
            )
            report_object = None
        report_metadata = {
            "path": str(stored_path.relative_to(directory)),
            "sha256": digest,
            "source_path": str(report_source),
        }

    attachment_metadata: list[dict[str, Any]] = []
    for index, item in enumerate(attachments or ()):
        attachment_source, label = _attachment_spec(item, index)
        stored_name = f"attachment-{index:03d}-" + _safe_name(label, f"attachment-{index:03d}")
        stored_path = evidence_dir / stored_name
        try:
            if stored_path.resolve(strict=False) == attachment_source:
                digest, _ = _bounded_file_hash(attachment_source, MAX_ATTACHMENT_BYTES)
            else:
                digest = _copy_exclusive(attachment_source, stored_path)
        except HitchCaptureError as error:
            _record_evidence_error(
                directory, source=attachment_source, label=label, error=error
            )
            raise
        attachment_metadata.append({
            "path": str(stored_path.relative_to(directory)),
            "sha256": digest,
            "source_path": str(attachment_source),
            "name": label,
        })

    validation = (_validate_browser_report(report_object, plan, slot)
                  if report_path is not None and start is not None else {
                      "valid": False,
                      "errors": (["no report supplied"] if report_path is None else
                                  ["attempt has no durable start record"]),
                      "validator": "browser_replay_validation.validate_report",
                  })
    summary = _report_summary(report_object, plan) if report_path is not None else {
        "native_over_1000_60": {"count": None, "unknown": 1, "source": UNKNOWN,
                                "threshold_ms": plan["thresholds"]["native_target_ms"]},
        "browser_over_1000_30": {"count": None, "unknown": 1, "source": UNKNOWN,
                                  "threshold_ms": plan["thresholds"]["browser_gap_ms"]},
        "native_hard_over_33ms": {"count": None, "unknown": 1, "source": UNKNOWN,
                                  "threshold_ms": plan["thresholds"]["native_hard_ms"]},
        "causal_classification": "NOT_CLASSIFIED",
    }
    # A raw report's ``pass`` field is retained in the copied bytes but never
    # drives acceptance.  The independent validator result is the ledger's
    # derived result, so malformed or stale reports remain failed evidence.
    summary.pop("reported_pass", None)
    summary["validated"] = bool(validation.get("valid"))
    acceptance_evidence = bool(
        status == "completed"
        and slot.get("mode") == "unprofiled"
        and validation.get("valid") is True
        and not plan["identities"]["legacy_red_reports"]
    )
    finish: dict[str, Any] = {
        "schema": FINISH_SCHEMA,
        "version": RECORD_VERSION,
        "plan_id": plan["plan_id"],
        "plan_sha256": plan_digest,
        "slot_id": slot_id,
        "attempt_id": start.get("attempt_id") if start is not None else None,
        "reservation_only": start is None,
        "status": status,
        "consumed": True,
        "finished_at": now or _utc_now(),
        "reason": reason or ("completed" if status == "completed" else status),
        "acceptance_evidence": acceptance_evidence,
        "report": report_metadata,
        "attachments": attachment_metadata,
        "validation": validation,
        "performance_summary": summary,
    }
    if not validation.get("valid"):
        finish["validation_error"] = list(validation.get("errors", []))
    if partial_evidence is not None:
        _require(isinstance(partial_evidence, Mapping), "partial_evidence must be an object")
        finish["partial_evidence"] = copy.deepcopy(dict(partial_evidence))
    sealed = _seal(finish)
    _write_exclusive(finish_path, _json_bytes(sealed))
    return {**sealed, "attempt_dir": str(directory), "attempt_directory": str(directory)}


def _verify_start_against_plan(start: Mapping[str, Any], plan: Mapping[str, Any],
                               plan_digest: str, slot_id: str) -> None:
    _verify_seal(start, f"slot {slot_id} start")
    _require(start.get("plan_id") == plan["plan_id"] and start.get("slot_id") == slot_id,
             f"slot {slot_id} start does not belong to this plan")
    _require(start.get("plan_sha256") == plan_digest,
             f"slot {slot_id} start references a different plan")
    _require(start.get("identity_bundle_sha256") == _identity_bundle_hash(plan),
             f"slot {slot_id} start identity bundle changed")
    _require(start.get("identities") == plan["identities"],
             f"slot {slot_id} start identities changed")


def _aggregate_summaries(summaries: Iterable[Mapping[str, Any]]) -> dict[str, Any]:
    names = ("native_over_1000_60", "browser_over_1000_30", "native_hard_over_33ms")
    result: dict[str, Any] = {}
    for name in names:
        count = 0
        unknown = 0
        denominator = 0
        denominator_unknown = 0
        maxima: list[float] = []
        maximum_unknown = 0
        threshold: float | None = None
        seen = False
        for summary in summaries:
            metric = summary.get(name) if isinstance(summary, Mapping) else None
            if not isinstance(metric, Mapping):
                unknown += 1
                continue
            seen = True
            threshold = metric.get("threshold_ms", threshold)
            value = metric.get("count")
            if type(value) is int and value >= 0:
                count += value
            else:
                unknown += int(metric.get("unknown", 1)) if type(metric.get("unknown", 1)) is int else 1
            denominator_value = metric.get("denominator")
            if type(denominator_value) is int and denominator_value >= 0:
                denominator += denominator_value
            else:
                denominator_unknown += 1
            maximum_value = metric.get("maximum_ms")
            if type(maximum_value) in (int, float) and not isinstance(maximum_value, bool):
                maxima.append(float(maximum_value))
            else:
                maximum_unknown += 1
        result[name] = {
            # Any missing denominator/counter keeps the public count UNKNOWN;
            # known_count preserves what was actually measured without
            # manufacturing a complete total.
            "count": count if seen and unknown == 0 else None,
            "known_count": count,
            "unknown": unknown,
            "denominator": denominator if denominator_unknown == 0 else None,
            "denominator_known": denominator,
            "denominator_unknown": denominator_unknown,
            "maximum_ms": max(maxima) if maxima else None,
            "maximum_unknown": maximum_unknown,
            "threshold_ms": threshold,
            "state": ("UNKNOWN" if not seen or (unknown and count == 0)
                      else ("PARTIAL" if unknown else "MEASURED")),
        }
    result["causal_classification"] = "NOT_CLASSIFIED"
    return result


def status(plan_path: str | os.PathLike[str]) -> dict[str, Any]:
    """Read and verify every immutable plan, attempt and attachment record."""

    source = Path(plan_path).expanduser().resolve(strict=False)
    plan, plan_digest = load_plan(source)
    attempts: list[dict[str, Any]] = []
    summaries: list[Mapping[str, Any]] = []
    summaries_by_mode: dict[str, list[Mapping[str, Any]]] = {}
    attempt_root = _attempt_root(source, plan)
    expected_slot_ids = {slot["slot_id"] for slot in plan["slots"]}
    if attempt_root.exists():
        _require(attempt_root.is_dir(), f"Attempt root is not a directory: {attempt_root}")
        for child in attempt_root.iterdir():
            if child.name not in expected_slot_ids:
                raise HitchCaptureError(
                    f"Unexpected attempt outside the frozen matrix: {child.name}"
                )
    counts = {"pending": 0, "reserved": 0, "open": 0, "consumed": 0, "completed": 0,
              "aborted": 0, "crashed": 0, "interrupted": 0, "timeout": 0,
              "failed_reports": 0}
    for slot in plan["slots"]:
        slot_id = slot["slot_id"]
        directory = _attempt_dir(source, plan, slot_id)
        start_path = directory / "start.json"
        finish_path = directory / "finish.json"
        entry: dict[str, Any] = {**slot, "attempt_dir": str(directory)}
        if not directory.exists():
            counts["pending"] += 1
            entry["state"] = "pending"
            entry["consumed"] = False
            attempts.append(entry)
            continue
        _require(directory.is_dir(), f"Attempt path is not a directory: {directory}")
        start: dict[str, Any] | None = None
        if start_path.exists():
            _require(start_path.is_file(), f"Slot {slot_id} start record is not a file")
            start, _ = _read_sealed(start_path, START_SCHEMA, f"slot {slot_id} start")
            _verify_start_against_plan(start, plan, plan_digest, slot_id)
            entry["start"] = start
        evidence_errors = _read_evidence_errors(directory)
        if evidence_errors:
            entry["evidence_errors"] = evidence_errors
        if not finish_path.exists():
            if start is None:
                counts["reserved"] += 1
                entry["state"] = "reserved_unstarted"
            else:
                counts["open"] += 1
                entry["state"] = "open"
            entry["consumed"] = False
            attempts.append(entry)
            continue
        finish, _ = _read_sealed(finish_path, FINISH_SCHEMA, f"slot {slot_id} finish")
        _require(finish.get("plan_id") == plan["plan_id"]
                 and finish.get("plan_sha256") == plan_digest
                 and finish.get("slot_id") == slot_id
                 and finish.get("attempt_id") == (start.get("attempt_id") if start else None),
                 f"slot {slot_id} finish does not belong to its start")
        _require(finish.get("reservation_only") is (start is None),
                 f"slot {slot_id} finish reservation state disagrees with start")
        report = finish.get("report")
        if report is not None:
            _stored_evidence(directory, report, f"slot {slot_id} report")
        attachments = finish.get("attachments", [])
        _require(isinstance(attachments, list), f"slot {slot_id} attachments are malformed")
        for index, attachment in enumerate(attachments):
            _stored_evidence(directory, attachment, f"slot {slot_id} attachment {index}")
        state = finish.get("status")
        _require(state in _ATTEMPT_STATUSES, f"slot {slot_id} has invalid finish status")
        counts["consumed"] += 1
        counts[state] += 1
        summary = finish.get("performance_summary")
        _require(isinstance(summary, Mapping), f"slot {slot_id} summary is missing")
        validation = finish.get("validation")
        _require(isinstance(validation, Mapping) and type(validation.get("valid")) is bool,
                 f"slot {slot_id} validation result is missing")
        summaries.append(summary)
        summaries_by_mode.setdefault(str(slot.get("mode")), []).append(summary)
        if not (isinstance(validation, Mapping) and validation.get("valid") is True):
            counts["failed_reports"] += 1
        entry["finish"] = finish
        entry["state"] = state
        entry["consumed"] = True
        attempts.append(entry)
    pending = counts["pending"]
    open_count = counts["open"]
    consumed = counts["consumed"]
    matrix_state = "complete" if consumed == len(plan["slots"]) else "in_progress"
    slot_statuses = []
    for entry in attempts:
        state = entry["state"]
        # Keep the richer state in ``attempts`` while exposing the compact
        # vocabulary expected by sequential runners.
        slot_statuses.append({
            "slot_id": entry["slot_id"],
            "status": "finished" if entry["consumed"] else ("started" if state == "open" else "pending"),
            "state": state,
            "consumed": entry["consumed"],
            "attempt_dir": entry["attempt_dir"],
        })
    by_mode = {
        mode: _aggregate_summaries(values)
        for mode, values in summaries_by_mode.items()
    }
    acceptance_evidence = bool(
        counts["completed"] == len(plan["slots"])
        and not counts["failed_reports"]
        and not plan["identities"]["legacy_red_reports"]
        and all(slot.get("mode") == "unprofiled" for slot in plan["slots"])
    )
    return {
        "schema": "melee-web-hitch-status",
        "version": 1,
        "plan_id": plan["plan_id"],
        "plan_sha256": plan_digest,
        "matrix_state": matrix_state,
        "planned": len(plan["slots"]),
        "pending": pending,
        "reserved": counts["reserved"],
        "open": open_count,
        "consumed": consumed,
        "counts": counts,
        "development_allowlist": list(plan["development_allowlist"]),
        "legacy_red_reports": copy.deepcopy(plan["identities"]["legacy_red_reports"]),
        "legacy_unresolved_reds": len(plan["identities"]["legacy_red_reports"]),
        # The acceptance aggregate excludes profiler rows.  Those rows stay
        # available in the per-mode diagnostic view and are never presented
        # as unprofiled timing evidence.
        "performance_aggregate": _aggregate_summaries(summaries_by_mode.get("unprofiled", [])),
        "performance_by_mode": by_mode,
        "acceptance_evidence": acceptance_evidence,
        "causal_classification": "NOT_CLASSIFIED",
        "attempts": attempts,
        "slots": slot_statuses,
    }


# Friendly aliases for callers that prefer verb-oriented names.
start_attempt = begin_attempt
finish = finish_attempt
read_status = status
