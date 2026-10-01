"""Exercise the patched Arrow shield-collision probe at its observation boundary."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".deps/melee/src/melee/lb"
PATCH = ROOT / "patches/melee-gameplay.patch"


def _between(source: str, start: str, end: str) -> str:
    first = source.index(start)
    return source[first:source.index(end, first)]


class GameplayLbCollisionProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shutil.which("clang") or shutil.which("cc")
        if cls.compiler is None:
            raise unittest.SkipTest("A native C compiler is required")
        if not (UPSTREAM / "lbcollision.c").is_file():
            raise unittest.SkipTest("Pinned Melee sources are required")
        cls.temp = tempfile.TemporaryDirectory(prefix="melee-lb-collision-probe-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        cls.source_root = cls.directory / "source"
        for rel in ("src/melee/lb/lbcollision.c", "src/melee/lb/lbcollision.h",
                    "src/melee/ft/ftcoll.c"):
            destination = cls.source_root / rel
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / ".deps/melee" / rel, destination)
        applied = subprocess.run(
            ["git", "apply", "--include=src/melee/lb/lbcollision.c",
             "--include=src/melee/ft/ftcoll.c", str(PATCH)],
            cwd=cls.source_root, capture_output=True, text=True, timeout=30)
        if applied.returncode:
            raise AssertionError(applied.stdout + applied.stderr)
        collision_source = (cls.source_root / "src/melee/lb/lbcollision.c").read_text()
        collision_header = (cls.source_root / "src/melee/lb/lbcollision.h").read_text()
        cls.collision_helpers = _between(
            collision_source, "static inline float sqrDistance(",
            "void lbColl_800077A0(")
        cls.collision_function = _between(
            collision_source, "void lbColl_800077A0(",
            "bool lbColl_80007AFC(")
        approx_start = collision_header.index(
            "static inline bool approximatelyZero(")
        approx_end = collision_header.index("\n}", approx_start) + 2
        cls.approximately_zero = collision_header[approx_start:approx_end]
        cls.ftcoll_source = (cls.source_root / "src/melee/ft/ftcoll.c").read_text()

    def _compile_and_run(self, name: str, source: str) -> str:
        program = self.directory / f"{name}.c"
        executable = self.directory / name
        program.write_text(source)
        built = subprocess.run(
            [self.compiler, "-std=c11", "-DMELEE_WEB_GAMEPLAY=1", "-O1",
             "-Wall", "-Wextra", "-Werror",
             "-ffp-contract=off", "-I", str(ROOT / "src"), str(program), "-lm",
             "-o", str(executable)],
            cwd=self.directory, capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(executable)], cwd=self.directory,
                             capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        return run.stdout

    def test_near_zero_and_quadratic_branches_match_with_probe_off_and_on(self):
        harness = r'''#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gameplay_cpu_observation.h"

typedef struct Vec3 { float x, y, z; } Vec3;
typedef float Mtx[3][4];
typedef float (*MtxPtr)[4];
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int observer_enabled;
static int observed_calls;
static uint8_t observed_branch;
static int observed_intermediates_valid;
static float observed_n0;
static double __frsqrte(double value) { return 1.0 / sqrt(value); }
static void PSMTXMultVec(MtxPtr matrix, Vec3* source, Vec3* destination)
{
    Vec3 result = {
        matrix[0][0] * source->x + matrix[0][1] * source->y +
            matrix[0][2] * source->z + matrix[0][3],
        matrix[1][0] * source->x + matrix[1][1] * source->y +
            matrix[1][2] * source->z + matrix[1][3],
        matrix[2][0] * source->x + matrix[2][1] * source->y +
            matrix[2][2] * source->z + matrix[2][3]
    };
    *destination = result;
}
static void PSVECNormalize(const Vec3* source, Vec3* destination)
{
    float length = sqrtf(source->x * source->x + source->y * source->y +
                         source->z * source->z);
    destination->x = source->x / length;
    destination->y = source->y / length;
    destination->z = source->z / length;
}
static float lbVector_AngleXY(Vec3* a, Vec3* b)
{
    return a->x + a->y + b->x + b->y;
}
static uint32_t float_bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
int melee_web_cpu_observation_lb_collision_probe_active(void)
{
    return observer_enabled;
}
void melee_web_cpu_observation_lb_collision_probe(const MeleeWebLbCollProbe* probe)
{
    ++observed_calls;
    observed_branch = probe->branch;
    if (probe->branch == MELEE_WEB_LB_COLL_BRANCH_QUADRATIC) {
        observed_intermediates_valid = 1;
        observed_n0 = probe->n0;
    } else {
        observed_intermediates_valid = 0;
    }
}
'''+self.approximately_zero+r'''
'''+self.collision_helpers+self.collision_function+r'''
typedef struct { uint32_t d[3], e[3], angle; } Result;
static Result run(float delta, int enable_probe)
{
    Vec3 a = {0.0f, 0.0f, 0.0f};
    Vec3 b = {1.0f, 0.0f, 0.0f};
    Vec3 c = {1.0f + delta, 0.0f, 0.0f};
    Vec3 d = {0.0f, 0.0f, 0.0f};
    Vec3 e = {0.0f, 0.0f, 0.0f};
    Mtx matrix = {{1.0f, 0.0f, 0.0f, 0.0f},
                  {0.0f, 1.0f, 0.0f, 0.0f},
                  {0.0f, 0.0f, 1.0f, 0.0f}};
    float angle = -1.0f;
    observer_enabled = enable_probe;
    observed_calls = 0;
    lbColl_800077A0(&a, matrix, &b, &c, &d, &e, &angle, 1.0f, 0.0f);
    assert(observed_calls == (enable_probe ? 1 : 0));
    Result result = {{float_bits(d.x), float_bits(d.y), float_bits(d.z)},
                     {float_bits(e.x), float_bits(e.y), float_bits(e.z)},
                     float_bits(angle)};
    return result;
}
int main(void)
{
    const float near_zero_delta = 0.001f;
    const float near_zero_dot = near_zero_delta * near_zero_delta;
    assert(near_zero_delta != 0.0f && near_zero_dot > 0.0f && near_zero_dot < 0.00001f);
    Result near_zero_without = run(near_zero_delta, 0);
    Result near_zero_with = run(near_zero_delta, 1);
    assert(memcmp(&near_zero_without, &near_zero_with, sizeof(near_zero_without)) == 0);
    assert(observed_branch == MELEE_WEB_LB_COLL_BRANCH_NEAR_ZERO);
    assert(!observed_intermediates_valid);

    Result quadratic_without = run(0.5f, 0);
    Result quadratic_with = run(0.5f, 1);
    assert(memcmp(&quadratic_without, &quadratic_with, sizeof(quadratic_without)) == 0);
    assert(observed_branch == MELEE_WEB_LB_COLL_BRANCH_QUADRATIC);
    assert(observed_intermediates_valid && observed_n0 != 0.0f);
    puts("lb collision probe branches and diagnostics-off outputs: passed");
    return 0;
}
'''
        output = self._compile_and_run("lb-collision-branches", harness)
        self.assertIn("lb collision probe branches and diagnostics-off outputs: passed", output)

    def test_consumer_emits_null_for_unavailable_quadratic_intermediates(self):
        observation_source = (ROOT / "src/gameplay_cpu_observation.c").read_text()
        active_start = observation_source.index(
            "int melee_web_cpu_observation_lb_collision_probe_active(void)")
        consumer = observation_source[active_start:observation_source.index(
            "void melee_web_cpu_observation_source_event(", active_start)]
        harness = r'''#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gameplay_cpu_observation.h"
static int enabled = 1;
static size_t source_event_cursor = 27055;
static unsigned source_event_sequence;
static size_t used;
static char line[65536];
static void put(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line + used, sizeof(line) - used, format, args);
    va_end(args);
    assert(n >= 0 && (size_t)n < sizeof(line) - used);
    used += (size_t)n;
}
static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static float from_bits(uint32_t value)
{
    float result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static void emit_source_event(void)
{
    puts(line);
    used = 0;
}
'''+consumer+r'''
int main(void)
{
    MeleeWebLbCollProbe probe = {0};
    probe.b[0] = from_bits(0xc22e70e6);
    probe.b[1] = from_bits(0x3fdb54c5);
    probe.b[2] = from_bits(0x00000000);
    probe.c[0] = from_bits(0xc229058f);
    probe.c[1] = from_bits(0x3f4fe2eb);
    probe.c[2] = from_bits(0x00000000);
    probe.n0 = 123.0f;
    probe.ba_dot = 456.0f;
    probe.n1 = 789.0f;
    probe.branch = MELEE_WEB_LB_COLL_BRANCH_NEAR_ZERO;
    melee_web_cpu_observation_lb_collision_probe(&probe);
    probe.branch = MELEE_WEB_LB_COLL_BRANCH_QUADRATIC;
    melee_web_cpu_observation_lb_collision_probe(&probe);
    return 0;
}
'''
        output = self._compile_and_run("lb-collision-schema", harness)
        records = [json.loads(line) for line in output.splitlines()]
        self.assertEqual(len(records), 2)
        self.assertEqual(records[0]["branch"], "near_zero")
        self.assertIsNone(records[0]["quadratic_intermediates"])
        self.assertNotIn("n0_bits", records[0])
        self.assertEqual(records[1]["branch"], "quadratic")
        self.assertEqual(records[1]["quadratic_intermediates"], {
            "n0_bits": "42f60000", "ba_dot_bits": "43e40000", "n1_bits": "44454000"})

    def test_neighboring_ftcoll_probe_guards_unwritten_collision_outputs(self):
        source = self.ftcoll_source
        self.assertIn("int shield_collision_outputs_valid = 0;", source)
        self.assertIn("shield_collision_outputs_valid = 1;", source)
        self.assertEqual(source.count(
            "shield_collision_outputs_valid ? &coll_pos : NULL"), 2)
        self.assertEqual(source.count(
            "shield_collision_outputs_valid ? coll_dist : 0.0f"), 2)
        # The original gameplay consumer keeps the same arguments.
        self.assertIn("ftColl_80077688(item, hurt, fp, &coll_pos, coll_dist);", source)


if __name__ == "__main__":
    unittest.main()
