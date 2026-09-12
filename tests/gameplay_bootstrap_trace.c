#include "gameplay_bootstrap.h"
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/objalloc.h>
#include <dolphin/os/OSAlloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char error[256];
static unsigned trace[128], trace_size, removals;
typedef struct TestObject { unsigned id, delete_self, remove_proc, reorder; } TestObject;

static void check(int condition, const char* message)
{
    if (!condition) { fprintf(stderr, "%s: %s\n", message, error); exit(1); }
}
static void removed(void* data)
{
    TestObject* state = data;
    check(state->id != 0, "userdata destructor executes once");
    check(!melee_web_gameplay_step(error, sizeof(error)), "userdata cleanup cannot reenter the scheduler");
    check(!melee_web_gameplay_shutdown(error, sizeof(error)), "userdata cleanup cannot reenter world destruction");
    ++removals;
    state->id = 0;
}
static void callback(HSD_GObj* object)
{
    TestObject* state = object->user_data;
    unsigned priority = (unsigned) HSD_GObj_804D7834;
    check(HSD_GObj_804D781C == object && HSD_GObj_804D7838->gobj == object,
          "original scheduler publishes the currently running object and process");
    check(trace_size < sizeof(trace) / sizeof(trace[0]), "trace capacity");
    trace[trace_size++] = state->id * 100 + priority;
    check(!melee_web_gameplay_step(error, sizeof(error)), "recursive ticks reject");
    check(!melee_web_gameplay_shutdown(error, sizeof(error)), "callback shutdown rejects");
    if (state->delete_self) {
        HSD_GObjPLink_80390228(object);
        check(object->user_data == state && state->id != 0,
              "original deletion defers until the current callback returns");
    } else if (state->remove_proc) {
        state->remove_proc = 0;
        HSD_GObjProc_8038FE24(HSD_GObj_804D7838);
    } else if (state->reorder) {
        state->reorder = 0;
        HSD_GObjPLink_8039032C(0, object, 1, 0, NULL);
    }
}
static HSD_GObj* create(TestObject* state, unsigned link, unsigned priority)
{
    HSD_GObj* object = GObj_Create(HSD_GOBJ_CLASS_FIGHTER, (u8) link, (u8) priority);
    check(object != NULL, "original GObj allocation");
    GObj_InitUserData(object, 0, removed, state);
    return object;
}
static void process(HSD_GObj* object, unsigned priority)
{
    check(HSD_GObj_SetupProc(object, callback, (u8) priority) != NULL, "original process allocation");
}
static void expect(const unsigned* expected, unsigned size)
{
    check(trace_size == size && !memcmp(trace, expected, size * sizeof(*expected)),
          "original process ordering trace");
    trace_size = 0;
}
#define EXPECT(...) do { const unsigned values[] = {__VA_ARGS__}; expect(values, sizeof(values) / sizeof(values[0])); } while (0)
static void step(void) { check(melee_web_gameplay_step(error, sizeof(error)), "original runtime tick"); }

static int replaced_heap_case(void)
{
    static _Alignas(32) unsigned char replacement[65536];
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)),
          "runtime startup for replacement check");
    const uint64_t generation = melee_web_gameplay_generation();
    check(generation != 0 && generation == melee_web_gameplay_stats().generation,
          "generation accessor agrees with live statistics");
    void* start = OSInitAlloc(replacement, replacement + sizeof(replacement), 1);
    check(start != NULL, "replacement SDK allocator initialization");
    check(melee_web_gameplay_generation() == 0,
          "generation accessor rejects replaced SDK ownership");
    check(melee_web_gameplay_stats().generation == 0,
          "full statistics retain replacement-ownership rejection");
    check(!melee_web_gameplay_shutdown(error, sizeof(error)),
          "shutdown rejects replaced SDK ownership");
    check(OSCreateHeap(start, replacement + sizeof(replacement)) == 0 && OSCheckHeap(0) > 0,
          "replacement SDK heap remains valid after rejected shutdown");
    puts("Gameplay generation accessor replacement guard: passed");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "replaced_heap")) return replaced_heap_case();
    check(argc == 1, "unexpected bootstrap trace arguments");
    check(!melee_web_gameplay_step(error, sizeof(error)), "uninitialized ticks reject");
    check(melee_web_gameplay_generation() == 0, "uninitialized generation rejects");
    check(!melee_web_gameplay_startup(1024, error, sizeof(error)), "undersized heap rejects");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "runtime startup");
    check(!melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "duplicate startup rejects");
    TestObject a = {1, 0, 0, 0}, b = {2, 0, 0, 0}, c = {3, 0, 0, 0};
    HSD_GObj* gc = create(&c, 8, 4);
    HSD_GObj* gb = create(&b, 8, 1);
    HSD_GObj* ga = create(&a, 3, 0);
    /* Deliberately register in a different order than the expected execution. */
    process(gc, 6); process(gc, 4); process(gc, 0);
    process(gb, 4); process(gb, 0); process(gb, 6); process(ga, 4);
    MeleeWebGameplayStats stats = melee_web_gameplay_stats();
    const uint64_t initial_generation = melee_web_gameplay_generation();
    check(initial_generation != 0 && stats.generation == initial_generation,
          "live generation matches full bootstrap statistics");
    check(stats.objects == 3 && stats.processes == 7 && stats.heap_free_bytes > 0,
          "original pool counts and real SDK heap validation");
    step(); EXPECT(200, 300, 104, 204, 304, 206, 306);
    step(); EXPECT(200, 300, 104, 204, 304, 206, 306);

    *HSD_GObjLibInitData.unk_2 = 1ULL << 8;
    step(); EXPECT(104);
    *HSD_GObjLibInitData.unk_2 = 0;
    HSD_GObj_80390C5C(gb);
    step(); EXPECT(300, 104, 304, 306);
    HSD_GObj_80390C84(gb);
    step(); EXPECT(200, 300, 104, 204, 304, 206, 306);

    b.delete_self = 1;
    step(); EXPECT(200, 300, 104, 304, 306);
    stats = melee_web_gameplay_stats();
    check(removals == 1 && stats.objects == 2 && stats.processes == 4,
          "deferred object deletion removes all owned processes and userdata");
    c.remove_proc = 1;
    step(); EXPECT(300, 104, 304, 306);
    step(); EXPECT(104, 304, 306);
    check(melee_web_gameplay_stats().processes == 3, "self-removal frees only the current process");

    c.reorder = 1;
    step(); EXPECT(104, 304, 306);
    step(); EXPECT(304, 104, 306);
    check(gc->p_link == 1, "deferred current-object reordering changes the next tick ordering");

    TestObject d = {4, 0, 0, 0};
    HSD_GObj* gd = create(&d, 8, 0);
    check(gd == gb, "original free-list allocation reuses the released GObj slot");
    process(gd, 0x18);
    step(); EXPECT(304, 104, 306, 424);
    check(melee_web_gameplay_stats().object_peak == 3, "original allocator preserves peak accounting");

    HSD_GObjObject_80390A70(gd, 0, &d);
    const uint64_t before = melee_web_gameplay_stats().ticks;
    check(!melee_web_gameplay_step(error, sizeof(error)), "uninitialized render-object lifetime rejects before stepping");
    check(!melee_web_gameplay_shutdown(error, sizeof(error)), "uninitialized render-object lifetime rejects before destruction");
    check(melee_web_gameplay_stats().ticks == before, "rejected ticks do not advance time");
    check(HSD_GObjObject_80390ADC(gd) == &d, "explicit source detach restores the supported lifecycle");

    check(melee_web_gameplay_shutdown(error, sizeof(error)), "runtime shutdown");
    check(melee_web_gameplay_generation() == 0, "shutdown clears live generation");
    check(removals == 4 && melee_web_gameplay_stats().objects == 0,
          "shutdown calls remaining original user-data destructors and clears state");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "shutdown is idempotent");
    check(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), "runtime restart");
    check(melee_web_gameplay_generation() != 0 &&
          melee_web_gameplay_generation() != initial_generation &&
          melee_web_gameplay_stats().generation == melee_web_gameplay_generation() &&
          melee_web_gameplay_stats().ticks == 0 && melee_web_gameplay_stats().objects == 0,
          "restart does not inherit old pool counters or process lists");
    step(); check(trace_size == 0, "empty restarted world has no stale callbacks");
    check(melee_web_gameplay_shutdown(error, sizeof(error)), "restarted shutdown");
    puts("Original HSD gameplay-bootstrap scheduler trace: passed");
}
