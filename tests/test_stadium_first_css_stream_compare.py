"""Run the private CSS stream comparator controls in the full suite."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class StadiumFirstCssStreamComparatorTests(unittest.TestCase):
    def test_strict_state_comparison_and_terminal_stops(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "tests/stadium_first_css_stream_compare.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
