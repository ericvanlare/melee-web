"""Focused policy controls for the bounded P2 combat probe."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime

RUNTIME_ROOT = Path(os.environ.get("MELEE_TEST_RUNTIME_ROOT", ROOT)).resolve()


class A3LocalRoomFullRouteCombatTests(unittest.TestCase):
    def test_bounded_combat_probe_policy_controls(self):
        result = subprocess.run(
            [str(node_runtime(RUNTIME_ROOT)), str(ROOT / "tests/a3_local_room_full_route_combat.test.mjs")],
            cwd=ROOT,
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("P2 approach accepts only bounded positive-gap tap hypotheses", result.stdout)
        self.assertIn("paired jab requires Attack11 and an increased victim damage reaction", result.stdout)
        self.assertIn("combat action retains complete inspected native pair", result.stdout)
        self.assertIn("each approach checksum poll retains both native cursors", result.stdout)


if __name__ == "__main__":
    unittest.main()
