"""Asset-free shared CSS choreography and callback recorder controls."""
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class GameplaySdBrowserHelpersTests(unittest.TestCase):
    def test_source_inputs_and_bounded_callback_capture(self):
        result = subprocess.run([str(node_runtime(ROOT)), "--test",
            str(ROOT / "tests/gameplay_sd_browser_helpers.test.mjs")], cwd=ROOT,
            capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)


if __name__ == "__main__":
    unittest.main()
