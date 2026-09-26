"""Real-archive Game & Watch visibility-list hydration and ownership."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameWatchVisibilityTests(unittest.TestCase):
    def test_all_article_visibility_descriptors(self):
        archive = ROOT / "assets-local/next-gate/PlGw.dat"
        copy_archive = ROOT / "assets-local/next-gate/PlKbCpGw.dat"
        popo_copy_archive = ROOT / "assets-local/next-gate/PlKbCpPp.dat"
        fox_copy_archive = ROOT / "assets-local/next-gate/PlKbCpFx.dat"
        if not all(path.is_file() for path in
                   (archive, copy_archive, popo_copy_archive, fox_copy_archive)):
            self.skipTest("Owned Game & Watch and Kirby copy archives are required")
        kirby_archive = ROOT / "assets-local/next-gate/PlKb.dat"
        targets = [
            ROOT / "build" / directory / "gameplay_gamewatch_visibility_trace.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the Game & Watch visibility trace target")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target), str(archive), str(copy_archive),
             str(kirby_archive), str(popo_copy_archive), str(fox_copy_archive)],
            cwd=ROOT, capture_output=True, text=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])
        self.assertIn("All ten Game & Watch Article visibility descriptors hydrated",
                      result.stdout)
        self.assertIn("Kirby copied Pan Article retains source lists",
                      result.stdout)
        self.assertIn("Kirby copied Ice Article hydrates its Popo-owned source attributes/state",
                      result.stdout)
        self.assertIn("Kirby copied Fox Laser/Blaster hydrate source models and 2/9 animation rows",
                      result.stdout)


if __name__ == "__main__":
    unittest.main()
