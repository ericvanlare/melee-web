"""The bundled render-pipeline seed is reviewed, portable cache data."""

import hashlib
import base64
import gzip
import sqlite3
from pathlib import Path
import tempfile
import unittest

from scripts.materialize_pipeline_cache import EXPECTED_SHA256, materialize


ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / "web" / "initial_pipeline_cache.db.gz.b64"


class PipelineCacheSeedTests(unittest.TestCase):
    def test_link_first_draw_compiler_stall_descriptors_are_prepared(self):
        # The correlated browser/native trace in issue #33 names these four
        # pipelines. Missing them compiled Metal shaders during the first live
        # Link/Young Link draw and blocked staging acquisition for 482.785 ms.
        expected = {
            0xde423b8a: "17aa2827133d2e10b8fe58465d1932c197e6780f41e16d3ef0f42bb24ba88f35",
            0xf3f744bb: "004853a34dc398d80489edd21e3c72e2b28a1f919cb819fbab7e4688101412c2",
            0xd1a85940: "9e159f55f141abae96fd3e3ffbd2f33c8e95060286225160d7c9cc507a59df98",
            0xa38c3036: "1170ff37aef88a06991abc6405bb7220d28b09f677b0862a1ee9ea559f92d502",
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "seed.db"
            materialize(SEED, output)
            with sqlite3.connect(output) as database:
                for key, digest in expected.items():
                    with self.subTest(pipeline=f"{key:08x}"):
                        row = database.execute(
                            "SELECT config_version, config_size, config FROM pipeline_cache "
                            "WHERE type=1 AND hash=?", (key,)
                        ).fetchone()
                        self.assertIsNotNone(row, "First live draw must find its reviewed descriptor")
                        self.assertEqual((65549, 2772), row[:2])
                        self.assertEqual(digest, hashlib.sha256(row[2]).hexdigest())

    def test_consumed_marth_holdout_descriptors_are_prepared(self):
        # The first frozen cold Marth/Battlefield holdout created two live
        # pipelines. Its exact saved IDBFS DB/WAL contains these new records;
        # the source is now a development regression, not fresh acceptance.
        expected = {
            0xdfe90cff: "f5b9b534b519652154580d7aba5cfde94f5c6149a46a7219de0ab36c9bb2a761",
            0xf285d8c5: "b1b50b4ce838807052b36cd70a18e0bb5dee14f134aaa7d1e22000ba6f4e5e52",
        }
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "seed.db"
            materialize(SEED, output)
            with sqlite3.connect(output) as database:
                for key, digest in expected.items():
                    with self.subTest(pipeline=f"{key:08x}"):
                        row = database.execute(
                            "SELECT config_version, config_size, config FROM pipeline_cache "
                            "WHERE type=1 AND hash=?", (key,)
                        ).fetchone()
                        self.assertIsNotNone(row, "Live draw must find its reviewed descriptor")
                        self.assertEqual((65549, 2772), row[:2])
                        self.assertEqual(digest, hashlib.sha256(row[2]).hexdigest())

    def test_runtime_identity_is_bound_to_the_verified_materialized_seed(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "initial_pipeline_cache.db"
            header = root / "melee_pipeline_seed_identity.h"
            materialize(SEED, output, header)
            actual_digest = hashlib.sha256(output.read_bytes()).hexdigest()
            self.assertIn(f'"{actual_digest}"', header.read_text())

            # A stale or modified seed must not mint a matching runtime identity.
            altered = root / "altered.gz.b64"
            altered.write_bytes(base64.b64encode(gzip.compress(output.read_bytes() + b"altered")))
            with self.assertRaisesRegex(ValueError, "digest mismatch"):
                materialize(altered, root / "rejected.db", root / "rejected.h")
            self.assertFalse((root / "rejected.db").exists())
            self.assertFalse((root / "rejected.h").exists())

    def test_seed_materializes_with_current_schema_and_admitted_pipelines(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "initial_pipeline_cache.db"
            materialize(SEED, output)

            self.assertEqual(2621440, output.stat().st_size)
            self.assertEqual(EXPECTED_SHA256, hashlib.sha256(output.read_bytes()).hexdigest())
            with sqlite3.connect(output) as database:
                self.assertEqual([(1,)], database.execute("SELECT value FROM aurora_schema").fetchall())
                self.assertEqual(
                    [(0, 1, 64), (1, 627, 1738044)],
                    database.execute(
                        "SELECT type, COUNT(*), SUM(length(config)) FROM pipeline_cache GROUP BY type"
                    ).fetchall(),
                )


if __name__ == "__main__":
    unittest.main()
