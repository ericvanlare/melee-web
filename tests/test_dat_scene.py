"""Checked IfAll/IfCoGet hydration, animation ownership, and repeat teardown."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class DatSceneTests(unittest.TestCase):
    def test_synthetic_and_optional_owned_scene_roots(self):
        candidates = [
            ROOT / "build" / directory / "dat_scene_trace.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in candidates if path.is_file()]
        if not targets:
            self.skipTest("Build dat_scene_trace before the typed scene check")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("SceneDesc missing-symbol rejection", result.stdout)

        ifall = ROOT / "assets-local" / "native-menus" / "IfAll.usd"
        ifcoget = ROOT / "assets-local" / "native-menus" / "IfCoGet.dat"
        if not ifall.is_file() or not ifcoget.is_file():
            return
        result = subprocess.run(
            [str(node_runtime()), str(target), "--assets", str(ifall), str(ifcoget)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Typed SceneDesc hydration", result.stdout)


if __name__ == "__main__":
    unittest.main()
