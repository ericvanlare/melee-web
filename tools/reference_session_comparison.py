"""Compare two independent, finalized retail reference sessions.

The comparison is deliberately narrower than a port or gold-corpus gate.  It
validates both immutable raw bundles, binds every result to each bundle's
manifest, and then compares the authoritative semantic rows in transport
order.  The only excluded values are run/host provenance fields whose values
are expected to change when the same saved retail setup is replayed through a
different input transport.  Source observations, including retail addresses,
remain comparison data; an address is never used as an input to a replay.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
from dataclasses import dataclass
import copy
import json
from pathlib import Path
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
OBSERVER_ROOT = ROOT / "reference-capture" / "dolphin"
if str(ROOT / "tools") not in sys.path:
    sys.path.insert(0, str(ROOT / "tools"))
if str(OBSERVER_ROOT) not in sys.path:
    sys.path.insert(0, str(OBSERVER_ROOT))

from reference_capture_replay import (  # noqa: E402
    ReplayError,
    _read_json,
    _read_jsonl,
    _read_observer_records,
    _semantic_row,
    _semantic_validator,
    _validate_authoritative_semantics,
    _without_bundle_identity,
)
from reference_capture_semantics import SemanticSession  # noqa: E402
from reference_session_bundle import (  # noqa: E402
    HEADER_NAME,
    MANIFEST_NAME,
    RAW_OBSERVER_NAME,
    RECORDS_NAME,
    SEMANTIC_VALIDATION_NAME,
    _final_manifest,
    _sha256_file,
    validate_bundle,
)


MAX_OBSERVER_BYTES = 512 * 1024 * 1024
MAX_RECORD_BYTES = 512 * 1024 * 1024
MAX_RECORDS = 1_000_000

# These are the only header values deliberately omitted from the identity
# comparison.  Everything else, including unknown future fields, remains
# bound and therefore fails closed when it changes.
HEADER_PROVENANCE_EXCLUSIONS = (
    ("created_at_unix", "independent run timestamp"),
    ("session_id", "independent session identity"),
    ("run_id", "independent run identity"),
    ("input_source", "host input transport may be physical or replay stream"),
    ("replay_source_manifest_sha256", "explicit relation to an independent original run"),
    ("private_settings_sha256", "host-local settings receipt"),
    ("environment.controller", "host controller/backend identity"),
    ("environment.physical_session_validated", "host-side validation state"),
)

# Observer end records can carry a receipt allocated by the transport rather
# than by the emulated source.  No current session requires this field, but
# naming the exact path keeps this exception auditable if a later observer
# version emits it.  All other end payload fields are compared exactly.
SEMANTIC_PROVENANCE_EXCLUSIONS = ()


class SessionComparisonError(ValueError):
    """A raw session cannot be admitted to an original-vs-original compare."""


@dataclass(frozen=True)
class _LoadedSession:
    path: Path
    header: dict[str, Any]
    manifest: dict[str, Any]
    records: list[dict[str, Any]]
    semantic_rows: list[dict[str, Any]]
    completion: dict[str, Any]
    observer_sha256: str


def _bounded_file(path: Path, limit: int, label: str) -> None:
    try:
        size = path.stat().st_size
    except OSError as error:
        raise SessionComparisonError(f"cannot stat {label}: {error}") from error
    if size > limit:
        raise SessionComparisonError(
            f"{label} exceeds comparison bound ({size} > {limit} bytes)"
        )


def _regular_file(path: Path, label: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise SessionComparisonError(f"{label} is missing or is not a regular file")


def _decode_semantics(
    header: Mapping[str, Any], observer_path: Path
) -> tuple[list[dict[str, Any]], dict[str, Any]]:
    try:
        raw = _read_observer_records(observer_path, header)
        if len(raw) > MAX_RECORDS:
            raise SessionComparisonError(
                f"observer stream exceeds comparison record bound ({len(raw)} > {MAX_RECORDS})"
            )
        session = SemanticSession()
        rows = [_semantic_row(session, record) for record in raw]
        completion = session.completion()
    except SessionComparisonError:
        raise
    except (ReplayError, Exception) as error:
        raise SessionComparisonError(f"cannot decode authoritative observer stream: {error}") from error
    if not isinstance(completion, Mapping) or completion.get("complete") is not True:
        raise SessionComparisonError("authoritative semantic session is incomplete")
    return rows, dict(completion)


def _load_bundle(path_value: str | Path) -> _LoadedSession:
    """Validate and decode one finalized bundle without mutating it."""

    path = Path(path_value).expanduser()
    if path.is_symlink():
        raise SessionComparisonError("bundle root must not be a symlink")
    if not path.exists() or not path.is_dir():
        raise SessionComparisonError(f"bundle directory is missing: {path}")

    try:
        manifest, manifest_errors = _final_manifest(path)
    except Exception as error:
        raise SessionComparisonError(f"cannot inspect bundle manifest: {error}") from error
    if manifest is None or manifest_errors:
        detail = "; ".join(manifest_errors) if manifest_errors else "manifest is missing"
        raise SessionComparisonError(f"bundle manifest is invalid: {detail}")
    if manifest.get("raw_immutable") is not True:
        raise SessionComparisonError("bundle manifest does not mark raw content immutable")

    try:
        header = _read_json(path / HEADER_NAME)
    except Exception as error:
        raise SessionComparisonError(f"cannot read bundle header: {error}") from error
    if not isinstance(header, dict):
        raise SessionComparisonError("bundle header is not an object")
    if manifest.get("session_id") != header.get("session_id"):
        raise SessionComparisonError("manifest/header session identity mismatch")

    observer_path = path / RAW_OBSERVER_NAME
    records_path = path / RECORDS_NAME
    validation_path = path / SEMANTIC_VALIDATION_NAME
    _regular_file(observer_path, "observer stream")
    _regular_file(records_path, "semantic records")
    _regular_file(validation_path, "semantic validation")
    _bounded_file(observer_path, MAX_OBSERVER_BYTES, "observer stream")
    _bounded_file(records_path, MAX_RECORD_BYTES, "semantic records")
    try:
        records = _read_jsonl(records_path)
    except Exception as error:
        raise SessionComparisonError(f"cannot read semantic records: {error}") from error
    if len(records) > MAX_RECORDS:
        raise SessionComparisonError(
            f"semantic records exceed comparison bound ({len(records)} > {MAX_RECORDS})"
        )

    report = validate_bundle(path, require_complete=True, semantic_validator=_semantic_validator)
    if not report.valid:
        raise SessionComparisonError("raw bundle validation failed: " + "; ".join(report.errors))
    try:
        stored_completion = _validate_authoritative_semantics(header, observer_path, records)
    except Exception as error:
        raise SessionComparisonError(f"stored semantic evidence is not authoritative: {error}") from error
    semantic_rows = [_without_bundle_identity(row, header, index)
                     for index, row in enumerate(records)]
    decoded_completion = stored_completion
    manifest_hash = manifest.get("manifest_sha256")
    if not isinstance(manifest_hash, str) or not manifest_hash:
        raise SessionComparisonError("bundle manifest has no binding hash")
    observer_hash = _sha256_file(observer_path)
    return _LoadedSession(
        path=path,
        header=header,
        manifest=dict(manifest),
        records=records,
        semantic_rows=semantic_rows,
        completion=decoded_completion,
        observer_sha256=observer_hash,
    )


def _without_path(value: Any, path: tuple[str, ...]) -> Any:
    """Copy JSON data while removing one exact object path."""

    if not path:
        return None
    if not isinstance(value, Mapping):
        return copy.deepcopy(value)
    result = {str(key): copy.deepcopy(child) for key, child in value.items()}
    cursor: dict[str, Any] = result
    for key in path[:-1]:
        child = cursor.get(key)
        if not isinstance(child, dict):
            return result
        cursor = child
    cursor.pop(path[-1], None)
    return result


def _header_identity(header: Mapping[str, Any]) -> dict[str, Any]:
    identity = copy.deepcopy(dict(header))
    for path, _reason in HEADER_PROVENANCE_EXCLUSIONS:
        identity = _without_path(identity, tuple(path.split(".")))
    return identity


def _semantic_identity(row: Mapping[str, Any]) -> dict[str, Any]:
    identity: Any = copy.deepcopy(dict(row))
    for path, _reason in SEMANTIC_PROVENANCE_EXCLUSIONS:
        identity = _without_path(identity, tuple(path.split(".")))
    return identity


def _display(value: Any, limit: int = 4096) -> Any:
    """Keep reports bounded without weakening the equality decision."""

    try:
        encoded = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True)
    except (TypeError, ValueError):
        encoded = repr(value)
    if len(encoded) <= limit:
        return value
    return {"sha256": __import__("hashlib").sha256(encoded.encode()).hexdigest(),
            "truncated": True, "bytes": len(encoded)}


def _first_difference(expected: Any, actual: Any, path: str = "$") -> dict[str, Any] | None:
    if type(expected) is not type(actual):
        return {"path": path, "expected": _display(expected), "actual": _display(actual)}
    if isinstance(expected, Mapping):
        left_keys = list(expected.keys())
        right_keys = list(actual.keys())
        for key in left_keys:
            if key not in actual:
                return {"path": f"{path}.{key}", "expected": _display(expected[key]), "actual": None}
        for key in right_keys:
            if key not in expected:
                return {"path": f"{path}.{key}", "expected": None, "actual": _display(actual[key])}
        for key in left_keys:
            difference = _first_difference(expected[key], actual[key], f"{path}.{key}")
            if difference is not None:
                return difference
        return None
    if isinstance(expected, Sequence) and not isinstance(expected, (str, bytes, bytearray)):
        if len(expected) != len(actual):
            return {"path": f"{path}.length", "expected": len(expected), "actual": len(actual)}
        for index, (left, right) in enumerate(zip(expected, actual)):
            difference = _first_difference(left, right, f"{path}[{index}]")
            if difference is not None:
                return difference
        return None
    if expected != actual:
        return {"path": path, "expected": _display(expected), "actual": _display(actual)}
    return None


def _domain_views(row: Mapping[str, Any]) -> dict[str, Any]:
    """Project one row into named coverage domains for diagnostics."""

    event = row.get("event")
    payload = row.get("payload")
    if not isinstance(payload, Mapping):
        payload = {}
    views: dict[str, Any] = {
        "transport": {key: row.get(key) for key in ("seq", "source_tick", "draw_ordinal", "event")},
    }
    if event in {"pad_poll", "pad_consume"}:
        views["pad"] = payload
    if event in {"match_enter", "match_initial", "fighter_create", "entry", "non_vs_entry"}:
        views["setup"] = payload
    if event == "source_tick":
        views["source"] = payload.get("retail", payload)
        retail = payload.get("retail")
        if isinstance(retail, Mapping):
            if "consumed_inputs" in retail:
                views["pad"] = retail["consumed_inputs"]
            for domain, keys in {
                "rng": ("rng",),
                "match": ("scene_frame", "match_frame"),
                "fighter": ("fighters",),
            }.items():
                selected = {key: retail[key] for key in keys if key in retail}
                if selected:
                    views[domain] = selected
    if event in {"draw_enter", "draw_return"}:
        views["draw"] = payload
    if event in {"result_enter", "result_return"} or "result" in payload:
        views["result"] = payload.get("result", payload)
    if event in {"scene_reset", "scene_teardown", "teardown"}:
        views["teardown"] = payload
    if "scene_transition" in payload or "scene_routing_hex" in payload or "source_scene_frame" in payload:
        views["scene"] = {
            key: payload[key]
            for key in ("scene_transition", "scene_routing_hex", "source_scene_frame")
            if key in payload
        }
    cpu = payload.get("cpu")
    if isinstance(cpu, Mapping):
        views["cpu"] = cpu
        if "camera" in cpu:
            views["camera"] = cpu["camera"]
        players = cpu.get("players")
        if isinstance(players, list):
            hud = [p.get("hud") for p in players if isinstance(p, Mapping) and "hud" in p]
            fighters = [p.get("subject") for p in players if isinstance(p, Mapping) and "subject" in p]
            if hud:
                views["hud"] = hud
            if fighters:
                views["subject"] = fighters
            magnifiers = [p.get("magnifier") for p in players if isinstance(p, Mapping) and "magnifier" in p]
            if magnifiers:
                views["magnifier"] = magnifiers
    return views


def _compare_domain(name: str, left: Sequence[Mapping[str, Any]], right: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    left_values = {index: views[name] for index, row in enumerate(left) if name in (views := _domain_views(row))}
    right_values = {index: views[name] for index, row in enumerate(right) if name in (views := _domain_views(row))}
    indices = sorted(set(left_values) | set(right_values))
    first: dict[str, Any] | None = None
    for index in indices:
        if index not in left_values or index not in right_values:
            first = {
                "index": index,
                "path": "$",
                "expected": "present" if index in left_values else None,
                "actual": "present" if index in right_values else None,
            }
            break
        difference = _first_difference(left_values[index], right_values[index], "$")
        if difference is not None:
            first = {"index": index, **difference}
            break
    count_left = len(left_values)
    count_right = len(right_values)
    if not indices:
        status = "not_observed"
    elif first is not None:
        status = "diverged"
    elif count_left != count_right:
        status = "coverage_mismatch"
    else:
        status = "matched"
    return {
        "status": status,
        "records_a": count_left,
        "records_b": count_right,
        "first_divergence": first,
    }


def _bundle_summary(bundle: _LoadedSession) -> dict[str, Any]:
    return {
        "path": str(bundle.path),
        "session_id": bundle.header.get("session_id"),
        "run_id": bundle.header.get("run_id"),
        "manifest_sha256": bundle.manifest.get("manifest_sha256"),
        "observer_sha256": bundle.observer_sha256,
        "record_count": len(bundle.semantic_rows),
        "completion": bundle.completion,
    }


def _invalid_report(errors: list[dict[str, Any] | str]) -> dict[str, Any]:
    return {
        "schema": "melee-web-reference-session-comparison",
        "version": 1,
        "status": "invalid",
        "scope": "two distinct finalized original retail sessions",
        "errors": errors,
        "claims": {
            "paired_acceptance": "not_claimed",
            "port_equivalence": "not_claimed",
            "gold_admission": "not_claimed",
        },
    }


def compare_bundles(bundle_a: str | Path, bundle_b: str | Path) -> dict[str, Any]:
    """Validate and compare two independent finalized original sessions."""

    loaded: list[_LoadedSession] = []
    errors: list[dict[str, Any]] = []
    for label, path in (("a", bundle_a), ("b", bundle_b)):
        try:
            loaded.append(_load_bundle(path))
        except SessionComparisonError as error:
            errors.append({"bundle": label, "error": str(error)})
    if errors:
        return _invalid_report(errors)
    left, right = loaded
    if left.manifest.get("manifest_sha256") == right.manifest.get("manifest_sha256"):
        return _invalid_report([{"error": "the two sides have the same raw manifest hash"}])
    if left.header.get("session_id") == right.header.get("session_id"):
        return _invalid_report([{"error": "the two sides have the same session identity"}])
    if left.header.get("run_id") == right.header.get("run_id"):
        return _invalid_report([{"error": "the two sides have the same run identity"}])

    header_left = _header_identity(left.header)
    header_right = _header_identity(right.header)
    header_difference = _first_difference(header_left, header_right)
    header = {
        "status": "diverged" if header_difference else "matched",
        "first_divergence": header_difference,
        "ignored_fields": [
            {"path": path, "reason": reason} for path, reason in HEADER_PROVENANCE_EXCLUSIONS
        ],
    }
    semantic_left = [_semantic_identity(row) for row in left.semantic_rows]
    semantic_right = [_semantic_identity(row) for row in right.semantic_rows]
    row_difference: dict[str, Any] | None = None
    for index, (left_row, right_row) in enumerate(zip(semantic_left, semantic_right)):
        difference = _first_difference(left_row, right_row, "$")
        if difference is not None:
            row_difference = {"index": index, "event_a": left_row.get("event"),
                             "event_b": right_row.get("event"), **difference}
            break
    if row_difference is None and len(semantic_left) != len(semantic_right):
        index = min(len(semantic_left), len(semantic_right))
        row_difference = {
            "index": index,
            "event_a": semantic_left[index].get("event") if index < len(semantic_left) else None,
            "event_b": semantic_right[index].get("event") if index < len(semantic_right) else None,
            "path": "$",
            "expected": "present" if index < len(semantic_left) else None,
            "actual": "present" if index < len(semantic_right) else None,
        }
    domains = {}
    domain_names = ("transport", "setup", "pad", "source", "rng", "match", "fighter",
                    "cpu", "camera", "hud", "subject", "magnifier", "draw", "result", "teardown", "scene")
    for name in domain_names:
        domains[name] = _compare_domain(name, left.semantic_rows, right.semantic_rows)
    missing = [name for name, value in domains.items()
               if value["status"] == "not_observed" or value["status"] == "coverage_mismatch"]
    semantic = {
        "status": "matched" if row_difference is None else "diverged",
        "records_a": len(semantic_left),
        "records_b": len(semantic_right),
        "first_divergence": row_difference,
        "ignored_fields": [
            {"path": path, "reason": reason}
            for path, reason in SEMANTIC_PROVENANCE_EXCLUSIONS
        ],
        "domains": domains,
        "missing_coverage": missing,
        "addresses_compared": True,
        "addresses_used_as_replay_inputs": False,
    }
    status = "matched" if not header_difference and row_difference is None else "diverged"
    return {
        "schema": "melee-web-reference-session-comparison",
        "version": 1,
        "status": status,
        "scope": "two distinct finalized original retail sessions",
        "bundles": {"a": _bundle_summary(left), "b": _bundle_summary(right)},
        "header": header,
        "semantic": semantic,
        "claims": {
            "original_session_pair": "matched" if status == "matched" else "diverged",
            "paired_acceptance": "not_claimed",
            "reference_repeatability": "diagnostic_pair_only",
            "port_equivalence": "not_claimed",
            "gold_admission": "not_claimed",
        },
    }


compare_reference_sessions = compare_bundles


__all__ = [
    "HEADER_PROVENANCE_EXCLUSIONS",
    "SEMANTIC_PROVENANCE_EXCLUSIONS",
    "SessionComparisonError",
    "compare_bundles",
    "compare_reference_sessions",
]
