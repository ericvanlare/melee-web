"""Run the bounded Stadium C1a CSS-to-SSS source/input contract."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class StadiumC1aCssSssReducerTests(unittest.TestCase):
    def test_css_sss_readiness_start_and_transition_contract(self):
        result = subprocess.run(
            [str(node_runtime()), "--test",
             str(ROOT / "tests/stadium_c1a_css_sss_reducer_contract_test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
