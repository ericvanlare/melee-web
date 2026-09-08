"""Check PowerPC reciprocal-square-root refinement on the Wasm runtime."""
import ast
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources


def original_function(text, name):
    """Extract one complete original C function, preserving its body."""
    match = re.search(
        r"(?m)^static inline float\s+" + re.escape(name) + r"\s*\([^;]*?\)\s*\{",
        text,
    )
    if match is None:
        raise AssertionError("Missing original function " + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


class GameplayIntrinsicTests(unittest.TestCase):
    def test_original_sqrt_refinement_uses_reciprocal_estimate(self):
        sdk = ROOT / ".deps/emsdk"
        compiler = sdk / "upstream/emscripten"
        config = sdk / ".emscripten"
        if not (compiler / "emcc.py").is_file() or not config.is_file():
            self.skipTest("Project SDK unavailable; run scripts/bootstrap.py")

        node_setting = next(
            ast.literal_eval(statement.value)
            for statement in ast.parse(config.read_text()).body
            if isinstance(statement, ast.Assign)
            and any(
                isinstance(target, ast.Name) and target.id == "NODE_JS"
                for target in statement.targets
            )
        )
        node = Path(node_setting.replace("$CFGDIR", str(sdk))).resolve()
        self.assertTrue(node.is_relative_to(sdk.resolve()))
        env = dict(
            os.environ,
            EMSDK=str(sdk),
            EM_CONFIG=str(config),
            EM_CACHE=str(compiler / "cache"),
            EMSDK_PYTHON=sys.executable,
        )

        # The test intentionally compiles the same extracted original function
        # twice.  Only the include root changes: the generated source carries
        # the TARGET_PC frsqrte adapter, while .deps/melee remains the broken
        # sqrt(x) placeholder used as a negative control.
        source = prepare_sources(ROOT)
        original = ROOT / ".deps/melee/src"
        itmaplib = (original / "melee/it/itmaplib.c").read_text()
        function = original_function(itmaplib, "sqrtf_accurate_local")
        self.assertIn("__frsqrte", function)
        self.assertIn("#define __frsqrte(x) frsqrte(x)", (source / "placeholder.h").read_text())

        harness = r'''
#include <math.h>
#include <stdio.h>
#include "placeholder.h"

''' + function + r'''

int main(void)
{
    static const float inputs[] = {2.3f, 25.0f, 1000000.0f, 0.04f, 0.0f};
    for (unsigned int i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        printf("%u %.9g\n", i, (double)sqrtf_accurate_local(inputs[i]));
    }
    return 0;
}
'''
        values = (2.3, 25.0, 1000000.0, 0.04, 0.0)
        expected = [math.sqrt(f32(value)) for value in values]

        with tempfile.TemporaryDirectory(prefix="melee gameplay intrinsics ") as directory:
            directory = Path(directory)
            outputs = {}
            for label, include_root in (("patched", source), ("broken", original)):
                unit = directory / (label + ".c")
                unit.write_text(harness)
                output = directory / (label + ".js")
                command = [
                    sys.executable,
                    str(compiler / "emcc.py"),
                    "-O1",
                    "-std=c11",
                    "-DTARGET_PC",
                    "-ffp-contract=off",
                    "-I",
                    str(include_root),
                    "-I",
                    str(ROOT / ".deps/aurora/include"),
                    str(unit),
                    "-sENVIRONMENT=node",
                    "-sEXIT_RUNTIME=1",
                    "-o",
                    str(output),
                ]
                result = subprocess.run(
                    command,
                    cwd=directory,
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run(
                    [str(node), str(output)],
                    cwd=directory,
                    env=env,
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                rows = result.stdout.strip().splitlines()
                self.assertEqual(len(rows), len(values), result.stdout)
                outputs[label] = [float(row.split()[1]) for row in rows]

            for value, actual in zip(expected, outputs["patched"]):
                with self.subTest(value=value):
                    self.assertTrue(math.isfinite(actual))
                    self.assertTrue(
                        math.isclose(actual, value, rel_tol=3e-6, abs_tol=3e-7),
                        (value, actual),
                    )

            broken_mismatches = sum(
                not math.isfinite(actual)
                or not math.isclose(actual, value, rel_tol=3e-6, abs_tol=3e-7)
                for value, actual in zip(expected[:-1], outputs["broken"][:-1])
            )
            self.assertEqual(
                broken_mismatches,
                len(expected) - 1,
                outputs["broken"],
            )


if __name__ == "__main__":
    unittest.main()
