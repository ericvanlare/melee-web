"""Compile the patched retail knockback bodies in a minimal Wasm harness."""
from __future__ import annotations

import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from check_gameplay import node_runtime  # noqa: E402


def _extract_function(source: str, name: str) -> str:
    signature = re.search(
        r"(?:(?:static\s+inline\s+s32)|(?:static\s+float)|(?:float))\s+"
        + re.escape(name) + r"\s*\(", source)
    if signature is None:
        raise AssertionError(f"patched source does not define {name}")
    brace = source.index("{", signature.end())
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[signature.start():index + 1]
    raise AssertionError(f"unterminated patched function {name}")


def _patched_function_text(directory: Path) -> str:
    """Apply the production patch, then extract its actual function bodies."""
    source = directory / "src" / "melee" / "ft" / "ftcoll.c"
    source.parent.mkdir(parents=True)
    source.write_bytes((ROOT / ".deps/melee/src/melee/ft/ftcoll.c").read_bytes())
    patch_text = (ROOT / "patches/melee-gameplay.patch").read_text()
    result = subprocess.run(
        ["git", "apply", "--include=src/melee/ft/ftcoll.c"], cwd=directory,
        input=patch_text, text=True, capture_output=True, timeout=30,
    )
    if result.returncode != 0:
        raise AssertionError(result.stdout + result.stderr)
    patched = source.read_text()
    return "\n\n".join(_extract_function(patched, name) for name in (
        "ftColl_GetDamageCount", "ftColl_CalcKnockback", "ftColl_80079AB0",
    ))


class GameplayKnockbackTests(unittest.TestCase):
    def test_patched_helper_and_ab0_preserve_retail_float_boundaries(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        config = sdk / ".emscripten"
        if not emcc.is_file() or not config.is_file():
            self.skipTest("Project-local Emscripten SDK unavailable")
        if shutil.which("git") is None:
            self.skipTest("git utility unavailable")

        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config),
                   EMSDK_PYTHON=sys.executable)
        with tempfile.TemporaryDirectory(prefix="melee-knockback-") as directory:
            directory = Path(directory)
            functions = _patched_function_text(directory)
            harness = (ROOT / "tests/gameplay_knockback_trace.c").read_text()
            harness = harness.replace("/* PATCHED_KNOCKBACK_FUNCTIONS */", functions)
            source = directory / "knockback.c"
            source.write_text(harness)
            output = directory / "knockback.js"
            result = subprocess.run([
                sys.executable, str(emcc), "-O2", "-std=c11",
                "-ffp-contract=off", str(source), "-sENVIRONMENT=node",
                "-sEXIT_RUNTIME=1", "-o", str(output),
            ], env=env, cwd=directory, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run(
                [str(node_runtime()), str(output)], cwd=directory,
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        self.assertEqual(result.stdout.strip().splitlines(), [
            "set 41bccccd 41bccccc",
            "ordinary-cap 451c4000",
            "ratios 41b0b780",
        ])


if __name__ == "__main__":
    unittest.main()
