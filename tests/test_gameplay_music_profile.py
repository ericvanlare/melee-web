"""Focused real-source music/profile ownership regression with private assets."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime


class MusicProfileTests(unittest.TestCase):
    def test_source_music_rng_and_profile_lifetime(self):
        target = ROOT / 'build/browser-release/gameplay_music_profile_trace.js'
        menu, game = ROOT / 'assets-local/native-menus', ROOT / 'assets-local/next-gate'
        if not target.is_file() or not all((game / name).is_file() for name in
                ('PlFe.dat', 'PlDr.dat', 'hyaku2.hps')) or not (menu / 'MnSlChr.usd').is_file():
            self.skipTest('Build gameplay_music_profile_trace and provide owned menu/game assets')
        result = subprocess.run([str(node_runtime()), str(target), str(menu), str(game)],
                                cwd=ROOT, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-6000:])
        self.assertIn('Original music profiles', result.stdout)
