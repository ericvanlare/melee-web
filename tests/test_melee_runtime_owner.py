"""Exercise shared player lifecycle without claiming native gameplay evidence."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime

class SharedRuntimeOwnerTests(unittest.TestCase):
    def test_native_command_audio_and_teardown_boundaries(self):
        result = subprocess.run([str(node_runtime()), str(ROOT / 'tests/melee_runtime_owner_test.mjs')],
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('repeat launch and reload-only destruction pass', result.stdout)
