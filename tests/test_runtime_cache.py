"""Exercise the optional browser IDBFS render-cache bridge with a Node VM."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class RuntimeCacheTests(unittest.TestCase):
    def test_mount_populate_save_and_failures(self):
        if not (ROOT / ".deps/emsdk/.emscripten").is_file():
            self.skipTest("Pinned Node runtime unavailable before SDK bootstrap")
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/runtime_cache_test.mjs")],
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Runtime cache mount, populate, dependency, serialization and failure checks passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
