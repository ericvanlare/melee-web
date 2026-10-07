from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class StadiumE8ObserverTests(unittest.TestCase):
    def test_bounded_wrappers_preserve_exact_names_and_delegate(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "stadium-e8-observer"
            subprocess.run(
                [
                    shutil.which("clang") or "cc",
                    "-std=gnu11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "src"),
                    "-I",
                    str(ROOT / "tests"),
                    str(ROOT / "tests/stadium_c1_e8_call_observer.c"),
                    str(ROOT / "tests/stadium_c1_e8_call_observer_test.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            result = subprocess.run(
                [str(binary)], check=False, capture_output=True, text=True,
                timeout=10,
            )
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
