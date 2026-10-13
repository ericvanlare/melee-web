from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class A3LocalRoomRebirthWaitTests(unittest.TestCase):
    def test_retained_first_use_queue_empty_controls(self):
        node = shutil.which("node")
        self.assertIsNotNone(node, "Node.js is required for retained first-use wait controls")
        result = subprocess.run(
            [node, "--test", str(ROOT / "tests/a3_local_room_full_route_rebirth_wait.test.mjs")],
            cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == "__main__":
    unittest.main()
