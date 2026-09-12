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
        stage_paths = [
            path for path in (
                ROOT / "assets-local" / "next-gate" / "GrNLa.dat",
                ROOT / "assets-local" / "next-gate" / "GrSt.dat",
                ROOT / "assets-local" / "next-gate" / "GrIz.dat",
                ROOT / "assets-local" / "next-gate" / "GrNBa.dat",
                ROOT / "assets-local" / "next-gate" / "GrOp.dat",
            ) if path.is_file()
        ]
        stage_args = [argument for path in stage_paths for argument in ("--stage", str(path))]
        result = subprocess.run(
            [str(node_runtime()), str(target), *stage_args],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(
            "DynamicModel hydration" if stage_paths else "SceneDesc missing-symbol rejection",
            result.stdout,
        )

        ifall = ROOT / "assets-local" / "native-menus" / "IfAll.usd"
        ifcoget = ROOT / "assets-local" / "native-menus" / "IfCoGet.dat"
        if not ifall.is_file() or not ifcoget.is_file():
            return
        result = subprocess.run(
            [str(node_runtime()), str(target), "--assets", str(ifall), str(ifcoget), *stage_args],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Typed SceneDesc and DynamicModel hydration passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
