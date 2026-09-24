"""Focused source-layout regression for the original Results tables."""

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class GameplayResultsLayoutTests(unittest.TestCase):
    def test_typed_aliases_and_authored_halfwords(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten/emcc.py"
        config = sdk / ".emscripten"
        if not compiler.is_file() or not config.is_file():
            self.skipTest("Project-local Emscripten SDK unavailable")

        sys.path.insert(0, str(ROOT / "scripts"))
        from gameplay_sources import prepare_sources

        source = prepare_sources(ROOT)
        emcc = [sys.executable, str(compiler)]
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config), EMSDK_PYTHON=sys.executable)
        common = [
            "-O1", "-std=c11", "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY",
            "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
            "-I", str(ROOT / "src"), "-I", str(source),
            "-I", str(ROOT / ".deps/aurora/include"),
            "-include", str(ROOT / "src/gameplay_compat.h"),
        ]
        source_files = [
            source / "melee/gm/gm_1798.c",
            source / "melee/gm/gmresultplayer.c",
            ROOT / "tests/gameplay_results_layout_trace.c",
        ]
        with tempfile.TemporaryDirectory(prefix="melee-results-layout-") as directory_name:
            directory = Path(directory_name)
            objects = []
            for index, source_file in enumerate(source_files):
                output = directory / f"source-{index}.o"
                result = subprocess.run(
                    emcc + common + ["-c", str(source_file), "-o", str(output)],
                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(output)
            output = directory / "results-layout.js"
            result = subprocess.run(
                emcc + ["-O1", "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1",
                        "-Wl,--gc-sections",
                        *map(str, objects), "-o", str(output)],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=120,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            from check_gameplay import node_runtime
            result = subprocess.run(
                [str(node_runtime(ROOT)), str(output)],
                cwd=ROOT, env=env, capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Results typed aggregate aliases and authored halfword tables: passed",
                          result.stdout)


if __name__ == "__main__":
    unittest.main()
