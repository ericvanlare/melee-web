"""Compile the patched effect parameter setters against non-adjacent globals."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def patch_block(patch_text: str, path: str) -> str:
    marker = f"diff --git a/{path} b/{path}"
    start = patch_text.find(marker)
    if start < 0:
        raise AssertionError(f"reviewed patch has no hunk for {path}")
    end = patch_text.find("\ndiff --git ", start + len(marker))
    return patch_text[start:] if end < 0 else patch_text[start:end]


def extract_function(source: str, name: str) -> str:
    start = source.index(f"void {name}(")
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


class EfLibParamTableTests(unittest.TestCase):
    def test_setters_compile_from_patch_and_do_not_use_queue_adjacency(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if compiler is None:
            self.skipTest("A C compiler is required")

        original = ROOT / ".deps/melee/src/melee/ef/eflib.c"
        patch = ROOT / "patches/melee-gameplay.patch"
        fixture = ROOT / "tests/eflib_param_table_trace.c"
        self.assertTrue(original.is_file(), original)
        self.assertTrue(patch.is_file(), patch)

        with tempfile.TemporaryDirectory(prefix="eflib-param-table-") as directory:
            directory = Path(directory)
            source_path = directory / "src/melee/ef/eflib.c"
            source_path.parent.mkdir(parents=True)
            source_path.write_text(original.read_text())
            diff = patch_block(patch.read_text(), "src/melee/ef/eflib.c")
            diff_path = directory / "eflib.patch"
            diff_path.write_text(diff)
            applied = subprocess.run(
                ["git", "apply", "--unsafe-paths", str(diff_path)],
                cwd=directory,
                capture_output=True,
                text=True,
            )
            self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)
            patched = source_path.read_text()

            setters = []
            for name in ("efLib_SetParamAlpha", "efLib_SetParamGfxId"):
                function = extract_function(patched, name)
                self.assertIn("efLib_ParamTable", function)
                self.assertNotIn("efLib_AnimQueue", function)
                setters.append(function)

            generated = fixture.read_text().replace(
                "/* EFLIB_PARAM_SETTERS */",
                "\n\n".join(setters),
            )
            generated_path = directory / "eflib_param_table_trace.c"
            binary = directory / "eflib_param_table_trace"
            generated_path.write_text(generated)
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-O0", str(generated_path), "-o", str(binary)],
                cwd=directory,
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0,
                             compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("preserve queue and update ParamTable: passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
