from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class MatchFlowTests(unittest.TestCase):
    def test_css_sss_loading_playing_two_cycle(self):
        if not (ROOT / ".deps/emsdk/.emscripten").is_file():
            self.skipTest("Pinned Node runtime unavailable before SDK bootstrap")
        result = subprocess.run(
            [str(node_runtime()), str(ROOT / "tests/match_flow_test.mjs")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Menu flow registry, immutable two-cycle lifecycle", result.stdout)


if __name__ == "__main__":
    unittest.main()
