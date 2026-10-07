#include "gameplay_vs_sis.h"
#include "gameplay_bootstrap.h"
#include "gameplay_archive_sections.h"
#include "gameplay_rumble.h"
#include "gameplay_render.h"
#include "gameplay_source_files.h"
#include "hsd_native_joint.h"
#include <melee/lb/types.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjobject.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <dolphin/os/OSAlloc.h>
#include <melee/cm/camera.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static char error[256];
static unsigned trace[128], trace_size, removals;
static struct Fighter_804D653C_t vs_rumble_rows[40];
static MeleeWebArchiveSections* vs_archive_scope;
typedef struct BorrowedSisOwner {
    uint32_t canary;
    SIS sis;
    SIS* table[1];
} BorrowedSisOwner;
static BorrowedSisOwner* borrowed_sis_owner;
static SIS* borrowed_sis_descriptor;
static HSD_GObj* borrowed_sis_text_gobj;
static int borrowed_sis_drain_seen;
static int borrowed_sis_sweep_seen;
static int borrowed_sis_replace_after_drain;
static unsigned borrowed_sis_shutdown_calls;
static SIS borrowed_sis_foreign_after_drain;
typedef struct TestObject { unsigned id, delete_self, remove_proc, reorder; } TestObject;

static void check(int condition, const char* message);

extern void gm_801A4BD4(void);
extern HSD_GObj* DevText_GetGObj(void);
extern HSD_CObj* melee_web_source_devtext_camera(void);
static int vs_manager_startup(char* error, size_t size)
{
    if (!melee_web_native_world_prepare_vs_manager(error, size)) return 0;
    HSD_SisLib_803A6048(0x4800);
    gm_801A4BD4();
    if (error && size) error[0] = '\0';
    return 1;
}
static int vs_sis_shutdown(MeleeWebGameplayVSSisOperation operation, int slot,
                           void* expected, char* error, size_t size)
{
    return melee_web_gameplay_vs_sis(operation, slot, expected, error, size);
}

static void borrowed_sis_parent_removed(void* data)
{
    BorrowedSisOwner* owner = data;
    check(owner == borrowed_sis_owner && owner->canary == 0x51A51A51,
          "borrowed SIS descriptor owner remains alive through the generic GObj sweep");
    check(borrowed_sis_drain_seen && HSD_SisLib_804D1124[1] == NULL,
          "checked borrowed SIS slot retires after source text drain and before generic sweep");
    borrowed_sis_sweep_seen = 1;
}

static int vs_borrowed_sis_shutdown(MeleeWebGameplayVSSisOperation operation,
                                   int slot, void* expected,
                                   char* failure, size_t size)
{
    if (!melee_web_gameplay_vs_sis(operation, slot, expected, failure, size)) return 0;
    if (operation != MELEE_WEB_VS_SIS_DRAIN) return 1;
    check(borrowed_sis_owner && borrowed_sis_owner->canary == 0x51A51A51,
          "borrowed SIS owner remains alive during original text teardown");
    check(HSD_SisLib_804D7978 == NULL && HSD_SisLib_804D797C == NULL,
          "original SIS shutdown drains source text and context lists");
    if (borrowed_sis_text_gobj) {
        int linked = 0;
        for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
            for (HSD_GObj* object = ((HSD_GObj**)HSD_GObj_Entities)[link];
                 object; object = object->next)
                if (object == borrowed_sis_text_gobj) linked = 1;
        check(!linked, "original SIS shutdown removes its text GObj before the generic sweep");
    }
    check(HSD_SisLib_804D1124[1] == borrowed_sis_descriptor ||
              HSD_SisLib_804D1124[1] == NULL,
          "source shutdown preserves only the expected or unpublished borrowed descriptor");
    borrowed_sis_drain_seen = 1;
    ++borrowed_sis_shutdown_calls;
    if (borrowed_sis_replace_after_drain)
        HSD_SisLib_804D1124[1] = &borrowed_sis_foreign_after_drain;
    return 1;
}

static void put_be32(uint8_t* bytes, size_t offset, uint32_t value)
{
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

static void make_borrowed_sis_archive(uint8_t* bytes, size_t size)
{
    const char symbol[] = "BorrowedSIS";
    check(size == 0x20 + 4 + 8 + sizeof(symbol),
          "synthetic SIS archive has its exact header, data, public row and name extent");
    memset(bytes, 0, size);
    put_be32(bytes, 0, (uint32_t)size);
    put_be32(bytes, 4, 4);
    put_be32(bytes, 12, 1);
    put_be32(bytes, 0x20 + 4, 0);
    put_be32(bytes, 0x20 + 8, 0);
    memcpy(bytes + 0x20 + 4 + 8, symbol, sizeof(symbol));
}

static int borrowed_sis_lifetime(unsigned generation, int publish_descriptor,
                                 int exercise_foreign_replacement)
{
    static uint8_t archive_bytes[0x20 + 4 + 8 + sizeof("BorrowedSIS")];
    make_borrowed_sis_archive(archive_bytes, sizeof(archive_bytes));
    /* Real synthetic DAT bytes establish the manager's exact RuntimeFiles
     * name; the existing typed preload owns its native rumble rows. */
    uint8_t rumble_bytes[0x20 + sizeof(vs_rumble_rows) + 8 + sizeof("lbRumbleData")];
    memset(rumble_bytes, 0, sizeof(rumble_bytes));
    put_be32(rumble_bytes, 0, sizeof(rumble_bytes));
    put_be32(rumble_bytes, 4, sizeof(vs_rumble_rows));
    put_be32(rumble_bytes, 12, 1);
    memcpy(rumble_bytes + 0x20 + sizeof(vs_rumble_rows) + 8,
           "lbRumbleData", sizeof("lbRumbleData"));
    uint8_t original_archive[sizeof(archive_bytes)], original_rumble[sizeof(rumble_bytes)];
    memcpy(original_archive, archive_bytes, sizeof(original_archive));
    memcpy(original_rumble, rumble_bytes, sizeof(original_rumble));
    const MeleeWebSourceFileInput source_files[] = {
        {"BorrowedSIS.usd", archive_bytes, sizeof(archive_bytes)},
        {"LbRb.dat", rumble_bytes, sizeof(rumble_bytes)}};
    static SIS* source_owned_table[1];
    const MeleeWebArchiveSymbol source_symbols[] = {
        {"LbRb.dat", "lbRumbleData", vs_rumble_rows},
        {"BorrowedSIS.usd", "BorrowedSIS", source_owned_table}};
    vs_archive_scope = melee_web_archive_sections_register(
        source_symbols, 2, error, sizeof(error));
    check(vs_archive_scope != NULL, "VS manager has its typed rumble source");
    MeleeWebSourceFileScope* source_scope = melee_web_source_files_begin(
        source_files, 2, error, sizeof(error));
    check(source_scope != NULL, "synthetic source archive file scope opens");

    check(melee_web_gameplay_prepare_vs_startup(vs_manager_startup,
                                                generation ? vs_borrowed_sis_shutdown : vs_sis_shutdown,
                                                error, sizeof(error)),
          "VS scene configures source SIS startup and checked teardown");
    check(melee_web_gameplay_startup(4U * 1024U * 1024U, error, sizeof(error)),
          "source manager and original SIS heap start for a borrowed lifetime");
    check(melee_web_native_world_enable(error, sizeof(error)),
          "source manager's original HSD GObj cleanup owner initializes");
    check(HSD_SisLib_FontSlotBorrowable(-1) == 0 &&
              HSD_SisLib_FontSlotBorrowable(5) == 0,
          "source archive-owner accessor rejects indices outside its authored array");

    HSD_SisLib_803A62A0(2, "BorrowedSIS.usd", "BorrowedSIS");
    SIS* source_owned_descriptor = HSD_SisLib_804D1124[2];
    check(source_owned_descriptor != NULL &&
              !HSD_SisLib_FontSlotBorrowable(2),
          "original SIS loader installs a real source-owned archive in font slot 2");
    check(!melee_web_gameplay_vs_register_borrowed_sis(
              2, source_owned_descriptor, error, sizeof(error)),
          "checked borrowed registration rejects a live source-owned archive slot");
    check(HSD_SisLib_804D1124[2] == source_owned_descriptor &&
              !HSD_SisLib_FontSlotBorrowable(2),
          "source-owned archive refusal leaves its original descriptor and archive untouched");

    if (generation == 0) {
        check(source_owned_descriptor == (SIS*)source_owned_table,
              "original SIS loader resolves the hydrated synthetic public table");
        check(melee_web_gameplay_shutdown(error, sizeof(error)),
              "reduced source-owned loader fixture drains original SIS and world");
        check(HSD_SisLib_804D1124[2] == NULL && HSD_SisLib_FontSlotBorrowable(2),
              "original SIS shutdown releases its actual archive owner");
        check(melee_web_rumble_clear_source_rows(vs_rumble_rows, error, sizeof(error)),
              "reduced loader clears source rumble borrow after shutdown");
        check(melee_web_archive_sections_close(vs_archive_scope, error, sizeof(error)),
              "reduced loader closes native symbols after original archive release");
        vs_archive_scope = NULL;
        check(melee_web_source_files_end(source_scope, error, sizeof(error)),
              "reduced loader closes its owned RuntimeFiles scope");
        check(!memcmp(original_archive, archive_bytes, sizeof(original_archive)) &&
                  !memcmp(original_rumble, rumble_bytes, sizeof(original_rumble)),
              "reduced loader preserves immutable original fixture bytes");
        return 1;
    }

    BorrowedSisOwner owner = {0};
    owner.canary = 0x51A51A51;
    owner.table[0] = &owner.sis;
    borrowed_sis_owner = &owner;
    borrowed_sis_descriptor = (SIS*)owner.table;
    borrowed_sis_text_gobj = NULL;
    borrowed_sis_drain_seen = 0;
    borrowed_sis_sweep_seen = 0;
    borrowed_sis_shutdown_calls = 0;
    borrowed_sis_replace_after_drain = generation == 4;

    static SIS* prior_foreign_table[1];
    if (generation == 2) {
        prior_foreign_table[0] = &owner.sis;
        HSD_SisLib_804D1124[1] = (SIS*)prior_foreign_table;
        check(!melee_web_gameplay_vs_register_borrowed_sis(
                  1, borrowed_sis_descriptor, error, sizeof(error)),
              "borrowed registration rejects a nonempty prior descriptor slot");
        check(HSD_SisLib_804D1124[1] == (SIS*)prior_foreign_table,
              "nonempty prior slot refusal preserves its existing descriptor");
        HSD_SisLib_804D1124[1] = NULL;
    }

    check(melee_web_gameplay_vs_register_borrowed_sis(
              1, borrowed_sis_descriptor, error, sizeof(error)),
          "one borrowed descriptor registers against an empty source slot");
    if (publish_descriptor) {
        /* This harness publication mirrors the original Stadium assignment;
         * it does not claim a natural browser or stage transition. */
        HSD_SisLib_804D1124[1] = borrowed_sis_descriptor;
    }

    HSD_GObj* parent = GObj_Create(0, 0x20, 0);
    check(parent != NULL, "borrowed SIS fixture creates an owner parent GObj");
    GObj_InitUserData(parent, 0, borrowed_sis_parent_removed, &owner);
    check(HSD_SisLib_803A611C(1, parent, 9, 0x0d, 0, 1, 0, 0) == 0,
          "original SIS context binds font 1 to the parent GObj");
    HSD_Text* text = HSD_SisLib_803A5ACC(1, 0, 1.0F, 2.0F, 0.0F, 0.0F, 0.0F);
    check(text != NULL, "original SIS text object is created");
    if (publish_descriptor) {
        HSD_SisLib_803A6368(text, 0);
        check(text->sis_buffer == &owner.sis,
              "original SIS text resolves its buffer through the borrowed descriptor table");
    }
    borrowed_sis_text_gobj = text->entity;
    check(borrowed_sis_text_gobj != NULL && HSD_SisLib_804D7978 == text,
          "original SIS text has a source GObj and remains on the SIS text list");

    if (exercise_foreign_replacement) {
        static SIS foreign_descriptor;
        HSD_SisLib_804D1124[1] = &foreign_descriptor;
        check(!melee_web_gameplay_shutdown(error, sizeof(error)),
              "shutdown preflight rejects a foreign SIS descriptor replacement");
        check(HSD_SisLib_804D1124[1] == &foreign_descriptor &&
                  HSD_SisLib_804D7978 == text && !borrowed_sis_drain_seen,
              "foreign replacement refusal leaves the descriptor and source text untouched");
        HSD_SisLib_804D1124[1] = borrowed_sis_descriptor;
    }

    if (borrowed_sis_replace_after_drain) {
        check(!melee_web_gameplay_shutdown(error, sizeof(error)),
              "foreign replacement after original drain retains a blocked world");
        MeleeWebGameplayBootstrapState state;
        check(melee_web_gameplay_bootstrap_state(&state, sizeof(state)) &&
                  state.shutting_down == 2 && state.vs_sis_live == 0 &&
                  state.tables_live == 1 && borrowed_sis_shutdown_calls == 1 &&
                  borrowed_sis_drain_seen && !borrowed_sis_sweep_seen &&
                  HSD_SisLib_804D1124[1] == &borrowed_sis_foreign_after_drain &&
                  owner.canary == 0x51A51A51,
              "post-drain refusal retains descriptor owner and generic objects");
        check(!melee_web_gameplay_step(error, sizeof(error)) &&
                  !melee_web_gameplay_vs_startup_active() &&
                  melee_web_gameplay_generation() == 0 &&
                  melee_web_gameplay_stats().generation == 0,
              "retained partial teardown cannot step or publish a live world");
        check(!melee_web_gameplay_shutdown(error, sizeof(error)) &&
                  borrowed_sis_shutdown_calls == 1 && !borrowed_sis_sweep_seen,
              "unrepaired retry fails without repeating source drain or sweeping");
        HSD_SisLib_804D1124[1] = borrowed_sis_descriptor;
    }
    check(melee_web_gameplay_shutdown(error, sizeof(error)),
          "original SIS drain retires the checked borrowed slot before GObj sweep");
    check(borrowed_sis_shutdown_calls == 1,
          "original SIS callback executes exactly once including a retained retry");
    check(borrowed_sis_drain_seen && borrowed_sis_sweep_seen,
          "borrowed descriptor owner spans source text drain and generic object sweep");
    check(HSD_SisLib_804D1124[1] == NULL &&
              HSD_SisLib_FontSlotBorrowable(1) &&
              HSD_SisLib_804D1124[2] == NULL &&
              HSD_SisLib_FontSlotBorrowable(2),
          "borrowed slot is retired and source-owned archive is released by original shutdown");
    check(owner.canary == 0x51A51A51,
          "descriptor allocation owner remains live through completed teardown");
    check(melee_web_rumble_clear_source_rows(vs_rumble_rows, error, sizeof(error)),
          "borrowed rumble rows release after world shutdown");
    check(melee_web_archive_sections_close(vs_archive_scope, error, sizeof(error)),
          "typed rumble archive scope closes after source manager teardown");
    vs_archive_scope = NULL;
    check(melee_web_source_files_end(source_scope, error, sizeof(error)),
          "synthetic source archive scope closes after original shutdown");
    check(!memcmp(original_archive, archive_bytes, sizeof(original_archive)) &&
              !memcmp(original_rumble, rumble_bytes, sizeof(original_rumble)),
          "original source loading leaves both fixture byte vectors immutable");
    borrowed_sis_owner = NULL;
    borrowed_sis_descriptor = NULL;
    return 1;
}

static int borrowed_sis_case(void)
{
    check(borrowed_sis_lifetime(1, 1, 1),
          "first published borrowed SIS lifetime completes");
    check(borrowed_sis_lifetime(2, 1, 0),
          "second published borrowed SIS lifetime completes without stale binding");
    check(borrowed_sis_lifetime(3, 0, 0),
          "incomplete borrowed SIS publication completes without a stale descriptor");
    check(borrowed_sis_lifetime(4, 1, 0),
          "post-drain foreign replacement blocks gameplay until checked retry");
    puts("Checked borrowed SIS source text lifetime trace: passed");
    return 0;
}

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

static int retained_session_case(void)
{
    const size_t bytes = 1024 * 1024;
    check(!melee_web_gameplay_session_active(), "fresh process has no gameplay session");
    check(!melee_web_gameplay_session_begin_with_pattern(bytes, 256, error, sizeof(error)) &&
              !melee_web_gameplay_session_active() &&
              melee_web_gameplay_allocation().identity == 0,
          "session arena pattern rejects values outside one byte before allocation");
    check(melee_web_gameplay_session_begin_with_pattern(bytes, 0xA5, error, sizeof(error)),
          "diagnostic session arena is patterned at allocation");
    const void* backing = NULL;
    size_t backing_bytes = 0;
    check(melee_web_gameplay_session_arena(&backing, &backing_bytes) &&
              backing != NULL && backing_bytes == bytes,
          "pattern diagnostic publishes only its new session arena");
    const unsigned char* pattern = (const unsigned char*) backing;
    for (size_t i = 0; i < bytes; ++i)
        check(pattern[i] == 0xA5, "pattern diagnostic fills exactly the new session arena");
    MeleeWebGameplayAllocation allocation = melee_web_gameplay_allocation();
    check(melee_web_gameplay_session_active() && allocation.identity != 0 &&
              allocation.generation != 0 && allocation.bytes == bytes,
          "session exposes its backing allocation identity and size");
    check(!melee_web_gameplay_session_begin_with_pattern(bytes, 0, error, sizeof(error)) &&
              melee_web_gameplay_allocation().identity == allocation.identity,
          "pattern refusal leaves an existing session allocation unchanged");
    check(!melee_web_gameplay_startup(bytes + 32, error, sizeof(error)),
          "session rejects a world with a different arena size");
    check(melee_web_gameplay_startup(bytes, error, sizeof(error)),
          "session world starts over its retained arena");
    const MeleeWebGameplayAllocation live = melee_web_gameplay_allocation();
    check(live.identity == allocation.identity && live.generation == allocation.generation &&
              live.bytes == allocation.bytes,
          "world startup preserves the session allocation identity");

    const int free_before_probe = OSCheckHeap(0);
    check(free_before_probe > 0x10000, "original SDK heap leaves room for the retention probe");
    unsigned char* filler = (unsigned char*) OSAlloc((u32) free_before_probe - 0x10000U);
    check(filler != NULL, "original SDK allocation reserves the retention guard");
    unsigned char* retained = (unsigned char*) OSAlloc(0x400);
    check(retained != NULL, "original SDK allocation reserves the retention probe");
    (void) filler;
    memset(retained, 0xA5, 0x400);
    check(!melee_web_gameplay_session_end(error, sizeof(error)),
          "session cannot end while its world is live");
    check(melee_web_gameplay_shutdown(error, sizeof(error)),
          "session world shuts down without releasing its arena");
    for (unsigned i = 0; i < 0x400; ++i)
        check(retained[i] == 0xA5, "world teardown does not clear retained arena bytes");
    check(melee_web_gameplay_session_active(), "session remains active after world shutdown");
    check(melee_web_gameplay_startup(bytes, error, sizeof(error)),
          "next world recreates the original SDK heap in place");
    const MeleeWebGameplayAllocation restarted = melee_web_gameplay_allocation();
    check(restarted.identity == allocation.identity &&
              restarted.generation == allocation.generation && restarted.bytes == allocation.bytes,
          "recreated world keeps allocation identity generation and size");
    for (unsigned i = 0; i < 0x400; ++i)
        check(retained[i] == 0xA5, "same-arena reset retains payload bytes");
    check(melee_web_gameplay_shutdown(error, sizeof(error)),
          "recreated session world shuts down");
    check(melee_web_gameplay_session_end(error, sizeof(error)),
          "session release frees the backing arena after all worlds close");
    check(!melee_web_gameplay_session_active() &&
              melee_web_gameplay_allocation().identity == 0,
          "session release clears the allocation identity");
    puts("Original gameplay session arena retention trace: passed");
    return 0;
}

static int vs_preload_case(void)
{
    const MeleeWebArchiveSymbol rumble_symbol={
        "LbRb.dat","lbRumbleData",vs_rumble_rows};
    vs_archive_scope=melee_web_archive_sections_register(
        &rumble_symbol,1,error,sizeof(error));
    check(vs_archive_scope!=NULL,"VS manager has its typed rumble source");
    check(melee_web_gameplay_prepare_vs_startup(vs_manager_startup,
                                                vs_sis_shutdown,
                                                error, sizeof(error)),
          "VS scene configures the original scene-manager owner");
    check(!melee_web_gameplay_initialize_vs_dynamics(error, sizeof(error)),
          "VS dynamics remain unavailable before scene initialization");
    check(melee_web_gameplay_startup(4U * 1024U * 1024U, error, sizeof(error)),
          "original VS scene manager starts inside the owned world");
    check(melee_web_native_world_enable(error,sizeof(error)),
          "source-created HSD kind registry adopts native lifetime cleanup");
    check(DevText_GetGObj()!=NULL,
          "original manager registers its DevText render owner");
    check(melee_web_source_devtext_camera()==NULL,
          "DevText camera ownership is not claimed before its lazy source draw allocation");
    check(HSD_SObjLib_804D7960==0&&HSD_GObj_CameraKind==1&&
          HSD_GObj_LightKind==2&&HSD_GObj_JObjKind==3&&HSD_GObj_FogKind==4,
          "original manager publishes the source SObj/camera/light/JObj/fog IDs");
    const unsigned objects_before_camera=melee_web_gameplay_stats().objects;
    MeleeWebRender* camera=melee_web_render_prepare_match_camera(error,sizeof(error));
    check(camera!=NULL,"source match camera can be prepared at the scene-entry boundary");
    check(melee_web_gameplay_stats().objects==objects_before_camera+1&&
          Camera_80030A50()!=NULL&&Camera_80030A50()->obj_kind==HSD_GObj_CameraKind,
          "source camera preparation owns only Camera_80030688's original GObj");
    check(!melee_web_render_use_match_passes(camera,error,sizeof(error)),
          "source camera cannot render before Ground camera setup");
    check(melee_web_render_end(camera,error,sizeof(error)),
          "prepared source camera releases both source CObjs through their registered destructor");
    check(melee_web_gameplay_stats().objects==objects_before_camera,
          "prepared source camera teardown returns the original GObj count");
    void* sis_block = HSD_SisLib_Alloc(0x4000);
    check(sis_block != NULL, "original 0x4800 SIS heap serves an owned allocation");
    HSD_SisLib_Free(sis_block);
    check(melee_web_gameplay_initialize_vs_dynamics(error, sizeof(error)),
          "original VS dynamics pool initializes at its scene boundary");
    check(!melee_web_gameplay_initialize_vs_dynamics(error, sizeof(error)),
          "VS dynamics pool cannot initialize twice in one world");
    check(melee_web_gameplay_shutdown(error, sizeof(error)),
          "VS startup owners release before the SDK heap");
    check(melee_web_rumble_clear_source_rows(vs_rumble_rows,error,sizeof(error)),
          "source manager's borrowed rumble rows release after world shutdown");
    check(melee_web_archive_sections_close(vs_archive_scope,error,sizeof(error)),
          "typed rumble archive scope closes after source manager teardown");
    vs_archive_scope=NULL;
    puts("Original VS scene manager, HSD kinds and dynamics lifecycle trace: passed");
    return 0;
}

int main(int argc, char** argv)
{
    if (argc == 2 && !strcmp(argv[1], "replaced_heap")) return replaced_heap_case();
    if (argc == 2 && !strcmp(argv[1], "retained_session")) return retained_session_case();
    if (argc == 2 && !strcmp(argv[1], "source_owned_sis")) {
        check(borrowed_sis_lifetime(0, 0, 0), "reduced source-owned SIS loader lifetime");
        puts("Original source-owned SIS loader trace: passed");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "borrowed_sis")) return borrowed_sis_case();
    if (argc == 2 && !strcmp(argv[1], "vs_preload")) return vs_preload_case();
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
