#!/usr/bin/env python3
"""Generate a private, deterministic pipeline-use requirement sidecar.

The input is deliberately split into three pieces:

* a recorder chunk (JSON or JSONL) containing the ordered use stream;
* a binding metadata object containing source/dependency/renderer identities; and
* a source-owned coverage manifest containing the cases and frozen input scope.

The SQLite file is opened read-only.  A nonempty WAL is rejected because its
rows are not covered by the materialized database digest.  No descriptor
bytes, game bytes, paths, addresses, or diagnostic fields are copied to the
generated sidecar.
"""

from __future__ import annotations

import argparse
import base64
import binascii
from collections import defaultdict
import copy
import gzip
import hashlib
import json
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence


SCHEMA = "melee-web-pipeline-requirements-v1"
INPUT_SCHEMA = "melee-web-pipeline-requirements-input-v1"
CAPTURE_SCHEMA = "melee-web-pipeline-use-v1"
COVERAGE_SCHEMA = "melee-web-pipeline-coverage-v1"
VERSION = 1
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
REVISION_RE = re.compile(r"^[0-9a-fA-F]{40,64}$")
HEX_RE = re.compile(r"^[0-9a-fA-F]+$")
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_RECORDS = 10_000_000
MAX_IDENTIFIER_LENGTH = 256

# Aurora's source phase enum is part of the capture contract.  Keep the
# mapping here so a source-owned manifest can use readable names while the
# native recorder's numeric ``context.phase`` remains the authority at join
# time.  Unknown private phase labels remain valid string identities; they are
# useful for a manifest that deliberately names an application-owned phase.
SOURCE_PHASE_IDS: dict[str, int] = {
    "preparation": 1,
    "entry": 2,
    "ready": 3,
    "interactive": 4,
    "death": 5,
    "respawn": 6,
    "ending": 7,
    "teardown": 8,
    "return": 9,
}
SOURCE_SCENE_IDS: dict[str, int] = {
    "boot": 1,
    "css": 2,
    "sss": 3,
    "match": 4,
    "teardown": 5,
    "return": 6,
}
# BOOT/PREPARATION is a valid global setup boundary.  Every other native
# source scene requires a live world for non-preparation lifecycle evidence;
# paired empty preparation scopes remain setup evidence only.
SOURCE_WORLD_SCENES = frozenset((
    SOURCE_SCENE_IDS["css"],
    SOURCE_SCENE_IDS["sss"],
    SOURCE_SCENE_IDS["match"],
    SOURCE_SCENE_IDS["teardown"],
    SOURCE_SCENE_IDS["return"],
))
MENU_FIXED_FIELD_GROUPS = (
    ("menu fighters", ("menu_fighter_numeric_ids", "menu_fighter_ids", "menu_fighters")),
    ("menu costumes", ("menu_costume_numeric_ids", "menu_costume_ids", "menu_costumes")),
    ("menu stage", ("menu_stage_numeric_id", "menu_stage_id", "menu_stage")),
    ("menu ground", ("menu_ground_numeric_id", "menu_ground_id", "menu_ground")),
)
MENU_IDENTITY_FIELDS = frozenset((
    "fighter_numeric_ids", "costume_numeric_ids", "stage_numeric_id", "ground_numeric_id",
))


class PipelineRequirementsError(ValueError):
    """Invalid recorder input; no requirement sidecar is safe to publish."""

    def __init__(self, errors: Sequence[Mapping[str, Any]] | Mapping[str, Any] | str):
        if isinstance(errors, str):
            errors = [{"code": "invalid", "message": errors}]
        elif isinstance(errors, Mapping):
            errors = [errors]
        self.errors = tuple(dict(item) for item in errors)
        message = "; ".join(
            f"{item.get('code', 'invalid')}: {item.get('message', 'invalid input')}"
            for item in self.errors
        )
        super().__init__(message)


def canonical_json(value: Any) -> bytes:
    """Return the stable JSON representation used for binding digests."""

    return json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def sha256_json(value: Any) -> str:
    return sha256_bytes(canonical_json(value))


def _issue(code: str, message: str, **extra: Any) -> dict[str, Any]:
    result = {"code": code, "message": message}
    result.update(extra)
    return result


def _require(condition: bool, errors: list[dict[str, Any]], code: str, message: str, **extra: Any) -> None:
    if not condition:
        errors.append(_issue(code, message, **extra))


def _safe_identifier(value: Any) -> bool:
    return (isinstance(value, str) and bool(value) and len(value) <= MAX_IDENTIFIER_LENGTH and
            "\x00" not in value and "/" not in value and "\\" not in value)


def _require_safe_identifier(value: Any, errors: list[dict[str, Any]], code: str, label: str) -> bool:
    """Reject copied labels that could carry a local path or unbounded value."""

    valid = _safe_identifier(value)
    _require(valid, errors, code, f"{label} must be a bounded path-free identifier")
    return valid


def _read_json(path: Path) -> tuple[Any, str]:
    try:
        data = path.read_bytes()
    except OSError as exc:
        raise PipelineRequirementsError(_issue("input_read", f"cannot read {path.name}: {exc}")) from exc
    if len(data) > MAX_JSON_BYTES:
        raise PipelineRequirementsError(_issue("input_size", f"{path.name} exceeds the private input limit"))
    try:
        value = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        # JSONL is accepted only for capture chunks, and is parsed by
        # load_capture below.  Keeping this function JSON-only prevents a
        # coverage/metadata file from accidentally being treated as a stream.
        raise PipelineRequirementsError(_issue("input_json", f"{path.name} is not UTF-8 JSON: {exc}")) from exc
    return value, sha256_bytes(data)


def _read_capture(path: Path) -> tuple[dict[str, Any], str]:
    """Read one JSON capture or a bounded-line JSONL drain stream.

    JSONL is intentionally streamed so a long run can contain many bounded
    recorder chunks without imposing a whole-run byte limit.  The record cap
    in ``_combine_chunks`` and the normal validator remains the hard total
    bound; no prefix is silently accepted.
    """

    try:
        file_size = path.stat().st_size
    except OSError as exc:
        raise PipelineRequirementsError(_issue("capture_read", "cannot stat capture input")) from exc
    digest = hashlib.sha256()
    if file_size <= MAX_JSON_BYTES:
        try:
            data = path.read_bytes()
        except OSError as exc:
            raise PipelineRequirementsError(_issue("capture_read", "cannot read capture input")) from exc
        digest.update(data)
        try:
            value = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError):
            rows = _capture_jsonl_rows(data.splitlines())
            return _capture_jsonl_value(rows), digest.hexdigest()
        return _capture_json_value(value), digest.hexdigest()

    # A file larger than the per-object limit is a streamed JSONL candidate.
    # A single oversized line is rejected as an oversized JSON object.
    rows: list[Any] = []
    try:
        with path.open("rb") as stream:
            for line_number, raw_line in enumerate(stream, 1):
                digest.update(raw_line)
                if len(raw_line.rstrip(b"\r\n")) > MAX_JSON_BYTES:
                    raise PipelineRequirementsError(_issue("capture_chunk_size",
                                                           f"capture JSONL chunk {line_number} exceeds the private input limit"))
                if not raw_line.strip():
                    continue
                try:
                    rows.append(json.loads(raw_line.decode("utf-8")))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise PipelineRequirementsError(_issue("capture_json",
                                                           f"capture JSONL line {line_number} is invalid: {exc}")) from exc
    except OSError as exc:
        raise PipelineRequirementsError(_issue("capture_read", "cannot read capture input")) from exc
    return _capture_jsonl_value(rows), digest.hexdigest()


def _capture_jsonl_rows(lines: Iterable[bytes]) -> list[Any]:
    rows: list[Any] = []
    for line_number, raw_line in enumerate(lines, 1):
        if len(raw_line.rstrip(b"\r\n")) > MAX_JSON_BYTES:
            raise PipelineRequirementsError(_issue("capture_chunk_size",
                                                   f"capture JSONL chunk {line_number} exceeds the private input limit"))
        if not raw_line.strip():
            continue
        try:
            rows.append(json.loads(raw_line.decode("utf-8")))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PipelineRequirementsError(_issue("capture_json",
                                                   f"capture JSONL line {line_number} is invalid: {exc}")) from exc
    return rows


def _capture_jsonl_value(rows: list[Any]) -> dict[str, Any]:
    if not rows or not isinstance(rows[0], dict):
        raise PipelineRequirementsError(_issue("capture_shape", "JSONL capture must begin with an object header"))
    if isinstance(rows[0].get("records"), list):
        return _combine_chunks(rows)
    value = dict(rows[0])
    value["records"] = rows[1:]
    return _capture_json_value(value)


def _expand_capture_chunk(chunk: Mapping[str, Any], label: str = "capture chunk") -> dict[str, Any]:
    """Expand the recorder's optional per-chunk context dictionary.

    Native drains use ``contexts`` plus an integer ``context_index`` on each
    record to avoid repeating a large immutable source context.  The rest of
    the generator intentionally consumes the historical verbose record shape,
    so expansion happens before chunks are joined and before any validation or
    grouping.  A context table is local to one chunk; indexes are never carried
    across the join boundary.

    A chunk with no indexed records remains valid in the existing verbose form.
    Unreferenced table entries are retained only long enough to inspect the
    chunk and are not required to be well formed.  A malformed entry becomes an
    error when an index actually references it.
    """

    value = dict(chunk)
    records = value.get("records")
    if not isinstance(records, list):
        return value

    contexts = value.get("contexts")
    # Context dictionaries are immutable for the duration of generation: all
    # downstream validation reads them and never mutates them.  Detach one
    # shallow outer mapping per referenced local index, then share it across
    # records.  This avoids copying the large player/source-context payload
    # once per event while preserving chunk-local index isolation.
    context_cache: dict[int, Mapping[str, Any]] = {}
    expanded: list[Any] = []
    for record_index, record in enumerate(records):
        if not isinstance(record, Mapping):
            # The normal record validator reports the stable record-shape
            # error.  Do not dereference arbitrary values while decoding.
            expanded.append(record)
            continue

        if "context_index" not in record:
            expanded.append(dict(record))
            continue

        direct_context_keys = ("context", "scope", "source_context")
        if any(key in record for key in direct_context_keys):
            raise PipelineRequirementsError(_issue(
                "capture_context_form",
                f"{label} record {record_index} carries both context_index and direct context",
            ))
        if not isinstance(contexts, list):
            raise PipelineRequirementsError(_issue(
                "capture_context_index",
                f"{label} record {record_index} has context_index but no contexts list",
            ))

        context_index = record.get("context_index")
        if isinstance(context_index, bool) or not isinstance(context_index, int):
            raise PipelineRequirementsError(_issue(
                "capture_context_index",
                f"{label} record {record_index} context_index is not an integer",
            ))
        if context_index < 0 or context_index >= len(contexts):
            raise PipelineRequirementsError(_issue(
                "capture_context_index",
                f"{label} record {record_index} context_index is outside contexts",
            ))

        context = contexts[context_index]
        if not isinstance(context, Mapping):
            raise PipelineRequirementsError(_issue(
                "capture_context",
                f"{label} context {context_index} is not an object",
            ))
        expanded_context = context_cache.get(context_index)
        if expanded_context is None:
            expanded_context = dict(context)
            context_cache[context_index] = expanded_context
        restored = dict(record)
        del restored["context_index"]
        restored["context"] = expanded_context
        expanded.append(restored)

    value["records"] = expanded
    # The historical shape has no chunk-level context table.  Removing it also
    # prevents an unused malformed table from crossing the join boundary.
    value.pop("contexts", None)
    return value


def _capture_json_value(value: Any) -> dict[str, Any]:
    if isinstance(value, list):
        if not all(isinstance(item, Mapping) for item in value):
            raise PipelineRequirementsError(_issue("capture_shape", "capture chunk list must contain objects"))
        return _combine_chunks(value)
    if not isinstance(value, dict):
        raise PipelineRequirementsError(_issue("capture_shape", "capture root must be an object"))
    return _expand_capture_chunk(value)


def _combine_chunks(chunks: Sequence[Mapping[str, Any]]) -> dict[str, Any]:
    """Join ordered JSON drain chunks without rewriting their event order."""

    if not chunks:
        raise PipelineRequirementsError(_issue("capture_chunks", "capture has no chunks"))
    # Context indexes are local to each native drain.  Decode each complete
    # chunk before appending records so a later chunk cannot accidentally use a
    # prior chunk's dictionary.
    expanded_chunks: list[dict[str, Any]] = []
    for index, chunk in enumerate(chunks):
        if not isinstance(chunk, Mapping):
            raise PipelineRequirementsError(_issue("capture_chunks", f"chunk {index} is not an object"))
        expanded_chunks.append(_expand_capture_chunk(chunk, f"capture chunk {index}"))

    result = dict(expanded_chunks[0])
    all_records: list[Any] = []
    all_descriptors: list[Any] = []
    previous_end: int | None = None
    for index, chunk in enumerate(expanded_chunks):
        if not isinstance(chunk, Mapping) or chunk.get("schema") != CAPTURE_SCHEMA or chunk.get("version") != VERSION:
            raise PipelineRequirementsError(_issue("capture_chunks", f"chunk {index} has an incompatible schema/version"))
        if index and chunk.get("capture_generation") != result.get("capture_generation"):
            raise PipelineRequirementsError(_issue("stale_generation", "capture chunks have different generations"))
        chunk_capture_id = chunk.get("capture_id")
        if chunk_capture_id is not None:
            if not _safe_identifier(chunk_capture_id):
                raise PipelineRequirementsError(_issue("capture_id", "capture chunks require bounded path-free capture IDs"))
            if index and chunk_capture_id != result.get("capture_id"):
                raise PipelineRequirementsError(_issue("capture_id", "capture chunks have different capture IDs"))
        chunk_status = chunk.get("status")
        if (not isinstance(chunk_status, Mapping) or chunk_status.get("valid") is not True or
                chunk_status.get("dropped") != 0 or chunk_status.get("errors")):
            raise PipelineRequirementsError(_issue("capture_invalid", f"capture chunk {index} is invalid"))
        if "overflow" in chunk_status and chunk_status.get("overflow") is not False:
            raise PipelineRequirementsError(_issue("capture_overflow", f"capture chunk {index} overflowed"))
        begin, end = chunk.get("sequence_begin"), chunk.get("sequence_end")
        records = chunk.get("records")
        descriptors = chunk.get("descriptors", [])
        if not isinstance(begin, int) or not isinstance(end, int) or not isinstance(records, list) or not isinstance(descriptors, list):
            raise PipelineRequirementsError(_issue("capture_chunks", f"chunk {index} has invalid sequence/records"))
        if not all(isinstance(record, Mapping) for record in records):
            raise PipelineRequirementsError(_issue("capture_shape", f"chunk {index} records must be objects"))
        if len(all_records) + len(records) > MAX_RECORDS:
            raise PipelineRequirementsError(_issue("capture_records", "capture exceeds recorder record limit"))
        if records and end - begin + 1 != len(records):
            raise PipelineRequirementsError(_issue("sequence_gap", f"chunk {index} has a sequence gap"))
        if not records and (begin != 0 or end != 0):
            raise PipelineRequirementsError(_issue("sequence_gap", f"chunk {index} advertises records but contains none"))
        if previous_end is not None and records and begin != previous_end + 1:
            raise PipelineRequirementsError(_issue("sequence_gap", "capture chunks are not contiguous"))
        if records:
            previous_end = end
            all_records.extend(records)
        all_descriptors.extend(descriptors)
        # The last drain owns the lifecycle counters.  Earlier chunks may be
        # active snapshots with open scopes or pending records; retaining the
        # first status would incorrectly reject a valid final drain.
        if index == len(chunks) - 1:
            result["status"] = dict(chunk_status)
    result["records"] = all_records
    result["sequence_begin"] = all_records[0]["sequence"] if all_records else 0
    result["sequence_end"] = all_records[-1]["sequence"] if all_records else 0
    result["descriptors"] = all_descriptors
    if isinstance(result.get("status"), Mapping):
        status = dict(result["status"])
        status["total_records"] = len(all_records)
        status["drained_records"] = len(all_records)
        result["status"] = status
    return result


def _object_input(value: Mapping[str, Any] | str | Path, label: str) -> tuple[dict[str, Any], str]:
    if isinstance(value, (str, Path)):
        parsed, digest = _read_json(Path(value))
    else:
        parsed, digest = dict(value), sha256_json(value)
    if not isinstance(parsed, dict):
        raise PipelineRequirementsError(_issue(f"{label}_shape", f"{label} root must be an object"))
    return parsed, digest


def _sha256(value: Any, label: str, errors: list[dict[str, Any]], *, required: bool = True) -> str | None:
    if value is None and not required:
        return None
    if not isinstance(value, str) or not SHA256_RE.fullmatch(value.lower()):
        errors.append(_issue("invalid_hash", f"{label} must be a lowercase SHA-256 digest"))
        return None
    return value.lower()


def _revision(value: Any, label: str, errors: list[dict[str, Any]]) -> str | None:
    if not isinstance(value, str) or not REVISION_RE.fullmatch(value):
        errors.append(_issue("invalid_revision", f"{label} must be a 40- or 64-digit revision"))
        return None
    return value.lower()


def _path_digest(path: Path) -> tuple[bytes, str]:
    """Read a decoded SQLite seed, accepting the checked-in gzip/base64 form."""

    try:
        raw = path.read_bytes()
    except OSError as exc:
        raise PipelineRequirementsError(_issue("seed_read", "cannot read seed input")) from exc
    if raw.startswith(b"SQLite format 3\0"):
        return raw, sha256_bytes(raw)
    try:
        decoded = gzip.decompress(base64.b64decode(b"".join(raw.split()), validate=True))
    except (OSError, EOFError, binascii.Error, ValueError) as exc:
        raise PipelineRequirementsError(_issue("seed_format", "seed is neither a decoded SQLite file nor gzip/base64 SQLite")) from exc
    if not decoded.startswith(b"SQLite format 3\0"):
        raise PipelineRequirementsError(_issue("seed_format", "decoded seed is not a SQLite database"))
    return decoded, sha256_bytes(decoded)


def _sqlite_rows(path: Path) -> tuple[bytes, str, list[dict[str, Any]]]:
    decoded, digest = _path_digest(path)
    wal_path = Path(str(path) + "-wal")
    try:
        wal_size = wal_path.stat().st_size if wal_path.is_file() else 0
    except OSError as exc:
        raise PipelineRequirementsError(_issue("seed_wal", "cannot inspect private SQLite WAL sidecar")) from exc
    if wal_size:
        # The binding covers the materialized database bytes.  Querying a
        # nonempty WAL would allow visible rows to differ without changing
        # that digest, so certification requires a materialized snapshot.
        raise PipelineRequirementsError(_issue("seed_wal", "seed has a nonempty WAL; provide a materialized SQLite snapshot"))
    with tempfile.TemporaryDirectory(prefix="melee-pipeline-seed-") as directory:
        copied = Path(directory) / "seed.db"
        copied.write_bytes(decoded)
        # Empty sidecars are harmless and can be copied for SQLite's read-only
        # open.  A nonempty WAL was rejected above; never checkpoint the
        # caller's database.
        for suffix in ("-wal", "-shm"):
            sidecar = Path(str(path) + suffix)
            if sidecar.is_file():
                shutil.copyfile(sidecar, Path(str(copied) + suffix))
        db: sqlite3.Connection | None = None
        try:
            db = sqlite3.connect(f"file:{copied}?mode=ro", uri=True)
            db.execute("PRAGMA query_only=ON")
            db.execute("PRAGMA busy_timeout=1000")
            if db.execute("PRAGMA integrity_check").fetchone() != ("ok",):
                raise PipelineRequirementsError(_issue("seed_integrity", "pipeline seed failed SQLite integrity_check"))
            tables = {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
            if "pipeline_cache" not in tables:
                raise PipelineRequirementsError(_issue("seed_schema", "pipeline seed has no pipeline_cache table"))
            columns = [row[1] for row in db.execute("PRAGMA table_info(pipeline_cache)")]
            expected = ["type", "hash", "config_version", "config_size", "config", "first_frame_used"]
            if columns != expected:
                raise PipelineRequirementsError(_issue("seed_schema", "pipeline_cache columns differ from Aurora cache schema"))
            rows = []
            for kind, ref, config_version, config_size, config, first_frame_used in db.execute(
                "SELECT type, hash, config_version, config_size, config, first_frame_used "
                "FROM pipeline_cache ORDER BY type, hash"
            ):
                if not isinstance(config, (bytes, bytearray, memoryview)):
                    raise PipelineRequirementsError(_issue("seed_row", "pipeline descriptor config is not a BLOB"))
                payload = bytes(config)
                try:
                    kind_value = int(kind)
                    ref_value = _ref_hex(ref)
                    version_value = int(config_version)
                    size_value = int(config_size)
                    first_frame_value = int(first_frame_used)
                except (TypeError, ValueError) as exc:
                    raise PipelineRequirementsError(_issue("seed_row", "pipeline descriptor row has invalid typed fields")) from exc
                if (kind_value < 0 or version_value < 0 or size_value < 0 or first_frame_value < 0):
                    raise PipelineRequirementsError(_issue("seed_row", "pipeline descriptor row has negative typed fields"))
                if size_value != len(payload):
                    raise PipelineRequirementsError(_issue("seed_row", "pipeline descriptor size does not match config"))
                rows.append({
                    "type": kind_value,
                    "ref_hex": ref_value,
                    "config_version": version_value,
                    "size": len(payload),
                    "sha256": sha256_bytes(payload),
                    "first_frame_used": first_frame_value,
                })
        except sqlite3.Error as exc:
            raise PipelineRequirementsError(_issue("seed_sqlite", f"cannot inspect private SQLite seed: {exc}")) from exc
        finally:
            if db is not None:
                db.close()
    return decoded, digest, rows


def _ref_hex(value: Any) -> str:
    """Normalize SQLite's signed integer key to a typed 64-bit hex row ref."""

    if isinstance(value, bool):
        raise ValueError("row reference must not be boolean")
    if isinstance(value, int):
        if value < -(1 << 63) or value >= (1 << 64):
            raise ValueError("row reference is outside SQLite's integer range")
        number = value & ((1 << 64) - 1)
    elif isinstance(value, str):
        token = value.lower()
        if token.startswith("0x"):
            token = token[2:]
        if not token or len(token) > 16 or not HEX_RE.fullmatch(token):
            raise ValueError("row reference must be a 64-bit hexadecimal value")
        number = int(token, 16)
    else:
        raise ValueError("row reference must be an integer or hexadecimal string")
    return f"{number:016x}"


def _descriptor_from(value: Any, errors: list[dict[str, Any]], where: str) -> dict[str, Any] | None:
    if not isinstance(value, Mapping):
        errors.append(_issue("descriptor_shape", f"{where} descriptor must be an object"))
        return None
    try:
        kind = value["type"]
        # ref_hex is the recorder contract.  Ref is accepted only to make
        # migration of an in-flight private capture explicit in diagnostics;
        # the generated sidecar always emits ref_hex.
        ref = value["ref_hex"] if "ref_hex" in value else value["ref"]
        size = value["size"] if "size" in value else value["bytes"]
        result = {
            "type": int(kind),
            "ref_hex": _ref_hex(ref),
            "config_version": int(value["config_version"]),
            "size": int(size),
            "sha256": str(value["sha256"]).lower(),
        }
    except (KeyError, TypeError, ValueError) as exc:
        errors.append(_issue("descriptor_shape", f"{where} descriptor has invalid typed row/config fields: {exc}"))
        return None
    _require(result["type"] >= 0, errors, "descriptor_type", f"{where} descriptor type is negative")
    _require(result["config_version"] >= 0 and result["size"] >= 0, errors,
             "descriptor_config", f"{where} descriptor config version/size is negative")
    _require(SHA256_RE.fullmatch(result["sha256"]) is not None, errors,
             "descriptor_digest", f"{where} descriptor digest is not SHA-256")
    return result


def _binding_metadata(metadata: Mapping[str, Any], coverage_digest: str, seed_digest: str,
                      source_root: Path | None) -> tuple[dict[str, Any], dict[str, Any], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    _require(metadata.get("schema") == INPUT_SCHEMA, errors, "metadata_schema", "unsupported or missing metadata schema")
    _require(metadata.get("version") == VERSION, errors, "metadata_version", "unsupported or missing metadata version")

    seed = metadata.get("seed") if isinstance(metadata.get("seed"), Mapping) else metadata
    declared_seed = seed.get("decoded_sha256") if isinstance(seed, Mapping) else None
    if declared_seed is None:
        declared_seed = metadata.get("seed_decoded_sha256")
    declared_seed = _sha256(declared_seed, "metadata seed decoded SHA-256", errors)
    _require(declared_seed == seed_digest, errors, "seed_digest_mismatch", "metadata seed digest differs from actual decoded seed",
             declared=declared_seed, actual=seed_digest)

    source = metadata.get("source") if isinstance(metadata.get("source"), Mapping) else metadata
    source_head = source.get("head") if isinstance(source, Mapping) else None
    source_head = source_head or metadata.get("source_head") or metadata.get("source_head_sha256")
    source_head = _revision(source_head, "source head", errors)
    dirty = source.get("dirty_overlay_sha256") if isinstance(source, Mapping) else None
    dirty = dirty or metadata.get("dirty_overlay_sha256")
    dirty = _sha256(dirty, "dirty overlay digest", errors)

    if source_root is not None:
        actual_head, actual_dirty = _git_binding(source_root, errors)
        _require(actual_head == source_head, errors, "source_head_mismatch", "source head differs from source checkout",
                 declared=source_head, actual=actual_head)
        _require(actual_dirty == dirty, errors, "dirty_overlay_mismatch", "dirty overlay differs from source checkout",
                 declared=dirty, actual=actual_dirty)

    dependencies = metadata.get("dependencies")
    _require(isinstance(dependencies, Mapping) and bool(dependencies), errors,
             "dependencies_missing", "dependency revisions are required")
    normalized_deps: dict[str, str] = {}
    if isinstance(dependencies, Mapping):
        for name in sorted(dependencies, key=lambda value: (type(value).__name__, str(value))):
            if not _require_safe_identifier(name, errors, "dependency_name", "dependency names"):
                continue
            revision = _revision(dependencies[name], f"dependency {name}", errors)
            if revision:
                normalized_deps[name] = revision

    renderer = metadata.get("renderer")
    _require(isinstance(renderer, Mapping), errors, "renderer_missing", "renderer/config layout binding is required")
    normalized_renderer: dict[str, Any] = {}
    if isinstance(renderer, Mapping):
        version = renderer.get("version")
        layout = renderer.get("config_layout")
        layout_digest = renderer.get("config_layout_sha256")
        if isinstance(version, str):
            _require_safe_identifier(version, errors, "renderer_version", "renderer version")
        else:
            _require(isinstance(version, int) and not isinstance(version, bool), errors,
                     "renderer_version", "renderer version is required")
        if isinstance(layout, str):
            _require_safe_identifier(layout, errors, "renderer_layout", "renderer config layout")
        else:
            _require(isinstance(layout, int) and not isinstance(layout, bool), errors,
                     "renderer_layout", "renderer config layout is required")
        if layout_digest is not None:
            layout_digest = _sha256(layout_digest, "renderer config layout digest", errors)
        normalized_renderer = {"version": version, "config_layout": layout}
        if layout_digest:
            normalized_renderer["config_layout_sha256"] = layout_digest

    registry = metadata.get("registry")
    _require(isinstance(registry, (Mapping, list)) and bool(registry), errors,
             "registry_missing", "numeric source registry identities are required")
    registry_digest = sha256_json(registry) if isinstance(registry, (Mapping, list)) else ""
    declared_registry = metadata.get("registry_sha256")
    if declared_registry is not None:
        declared_registry = _sha256(declared_registry, "registry digest", errors)
        _require(declared_registry == registry_digest, errors, "registry_digest_mismatch",
                 "metadata registry digest does not match registry identities")

    declared_coverage = metadata.get("coverage_manifest_sha256")
    if declared_coverage is not None:
        declared_coverage = _sha256(declared_coverage, "coverage manifest digest", errors)
        _require(declared_coverage == coverage_digest, errors, "coverage_digest_mismatch",
                 "metadata coverage digest does not match coverage manifest")

    if errors:
        raise PipelineRequirementsError(errors)
    binding = {
        "seed_decoded_sha256": seed_digest,
        "source_head": source_head,
        "dirty_overlay_sha256": dirty,
        "dependencies": normalized_deps,
        "renderer": normalized_renderer,
        "registry_sha256": registry_digest,
        "coverage_manifest_sha256": coverage_digest,
    }
    # Registry names and paths are deliberately not copied to output.
    public = {
        "seed_decoded_sha256": seed_digest,
        "source_head": source_head,
        "dirty_overlay_sha256": dirty,
        "dependencies": normalized_deps,
        "renderer": normalized_renderer,
        "registry_sha256": registry_digest,
        "coverage_manifest_sha256": coverage_digest,
    }
    return binding, public, []


def _git_binding(root: Path, errors: list[dict[str, Any]]) -> tuple[str | None, str | None]:
    """Hash HEAD plus tracked and untracked source edits in an isolated index."""

    try:
        head = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip().lower()
        with tempfile.TemporaryDirectory(prefix="melee-pipeline-index-") as directory:
            isolated_index = Path(directory) / "index"
            environment = dict(os.environ)
            environment["GIT_INDEX_FILE"] = str(isolated_index)
            subprocess.check_call(["git", "-C", str(root), "read-tree", "HEAD"], env=environment,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            subprocess.check_call(["git", "-C", str(root), "add", "--all"], env=environment,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            overlay = subprocess.check_output(["git", "-C", str(root), "diff", "--cached", "--binary", "HEAD"],
                                               env=environment, text=False)
    except (OSError, subprocess.CalledProcessError) as exc:
        # Keep private checkout paths out of the hard-rejection sidecar.
        errors.append(_issue("source_checkout", "cannot derive source checkout binding"))
        return None, None
    return head, sha256_bytes(overlay)


def _normalize_cases(coverage: Mapping[str, Any], capture_id: str | None) -> tuple[list[dict[str, Any]], set[tuple[str, Any, Any]], list[dict[str, Any]]]:
    errors: list[dict[str, Any]] = []
    _require(coverage.get("schema") == COVERAGE_SCHEMA, errors, "coverage_schema", "unsupported or missing coverage schema")
    _require(coverage.get("version") == VERSION, errors, "coverage_version", "unsupported or missing coverage version")
    phase_ids = dict(SOURCE_PHASE_IDS)
    declared_phase_ids = coverage.get("phase_ids", coverage.get("phase_aliases"))
    if declared_phase_ids is not None:
        _require(isinstance(declared_phase_ids, Mapping), errors, "coverage_phase_ids",
                 "coverage phase_ids must map names to numeric source phase IDs")
        if isinstance(declared_phase_ids, Mapping):
            for name, value in declared_phase_ids.items():
                _require(isinstance(name, str) and bool(name), errors, "coverage_phase_ids",
                         "coverage phase IDs require non-empty string names")
                _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                         errors, "coverage_phase_ids", f"coverage phase ID for {name!r} is invalid")
                if isinstance(name, str) and isinstance(value, int) and not isinstance(value, bool) and value >= 0:
                    _require(name not in SOURCE_PHASE_IDS or SOURCE_PHASE_IDS[name] == value, errors,
                             "coverage_phase_ids", f"coverage phase ID for native name {name!r} conflicts with source enum")
                    phase_ids[name] = value
    scene_ids = dict(SOURCE_SCENE_IDS)
    declared_scene_ids = coverage.get("scene_ids", coverage.get("scene_aliases"))
    if declared_scene_ids is not None:
        _require(isinstance(declared_scene_ids, Mapping), errors, "coverage_scene_ids",
                 "coverage scene_ids must map names to numeric source scene IDs")
        if isinstance(declared_scene_ids, Mapping):
            for name, value in declared_scene_ids.items():
                _require(isinstance(name, str) and bool(name), errors, "coverage_scene_ids",
                         "coverage scene IDs require non-empty string names")
                _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                         errors, "coverage_scene_ids", f"coverage scene ID for {name!r} is invalid")
                if isinstance(name, str) and isinstance(value, int) and not isinstance(value, bool) and value >= 0:
                    _require(name not in SOURCE_SCENE_IDS or SOURCE_SCENE_IDS[name] == value, errors,
                             "coverage_scene_ids", f"coverage scene ID for native name {name!r} conflicts with source enum")
                    scene_ids[name] = value
    declared_case_ids = coverage.get("case_ids", coverage.get("case_aliases"))
    case_aliases: dict[int, str] = {}
    if declared_case_ids is not None:
        _require(isinstance(declared_case_ids, Mapping), errors, "coverage_case_ids",
                 "coverage case_ids must map numeric source IDs to case IDs")
        if isinstance(declared_case_ids, Mapping):
            for numeric, target in declared_case_ids.items():
                try:
                    numeric_id = int(numeric)
                except (TypeError, ValueError):
                    numeric_id = -1
                _require(isinstance(numeric, (str, int)) and not isinstance(numeric, bool) and
                         str(numeric).lstrip("+").isdigit() and numeric_id >= 0,
                         errors, "coverage_case_ids", f"coverage case ID {numeric!r} is invalid")
                _require(isinstance(target, (str, int)) and not isinstance(target, bool) and bool(target),
                         errors, "coverage_case_ids", f"coverage case alias {numeric!r} has no case target")
                if numeric_id >= 0 and isinstance(target, (str, int)) and not isinstance(target, bool) and bool(target):
                    target_key = str(target)
                    if _require_safe_identifier(target_key, errors, "coverage_case_id", "coverage case alias target"):
                        prior_target = case_aliases.get(numeric_id)
                        _require(prior_target in (None, target_key), errors, "coverage_case_ids",
                                 f"numeric coverage case ID {numeric_id} maps to conflicting cases")
                        case_aliases[numeric_id] = target_key
    cases_raw = coverage.get("cases")
    _require(isinstance(cases_raw, list) and bool(cases_raw), errors, "coverage_cases", "coverage manifest must declare cases")
    cases: list[dict[str, Any]] = []
    seen: set[str] = set()
    if isinstance(cases_raw, list):
        for index, item in enumerate(cases_raw):
            if not isinstance(item, Mapping):
                errors.append(_issue("coverage_case", f"coverage case {index} is not an object"))
                continue
            case_id = item.get("case_id", item.get("id"))
            route_id = item.get("route_id")
            route = item.get("route")
            if route_id is None and isinstance(route, Mapping):
                route_id = route.get("route_id", route.get("id"))
            if route_id is None and isinstance(route, str):
                route_id = route
            phases = item.get("expected_phases", item.get("phases"))
            expected_scenes = item.get("expected_scenes", item.get("scenes"))
            actions = item.get("expected_actions", item.get("actions"))
            costumes = item.get("expected_costumes", item.get("costumes"))
            lifecycle = item.get("lifecycle", item.get("expected_lifecycle"))
            source_ticks = item.get("expected_source_ticks", item.get("expected_ticks", item.get("source_ticks", [])))
            numeric_case_id = item.get("coverage_case_id", item.get("numeric_id"))
            case_label = f"coverage case {index}"
            _require(numeric_case_id is None or
                     (isinstance(numeric_case_id, int) and not isinstance(numeric_case_id, bool) and numeric_case_id >= 0),
                     errors, "coverage_case_ids", f"{case_label} has an invalid numeric coverage_case_id")
            _require(isinstance(case_id, (str, int)) and not isinstance(case_id, bool) and bool(case_id), errors, "coverage_case_id", f"{case_label} has no case_id")
            _require(isinstance(route_id, (str, int)) and not isinstance(route_id, bool) and bool(route_id), errors, "coverage_route_id", f"{case_label} has no route_id")
            for label, value in (("phases", phases), ("actions", actions), ("costumes", costumes),
                                 ("lifecycle", lifecycle), ("source_ticks", source_ticks)):
                _require(isinstance(value, list), errors, "coverage_field", f"{case_label} must declare {label}")
            for label, values in (("actions", actions), ("costumes", costumes), ("lifecycle", lifecycle)):
                if isinstance(values, list):
                    for value in values:
                        normalized = _stable_scalar(value)
                        if isinstance(normalized, str):
                            _require_safe_identifier(normalized, errors, f"coverage_{label}", f"coverage {label}")
            if isinstance(source_ticks, list):
                for tick in source_ticks:
                    _require(isinstance(tick, int) and not isinstance(tick, bool) and tick >= 0, errors,
                             "coverage_field", f"{case_label} source ticks must be non-negative integers")
            _require(expected_scenes is None or isinstance(expected_scenes, list), errors, "coverage_field",
                     f"{case_label} expected_scenes must be a list")
            if isinstance(phases, list) and isinstance(expected_scenes, list):
                _require(len(expected_scenes) == len(phases), errors, "coverage_field",
                         f"{case_label} expected_scenes must align with expected_phases")
            if not isinstance(case_id, (str, int)) or isinstance(case_id, bool) or not isinstance(route_id, (str, int)) or isinstance(route_id, bool):
                continue
            case_key, route_key = str(case_id), str(route_id)
            safe_case = _require_safe_identifier(case_key, errors, "coverage_case_id", "coverage case_id")
            safe_route = _require_safe_identifier(route_key, errors, "coverage_route_id", "coverage route_id")
            if not safe_case or not safe_route:
                # Do not continue normalizing a rejected identity: later
                # diagnostics must not echo a path-shaped user value.
                continue
            _require(case_key not in seen, errors, "coverage_duplicate_case", "duplicate coverage case_id")
            seen.add(case_key)
            input_binding = item.get("input", item.get("frozen_input"))
            _require(isinstance(input_binding, Mapping), errors, "coverage_input", f"{case_label} has no frozen input binding")
            if isinstance(input_binding, Mapping):
                input_digest = (input_binding["sha256"] if "sha256" in input_binding else
                                input_binding.get("digest"))
            else:
                input_digest = None
            input_digest = _sha256(input_digest, f"{case_label} input digest", errors)
            if isinstance(input_binding, Mapping):
                expected_capture = (input_binding["capture_id"] if "capture_id" in input_binding else
                                    input_binding.get("capture"))
            else:
                expected_capture = None
            _require_safe_identifier(expected_capture, errors, "coverage_capture_id", "coverage capture_id")
            if capture_id is not None:
                _require(expected_capture == capture_id, errors, "coverage_capture_mismatch",
                         f"{case_label} is bound to a different capture")
            route_identity = copy.deepcopy(route) if isinstance(route, Mapping) else {"route_id": route_id}
            _validate_menu_identity_route(route_identity, errors, case_label)
            phase_specs: list[tuple[Any, Any]] = []
            if isinstance(phases, list):
                for phase_index, phase_spec in enumerate(phases):
                    phase_scene: Any = None
                    phase_value: Any = phase_spec
                    if isinstance(phase_spec, Mapping):
                        phase_scene = _scene_value(phase_spec.get("scene"), scene_ids)
                        phase_value = phase_spec.get("phase")
                    elif isinstance(expected_scenes, list) and phase_index < len(expected_scenes):
                        phase_scene = _scene_value(expected_scenes[phase_index], scene_ids)
                    normalized_phase = _phase_value(phase_value, phase_ids)
                    if isinstance(normalized_phase, str):
                        _require_safe_identifier(normalized_phase, errors, "coverage_phase", "coverage phase")
                    if isinstance(phase_scene, str):
                        _require_safe_identifier(phase_scene, errors, "coverage_scene", "coverage scene")
                    phase_specs.append((phase_scene, normalized_phase))
            cases.append({
                "case_id": case_key,
                "route_id": route_key,
                "route_identity": route_identity,
                "route_identity_sha256": sha256_json(route_identity),
                "expected_phases": tuple(phase for _scene, phase in phase_specs),
                "expected_contexts": tuple(phase_specs),
                "expected_actions": tuple(_stable_scalar(x) for x in actions) if isinstance(actions, list) else tuple(),
                "expected_costumes": tuple(_stable_scalar(x) for x in costumes) if isinstance(costumes, list) else tuple(),
                "lifecycle": tuple(_stable_scalar(x) for x in lifecycle) if isinstance(lifecycle, list) else tuple(),
                "expected_source_ticks": tuple(_stable_scalar(x) for x in source_ticks) if isinstance(source_ticks, list) else tuple(),
                "input_sha256": input_digest,
                "capture_id": expected_capture,
                "phase_ids": phase_ids,
                "scene_ids": scene_ids,
                "coverage_case_id": numeric_case_id,
            })
    case_by_id = {case["case_id"]: case for case in cases}
    for case in cases:
        numeric_case_id = case.get("coverage_case_id")
        if numeric_case_id is not None:
            prior_target = case_aliases.get(numeric_case_id)
            _require(prior_target in (None, case["case_id"]), errors, "coverage_case_ids",
                     f"numeric coverage case ID {numeric_case_id} maps to conflicting cases")
            case_aliases[numeric_case_id] = case["case_id"]
    for numeric_case_id, case_id in case_aliases.items():
        _require(case_id in case_by_id, errors, "coverage_case_ids",
                 f"numeric coverage case ID {numeric_case_id} targets unknown case {case_id!r}")
        if case_id in case_by_id:
            case_by_id[case_id]["coverage_case_id"] = numeric_case_id
    expected_groups = {
        (case["route_id"], scene, phase)
        for case in cases
        for scene, phase in case["expected_contexts"]
    }
    if errors:
        raise PipelineRequirementsError(errors)
    return sorted(cases, key=lambda case: case["case_id"]), expected_groups, errors


def _stable_scalar(value: Any) -> str | int | float | bool | None:
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def _phase_value(value: Any, phase_ids: Mapping[str, int]) -> str | int | float | bool | None:
    """Normalize readable manifest aliases to the native numeric phase enum."""

    if isinstance(value, str) and value in phase_ids:
        return phase_ids[value]
    return _stable_scalar(value)


def _scene_value(value: Any, scene_ids: Mapping[str, int]) -> str | int | float | bool | None:
    """Normalize readable manifest scene names to the native scene enum."""

    if isinstance(value, str) and value in scene_ids:
        return scene_ids[value]
    return _stable_scalar(value)


def _capture_provenance(capture: Mapping[str, Any], binding: Mapping[str, Any], errors: list[dict[str, Any]],
                        default_capture_id: str | None = None) -> tuple[str, str | None]:
    _require(capture.get("schema") == CAPTURE_SCHEMA, errors, "capture_schema", "unsupported pipeline-use capture schema")
    _require(capture.get("version") == VERSION, errors, "capture_version", "unsupported pipeline-use capture version")
    capture_id = capture["capture_id"] if "capture_id" in capture else default_capture_id
    _require_safe_identifier(capture_id, errors, "capture_id", "capture_id")
    provenance = capture.get("provenance")
    # The recorder chunk intentionally carries only runtime identities.  The
    # external binding metadata is joined by this generator; a provenance
    # object is accepted when a capture wrapper has included one.
    if provenance is None:
        return str(capture_id), None
    _require(isinstance(provenance, Mapping), errors, "capture_provenance", "capture provenance binding is not an object")
    if not isinstance(provenance, Mapping):
        return str(capture_id), None
    fields = ("seed_decoded_sha256", "source_head", "dirty_overlay_sha256", "dependencies",
              "renderer", "registry_sha256", "coverage_manifest_sha256")
    for field in fields:
        actual = provenance.get(field)
        expected = binding.get(field)
        _require(actual == expected, errors, "capture_binding_mismatch",
                 f"capture provenance field {field} differs from bound metadata", field=field)
    declared_digest = provenance.get("binding_sha256")
    if declared_digest is not None:
        expected_digest = sha256_json({key: binding[key] for key in fields})
        _require(declared_digest == expected_digest, errors, "binding_digest_mismatch",
                 "capture provenance binding digest differs from metadata")
    return capture_id, provenance.get("input_manifest_sha256")


def _validate_status(capture: Mapping[str, Any], records: list[Any], errors: list[dict[str, Any]]) -> None:
    status = capture.get("status")
    _require(isinstance(status, Mapping), errors, "capture_status", "capture status is required")
    if not isinstance(status, Mapping):
        return
    _require(status.get("valid") is True, errors, "capture_invalid", "recorder marked capture invalid")
    if "final" in status:
        _require(status.get("final") is True and status.get("capture_active") is not True,
                 errors, "capture_invalid", "capture snapshot is not a final inactive recorder state")
    if "capture_generation" in status and "capture_generation" in capture:
        _require(status.get("capture_generation") == capture.get("capture_generation"), errors,
                 "stale_generation", "capture status generation differs from capture header")
    dropped = status.get("dropped")
    _require("dropped" in status, errors, "capture_status", "capture status has no dropped count")
    _require(isinstance(dropped, int) and dropped == 0, errors, "capture_dropped", "capture reports dropped records")
    if "overflow" in status:
        _require(status.get("overflow") is False, errors, "capture_overflow", "capture recorder overflowed")
    _require("errors" in status and not status.get("errors"), errors, "capture_errors", "capture reports recorder errors")
    open_scopes = status.get("open_scopes")
    _require("open_scopes" in status and open_scopes in (0, []), errors, "capture_open_scopes", "capture reports open scopes")
    for field in ("total_records", "drained_records"):
        if field in status:
            _require(status[field] == len(records), errors, "capture_count", f"capture {field} disagrees with records")


def _record_descriptor(record: Mapping[str, Any], errors: list[dict[str, Any]], where: str) -> dict[str, Any] | None:
    value = record.get("descriptor", record.get("pipeline"))
    if value is not None:
        return _descriptor_from(value, errors, where)
    # The native recorder writes event fields directly.  Its descriptor
    # dictionary is keyed by SHA-256, so size is joined by the caller.
    required = ("type", "pipeline_ref", "config_version", "descriptor_sha256")
    if not all(key in record for key in required):
        errors.append(_issue("descriptor_shape", f"{where} descriptor is missing"))
        return None
    try:
        return {
            "type": int(record["type"]),
            "ref_hex": _ref_hex(record["pipeline_ref"]),
            "config_version": int(record["config_version"]),
            "size": int(record.get("bytes", record.get("size", 0))),
            "sha256": str(record["descriptor_sha256"]).lower(),
        }
    except (TypeError, ValueError) as exc:
        errors.append(_issue("descriptor_shape", f"{where} descriptor has invalid event fields: {exc}"))
        return None


def _record_context(record: Mapping[str, Any]) -> Mapping[str, Any] | None:
    for key in ("scope", "context", "source_context"):
        value = record.get(key)
        if isinstance(value, Mapping):
            return value
    return None


def _context_value(context: Mapping[str, Any], record: Mapping[str, Any], *keys: str) -> Any:
    for key in keys:
        if key in context:
            return context[key]
        if key in record:
            return record[key]
    return None


def _validate_menu_identity_route(route: Mapping[str, Any], errors: list[dict[str, Any]],
                                  where: str) -> None:
    """Validate finite transient CSS/SSS identity tuples in a route manifest."""

    fixed_fields = [key for _label, aliases in MENU_FIXED_FIELD_GROUPS for key in aliases
                    if key in route]
    for label, aliases in MENU_FIXED_FIELD_GROUPS:
        present = [key for key in aliases if key in route]
        _require(len(present) <= 1, errors, "coverage_menu_identity",
                 f"{where} has ambiguous {label} fields")

    if "menu_identities" not in route:
        return
    _require(not fixed_fields, errors, "coverage_menu_identity",
             f"{where} cannot combine menu_identities with fixed menu fields")
    identities = route.get("menu_identities")
    _require(isinstance(identities, list) and bool(identities), errors, "coverage_menu_identity",
             f"{where} menu_identities must be a non-empty list")
    if not isinstance(identities, list):
        return

    seen: set[tuple[tuple[int, ...], tuple[int, ...], int, int]] = set()
    for index, identity in enumerate(identities):
        identity_where = f"{where} menu_identities[{index}]"
        _require(isinstance(identity, Mapping), errors, "coverage_menu_identity",
                 f"{identity_where} must be an object")
        if not isinstance(identity, Mapping):
            continue
        _require(set(identity) == MENU_IDENTITY_FIELDS, errors, "coverage_menu_identity",
                 f"{identity_where} must contain exactly fighter/costume/stage/ground numeric fields")
        fighters = identity.get("fighter_numeric_ids")
        costumes = identity.get("costume_numeric_ids")
        stage = identity.get("stage_numeric_id")
        ground = identity.get("ground_numeric_id")
        _require(isinstance(fighters, list) and bool(fighters), errors, "coverage_menu_identity",
                 f"{identity_where} fighter_numeric_ids must be a non-empty list")
        _require(isinstance(costumes, list) and bool(costumes), errors, "coverage_menu_identity",
                 f"{identity_where} costume_numeric_ids must be a non-empty list")
        if isinstance(fighters, list) and isinstance(costumes, list):
            _require(len(fighters) == len(costumes), errors, "coverage_menu_identity",
                     f"{identity_where} fighter and costume lists must have equal lengths")
        for label, values in (("fighter_numeric_ids", fighters), ("costume_numeric_ids", costumes)):
            if isinstance(values, list):
                for value in values:
                    _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                             errors, "coverage_menu_identity", f"{identity_where} {label} contains a non-numeric ID")
        for label, value in (("stage_numeric_id", stage), ("ground_numeric_id", ground)):
            _require(isinstance(value, int) and not isinstance(value, bool) and value >= 0,
                     errors, "coverage_menu_identity", f"{identity_where} {label} must be a non-negative integer")
        if (isinstance(fighters, list) and isinstance(costumes, list) and len(fighters) == len(costumes) and
                all(isinstance(value, int) and not isinstance(value, bool) and value >= 0 for value in fighters) and
                all(isinstance(value, int) and not isinstance(value, bool) and value >= 0 for value in costumes) and
                isinstance(stage, int) and not isinstance(stage, bool) and stage >= 0 and
                isinstance(ground, int) and not isinstance(ground, bool) and ground >= 0):
            key = (tuple(fighters), tuple(costumes), stage, ground)
            _require(key not in seen, errors, "coverage_menu_identity",
                     f"{identity_where} duplicates an earlier menu identity")
            seen.add(key)


def _observed_menu_identity(context: Mapping[str, Any], record: Mapping[str, Any]) -> tuple[Any, ...] | None:
    """Extract one complete transient menu tuple from a recorder context."""

    players = _active_players(context, record)
    fighters: list[Any] = []
    for player in players:
        if isinstance(player, Mapping):
            value = player.get("character")
            if value is None and "fighter_kind" in player:
                value = player.get("fighter_kind")
            fighters.append(value)
        else:
            fighters.append(player)
    costumes = _context_value(context, record, "costumes", "costume_ids")
    if not isinstance(costumes, list):
        costumes = [player.get("costume") for player in players
                    if isinstance(player, Mapping) and "costume" in player]
    stage = _context_value(context, record, "stage_numeric_id", "stage_id", "stage")
    ground = _context_value(context, record, "ground_numeric_id", "ground_id", "ground")
    if (not fighters or not isinstance(costumes, list) or not costumes or len(fighters) != len(costumes) or
            stage is None or ground is None):
        return None
    return (tuple(_stable_scalar(value) for value in fighters),
            tuple(_stable_scalar(value) for value in costumes),
            _stable_scalar(stage), _stable_scalar(ground))


def _menu_identity_observation_missing(case: Mapping[str, Any], scene: Any,
                                       context: Mapping[str, Any], record: Mapping[str, Any]) -> bool:
    route = case.get("route_identity")
    return (scene in (SOURCE_SCENE_IDS["css"], SOURCE_SCENE_IDS["sss"]) and
            isinstance(route, Mapping) and "menu_identities" in route and
            _observed_menu_identity(context, record) is None)


def _validate_route_content(case: Mapping[str, Any], context: Mapping[str, Any],
                            record: Mapping[str, Any], errors: list[dict[str, Any]], index: int) -> None:
    """Compare source content identities when both sides carry the field."""

    route = case.get("route_identity")
    if not isinstance(route, Mapping):
        return
    # Route labels identify the case; these optional checks bind the selected
    # source stage/ground and player registry rows when the recorder supplied
    # them.  Missing optional detail remains covered by the manifest digest,
    # while contradictory detail is never silently accepted.
    observed_scene = _scene_value(_context_value(context, record, "scene"), SOURCE_SCENE_IDS)
    menu_scene = observed_scene in (SOURCE_SCENE_IDS["css"], SOURCE_SCENE_IDS["sss"])
    if menu_scene and "menu_identities" in route:
        observed = _observed_menu_identity(context, record)
        if observed is not None:
            expected = {
                (tuple(identity["fighter_numeric_ids"]), tuple(identity["costume_numeric_ids"]),
                 identity["stage_numeric_id"], identity["ground_numeric_id"])
                for identity in route["menu_identities"]
            }
            _require(observed in expected, errors, "content_mismatch",
                     f"record {index} menu selection is not one of the route's finite identities")
        return
    route_fields = (
        (("menu_stage_numeric_id", "menu_stage_id", "menu_stage"),
         ("stage_numeric_id", "stage_id", "stage")) if menu_scene else
        (("stage_numeric_id", "stage_id", "stage"), ("stage_numeric_id", "stage_id", "stage")),
        (("menu_ground_numeric_id", "menu_ground_id", "menu_ground"),
         ("ground_numeric_id", "ground_id", "ground")) if menu_scene else
        (("ground_numeric_id", "ground_id", "ground"), ("ground_numeric_id", "ground_id", "ground")),
    )
    for expected_keys, observed_keys in route_fields:
        expected = next((route[key] for key in expected_keys if key in route), None)
        observed = _context_value(context, record, *observed_keys)
        if expected is not None and observed is not None:
            _require(_stable_scalar(observed) == _stable_scalar(expected), errors, "content_mismatch",
                     f"record {index} source content differs from case route")
    if menu_scene:
        expected_fighters = next((route[key] for key in
                                  ("menu_fighter_numeric_ids", "menu_fighter_ids", "menu_fighters")
                                  if key in route), None)
        expected_costumes = next((route[key] for key in
                                  ("menu_costume_numeric_ids", "menu_costume_ids", "menu_costumes")
                                  if key in route), None)
    else:
        expected_fighters = route.get("fighter_numeric_ids", route.get("fighters"))
        expected_costumes = next((route[key] for key in
                                  ("costume_numeric_ids", "costume_ids", "costumes")
                                  if key in route), None)
    observed_players = _context_value(context, record, "players")
    if isinstance(expected_fighters, list) and isinstance(observed_players, list):
        active_count = _context_value(context, record, "active_player_count")
        if isinstance(active_count, int) and not isinstance(active_count, bool):
            observed_players = observed_players[:max(0, min(active_count, len(observed_players)))]
        observed_fighters = []
        for player in observed_players:
            if isinstance(player, Mapping):
                observed_fighters.append(player.get("character", player.get("fighter_kind")))
            else:
                observed_fighters.append(player)
        _require([_stable_scalar(x) for x in observed_fighters] ==
                 [_stable_scalar(x) for x in expected_fighters], errors, "content_mismatch",
                 f"record {index} fighter registry identities differ from case route")
    if isinstance(expected_costumes, list):
        observed_costume_values = _context_value(context, record, "costumes", "costume_ids")
        if not isinstance(observed_costume_values, list) and isinstance(observed_players, list):
            observed_costume_values = [player.get("costume") for player in observed_players
                                       if isinstance(player, Mapping) and "costume" in player]
        if isinstance(observed_costume_values, list):
            _require([_stable_scalar(x) for x in observed_costume_values] ==
                     [_stable_scalar(x) for x in expected_costumes], errors, "content_mismatch",
                     f"record {index} costume registry identities differ from case route")


def _menu_identity_missing(case: Mapping[str, Any], scene: Any) -> bool:
    """Return whether a menu scope lacks an explicit transient selection binding."""

    if scene not in (SOURCE_SCENE_IDS["css"], SOURCE_SCENE_IDS["sss"]):
        return False
    route = case.get("route_identity")
    if not isinstance(route, Mapping):
        return False
    if "menu_identities" in route:
        return False
    has_final_fighters = any(key in route for key in ("fighter_numeric_ids", "fighters"))
    has_menu_fighters = any(key in route for key in ("menu_fighter_numeric_ids", "menu_fighter_ids", "menu_fighters"))
    has_final_stage = any(key in route for key in ("stage_numeric_id", "stage_id", "stage"))
    has_menu_stage = any(key in route for key in ("menu_stage_numeric_id", "menu_stage_id", "menu_stage"))
    has_final_ground = any(key in route for key in ("ground_numeric_id", "ground_id", "ground"))
    has_menu_ground = any(key in route for key in ("menu_ground_numeric_id", "menu_ground_id", "menu_ground"))
    has_final_costumes = any(key in route for key in ("costume_numeric_ids", "costume_ids", "costumes"))
    has_menu_costumes = any(key in route for key in
                            ("menu_costume_numeric_ids", "menu_costume_ids", "menu_costumes"))
    return ((has_final_fighters and not has_menu_fighters) or
            (has_final_stage and not has_menu_stage) or
            (has_final_ground and not has_menu_ground) or
            (has_final_costumes and not has_menu_costumes))


def _validate_scope_identity(previous: Mapping[str, Any], current: Mapping[str, Any],
                             record: Mapping[str, Any], errors: list[dict[str, Any]],
                             index: int, scope_id: str, *, check_thread: bool = True) -> None:
    """Require every identity field present at both ends to remain immutable."""

    previous_context = previous.get("context", previous) if isinstance(previous, Mapping) else {}
    if not isinstance(previous_context, Mapping):
        previous_context = {}
    identity_fields = (
        ("case_id", "coverage_case_id"),
        ("route_id", "route", "world_route"),
        ("scene",),
        ("phase",),
        ("world_generation",),
        ("route_epoch",),
        ("source_tick", "tick"),
        ("input_manifest_sha256", "frozen_input_sha256", "input_binding_sha256"),
        ("stage", "stage_id", "stage_numeric_id"),
        ("ground", "ground_id", "ground_numeric_id"),
        ("hud_layout",),
        ("gobj_classifier",),
        ("gx_link",),
        ("render_pass",),
        ("active_player_count",),
        ("players",),
        ("owner_kind",),
        ("owner_id",),
        ("owner_effect_bank",),
        ("owner_explicit_unknown",),
    )
    for keys in identity_fields:
        prior = _context_value(previous_context, {}, *keys)
        observed = _context_value(current, record, *keys)
        if prior is not None and observed is not None:
            _require(_stable_scalar(prior) == _stable_scalar(observed), errors, "scope_mismatch",
                     f"scope {scope_id} changed immutable source context at record {index}")
    if check_thread:
        prior_thread = previous.get("thread_id") if isinstance(previous, Mapping) else None
        current_thread = _context_value(current, record, "thread_id")
        if prior_thread is not None and current_thread is not None:
            _require(prior_thread == current_thread, errors, "scope_pair",
                     f"scope {scope_id} changed thread identity at record {index}")


def _validate_records(capture: Mapping[str, Any], binding: Mapping[str, Any], seed_rows: list[dict[str, Any]],
                      cases: list[dict[str, Any]], expected_groups: set[tuple[str, Any, Any]],
                      errors: list[dict[str, Any]], capture_id: str) -> tuple[list[dict[str, Any]], dict[tuple[str, Any, Any], list[dict[str, Any]]], list[dict[str, Any]]]:
    records = capture.get("records")
    _require(isinstance(records, list), errors, "capture_records", "capture records must be a list")
    if not isinstance(records, list):
        return [], {}, []
    _require(len(records) <= MAX_RECORDS, errors, "capture_records", "capture exceeds recorder record limit")
    begin = capture.get("sequence_begin")
    end = capture.get("sequence_end")
    _require(isinstance(begin, int) and isinstance(end, int) and end >= begin,
             errors, "capture_sequence", "capture sequence bounds are invalid")
    if isinstance(begin, int) and isinstance(end, int) and end >= begin:
        _require(len(records) == end - begin + 1, errors, "sequence_gap", "capture sequence bounds contain missing records")
    generation = capture.get("capture_generation")
    _require(isinstance(generation, int) and generation >= 0, errors, "capture_generation", "capture generation is invalid")
    status = capture.get("status") if isinstance(capture.get("status"), Mapping) else {}
    renderer_generation = capture.get("renderer_generation", status.get("renderer_generation"))
    device_generation = capture.get("device_generation", status.get("device_generation"))
    for label, value in (("renderer_generation", renderer_generation), ("device_generation", device_generation)):
        if value is not None:
            _require(isinstance(value, int) and value >= 0, errors, "capture_generation", f"{label} is invalid")
    seed_map = {(row["type"], row["ref_hex"]): row for row in seed_rows}
    descriptor_map: dict[tuple[int, str], dict[str, Any]] = {}
    descriptor_digest_map: dict[tuple[int, int, str], dict[str, Any]] = {}
    catalog = capture.get("descriptors")
    _require(isinstance(catalog, list), errors, "capture_descriptors", "capture descriptors must be a list")
    if not isinstance(catalog, list):
        catalog = []
    for index, item in enumerate(catalog):
        if not isinstance(item, Mapping):
            errors.append(_issue("descriptor_shape", f"capture descriptor {index} is not an object"))
            continue
        # The C recorder's compact descriptor dictionary intentionally omits
        # the row reference; each event still carries its typed reference.
        has_ref = "ref_hex" in item or "ref" in item
        if has_ref:
            descriptor = _descriptor_from(item, errors, f"capture descriptor {index}")
            if descriptor is None:
                continue
            key = (descriptor["type"], descriptor["ref_hex"])
            prior = descriptor_map.get(key)
            if prior is not None and prior != descriptor:
                errors.append(_issue("conflicting_descriptor", f"descriptor {key} has conflicting hash/config metadata"))
            descriptor_map[key] = descriptor
            expected = seed_map.get(key)
            _require(expected is not None, errors, "unknown_descriptor", f"capture descriptor {key} is absent from seed")
            if expected is not None:
                for field in ("config_version", "size", "sha256"):
                    _require(descriptor[field] == expected[field], errors, "descriptor_mismatch",
                             f"descriptor {key} {field} differs from seed")
        else:
            try:
                descriptor = {
                    "type": int(item["type"]),
                    "ref_hex": "",
                    "config_version": int(item["config_version"]),
                    "size": int(item["size"] if "size" in item else item["bytes"]),
                    "sha256": str(item["sha256"]).lower(),
                }
            except (KeyError, TypeError, ValueError) as exc:
                errors.append(_issue("descriptor_shape", f"capture descriptor {index} has invalid fields: {exc}"))
                continue
            _require(SHA256_RE.fullmatch(descriptor["sha256"]) is not None, errors,
                     "descriptor_digest", f"capture descriptor {index} digest is not SHA-256")
            _require(any(row["type"] == descriptor["type"] and
                         row["config_version"] == descriptor["config_version"] and
                         row["size"] == descriptor["size"] and
                         row["sha256"] == descriptor["sha256"] for row in seed_rows),
                     errors, "unknown_descriptor", f"capture descriptor digest {descriptor['sha256']} is absent from seed")
            digest_key = (descriptor["type"], descriptor["config_version"], descriptor["sha256"])
            prior = descriptor_digest_map.get(digest_key)
            if prior is not None and prior["size"] != descriptor["size"]:
                errors.append(_issue("conflicting_descriptor", f"descriptor digest {descriptor['sha256']} has conflicting size"))
            descriptor_digest_map[digest_key] = descriptor

    case_map = {case["case_id"]: case for case in cases}
    for case in cases:
        numeric_case_id = case.get("coverage_case_id")
        if numeric_case_id is not None:
            alias = str(numeric_case_id)
            _require(alias not in case_map or case_map[alias] is case, errors, "coverage_case_ids",
                     f"numeric coverage case ID {numeric_case_id} collides with a case ID")
            case_map[alias] = case
    record_rows: list[dict[str, Any]] = []
    groups: dict[tuple[str, Any, Any], list[dict[str, Any]]] = defaultdict(list)
    scope_evidence: list[dict[str, Any]] = []
    # Scope content cannot be classified at scope_end: a deferred worker may
    # publish its demand after the producer closes the scope.  Retain paired
    # boundaries until all demand rows have been joined, then apply route
    # identity checks only to demanded scopes.  Empty BOOT/PREPARATION
    # boundaries are setup evidence and must not be treated as source content.
    pending_scope_evidence: list[dict[str, Any]] = []
    active_scopes: dict[str, Mapping[str, Any]] = {}
    closed_scopes: dict[str, Mapping[str, Any]] = {}
    seen_sequences: set[int] = set()
    prior_sequence: int | None = None
    allowed_kinds = {
        "import", "use", "draw_use", "merge_use", "demand", "scope_begin", "scope_end",
        # Exact names emitted by the private C recorder.  All finder outcomes
        # are demand edges; only import is intentionally excluded below.
        "last_ref", "ready", "pending", "create", "draw_cache_reuse", "merge", "packet_use",
    }
    for index, item in enumerate(records):
        if not isinstance(item, Mapping):
            errors.append(_issue("record_shape", f"record {index} is not an object"))
            continue
        sequence = item.get("sequence")
        _require(isinstance(sequence, int), errors, "record_sequence", f"record {index} has no integer sequence")
        if isinstance(sequence, int):
            _require(sequence not in seen_sequences, errors, "sequence_duplicate", f"record sequence {sequence} is duplicated")
            seen_sequences.add(sequence)
            if prior_sequence is not None:
                _require(sequence == prior_sequence + 1, errors, "sequence_gap", f"record sequence gap before {sequence}")
            prior_sequence = sequence
            if isinstance(begin, int):
                _require(sequence == begin + index, errors, "sequence_order", "capture records are not in canonical event order")
        kind = item.get("kind")
        _require(kind in allowed_kinds, errors, "record_kind", f"record {index} has unknown kind {kind!r}")
        if kind not in allowed_kinds:
            continue
        record_generation = item.get("capture_generation", item.get("generation"))
        if record_generation is not None:
            _require(record_generation == generation, errors, "stale_generation", f"record {index} has stale capture generation")
        for label, expected_generation in (("renderer_generation", renderer_generation),
                                           ("device_generation", device_generation)):
            if label in item:
                import_zero = kind == "import" and item[label] == 0
                scope_zero = kind in {"scope_begin", "scope_end"} and item[label] == 0
                native_demand = kind not in {"import", "scope_begin", "scope_end"} and "pipeline_ref" in item
                _require(import_zero or scope_zero or (expected_generation is not None and item[label] == expected_generation), errors,
                         "stale_generation", f"record {index} has stale {label}")
                if native_demand:
                    _require(isinstance(item[label], int) and item[label] > 0, errors, "stale_generation",
                             f"record {index} has no live {label}")
            elif kind not in {"import", "scope_begin", "scope_end"} and "pipeline_ref" in item:
                errors.append(_issue("stale_generation", f"record {index} native demand has no {label}"))

        context = _record_context(item)
        scope_id = _context_value(context or {}, item, "scope_id", "id")
        if isinstance(scope_id, int) and not isinstance(scope_id, bool):
            scope_id = str(scope_id)
        if isinstance(scope_id, str) and not _require_safe_identifier(scope_id, errors, "scope_pair", "scope_id"):
            # Keep the invalid value out of subsequent diagnostics and output.
            scope_id = None
        thread_id = _context_value(context or {}, item, "thread_id")
        if isinstance(thread_id, str) and not _require_safe_identifier(thread_id, errors, "thread_id", "thread_id"):
            thread_id = None
        if kind in {"scope_begin", "scope_end"}:
            for label, keys, code in (
                ("case_id", ("case_id",), "case_id"),
                ("route_id", ("route_id", "route", "world_route"), "route_id"),
                ("phase", ("phase",), "phase_id"),
                ("scene", ("scene",), "scene_id"),
            ):
                value = _context_value(context or {}, item, *keys)
                if isinstance(value, str):
                    _require_safe_identifier(value, errors, code, label)
        # Scope IDs are stable numeric identities emitted by the native
        # recorder (or explicit private strings).  A missing ID cannot be
        # reconstructed safely, especially when deferred work runs later.
        if kind == "scope_end" and scope_id is None and active_scopes:
            errors.append(_issue("scope_pair", f"record {index} scope_end has no scope_id"))
        if kind == "scope_begin":
            _require(isinstance(scope_id, str) and bool(scope_id), errors, "scope_begin", "scope_begin requires scope_id")
            _require(context is not None, errors, "missing_scope", f"scope_begin record {index} has no source context")
            if isinstance(scope_id, str):
                _require(scope_id not in active_scopes and scope_id not in closed_scopes, errors,
                         "scope_pair", f"scope {scope_id} began twice")
                active_scopes[scope_id] = {"context": context or {}, "thread_id": thread_id}
            continue
        if kind == "scope_end":
            _require(isinstance(scope_id, str) and scope_id in active_scopes, errors, "scope_pair", f"scope {scope_id!r} ended without begin")
            _require(context is not None, errors, "missing_scope", f"scope_end record {index} has no source context")
            if isinstance(scope_id, str):
                prior_scope = active_scopes.pop(scope_id, None)
                if prior_scope is not None and context is not None:
                    _validate_scope_identity(prior_scope, context, item, errors, index, scope_id)
                    closed_scopes[scope_id] = prior_scope
                    prior_context = prior_scope.get("context", {})
                    prior_case_value = _context_value(prior_context, {}, "case_id", "coverage_case_id")
                    prior_case_id = (str(prior_case_value)
                                     if isinstance(prior_case_value, (str, int)) and not isinstance(prior_case_value, bool)
                                     else prior_case_value)
                    if isinstance(prior_case_id, str) and prior_case_id in case_map:
                        prior_case = case_map[prior_case_id]
                        prior_route = _context_value(prior_context, {}, "route_id", "route", "world_route")
                        prior_route = (str(prior_route)
                                       if isinstance(prior_route, (str, int)) and not isinstance(prior_route, bool)
                                       else prior_route)
                        if prior_route is None:
                            prior_route = prior_case["route_id"]
                        prior_phase = _phase_value(_context_value(prior_context, {}, "phase"), prior_case["phase_ids"])
                        prior_scene = _scene_value(_context_value(prior_context, {}, "scene"), prior_case["scene_ids"])
                        prior_group = (prior_route, prior_scene, prior_phase)
                        _require(prior_route == prior_case["route_id"], errors, "scope_mismatch",
                                 f"scope {scope_id} route differs from case manifest")
                        _require(prior_group in expected_groups, errors, "unknown_group",
                                 f"scope {scope_id} route/scene/phase is not in coverage manifest")
                        observed_scope_input = _context_value(prior_context, {},
                                                              "input_manifest_sha256", "frozen_input_sha256",
                                                              "input_binding_sha256")
                        _require(isinstance(observed_scope_input, str) and
                                 observed_scope_input == prior_case["input_sha256"], errors,
                                 "input_mismatch", f"scope {scope_id} input digest differs from manifest")
                        if (isinstance(prior_route, str) and isinstance(prior_phase, (str, int)) and
                                not isinstance(prior_phase, bool) and prior_group in expected_groups):
                            # Do not validate route content yet.  A scope can
                            # have no demand before its end and still receive
                            # a deferred demand afterward.  The final pass
                            # below distinguishes that case from a genuinely
                            # empty setup boundary.
                            pending_scope_evidence.append({
                                "scope_id": scope_id,
                                "case": prior_case,
                                "route_id": prior_route,
                                "scene": prior_scene,
                                "phase": prior_phase,
                                "begin_context": prior_context,
                                "end_context": context,
                                "end_record": item,
                                "index": index,
                            })
            continue

        if kind == "import":
            # Import runs are intentionally outside source scopes and the C
            # recorder pins them to the BOOT/PREPARATION context with no frame,
            # packet, device or renderer lifetime.  Accept omitted fields from
            # a small wrapper, but reject any contradictory native fields.
            for key in ("frame_id", "packet_id", "renderer_generation", "device_generation"):
                if key in item:
                    _require(item[key] == 0, errors, "import_context", f"import record {index} has nonzero {key}")
            import_scope_id = item.get("scope_id")
            _require(import_scope_id in (None, 0), errors, "import_context",
                     f"import record {index} is attached to a source scope")
            _require(context is not None, errors, "import_context", f"import record {index} has no boot context")
            if context is not None:
                _require(_context_value(context, item, "scene") == 1 and
                         _context_value(context, item, "phase") == 1, errors,
                         "import_context", f"import record {index} is not BOOT/PREPARATION context")

        descriptor = _record_descriptor(item, errors, f"record {index}")
        if descriptor is None:
            continue
        digest_key = (descriptor["type"], descriptor["config_version"], descriptor["sha256"])
        compact = descriptor_digest_map.get(digest_key)
        if compact is not None and descriptor["size"] == 0:
            descriptor = dict(descriptor, size=compact["size"])
        key = (descriptor["type"], descriptor["ref_hex"])
        expected = seed_map.get(key)
        _require(expected is not None, errors, "unknown_descriptor", f"record {index} references unknown descriptor {key}")
        if expected is not None:
            for field in ("config_version", "size", "sha256"):
                _require(descriptor[field] == expected[field], errors, "descriptor_mismatch",
                         f"record {index} descriptor {key} {field} differs from seed")
        top = descriptor_map.get(key)
        if top is None and compact is not None:
            top = compact
        _require(top is not None, errors, "descriptor_missing", f"record {index} descriptor {key} is missing from capture catalog")
        if top is not None:
            for field in ("type", "config_version", "size", "sha256"):
                _require(top[field] == descriptor[field], errors, "conflicting_descriptor",
                         f"record {index} conflicts with capture descriptor catalog")
        if kind == "import":
            # Imports are useful for catalog consistency but never create a
            # source requirement group.
            continue

        _require(context is not None, errors, "missing_scope", f"record {index} has no source scope context")
        if context is None:
            continue
        case_value = _context_value(context, item, "case_id", "coverage_case_id")
        case_id = str(case_value) if isinstance(case_value, (str, int)) and not isinstance(case_value, bool) else case_value
        if isinstance(case_id, str) and not _require_safe_identifier(case_id, errors, "case_id", "case_id"):
            case_id = None
        route_value = _context_value(context, item, "route_id", "route", "world_route")
        route_id = str(route_value) if isinstance(route_value, (str, int)) and not isinstance(route_value, bool) else route_value
        route_invalid = isinstance(route_id, str) and not _require_safe_identifier(route_id, errors, "route_id", "route_id")
        if route_invalid:
            route_id = None
        _require(isinstance(case_id, str) and case_id in case_map, errors, "unknown_case", f"record {index} has unknown case {case_id!r}")
        # A source owner may carry no separate route label; the bound case is
        # then the route authority.  This is safe because the case itself is
        # checked against the frozen input manifest below.
        if route_id is None and not route_invalid and isinstance(case_id, str) and case_id in case_map:
            route_id = case_map[case_id]["route_id"]
        raw_phase = _context_value(context, item, "phase")
        phase = _phase_value(raw_phase, case_map[case_id]["phase_ids"]) if isinstance(case_id, str) and case_id in case_map else raw_phase
        raw_scene = _context_value(context, item, "scene")
        scene = _scene_value(raw_scene, case_map[case_id]["scene_ids"]) if isinstance(case_id, str) and case_id in case_map else raw_scene
        if isinstance(raw_phase, int) and not isinstance(raw_phase, bool) and raw_phase not in SOURCE_PHASE_IDS.values():
            errors.append(_issue("phase_id", f"record {index} has an unknown native phase"))
            phase = None
        if isinstance(raw_scene, int) and not isinstance(raw_scene, bool) and raw_scene not in SOURCE_SCENE_IDS.values():
            errors.append(_issue("scene_id", f"record {index} has an unknown native scene"))
            scene = None
        if isinstance(raw_phase, int) and not isinstance(raw_phase, bool) and raw_scene is None:
            # Native recorder contexts are a scene/phase pair.  A numeric
            # phase without its scene could merge CSS, SSS and MATCH uses.
            errors.append(_issue("scene_binding", f"record {index} numeric phase has no source scene"))
        if isinstance(phase, str) and not _require_safe_identifier(phase, errors, "phase_id", "phase"):
            phase = None
        if isinstance(scene, str) and not _require_safe_identifier(scene, errors, "scene_id", "scene"):
            scene = None
        _require(isinstance(route_id, str) and isinstance(phase, (str, int)) and not isinstance(phase, bool) and
                 (scene is None or isinstance(scene, (str, int))) and not isinstance(scene, bool), errors,
                 "scope_context", f"record {index} has incomplete route/phase context")
        if not isinstance(case_id, str) or case_id not in case_map or not isinstance(route_id, str) or not isinstance(phase, (str, int)) or isinstance(phase, bool) or (scene is not None and (not isinstance(scene, (str, int)) or isinstance(scene, bool))):
            continue
        case = case_map[case_id]
        canonical_case_id = case["case_id"]
        expected_contexts = set(case["expected_contexts"])
        if scene is not None and (None, phase) in expected_contexts:
            errors.append(_issue("scene_binding", f"record {index} has a numeric scene without a bound manifest scene"))
        group_key = (route_id, scene, phase)
        _require(route_id == case["route_id"], errors, "scope_mismatch", f"record {index} route differs from case manifest")
        _require(group_key in expected_groups, errors, "unknown_group", f"record {index} route/scene/phase is not in coverage manifest")
        if route_id != case["route_id"] or group_key not in expected_groups:
            continue
        # This branch handles demand edges only (scope boundaries continued
        # above), so any world-less source demand is unbound, including a
        # PREPARATION demand.  Empty PREPARATION scopes remain valid setup
        # evidence through the separate scope-evidence path.
        unbound_world = (scene in SOURCE_WORLD_SCENES and
                         _context_value(context, item, "world_generation") == 0)
        finite_menu_binding = (scene in (SOURCE_SCENE_IDS["css"], SOURCE_SCENE_IDS["sss"]) and
                               isinstance(case.get("route_identity"), Mapping) and
                               "menu_identities" in case["route_identity"])
        if not unbound_world or finite_menu_binding:
            _validate_route_content(case, context, item, errors, index)
        scope_key = scope_id
        _require(isinstance(scope_key, str) and (scope_key in active_scopes or scope_key in closed_scopes), errors,
                 "scope_pair", f"record {index} uses an unknown source scope")
        if isinstance(scope_key, str):
            source_scope = active_scopes.get(scope_key, closed_scopes.get(scope_key))
            if source_scope is not None:
                _validate_scope_identity(source_scope, context, item, errors, index, scope_key,
                                         check_thread=False)
        observed_input = _context_value(context, item, "input_manifest_sha256", "frozen_input_sha256", "input_binding_sha256")
        _require(isinstance(observed_input, str) and observed_input == case["input_sha256"], errors,
                 "input_mismatch", f"record {index} input digest differs from manifest")
        source_tick = _context_value(context, item, "source_tick", "tick")
        packet = _context_value(context, item, "render_packet_id", "packet_id")
        row = {
            "descriptor": descriptor,
            "sequence": sequence,
            "capture_id": capture_id,
            "case_id": canonical_case_id,
            "route_id": route_id,
            "scene": scene,
            "phase": phase,
            "scope_id": scope_id,
            "source_tick": source_tick if isinstance(source_tick, int) else None,
            "render_packet_id": packet if isinstance(packet, int) else None,
            "action": _observed_action(context, item),
            "observed_actions": _observed_actions(context, item),
            "motion_ids": _observed_motion_ids(context, item),
            "costumes": _observed_costumes(context, item),
            "lifecycle": _observed_lifecycle(context, item),
            "thread_id": thread_id,
            "unbound_world": unbound_world,
            "menu_identity_missing": (_menu_identity_missing(case, scene) or
                                       _menu_identity_observation_missing(case, scene, context, item)),
        }
        record_rows.append(row)
        groups[group_key].append(row)
    demanded_scope_ids = {row["scope_id"] for row in record_rows
                          if isinstance(row.get("scope_id"), str)}
    for pending in pending_scope_evidence:
        scope_id = pending["scope_id"]
        case = pending["case"]
        scene = pending["scene"]
        phase = pending["phase"]
        begin_context = pending["begin_context"]
        end_context = pending["end_context"]
        has_demand = scope_id in demanded_scope_ids
        setup_only = (not has_demand and
                      (scene == SOURCE_SCENE_IDS["boot"] or
                       phase == SOURCE_PHASE_IDS["preparation"]))
        if not setup_only:
            # Both boundary snapshots remain strict whenever the scope has a
            # demand, including one joined after scope_end.  Empty non-
            # preparation scopes also stay strict and are marked incomplete
            # below when they have no live source world.
            _validate_route_content(case, begin_context, {}, errors, pending["index"])
            _validate_route_content(case, end_context, pending["end_record"], errors, pending["index"])
        if setup_only:
            # Setup boundaries carry no source action or menu identity.  Do
            # not let default player/selection fields in a BOOT/PREPARATION
            # context satisfy route content requirements accidentally.
            observed_actions = tuple()
            observed_action = None
            observed_costumes = tuple()
            menu_identity_missing = False
        else:
            observed_actions = _observed_actions(begin_context, {})
            observed_action = _observed_action(begin_context, {})
            observed_costumes = _observed_costumes(begin_context, {})
            menu_identity_missing = (_menu_identity_missing(case, scene) or
                                     (_menu_identity_observation_missing(case, scene,
                                                                         begin_context, {}) and
                                      _menu_identity_observation_missing(case, scene,
                                                                          end_context, pending["end_record"])))
        scope_evidence.append({
            "case_id": case["case_id"],
            "route_id": pending["route_id"],
            "scene": scene,
            "phase": phase,
            "observed_actions": observed_actions,
            "action": observed_action,
            "costumes": observed_costumes,
            "lifecycle": _observed_lifecycle(begin_context, {}),
            # A paired empty preparation scope is boundary evidence only.
            # Non-preparation source lifecycle scopes are content evidence and
            # require a live world.
            "unbound_world": (scene in SOURCE_WORLD_SCENES and
                               phase != SOURCE_PHASE_IDS["preparation"] and
                               _context_value(begin_context, {}, "world_generation") == 0),
            "menu_identity_missing": menu_identity_missing,
        })
    _require(not active_scopes, errors, "scope_pair", "capture ended with unpaired scopes", scopes=sorted(active_scopes))
    return record_rows, groups, scope_evidence


def _active_players(context: Mapping[str, Any], record: Mapping[str, Any]) -> list[Any]:
    players = _context_value(context, record, "players")
    if not isinstance(players, list):
        return []
    active_count = _context_value(context, record, "active_player_count")
    if isinstance(active_count, int) and not isinstance(active_count, bool):
        return players[:max(0, min(active_count, len(players)))]
    return players


def _observed_motion_ids(context: Mapping[str, Any], record: Mapping[str, Any]) -> tuple[Any, ...]:
    direct = _context_value(context, record, "motion_ids")
    if isinstance(direct, (list, tuple)):
        return tuple(_stable_scalar(value) for value in direct)
    motions = []
    for player in _active_players(context, record):
        if isinstance(player, Mapping) and "motion_id" in player:
            motions.append(_stable_scalar(player["motion_id"]))
    return tuple(motions)


def _observed_actions(context: Mapping[str, Any], record: Mapping[str, Any]) -> tuple[Any, ...]:
    direct = _context_value(context, record, "action", "action_id")
    if direct is not None:
        return (_stable_scalar(direct),)
    return _observed_motion_ids(context, record)


def _observed_action(context: Mapping[str, Any], record: Mapping[str, Any]) -> Any:
    actions = _observed_actions(context, record)
    return actions[0] if actions else None


def _observed_lifecycle(context: Mapping[str, Any], record: Mapping[str, Any]) -> Any:
    direct = _context_value(context, record, "lifecycle", "lifecycle_event")
    if direct is not None:
        return _stable_scalar(direct)
    phase = _context_value(context, record, "phase")
    if isinstance(phase, str) and phase in SOURCE_PHASE_IDS:
        return phase
    reverse = {value: name for name, value in SOURCE_PHASE_IDS.items()}
    return reverse.get(phase)


def _observed_costumes(context: Mapping[str, Any], record: Mapping[str, Any]) -> tuple[Any, ...]:
    values = _context_value(context, record, "costumes", "costume_ids")
    if isinstance(values, (list, tuple)):
        return tuple(_stable_scalar(value) for value in values)
    players = _active_players(context, record)
    if players:
        costumes = [player.get("costume") for player in players
                    if isinstance(player, Mapping) and "costume" in player]
        if costumes:
            return tuple(_stable_scalar(value) for value in costumes)
    value = _context_value(context, record, "costume", "costume_id")
    return (_stable_scalar(value),) if value is not None else tuple()


def _coverage_completeness(cases: list[dict[str, Any]], groups: Mapping[tuple[str, Any, Any], list[dict[str, Any]]],
                           scope_evidence: Sequence[Mapping[str, Any]], capture: Mapping[str, Any],
                           provenance_input: str | None) -> list[dict[str, Any]]:
    incomplete: list[dict[str, Any]] = []
    for case in cases:
        rows = [row for values in groups.values() for row in values if row["case_id"] == case["case_id"]]
        evidence = [row for row in scope_evidence if row["case_id"] == case["case_id"]]
        all_evidence = rows + evidence
        observed_contexts = {(row.get("scene"), row["phase"]) for row in all_evidence}
        expected_contexts = set(case["expected_contexts"])
        observed_phases = {row["phase"] for row in all_evidence}
        observed_actions = {action for row in rows for action in row["observed_actions"]}
        observed_costumes = {value for row in all_evidence for value in row["costumes"]}
        observed_lifecycle = {_stable_scalar(row["lifecycle"]) for row in all_evidence if row["lifecycle"] is not None}
        observed_source_ticks = {row["source_tick"] for row in all_evidence if isinstance(row.get("source_tick"), int)}
        missing_phases = sorted(set(case["expected_phases"]) - observed_phases, key=lambda value: (type(value).__name__, str(value)))
        missing_actions = sorted(set(case["expected_actions"]) - observed_actions, key=lambda value: (type(value).__name__, str(value)))
        missing_costumes = sorted(set(case["expected_costumes"]) - observed_costumes, key=lambda value: str(value))
        missing_lifecycle = sorted(set(case["lifecycle"]) - observed_lifecycle, key=lambda value: (type(value).__name__, str(value)))
        missing_source_ticks = sorted(set(case["expected_source_ticks"]) - observed_source_ticks)
        missing_contexts = sorted(expected_contexts - observed_contexts,
                                  key=lambda value: (type(value[0]).__name__, str(value[0]),
                                                      type(value[1]).__name__, str(value[1])))
        unbound_scopes = sum(1 for row in all_evidence if row.get("unbound_world"))
        menu_identity_missing = sum(1 for row in all_evidence if row.get("menu_identity_missing"))
        if provenance_input is not None and provenance_input != case["input_sha256"]:
            missing_input = True
        else:
            missing_input = False
        if (missing_phases or missing_actions or missing_costumes or missing_lifecycle or missing_source_ticks or
                missing_input or missing_contexts or unbound_scopes or menu_identity_missing):
            incomplete.append({
                "case_id": case["case_id"],
                "route_id": case["route_id"],
                "missing_phases": missing_phases,
                "missing_actions": missing_actions,
                "missing_costumes": missing_costumes,
                "missing_lifecycle": missing_lifecycle,
                "missing_source_ticks": missing_source_ticks,
                "missing_contexts": [
                    {"scene": scene, "phase": phase} for scene, phase in missing_contexts
                ],
                "unbound_source_scopes": unbound_scopes,
                "menu_identity_missing": menu_identity_missing,
                "input_binding_missing": missing_input,
            })
    return incomplete


def _groups_output(cases: list[dict[str, Any]], groups: Mapping[tuple[str, Any, Any], list[dict[str, Any]]],
                   expected_groups: set[tuple[str, Any, Any]]) -> list[dict[str, Any]]:
    case_for_route: dict[str, list[str]] = defaultdict(list)
    for case in cases:
        case_for_route[case["route_id"]].append(case["case_id"])
    output: list[dict[str, Any]] = []
    for route_id, scene, phase in sorted(expected_groups,
                                         key=lambda value: (value[0], type(value[1]).__name__, str(value[1]),
                                                            type(value[2]).__name__, str(value[2]))):
        members: dict[tuple[int, str], dict[str, Any]] = {}
        provenance_seen: dict[tuple[int, str], set[tuple[Any, ...]]] = defaultdict(set)
        for row in groups.get((route_id, scene, phase), []):
            descriptor = row["descriptor"]
            key = (descriptor["type"], descriptor["ref_hex"])
            member = members.setdefault(key, {
                "type": descriptor["type"],
                "ref_hex": descriptor["ref_hex"],
                "config_version": descriptor["config_version"],
                "size": descriptor["size"],
                "sha256": descriptor["sha256"],
                "provenance": [],
            })
            link = {
                "capture_id": row["capture_id"],
                "sequence": row["sequence"],
                "case_id": row["case_id"],
            }
            if row["scope_id"] is not None:
                link["scope_id"] = row["scope_id"]
            if row["source_tick"] is not None:
                link["source_tick"] = row["source_tick"]
            if row["render_packet_id"] is not None:
                link["render_packet_id"] = row["render_packet_id"]
            if row["thread_id"] is not None:
                link["thread_id"] = row["thread_id"]
            # All link values are validated scalar identities.  Keep the
            # historical dictionary-equality deduplication semantics while
            # avoiding a linear scan of the growing provenance list.
            link_key = tuple(link.get(field) for field in (
                "capture_id", "sequence", "case_id", "scope_id", "source_tick",
                "render_packet_id", "thread_id",
            ))
            if link_key not in provenance_seen[key]:
                provenance_seen[key].add(link_key)
                member["provenance"].append(link)
        for member in members.values():
            member["provenance"].sort(key=lambda link: (link["sequence"], link["case_id"]))
        output_group = {
            "id": f"{route_id}:{phase}" if scene is None else f"{route_id}:{scene}:{phase}",
            "route_id": route_id,
            "cases": sorted(case_for_route.get(route_id, [])),
            "members": sorted(members.values(), key=lambda member: (member["type"], member["ref_hex"])),
            "phase": phase,
        }
        if scene is not None:
            output_group["scene"] = scene
        output.append(output_group)
    return output


def group_union(*groups: Iterable[Mapping[str, Any]]) -> list[dict[str, Any]]:
    """Return the sorted typed-descriptor union of requirement groups.

    This small public helper is used by preparation callers when requesting a
    route's shared phase plus content groups.  It keys rows by typed reference,
    then verifies that repeated membership agrees on config metadata instead of
    allowing a collision to be silently deduplicated.
    """

    # Accept either group_union(group_a, group_b) or group_union([group_a,
    # group_b]) for convenient use by callers and focused tests.
    if len(groups) == 1:
        first = list(groups[0])
        if first and all(isinstance(item, Mapping) and "members" in item for item in first):
            groups = tuple(item.get("members", []) for item in first)
        else:
            groups = (first,)
    merged: dict[tuple[int, str], dict[str, Any]] = {}
    errors: list[dict[str, Any]] = []
    for group in groups:
        for member in group:
            candidate = member.get("descriptor", member) if isinstance(member, Mapping) else None
            descriptor = _descriptor_from(candidate, errors, "group union")
            if descriptor is None:
                continue
            key = (descriptor["type"], descriptor["ref_hex"])
            prior = merged.get(key)
            if prior is not None and prior != descriptor:
                errors.append(_issue("conflicting_descriptor", f"group union row {key} has conflicting metadata"))
            merged[key] = descriptor
    if errors:
        raise PipelineRequirementsError(errors)
    return sorted(merged.values(), key=lambda row: (row["type"], row["ref_hex"]))


def generate(capture: Mapping[str, Any] | str | Path,
             seed: str | Path,
             metadata: Mapping[str, Any] | str | Path,
             coverage: Mapping[str, Any] | str | Path,
             *, source_root: str | Path | None = None) -> dict[str, Any]:
    """Validate inputs and return a deterministic requirement sidecar.

    ``PipelineRequirementsError`` is raised for hard-invalid evidence.  A
    valid capture with missing declared coverage returns an explicit draft
    whose ``status.kind`` is ``incomplete``.
    """

    errors: list[dict[str, Any]] = []
    if isinstance(capture, (str, Path)):
        capture_value, capture_file_digest = _read_capture(Path(capture))
    else:
        # Keep programmatic inputs on the same decode path as file inputs.
        # This matters for tests and callers that pass a native compact chunk
        # directly instead of first serializing it to JSON.
        capture_file_digest = sha256_json(capture)
        capture_value = _capture_json_value(capture)
    metadata_value, _metadata_file_digest = _object_input(metadata, "metadata")
    coverage_value, coverage_digest = _object_input(coverage, "coverage")
    if coverage_value.get("manifest_sha256") is not None:
        declared_manifest = _sha256(coverage_value.get("manifest_sha256"), "coverage manifest_sha256", errors)
        _require(declared_manifest == coverage_digest, errors, "coverage_digest_mismatch",
                 "coverage manifest self-digest differs from supplied bytes")
    decoded_seed, seed_digest, seed_rows = _sqlite_rows(Path(seed))
    del decoded_seed  # The raw seed never crosses the output boundary.
    binding, public_binding, _ = _binding_metadata(metadata_value, coverage_digest, seed_digest,
                                                   Path(source_root) if source_root is not None else None)
    capture_id_hint = capture_value.get("capture_id") if isinstance(capture_value.get("capture_id"), str) else None
    cases, expected_groups, _ = _normalize_cases(coverage_value, capture_id_hint)
    declared_ids = sorted({case["capture_id"] for case in cases})
    default_capture_id = capture_id_hint or (declared_ids[0] if len(declared_ids) == 1 else None)
    capture_id, provenance_input = _capture_provenance(capture_value, binding, errors, default_capture_id)
    records = capture_value.get("records") if isinstance(capture_value.get("records"), list) else []
    _validate_status(capture_value, records, errors)
    rows, groups, scope_evidence = _validate_records(capture_value, binding, seed_rows, cases, expected_groups, errors, capture_id)
    if errors:
        raise PipelineRequirementsError(errors)
    incomplete = _coverage_completeness(cases, groups, scope_evidence, capture_value, provenance_input)
    output_groups = _groups_output(cases, groups, expected_groups)
    all_members = {(member["type"], member["ref_hex"]): member
                   for member in group_union(*(group["members"] for group in output_groups))}
    output = {
        "schema": SCHEMA,
        "version": VERSION,
        "status": {
            "kind": "certificate" if not incomplete else "incomplete",
            "valid": True,
            "certified": not incomplete,
            "errors": [],
            "incomplete_scopes": incomplete,
        },
        "binding": public_binding,
        "capture": {
            "capture_id": capture_id,
            "capture_generation": capture_value["capture_generation"],
            "sequence_begin": capture_value["sequence_begin"],
            "sequence_end": capture_value["sequence_end"],
            "capture_sha256": capture_file_digest,
        },
        "descriptors": [
            {key: row[key] for key in ("type", "ref_hex", "config_version", "size", "sha256")}
            for row in sorted(all_members.values(), key=lambda row: (row["type"], row["ref_hex"]))
        ],
        "groups": output_groups,
        "coverage": {
            "manifest_sha256": coverage_digest,
            "cases": [
                {
                    "case_id": case["case_id"],
                    "route_id": case["route_id"],
                    **({"coverage_case_id": case["coverage_case_id"]}
                       if case.get("coverage_case_id") is not None else {}),
                    "route_identity_sha256": case["route_identity_sha256"],
                    "expected_phases": list(case["expected_phases"]),
                    "expected_contexts": [
                        {"scene": scene, "phase": phase} for scene, phase in case["expected_contexts"]
                    ],
                    "expected_actions": list(case["expected_actions"]),
                    "expected_costumes": list(case["expected_costumes"]),
                    "lifecycle": list(case["lifecycle"]),
                    "expected_source_ticks": list(case["expected_source_ticks"]),
                    "input_sha256": case["input_sha256"],
                    "capture_id": case["capture_id"],
                }
                for case in cases
            ],
        },
    }
    # Ensure accidental input additions (paths, raw traces, addresses) cannot
    # influence output, while retaining deterministic binding and provenance.
    return json.loads(canonical_json(output).decode("utf-8"))


def write_sidecar(output: Mapping[str, Any], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(output) + b"\n")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--seed", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-root", type=Path)
    args = parser.parse_args(argv)
    try:
        result = generate(args.capture, args.seed, args.metadata, args.coverage, source_root=args.source_root)
    except PipelineRequirementsError as exc:
        # A hard rejection is explicit and machine-readable, but is never
        # mistaken for a usable requirement sidecar by a caller.
        rejected = {
            "schema": SCHEMA,
            "version": VERSION,
            "status": {"kind": "hard_reject", "valid": False, "certified": False, "errors": list(exc.errors)},
        }
        write_sidecar(rejected, args.output)
        print(json.dumps(rejected, sort_keys=True), file=sys.stderr)
        return 2
    write_sidecar(result, args.output)
    print(json.dumps(result["status"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
