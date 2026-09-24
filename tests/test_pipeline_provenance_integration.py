"""Join the compiled private recorder's synthetic output to an independent seed.

This optional fixture never invokes a compiler or browser. Set
MELEE_WEB_PIPELINE_PROVENANCE_FIXTURE to the fixture executable after compiling
it in an authorized quiet window. A passing synthetic join is no route capture.
"""
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import tempfile
import unittest

from scripts.generate_pipeline_requirements import generate, sha256_json


FIXTURE = os.environ.get("MELEE_WEB_PIPELINE_PROVENANCE_FIXTURE")


@unittest.skipUnless(FIXTURE, "compiled private provenance fixture not supplied")
class PipelineProvenanceIntegrationTests(unittest.TestCase):
    def test_actual_recorder_json_joins_typed_seed_and_frozen_input(self):
        result = subprocess.run([str(Path(FIXTURE).resolve()), "--json"],
                                check=True, capture_output=True, text=True, timeout=30)
        capture = json.loads(result.stdout)
        self.assertTrue(capture["status"]["valid"])
        self.assertTrue(capture["status"]["final"])
        coverage = {
            "schema": "melee-web-pipeline-coverage-v1", "version": 1,
            "cases": [{
                "case_id": "synthetic-recorder", "coverage_case_id": 23,
                "route_id": "synthetic-recorder",
                "route": {"fighter_numeric_ids": [0, 1], "stage_numeric_id": 37},
                "expected_phases": [{"scene": "match", "phase": "interactive"}],
                "expected_actions": [14], "expected_costumes": [1, 2],
                "lifecycle": ["interactive"],
                "input": {"capture_id": "synthetic-core-json", "sha256": "ab" * 32},
            }],
        }
        with tempfile.TemporaryDirectory() as directory:
            seed = Path(directory) / "synthetic-seed.db"
            with sqlite3.connect(seed) as db:
                db.execute("CREATE TABLE pipeline_cache(type INTEGER, hash INTEGER, "
                           "config_version INTEGER, config_size INTEGER, config BLOB, "
                           "first_frame_used INTEGER, PRIMARY KEY(type, hash))")
                db.executemany("INSERT INTO pipeline_cache VALUES (?, ?, ?, 4, ?, 0)",
                               [(4, 0x10, 2, bytes([1, 2, 3, 4])),
                                (8, 0x20, 3, bytes([1, 2, 3, 4]))])
            metadata = {
                "schema": "melee-web-pipeline-requirements-input-v1", "version": 1,
                "seed": {"decoded_sha256": hashlib.sha256(seed.read_bytes()).hexdigest()},
                "source": {"head": "a" * 40, "dirty_overlay_sha256": "b" * 64},
                "dependencies": {"aurora": "c" * 40, "melee": "d" * 40},
                "renderer": {"version": "synthetic", "config_layout": "synthetic",
                             "config_layout_sha256": "e" * 64},
                "registry": {"fighters": [{"numeric_id": 2}],
                             "stages": [{"numeric_id": 37}]},
                "coverage_manifest_sha256": sha256_json(coverage),
            }
            sidecar = generate(capture, seed, metadata, coverage)
        self.assertTrue(sidecar["status"]["certified"])
        members = [member for group in sidecar["groups"] for member in group["members"]]
        self.assertEqual(len(members), 1)
        self.assertEqual(members[0]["type"], 4)
        self.assertEqual(members[0]["ref_hex"], "0000000000000010")


if __name__ == "__main__":
    unittest.main()
