"""Original queue bodies + restoration fragment; explicit synthetic services.

Native ASan/UBSan checks ordering/lifetime, not SDK ABI or Stadium OnStart.
The separate asset-free SDK control reproduces exact live header/root loss.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import sys

from test_stadium_on_init_boundary import function_body

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


class GroundPendingCallbackTests(unittest.TestCase):
    def test_original_order_repeat_and_current_restore_loss(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        source_path = ROOT / "build/gameplay-source/src/melee/gr/ground.c"
        if not compiler or not source_path.is_file():
            self.skipTest("Prepared pinned source with checked TARGET_PC adaptation and native compiler required")
        original = source_path.read_text()
        enqueue = function_body(original, "void Ground_801C10B8(")
        drain = function_body(original, "void Ground_801C0FB8(")
        fragment = function_body(
            (ROOT / "tests/stadium_c1_stage_state_probe.c").read_text(),
            "static void queue_restore_stage_last_fragment(")
        end = function_body((ROOT / "src/gameplay_stage_last.c").read_text(),
                            "int melee_web_stage_last_end(")
        self.assertEqual(fragment.strip(), "stage_info=h->saved;")
        self.assertIn(fragment.strip(), end)
        harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct HSD_GObj { int witness; } HSD_GObj;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef struct StageIdPair { int grkind; } StageIdPair;
struct StageInfo { void* x6A4; };
static struct StageInfo stage_info;
struct MeleeWebStadiumC1StageInfoSnapshot { struct StageInfo saved; };
static void* headers[2];
static unsigned live, frees, callbacks, events[16], event_count;
static HSD_GObj borrowed[2] = {{1}, {2}}, scheduler;
static void* HSD_MemAlloc(size_t size)
{
    assert(size == 3 * sizeof(void*));
    assert(live < 2);
    void* allocation = malloc(size);
    assert(allocation);
    headers[live++] = allocation;
    return allocation;
}
static void HSD_Free(void* allocation)
{
    assert(live > 0 && frees < 2);
    assert(allocation == headers[1 - frees]);
    /* Each callback must have run before its own header is freed. */
    assert(callbacks == frees + 1);
    events[event_count++] = 20 + frees;
    ++frees; --live;
    free(allocation);
}
static void synthetic_on_start(void)
{
    assert(stage_info.x6A4 && live == 2 && callbacks == 0 && frees == 0);
    events[event_count++] = 1;
}
struct SyntheticStageData { void (*on_start)(void); };
static struct SyntheticStageData row = {synthetic_on_start};
static struct SyntheticStageData* stage_datas[] = {&row};
static void synthetic_callback(HSD_GObj* argument)
{
    assert(argument == &borrowed[1 - callbacks]);
    assert(argument->witness == 2 - (int)callbacks);
    assert(frees == callbacks && live == 2 - callbacks);
    events[event_count++] = 10 + callbacks;
    ++callbacks;
}
#define HSD_GOBJ_CLASS_STAGE 3
static HSD_GObj* GObj_Create(int classifier, int link, int priority)
{
    assert(classifier == HSD_GOBJ_CLASS_STAGE && link == 5 && priority == 0);
    assert(stage_info.x6A4 == NULL && live == 0 && callbacks == 2 && frees == 2);
    events[event_count++] = 30;
    return &scheduler;
}
static void Ground_801C0C2C(HSD_GObj* unused) { (void)unused; abort(); }
static void HSD_GObj_SetupProc(HSD_GObj* object, HSD_GObjEvent callback, int priority)
{
    assert(object == &scheduler && callback == Ground_801C0C2C && priority == 10);
    events[event_count++] = 40;
}
static void OSReport(const char* format, const char* file, int line)
{ (void)format; (void)file; (void)line; abort(); }
static void OSPanic(const char* file, int line, const char* message)
{ (void)file; (void)line; (void)message; abort(); }
'''
        program = harness + "\nvoid Ground_801C10B8(HSD_GObj* arg0, HSD_GObjEvent arg1) {" + enqueue + "}\n"
        program += "void Ground_801C0FB8(StageIdPair* pair) {" + drain + "}\n"
        program += "static void queue_restore_stage_last_fragment(struct MeleeWebStadiumC1StageInfoSnapshot* h) {" + fragment + "}\n"
        program += r'''
int main(void)
{
    const unsigned expected[] = {1, 10, 20, 11, 21, 30, 40};
    StageIdPair pair = {0};
    for (unsigned repeat = 0; repeat != 2; ++repeat) {
        assert(!stage_info.x6A4 && !live);
        frees = callbacks = event_count = 0;
        Ground_801C10B8(&borrowed[0], synthetic_callback);
        Ground_801C10B8(&borrowed[1], synthetic_callback);
        Ground_801C0FB8(&pair);
        assert(!stage_info.x6A4 && !live && frees == 2);
        assert(event_count == sizeof(expected) / sizeof(expected[0]));
        assert(memcmp(events, expected, sizeof(expected)) == 0);
        assert(borrowed[0].witness == 1 && borrowed[1].witness == 2);
    }
    frees = callbacks = event_count = 0;
    struct MeleeWebStadiumC1StageInfoSnapshot saved = {stage_info};
    Ground_801C10B8(&borrowed[0], synthetic_callback);
    Ground_801C10B8(&borrowed[1], synthetic_callback);
    queue_restore_stage_last_fragment(&saved);
    assert(!stage_info.x6A4 && live == 2 && !frees && !callbacks);
    assert(borrowed[0].witness == 1 && borrowed[1].witness == 2);
    puts("Original queue order/repeat and current restoration loss reproduced; synthetic service witnesses only");
    /* Test-only allocator storage cleanup after proving loss.  Not a runtime
     * retirement, callback execution, or heap-equality acceptance. */
    free(headers[0]); free(headers[1]);
    return 0;
}
'''
        work = ROOT / "work"
        work.mkdir(exist_ok=True)
        scratch = Path(tempfile.mkdtemp(prefix="ground-pending-queue-", dir=work))
        passed = False
        try:
            source = scratch / "queue.c"
            source.write_text(program)
            binary = scratch / "queue"
            build = subprocess.run(
                [compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                 "-DTARGET_PC=1", "-ffp-contract=off", "-fsanitize=address,undefined",
                 str(source), "-o", str(binary)], capture_output=True, text=True, timeout=60)
            (scratch / "compile.stdout").write_text(build.stdout)
            (scratch / "compile.stderr").write_text(build.stderr)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            (scratch / "run.stdout").write_text(run.stdout)
            (scratch / "run.stderr").write_text(run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout, end="", flush=True)
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)
            else:
                print(f"Retained queue reducer: {scratch}", flush=True)

    def test_pinned_stadium_callback_registration_borrowers(self):
        source_path = ROOT / "build/gameplay-source/src/melee/gr/grpstadium.c"
        if not source_path.is_file():
            self.skipTest("Prepared pinned Stadium source required")
        source = source_path.read_text()
        # Extract the balanced initializer and its top-level callback rows.
        opening = source.index("{", source.index("static StageCallbacks grPs_StageCallbacks[]"))
        depth, row_start, rows = 0, None, []
        for offset in range(opening, len(source)):
            character = source[offset]
            if character == "{":
                depth += 1
                if depth == 2:
                    row_start = offset + 1
            elif character == "}":
                if depth == 2:
                    rows.append([field.strip() for field in source[row_start:offset].split(",") if field.strip()])
                depth -= 1
                if depth == 0:
                    break
        self.assertEqual(depth, 0, "Balanced callback table initializer required")
        self.assertEqual(rows[1], ["grStadium_801D1290", "grStadium_801D1388",
                                   "grStadium_801D1390", "grStadium_801D13C4", "0"])
        self.assertEqual(rows[2], ["grStadium_801D13E0", "grStadium_801D1518",
                                   "grStadium_801D1520", "grStadium_801D156C", "0xC0000000"])
        display = function_body(source, "void grStadium_801D1290(")
        map2 = function_body(source, "void grStadium_801D13E0(")
        self.assertIn("Ground_801C10B8(gobj, fn_801D11E4);", display)
        self.assertIn("gr->u.stadium.xE4 = grStadium_801D10F8(gr->u.stadium.xDE);", map2)
        self.assertIn("gr->u.stadium.xDE = 5;", map2)
        self.assertIn("Ground_801C10B8(gobj, fn_801D13C8);", map2)
        self.assertNotIn("gobj =", map2)

    def test_sdk_queue_loss_control(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed diagnostic native menu trace first")
        from check_gameplay import node_runtime
        scratch = Path(tempfile.mkdtemp(prefix="ground-pending-sdk-", dir=ROOT / "work"))
        passed = False
        try:
            command = [str(node_runtime()), str(target), "--ground-pending-callback-controls"]
            (scratch / "command.txt").write_text(" ".join(command) + "\n")
            try:
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
            except subprocess.TimeoutExpired as failure:
                for stream in ("stdout", "stderr"):
                    value = getattr(failure, stream)
                    (scratch / stream).write_bytes(
                        value.encode() if isinstance(value, str) else (value or b""))
                raise
            (scratch / "stdout").write_text(run.stdout)
            (scratch / "stderr").write_text(run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("scope=actual-SDK-enqueue-and-StageLast-assignment-fragment", run.stdout)
            self.assertIn("expected_root_loss_reproduced=1", run.stdout)
            self.assertIn("source_pair_order=map2-map1 nested_map5_refused=1 pair_checks_pure=1", run.stdout)
            self.assertEqual(run.stdout.count("GROUND_QUEUE_PAIR index="), 2)
            self.assertIn("GROUND_QUEUE_MAP5_NEGATIVE", run.stdout)
            self.assertIn("full_StageLast_executed=0 Stadium_OnStart_executed=0 raw_shutdown=0", run.stdout)
            print(run.stdout, end="", flush=True)
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)
            else:
                print(f"Retained SDK queue reducer: {scratch}", flush=True)

    def test_sdk_generator_lifetime_control(self):
        target = ROOT / "build/browser-stadium-c1a-release/native_menu_host_trace.js"
        if not target.is_file():
            self.skipTest("Build the reviewed diagnostic native menu trace first")
        from check_gameplay import node_runtime
        scratch = Path(tempfile.mkdtemp(prefix="stadium-generator-sdk-", dir=ROOT / "work"))
        passed = False
        try:
            command = [str(node_runtime()), str(target), "--stadium-generator-lifetime-controls"]
            (scratch / "command.txt").write_text(" ".join(command) + "\n")
            try:
                run = subprocess.run(command, cwd=ROOT, capture_output=True,
                                     text=True, timeout=30)
            except subprocess.TimeoutExpired as failure:
                for stream in ("stdout", "stderr"):
                    value = getattr(failure, stream)
                    (scratch / stream).write_bytes(
                        value.encode() if isinstance(value, str) else (value or b""))
                raise
            (scratch / "stdout").write_text(run.stdout)
            (scratch / "stderr").write_text(run.stderr)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertIn("STADIUM_GENERATOR_CONTROL lifetimes=2", run.stdout)
            self.assertIn("borrowed_refusals=2 original_onload_onstart=1 ticks=0", run.stdout)
            print(run.stdout, end="", flush=True)
            passed = True
        finally:
            if passed:
                shutil.rmtree(scratch)
            else:
                print(f"Retained SDK generator control: {scratch}", flush=True)
