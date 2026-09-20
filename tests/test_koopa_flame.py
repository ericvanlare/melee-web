"""Owned Bowser Flame Article decoding, publication and lifetime boundary."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class KoopaFlameTests(unittest.TestCase):
    def test_real_flame_article(self):
        asset = ROOT / "assets-local/full-game-koopa/PlKp.dat"
        if not asset.is_file():
            self.skipTest("Owned Koopa fighter archive is required")
        targets = [ROOT / "build" / directory / "gameplay_koopa_flame_trace.js"
                   for directory in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the Koopa Flame Article trace target")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run([str(node_runtime()), str(target), str(asset)],
                                cwd=ROOT, capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        self.assertIn("Koopa Flame 24-byte special, one state, source null-joint model, "
                      "and malformed null-form rejection passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
