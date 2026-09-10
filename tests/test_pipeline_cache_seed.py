"""The bundled render-pipeline seed is reviewed, portable cache data."""

import hashlib
import sqlite3
from pathlib import Path
import tempfile
import unittest

from scripts.materialize_pipeline_cache import EXPECTED_SHA256, materialize


ROOT = Path(__file__).resolve().parents[1]
SEED = ROOT / "web" / "initial_pipeline_cache.db.gz.b64"


class PipelineCacheSeedTests(unittest.TestCase):
    def test_seed_materializes_with_current_schema_and_admitted_pipelines(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "initial_pipeline_cache.db"
            materialize(SEED, output)

            self.assertEqual(1368 * 1024, output.stat().st_size)
            self.assertEqual(EXPECTED_SHA256, hashlib.sha256(output.read_bytes()).hexdigest())
            with sqlite3.connect(output) as database:
                self.assertEqual([(1,)], database.execute("SELECT value FROM aurora_schema").fetchall())
                self.assertEqual(
                    [(0, 1, 64), (1, 333, 923076)],
                    database.execute(
                        "SELECT type, COUNT(*), SUM(length(config)) FROM pipeline_cache GROUP BY type"
                    ).fetchall(),
                )


if __name__ == "__main__":
    unittest.main()
