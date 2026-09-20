"""Focused real-archive Article construction for Pikachu and Pichu."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class PikachuArticleTests(unittest.TestCase):
    def test_real_article_owners(self):
        pikachu = ROOT / "assets-local/full-game-pikachu/PlPk.dat"
        pichu = ROOT / "assets-local/full-game-pichu/PlPc.dat"
        if not pikachu.is_file() or not pichu.is_file():
            self.skipTest("Owned Pikachu and Pichu fighter archives are required")

        targets = [
            ROOT / "build" / directory / "gameplay_pikachu_articles_trace.js"
            for directory in ("browser", "browser-release")
        ]
        targets = [path for path in targets if path.is_file()]
        if not targets:
            self.skipTest("Build the Pikachu Article trace target")
        target = max(targets, key=lambda path: path.stat().st_mtime)
        result = subprocess.run(
            [str(node_runtime()), str(target), str(pikachu), str(pichu)],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=180,
        )
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])
        self.assertIn(
            "Pikachu/Pichu six Article schemas hydrated and torn down; "
            "undersized Thunder special rejected",
            result.stdout,
        )


if __name__ == "__main__":
    unittest.main()
