#include "gameplay_results_context.h"

#include "gameplay_bootstrap.h"
#include "gameplay_collision.h"
#include "gameplay_fighter_assets.h"
#include "gameplay_match_clock.h"
#include "hsd_native_joint.h"

#include <melee/cm/camera.h>
#include <melee/cm/types.h>
#include <melee/ft/ftdevice.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1A36.h>
#include <melee/gm/gmresult.h>
#include <melee/gm/types.h>
#include <melee/gr/ground.h>
#include <melee/gr/stage.h>
#include <melee/gr/types.h>
#include <melee/it/item.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbcardgame.h>
#include <melee/lb/lbcardnew.h>
#include <melee/lb/lblanguage.h>
#include <melee/pl/player.h>
#include <melee/ty/toy.h>
#include <melee/ty/tydisplay.h>
#include <melee/ty/types.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/initialize.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/rumble.h>
#include <sysdolphin/baselib/shadow.h>
#include <sysdolphin/baselib/sislib.h>
#include <sysdolphin/baselib/state.h>
#include <sysdolphin/baselib/video.h>

#include <dolphin/gx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* These are source-owned globals. Their declarations intentionally stay here:
 * this boundary saves/restores the original typed storage, rather than
 * maintaining a second Results ABI. */
extern void* melee_web_camera_state(void);
extern uint32_t melee_web_camera_pool_allocation_generation_value(void);
extern int melee_web_camera_pool_last_subject_count_value(void);
extern CmSubject *cm_804D6458, *cm_804D645C, *cm_804D6460, *cm_804D6468;
extern HSD_CObj* cm_804D6464;
extern CameraDebugMode cm_80453004;
extern u16 staleAttackInstance, unk_804D6480;
extern PadLibData default_libinfo_data;
extern HSD_PadStatus default_status_data;
extern HSD_RumbleData HSD_Rumble_804C22E0[4];
extern HSD_Archive *_Toy_sbss_804D6ED0, *_Toy_sbss_804D6ECC, *Toy_sbss_804D6EC8;
extern struct TrophyData *_Toy_sbss_804D6EC4, *_Toy_sbss_804D6EC0;
extern void *_Toy_sbss_804D6EBC, *_Toy_sbss_804D6EB8,
    *_Toy_sbss_804D6EA8, *_Toy_sbss_804D6EA4;
extern s16* _Toy_sbss_804D6EB4;
extern TyDspEntry *Toy_sbss_804D6EB0, *Toy_sbss_804D6EAC;

/* Results invokes this source allocator itself in fn_8017AA78. This accessor
 * is read-only here: a competing stage scope must fail before OnEnter. */
extern int melee_web_ground_map_storage_available(void);
extern int melee_web_ground_map_storage_end(void);
extern int melee_web_stage_selection_begin(int);
extern int melee_web_stage_selection_end(void);
extern int melee_web_audio_is_active(MeleeWebAudio*);
extern void melee_web_bg_flash_save_state(void);
extern void melee_web_bg_flash_restore_state(void);
extern HSD_GObj* melee_web_bg_flash_overlay_owner(void);
extern HSD_GObj* melee_web_bg_flash_camera_owner(void);
extern int melee_web_bg_flash_destroy(HSD_GObj*, HSD_GObj*);

struct MeleeWebResultsContext {
    struct ResultsMatchInfo match;
    MeleeWebAudio* audio;
    uint64_t generation, audio_generation;
    uint32_t seed;
    uint32_t ticks;
    uint32_t baseline_count;
    HSD_GObj** baseline;
    HSD_PadData queue;
    PadLibData saved_pad_library;
    HSD_PadStatus saved_game[4], saved_master[4], saved_copy[4];
    HSD_RumbleData saved_rumble[4];
    HSD_PadRumbleListData rumble_lists[12];
    StaticPlayer saved_players[6];
    StageInfo saved_stage;
    struct ftDeviceUnk3 saved_device1[1], saved_device3[1];
    struct ftDeviceUnk5 saved_device2[2];
    struct ftDeviceUnk4 saved_device4;
    int saved_device_count, saved_bury_count;
    Camera saved_camera;
    CameraDebugMode saved_camera_debug;
    CmSubject *saved_camera_free, *saved_camera_pool, *saved_camera_active,
        *saved_camera_tail;
    HSD_CObj* saved_camera_object;
    u32* saved_seed;
    u16 saved_stale, saved_attack;
    int saved_language, saved_saved_language;
    CmSubject* camera_pool;
    CmSubject *camera_pool_before_onenter, *camera_pool_after_onenter;
    CmSubject *camera_pool_after_collision_adoption, *owner_camera_pool_before_onenter;
    uint32_t camera_allocation_generation_before_onenter;
    uint32_t camera_allocation_generation_after_onenter, camera_allocation_generation;
    uint32_t camera_allocation_generation_after_collision_adoption;
    int camera_allocation_subject_count_after_onenter;
    int camera_allocation_subject_count_after_collision_adoption;
    uint32_t camera_pool_event_count;
    int camera_pool_event_overflow;
    MeleeWebResultsCameraPoolEvent camera_pool_events[
        MELEE_WEB_RESULTS_CAMERA_POOL_EVENT_CAPACITY];
    uint32_t camera_subject_event_count;
    int camera_subject_event_overflow;
    MeleeWebResultsCameraSubjectEvent camera_subject_events[
        MELEE_WEB_RESULTS_CAMERA_SUBJECT_EVENT_CAPACITY];
    MeleeWebResultsCameraEntrySnapshot* camera_snapshot_sink;
    MeleeWebCollision* collision;
    HSD_GObj *flash_overlay, *flash_camera;
    int scene_entered, drawing, transition, flash_saved;
};

static MeleeWebResultsContext* owner;
/* Retain the published allocation independently of the context being checked:
 * a changed context field must be distinguishable from a source-global write. */
static CmSubject* owner_camera_pool;

static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

static int ok(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
    return 1;
}

static int live(const MeleeWebResultsContext* context, char* error, size_t size)
{
    if (!context || context != owner || !context->generation ||
        context->generation != melee_web_gameplay_generation() ||
        !context->audio ||
        context->audio_generation != melee_web_audio_generation(context->audio) ||
        !melee_web_audio_is_active(context->audio))
        return fail(error, size, "Original Results ownership or audio changed");
    if (!melee_web_fighter_assets_check_owned("Results scene", error, size)) return 0;
    if (seed_ptr != &context->seed)
        return fail(error, size, "Original Results RNG ownership changed");
    return 1;
}

static int camera_pool_owned(const MeleeWebResultsContext* context,
                             const char* phase, const void* actor,
                             char* error, size_t size)
{
    uint32_t source_generation = melee_web_camera_pool_allocation_generation_value();
    int last_subject_count = melee_web_camera_pool_last_subject_count_value();
    if (cm_804D645C == context->camera_pool &&
        context->camera_pool == owner_camera_pool &&
        source_generation == context->camera_allocation_generation) return 1;
    const MeleeWebResultsCameraPoolEvent* last_event =
        context->camera_pool_event_count ?
            &context->camera_pool_events[context->camera_pool_event_count - 1] : NULL;
    char message[512];
    snprintf(message, sizeof(message),
             "Original Results camera pool ownership changed at %s tick=%u "
             "source=%p context_pool=%p initial=%p context=%p actor=%p "
             "source_allocation_generation=%u expected_generation=%u "
             "last_subject_count=%d camera_events=%u last_event=%s@%u "
             "last_event_pool=%p last_event_generation=%u",
             phase, context->ticks, (void*) cm_804D645C,
             (void*) context->camera_pool, (void*) owner_camera_pool,
             (const void*) context, actor, source_generation,
             context->camera_allocation_generation, last_subject_count,
             context->camera_pool_event_count,
             last_event ? last_event->phase : "none",
             last_event ? last_event->source_tick : 0,
             last_event ? (void*) last_event->source_pool : NULL,
             last_event ? last_event->allocation_generation : 0);
    fprintf(stderr, "%s\n", message);
    return fail(error, size, message);
}

void melee_web_results_camera_pool_allocation_trace(const char* phase,
                                                     int subject_count)
{
    MeleeWebResultsContext* context = owner;
    if (!context) return;
    if (context->camera_pool_event_count >=
        MELEE_WEB_RESULTS_CAMERA_POOL_EVENT_CAPACITY) {
        context->camera_pool_event_overflow = 1;
        return;
    }
    MeleeWebResultsCameraPoolEvent* event =
        &context->camera_pool_events[context->camera_pool_event_count++];
    memset(event, 0, sizeof(*event));
    snprintf(event->phase, sizeof(event->phase), "%s", phase);
    event->source_tick = context->ticks;
    event->scene_entered = context->scene_entered;
    event->subject_count = subject_count;
    event->source_free = cm_804D6458;
    event->source_pool = cm_804D645C;
    event->source_active = cm_804D6460;
    event->source_tail = cm_804D6468;
    event->owner_pool = owner_camera_pool;
    event->context_pool = context->camera_pool;
    event->expected_pool = context->camera_pool_after_onenter;
    event->allocation_generation =
        melee_web_camera_pool_allocation_generation_value();
    event->generation_before_onenter =
        context->camera_allocation_generation_before_onenter;
    event->expected_generation = context->camera_allocation_generation;
}

static int camera_pool_contains(const void* pointer, int allow_null)
{
    if (!pointer) return allow_null;
    if (!cm_804D645C) return 0;
    const int count = melee_web_camera_pool_last_subject_count_value();
    if (count <= 0) return 0;
    const uintptr_t begin = (uintptr_t) cm_804D645C;
    const uintptr_t address = (uintptr_t) pointer;
    const uintptr_t bytes = (uintptr_t) count * sizeof(CmSubject);
    return address >= begin && address - begin < bytes &&
           (address - begin) % sizeof(CmSubject) == 0;
}

void melee_web_results_camera_subject_list_trace(const char* phase,
                                                   const void* subject_pointer)
{
    MeleeWebResultsContext* context = owner;
    if (!context) return;
    if (context->camera_subject_event_count >=
        MELEE_WEB_RESULTS_CAMERA_SUBJECT_EVENT_CAPACITY) {
        context->camera_subject_event_overflow = 1;
        if (context->camera_snapshot_sink)
            context->camera_snapshot_sink->source_camera_subject_event_overflow = 1;
        return;
    }
    const int subject_in_pool = camera_pool_contains(subject_pointer, 0);
    const CmSubject* subject = subject_in_pool ? (const CmSubject*) subject_pointer : NULL;
    MeleeWebResultsCameraSubjectEvent* event =
        &context->camera_subject_events[context->camera_subject_event_count++];
    memset(event, 0, sizeof(*event));
    snprintf(event->phase, sizeof(event->phase), "%s", phase ? phase : "unknown");
    event->source_tick = context->ticks;
    event->scene_entered = context->scene_entered;
    event->subject = subject_pointer;
    event->subject_prev = subject ? subject->prev : NULL;
    event->subject_next = subject ? subject->next : NULL;
    event->source_free = cm_804D6458;
    event->source_pool = cm_804D645C;
    event->source_active = cm_804D6460;
    event->source_tail = cm_804D6468;
    event->owner_pool = owner_camera_pool;
    event->context_pool = context->camera_pool;
    event->allocation_generation =
        melee_web_camera_pool_allocation_generation_value();
    event->subject_in_pool = subject_in_pool;
    event->subject_prev_in_pool = camera_pool_contains(event->subject_prev, 1);
    event->subject_next_in_pool = camera_pool_contains(event->subject_next, 1);
    event->free_in_pool = camera_pool_contains(event->source_free, 1);
    event->active_in_pool = camera_pool_contains(event->source_active, 1);
    event->tail_in_pool = camera_pool_contains(event->source_tail, 1);

    MeleeWebResultsCameraEntrySnapshot* sink = context->camera_snapshot_sink;
    if (sink) {
        const uint32_t index = context->camera_subject_event_count - 1;
        sink->source_camera_subject_events[index] = *event;
        sink->source_camera_subject_event_count = context->camera_subject_event_count;
        sink->source_camera_subject_event_overflow = context->camera_subject_event_overflow;
    }
}

static int baseline_has(const MeleeWebResultsContext* context, HSD_GObj* object)
{
    for (uint32_t i = 0; i < context->baseline_count; ++i)
        if (context->baseline[i] == object) return 1;
    return 0;
}

static HSD_GObj* first_new_object_on_link(const MeleeWebResultsContext* context,
                                          unsigned link)
{
    for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link]; object;
         object = object->next)
        if (!baseline_has(context, object)) return object;
    return NULL;
}

static int capture_baseline(MeleeWebResultsContext* context, char* error, size_t size)
{
    uint32_t count = 0;
    if (!HSD_GObj_Entities || !HSD_GObjLibInitData.p_link_max)
        return fail(error, size, "Results scene requires the live source object registry");
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link]; object;
             object = object->next)
            ++count;
    if (count == 0) {
        context->baseline = NULL;
        context->baseline_count = 0;
        return ok(error, size);
    }
    context->baseline = calloc(count, sizeof(*context->baseline));
    if (!context->baseline)
        return fail(error, size, "Cannot retain the prepared Results object registry");
    for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max; ++link)
        for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link]; object;
             object = object->next)
            context->baseline[context->baseline_count++] = object;
    return ok(error, size);
}

static int delete_new_objects(MeleeWebResultsContext* context, char* error, size_t size)
{
    /* Original Items can retain a fighter owner during their destructor. */
    while (HSD_GObjLibInitData.p_link_max >= 9) {
        HSD_GObj* item = first_new_object_on_link(context, 9);
        if (!item) break;
        if (HSD_GObj_804D781C || HSD_GObj_804D7814 || HSD_GObj_804D7818)
            return fail(error, size,
                        "Results item teardown entered from an active source callback");
        Item_8026A8EC(item);
        if (!camera_pool_owned(context, "item destructor", item, error, size)) return 0;
        if (!melee_web_fighter_assets_check_owned("Results item destructor", error, size)) return 0;
    }
    /* Release demo fighters through the registered original destructor before
     * releasing their asset/action owners or the source camera pool. */
    for (unsigned slot = 0; slot < 6; ++slot) {
        StaticPlayer* player = Player_GetPtrForSlot(slot);
        for (unsigned entity = 0; entity < 2; ++entity) {
            HSD_GObj* fighter = player->player_entity[entity];
            if (!fighter) continue;
            HSD_GObjPLink_80390228(fighter);
            if (!camera_pool_owned(context, "fighter destructor", fighter, error, size)) return 0;
            char phase[96];
            snprintf(phase, sizeof(phase),
                     "Results fighter destructor slot=%u entity=%u", slot, entity);
            if (!melee_web_fighter_assets_check_owned(phase, error, size)) return 0;
        }
    }
    for (unsigned pass = 0; pass < 2048; ++pass) {
        HSD_GObj* target = NULL;
        for (unsigned link = 0; link <= HSD_GObjLibInitData.p_link_max && !target; ++link)
            for (HSD_GObj* object = ((HSD_GObj**) HSD_GObj_Entities)[link]; object;
                 object = object->next)
                if (!baseline_has(context, object)) {
                    target = object;
                    break;
                }
        if (!target) return ok(error, size);
        if (HSD_GObj_804D781C || HSD_GObj_804D7814 || HSD_GObj_804D7818)
            return fail(error, size, "Results GObj teardown entered from an active source callback");
        HSD_GObjPLink_80390228(target);
        if (!camera_pool_owned(context, "GObj destructor", target, error, size)) return 0;
        if (!melee_web_fighter_assets_check_owned("Results GObj destructor", error, size)) return 0;
    }
    return fail(error, size, "Results source object teardown exceeded its bounded object set");
}

static int release_source_camera_and_ground(MeleeWebResultsContext* context,
                                            char* error, size_t size)
{
    if (cm_804D6460 || cm_804D6468 || HSD_ShadowGetAllocData()->used)
        return fail(error, size, "Results camera subjects or shadows remain during teardown");
    if (!camera_pool_owned(context, "camera release", NULL, error, size)) return 0;
    melee_web_results_camera_subject_list_trace("Results before HSD_Free", NULL);
    if (context->camera_pool) HSD_Free(context->camera_pool);
    melee_web_results_camera_subject_list_trace("Results after HSD_Free", NULL);
    context->camera_pool = NULL;
    owner_camera_pool = NULL;
    cm_804D6458 = context->saved_camera_free;
    cm_804D645C = context->saved_camera_pool;
    cm_804D6460 = context->saved_camera_active;
    cm_804D6468 = context->saved_camera_tail;
    cm_804D6464 = context->saved_camera_object;
    melee_web_results_camera_subject_list_trace("Results after camera-global restore", NULL);
    if (!melee_web_ground_map_storage_end())
        return fail(error, size, "Original Results Ground storage still owns stage objects");
    stage_info = context->saved_stage;
    if (cm_804D6458 != context->saved_camera_free ||
        cm_804D645C != context->saved_camera_pool ||
        cm_804D6460 != context->saved_camera_active ||
        cm_804D6468 != context->saved_camera_tail ||
        cm_804D6464 != context->saved_camera_object || owner_camera_pool)
        return fail(error, size,
                    "Original Results camera globals were not restored after scene teardown");
    return ok(error, size);
}

static void restore_pad_and_source_globals(MeleeWebResultsContext* context)
{
    HSD_PadLibData = context->saved_pad_library;
    memcpy(HSD_PadGameStatus, context->saved_game, sizeof(context->saved_game));
    memcpy(HSD_PadMasterStatus, context->saved_master, sizeof(context->saved_master));
    memcpy(HSD_PadCopyStatus, context->saved_copy, sizeof(context->saved_copy));
    memcpy(HSD_Rumble_804C22E0, context->saved_rumble, sizeof(context->saved_rumble));
    for (unsigned i = 0; i < 4; ++i) PADControlMotor(i, PAD_MOTOR_STOP_HARD);
    memcpy(ft_80459A68, context->saved_device1, sizeof(context->saved_device1));
    memcpy(ftDevice_BuryThings, context->saved_device2, sizeof(context->saved_device2));
    memcpy(ft_80459A8C, context->saved_device3, sizeof(context->saved_device3));
    ft_804D6578 = context->saved_device4;
    ft_804D6570 = context->saved_device_count;
    ftDevice_BuryThingCount = context->saved_bury_count;
    for (unsigned slot = 0; slot < 6; ++slot)
        *Player_GetPtrForSlot(slot) = context->saved_players[slot];
    *(Camera*) melee_web_camera_state() = context->saved_camera;
    cm_80453004 = context->saved_camera_debug;
    seed_ptr = context->saved_seed;
    staleAttackInstance = context->saved_stale;
    unk_804D6480 = context->saved_attack;
    lbLang_SetLanguageSetting(context->saved_language);
    lbLang_SetSavedLanguage(context->saved_saved_language);
}


MeleeWebResultsContext* melee_web_results_context_begin(
    const struct ResultsMatchInfo* match, uint32_t seed,
    const MeleeWebPadState* input, MeleeWebAudio* audio,
    char* error, size_t error_size)
{
    const uint64_t generation = melee_web_gameplay_generation();
    MeleeWebResultsContext* context;
    Camera* camera;

    if (!match || !input || !audio || owner || !generation || !seed_ptr ||
        !melee_web_audio_is_active(audio) || !melee_web_audio_generation(audio) ||
        !melee_web_ground_map_storage_available() ||
        !melee_web_collision_source_available())
        return fail(error, error_size,
                    "Results requires a prepared world, active audio, and free Ground storage"), NULL;
    if (!melee_web_fighter_assets_check_owned("Results begin", error, error_size)) return NULL;
    /* Match teardown has completed. Mirror gm_1A3F's preloadState boundary:
     * retire scene-heap trophy aliases before checking that the Results scene
     * starts with no previous Toy archive owners. The source routine clears
     * aliases only; persistent trophy/profile state remains in save storage. */
    Toy_803127D4();
    tyDisplay_8031C8B8();
    if (_Toy_sbss_804D6ED0 || _Toy_sbss_804D6ECC || Toy_sbss_804D6EC8 ||
        _Toy_sbss_804D6EC4 || _Toy_sbss_804D6EC0 || _Toy_sbss_804D6EBC ||
        _Toy_sbss_804D6EB8 || _Toy_sbss_804D6EB4 || Toy_sbss_804D6EB0 ||
        Toy_sbss_804D6EAC || _Toy_sbss_804D6EA8 || _Toy_sbss_804D6EA4)
        return fail(error, error_size, "Results requires idle source trophy archive owners"), NULL;
    for (unsigned slot = 0; slot < 6; ++slot) {
        StaticPlayer* player = Player_GetPtrForSlot(slot);
        if (player->player_entity[0] || player->player_entity[1] || player->player_state)
            return fail(error, error_size, "Results requires idle source player slots"), NULL;
    }
    context = calloc(1, sizeof(*context));
    if (!context) return fail(error, error_size, "Cannot allocate Results context"), NULL;
    context->match = *match;
    context->audio = audio;
    context->audio_generation = melee_web_audio_generation(audio);
    context->generation = generation;
    context->seed = seed;
    context->saved_seed = seed_ptr;
    context->saved_camera = *(Camera*) melee_web_camera_state();
    context->saved_camera_debug = cm_80453004;
    context->saved_camera_free = cm_804D6458;
    context->saved_camera_pool = cm_804D645C;
    context->saved_camera_active = cm_804D6460;
    context->saved_camera_tail = cm_804D6468;
    context->saved_camera_object = cm_804D6464;
    context->saved_stage = stage_info;
    context->saved_stale = staleAttackInstance;
    context->saved_attack = unk_804D6480;
    context->saved_language = lbLang_GetLanguageSetting();
    context->saved_saved_language = lbLang_GetSavedLanguage();
    for (unsigned slot = 0; slot < 6; ++slot)
        context->saved_players[slot] = *Player_GetPtrForSlot(slot);
    memcpy(context->saved_device1, ft_80459A68, sizeof(context->saved_device1));
    memcpy(context->saved_device2, ftDevice_BuryThings, sizeof(context->saved_device2));
    memcpy(context->saved_device3, ft_80459A8C, sizeof(context->saved_device3));
    context->saved_device4 = ft_804D6578;
    context->saved_device_count = ft_804D6570;
    context->saved_bury_count = ftDevice_BuryThingCount;
    context->saved_pad_library = HSD_PadLibData;
    memcpy(context->saved_game, HSD_PadGameStatus, sizeof(context->saved_game));
    memcpy(context->saved_master, HSD_PadMasterStatus, sizeof(context->saved_master));
    memcpy(context->saved_copy, HSD_PadCopyStatus, sizeof(context->saved_copy));
    memcpy(context->saved_rumble, HSD_Rumble_804C22E0, sizeof(context->saved_rumble));
    camera = (Camera*) melee_web_camera_state();
    if (camera->gobj || context->saved_camera_free || context->saved_camera_pool ||
        context->saved_camera_active || context->saved_camera_tail || context->saved_camera_object ||
        HSD_ShadowGetAllocData()->used) {
        free(context);
        return fail(error, error_size, "Results requires an idle source camera and shadow pool"), NULL;
    }
    /* Native HSD initialization resets component allocator metadata. Complete
     * source ownership checks before allowing that destructive boundary. */
    if (!melee_web_native_world_enable(error, error_size)) {
        free(context);
        return NULL;
    }
    if (!capture_baseline(context, error, error_size)) {
        free(context);
        return NULL;
    }
    if (!melee_web_stage_selection_begin(St_Kind_Dummy)) {
        free(context->baseline); free(context);
        return fail(error, error_size, "Original stage selection is already owned"), NULL;
    }
    if (!melee_web_menu_clock_begin()) {
        if (!melee_web_stage_selection_end()) abort();
        free(context->baseline); free(context);
        return fail(error, error_size, "Original Results scene clock is already owned"), NULL;
    }
    owner = context;
    seed_ptr = &context->seed;
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    HSD_PadLibData = default_libinfo_data;
    HSD_PadLibData.rumble_info = context->saved_pad_library.rumble_info;
    HSD_PadRumbleInit(12, context->rumble_lists);
    HSD_PadLibData.qnum = 1;
    HSD_PadLibData.queue = &context->queue;
    HSD_PadLibData.clamp_stickType = 0;
    HSD_PadLibData.clamp_stickShift = 1;
    HSD_PadLibData.clamp_stickMax = 80;
    HSD_PadLibData.clamp_stickMin = 0;
    HSD_PadLibData.scale_stick = 80;
    HSD_PadLibData.clamp_analogLRShift = 1;
    HSD_PadLibData.clamp_analogLRMax = 140;
    HSD_PadLibData.clamp_analogLRMin = 0;
    HSD_PadLibData.scale_analogLR = 140;
    for (unsigned i = 0; i < 4; ++i)
        HSD_PadGameStatus[i] = HSD_PadMasterStatus[i] =
            HSD_PadCopyStatus[i] = default_status_data;
    melee_web_pad_state_apply(input);
    lbAudioAx_8002835C();
    HSD_SisLib_803A6048(0xC000);
    lb_8001C5A4();
    lb_8001D1F4();
    melee_web_bg_flash_save_state();
    context->flash_saved = 1;
    context->camera_allocation_generation_before_onenter =
        melee_web_camera_pool_allocation_generation_value();
    context->camera_pool_before_onenter = cm_804D645C;
    context->owner_camera_pool_before_onenter = owner_camera_pool;
    gm_Scene_Results_OnEnter(&context->match);
    context->camera_allocation_generation_after_onenter =
        melee_web_camera_pool_allocation_generation_value();
    context->camera_allocation_subject_count_after_onenter =
        melee_web_camera_pool_last_subject_count_value();
    context->camera_pool_after_onenter = cm_804D645C;
    if (context->camera_allocation_generation_after_onenter !=
            context->camera_allocation_generation_before_onenter + 1u ||
        context->camera_allocation_subject_count_after_onenter != 8 ||
        !context->camera_pool_after_onenter) {
        char message[256];
        snprintf(message, sizeof(message),
                 "Results OnEnter source camera allocation identity changed: "
                 "generation=%u->%u subjects=%d expected=8 pool=%p",
                 context->camera_allocation_generation_before_onenter,
                 context->camera_allocation_generation_after_onenter,
                 context->camera_allocation_subject_count_after_onenter,
                 (void*) context->camera_pool_after_onenter);
        fprintf(stderr, "%s\n", message);
        if (error && error_size) snprintf(error, error_size, "%s", message);
        abort();
    }
    context->camera_allocation_generation =
        context->camera_allocation_generation_after_onenter;
    context->collision=melee_web_collision_adopt_dummy(error,error_size);
    if(!context->collision){
        fprintf(stderr,"Results collision ownership failure: %s\n",error?error:"unavailable");
        abort();
    }
    context->camera_pool_after_collision_adoption = cm_804D645C;
    context->camera_allocation_generation_after_collision_adoption =
        melee_web_camera_pool_allocation_generation_value();
    context->camera_allocation_subject_count_after_collision_adoption =
        melee_web_camera_pool_last_subject_count_value();
    /* Collision adoption owns only the dummy-map process and arrays. It must
     * not replace the camera pool allocated by original Results OnEnter. */
    context->camera_pool = context->camera_pool_after_onenter;
    owner_camera_pool = context->camera_pool;
    if (!camera_pool_owned(context, "collision adoption", context->collision,
                           error, error_size)) {
        fprintf(stderr, "Results camera ownership failure after collision adoption: %s\n",
                error ? error : "unavailable");
        abort();
    }
    context->flash_overlay = melee_web_bg_flash_overlay_owner();
    context->flash_camera = melee_web_bg_flash_camera_owner();
    context->scene_entered = 1;
    if (!melee_web_fighter_assets_check_owned("Results OnEnter", error, error_size)) {
        /* A corrupt published asset scope cannot be silently disposed. */
        fprintf(stderr, "Results OnEnter ownership failure: %s\n", error ? error : "unavailable");
        abort();
    }
    if (error && error_size) *error = 0;
    return context;
}

int melee_web_results_context_tick(MeleeWebResultsContext* context,
                                   const PADStatus raw[4], char* error, size_t error_size)
{
    int request = 0;
    if (!live(context, error, error_size) || !context->scene_entered || context->drawing || !raw)
        return fail(error, error_size, "Results tick requires a live idle scene and four raw ports");
    if (!camera_pool_owned(context, "tick entry", NULL, error, error_size)) return 0;
    if (HSD_PadLibData.queue != &context->queue || HSD_PadLibData.qnum != 1 ||
        HSD_PadLibData.qcount)
        return fail(error, error_size, "Results raw PAD queue is not owned and idle");
    memset(&context->queue, 0, sizeof(context->queue));
    memcpy(context->queue.stat, raw, sizeof(context->queue.stat));
    HSD_PadLibData.qread = HSD_PadLibData.qwrite = 0;
    HSD_PadLibData.qcount = 1;
    HSD_PadRumbleInterpret();
    HSD_PadRenewMasterStatus();
    HSD_PadRenewCopyStatus();
    HSD_PadRenewGameStatus();
    if (HSD_PadLibData.qcount)
        return fail(error, error_size, "Original Results PAD processing did not consume its sample");
    gm_EvaluateAllControllerInputs();
    lbAudioAx_80027DF8();
    if (!camera_pool_owned(context, "input/audio", NULL, error, error_size)) return 0;
    if (!melee_web_gameplay_step(error, error_size)) return 0;
    if (!camera_pool_owned(context, "source step", NULL, error, error_size)) return 0;
    if (!melee_web_menu_clock_tick())
        return fail(error, error_size, "Original Results scene clock lost frame ownership");
    if (!melee_web_menu_clock_request(&request))
        return fail(error, error_size, "Original Results transition state is unavailable");
    context->transition = request;
    context->ticks++;
    return ok(error, error_size);
}

int melee_web_results_context_draw(MeleeWebResultsContext* context,
                                   char* error, size_t error_size)
{
    if (!live(context, error, error_size) || !context->scene_entered || context->drawing)
        return fail(error, error_size, "Results draw requires a live idle scene");
    if (!camera_pool_owned(context, "draw entry", NULL, error, error_size)) return 0;
    if (context->transition == 2) return ok(error, error_size);
    context->drawing = 1;
    GXRenderModeObj saved_mode = *HSD_VIGetRenderMode();
    *HSD_VIGetRenderMode() = GXNtsc480IntDf;
    GXInvalidateVtxCache();
    GXInvalidateTexAll();
    HSD_StartRender(HSD_RP_SCREEN);
    HSD_StateInvalidate(HSD_STATE_ALL);
    HSD_GObj_80390FC0();
    HSD_Init_803755A8();
    *HSD_VIGetRenderMode() = saved_mode;
    context->drawing = 0;
    if (!camera_pool_owned(context, "source draw", NULL, error, error_size)) return 0;
    if (!melee_web_menu_clock_present())
        return fail(error, error_size, "Results presentation clock lost ownership");
    return ok(error, error_size);
}

int melee_web_results_context_requested(const MeleeWebResultsContext* context)
{
    return context && context == owner ? context->transition : 0;
}

uint32_t melee_web_results_context_random_seed(const MeleeWebResultsContext* context)
{
    return context && context == owner ? context->seed : 0;
}

uint32_t melee_web_results_context_ticks(const MeleeWebResultsContext* context)
{
    return context && context == owner ? context->ticks : 0;
}

static void copy_camera_entry_snapshot(
    const MeleeWebResultsContext* context,
    MeleeWebResultsCameraEntrySnapshot* snapshot)
{
    snapshot->saved_pool_at_context_begin = context->saved_camera_pool;
    snapshot->source_pool_before_onenter = context->camera_pool_before_onenter;
    snapshot->owner_pool_before_onenter = context->owner_camera_pool_before_onenter;
    snapshot->source_pool_after_onenter = context->camera_pool_after_onenter;
    snapshot->source_pool_after_collision_adoption = context->camera_pool_after_collision_adoption;
    snapshot->context_pool_after_adoption = context->camera_pool;
    snapshot->owner_pool_after_adoption = owner_camera_pool;
    snapshot->source_camera_allocation_generation_before_onenter =
        context->camera_allocation_generation_before_onenter;
    snapshot->source_camera_allocation_generation_after_onenter =
        context->camera_allocation_generation_after_onenter;
    snapshot->source_camera_allocation_subject_count_after_onenter =
        context->camera_allocation_subject_count_after_onenter;
    snapshot->source_camera_allocation_generation_after_collision_adoption =
        context->camera_allocation_generation_after_collision_adoption;
    snapshot->source_camera_allocation_subject_count_after_collision_adoption =
        context->camera_allocation_subject_count_after_collision_adoption;
    snapshot->source_camera_pool_event_count = context->camera_pool_event_count;
    snapshot->source_camera_pool_event_overflow = context->camera_pool_event_overflow;
    memcpy(snapshot->source_camera_pool_events, context->camera_pool_events,
           sizeof(snapshot->source_camera_pool_events));
    snapshot->source_camera_subject_event_count = context->camera_subject_event_count;
    snapshot->source_camera_subject_event_overflow = context->camera_subject_event_overflow;
    memcpy(snapshot->source_camera_subject_events, context->camera_subject_events,
           context->camera_subject_event_count * sizeof(context->camera_subject_events[0]));
}

int melee_web_results_context_camera_entry_snapshot(
    const MeleeWebResultsContext* context,
    MeleeWebResultsCameraEntrySnapshot* snapshot)
{
    if (!context || context != owner || !snapshot) return 0;
    memset(snapshot, 0, sizeof(*snapshot));
    copy_camera_entry_snapshot(context, snapshot);
    return 1;
}

int melee_web_results_context_exit(MeleeWebResultsContext* context,
                                   char* error, size_t error_size)
{
    if (!live(context, error, error_size) || context->drawing)
        return fail(error, error_size, "Results exit requires a live idle scene");
    if (!camera_pool_owned(context, "scene exit entry", NULL, error, error_size)) return 0;
    if (context->scene_entered) {
        gm_Scene_Results_OnExit(NULL);
        if (!camera_pool_owned(context, "scene OnExit", NULL, error, error_size)) return 0;
        if (!melee_web_fighter_assets_check_owned("Results scene OnExit", error, error_size)) {
            fprintf(stderr, "Results fighter ownership failure after source OnExit: %s\n",
                    error ? error : "unavailable");
            return 0;
        }
        context->scene_entered = 0;
    }
    return ok(error, error_size);
}

int melee_web_results_context_check_handoff(const char* phase,
                                           char* error, size_t error_size)
{
    if (!phase || !owner || owner->scene_entered || owner->drawing)
        return fail(error, error_size, "Results mode exit requires its live source world");
    if (!live(owner, error, error_size)) return 0;
    if (!camera_pool_owned(owner, phase, NULL, error, error_size)) return 0;
    return ok(error, error_size);
}

int melee_web_results_context_end(MeleeWebResultsContext* context,
                                  MeleeWebResultsCameraEntrySnapshot* snapshot,
                                  char* error, size_t error_size)
{
    if (snapshot) memset(snapshot, 0, sizeof(*snapshot));
    if (!context) return ok(error, error_size);
    if (snapshot) {
        copy_camera_entry_snapshot(context, snapshot);
        context->camera_snapshot_sink = snapshot;
    }
    if (!live(context, error, error_size)) return 0;
    if (context->drawing)
        return fail(error, error_size, "Results end requires an idle scene");
    if (!camera_pool_owned(context, "close entry", NULL, error, error_size)) return 0;
    if (!melee_web_results_context_exit(context, error, error_size)) return 0;
    if (context->flash_saved) {
        if (!melee_web_bg_flash_destroy(context->flash_overlay, context->flash_camera))
            return fail(error, error_size, "Original Results screen-flash ownership changed");
        melee_web_bg_flash_restore_state();
        context->flash_saved = 0;
        if (!camera_pool_owned(context, "flash teardown", NULL, error, error_size)) return 0;
        if (!melee_web_fighter_assets_check_owned("Results flash teardown", error, error_size)) return 0;
    }
    if (!delete_new_objects(context, error, error_size)) return 0;
    if (!melee_web_collision_destroy(context->collision, error, error_size)) return 0;
    context->collision=NULL;
    if (!camera_pool_owned(context, "collision teardown", NULL, error, error_size)) return 0;
    if (!melee_web_fighter_assets_check_owned("Results collision teardown", error, error_size)) return 0;
    if (!melee_web_fighter_assets_check_owned("Results OnExit", error, error_size)) return 0;
    if (!release_source_camera_and_ground(context, error, error_size)) return 0;
    HSD_SisLib_803A5FBC();
    /* No source consumer may retain a typed archive alias after this world's
     * heap and its descriptors are released. This is the same reset used by
     * the original next-scene preload. */
    Toy_803127D4();
    tyDisplay_8031C8B8();
    HSD_PadRumbleRemoveAll();
    for (unsigned i = 0; i < 4; ++i) HSD_PadRumbleOffN(i);
    HSD_PadRumbleInterpret();
    restore_pad_and_source_globals(context);
    lb_8001D1F4();
    lb_8001C5A4();
    if (!melee_web_stage_selection_end())
        return fail(error, error_size, "Original Results stage selection cannot be restored");
    if (!melee_web_menu_clock_end())
        return fail(error, error_size, "Original Results scene clock cannot be restored");
    if (snapshot) copy_camera_entry_snapshot(context, snapshot);
    context->camera_snapshot_sink = NULL;
    owner = NULL;
    free(context->baseline);
    free(context);
    return ok(error, error_size);
}
