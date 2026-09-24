"""Original ECB and Roy joint matrices retain both fused SRT regressions."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/gameplay_srt_scalars.json"
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources
from check_gameplay import node_runtime


def _words(value, count):
    if value is None:
        return None
    if not isinstance(value, list) or len(value) != count:
        raise ValueError("fixture word count is invalid")
    if any(not isinstance(word, str) or len(word) != 8 for word in value):
        raise ValueError("fixture word is invalid")
    return [int(word, 16) for word in value]


def _c_words(words):
    return "{" + ", ".join(f"0x{word:08x}u" for word in words) + "}"


def _fixture():
    data = json.loads(FIXTURE.read_text(encoding="utf-8"))
    if data.get("schema") != "melee-web-gameplay-srt-scalar-fixture":
        raise ValueError("unexpected SRT fixture schema")
    instruction = data.get("source_instruction")
    if instruction.get("routine") != "HSD_MtxSRT":
        raise ValueError("fixture instruction provenance is invalid")
    cases = []
    for item in data.get("cases", []):
        case = {
            "index": len(cases),
            "scale": _words(item["scale_bits"], 3),
            "rotation": _words(item["rotation_bits"], 4),
            "translation": _words(item["translation_bits"], 3),
            "parent": _words(item["parent_matrix_bits"], 12),
            "parent_scale": _words(item.get("parent_scale_bits"), 3),
            "expected": _words(item["expected_world_bits"], 12),
            "negative": _words(item["negative_world_bits"], 12),
        }
        if case["expected"] == case["negative"]:
            raise ValueError("fixture case lacks a negative control")
        cases.append(case)
    if len(cases) != data["capture_provenance"]["eligible_rows"]:
        raise ValueError("fixture eligibility count is inconsistent")
    if not cases:
        raise ValueError("SRT fixture is empty")
    return data, cases


def _runner_source(cases):
    rows = []
    for case in cases:
        rows.append(
            "    {.index=%d, .scale=%s, .rotation=%s, .translation=%s, "
            ".parent=%s, .parent_scale=%s, .has_parent_scale=%d, .expected=%s, "
            ".negative=%s}," % (
                case["index"], _c_words(case["scale"]),
                _c_words(case["rotation"]), _c_words(case["translation"]),
                _c_words(case["parent"]),
                _c_words(case["parent_scale"] or (0, 0, 0)),
                1 if case["parent_scale"] is not None else 0,
                _c_words(case["expected"]), _c_words(case["negative"])))
    return r'''#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dolphin/mtx.h>
#include <sysdolphin/baselib/mtx.h>

void melee_web_ps_mtx_concat(const Mtx a, const Mtx b, Mtx out);

typedef struct {
    unsigned index;
    uint32_t scale[3];
    uint32_t rotation[4];
    uint32_t translation[3];
    uint32_t parent[12];
    uint32_t parent_scale[3];
    unsigned has_parent_scale;
    uint32_t expected[12];
    uint32_t negative[12];
} SrtCase;

static float from_bits(uint32_t value)
{
    float result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static const SrtCase cases[] = {
__ROWS__
};

static void emit_matrix(const Mtx value)
{
    unsigned row;
    unsigned column;
    for (row = 0; row < 3; row++) {
        for (column = 0; column < 4; column++) {
            printf("%s%08x", row == 0 && column == 0 ? "" : " ",
                   bits(value[row][column]));
        }
    }
}

int main(void)
{
    unsigned index;
    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        const SrtCase* item = &cases[index];
        Vec3 scale = {from_bits(item->scale[0]), from_bits(item->scale[1]),
                      from_bits(item->scale[2])};
        Vec3 rotation = {from_bits(item->rotation[0]),
                         from_bits(item->rotation[1]),
                         from_bits(item->rotation[2])};
        Vec3 translation = {from_bits(item->translation[0]),
                            from_bits(item->translation[1]),
                            from_bits(item->translation[2])};
        Vec3 parent_scale;
        Mtx parent;
        Mtx local;
        Mtx world;
        unsigned row;
        unsigned column;
        for (row = 0; row < 3; row++) {
            for (column = 0; column < 4; column++) {
                parent[row][column] =
                    from_bits(item->parent[row * 4 + column]);
            }
        }
        parent_scale.x = from_bits(item->parent_scale[0]);
        parent_scale.y = from_bits(item->parent_scale[1]);
        parent_scale.z = from_bits(item->parent_scale[2]);
        HSD_MtxSRT(local, &scale, &rotation, &translation,
                   item->has_parent_scale ? &parent_scale : NULL);
        melee_web_ps_mtx_concat(parent, local, world);
        printf("%u\t", item->index);
        emit_matrix(world);
        putchar('\n');
    }
    return 0;
}
'''.replace("__ROWS__", "\n".join(rows))


class GameplaySrtTests(unittest.TestCase):
    def _run(self, source, cases, *, pristine=False):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-fno-builtin-sinf", "-fno-builtin-cosf", "-fno-builtin-tanf",
                  "-fno-builtin-atanf", "-fno-builtin-atan2f", "-fno-builtin-acosf",
                  "-ffunction-sections", "-fdata-sections", "-ffp-contract=off",
                  "-DTARGET_PC", "-DMELEE_WEB_GAMEPLAY",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(source / "sysdolphin/baselib"),
                  "-I", str(source / "melee/lb"),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h")]
        files = [source / "sysdolphin/baselib/mtx.c",
                 source / "MSL/trigf.c", source / "MSL/math_data.c",
                 source / "MSL/float.c", ROOT / "src/gameplay_ps_math.c"]
        with tempfile.TemporaryDirectory(prefix="melee-srt-") as directory:
            if pristine:
                control = Path(directory) / "mtx.c"
                control.write_text(
                    (ROOT / ".deps/melee/src/sysdolphin/baselib/mtx.c").read_text(),
                    encoding="utf-8")
                files[0] = control
            target = Path(directory) / "srt.js"
            runner = Path(directory) / "srt.c"
            runner.write_text(_runner_source(cases), encoding="utf-8")
            result = subprocess.run(
                common + [str(runner)] + [str(path) for path in files] + [
                    "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-sASSERTIONS=2",
                    "-sSAFE_HEAP=1", "-o", str(target)],
                env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime(ROOT)), str(target)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return [line.split("\t", 1)[1] for line in result.stdout.splitlines()]

    def _run_native(self, source, cases, *, pristine=False):
        compiler = shutil.which("clang")
        if compiler is None:
            self.skipTest("clang unavailable for optional native scalar check")
        common = [compiler, "-O2", "-std=c11", "-fno-builtin-sinf",
                  "-fno-builtin-cosf", "-fno-builtin-tanf", "-ffunction-sections",
                  "-fdata-sections", "-ffp-contract=off", "-DTARGET_PC",
                  "-DMELEE_WEB_GAMEPLAY", "-I", str(ROOT / "src"), "-I",
                  str(source), "-I", str(source / "sysdolphin/baselib"), "-I",
                  str(source / "melee/lb"), "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h")]
        files = [source / "sysdolphin/baselib/mtx.c",
                 source / "MSL/trigf.c", source / "MSL/math_data.c",
                 source / "MSL/float.c", ROOT / "src/gameplay_ps_math.c"]
        linker_gc = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
        with tempfile.TemporaryDirectory(prefix="melee-srt-native-") as directory:
            if pristine:
                control = Path(directory) / "mtx.c"
                control.write_text(
                    (ROOT / ".deps/melee/src/sysdolphin/baselib/mtx.c").read_text(),
                    encoding="utf-8")
                files[0] = control
            target = Path(directory) / "srt"
            runner = Path(directory) / "srt.c"
            runner.write_text(_runner_source(cases), encoding="utf-8")
            result = subprocess.run(
                common + [str(runner)] + [str(path) for path in files] +
                [linker_gc, "-lm", "-o", str(target)],
                capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(target)], capture_output=True, text=True,
                                    timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return [line.split("\t", 1)[1] for line in result.stdout.splitlines()]

    def test_transformed_srt_matches_original_and_pristine_is_negative(self):
        fixture, cases = _fixture()
        transformed = prepare_sources()
        expected = [" ".join(f"{word:08x}" for word in case["expected"])
                    for case in cases]
        negative = [" ".join(f"{word:08x}" for word in case["negative"])
                    for case in cases]
        self.assertEqual(self._run(transformed, cases), expected)
        self.assertEqual(self._run(transformed, cases, pristine=True), negative)
        self.assertNotEqual(expected, negative)

    def test_native_clang_matches_the_checked_wasm_boundary(self):
        fixture, cases = _fixture()
        if shutil.which("clang") is None:
            self.skipTest("clang unavailable for optional native scalar check")
        transformed = prepare_sources()
        expected = [" ".join(f"{word:08x}" for word in case["expected"])
                    for case in cases]
        negative = [" ".join(f"{word:08x}" for word in case["negative"])
                    for case in cases]
        self.assertEqual(self._run_native(transformed, cases), expected)
        self.assertEqual(self._run_native(transformed, cases, pristine=True), negative)


    def test_original_parent_and_child_matrices_and_unfused_control(self):
        sdk = ROOT / ".deps/emsdk"
        emcc = sdk / "upstream/emscripten/emcc.py"
        if not emcc.is_file():
            self.skipTest("Project-local SDK unavailable")
        source = prepare_sources()
        env = dict(os.environ, EMSDK=str(sdk), EM_CONFIG=str(sdk / ".emscripten"),
                   EMSDK_PYTHON=sys.executable)
        common = [sys.executable, str(emcc), "-O2", "-std=c11",
                  "-fno-builtin-sinf", "-fno-builtin-cosf", "-ffp-contract=off",
                  "-ffunction-sections", "-fdata-sections", "-DTARGET_PC",
                  "-I", str(ROOT / "src"), "-I", str(source),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h"),
                  str(ROOT / "tests/gameplay_srt_trace.c"),
                  str(ROOT / "src/gameplay_ps_math.c"),
                  str(source / "MSL/trigf.c"), str(source / "MSL/math_data.c"),
                  str(source / "MSL/float.c"),
                  "-sENVIRONMENT=node", "-sEXIT_RUNTIME=1"]
        output = {}
        with tempfile.TemporaryDirectory(prefix="melee-srt-") as directory:
            for name, matrix_source in (
                ("source", source / "sysdolphin/baselib/mtx.c"),
                ("unfused", ROOT / ".deps/melee/src/sysdolphin/baselib/mtx.c"),
            ):
                target = Path(directory) / (name + ".js")
                result = subprocess.run(common + [str(matrix_source), "-o", str(target)],
                                        env=env, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                result = subprocess.run([str(node_runtime()), str(target)],
                                        capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                output[name] = result.stdout.strip().splitlines()
        self.assertEqual(output["source"], [
            "3effef31 bcb5e192 3f750455 c271c1d2 bc9df328 3f8a2519 "
            "3d0fcb75 4189b18b bf75087a bd088ad0 3eff8e2a 32d201bf",
            "3f3235cd 3e9cfd55 3f444476 c271b531 be2d5932 3f84699f "
            "be85209c 419785a6 bf4ee44e 3d42405d 3f370065 bce87db0",
        ])
        self.assertNotEqual(output["unfused"], output["source"])
        self.assertEqual(output["unfused"][0].split()[10], "3eff8e2b")
        self.assertEqual(output["unfused"][1].split()[11], "bce87daf")


if __name__ == "__main__":
    unittest.main()
