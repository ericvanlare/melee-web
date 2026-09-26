"""Focused real-archive ownership for Ground's Random Pokémon state tail."""

from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class RandomArticleTests(unittest.TestCase):
    def test_real_random_article_owns_stage_state_tail(self):
        asset = ROOT / "assets-local" / "session-equivalence" / "ItCo.usd"
        if not asset.is_file():
            self.skipTest("Owned ItCo.usd is required")
        target = ROOT / "build" / "browser" / "gameplay_random_article_trace.js"
        if not target.is_file():
            self.skipTest("Build the Random Article trace target")
        result = subprocess.run(
            [str(node_runtime()), str(target), str(asset)],
            cwd=ROOT, capture_output=True, text=True, timeout=180,
        )
        self.assertEqual(result.returncode, 0,
                         (result.stdout + result.stderr)[-6000:])
        self.assertIn("Ground ALDYakuAll rows writable", result.stdout)


if __name__ == "__main__":
    unittest.main()
