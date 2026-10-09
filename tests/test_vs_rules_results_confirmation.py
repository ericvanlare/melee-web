"""Focused source-observed two-Human Results browser-driver controls."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class VsRulesResultsConfirmationTests(unittest.TestCase):
    def test_source_phase_and_port_confirmation_driver(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test",
            str(ROOT / "tests/vs_rules_results_confirmation_driver.test.mjs")], cwd=ROOT,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
