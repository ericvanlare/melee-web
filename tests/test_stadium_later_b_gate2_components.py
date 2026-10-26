"""Register focused Gate2 shell, consumer, and adapter controls with unittest."""
import os
from pathlib import Path
import subprocess
import sys
import unittest

SOURCE_ROOT = Path(__file__).resolve().parents[1]
ROOT = Path(os.environ.get("MELEE_LATER_B_SOURCE_ROOT", SOURCE_ROOT)).resolve()
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime


class StadiumLaterBGate2ComponentsTests(unittest.TestCase):
    """Run each focused JavaScript component suite with the project-pinned Node."""

    TIMEOUT_SECONDS = 60

    def run_component_suite(self, name):
        test_path = ROOT / "tests" / name
        result = subprocess.run(
            [str(node_runtime(ROOT)), "--test", str(test_path)],
            cwd=ROOT,
            env=os.environ.copy(),
            capture_output=True,
            text=True,
            timeout=self.TIMEOUT_SECONDS,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"{name} failed with exit {result.returncode}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )

    def test_gate2_shell_controls(self):
        self.run_component_suite("stadium_later_b_gate2_shell.test.mjs")

    def test_gate2_consumer_controls(self):
        self.run_component_suite("stadium_later_b_gate2_consumer.test.mjs")

    def test_gate2_adapter_controls(self):
        self.run_component_suite("stadium_later_b_gate2_adapter.test.mjs")


if __name__ == "__main__":
    unittest.main()
