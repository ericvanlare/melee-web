"""Compile Bowser's special-hi callbacks with their retail event signature."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources  # noqa: E402


def function_body(source: str, name: str) -> str:
    match = re.search(r"\bvoid\s+" + re.escape(name) + r"\s*\([^)]*\)\s*\{", source)
    if match is None:
        raise AssertionError(f"patched source does not define {name}")
    depth = 0
    for index in range(source.index("{", match.start()), len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[match.start():index + 1]
    raise AssertionError(f"unterminated patched function {name}")


class KoopaCallbackAbiTests(unittest.TestCase):
    def test_damage_and_death_callbacks_use_hsd_event_abi(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        config = sdk / ".emscripten"
        if not emcc.is_file() or not config.is_file():
            self.skipTest("Project-local Emscripten SDK unavailable")
        if shutil.which("git") is None:
            self.skipTest("git utility unavailable")

        source_root = prepare_sources(ROOT)
        koopa = source_root / "melee/ft/kinds/ftKoopa"
        callback = "ftKp_Init_80132B38"
        definition = function_body((koopa / "ftkoopa.c").read_text(), callback)
        special = (koopa / "ftkoopaspecialhi.c").read_text()
        assignments = re.findall(
            r"fp->(?:take_dmg_cb|death2_cb)\s*=\s*[^;]+;", special
        )
        self.assertEqual(len(assignments), 2)
        self.assertTrue(all(re.fullmatch(
            r"fp->(?:take_dmg_cb|death2_cb)\s*=\s*" + callback + r"\s*;", line
        ) for line in assignments), assignments)

        harness = """\
typedef struct HSD_GObj { int unused; } HSD_GObj;
typedef struct Fighter {
    void (*take_dmg_cb)(HSD_GObj*);
    void (*death2_cb)(HSD_GObj*);
} Fighter;
""" + definition + "\nvoid assign_callbacks(Fighter* fp) {\n" + "\n".join(assignments) + "\n}\n"
        with tempfile.TemporaryDirectory(prefix="melee-koopa-callback-abi-") as directory:
            source = Path(directory) / "koopa_callback_abi.c"
            source.write_text(harness)
            env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(config), EMSDK_PYTHON=sys.executable)
            result = subprocess.run([
                sys.executable, str(emcc), "-std=c11", "-Wall", "-Werror",
                "-Werror=incompatible-function-pointer-types", "-fsyntax-only", str(source),
            ], env=env, cwd=directory, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
