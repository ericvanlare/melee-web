"""Exact scalar pose checks against the transformed gameplay sources."""

import json
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/gameplay_pose_scalars.json"
sys.path.insert(0, str(ROOT / "scripts"))
from gameplay_sources import prepare_sources
from check_gameplay import node_runtime


def _words(bits, count):
    if not isinstance(bits, str) or len(bits) != count * 8:
        raise ValueError("fixture word count is invalid")
    return [int(bits[index:index + 8], 16)
            for index in range(0, len(bits), 8)]


def _c_words(words):
    return "{" + ", ".join(f"0x{word:08x}u" for word in words) + "}"


def _fixture():
    data = json.loads(FIXTURE.read_text(encoding="utf-8"))
    if data.get("schema") != "melee-web-gameplay-pose-scalar-fixture":
        raise ValueError("unexpected pose fixture schema")
    cases = []
    for item in data.get("cases", []):
        kind = item.get("kind")
        case = {"kind": kind, "expected": item.get("expected_bits")}
        if kind == "EulerToQuat":
            case.update(a=_words(item["input_bits"], 3))
        elif kind == "HSD_QuatLib_8037EF28":
            case.update(a=_words(item["p_bits"], 4), b=_words(item["q_bits"], 4),
                        t=_words(item["t_bits"], 1)[0])
        elif kind == "lb_8000C490":
            case.update(
                arg8=_words(item["arg8_bits"], 1)[0],
                arg9=_words(item["arg9_bits"], 1)[0],
                input1=item["input1"], input2=item["input2"],
                expected_obj=item["expected"],
            )
            for obj_name in ("input1", "input2"):
                obj = case[obj_name]
                obj["rotate"] = _words(obj.pop("rotate_bits"), 4)
                obj["scale"] = _words(obj.pop("scale_bits"), 3)
                obj["translate"] = _words(obj.pop("translate_bits"), 3)
            expected_obj = case.pop("expected_obj")
            case["expected"] = (f"{expected_obj['flags']:08x}"
                                 + expected_obj["rotate_bits"]
                                 + expected_obj["scale_bits"]
                                 + expected_obj["translate_bits"])
        else:
            raise ValueError(f"unsupported fixture kind {kind!r}")
        cases.append(case)
    if not cases:
        raise ValueError("pose fixture is empty")
    return data, cases


def _runner_source(cases):
    rows = []
    for index, case in enumerate(cases):
        row = ("    {.kind='%s', .index=%d, .a=%s, .b=%s, .t=0x%08xu" %
               (case["kind"][0], index, _c_words(case.get("a", (0, 0, 0, 0))),
                _c_words(case.get("b", (0, 0, 0, 0))), case.get("t", 0)))
        if case["kind"] == "lb_8000C490":
            for field in ("input1", "input2"):
                obj = case[field]
                row += (", .%s_flags=0x%08xu, .%s_rotate=%s, .%s_scale=%s, "
                        ".%s_translate=%s" %
                        (field, obj["flags"], field, _c_words(obj["rotate"]),
                         field, _c_words(obj["scale"]), field,
                         _c_words(obj["translate"])))
            row += ", .arg8=0x%08xu, .arg9=0x%08xu" % (case["arg8"], case["arg9"])
        rows.append(row + "},")
    return """#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <dolphin/mtx.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/quatlib.h>
#include <melee/lb/lb_00B0.h>

typedef struct {
    char kind;
    unsigned index;
    uint32_t a[4];
    uint32_t b[4];
    uint32_t t;
    u32 input1_flags;
    u32 input2_flags;
    uint32_t input1_rotate[4];
    uint32_t input1_scale[3];
    uint32_t input1_translate[3];
    uint32_t input2_rotate[4];
    uint32_t input2_scale[3];
    uint32_t input2_translate[3];
    uint32_t arg8;
    uint32_t arg9;
} PoseCase;

static float from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

u32 HSD_JObjGetFlags(HSD_JObj* jobj) { return jobj->flags; }
void HSD_JObjSetFlags(HSD_JObj* jobj, u32 flags) { jobj->flags |= flags; }
void HSD_JObjClearFlags(HSD_JObj* jobj, u32 flags) { jobj->flags &= ~flags; }

static const PoseCase cases[] = {
__ROWS__
};

static void emit_quat(const PoseCase* item, const Quaternion* value)
{
    printf("%u\\t%08x%08x%08x%08x\\n", item->index,
           bits(value->x), bits(value->y), bits(value->z), bits(value->w));
}

static void emit_jobj(const PoseCase* item, const HSD_JObj* value)
{
    printf("%u\\t%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x%08x\\n",
           item->index, (unsigned int) value->flags,
           bits(value->rotate.x), bits(value->rotate.y), bits(value->rotate.z),
           bits(value->rotate.w), bits(value->scale.x), bits(value->scale.y),
           bits(value->scale.z), bits(value->translate.x),
           bits(value->translate.y), bits(value->translate.z));
}

int main(void)
{
    for (unsigned index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const PoseCase* item = &cases[index];
        if (item->kind == 'E') {
            Vec3 input = {from_bits(item->a[0]), from_bits(item->a[1]),
                          from_bits(item->a[2])};
            Quaternion output;
            EulerToQuat(&input, &output);
            emit_quat(item, &output);
        } else if (item->kind == 'H') {
            Quaternion p = {from_bits(item->a[0]), from_bits(item->a[1]),
                            from_bits(item->a[2]), from_bits(item->a[3])};
            Quaternion q = {from_bits(item->b[0]), from_bits(item->b[1]),
                            from_bits(item->b[2]), from_bits(item->b[3])};
            Quaternion output;
            HSD_QuatLib_8037EF28(&p, &q, &output, from_bits(item->t));
            emit_quat(item, &output);
        } else {
            HSD_JObj input1 = {0};
            HSD_JObj input2 = {0};
            input1.flags = item->input1_flags;
            input2.flags = item->input2_flags;
            input1.rotate.x = from_bits(item->input1_rotate[0]);
            input1.rotate.y = from_bits(item->input1_rotate[1]);
            input1.rotate.z = from_bits(item->input1_rotate[2]);
            input1.rotate.w = from_bits(item->input1_rotate[3]);
            input2.rotate.x = from_bits(item->input2_rotate[0]);
            input2.rotate.y = from_bits(item->input2_rotate[1]);
            input2.rotate.z = from_bits(item->input2_rotate[2]);
            input2.rotate.w = from_bits(item->input2_rotate[3]);
            input1.scale.x = from_bits(item->input1_scale[0]);
            input1.scale.y = from_bits(item->input1_scale[1]);
            input1.scale.z = from_bits(item->input1_scale[2]);
            input2.scale.x = from_bits(item->input2_scale[0]);
            input2.scale.y = from_bits(item->input2_scale[1]);
            input2.scale.z = from_bits(item->input2_scale[2]);
            input1.translate.x = from_bits(item->input1_translate[0]);
            input1.translate.y = from_bits(item->input1_translate[1]);
            input1.translate.z = from_bits(item->input1_translate[2]);
            input2.translate.x = from_bits(item->input2_translate[0]);
            input2.translate.y = from_bits(item->input2_translate[1]);
            input2.translate.z = from_bits(item->input2_translate[2]);
            lb_8000C490(&input1, &input2, &input2, from_bits(item->arg8),
                        from_bits(item->arg9));
            emit_jobj(item, &input2);
        }
    }
    return 0;
}
""".replace("__ROWS__", "\n".join(rows))


class GameplayPoseTests(unittest.TestCase):
    def _run(self, source, cases, *, unfused=False):
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
                  "-I", str(ROOT / "src"),
                  "-I", str(source),
                  "-I", str(source / "sysdolphin/baselib"),
                  "-I", str(source / "melee/lb"),
                  "-I", str(ROOT / ".deps/aurora/include"),
                  "-I", str(ROOT / ".deps/melee/extern/dolphin/include"),
                  "-include", str(ROOT / "src/gameplay_compat.h")]
        files = [source / "sysdolphin/baselib/quatlib.c",
                 source / "MSL/trigf.c", source / "MSL/math_data.c",
                 source / "MSL/float.c", source / "melee/lb/lbtrigf.c",
                 source / "melee/lb/lb_00B0.c"]
        with tempfile.TemporaryDirectory(prefix="melee-pose-") as directory:
            if unfused:
                # Keep the current ABI and original trig kernel; replace only
                # the two pre-fix pose producers with their pristine sources.
                for index, relative in ((0, "sysdolphin/baselib/quatlib.c"),
                                        (5, "melee/lb/lb_00B0.c")):
                    control = Path(directory) / Path(relative).name
                    original = (ROOT / ".deps/melee/src" / relative).read_text()
                    control.write_text(re.sub(r"\bbool\b", "melee_source_bool", original))
                    files[index] = control
            target = Path(directory) / "pose.js"
            runner = Path(directory) / "pose.c"
            runner.write_text(_runner_source(cases), encoding="utf-8")
            result = subprocess.run(common + [str(runner)] + [str(path) for path in files]
                                    + ["-sENVIRONMENT=node", "-sEXIT_RUNTIME=1", "-o", str(target)],
                                    env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(node_runtime(ROOT)), str(target)],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return [line.split("\t", 1)[1] for line in result.stdout.splitlines()]

    def test_transformed_sources_match_numeric_fixture_and_pristine_is_negative(self):
        fixture, cases = _fixture()
        transformed = prepare_sources()
        expected = [case["expected"] for case in cases]
        self.assertEqual(self._run(transformed, cases), expected)
        self.assertNotEqual(self._run(transformed, cases, unfused=True), expected)


if __name__ == "__main__":
    unittest.main()
