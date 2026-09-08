"""Run the built focused Wasm trace with an optional user-owned local asset.

Build with `python3 scripts/build.py --target fighter`. The test skips when the
trace or asset is absent; it never downloads or extracts copyrighted assets.
"""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplayBonusDataTests(unittest.TestCase):
    def test_actual_threshold_words_and_scoped_publication(self):
        target = ROOT / "build/browser/gameplay_bonus_data_trace.js"
        if not target.is_file() or not (ROOT / ".deps/emsdk/.emscripten").is_file():
            self.skipTest("Build fighter targets before running the focused bonus_data trace")
        asset = ROOT / "assets-local/next-gate/PdPm.dat"
        if not asset.is_file():
            self.skipTest("Optional user-owned PdPm.dat is unavailable")
        result = subprocess.run([str(node_runtime()), str(target), str(asset)],
                                cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Original bonus getter reads all97 actual PdPm words; scoped restore/restart and5 malformed cases passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
