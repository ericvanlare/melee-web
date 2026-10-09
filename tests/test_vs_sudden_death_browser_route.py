"""Asset-free actual SD browser recipe bounds, dispatch, and cleanup controls."""
from pathlib import Path
import subprocess
import sys
import unittest
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from check_gameplay import node_runtime
class VsSuddenDeathBrowserRouteTests(unittest.TestCase):
    def test_actual_recipe_controls(self):
        result = subprocess.run([str(node_runtime(ROOT)), '--test',
            str(ROOT / 'tests/vs_sudden_death_browser_route.test.mjs'),
            str(ROOT / 'tests/runtime_owner_observation.test.mjs')],
            cwd=ROOT, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
