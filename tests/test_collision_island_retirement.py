"""Source-body retirement reducer; allocator accounting, not SDK/gameplay evidence.

Compile the production release and pinned original island routines verbatim.
Native pointers change the ABI, so the allocator widens island storage while
accounting the original requested bytes. Device/loader services outside these
routines are explicit fixture bindings; no complete stage is constructed.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / ".deps/melee/src/melee/mp"


def between(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


def declaration(source, name):
    first = source.index("struct " + name + " {")
    return source[first:source.index("};", first) + 2]


class CollisionIslandRetirementTests(unittest.TestCase):
    def test_original_partition_reuse_and_production_retirement(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        if not compiler or not (UPSTREAM / "mpisland.c").is_file():
            self.skipTest("Native compiler and pinned Melee source required")
        island = (UPSTREAM / "mpisland.c").read_text()
        types = (UPSTREAM / "types.h").read_text()
        release = between((ROOT / "src/gameplay_collision.c").read_text(),
                          "static void collision_release_storage(void)",
                          "static void collision_release(void* data)")
        declarations = "\n".join(declaration(types, name) for name in (
            "mpIsland_80458E88_t", "mp_UnkStruct0", "MapLine", "CollLine",
            "CollVtx", "MapJoint", "CollJoint", "MapCollData"))
        original = between(island, "void mpIsland_8005A6F8(void)",
                           "mp_UnkStruct0* mpIsland_8005AB54(")
        original += between(island, "void mpIsland_8005AE1C(",
                            "void mpIsland_8005B334(")
        original += island[island.index("void mpIsland_8005B334("):]
        harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t s16;
typedef float f32;
typedef struct { float x, y; } Vec2;
typedef struct { float x, y, z; } Vec3;
typedef void HSD_JObj;
typedef void Ground;
typedef void (*mpLib_JointCollisionCallback)(void);
typedef struct mp_UnkStruct3 mp_UnkStruct3;
typedef struct mp_UnkStruct0 mp_UnkStruct0;
typedef struct MapLine MapLine;
typedef struct CollLine CollLine;
typedef struct CollVtx CollVtx;
typedef struct MapJoint MapJoint;
typedef struct CollJoint CollJoint;
typedef struct MapCollData MapCollData;
#define UNUSED
#define PAD_STACK(n)
#define F32_MAX FLT_MAX
#define HSD_ASSERT(line, condition) assert(condition)
#define LINE_FLAG_ENABLED (1 << 16)
#define LINE_FLAG_HIDDEN (1 << 18)
#define CollLine_Floor 1
#define CollLine_Ceiling 2
#define memzero(p,n) memset(p,0,n)
''' + declarations + r'''
static struct mpIsland_80458E88_t mpIsland_80458E88;
static CollVtx* groundCollVtx;
static CollLine* groundCollLine;
static CollJoint* groundCollJoint;
static CollJoint *jointListStart, *jointListEnd;
static MapCollData* mpLib_804D64B4;
static bool didCheckBounding;
static int reset_calls;
static void grDynamicAttr_801CA0B4(void) { ++reset_calls; }
static CollVtx* mpGetGroundCollVtx(void) { return groundCollVtx; }
static CollLine* mpGetGroundCollLine(void) { return groundCollLine; }
static CollJoint* mpGetGroundCollJoint(void) { return groundCollJoint; }
static MapCollData* mpLib_8004D164(void) { return mpLib_804D64B4; }
static int mpJointFromLine(int line) { (void)line; return 0; }
static struct { void* pointer; size_t requested; } leases[64];
static size_t live_bytes, live_count, allocations;
static void* HSD_MemAlloc(size_t requested)
{
    /* The original routine requests the 0x2c PPC prefix. Host pointer
     * alignment needs more backing bytes, independent of accounted bytes. */
    size_t backing = requested < sizeof(mp_UnkStruct0) ? sizeof(mp_UnkStruct0) : requested;
    void* pointer = calloc(1, backing);
    assert(pointer);
    for (size_t i = 0; i < 64; ++i) if (!leases[i].pointer) {
        leases[i].pointer = pointer; leases[i].requested = requested;
        live_bytes += requested; ++live_count; ++allocations;
        return pointer;
    }
    abort();
}
static void HSD_Free(void* pointer)
{
    if (!pointer) return;
    for (size_t i = 0; i < 64; ++i) if (leases[i].pointer == pointer) {
        live_bytes -= leases[i].requested; --live_count;
        leases[i].pointer = NULL; free(pointer); return;
    }
    fputs("duplicate or foreign allocation retirement\n", stderr); abort();
}
''' + original + release + r'''
static size_t count(mp_UnkStruct0* node)
{
    size_t result = 0;
    for (; node; node = node->next) { ++result; assert(result < 16); }
    return result;
}
static void assert_partition(size_t expected)
{
    mp_UnkStruct0* roots[] = {mpIsland_80458E88.next, mpIsland_80458E88.x4,
        mpIsland_80458E88.x18, mpIsland_80458E88.x1C, mpIsland_80458E88.x20};
    mp_UnkStruct0* seen[16]; size_t used = 0;
    for (size_t i = 0; i < 5; ++i) for (mp_UnkStruct0* n = roots[i]; n; n = n->next) {
        assert(used < 16);
        for (size_t j = 0; j < used; ++j) assert(seen[j] != n);
        seen[used++] = n;
    }
    assert(used == expected);
}
int main(void)
{
    MapLine lines[6] = {0}; MapJoint joint = {0}; MapCollData map = {0};
    map.floor_count = 2; map.ceiling_start = 2; map.ceiling_count = 2;
    joint.dynamic_start = 4; joint.dynamic_count = 2;
    for (int i = 0; i < 6; ++i) {
        lines[i].v0_idx = i * 2; lines[i].v1_idx = i * 2 + 1;
        lines[i].prev_id0 = lines[i].next_id0 = -1;
        lines[i].hi_flags = i < 2 ? 1 : i < 4 ? 2 : i == 4 ? 0x11 : 0x12;
    }
    for (int cycle = 0; cycle < 4; ++cycle) {
        assert(live_bytes == 0 && live_count == 0);
        groundCollVtx = HSD_MemAlloc(12 * sizeof(CollVtx));
        groundCollLine = HSD_MemAlloc(sizeof(lines) / sizeof(lines[0]) * sizeof(CollLine));
        groundCollJoint = HSD_MemAlloc(sizeof(CollJoint));
        groundCollJoint->inner = &joint; mpLib_804D64B4 = &map;
        for (int i = 0; i < 6; ++i) {
            groundCollLine[i].x0 = &lines[i];
            groundCollLine[i].flags = lines[i].hi_flags | LINE_FLAG_ENABLED;
        }
        for (int i = 0; i < 12; ++i) groundCollVtx[i].pos.x = (float)i;
        mpIsland_8005A728();
        assert_partition(4);
        mp_UnkStruct0 *floor = mpIsland_80458E88.next;
        mp_UnkStruct0 *ceiling = mpIsland_80458E88.x4;
        /* Partial disable retains disjoint active/disabled owners. */
        mpIsland_8005B334(0, 0, 2, false);
        assert(count(mpIsland_80458E88.x18) == 1);
        assert(count(mpIsland_80458E88.next) == 2); /* one static, one dynamic */
        assert_partition(6);
        size_t after_first_dynamic = allocations;
        mpIsland_8005B334(0, 0, 12, false);
        assert(mpIsland_80458E88.x18 && mpIsland_80458E88.x1C);
        assert_partition(6);
        mpIsland_8005B334(0, 0, 12, true);
        assert(!mpIsland_80458E88.x18 && !mpIsland_80458E88.x1C);
        assert(count(mpIsland_80458E88.next) == 3 && count(mpIsland_80458E88.x4) == 3);
        assert_partition(6);
        /* Move vertices and regenerate dynamic chains using existing leases. */
        groundCollVtx[floor->x4].pos.x = 25.0F;
        mpIsland_8005B334(0, 0, 12, true);
        assert(floor->x8.x == 25.0F && ceiling->x8.x == groundCollVtx[ceiling->x4].pos.x);
        assert(allocations == after_first_dynamic);
        if (cycle == 0) mpIsland_8005B334(0, 0, 12, false);
        if (cycle == 3) mpIsland_8005B334(0, 0, 2, false);
        if (cycle == 1) {
            /* Changing authored dynamic classification puts both dynamic
             * allocations into the reusable x20 chain, outside active roots. */
            groundCollLine[4].flags = groundCollLine[5].flags = 0;
            mpIsland_8005B334(0, 0, 12, false);
            assert(!mpIsland_80458E88.x10 && !mpIsland_80458E88.x14);
            assert(count(mpIsland_80458E88.x20) == 2);
            groundCollLine[4].flags = 0x11 | LINE_FLAG_ENABLED;
            groundCollLine[5].flags = 0x12 | LINE_FLAG_ENABLED;
            mpIsland_8005B334(0, 0, 12, true);
            assert(!mpIsland_80458E88.x20 && allocations == after_first_dynamic);
            groundCollLine[4].flags = groundCollLine[5].flags = 0;
            mpIsland_8005B334(0, 0, 12, false);
            assert(count(mpIsland_80458E88.x20) == 2);
        }
        assert_partition(6);
        collision_release_storage();
        fprintf(stderr, "cycle=%d live_bytes=%zu live_allocations=%zu\n", cycle, live_bytes, live_count);
        assert(live_bytes == 0 && live_count == 0);
        assert(!groundCollVtx && !groundCollLine && !groundCollJoint && !mpLib_804D64B4);
        assert(!mpIsland_80458E88.next && !mpIsland_80458E88.x4 && !mpIsland_80458E88.x8 &&
               !mpIsland_80458E88.xC && !mpIsland_80458E88.x10 && !mpIsland_80458E88.x14 &&
               !mpIsland_80458E88.x18 && !mpIsland_80458E88.x1C && !mpIsland_80458E88.x20);
        collision_release_storage();
        assert(live_bytes == 0 && live_count == 0);
    }
    assert(reset_calls == 8);
    puts("Original island partitions/reuse and production retirement: passed");
}
'''
        # Preserve the first failed compiler/run artifact; remove successful scratch.
        (ROOT / "work").mkdir(exist_ok=True)
        directory = Path(tempfile.mkdtemp(prefix="collision-retirement-", dir=ROOT / "work"))
        self.addCleanup(lambda: shutil.rmtree(directory) if getattr(self, "passed", False) else None)
        print(f"Collision retirement reducer scratch: {directory}", flush=True)
        source = directory / "reducer.c"
        source.write_text(harness)
        binary = directory / "reducer"
        built = subprocess.run([compiler, "-std=c11", "-O1", "-g", "-Wall", "-Wextra",
                                "-Werror", "-Wno-unused-variable", "-Wno-unused-parameter", "-ffp-contract=off",
                                "-fsanitize=address,undefined", str(source), "-o", str(binary)],
                               capture_output=True, text=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("Original island partitions/reuse and production retirement: passed", run.stdout)
        print(run.stdout + run.stderr, end="", flush=True)
        self.passed = True


if __name__ == "__main__":
    unittest.main()
