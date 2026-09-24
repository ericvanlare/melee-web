"""Bounded-memory allocation trace input storage tests."""
from __future__ import annotations

import gzip
import json
from pathlib import Path
import secrets
import sqlite3
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import Mock, patch

from tools import allocation_history_replay as replay_module
from tools.allocation_history_replay import ReplayProblem, load_trace
from tools.allocation_trace_store import TraceMapping, TraceRows, TraceStore, TraceStoreError


class AllocationTraceStoreTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="allocation trace store ")
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    @staticmethod
    def trace_rows():
        return [
            {
                "record": "header", "sequence": 0,
                "schema": "melee-web-original-allocation-history", "version": 1,
                "scope": "boot_only", "nested": {"source": [1, 2, 3]},
            },
            {
                "record": "enter", "sequence": 1, "call": 10,
                "function": "root", "thread": 7, "parent": None,
                "sp": 0x80001000, "lr": 0x80002000, "args": [4, {"x": True}],
            },
            {
                "record": "enter", "sequence": 2, "call": 11,
                "function": "child", "thread": 7, "parent": 10,
                "sp": 0x80000F00, "lr": 0x80002000, "args": [],
            },
            {
                "record": "return", "sequence": 3, "call": 11,
                "function": "child", "thread": 7, "result": 0x20,
                "observed": {"payload": [0, 0xFFFFFFFF]},
            },
            {
                "record": "return", "sequence": 4, "call": 10,
                "function": "root", "thread": 7, "result": 0,
                "observed": {"raw": "preserved"},
            },
            {"record": "end", "sequence": 5, "status": "captured", "calls": 2},
        ]

    def write(self, rows, *, gzip_path=False, truncate=False):
        path = self.root / ("trace.jsonl.gz" if gzip_path else "trace.jsonl")
        encoded = ("\n".join(json.dumps(row, sort_keys=True) for row in rows) + "\n").encode()
        if gzip_path:
            path.write_bytes(gzip.compress(encoded))
            if truncate:
                path.write_bytes(path.read_bytes()[:-3])
        else:
            path.write_bytes(encoded)
        return path

    def test_forced_disk_store_preserves_rows_and_mapping_views(self):
        path = self.write(self.trace_rows())
        header, rows, enters, returns = load_trace(
            path, disk_backed=True, temp_directory=self.root,
        )

        self.assertEqual(header["nested"], {"source": [1, 2, 3]})
        self.assertIsInstance(rows, TraceRows)
        self.assertIsInstance(enters, TraceMapping)
        self.assertIsInstance(returns, TraceMapping)
        self.assertEqual(len(rows), 6)
        self.assertEqual(rows[1]["args"], [4, {"x": True}])
        self.assertEqual(rows[-1]["record"], "end")
        self.assertEqual([row["sequence"] for row in rows], list(range(6)))
        self.assertEqual([row["sequence"] for row in reversed(rows)], [5, 4, 3, 2, 1, 0])
        self.assertEqual(enters.get(10), rows[1])
        self.assertIn(11, enters)
        self.assertNotIn(99, enters)
        self.assertEqual(set(enters.keys()), {10, 11})
        self.assertEqual({row["call"] for row in enters.values()}, {10, 11})
        self.assertEqual(set(returns.keys()), {10, 11})
        self.assertEqual(returns[11]["observed"]["payload"], [0, 0xFFFFFFFF])
        rows.close()

    def test_auto_migration_and_gzip_retain_small_default_contract(self):
        path = self.write(self.trace_rows(), gzip_path=True)
        header, rows, enters, returns = load_trace(path)
        self.assertIsInstance(rows, list)
        self.assertIsInstance(enters, dict)
        self.assertEqual(rows[2]["parent"], 10)
        self.assertEqual(returns[10]["observed"], {"raw": "preserved"})

        header, rows, enters, returns = load_trace(path, disk_threshold=2)
        self.assertIsInstance(rows, TraceRows)
        self.assertEqual(header["record"], "header")
        self.assertEqual(enters[10]["function"], "root")
        rows.close()

    def test_truncated_gzip_is_rejected(self):
        path = self.write(self.trace_rows(), gzip_path=True, truncate=True)
        with self.assertRaises(ReplayProblem) as raised:
            load_trace(path, disk_backed=True)
        self.assertEqual(raised.exception.kind, "stream")
        self.assertIn("gzip", raised.exception.message)

    def test_gzip_decode_error_is_reported_as_stream_failure(self):
        path = self.root / "invalid-utf8.jsonl.gz"
        path.write_bytes(gzip.compress(b"\xff\n"))
        with self.assertRaises(ReplayProblem) as raised:
            load_trace(path, disk_backed=True)
        self.assertEqual(raised.exception.kind, "stream")
        self.assertIn("gzip", raised.exception.message)

    def test_store_requires_free_reserve_and_hard_budget(self):
        with patch("tools.allocation_trace_store.shutil.disk_usage",
                   return_value=SimpleNamespace(free=TraceStore.MIN_FREE_BYTES - 1)):
            with self.assertRaisesRegex(TraceStoreError, "free disk"):
                TraceStore(directory=self.root)

        store = TraceStore(directory=self.root, max_bytes=64 * 1024)
        try:
            with self.assertRaisesRegex(TraceStoreError, "disk budget"):
                store.add({"record": "repeated_stop", "sequence": 0,
                           "payload": secrets.token_hex(100_000)})
        finally:
            store.close()

    def test_sqlite_query_failures_are_wrapped(self):
        path = self.write(self.trace_rows())
        _, rows, enters, _ = load_trace(path, disk_backed=True)
        store = rows._store
        connection = store._connection
        failing = Mock(wraps=connection)
        failing.execute.side_effect = sqlite3.OperationalError("injected query failure")
        store._connection = failing
        try:
            operations = (
                lambda: rows[1], lambda: list(rows), lambda: list(reversed(rows)),
                lambda: enters[10], lambda: 10 in enters,
                lambda: list(enters), lambda: list(enters.values()), lambda: list(enters.items()),
            )
            for operation in operations:
                with self.assertRaises(TraceStoreError):
                    operation()
        finally:
            store._connection = connection
            rows.close()

    def test_replay_closes_loaded_views_when_provenance_fails(self):
        profile = self.root / "profile.json"
        profile.write_text(json.dumps({
            "schema": "melee-web-original-allocation-profile",
            "version": 1,
            "source_revision": "synthetic-cleanup-test",
            "functions": [],
        }))
        trace = self.root / "trace.jsonl"
        trace.write_text("unused\n")
        views = [Mock(), Mock(), Mock()]
        header = {"record": "header", "schema": "wrong", "version": 1}
        with patch.object(replay_module, "load_trace",
                          return_value=(header, *views)):
            with self.assertRaises(ReplayProblem):
                replay_module.replay(
                    trace, profile, None,
                    {key: None for key in ("arena_lo", "arena_hi", "heap_max_num",
                                           "audio_heap_size", "lbmemory_arena_lo",
                                           "lbmemory_arena_hi")},
                    None, None,
                )
        for view in views:
            view.close.assert_called_once_with()

    def test_sequence_and_nesting_validation_also_apply_to_disk_store(self):
        rows = self.trace_rows()
        rows[2] = {**rows[2], "sequence": 99}
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

        rows = self.trace_rows()
        rows[0] = {**rows[0], "version": True}
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

        rows = self.trace_rows()
        rows[3] = {**rows[3], "call": 10}
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

        rows = self.trace_rows()
        rows[-1] = {**rows[-1], "pending_calls": [10]}
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

        rows = self.trace_rows()
        rows[1] = {**rows[1], "thread": [7]}
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

    def test_duplicate_entry_and_return_are_rejected_before_indexing(self):
        rows = self.trace_rows()
        duplicate = {**rows[1], "sequence": 2}
        rows.insert(2, duplicate)
        for index, row in enumerate(rows):
            row["sequence"] = index
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)

        rows = self.trace_rows()
        duplicate = {**rows[3], "sequence": 4}
        rows.insert(4, duplicate)
        for index, row in enumerate(rows):
            row["sequence"] = index
        with self.assertRaises(ReplayProblem):
            load_trace(self.write(rows), disk_backed=True)


if __name__ == "__main__":
    unittest.main()
