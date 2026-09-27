from pathlib import Path
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameCubeSaveTests(unittest.TestCase):
    def test_node_codec(self):
        subprocess.run(
            ["node", str(ROOT / "tests" / "gamecube_save_test.mjs")],
            cwd=ROOT, check=True, capture_output=True, text=True,
        )


if __name__ == "__main__":
    unittest.main()
