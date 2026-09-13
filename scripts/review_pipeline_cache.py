#!/usr/bin/env python3
"""Review and, with fresh-origin provenance, merge an Aurora pipeline cache.

The input cache may be a SQLite database whose live rows are still in a
separate WAL.  The database and WAL are copied to a private temporary
directory before SQLite opens them, so review never changes an evidence
export.  A merge always starts with the reviewed seed and only appends new
type-1 rows whose Aurora config version and payload size are the reviewed
ones.  Existing rows, including the type-0 shader row, are copied field for
field from the seed.

This is intentionally independent of the browser, Dawn, and Aurora build
tree.  A merge requires an explicit provenance manifest with
``origin_cache_cleared: true`` and ``dawn_driver_cache_included: false``.
"""

from __future__ import annotations

import argparse
import base64
from contextlib import contextmanager
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
from typing import Any, Iterator


SCHEMA_VERSION = 1
PIPELINE_TYPE = 1
PIPELINE_CONFIG_VERSION = 65549
PIPELINE_CONFIG_SIZE = 2772
DEFAULT_BASE_PIPELINES = 427
PIPELINE_COLUMNS = (
    "type",
    "hash",
    "config_version",
    "config_size",
    "config",
    "first_frame_used",
)
CREATE_SCHEMA = """
CREATE TABLE aurora_schema(value INTEGER);
CREATE TABLE pipeline_cache (
  type INTEGER NOT NULL,
  hash INTEGER NOT NULL,
  config_version INTEGER NOT NULL,
  config_size INTEGER NOT NULL,
  config BLOB NOT NULL,
  first_frame_used INTEGER NOT NULL,
  PRIMARY KEY (type, hash)
);
CREATE INDEX pipeline_cache_load_order_idx
  ON pipeline_cache(type, config_version, first_frame_used);
"""


class CacheError(ValueError):
    """An input cache or provenance manifest is not safe to merge."""


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _read_seed_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw.startswith(b"SQLite format 3\0"):
        return raw
    try:
        if path.name.endswith(".gz.b64"):
            return gzip.decompress(base64.b64decode(b"".join(raw.split()), validate=True))
        if raw.startswith(b"\x1f\x8b"):
            return gzip.decompress(raw)
    except (OSError, ValueError) as exc:
        raise CacheError(f"cannot decode seed {path}: {exc}") from exc
    raise CacheError(f"{path} is neither a SQLite database nor a gzip/base64 seed")


@contextmanager
def _private_sqlite_copy(path: Path, wal: Path | None = None) -> Iterator[Path]:
    """Yield a private SQLite copy, pairing an explicitly supplied WAL."""

    if not path.is_file():
        raise CacheError(f"missing SQLite database: {path}")
    with tempfile.TemporaryDirectory(prefix="pipeline-cache-") as directory:
        private = Path(directory) / "cache.db"
        shutil.copyfile(path, private)
        selected_wal = _select_wal(path, wal)
        if selected_wal is not None:
            if not selected_wal.is_file():
                raise CacheError(f"missing SQLite WAL: {selected_wal}")
            shutil.copyfile(selected_wal, Path(f"{private}-wal"))
        yield private


def _select_wal(path: Path, wal: Path | None) -> Path | None:
    """Resolve an explicit WAL or the live adjacent WAL for ``path``."""

    if wal is not None:
        return wal
    adjacent = Path(f"{path}-wal")
    return adjacent if adjacent.is_file() else None


@contextmanager
def _seed_sqlite_copy(path: Path) -> Iterator[Path]:
    with tempfile.TemporaryDirectory(prefix="pipeline-seed-") as directory:
        private = Path(directory) / "seed.db"
        private.write_bytes(_read_seed_bytes(path))
        yield private


def _schema_objects(connection: sqlite3.Connection) -> list[tuple[str, str, str | None]]:
    return connection.execute(
        "SELECT type, name, sql FROM sqlite_master "
        "WHERE name NOT LIKE 'sqlite_%' ORDER BY type, name"
    ).fetchall()


def _check_database_schema(connection: sqlite3.Connection, label: str) -> None:
    try:
        connection.execute("PRAGMA query_only = ON")
        if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
            raise CacheError(f"{label} failed SQLite integrity_check")
        schema = connection.execute("SELECT value FROM aurora_schema").fetchall()
        if schema != [(SCHEMA_VERSION,)]:
            raise CacheError(f"{label} has unexpected Aurora schema rows: {schema!r}")
        columns = tuple(row[1] for row in connection.execute("PRAGMA table_info(pipeline_cache)"))
        if columns != PIPELINE_COLUMNS:
            raise CacheError(f"{label} has unexpected pipeline_cache columns: {columns!r}")
        names = {(kind, name) for kind, name, _ in _schema_objects(connection)}
        required = {("table", "aurora_schema"), ("table", "pipeline_cache")}
        if not required.issubset(names):
            raise CacheError(f"{label} is missing required Aurora tables")
        unexpected = names - required - {("index", "pipeline_cache_load_order_idx")}
        if unexpected:
            raise CacheError(f"{label} contains unexpected schema objects: {sorted(unexpected)!r}")
    except sqlite3.DatabaseError as exc:
        raise CacheError(f"cannot inspect {label}: {exc}") from exc


def _rows(connection: sqlite3.Connection, label: str) -> list[tuple[int, int, int, int, bytes, int]]:
    try:
        raw_rows = connection.execute(
            "SELECT type, hash, config_version, config_size, config, first_frame_used "
            "FROM pipeline_cache ORDER BY rowid"
        ).fetchall()
    except sqlite3.DatabaseError as exc:
        raise CacheError(f"cannot read {label} pipeline rows: {exc}") from exc

    rows: list[tuple[int, int, int, int, bytes, int]] = []
    keys: set[tuple[int, int]] = set()
    for raw in raw_rows:
        if len(raw) != len(PIPELINE_COLUMNS):
            raise CacheError(f"{label} returned a malformed pipeline row")
        type_value, hash_value, version, config_size, config, first_frame = raw
        integer_values = (type_value, hash_value, version, config_size, first_frame)
        if any(type(value) is not int for value in integer_values):
            raise CacheError(f"{label} contains a non-integer pipeline field")
        if not 0 <= type_value <= 0xFFFFFFFF or not 0 <= hash_value <= 0xFFFFFFFF:
            raise CacheError(f"{label} contains an out-of-range pipeline key")
        if not 0 <= version <= 0xFFFFFFFF or not 0 <= config_size <= 0xFFFFFFFF:
            raise CacheError(f"{label} contains an out-of-range config field")
        if not 0 <= first_frame <= 0xFFFFFFFF:
            raise CacheError(f"{label} contains an out-of-range first_frame_used")
        if not isinstance(config, bytes) or config_size != len(config):
            raise CacheError(f"{label} has a config_size/blob mismatch")
        key = (type_value, hash_value)
        if key in keys:
            raise CacheError(f"{label} contains duplicate pipeline key {key!r}")
        keys.add(key)
        rows.append((type_value, hash_value, version, config_size, config, first_frame))
    return rows


def _inventory(rows: list[tuple[int, int, int, int, bytes, int]]) -> dict[str, Any]:
    by_type: dict[str, dict[str, Any]] = {}
    for type_value in sorted({row[0] for row in rows}):
        typed = [row for row in rows if row[0] == type_value]
        by_type[str(type_value)] = {
            "count": len(typed),
            "config_bytes": sum(row[3] for row in typed),
            "config_versions": sorted({row[2] for row in typed}),
            "config_sizes": sorted({row[3] for row in typed}),
            "first_frame_min": min(row[5] for row in typed),
            "first_frame_max": max(row[5] for row in typed),
        }
    return {"rows": len(rows), "by_type": by_type}


def _validate_base(rows: list[tuple[int, int, int, int, bytes, int]], expected: int) -> None:
    if any(row[0] not in (0, PIPELINE_TYPE) for row in rows):
        raise CacheError("base seed contains a shader type outside type 0 and type 1")
    shaders = [row for row in rows if row[0] == 0]
    pipelines = [row for row in rows if row[0] == PIPELINE_TYPE]
    if len(shaders) != 1:
        raise CacheError(f"base seed must retain exactly one type-0 shader row, got {len(shaders)}")
    if len(pipelines) != expected:
        raise CacheError(f"base seed must contain {expected} type-1 rows, got {len(pipelines)}")
    if any(row[2:4] != (PIPELINE_CONFIG_VERSION, PIPELINE_CONFIG_SIZE) for row in pipelines):
        raise CacheError("base seed has a type-1 row outside the reviewed version/size")
    shader = shaders[0]
    if shader[2] != 4 or shader[3] != len(shader[4]):
        raise CacheError("base seed type-0 shader row is malformed")


def _validate_candidate(rows: list[tuple[int, int, int, int, bytes, int]]) -> None:
    shaders = [row for row in rows if row[0] == 0]
    if len(shaders) != 1:
        raise CacheError(
            f"candidate must retain exactly one type-0 shader row, got {len(shaders)}"
        )
    shader = shaders[0]
    if shader[2] != 4 or shader[3] != len(shader[4]):
        raise CacheError("candidate type-0 shader row is malformed")
    for row in rows:
        if row[0] not in (0, PIPELINE_TYPE):
            raise CacheError(f"candidate contains unsupported shader type {row[0]}")
        if row[0] == PIPELINE_TYPE and (row[2], row[3]) != (
            PIPELINE_CONFIG_VERSION,
            PIPELINE_CONFIG_SIZE,
        ):
            raise CacheError("candidate contains a type-1 row outside the reviewed version/size")


def _load_provenance(path: Path | None) -> dict[str, Any]:
    if path is None:
        return {"merge_allowed": False, "reason": "no provenance manifest supplied"}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CacheError(f"cannot read provenance manifest {path}: {exc}") from exc
    if not isinstance(data, dict):
        raise CacheError("provenance manifest must be a JSON object")
    if data.get("schema") != "melee-web-pipeline-cache-export" or data.get("version") != 1:
        raise CacheError("provenance manifest has the wrong schema/version")
    if type(data.get("origin_cache_cleared")) is not bool:
        raise CacheError("provenance origin_cache_cleared must be boolean")
    if type(data.get("dawn_driver_cache_included")) is not bool:
        raise CacheError("provenance dawn_driver_cache_included must be boolean")
    return data


def _provenance_check(
    provenance: dict[str, Any], candidate: Path, wal: Path | None
) -> tuple[bool, list[str]]:
    reasons: list[str] = []
    if provenance.get("origin_cache_cleared") is not True:
        reasons.append("candidate origin cache was not declared cleared")
    if provenance.get("dawn_driver_cache_included") is not False:
        reasons.append("Dawn driver cache must be explicitly excluded")
    expected_db = provenance.get("export_db_sha256")
    if expected_db != sha256_file(candidate):
        reasons.append("candidate DB SHA-256 does not match provenance")
    expected_wal = provenance.get("export_wal_sha256")
    if wal is not None:
        if expected_wal != sha256_file(wal):
            reasons.append("candidate WAL SHA-256 does not match provenance")
    elif expected_wal is not None:
        reasons.append("provenance names a WAL but no WAL was supplied")
    return not reasons, reasons


def _row_summary(row: tuple[int, int, int, int, bytes, int]) -> dict[str, Any]:
    return {
        "type": row[0],
        "hash": row[1],
        "config_version": row[2],
        "config_size": row[3],
        "config_sha256": hashlib.sha256(row[4]).hexdigest(),
        "first_frame_used": row[5],
    }


def review(
    base_path: Path,
    candidate_path: Path,
    candidate_wal: Path | None,
    provenance_path: Path | None,
    expected_base_pipelines: int,
) -> tuple[dict[str, Any], list[tuple[int, int, int, int, bytes, int]]]:
    selected_candidate_wal = _select_wal(candidate_path, candidate_wal)
    with _seed_sqlite_copy(base_path) as base_db:
        try:
            base_connection = sqlite3.connect(f"file:{base_db}?mode=ro", uri=True)
        except sqlite3.DatabaseError as exc:
            raise CacheError(f"cannot open base seed {base_path}: {exc}") from exc
        with base_connection:
            _check_database_schema(base_connection, "base seed")
            base_rows = _rows(base_connection, "base seed")

    with _private_sqlite_copy(candidate_path, selected_candidate_wal) as candidate_db:
        try:
            candidate_connection = sqlite3.connect(f"file:{candidate_db}?mode=ro", uri=True)
        except sqlite3.DatabaseError as exc:
            raise CacheError(f"cannot open candidate cache {candidate_path}: {exc}") from exc
        with candidate_connection:
            _check_database_schema(candidate_connection, "candidate cache")
            candidate_rows = _rows(candidate_connection, "candidate cache")

    _validate_base(base_rows, expected_base_pipelines)
    _validate_candidate(candidate_rows)
    base_by_key = {(row[0], row[1]): row for row in base_rows}
    candidate_by_key = {(row[0], row[1]): row for row in candidate_rows}
    base_shader = next(row for row in base_rows if row[0] == 0)
    candidate_shader = next(row for row in candidate_rows if row[0] == 0)
    if candidate_shader[:5] != base_shader[:5]:
        raise CacheError("candidate type-0 shader row does not match the reviewed base")
    new_rows = [candidate_by_key[key] for key in sorted(set(candidate_by_key) - set(base_by_key))]
    missing_rows = sorted(set(base_by_key) - set(candidate_by_key))
    payload_conflicts: list[dict[str, Any]] = []
    first_frame_changes: list[dict[str, Any]] = []
    for key in sorted(set(base_by_key) & set(candidate_by_key)):
        old = base_by_key[key]
        observed = candidate_by_key[key]
        if old[2:5] != observed[2:5]:
            payload_conflicts.append(
                {"key": list(key), "base": _row_summary(old), "candidate": _row_summary(observed)}
            )
        elif old[5] != observed[5]:
            first_frame_changes.append(
                {"key": list(key), "base": old[5], "candidate": observed[5]}
            )
    provenance = _load_provenance(provenance_path)
    provenance_ok, provenance_reasons = _provenance_check(
        provenance, candidate_path, selected_candidate_wal
    )
    merge_allowed = provenance_ok and not payload_conflicts
    reasons = list(provenance_reasons)
    if payload_conflicts:
        reasons.append("candidate changes an existing reviewed row payload")
    report: dict[str, Any] = {
        "schema": "melee-web-pipeline-cache-review",
        "version": 1,
        "base": {
            "path": str(base_path),
            "sha256": sha256_file(base_path),
            "decoded_inventory": _inventory(base_rows),
            "expected_type1_rows": expected_base_pipelines,
        },
        "candidate": {
            "path": str(candidate_path),
            "sha256": sha256_file(candidate_path),
            "wal_path": str(selected_candidate_wal) if selected_candidate_wal is not None else None,
            "wal_sha256": (
                sha256_file(selected_candidate_wal)
                if selected_candidate_wal is not None
                else None
            ),
            "decoded_inventory": _inventory(candidate_rows),
            "provenance_path": str(provenance_path) if provenance_path is not None else None,
            "provenance": provenance,
        },
        "merge": {
            "eligible": merge_allowed,
            "reasons": reasons,
            "preserved_base_rows": len(base_rows),
            "candidate_new_rows": len(new_rows),
            "candidate_missing_base_rows": len(missing_rows),
            "candidate_existing_payload_conflicts": len(payload_conflicts),
            "candidate_existing_first_frame_changes": len(first_frame_changes),
            "new_rows": [_row_summary(row) for row in new_rows],
            "missing_base_keys": [list(key) for key in missing_rows],
            "payload_conflicts": payload_conflicts,
            "first_frame_changes": first_frame_changes,
            "dawn_driver_cache_included": False,
        },
    }
    return report, new_rows


def _write_merged_seed(
    output: Path,
    base_path: Path,
    new_rows: list[tuple[int, int, int, int, bytes, int]],
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="pipeline-merge-") as directory:
        temporary = Path(directory) / "merged.db"
        with _seed_sqlite_copy(base_path) as base_db:
            with sqlite3.connect(temporary) as connection:
                connection.executescript(CREATE_SCHEMA)
                connection.execute("INSERT INTO aurora_schema(value) VALUES (?)", (SCHEMA_VERSION,))
                with sqlite3.connect(f"file:{base_db}?mode=ro", uri=True) as source:
                    base_rows = _rows(source, "base seed")
                connection.executemany(
                    "INSERT INTO pipeline_cache "
                    "(type, hash, config_version, config_size, config, first_frame_used) "
                    "VALUES (?, ?, ?, ?, ?, ?)",
                    base_rows + new_rows,
                )
                connection.commit()
                if connection.execute("PRAGMA integrity_check").fetchone() != ("ok",):
                    raise CacheError("generated merged seed failed SQLite integrity_check")
        try:
            # O_EXCL makes creation atomic with respect to a racing writer and
            # rejects existing files, including dangling symlinks.
            with temporary.open("rb") as source, output.open("xb") as destination:
                shutil.copyfileobj(source, destination)
        except OSError as exc:
            raise CacheError(f"cannot create merged seed {output}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True, type=Path, help="reviewed seed DB or .gz.b64 file")
    parser.add_argument("--candidate", required=True, type=Path, help="candidate SQLite DB")
    parser.add_argument("--candidate-wal", type=Path, help="candidate WAL, when not adjacent to --candidate")
    parser.add_argument("--provenance", type=Path, help="explicit pipeline export provenance manifest")
    parser.add_argument("--report", required=True, type=Path, help="JSON report destination")
    parser.add_argument("--output", type=Path, help="merged DB; only allowed for fresh-origin evidence")
    parser.add_argument("--expected-base-pipelines", type=int, default=DEFAULT_BASE_PIPELINES)
    args = parser.parse_args()
    inputs = {path.resolve() for path in (
        args.base, args.candidate, _select_wal(args.candidate, args.candidate_wal),
        args.provenance,
    ) if path is not None}
    outputs = [path.resolve() for path in (args.report, args.output) if path is not None]
    if any(path in inputs for path in outputs) or len(set(outputs)) != len(outputs):
        parser.error("report and merged output must be distinct from input evidence and each other")
    try:
        report, new_rows = review(
            args.base,
            args.candidate,
            args.candidate_wal,
            args.provenance,
            args.expected_base_pipelines,
        )
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        if args.output is not None:
            if not report["merge"]["eligible"]:
                raise CacheError("refusing merge: " + "; ".join(report["merge"]["reasons"]))
            _write_merged_seed(args.output, args.base, new_rows)
        print(json.dumps(report["merge"], indent=2, sort_keys=True))
        return 0 if report["merge"]["eligible"] or args.output is None else 2
    except CacheError as exc:
        print(f"pipeline cache review failed: {exc}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
