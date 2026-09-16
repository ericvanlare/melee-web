"""Fail-closed lifecycle for retail reference-session bundles.

The capture process writes an append-only JSONL stream while a run is active.
The stream is deliberately separate from any derived comparison output: a raw
bundle can only become accepted after its identity, ordering, observer faults,
and lifecycle are validated.  A failed validation keeps the ``.partial``
directory intact so that the failure is inspectable and cannot be mistaken for
an accepted retail capture.

The module is intentionally independent of the passive observer's payload
schema. It requires a typed record envelope with an ``event`` discriminator
and a small set of lifecycle semantics; observer-specific fields remain in the
typed ``payload`` object and are not interpreted as evidence.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass
import contextlib
import hashlib
import json
import os
from pathlib import Path
import re
import time
import uuid
from typing import Any, Iterator


SCHEMA = "melee-web-reference-session-v1"
VERSION = 1
MANIFEST_VERSION = 1

ACTIVE_PARTIALS = "activepartials"
ACCEPTED_UNPROCESSED = "acceptedunprocessed"
FAILED = "failed"
INGESTED = "ingested"
DERIVED = "derived"
INBOX_STATES = (
    ACTIVE_PARTIALS,
    ACCEPTED_UNPROCESSED,
    FAILED,
    INGESTED,
    DERIVED,
)

HEADER_NAME = "header.json"
RECORDS_NAME = "records.jsonl"
RAW_OBSERVER_NAME = "observer.bin"
STATUS_NAME = "status.json"
FAILURE_NAME = "failure.json"
SEMANTIC_VALIDATION_NAME = "validation.json"
MANIFEST_NAME = "manifest.json"
DERIVED_MANIFEST_NAME = "derived-manifest.json"

FAULT_NAMES = frozenset(
    {
        "missing_poll",
        "missingpoll",
        "sequence_gap",
        "seq_gap",
        "drift",
        "overflow",
        "writer_error",
        "crash",
        "observer_crash",
        "transport_error",
        "collector_error",
        "capture_error",
        "duplicate",
        "dropped",
        "drop",
        "invalid",
    }
)

_ID_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,255}$")
_KIND_FIELDS = ("event", "phase")


class BundleError(RuntimeError):
    """Base class for bundle lifecycle errors."""


class BundleExistsError(BundleError):
    """The requested run or destination already exists."""


class BundleStateError(BundleError):
    """An operation is invalid for the current bundle state."""


class BundleValidationError(BundleError):
    """A bundle did not satisfy the acceptance contract."""

    def __init__(self, errors: list[str], report: "ValidationReport | None" = None):
        self.errors = tuple(errors)
        self.report = report
        message = "; ".join(errors) if errors else "bundle validation failed"
        super().__init__(message)


class UnsafePathError(BundleError):
    """A requested path would cross a symlink or an unsafe identifier."""


def _canonical_bytes(value: Any) -> bytes:
    return (
        json.dumps(
            value,
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=True,
            allow_nan=False,
        ).encode("utf-8")
    )


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def _write_bytes_atomic(path: Path, data: bytes) -> None:
    """Write one mutable sidecar without exposing a partially written file."""

    _reject_symlink(path)
    temporary = path.with_name(f".{path.name}.{uuid.uuid4().hex}.tmp")
    _reject_symlink(temporary)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(temporary, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            descriptor = -1
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
        _fsync_directory(path.parent)
    except Exception:
        if descriptor >= 0:
            os.close(descriptor)
        with contextlib.suppress(FileNotFoundError):
            temporary.unlink()
        raise


def _write_json_atomic(path: Path, value: Any) -> None:
    _write_bytes_atomic(path, _canonical_bytes(value) + b"\n")


def _fsync_directory(path: Path) -> None:
    with contextlib.suppress(OSError):
        descriptor = os.open(path, os.O_RDONLY)
        try:
            os.fsync(descriptor)
        finally:
            os.close(descriptor)


def _reject_symlink(path: Path) -> None:
    """Reject the final component if it is a symlink."""

    try:
        if path.is_symlink():
            raise UnsafePathError(f"symlink is not allowed: {path.name}")
    except OSError as error:
        raise UnsafePathError(f"cannot inspect path {path}: {error}") from error


def _safe_id(value: Any, label: str, *, allow_partial_suffix: bool = False) -> str:
    if not isinstance(value, str) or not value or not _ID_RE.fullmatch(value):
        raise ValueError(
            f"{label} must be a nonempty identifier (letters, digits, _, -, . only)"
        )
    if value in {".", ".."} or (value.endswith(".partial") and not allow_partial_suffix):
        raise ValueError(f"{label} is not a safe bundle identifier")
    return value


def _safe_artifact_name(value: Any) -> str:
    name = _safe_id(value, "artifact name")
    if name in {HEADER_NAME, RECORDS_NAME, STATUS_NAME, MANIFEST_NAME}:
        raise ValueError("artifact name is reserved")
    return name


def _ensure_public_data(value: Any, *, path: str = "metadata") -> None:
    """Ensure sidecars contain JSON data without imposing payload semantics.

    A collector may retain a diagnostic path or a private field in a local
    failed attempt.  The bundle lifecycle does not guess whether that string
    is sensitive; supervisors decide what enters the tracked evidence set.
    Tracked fixtures remain synthetic and path-free by construction.
    """

    if isinstance(value, Mapping):
        for key, child in value.items():
            if not isinstance(key, str):
                raise ValueError(f"{path} contains a non-string key")
            _ensure_public_data(child, path=f"{path}.{key}")
    elif isinstance(value, list):
        for index, child in enumerate(value):
            _ensure_public_data(child, path=f"{path}[{index}]")
    elif isinstance(value, (str, int, float, bool)) or value is None:
        return
    else:
        raise ValueError(f"{path} contains unsupported value {type(value).__name__}")


def _normalise_kind(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", value.strip().lower()).strip("_")


def _record_kinds(record: Mapping[str, Any]) -> list[str]:
    kinds: list[str] = []
    for field in _KIND_FIELDS:
        value = record.get(field)
        if isinstance(value, str) and value.strip():
            kinds.append(_normalise_kind(value))
    return kinds


def _typed_event(record: Mapping[str, Any]) -> str | None:
    """Return the canonical passive-observer event, if present.

    The observer transport has one semantic discriminator: ``event``.  Other
    fields are retained as payload/context and are never silently promoted to
    an event name, which keeps a malformed or fabricated row from satisfying a
    lifecycle phase by accident.
    """

    value = record.get("event")
    if not isinstance(value, str) or not value.strip():
        return None
    return _normalise_kind(value)


def _is_positive_fault(value: Any) -> bool:
    if value is None or value is False or value == 0 or value == "":
        return False
    if isinstance(value, (list, tuple, dict, set)):
        return bool(value)
    return True


def _faults_from(value: Any, prefix: str = "") -> list[str]:
    found: list[str] = []
    if isinstance(value, Mapping):
        for key, child in value.items():
            normal = _normalise_kind(str(key))
            path = f"{prefix}.{key}" if prefix else str(key)
            if normal in FAULT_NAMES and _is_positive_fault(child):
                found.append(path)
            elif normal in {"fault", "faults", "error", "errors"}:
                if isinstance(child, Mapping):
                    found.extend(_faults_from(child, path))
                elif _is_positive_fault(child):
                    found.append(path)
            elif isinstance(child, (Mapping, list)):
                found.extend(_faults_from(child, path))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            if isinstance(child, str):
                normal = _normalise_kind(child)
                if normal in FAULT_NAMES or any(name in normal for name in FAULT_NAMES):
                    found.append(f"{prefix}[{index}]={child}")
                    continue
            found.extend(_faults_from(child, f"{prefix}[{index}]"))
    return found


def _record_faults(record: Mapping[str, Any]) -> list[str]:
    found = _faults_from(record)
    kinds = set(_record_kinds(record))
    if kinds & {"fault", "error", "crash", "writer_error", "observer_crash"}:
        found.append("record.kind")
    return sorted(set(found))


def _observer_end_faults(record: Mapping[str, Any], classes: set[str]) -> list[str]:
    """Return explicit negative transport/end markers from the terminal row.

    The binary observer owns framing and sequence validation, but the bundle
    must retain a fail-closed guard when a caller supplies the parsed terminal
    envelope.  Missing markers remain compatible with older callers; any
    explicit negative marker is a capture fault and therefore prevents the
    partial from being promoted.
    """

    if "observer_end" not in classes:
        return []
    payload = record.get("payload")
    if not isinstance(payload, Mapping):
        return []
    faults: list[str] = []
    for field in ("clean", "transport_ok", "sequence_ok", "sequence_valid"):
        if payload.get(field) is False:
            faults.append(f"observer_end.{field}=false")
    for field in ("status", "state"):
        value = payload.get(field)
        if isinstance(value, str) and _normalise_kind(value) in {
            "failed",
            "error",
            "invalid",
            "interrupted",
            "crashed",
            "truncated",
        }:
            faults.append(f"observer_end.{field}={value}")
    return faults


def _classify_record(record: Mapping[str, Any]) -> set[str]:
    """Map explicit typed fields to the small lifecycle vocabulary."""

    kinds = set(_record_kinds(record))
    classes: set[str] = set()
    entry = {
        "entry",
        "match_entry",
        "match_enter",
        "entry_complete",
        "match_enter_complete",
        "enter",
        "match_initial",
    }
    gameplay = {
        "gameplay",
        "gameplay_tick",
        "match",
        "match_tick",
        "interactive",
        "tick",
        "frame",
        "source_tick",
        "source_step",
    }
    ending = {
        "ending",
        "result",
        "ending_result",
        "match_ending",
        "match_result",
        "match_end",
        "game_end",
        "finish",
        "finished",
    }
    teardown = {
        "teardown",
        "destroy",
        "unload",
        "scene_teardown",
        "teardown_complete",
        "source_teardown",
        "world_teardown",
    }
    observer_end = {
        "observer_end",
        "observer_finished",
        "capture_end",
        "capture_finished",
        "end_observer",
        "observer_end_clean",
    }
    if kinds & entry:
        classes.add("entry")
    if kinds & gameplay:
        classes.add("gameplay")
    if kinds & ending:
        classes.add("ending_result")
    payload = record.get("payload")
    if kinds & teardown:
        classes.add("teardown")
    if "scene_reset" in kinds:
        if isinstance(payload, Mapping) and payload.get("released_fighter_slots"):
            classes.add("teardown")
    if "result_return" in kinds:
        if isinstance(payload, Mapping) and payload.get("result"):
            classes.add("ending_result")
    if kinds & {"observer_end"}:
        classes.add("observer_end")
    if "observer_end_clean" in kinds:
        if isinstance(payload, Mapping) and payload.get("clean") is True:
            classes.add("observer_end")
    return classes


@dataclass(frozen=True)
class ValidationReport(Mapping[str, Any]):
    """Mapping-compatible validation result for callers and CLI output."""

    valid: bool
    errors: tuple[str, ...]
    session_id: str | None = None
    record_count: int = 0
    phases: tuple[str, ...] = ()
    faults: tuple[str, ...] = ()
    manifest_sha256: str | None = None

    def to_dict(self) -> dict[str, Any]:
        return {
            "valid": self.valid,
            "errors": list(self.errors),
            "session_id": self.session_id,
            "record_count": self.record_count,
            "phases": list(self.phases),
            "faults": list(self.faults),
            "manifest_sha256": self.manifest_sha256,
        }

    def __getitem__(self, key: str) -> Any:
        return self.to_dict()[key]

    def __iter__(self) -> Iterator[str]:
        return iter(self.to_dict())

    def __len__(self) -> int:
        return len(self.to_dict())


def _inbox_root(root: str | os.PathLike[str]) -> Path:
    candidate = Path(root)
    if candidate.exists() and candidate.is_symlink():
        raise UnsafePathError("inbox root cannot be a symlink")
    candidate.mkdir(parents=True, exist_ok=True)
    if not candidate.is_dir():
        raise NotADirectoryError(candidate)
    candidate = candidate.resolve()
    for state in INBOX_STATES:
        child = candidate / state
        _reject_symlink(child)
        child.mkdir(exist_ok=True)
        if not child.is_dir():
            raise NotADirectoryError(child)
    return candidate


def _safe_state_child(root: Path, state: str, identifier: str) -> Path:
    if state not in INBOX_STATES:
        raise ValueError(f"unknown inbox state: {state}")
    safe = _safe_id(
        identifier,
        "session id",
        allow_partial_suffix=(state == ACTIVE_PARTIALS),
    )
    parent = root / state
    _reject_symlink(parent)
    path = parent / safe
    _reject_symlink(path)
    return path


@contextlib.contextmanager
def _inbox_lock(root: Path):
    lock_path = root / ".reference-session.lock"
    _reject_symlink(lock_path)
    descriptor = os.open(lock_path, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        try:
            import fcntl

            fcntl.flock(descriptor, fcntl.LOCK_EX)
        except (ImportError, OSError):
            pass
        yield
    finally:
        with contextlib.suppress(Exception):
            import fcntl

            fcntl.flock(descriptor, fcntl.LOCK_UN)
        os.close(descriptor)


def _atomic_move(source: Path, destination: Path) -> None:
    _reject_symlink(source)
    _reject_symlink(destination)
    if destination.exists() or destination.is_symlink():
        raise BundleExistsError(f"destination already exists: {destination.name}")
    os.rename(source, destination)
    _fsync_directory(destination.parent)


def _read_json(path: Path) -> Any:
    _reject_symlink(path)
    with path.open("rb") as stream:
        return json.loads(stream.read().decode("utf-8"))


def _read_stream(path: Path) -> tuple[list[dict[str, Any]], list[str]]:
    errors: list[str] = []
    records: list[dict[str, Any]] = []
    _reject_symlink(path)
    try:
        with path.open("rb") as stream:
            for line_number, raw in enumerate(stream, 1):
                if not raw.strip():
                    errors.append(f"records.jsonl:{line_number}: blank record")
                    continue
                try:
                    record = json.loads(raw.decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as error:
                    errors.append(f"records.jsonl:{line_number}: invalid JSON ({error})")
                    continue
                if not isinstance(record, dict):
                    errors.append(f"records.jsonl:{line_number}: record is not an object")
                    continue
                records.append(record)
    except OSError as error:
        errors.append(f"cannot read records.jsonl: {error}")
    return records, errors


def _manifest_binding(manifest: Mapping[str, Any]) -> str:
    unsigned = dict(manifest)
    unsigned.pop("manifest_sha256", None)
    return _sha256_bytes(_canonical_bytes(unsigned))


def _semantic_report_errors(report: Any) -> list[str]:
    """Validate the passive observer's domain-completion summary.

    ``reference_capture_semantics.SemanticSession.completion()`` returns a
    mapping.  The adapter remains here instead of importing that parser so
    this lifecycle module can be tested independently while still rejecting a
    stream that has only fabricated lifecycle labels.
    """

    if report is None:
        return ["semantic completion report is missing"]
    if hasattr(report, "to_dict") and callable(report.to_dict):
        report = report.to_dict()
    if not isinstance(report, Mapping):
        return ["semantic completion report is not an object"]
    errors: list[str] = []
    if report.get("complete") is not True:
        errors.append("semantic completion is not complete")
    report_faults = _faults_from(report.get("faults", {}), "semantic.faults")
    if report.get("invalid") is True or report.get("error"):
        report_faults.append("semantic.error")
    if report_faults:
        errors.extend(f"semantic fault: {fault}" for fault in sorted(set(report_faults)))
    # ``missing_coverage`` is an explicit scope ledger (the reference
    # collector intentionally does not claim audio/menu/controller gates).  It
    # is retained in validation.json; only the semantic session's ``complete``
    # bit and the required source counters decide whether this raw run closed.
    for field in ("source_ticks", "source_draws", "pad_polls"):
        value = report.get(field)
        if isinstance(value, bool) or not isinstance(value, int) or value <= 0:
            errors.append(f"semantic completion has no positive {field}")
    if not report.get("result"):
        errors.append("semantic completion has no match result")
    return errors


def _final_manifest(path: Path) -> tuple[dict[str, Any] | None, list[str]]:
    errors: list[str] = []
    manifest_path = path / MANIFEST_NAME
    if not manifest_path.exists() or manifest_path.is_symlink():
        return None, ["missing manifest.json"]
    try:
        manifest = _read_json(manifest_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return None, [f"invalid manifest.json: {error}"]
    if not isinstance(manifest, dict):
        return None, ["manifest.json is not an object"]
    if manifest.get("manifest_sha256") != _manifest_binding(manifest):
        errors.append("manifest binding hash mismatch")
    files = manifest.get("files")
    if not isinstance(files, list):
        errors.append("manifest files list is missing")
        return manifest, errors
    declared_names: set[str] = set()
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
            errors.append("manifest contains malformed file entry")
            continue
        name = entry["name"]
        if "/" in name or "\\" in name or name in {"", ".", ".."}:
            errors.append(f"manifest contains unsafe file name: {name!r}")
            continue
        if name == MANIFEST_NAME or name in declared_names:
            errors.append(f"manifest contains duplicate or recursive file name: {name}")
            continue
        declared_names.add(name)
        file_path = path / name
        if file_path.is_symlink() or not file_path.is_file():
            errors.append(f"manifest file missing or symlink: {name}")
            continue
        observed_size = file_path.stat().st_size
        observed_hash = _sha256_file(file_path)
        if entry.get("bytes") != observed_size:
            errors.append(f"manifest byte count mismatch: {name}")
        if entry.get("sha256") != observed_hash:
            errors.append(f"manifest hash mismatch: {name}")
    observed_names: set[str] = set()
    for child in path.iterdir():
        if child.name == MANIFEST_NAME:
            continue
        if child.is_symlink() or not child.is_file():
            errors.append(f"bundle contains symlink or non-regular file: {child.name}")
            continue
        observed_names.add(child.name)
    if observed_names != declared_names:
        missing = sorted(observed_names - declared_names)
        extra = sorted(declared_names - observed_names)
        if missing:
            errors.append(f"manifest omits files: {', '.join(missing)}")
        if extra:
            errors.append(f"manifest lists missing files: {', '.join(extra)}")
    return manifest, errors


def validate_bundle(
    path: str | os.PathLike[str],
    *,
    expected_session_id: str | None = None,
    expected_game_id: str | None = None,
    expected_run_id: str | None = None,
    require_complete: bool = True,
    semantic_validator=None,
    semantic_check=None,
    semantic_report=None,
) -> ValidationReport:
    """Validate a partial or finalized bundle without mutating it.

    Validation never sorts records.  A missing or repeated sequence number is
    an error even when all records could otherwise be grouped into phases.
    """

    bundle_path = Path(path)
    errors: list[str] = []
    if not bundle_path.exists() or not bundle_path.is_dir() or bundle_path.is_symlink():
        return ValidationReport(False, ("bundle directory is missing or unsafe",))
    header_path = bundle_path / HEADER_NAME
    records_path = bundle_path / RECORDS_NAME
    try:
        header = _read_json(header_path)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        return ValidationReport(False, (f"invalid header.json: {error}",))
    if not isinstance(header, dict):
        return ValidationReport(False, ("header.json is not an object",))
    if header.get("schema") != SCHEMA or header.get("version") != VERSION:
        errors.append("unsupported bundle schema")
    try:
        session_id = _safe_id(header.get("session_id"), "session_id")
    except ValueError as error:
        session_id = None
        errors.append(str(error))
    for field, expected in (
        ("session_id", expected_session_id),
        ("game_id", expected_game_id),
        ("run_id", expected_run_id),
    ):
        if expected is not None and header.get(field) != expected:
            errors.append(f"{field} does not match requested session")
    if not isinstance(header.get("game_id"), str) or not header.get("game_id"):
        errors.append("header game_id is missing")
    if not isinstance(header.get("run_id"), str) or not header.get("run_id"):
        errors.append("header run_id is missing")
    try:
        sequence_start = header["sequence_start"]
        if isinstance(sequence_start, bool) or not isinstance(sequence_start, int):
            raise ValueError
    except (KeyError, ValueError):
        sequence_start = 1
        errors.append("header sequence_start is not an integer")

    for field in ("metadata", "observer", "provenance"):
        if field in header:
            try:
                _ensure_public_data(header[field], path=f"header.{field}")
            except ValueError as error:
                errors.append(str(error))
    if header.get("synthetic") is True or header.get("capture_origin") == "synthetic":
        errors.append("synthetic capture cannot be accepted as retail evidence")
    if isinstance(header.get("provenance"), Mapping) and header["provenance"].get("synthetic") is True:
        errors.append("synthetic provenance cannot be accepted as retail evidence")

    try:
        records, read_errors = _read_stream(records_path)
    except Exception as error:  # defensive: a writer failure must fail closed
        records, read_errors = [], [f"record stream read failure: {error}"]
    errors.extend(read_errors)
    phases: list[str] = []
    expected_seq = sequence_start
    faults = list(_faults_from(header.get("faults", {}), "header.faults"))
    faults.extend(_faults_from(header.get("status", {}), "header.status"))
    faults.extend(_faults_from(header.get("observer", {}), "header.observer"))
    faults.extend(_faults_from(header.get("metadata", {}), "header.metadata"))
    status_path = bundle_path / STATUS_NAME
    if status_path.exists() and not status_path.is_symlink():
        try:
            status_value = _read_json(status_path)
            if isinstance(status_value, Mapping):
                faults.extend(_faults_from(status_value.get("faults", {}), "status.faults"))
                if status_value.get("state") == "failed":
                    faults.append("status.failed")
                if status_value.get("record_count") != len(records):
                    errors.append("status record count mismatch")
        except (OSError, ValueError, json.JSONDecodeError) as error:
            errors.append(f"invalid status.json: {error}")
    positions: dict[str, int] = {}
    for position, record in enumerate(records):
        kinds = _record_kinds(record)
        if _typed_event(record) is None:
            errors.append(f"record {position} has no typed event")
        payload = record.get("payload")
        if payload is not None and not isinstance(payload, dict):
            errors.append(f"record {position} payload is not an object")
        try:
            seq = record["seq"]
            if isinstance(seq, bool) or not isinstance(seq, int):
                raise ValueError
        except (KeyError, ValueError):
            errors.append(f"record {position} has no integer seq")
            seq = expected_seq
        if seq != expected_seq:
            errors.append(f"record {position} sequence expected {expected_seq}, got {seq}")
            expected_seq = seq + 1 if isinstance(seq, int) else expected_seq + 1
        else:
            expected_seq += 1
        for field in ("session_id", "game_id", "run_id"):
            if field in record and record[field] != header.get(field):
                errors.append(f"record {position} {field} does not match header")
        record_faults = _record_faults(record)
        faults.extend(f"record[{position}].{item}" for item in record_faults)
        classes = _classify_record(record)
        faults.extend(
            f"record[{position}].{item}"
            for item in _observer_end_faults(record, classes)
        )
        for phase in classes:
            phases.append(phase)
            positions.setdefault(phase, position)
            if require_complete and (payload is None or not payload):
                errors.append(f"record {position} lifecycle event {phase} has no evidence payload")
    if status_path.exists() and not status_path.is_symlink():
        with contextlib.suppress(OSError, ValueError, json.JSONDecodeError):
            status_value = _read_json(status_path)
            if isinstance(status_value, Mapping) and status_value.get("state") == "complete" and status_value.get("next_seq") != expected_seq:
                errors.append("status next sequence mismatch")
    faults = sorted(set(faults))
    required = ("entry", "gameplay", "ending_result", "teardown", "observer_end")
    if require_complete:
        for phase in required:
            if phase not in positions:
                errors.append(f"missing lifecycle phase: {phase}")
        if all(phase in positions for phase in required):
            for before, after in zip(required, required[1:]):
                if positions[before] >= positions[after]:
                    errors.append(f"lifecycle phase order is invalid: {before} before {after}")
            if positions["observer_end"] != len(records) - 1:
                errors.append("observer_end must be the final semantic record")
    if faults:
        errors.extend(f"fault: {fault}" for fault in faults)
        errors.append("observer/capture faults present")
    manifest_sha256: str | None = None
    if (bundle_path / MANIFEST_NAME).exists():
        manifest, manifest_errors = _final_manifest(bundle_path)
        errors.extend(manifest_errors)
        if manifest is not None:
            manifest_sha256 = manifest.get("manifest_sha256")
            if manifest.get("session_id") != session_id:
                errors.append("manifest session identity mismatch")
            if manifest.get("game_id") != header.get("game_id"):
                errors.append("manifest game identity mismatch")
            if manifest.get("run_id") != header.get("run_id"):
                errors.append("manifest run identity mismatch")
            if manifest.get("record_count") != len(records):
                errors.append("manifest record count mismatch")
            if manifest.get("sequence_start") != sequence_start:
                errors.append("manifest sequence start mismatch")
            if manifest.get("sequence_end") != expected_seq - 1:
                errors.append("manifest sequence end mismatch")
    elif require_complete:
        errors.append("finalized bundle is missing manifest.json")
    if require_complete and bundle_path.name.endswith(".partial") is True:
        errors.append("partial bundle has not been finalized")
    if require_complete:
        validation_path = bundle_path / SEMANTIC_VALIDATION_NAME
        if not validation_path.exists() or validation_path.is_symlink():
            errors.append("finalized bundle is missing validation.json")
        else:
            try:
                validation = _read_json(validation_path)
                if not isinstance(validation, Mapping) or validation.get("valid") is not True:
                    errors.append("semantic validation sidecar is not valid")
            except (OSError, ValueError, json.JSONDecodeError) as error:
                errors.append(f"invalid validation.json: {error}")
    validator = semantic_validator if semantic_validator is not None else semantic_check
    if validator is not None:
        try:
            result = validator(header, records)
            if result is False:
                errors.append("semantic evidence validator rejected the record stream")
            elif isinstance(result, str):
                errors.append(result)
            elif isinstance(result, Mapping):
                errors.extend(_semantic_report_errors(result))
            elif result is not None and result is not True:
                errors.extend(str(error) for error in result)
        except Exception as error:
            errors.append(f"semantic evidence validator failed: {error}")
    if semantic_report is not None and require_complete:
        errors.extend(_semantic_report_errors(semantic_report))
    return ValidationReport(
        not errors,
        tuple(sorted(set(errors))),
        session_id,
        len(records),
        tuple(phases),
        tuple(faults),
        manifest_sha256,
    )


class ReferenceSessionBundle:
    """Append-only writer for one reference capture run."""

    def __init__(
        self,
        inbox: "ReferenceCaptureInbox",
        partial_path: Path,
        header: dict[str, Any],
    ) -> None:
        self.inbox = inbox
        self.partial_path = partial_path
        self.path = partial_path
        self.header = dict(header)
        self._next_seq = int(header["sequence_start"])
        self._record_count = 0
        self._faults: list[str] = []
        self._closed = False
        self._final_path: Path | None = None
        self._stream = None
        self._raw_stream = None

    @classmethod
    def begin(
        cls,
        root: str | os.PathLike[str],
        session_id: str,
        game_id: str,
        run_id: str,
        *,
        metadata: Mapping[str, Any] | None = None,
        observer: Mapping[str, Any] | None = None,
        sequence_start: int = 1,
        raw_observer: bytes | bytearray | memoryview | None = None,
    ) -> "ReferenceSessionBundle":
        inbox = ReferenceCaptureInbox(root)
        session_id = _safe_id(session_id, "session_id")
        game_id = _safe_id(game_id, "game_id")
        run_id = _safe_id(run_id, "run_id")
        if isinstance(sequence_start, bool) or not isinstance(sequence_start, int) or sequence_start < 0:
            raise ValueError("sequence_start must be a nonnegative integer")
        if metadata is not None and not isinstance(metadata, Mapping):
            raise TypeError("metadata must be an object")
        if observer is not None and not isinstance(observer, Mapping):
            raise TypeError("observer must be an object")
        combined = dict(metadata or {})
        if observer is not None:
            combined["observer"] = dict(observer)
        _ensure_public_data(combined)
        header = {
            "schema": SCHEMA,
            "version": VERSION,
            "session_id": session_id,
            "game_id": game_id,
            "run_id": run_id,
            "sequence_start": sequence_start,
            "created_at_unix": time.time(),
        }
        header.update(combined)
        partial = _safe_state_child(inbox.root, ACTIVE_PARTIALS, session_id + ".partial")
        with _inbox_lock(inbox.root):
            if partial.exists() or partial.is_symlink():
                raise BundleExistsError(f"active partial already exists: {partial.name}")
            for state in (ACCEPTED_UNPROCESSED, FAILED, INGESTED):
                destination = _safe_state_child(inbox.root, state, session_id)
                if destination.exists() or destination.is_symlink():
                    raise BundleExistsError(f"session already exists: {destination}")
            partial.mkdir()
            _write_json_atomic(partial / HEADER_NAME, header)
            _write_bytes_atomic(partial / RECORDS_NAME, b"")
            status = {
                "schema": SCHEMA,
                "version": VERSION,
                "state": "active",
                "session_id": session_id,
                "record_count": 0,
                "next_seq": sequence_start,
                "faults": [],
                "updated_at_unix": time.time(),
            }
            _write_json_atomic(partial / STATUS_NAME, status)
            _fsync_directory(partial)
        bundle = cls(inbox, partial, header)
        bundle._stream = bundle._open_stream()
        if raw_observer is not None:
            bundle.append_raw(raw_observer)
        return bundle

    # Public alias used by supervisors that prefer an explicit constructor name.
    start = begin

    def _open_stream(self):
        _reject_symlink(self.partial_path / RECORDS_NAME)
        return (self.partial_path / RECORDS_NAME).open("ab", buffering=0)

    def _open_raw_stream(self):
        path = self.partial_path / RAW_OBSERVER_NAME
        _reject_symlink(path)
        return path.open("ab", buffering=0)

    @property
    def raw_path(self) -> Path:
        return self.partial_path / RAW_OBSERVER_NAME

    @property
    def raw_observer_path(self) -> Path:
        return self.raw_path

    @property
    def session_id(self) -> str:
        return self.header["session_id"]

    @property
    def game_id(self) -> str:
        return self.header["game_id"]

    @property
    def run_id(self) -> str:
        return self.header["run_id"]

    @property
    def final_path(self) -> Path | None:
        return self._final_path

    def _write_status(self, state: str, **extra: Any) -> None:
        status = {
            "schema": SCHEMA,
            "version": VERSION,
            "state": state,
            "session_id": self.session_id,
            "record_count": self._record_count,
            "next_seq": self._next_seq,
            "faults": sorted(set(self._faults)),
            "updated_at_unix": time.time(),
        }
        status.update(extra)
        _write_json_atomic(self.partial_path / STATUS_NAME, status)

    def _fail(self, errors: list[str], *, error_type: str = "validation") -> None:
        unique = sorted(set(str(error) for error in errors if error)) or ["unknown failure"]
        self._faults.extend(unique)
        with contextlib.suppress(Exception):
            self._write_status("failed", failure=unique)
            _write_json_atomic(
                self.partial_path / FAILURE_NAME,
                {
                    "schema": SCHEMA,
                    "version": VERSION,
                    "session_id": self.session_id,
                    "error_type": error_type,
                    "errors": unique,
                    "record_count": self._record_count,
                    "failed_at_unix": time.time(),
                },
            )

    def append(self, record: Mapping[str, Any] | None = None, **fields: Any) -> int:
        if self._closed:
            raise BundleStateError("cannot append to a closed bundle")
        if record is None:
            record = fields
        elif fields:
            raise TypeError("append accepts a record object or keyword fields, not both")
        if not isinstance(record, Mapping):
            raise TypeError("record must be an object")
        value = dict(record)
        try:
            seq = value["seq"]
            if isinstance(seq, bool) or not isinstance(seq, int):
                raise ValueError
        except (KeyError, ValueError) as error:
            self._fail(["record requires an integer seq"], error_type="writer")
            raise BundleValidationError(["record requires an integer seq"]) from error
        if seq != self._next_seq:
            error = f"record sequence expected {self._next_seq}, got {seq}"
            self._fail([error, "sequence_gap"], error_type="sequence")
            raise BundleValidationError([error])
        if _typed_event(value) is None:
            error = "record requires a typed event"
            self._fail([error], error_type="writer")
            raise BundleValidationError([error])
        for field in ("session_id", "game_id", "run_id"):
            if field in value and value[field] != self.header[field]:
                error = f"record {field} does not match session header"
                self._fail([error], error_type="identity")
                raise BundleValidationError([error])
        try:
            _ensure_public_data(value, path="record")
            encoded = _canonical_bytes(value) + b"\n"
            if self._stream is None:
                self._stream = self._open_stream()
            self._stream.write(encoded)
            self._stream.flush()
            os.fsync(self._stream.fileno())
        except Exception as error:
            self._fail([f"writer error: {error}", "writer_error"], error_type="writer")
            raise BundleError(f"cannot append record: {error}") from error
        self._record_count += 1
        self._next_seq += 1
        self._faults.extend(_record_faults(value))
        try:
            self._write_status("active")
        except Exception as error:
            self._fail([f"writer status error: {error}", "writer_error"], error_type="writer")
            raise BundleError(f"cannot update bundle status: {error}") from error
        return seq

    append_record = append
    write_record = append

    def append_raw(self, data: bytes | bytearray | memoryview) -> int:
        """Retain original fixed-binary observer bytes before parsing."""

        if self._closed:
            raise BundleStateError("cannot append raw bytes to a closed bundle")
        if not isinstance(data, (bytes, bytearray, memoryview)):
            raise TypeError("raw observer data must be bytes-like")
        raw = bytes(data)
        try:
            if self._raw_stream is None:
                self._raw_stream = self._open_raw_stream()
            self._raw_stream.write(raw)
            self._raw_stream.flush()
            os.fsync(self._raw_stream.fileno())
        except Exception as error:
            self._fail([f"writer error: {error}", "writer_error"], error_type="writer")
            raise BundleError(f"cannot append raw observer bytes: {error}") from error
        return len(raw)

    append_raw_bytes = append_raw

    def status(self) -> dict[str, Any]:
        if self.partial_path.exists():
            return _read_json(self.partial_path / STATUS_NAME)
        if self._final_path is not None:
            return _read_json(self._final_path / STATUS_NAME)
        return {"state": "missing"}

    def validate(self, *, require_complete: bool | None = None, **kwargs) -> ValidationReport:
        if require_complete is None:
            require_complete = self._final_path is not None
        return validate_bundle(self.path, require_complete=require_complete, **kwargs)

    def complete(
        self,
        *,
        expected_session_id: str | None = None,
        expected_game_id: str | None = None,
        expected_run_id: str | None = None,
        observer_end: Mapping[str, Any] | None = None,
        semantic_session=None,
        semantic_report=None,
        semantic_validator=None,
        semantic_check=None,
    ) -> Path:
        if self._closed:
            raise BundleStateError("bundle is already closed")
        if observer_end is not None:
            self.append(observer_end)
        if semantic_report is None and semantic_session is not None:
            completion = getattr(semantic_session, "completion", None)
            if not callable(completion):
                raise TypeError("semantic_session must expose completion()")
            semantic_report = completion()
        if semantic_report is not None and hasattr(semantic_report, "to_dict"):
            semantic_report = semantic_report.to_dict()
        if self._stream is not None:
            self._stream.flush()
            os.fsync(self._stream.fileno())
            self._stream.close()
            self._stream = None
        if self._raw_stream is not None:
            self._raw_stream.flush()
            os.fsync(self._raw_stream.fileno())
            self._raw_stream.close()
            self._raw_stream = None
        self._closed = True
        if semantic_report is None and semantic_validator is None and semantic_check is None:
            self._fail(["semantic evidence validation is required"], error_type="semantic")
            raise BundleValidationError(["semantic evidence validation is required"])
        report = validate_bundle(
            self.partial_path,
            expected_session_id=expected_session_id or self.session_id,
            expected_game_id=expected_game_id or self.game_id,
            expected_run_id=expected_run_id or self.run_id,
            require_complete=True,
            semantic_validator=semantic_validator,
            semantic_check=semantic_check,
            semantic_report=semantic_report,
        )
        # A partial path is expected during pre-finalization; remove only the
        # path-level error while retaining every content/lifecycle error.
        content_errors = [
            error for error in report.errors
            if error not in {
                "finalized bundle is missing manifest.json",
                "finalized bundle is missing validation.json",
            }
            and "partial bundle has not been finalized" not in error
        ]
        if content_errors:
            self._fail(content_errors)
            raise BundleValidationError(content_errors, report)
        self._write_status("complete", completed_at_unix=time.time())
        validation = {
            "schema": "melee-web-reference-validation-v1",
            "version": 1,
            "session_id": self.session_id,
            "valid": True,
            "semantic": semantic_report,
            "record_count": self._record_count,
            "validated_at_unix": time.time(),
        }
        _ensure_public_data(validation, path="validation")
        _write_json_atomic(self.partial_path / SEMANTIC_VALIDATION_NAME, validation)
        files = []
        manifest_names = []
        for child in sorted(self.partial_path.iterdir(), key=lambda item: item.name):
            if child.name == MANIFEST_NAME:
                continue
            if child.is_symlink() or not child.is_file():
                self._fail([f"bundle payload is not a regular file: {child.name}"], error_type="writer")
                raise BundleValidationError([f"bundle payload is not a regular file: {child.name}"])
            if "/" in child.name or "\\" in child.name or child.name in {"", ".", ".."}:
                self._fail([f"bundle payload has unsafe name: {child.name}"], error_type="writer")
                raise BundleValidationError([f"bundle payload has unsafe name: {child.name}"])
            manifest_names.append(child.name)
        required_files = {HEADER_NAME, RECORDS_NAME, STATUS_NAME, SEMANTIC_VALIDATION_NAME}
        if not required_files.issubset(manifest_names):
            missing = sorted(required_files - set(manifest_names))
            self._fail([f"bundle payload missing files: {', '.join(missing)}"], error_type="writer")
            raise BundleValidationError([f"bundle payload missing files: {', '.join(missing)}"])
        for name in manifest_names:
            file_path = self.partial_path / name
            files.append({"name": name, "bytes": file_path.stat().st_size, "sha256": _sha256_file(file_path)})
        manifest = {
            "schema": SCHEMA,
            "version": VERSION,
            "manifest_version": MANIFEST_VERSION,
            "session_id": self.session_id,
            "game_id": self.game_id,
            "run_id": self.run_id,
            "record_count": self._record_count,
            "sequence_start": self.header["sequence_start"],
            "sequence_end": self._next_seq - 1,
            "raw_immutable": True,
            "files": files,
            "created_at_unix": time.time(),
        }
        manifest["manifest_sha256"] = _manifest_binding(manifest)
        _write_json_atomic(self.partial_path / MANIFEST_NAME, manifest)
        _fsync_directory(self.partial_path)
        destination = _safe_state_child(self.inbox.root, ACCEPTED_UNPROCESSED, self.session_id)
        with _inbox_lock(self.inbox.root):
            if destination.exists() or destination.is_symlink():
                self._fail([f"destination already exists: {destination.name}"], error_type="destination")
                raise BundleExistsError(f"destination already exists: {destination.name}")
            try:
                _atomic_move(self.partial_path, destination)
            except Exception as error:
                self._fail([f"atomic finalization failed: {error}"], error_type="finalization")
                raise
        self._final_path = destination
        self.path = destination
        return destination

    finalize = complete
    finish = complete

    def fail(self, reason: str, *, error_type: str = "operator") -> None:
        """Record a failed attempt while intentionally preserving its partial."""

        if self._closed:
            raise BundleStateError("bundle is already closed")
        if self._stream is not None:
            self._stream.flush()
            self._stream.close()
            self._stream = None
        if self._raw_stream is not None:
            self._raw_stream.flush()
            self._raw_stream.close()
            self._raw_stream = None
        self._closed = True
        self._fail([reason], error_type=error_type)

    def close(self) -> None:
        if not self._closed:
            self.fail("writer closed before observer_end", error_type="crash")

    def __enter__(self) -> "ReferenceSessionBundle":
        return self

    def __exit__(self, exc_type, exc, traceback) -> bool:
        if exc_type is not None and not self._closed:
            self.fail(f"writer exited with {exc_type.__name__}", error_type="crash")
        elif not self._closed:
            self.close()
        return False


class ReferenceCaptureInbox:
    """Owns the five-state on-disk inbox and derived artifact boundary."""

    def __init__(self, root: str | os.PathLike[str]):
        self.root = _inbox_root(root)

    @property
    def active_partials(self) -> Path:
        return self.root / ACTIVE_PARTIALS

    @property
    def accepted_unprocessed(self) -> Path:
        return self.root / ACCEPTED_UNPROCESSED

    @property
    def failed(self) -> Path:
        return self.root / FAILED

    @property
    def ingested(self) -> Path:
        return self.root / INGESTED

    @property
    def derived(self) -> Path:
        return self.root / DERIVED

    def begin(self, *args, **kwargs) -> ReferenceSessionBundle:
        return ReferenceSessionBundle.begin(self.root, *args, **kwargs)

    start = begin

    def state_path(self, state: str, identifier: str) -> Path:
        return _safe_state_child(self.root, state, identifier)

    def list_runs(self, state: str | None = None) -> list[dict[str, Any]]:
        states = [state] if state else list(INBOX_STATES)
        rows: list[dict[str, Any]] = []
        derived: list[dict[str, Any]] = []
        # Replay revisions can use a separate parent under derived/. Discover
        # their manifests without following links or scanning payload contents.
        pending = [(self.root / DERIVED, 0)]
        visited = 0
        while pending:
            directory, depth = pending.pop()
            _reject_symlink(directory)
            for path in sorted(directory.iterdir()):
                visited += 1
                if visited > 10000:
                    raise BundleError("derived discovery exceeds its directory bound")
                if path.name.startswith(".") or path.is_symlink() or not path.is_dir():
                    continue
                manifest_path = path / DERIVED_MANIFEST_NAME
                if manifest_path.is_file() and not manifest_path.is_symlink():
                    row = {"state": DERIVED, "session_id": path.name,
                           "path": path.relative_to(self.root).as_posix(),
                           "artifacts": sorted(child.name for child in path.iterdir()
                                               if child.is_file() and not child.is_symlink())}
                    try:
                        manifest = _read_json(manifest_path)
                        row.update(session_id=manifest["session_id"],
                                   source_manifest_sha256=manifest["source"]["manifest_sha256"],
                                   manifest_sha256=manifest["manifest_sha256"])
                    except (KeyError, TypeError, ValueError, OSError):
                        row["status"] = "invalid_manifest"
                    derived.append(row)
                elif depth < 3:
                    pending.append((path, depth + 1))
        for current in states:
            if current not in INBOX_STATES:
                raise ValueError(f"unknown inbox state: {current}")
            directory = self.root / current
            _reject_symlink(directory)
            if current == DERIVED:
                rows.extend(sorted(derived, key=lambda row: row["path"]))
                continue
            for path in sorted(directory.iterdir()):
                if path.name.startswith(".") or path.is_symlink():
                    continue
                if not path.is_dir():
                    continue
                row: dict[str, Any] = {"state": current, "session_id": path.name}
                with contextlib.suppress(Exception):
                    header = _read_json(path / HEADER_NAME)
                    row.update({key: header.get(key) for key in ("game_id", "run_id")})
                with contextlib.suppress(Exception):
                    status = _read_json(path / STATUS_NAME)
                    row["status"] = status.get("state")
                    row["record_count"] = status.get("record_count")
                row["derived_outputs"] = []
                with contextlib.suppress(Exception):
                    binding = _read_json(path / MANIFEST_NAME)["manifest_sha256"]
                    row["derived_outputs"] = [item for item in derived
                        if item.get("source_manifest_sha256") == binding
                        and item["session_id"] == path.name]
                rows.append(row)
        return rows

    def validate(self, session_id: str, *, state: str = ACCEPTED_UNPROCESSED, **kwargs) -> ValidationReport:
        path = self.state_path(state, session_id)
        return validate_bundle(path, **kwargs)

    def ingest(self, session_id: str, *, expected_manifest_sha256: str | None = None) -> Path:
        source = self.state_path(ACCEPTED_UNPROCESSED, session_id)
        if not source.is_dir() or source.is_symlink():
            raise BundleError(f"accepted bundle does not exist: {session_id}")
        manifest, errors = _final_manifest(source)
        if errors or manifest is None:
            raise BundleValidationError(errors or ["accepted bundle manifest is missing"])
        if expected_manifest_sha256 is not None and manifest.get("manifest_sha256") != expected_manifest_sha256:
            raise BundleValidationError(["accepted manifest hash does not match caller"])
        report = validate_bundle(source, require_complete=True)
        if not report.valid:
            raise BundleValidationError(list(report.errors), report)
        destination = self.state_path(INGESTED, session_id)
        with _inbox_lock(self.root):
            _atomic_move(source, destination)
        return destination

    def quarantine(self, session_id: str, *, state: str = ACTIVE_PARTIALS) -> Path:
        """Move an inspectable failed run to ``failed`` without deleting it."""

        identifier = session_id
        final_identifier = identifier
        if state == ACTIVE_PARTIALS and not identifier.endswith(".partial"):
            final_identifier = identifier + ".partial"
        source = self.state_path(state, final_identifier)
        destination = self.root / FAILED / final_identifier
        _reject_symlink(destination)
        with _inbox_lock(self.root):
            _atomic_move(source, destination)
        return destination

    def store_derived(
        self,
        session_id: str,
        artifact_name: str,
        payload: Any,
        *,
        source_state: str = ACCEPTED_UNPROCESSED,
    ) -> Path:
        """Store derived output under its own hash binding.

        The source bundle remains immutable and is never rewritten with a
        comparison result.  Derived JSON is canonicalized to make its digest
        independent of dictionary insertion order.
        """

        artifact_name = _safe_artifact_name(artifact_name)
        source = self.state_path(source_state, session_id)
        manifest, errors = _final_manifest(source)
        if errors or manifest is None:
            raise BundleValidationError(errors or ["source bundle manifest is missing"])
        source_report = validate_bundle(source, require_complete=True)
        if not source_report.valid:
            raise BundleValidationError(list(source_report.errors), source_report)
        _ensure_public_data(payload, path="derived.payload")
        destination_dir = self.root / DERIVED / session_id
        _reject_symlink(destination_dir)
        with _inbox_lock(self.root):
            if destination_dir.exists() and not destination_dir.is_dir():
                raise UnsafePathError("derived session path is not a directory")
            destination_dir.mkdir(exist_ok=True)
            artifact_path = destination_dir / f"{artifact_name}.json"
            manifest_path = destination_dir / DERIVED_MANIFEST_NAME
            if artifact_path.exists() or artifact_path.is_symlink() or manifest_path.exists() or manifest_path.is_symlink():
                raise BundleExistsError(f"derived output already exists for {session_id}")
            data = _canonical_bytes(payload) + b"\n"
            _write_bytes_atomic(artifact_path, data)
            derived_manifest = {
                "schema": "melee-web-reference-derived-v1",
                "version": 1,
                "session_id": session_id,
                "artifact": {
                    "name": artifact_path.name,
                    "bytes": len(data),
                    "sha256": _sha256_bytes(data),
                },
                "source": {
                    "state": source_state,
                    "manifest_sha256": manifest["manifest_sha256"],
                    "raw_immutable": True,
                },
                "created_at_unix": time.time(),
            }
            derived_manifest["manifest_sha256"] = _manifest_binding(derived_manifest)
            _write_json_atomic(manifest_path, derived_manifest)
            _fsync_directory(destination_dir)
        return artifact_path

    write_derived = store_derived


def begin_session(*args, **kwargs) -> ReferenceSessionBundle:
    """Functional API alias for capture supervisors."""

    return ReferenceSessionBundle.begin(*args, **kwargs)


def validate_session(*args, **kwargs) -> ValidationReport:
    return validate_bundle(*args, **kwargs)


__all__ = [
    "SCHEMA",
    "VERSION",
    "ACTIVE_PARTIALS",
    "ACCEPTED_UNPROCESSED",
    "FAILED",
    "INGESTED",
    "DERIVED",
    "RAW_OBSERVER_NAME",
    "MANIFEST_NAME",
    "SEMANTIC_VALIDATION_NAME",
    "BundleError",
    "BundleExistsError",
    "BundleStateError",
    "BundleValidationError",
    "UnsafePathError",
    "ValidationReport",
    "ReferenceSessionBundle",
    "ReferenceCaptureInbox",
    "begin_session",
    "validate_bundle",
    "validate_session",
]
