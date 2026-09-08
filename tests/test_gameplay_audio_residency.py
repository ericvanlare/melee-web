"""Compile and run the standalone native SSM registry boundary test."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayAudioResidencyTests(unittest.TestCase):
    def test_registry_validation_and_optional_local_banks(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C11 compiler is required for the native registry test")

        with tempfile.TemporaryDirectory(prefix="melee audio residency ") as directory:
            binary = Path(directory) / "gameplay_audio_residency_test"
            result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-I", str(ROOT / "src"),
                 str(ROOT / "src" / "gameplay_audio_residency.c"),
                 str(ROOT / "tests" / "gameplay_audio_residency_test.c"),
                 "-o", str(binary)],
                cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            banks = [ROOT / "assets-local" / "native-menus" / name for name in (
                "nr_select.ssm", "nr_title.ssm", "nr_name.ssm",
                "pokemon.ssm", "end.ssm",
            )]
            arguments = [str(binary), *(str(path) for path in banks if path.is_file())]
            result = subprocess.run(
                arguments, cwd=ROOT, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Audio SSM registry path, descriptor, and bounded transport checks passed",
                          result.stdout)


if __name__ == "__main__":
    unittest.main()
