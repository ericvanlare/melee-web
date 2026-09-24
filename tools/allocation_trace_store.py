"""Bounded-memory storage for original allocation-history JSONL traces.

The normal trace path is intentionally a list of dictionaries so the small
synthetic fixtures and existing callers retain their old interface.  Large
captures can use :class:`TraceStore` through ``load_trace``; rows and the
entry/return indexes then live in a temporary SQLite database.  JSON rows are
compressed individually, so every field remains lossless while replay only
materializes the row currently being inspected.
"""
from __future__ import annotations

from collections.abc import Iterator, Mapping, Sequence
import json
from pathlib import Path
import shutil
import sqlite3
import tempfile
from typing import Any
import zlib


class TraceStoreError(Exception):
    """A trace could not be stored or a disk-backed row could not be read."""


def _encode(row: dict[str, Any]) -> bytes:
    try:
        raw = json.dumps(row, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        return zlib.compress(raw, level=1)
    except (TypeError, ValueError, OverflowError, UnicodeError) as error:
        raise TraceStoreError(f"trace row cannot be encoded: {error}") from error


def _decode(blob: bytes) -> dict[str, Any]:
    try:
        row = json.loads(zlib.decompress(blob).decode("utf-8"))
    except (ValueError, TypeError, zlib.error, UnicodeError) as error:
        raise TraceStoreError(f"stored trace row is corrupt: {error}") from error
    if not isinstance(row, dict):
        raise TraceStoreError("stored trace row is not an object")
    return row


class TraceStore:
    """Temporary compressed SQLite store and its sequence/mapping views.

    The database is deliberately private and temporary.  Its owner is kept by
    every returned view, so it remains readable for the complete replay call;
    ``close`` is available to callers that need deterministic cleanup.
    """

    MIN_FREE_BYTES = 2 * 1024**3
    MAX_STORE_BYTES = 8 * 1024**3

    def __init__(self, *, directory: Path | None = None,
                 min_free_bytes: int = MIN_FREE_BYTES,
                 max_bytes: int = MAX_STORE_BYTES):
        if (isinstance(min_free_bytes, bool) or not isinstance(min_free_bytes, int)
                or min_free_bytes < self.MIN_FREE_BYTES):
            raise TraceStoreError(
                f"trace-store minimum free reserve is {self.MIN_FREE_BYTES} bytes"
            )
        if (isinstance(max_bytes, bool) or not isinstance(max_bytes, int)
                or max_bytes <= 0 or max_bytes > self.MAX_STORE_BYTES):
            raise TraceStoreError(
                f"trace-store byte budget must be between 1 and {self.MAX_STORE_BYTES} bytes"
            )
        self._min_free_bytes = min_free_bytes
        self._max_bytes = max_bytes
        try:
            self._temporary = tempfile.TemporaryDirectory(
                prefix="melee-allocation-trace-",
                dir=str(directory) if directory is not None else None,
            )
            self.path = Path(self._temporary.name) / "trace.sqlite3"
            self._connection = sqlite3.connect(str(self.path))
            self._connection.execute("PRAGMA journal_mode=OFF")
            self._connection.execute("PRAGMA synchronous=OFF")
            self._connection.execute("PRAGMA temp_store=FILE")
            self._connection.execute("PRAGMA cache_size=-8192")
            self._connection.execute("PRAGMA mmap_size=0")
            self._connection.executescript(
                """
                CREATE TABLE rows (
                    sequence INTEGER PRIMARY KEY,
                    blob BLOB NOT NULL
                );
                CREATE TABLE enters (
                    call INTEGER PRIMARY KEY,
                    sequence INTEGER NOT NULL UNIQUE,
                    FOREIGN KEY(sequence) REFERENCES rows(sequence)
                );
                CREATE TABLE returns (
                    call INTEGER PRIMARY KEY,
                    sequence INTEGER NOT NULL UNIQUE,
                    FOREIGN KEY(sequence) REFERENCES rows(sequence)
                );
                """
            )
            self._connection.commit()
            self._closed = False
            self._pending = 0
            self._row_count = 0
            self._mapping_counts = {"enters": 0, "returns": 0}
            self._unchecked_budget_bytes = 0
            self._unchecked_budget_rows = 0
            self._check_storage_budget(0)
        except TraceStoreError:
            self.close()
            raise
        except (OSError, sqlite3.Error) as error:
            self.close()
            raise TraceStoreError(f"cannot create temporary trace store: {error}") from error

    def _check_storage_budget(self, additional_bytes: int) -> None:
        """Reserve disk headroom before SQLite can grow the temporary index."""
        try:
            free = shutil.disk_usage(self._temporary.name).free
            current = self.path.stat().st_size
        except OSError as error:
            raise TraceStoreError(f"cannot inspect trace-store disk budget: {error}") from error
        # SQLite may allocate a page and metadata around each compressed blob;
        # keep a page-sized allowance in the projection and preserve the
        # minimum free-space reserve after the write.
        projected = current + max(0, additional_bytes) + 4096
        if projected > self._max_bytes:
            raise TraceStoreError(
                f"trace-store exceeded its {self._max_bytes} byte disk budget"
            )
        if free < self._min_free_bytes + max(0, additional_bytes) + 4096:
            raise TraceStoreError(
                f"trace-store requires at least {self._min_free_bytes} free disk bytes"
            )

    def _check_open(self) -> None:
        if self._closed:
            raise TraceStoreError("trace store is closed")

    def add(self, row: dict[str, Any]) -> None:
        """Insert one row, preserving its complete JSON object."""
        self._check_open()
        sequence = row.get("sequence")
        record = row.get("record")
        blob = _encode(row)
        self._unchecked_budget_bytes += len(blob) + 4096
        self._unchecked_budget_rows += 1
        if len(blob) + 4096 > self._max_bytes:
            self._check_storage_budget(len(blob) + 4096)
        elif self._unchecked_budget_rows >= 4096:
            self._check_storage_budget(self._unchecked_budget_bytes)
            self._unchecked_budget_bytes = 0
            self._unchecked_budget_rows = 0
        try:
            self._connection.execute(
                "INSERT INTO rows(sequence, blob) VALUES (?, ?)",
                (sequence, sqlite3.Binary(blob)),
            )
            if record == "enter":
                self._connection.execute(
                    "INSERT INTO enters(call, sequence) VALUES (?, ?)",
                    (row.get("call"), sequence),
                )
            elif record == "return":
                self._connection.execute(
                    "INSERT INTO returns(call, sequence) VALUES (?, ?)",
                    (row.get("call"), sequence),
                )
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace index insert failed: {error}") from error
        self._pending += 1
        self._row_count += 1
        table = record + "s" if record in ("enter", "return") else None
        if table is not None:
            self._mapping_counts[table] += 1
        if self._pending >= 4096:
            self._connection.commit()
            self._pending = 0

    def finish(self) -> None:
        self._check_open()
        try:
            self._connection.commit()
            self._check_storage_budget(0)
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace index commit failed: {error}") from error
        self._pending = 0

    def count(self) -> int:
        self._check_open()
        return self._row_count

    def mapping_count(self, table: str) -> int:
        self._check_open()
        if table not in ("enters", "returns"):
            raise TraceStoreError("invalid trace mapping table")
        return self._mapping_counts[table]

    def _row_at(self, sequence: int) -> dict[str, Any]:
        self._check_open()
        try:
            result = self._connection.execute(
                "SELECT blob FROM rows WHERE sequence = ?", (sequence,)
            ).fetchone()
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace row lookup failed: {error}") from error
        if result is None:
            raise IndexError(sequence)
        return _decode(result[0])

    def _iter_rows(self, descending: bool = False) -> Iterator[dict[str, Any]]:
        self._check_open()
        order = "DESC" if descending else "ASC"
        try:
            cursor = self._connection.execute(f"SELECT blob FROM rows ORDER BY sequence {order}")
            for (blob,) in cursor:
                yield _decode(blob)
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace row iteration failed: {error}") from error

    def _mapping_row(self, table: str, call: int) -> dict[str, Any]:
        self._check_open()
        if table not in ("enters", "returns"):
            raise TraceStoreError("invalid trace mapping table")
        try:
            result = self._connection.execute(
                f"SELECT rows.blob FROM {table} JOIN rows ON rows.sequence = {table}.sequence "
                f"WHERE {table}.call = ?", (call,)
            ).fetchone()
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace mapping lookup failed: {error}") from error
        if result is None:
            raise KeyError(call)
        return _decode(result[0])

    def _mapping_contains(self, table: str, call: object) -> bool:
        self._check_open()
        if isinstance(call, bool) or not isinstance(call, int):
            return False
        try:
            result = self._connection.execute(
                f"SELECT 1 FROM {table} WHERE call = ?", (call,)
            ).fetchone()
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace mapping membership lookup failed: {error}") from error
        return result is not None

    def _iter_mapping(self, table: str, *, keys: bool) -> Iterator[Any]:
        self._check_open()
        try:
            cursor = self._connection.execute(
                f"SELECT {table}.call, rows.blob FROM {table} "
                f"JOIN rows ON rows.sequence = {table}.sequence ORDER BY {table}.sequence"
            )
            for call, blob in cursor:
                yield call if keys else _decode(blob)
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace mapping iteration failed: {error}") from error

    def _iter_mapping_items(self, table: str) -> Iterator[tuple[int, dict[str, Any]]]:
        self._check_open()
        if table not in ("enters", "returns"):
            raise TraceStoreError("invalid trace mapping table")
        try:
            cursor = self._connection.execute(
                f"SELECT {table}.call, rows.blob FROM {table} "
                f"JOIN rows ON rows.sequence = {table}.sequence "
                f"ORDER BY {table}.sequence"
            )
            for call, blob in cursor:
                yield call, _decode(blob)
        except sqlite3.Error as error:
            raise TraceStoreError(f"trace mapping item iteration failed: {error}") from error

    def close(self) -> None:
        if getattr(self, "_closed", False):
            return
        self._closed = True
        try:
            connection = getattr(self, "_connection", None)
            if connection is not None:
                connection.close()
        finally:
            temporary = getattr(self, "_temporary", None)
            if temporary is not None:
                temporary.cleanup()

    def __del__(self):  # pragma: no cover - interpreter cleanup is best effort
        try:
            self.close()
        except Exception:
            pass

    def views(self) -> tuple["TraceRows", "TraceMapping", "TraceMapping"]:
        self.finish()
        return TraceRows(self), TraceMapping(self, "enters"), TraceMapping(self, "returns")


class TraceRows(Sequence[dict[str, Any]]):
    """A sequence-compatible view over compressed trace rows."""

    def __init__(self, store: TraceStore):
        self._store = store

    def __len__(self) -> int:
        return self._store.count()

    def __getitem__(self, index):
        if isinstance(index, slice):
            raise TypeError("disk-backed trace rows do not materialize slices; iterate the bounded view")
        if isinstance(index, bool) or not isinstance(index, int):
            raise TypeError("trace row indices must be integers")
        count = len(self)
        if index < 0:
            index += count
        if index < 0 or index >= count:
            raise IndexError(index)
        return self._store._row_at(index)

    def __iter__(self) -> Iterator[dict[str, Any]]:
        return self._store._iter_rows()

    def __reversed__(self) -> Iterator[dict[str, Any]]:
        return self._store._iter_rows(descending=True)

    def close(self) -> None:
        self._store.close()


class TraceMapping(Mapping[int, dict[str, Any]]):
    """Mapping-compatible call index backed by the compressed trace store."""

    def __init__(self, store: TraceStore, table: str):
        if table not in ("enters", "returns"):
            raise ValueError(table)
        self._store = store
        self._table = table

    def __len__(self) -> int:
        return self._store.mapping_count(self._table)

    def __iter__(self) -> Iterator[int]:
        return self._store._iter_mapping(self._table, keys=True)

    def __getitem__(self, call: int) -> dict[str, Any]:
        return self._store._mapping_row(self._table, call)

    def __contains__(self, call: object) -> bool:
        return self._store._mapping_contains(self._table, call)

    def get(self, call: int, default=None):
        try:
            return self[call]
        except KeyError:
            return default

    def keys(self) -> Iterator[int]:
        return iter(self)

    def values(self) -> Iterator[dict[str, Any]]:
        return self._store._iter_mapping(self._table, keys=False)

    def items(self) -> Iterator[tuple[int, dict[str, Any]]]:
        return self._store._iter_mapping_items(self._table)

    def close(self) -> None:
        self._store.close()


def new_disk_store(*, directory: Path | None = None,
                   min_free_bytes: int = TraceStore.MIN_FREE_BYTES,
                   max_bytes: int = TraceStore.MAX_STORE_BYTES) -> TraceStore:
    """Create a temporary disk-backed store for ``allocation_history_replay``."""
    return TraceStore(directory=directory, min_free_bytes=min_free_bytes, max_bytes=max_bytes)


__all__ = ["TraceMapping", "TraceRows", "TraceStore", "TraceStoreError", "new_disk_store"]
