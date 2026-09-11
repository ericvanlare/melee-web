"""Focused checks for the reviewed portable Aurora pipeline-cache merge."""

from __future__ import annotations

import json
from pathlib import Path
import sqlite3
import sys
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts import review_pipeline_cache as CACHE  # noqa: E402


def _rows(*, extra: bool = True, bad_type: int | None = None) -> list[tuple]:
    shader = (0, 0x100, 4, 13, b"shader-config", 3)
    first = (1, 0x200, CACHE.PIPELINE_CONFIG_VERSION, CACHE.PIPELINE_CONFIG_SIZE,
             b"a" * CACHE.PIPELINE_CONFIG_SIZE, 10)
    second = (1, 0x201, CACHE.PIPELINE_CONFIG_VERSION, CACHE.PIPELINE_CONFIG_SIZE,
              b"b" * CACHE.PIPELINE_CONFIG_SIZE, 20)
    result = [shader, first, second]
    if extra:
        result.append(
            (bad_type if bad_type is not None else 1, 0x300,
             CACHE.PIPELINE_CONFIG_VERSION, CACHE.PIPELINE_CONFIG_SIZE,
             b"c" * CACHE.PIPELINE_CONFIG_SIZE, 30)
        )
    return result


def _create_db(path: Path, rows: list[tuple], *, wal: bool = False) -> sqlite3.Connection:
    connection = sqlite3.connect(path)
    if wal:
        connection.execute("PRAGMA journal_mode=WAL")
        connection.execute("PRAGMA wal_autocheckpoint=0")
    connection.executescript(CACHE.CREATE_SCHEMA)
    connection.execute("INSERT INTO aurora_schema(value) VALUES (?)", (CACHE.SCHEMA_VERSION,))
    connection.executemany(
        "INSERT INTO pipeline_cache "
        "(type, hash, config_version, config_size, config, first_frame_used) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        rows,
    )
    connection.commit()
    return connection


def _manifest(path: Path, wal: Path | None = None, **updates: object) -> dict:
    manifest = {
        "schema": "melee-web-pipeline-cache-export",
        "version": 1,
        "origin_cache_cleared": True,
        "dawn_driver_cache_included": False,
        "export_db_sha256": CACHE.sha256_file(path),
    }
    if wal is not None:
        manifest["export_wal_sha256"] = CACHE.sha256_file(wal)
    manifest.update(updates)
    return manifest


class ReviewPipelineCacheTests(unittest.TestCase):
    def test_merged_seed_rejects_dangling_symlink_without_touching_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            output = root / "merged.db"
            target = root / "outside.db"
            _create_db(base, _rows(extra=False)).close()
            output.symlink_to(target)

            with self.assertRaisesRegex(CACHE.CacheError, "cannot create merged seed"):
                CACHE._write_merged_seed(output, base, [_rows()[-1]])

            self.assertTrue(output.is_symlink())
            self.assertFalse(target.exists())

    def test_cli_refuses_to_overwrite_input_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base, candidate = root / "base.db", root / "candidate.db"
            _create_db(base, _rows(extra=False)).close()
            _create_db(candidate, _rows()).close()
            before = (base.read_bytes(), candidate.read_bytes())
            result = subprocess.run([
                sys.executable, str(ROOT / "scripts/review_pipeline_cache.py"),
                "--base", str(base), "--candidate", str(candidate),
                "--report", str(candidate), "--expected-base-pipelines", "2",
            ], capture_output=True, text=True)
            self.assertEqual(2, result.returncode)
            self.assertIn("distinct from input evidence", result.stderr)
            self.assertEqual(before, (base.read_bytes(), candidate.read_bytes()))

    def test_wal_review_preserves_inputs_and_only_appends_new_rows(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            candidate = root / "candidate.db"
            provenance = root / "candidate.json"
            output = root / "merged.db"
            _create_db(base, _rows(extra=False)).close()
            candidate_rows = _rows()
            # A first-frame observation is allowed to differ; the reviewed seed
            # still supplies the preserved base row field-for-field.
            candidate_rows[1] = (*candidate_rows[1][:5], 99)
            writer = _create_db(candidate, candidate_rows, wal=True)
            wal = Path(f"{candidate}-wal")
            self.assertTrue(wal.is_file())
            before = (CACHE.sha256_file(candidate), CACHE.sha256_file(wal))
            provenance.write_text(
                json.dumps(_manifest(candidate, wal)) + "\n", encoding="utf-8"
            )

            try:
                report, new_rows = CACHE.review(
                    base, candidate, None, provenance, expected_base_pipelines=2
                )
                self.assertTrue(report["merge"]["eligible"])
                self.assertEqual(1, report["merge"]["candidate_new_rows"])
                self.assertEqual(str(wal), report["candidate"]["wal_path"])
                self.assertEqual(before, (CACHE.sha256_file(candidate), CACHE.sha256_file(wal)))

                CACHE._write_merged_seed(output, base, new_rows)
                with sqlite3.connect(output) as connection:
                    merged = connection.execute(
                        "SELECT type, hash, config_version, config_size, config, first_frame_used "
                        "FROM pipeline_cache ORDER BY rowid"
                    ).fetchall()
                self.assertEqual(_rows(extra=False) + [_rows()[-1]], merged)
            finally:
                writer.close()

    def test_provenance_db_and_wal_hashes_bind_the_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            candidate = root / "candidate.db"
            provenance = root / "candidate.json"
            _create_db(base, _rows(extra=False)).close()
            writer = _create_db(candidate, _rows(), wal=True)
            wal = Path(f"{candidate}-wal")
            try:
                manifest = _manifest(candidate, wal, export_wal_sha256="00" * 32)
                provenance.write_text(json.dumps(manifest), encoding="utf-8")
                report, _ = CACHE.review(
                    base, candidate, None, provenance, expected_base_pipelines=2
                )
                self.assertFalse(report["merge"]["eligible"])
                self.assertIn("candidate WAL SHA-256 does not match provenance",
                              report["merge"]["reasons"])

                # The database hash is checked independently once the live WAL
                # has been closed and the candidate is a normal SQLite file.
                writer.close()
                manifest = _manifest(candidate)
                provenance.write_text(json.dumps(manifest), encoding="utf-8")
                with sqlite3.connect(candidate) as connection:
                    connection.execute("PRAGMA journal_mode=DELETE")
                    connection.execute(
                        "UPDATE pipeline_cache SET first_frame_used = 31 WHERE hash = 768"
                    )
                writer = None
                report, _ = CACHE.review(
                    base, candidate, None, provenance, expected_base_pipelines=2
                )
                self.assertFalse(report["merge"]["eligible"])
                self.assertIn("candidate DB SHA-256 does not match provenance",
                              report["merge"]["reasons"])
            finally:
                if writer is not None:
                    writer.close()

    def test_bad_typed_descriptor_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            candidate = root / "candidate.db"
            _create_db(base, _rows(extra=False)).close()
            rows = _rows()
            rows[-1] = (1, rows[-1][1], CACHE.PIPELINE_CONFIG_VERSION - 1,
                        rows[-1][3], rows[-1][4], rows[-1][5])
            _create_db(candidate, rows).close()
            with self.assertRaisesRegex(CACHE.CacheError, "outside the reviewed version/size"):
                CACHE.review(base, candidate, None, None, expected_base_pipelines=2)

    def test_unknown_type_and_schema_objects_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            candidate = root / "candidate.db"
            _create_db(base, _rows(extra=False)).close()
            _create_db(candidate, _rows(bad_type=7)).close()
            with self.assertRaisesRegex(CACHE.CacheError, "unsupported shader type 7"):
                CACHE.review(base, candidate, None, None, expected_base_pipelines=2)

            candidate.unlink()
            connection = _create_db(candidate, _rows())
            connection.execute("CREATE TABLE unexpected(value INTEGER)")
            connection.commit()
            connection.close()
            with self.assertRaisesRegex(CACHE.CacheError, "unexpected schema objects"):
                CACHE.review(base, candidate, None, None, expected_base_pipelines=2)

    def test_candidate_must_have_one_matching_shader_descriptor(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "base.db"
            candidate = root / "candidate.db"
            _create_db(base, _rows(extra=False)).close()
            rows = [row for row in _rows() if row[0] != 0]
            _create_db(candidate, rows).close()
            with self.assertRaisesRegex(CACHE.CacheError, "exactly one type-0 shader"):
                CACHE.review(base, candidate, None, None, expected_base_pipelines=2)

            candidate.unlink()
            rows = _rows()
            rows[0] = (0, 0x999, 4, 13, b"shader-config", 3)
            _create_db(candidate, rows).close()
            with self.assertRaisesRegex(CACHE.CacheError, "does not match the reviewed base"):
                CACHE.review(base, candidate, None, None, expected_base_pipelines=2)


if __name__ == "__main__":
    unittest.main()
