"""Compile the dependency-light whole-session completion policy regression."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ReplayCompletionPolicyTests(unittest.TestCase):
    def test_final_results_or_prize_requires_css_transition_and_no_extra_source_step(self):
        compiler = shutil.which("clang++") or shutil.which("c++")
        self.assertIsNotNone(compiler, "A C++20 compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee replay completion ") as directory:
            binary = Path(directory) / "replay_completion_policy"
            built = subprocess.run(
                [
                    compiler,
                    "-std=c++20",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-O1",
                    "-I",
                    str(ROOT / "src"),
                    str(ROOT / "tests/gameplay_replay_completion_policy_test.cpp"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                timeout=30,
            )
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
