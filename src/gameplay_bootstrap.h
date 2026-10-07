#ifndef MELEE_WEB_GAMEPLAY_BOOTSTRAP_H
#define MELEE_WEB_GAMEPLAY_BOOTSTRAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MeleeWebGameplayStats {
    uint64_t ticks;
    uint32_t objects, processes, object_peak, process_peak;
    int32_t heap_free_bytes;
    uint64_t generation; /* Changes when a fresh arena/world is created. */
} MeleeWebGameplayStats;

/* Backing allocation identity is stable across worlds while an application
 * session owns the arena. It is numeric only for lifecycle diagnostics; no
 * caller may dereference it. All fields are zero after final session release. */
typedef struct MeleeWebGameplayAllocation {
    uint64_t identity;
    uint64_t generation;
    uint64_t bytes;
} MeleeWebGameplayAllocation;

/* Read-only identity of every private scalar/pointer/callback owned by
 * gameplay_bootstrap.c. Pointer and function fields are numeric identities;
 * this record never grants a caller permission to dereference or restore
 * them. The fixed layout is Wasm32-only and is diagnostic, not a snapshot
 * admission or restore format. */
typedef struct MeleeWebGameplayBootstrapState {
    uint32_t abi_version;
    uint32_t abi_size;
    uint32_t schema;
    uint32_t vs_borrowed_sis_slot;
    uint64_t ticks;
    uint64_t disabled_links;
    uint64_t generation;
    uint64_t allocation_generation;
    uint64_t arena_bytes;
    uint64_t session_bytes;
    uint32_t arena_identity;
    uint32_t session_identity;
    int32_t heap_handle;
    uint32_t object_kind_count;
    uint32_t stepping;
    uint32_t shutting_down; /* 0 idle, 1 active, 2 retained after SIS drain. */
    uint32_t tables_live;
    uint32_t vs_startup_pending;
    uint32_t startup_in_progress;
    uint32_t vs_sis_live;
    uint32_t vs_dynamics_ready;
    uint32_t vs_manager_ready;
    uint32_t vs_startup_callback;
    uint32_t vs_shutdown_callback;
    uint32_t finish_hsd_objects;
    uint32_t vs_borrowed_sis_descriptor;
} MeleeWebGameplayBootstrapState;

enum {
    MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_ABI_VERSION = 1,
    MELEE_WEB_GAMEPLAY_BOOTSTRAP_STATE_SCHEMA = 0x47504232u /* GPB2 */,
};

#if defined(__cplusplus)
static_assert(sizeof(MeleeWebGameplayBootstrapState) == 128,
              "Bootstrap state ABI must remain 128 bytes");
static_assert(offsetof(MeleeWebGameplayBootstrapState, ticks) == 16 &&
              offsetof(MeleeWebGameplayBootstrapState, arena_identity) == 64 &&
              offsetof(MeleeWebGameplayBootstrapState, heap_handle) == 72 &&
              offsetof(MeleeWebGameplayBootstrapState, stepping) == 80 &&
              offsetof(MeleeWebGameplayBootstrapState, vs_startup_callback) == 112 &&
              offsetof(MeleeWebGameplayBootstrapState, vs_borrowed_sis_descriptor) == 124,
              "Bootstrap state ABI offsets changed");
#else
_Static_assert(sizeof(MeleeWebGameplayBootstrapState) == 128,
               "Bootstrap state ABI must remain 128 bytes");
_Static_assert(offsetof(MeleeWebGameplayBootstrapState, ticks) == 16 &&
               offsetof(MeleeWebGameplayBootstrapState, arena_identity) == 64 &&
               offsetof(MeleeWebGameplayBootstrapState, heap_handle) == 72 &&
               offsetof(MeleeWebGameplayBootstrapState, stepping) == 80 &&
               offsetof(MeleeWebGameplayBootstrapState, vs_startup_callback) == 112 &&
               offsetof(MeleeWebGameplayBootstrapState, vs_borrowed_sis_descriptor) == 124,
               "Bootstrap state ABI offsets changed");
#endif

/* Reserve one backing arena for an application session. The arena is not
 * initialized as an SDK heap until the first world starts. A session keeps the
 * allocation and its payload bytes across world shutdown/startup; ordinary
 * startup/shutdown retains its existing malloc/free behavior. */
int melee_web_gameplay_session_begin(size_t heap_bytes, char* error, size_t error_size);
/* Opt-in diagnostic allocation path. Fills only the newly allocated session
 * arena before publishing it; -1 preserves malloc's bytes. Other live SDK
 * owners or allocator state cause refusal. */
int melee_web_gameplay_session_begin_with_pattern(size_t heap_bytes, int pattern,
                                                  char* error, size_t error_size);
int melee_web_gameplay_session_end(char* error, size_t error_size);
/* Read-only view of the current session arena; 0 when no session is owned. */
int melee_web_gameplay_session_arena(const void** base, size_t* bytes);
int melee_web_gameplay_session_active(void);
MeleeWebGameplayAllocation melee_web_gameplay_allocation(void);
/* Public prototype for the typed read-only owner producer. */
int melee_web_gameplay_bootstrap_state(MeleeWebGameplayBootstrapState* out,
                                       uint32_t out_size);

/* Owns a single isolated SDK heap and the HSD process tables. This nonrendering
 * bootstrap executes original allocation, object lifecycle and scheduling;
 * it does not initialize Fighter, stage, audio or graphics-object destructors.
 * GObj_Create/HSD_GObj_SetupProc remain the original APIs. Do not attach an
 * HSD render object until the full object-kind initialization is integrated.
 * Startup rejects an existing HSD object world or SDK heap. */
int melee_web_gameplay_startup(size_t heap_bytes, char* error, size_t error_size);
/* Configure the next world for original VS scene-manager startup. The callback
 * prepares source HSD components, starts the source SIS owner, and invokes the
 * original gm_801A4BD4 manager, which creates the GObj tables itself. */
typedef int (*MeleeWebGameplayVSStartup)(char* error, size_t error_size);
typedef void (*MeleeWebGameplayVSShutdown)(void);
int melee_web_gameplay_prepare_vs_startup(MeleeWebGameplayVSStartup startup,
                                          MeleeWebGameplayVSShutdown shutdown,
                                          char* error, size_t error_size);
/* Register one externally-owned SIS descriptor before source code publishes it
 * to the global font table. The caller keeps that descriptor's allocation
 * alive through gameplay shutdown and its generic GObj sweep. A failed
 * initial shutdown preflight leaves the world and every source SIS slot unchanged.
 * Failure after source drain retains the owner and blocks gameplay until a
 * checked shutdown retry succeeds. */
int melee_web_gameplay_vs_register_borrowed_sis(int font_slot,
                                                void* descriptor,
                                                char* error,
                                                size_t error_size);
int melee_web_gameplay_vs_startup_active(void);
/* True only while the configured VS callback owns the heap and generation,
 * before the original scene manager publishes its GObj tables. */
int melee_web_gameplay_vs_manager_preparing(void);
int melee_web_gameplay_initialize_vs_dynamics(char* error, size_t error_size);
int melee_web_gameplay_step(char* error, size_t error_size);
int melee_web_gameplay_shutdown(char* error, size_t error_size);
MeleeWebGameplayStats melee_web_gameplay_stats(void);
/* Returns the live world's generation without walking SDK heap statistics.
 * Zero means that the tables are not live, teardown is in progress, or the
 * SDK allocator no longer owns the bootstrap arena. */
uint64_t melee_web_gameplay_generation(void);
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
/* Private read-only source tick; does not walk the SDK heap. */
uint64_t melee_web_gameplay_provenance_tick(void);
#endif
/* Remains true during teardown, until the owned SDK arena is released. */
int melee_web_gameplay_world_exists(void);

/* Optional native HSD lifetime lane. Installs the original camera/light/joint/
 * fog destructor registry, without creating or rendering any such objects.
 * A single owner supplies the post-object class/ID cleanup before arena release.
 * Re-registering the same callback is idempotent within the current world. */
int melee_web_gameplay_enable_hsd_objects(void (*after_objects)(void),
                                         char* error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
