"""Real-asset Results owner rejection and source mode-exit controls.

These synthetic standings are not a reproduction of a natural match failure.
"""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class ResultsCameraOwnershipTests(unittest.TestCase):
    def test_live_pool_guard_and_sheik_mode_exit_controls(self):
        roots = [ROOT / "assets-local" / name for name in
                 ("native-menus", "repro-results-v1", "next-gate")]
        required = [roots[0] / "MnSlChr.usd", roots[1] / "GmRst.usd",
                    roots[1] / "SdRst.usd", roots[1] / "TyDatai.usd",
                    roots[2] / "PlSk.dat", roots[2] / "PlZd.dat",
                    roots[2] / "PlYs.dat", roots[2] / "PlSs.dat",
                    roots[2] / "PlFc.dat"]
        if not all(path.is_file() for path in required):
            self.skipTest("Owned four-participant B Results fixtures required")
        targets = [ROOT / "build" / directory / "gameplay_results_scene_trace.js"
                   for directory in ("browser", "browser-release")]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build gameplay_results_scene_trace first")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        cases = {
            "--lineup-b-camera-pool-guard":
                ("rejected tick entry", "rejected draw entry",
                 "rejected scene exit entry", "rejected close entry"),
            "--lineup-b-zelda-sheik-mode-exit": ("winner_ckind=18 winner_ftkind=7",),
            "--lineup-b-sheik-mode-exit": ("winner_ckind=19 winner_ftkind=7",),
        }
        for command, markers in cases.items():
            with self.subTest(command=command):
                result = subprocess.run([str(node_runtime()), str(target), command,
                                         *map(str, roots)], cwd=ROOT, capture_output=True,
                                        text=True, timeout=90)
                output = "\n".join(line[:500] for line in
                                   (result.stdout + result.stderr).splitlines())[-12000:]
                self.assertEqual(result.returncode, 0, output)
                for marker in (*markers, "all four participant demo owners constructed and closed"):
                    self.assertIn(marker, result.stdout)


if __name__ == "__main__":
    unittest.main()
