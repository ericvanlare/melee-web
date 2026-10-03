"""Compile the source match-rules guard without private game assets."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class GameplayMatchItemMaskTests(unittest.TestCase):
    def test_custom_item_mask_is_limited_to_items_disabled_by_source_frequency(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler:
            self.skipTest("a C compiler is required")
        with tempfile.TemporaryDirectory(prefix="melee match item rules ") as directory:
            output = Path(directory) / "gameplay_match_item_mask_trace"
            command = [
                compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-variable", "-DAURORA", "-DTARGET_PC",
                "-I", str(ROOT / "src"),
                "-I", str(ROOT / "build/gameplay-source/src"),
                "-I", str(ROOT / ".deps/aurora/include"),
                "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                "-O1", "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                "-include", str(ROOT / "src/gameplay_compat.h"),
                str(ROOT / "src/gameplay_match_rules.c"),
                str(ROOT / "tests/gameplay_match_item_mask_trace.c"),
                "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                "-o", str(output),
            ]
            result = subprocess.run(command, cwd=ROOT, capture_output=True,
                                    text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            run = subprocess.run([str(output)], cwd=ROOT, capture_output=True,
                                 text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("source match rules preserve item masks", run.stdout)


if __name__ == "__main__":
    unittest.main()
