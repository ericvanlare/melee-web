"""Focused source-stage lifecycle coverage for Yoshi's Island 64."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class OldYoshiStageTests(unittest.TestCase):
    def test_source_stage_owner_lifecycles(self):
        old_yoshi = ROOT / "assets-local/full-game-stage-yoshis-island-64"
        common = ROOT / "assets-local/full-game-ganon"
        required_old_yoshi = ("GrOy.dat", "old_ys.hps")
        required_common = (
            "PlCo.dat", "PlMr.dat", "PlMrAJ.dat", "EfMrData.dat",
            "ItCo.usd", "EfCoData.dat", "PdPm.dat", "LbRb.dat",
            "sislib_font.bin",
        )
        if (not old_yoshi.is_dir() or not common.is_dir() or
                not all((old_yoshi / name).is_file() for name in required_old_yoshi) or
                not all((common / name).is_file() for name in required_common)):
            self.skipTest("Owned Old Yoshi and common runtime fixtures are required")

        targets = [
            ROOT / "build" / directory / "gameplay_stage_old_yoshi_trace.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the Old Yoshi source stage trace target")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target), str(old_yoshi), str(common)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=180,
        )
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])
        self.assertIn(
            "Original Old Yoshi source maps, cloud collision collapse/reappear, "
            "guest scheduler and repeated teardown passed",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
