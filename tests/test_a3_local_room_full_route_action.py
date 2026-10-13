import subprocess
import sys
import unittest
from pathlib import Path

# Focused controls for the optional full-route gameplay action window.
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class FullRouteActionWindowTests(unittest.TestCase):
    def test_literal_jab_and_jump_windows(self):
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test",
             str(ROOT / "tests/a3_local_room_full_route_action.test.mjs"),
             str(ROOT / "tests/a3_action_animation_observation.test.mjs")],
            cwd=ROOT, capture_output=True, text=True, timeout=20,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
