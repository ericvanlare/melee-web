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

            self.assertEqual(2285568, output.stat().st_size)
            self.assertEqual(EXPECTED_SHA256, hashlib.sha256(output.read_bytes()).hexdigest())
            with sqlite3.connect(output) as database:
                self.assertEqual([(1,)], database.execute("SELECT value FROM aurora_schema").fetchall())
                self.assertEqual(
                    [(0, 1, 64), (1, 547, 1516284)],
                    database.execute(
                        "SELECT type, COUNT(*), SUM(length(config)) FROM pipeline_cache GROUP BY type"
                    ).fetchall(),
                )


if __name__ == "__main__":
    unittest.main()
