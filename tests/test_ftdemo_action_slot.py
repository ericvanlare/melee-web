"""Guard the native demo fighter's owned animation-slot sentinel.

The demo initializer adapts ``plAllocInfo2`` to the original
``Fighter_UnkInitLoad_80068914`` input.  The original input has one additional
byte, ``x5``, which is copied to ``Fighter.x61C`` and later indexes the legacy
animation cache.  Native demo fighters own a separate action domain, so that
byte must carry the source no-slot sentinel (-1), rather than stack garbage.

This test extracts the real initializer from the prepared source and compiles
it with a mock loader.  Clang's patterned automatic initialization makes the
unassigned byte deterministic, and compiling the pristine source alongside the
prepared source proves that the assertion detects the historical omission.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


def _extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
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


def _compile_and_run(compiler: str, function: str, directory: Path,
                     feature_enabled: bool):
    harness = f"""
#include <stdint.h>
#include <stdio.h>

typedef signed char s8;
typedef unsigned char u8;
typedef int FighterKind;
typedef struct HSD_GObj HSD_GObj;

typedef struct plAllocInfo2 {{
    FighterKind internal_id;
    u8 slot;
    int unk8;
    u8 b0 : 1;
    u8 has_transformation : 1;
    u8 b2 : 1;
    u8 b3 : 1;
    u8 b4 : 1;
    u8 b5 : 1;
    u8 b6 : 1;
    u8 b7 : 1;
}} plAllocInfo2;

struct plAllocInfo {{
    FighterKind internal_id;
    u8 slot;
    s8 x5;
    u8 b0 : 1;
    u8 has_transformation : 1;
    u8 b2 : 1;
    u8 b3 : 1;
    u8 b4 : 1;
    u8 b5 : 1;
    u8 b6 : 1;
    u8 b7 : 1;
}};

static int failures;

void Fighter_UnkInitLoad_80068914(HSD_GObj* gobj, struct plAllocInfo* arg)
{{
    (void) gobj;
    if (arg->internal_id != 7 || arg->slot != 3 || arg->b0 != 1 ||
        arg->x5 != -1) {{
        fprintf(stderr, "loader saw kind=%d slot=%u b0=%u x5=%d\\n",
                arg->internal_id, arg->slot, arg->b0, arg->x5);
        failures++;
    }}
}}

{function}

int main(void)
{{
    plAllocInfo2 input = {{0}};
    input.internal_id = 7;
    input.slot = 3;
    input.has_transformation = 1;
    initFighter((HSD_GObj*) (uintptr_t) 1, &input);
    return failures;
}}
"""
    source = directory / ("ftdemo_fixed.c" if feature_enabled else "ftdemo_original.c")
    binary = directory / ("ftdemo_fixed" if feature_enabled else "ftdemo_original")
    source.write_text(harness)
    command = [
        compiler,
        "-std=c11",
        "-O0",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-ftrivial-auto-var-init=pattern",
    ]
    if feature_enabled:
        command.append("-DMELEE_WEB_GAMEPLAY")
    command.extend([str(source), "-o", str(binary)])
    compiled = subprocess.run(command, capture_output=True, text=True, timeout=30)
    if compiled.returncode != 0:
        return compiled, None
    return compiled, subprocess.run(
        [str(binary)], capture_output=True, text=True, timeout=10
    )


class FtdemoActionSlotTests(unittest.TestCase):
    def test_prepared_demo_initializer_forwards_no_legacy_slot(self):
        compiler = shutil.which("clang")
        if compiler is None:
            self.skipTest("clang is required for the extracted-source regression")

        original_path = ROOT / ".deps/melee/src/melee/ft/ftdemo.c"
        patch_path = ROOT / "patches/melee-gameplay.patch"
        self.assertTrue(original_path.is_file(), original_path)
        self.assertTrue(patch_path.is_file(), patch_path)

        with tempfile.TemporaryDirectory(prefix="ftdemo-action-slot-") as name:
            directory = Path(name)
            source_path = directory / "src/melee/ft/ftdemo.c"
            source_path.parent.mkdir(parents=True)
            source_path.write_text(original_path.read_text())
            applied = subprocess.run(
                ["git", "apply", "--include=src/melee/ft/ftdemo.c", str(patch_path)],
                cwd=directory,
                capture_output=True,
                text=True,
            )
            self.assertEqual(applied.returncode, 0, applied.stdout + applied.stderr)

            original = _extract_function(
                original_path.read_text(), "static void initFighter("
            )
            prepared = _extract_function(
                source_path.read_text(), "static void initFighter("
            )
            self.assertNotIn("temp1.x5", original)
            self.assertIn("temp1.x5 = -1", prepared)

            baseline_compile, baseline = _compile_and_run(
                compiler, original, directory, False
            )
            self.assertEqual(baseline_compile.returncode, 0,
                             baseline_compile.stdout + baseline_compile.stderr)
            self.assertIsNotNone(baseline)
            self.assertEqual(baseline.returncode, 1,
                             baseline.stdout + baseline.stderr)
            self.assertRegex(
                baseline.stderr,
                r"loader saw kind=7 slot=3 b0=1 x5=-86(?:\n|$)",
            )

            fixed_compile, fixed = _compile_and_run(
                compiler, prepared, directory, True
            )
            self.assertEqual(fixed_compile.returncode, 0,
                             fixed_compile.stdout + fixed_compile.stderr)
            self.assertIsNotNone(fixed)
            self.assertEqual(fixed.returncode, 0,
                             fixed.stdout + fixed.stderr)


if __name__ == "__main__":
    unittest.main()
