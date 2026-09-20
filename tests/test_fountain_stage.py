"""Focused source-stage lifecycle coverage for Fountain of Dreams."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class FountainStageTests(unittest.TestCase):
    def test_source_stage_owner_lifecycles(self):
        fountain = ROOT / "assets-local/full-game-stage-fountain"
        common = ROOT / "assets-local/full-game-ganon"
        required_fountain = ("GrIz.dat", "izumi.hps")
        required_common = (
            "PlCo.dat", "PlMr.dat", "PlMrAJ.dat", "EfMrData.dat",
            "ItCo.usd", "EfCoData.dat", "PdPm.dat", "LbRb.dat",
            "sislib_font.bin",
        )
        if (not fountain.is_dir() or not common.is_dir() or
                not all((fountain / name).is_file() for name in required_fountain) or
                not all((common / name).is_file() for name in required_common)):
            self.skipTest("Owned Fountain and common runtime fixtures are required")

        targets = [
            ROOT / "build" / directory / "gameplay_stage_fountain_trace.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the Fountain source stage trace target")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target), str(fountain), str(common)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=180,
        )
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])
        self.assertIn(
            "Original Fountain source maps, dynamic platform collision, "
            "scaled markers/lights, reflection and two-lifetime teardown passed",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
