"""Typed CSS card support archive boundary; no renderer or browser claim."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class DatMenuSupportTests(unittest.TestCase):
    def test_support_roots_and_optional_local_assets(self):
        candidates = [ROOT / "build" / directory / "dat_menu_support_trace.js"
                      for directory in ("browser", "browser-release")]
        targets = [path for path in candidates if path.is_file()]
        if not targets:
            self.skipTest("Build fighter targets before the typed support check")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target)],
            cwd=ROOT, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("synthetic CSS card support roots", result.stdout)

        icons = ROOT / "assets-local" / "native-menus" / "LbMcGame.usd"
        scene = ROOT / "assets-local" / "native-menus" / "NtMemAc.usd"
        if not icons.is_file() or not scene.is_file():
            return
        result = subprocess.run(
            [str(node_runtime()), str(target), "--assets", str(icons), str(scene)],
            cwd=ROOT, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("typed CSS card support roots", result.stdout)


if __name__ == "__main__":
    unittest.main()
