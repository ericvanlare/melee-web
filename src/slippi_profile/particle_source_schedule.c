/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "particle_source_schedule.h"

#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE

#include "particle_pass_plan.h"
#include "gameplay_cpu_interval.h"

#include <melee/ef/eflib.h>
#include <melee/gr/grdisplay.h>
#include <melee/if/ifstatus.h>
#include <melee/if/ifstock.h>
#include <melee/if/iftime.h>
#include <melee/if/ifmagnify.h>
#include <melee/if/if_2F72.h>
#include <melee/if/if_2FD9.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/object.h>
#include <sysdolphin/baselib/objalloc.h>

#include <stdio.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

extern HSD_ObjAllocData gobj_alloc_data;
extern HSD_CObjInfo hsdCObj;
/* Match lobj.c's definition; its upstream header spells this differently. */
extern HSD_LObjInfo hsdLObj;
/* eflib.c exports this callback but does not declare it in eflib.h. */
extern void efLib_render_callback(HSD_GObj*, int);

/* Implemented beside the private camera callback and its process in camera.c.
 * These are source identities, not guessed code addresses. */
extern void slippi_particle_source_main_camera_identity(
    HSD_GObj** camera, GObj_RenderFunc* render_callback,
    HSD_GObjEvent* process_callback, unsigned* x398_b3);
extern void slippi_particle_source_hud_identity(
    HSD_GObj** camera, GObj_RenderFunc* render_callback,
    unsigned* hidden);
extern void slippi_particle_source_nametag_identity(
    HSD_GObj** camera, GObj_RenderFunc* render_callback);
extern GObj_RenderFunc slippi_particle_source_magnify_callback(void);
extern HSD_GObj* melee_web_bg_flash_camera_owner(void);
extern void fn_802F9680(HSD_GObj*, int);
extern void fn_802F94E0(HSD_GObj*, int);
extern void fn_802F95E8(HSD_GObj*, int);
extern void fn_802F9548(HSD_GObj*, int);
extern void fn_802F9598(HSD_GObj*, int);
extern void ifStatus_802F5DE0(HSD_GObj*, int);
extern void ifStatus_802F5E50(HSD_GObj*, int);
extern void fn_802FDA4C(HSD_GObj*, int);
extern void fn_802F770C(HSD_GObj*, int);

/* Only the callback identities registered by the direct match HUD setup are
 * admitted on GX10/11. Every JObj callback is also checked against the base
 * display classes and bounded topology by the planner's shared source proof. */
static int hud_display_callback_is_known(unsigned link, GObj_RenderFunc callback)
{
    if (link == 10) return callback == HSD_GObj_LObjCallback;
    if (link != 11) return 0;
    return callback == HSD_GObj_JObjCallback ||
           callback == ifMagnify_802FB8C0 ||
           callback == fn_802F9680 || callback == fn_802F94E0 ||
           callback == fn_802F95E8 || callback == fn_802F9548 ||
           callback == fn_802F9598 || callback == ifStatus_802F5DE0 ||
           callback == ifStatus_802F5E50 || callback == fn_802FDA4C ||
           callback == fn_802F770C;
}

typedef struct NodeProof {
    const HSD_GObj* object;
    const HSD_GObj* next;
    const HSD_GObj* previous;
    const HSD_GObjProc* process;
    const HSD_GObj* process_gobj;
    const HSD_GObjProc* process_child;
    HSD_GObjEvent process_callback;
    GObj_RenderFunc render_callback;
    void* hsd_object;
    void* user_data;
    void (*user_data_remove)(void*);
    uint64_t gxlink_priorities;
    uint16_t classifier;
    uint8_t p_link, gx_link, p_priority, render_priority;
    uint8_t object_kind, user_data_kind, process_link;
} NodeProof;

typedef struct CameraEvent {
    NodeProof proof;
    size_t visit_first;
    size_t visit_count;
    SlippiParticleCameraKind kind;
} CameraEvent;

typedef struct Schedule {
    uint64_t world, interval;
    unsigned main_x398_b3, hud_hidden, selected_links;
    NodeProof* camera_nodes;
    size_t camera_node_count;
    CameraEvent* events;
    size_t event_count;
    SlippiParticlePassVisit* visits;
    size_t visit_count;
    NodeProof* selected_nodes;
    size_t selected_node_count;
    size_t link_first[5], link_count[5];
} Schedule;

static struct {
    uint64_t world, interval;
    Schedule schedule;
    size_t event_cursor;
    const HSD_GObj* open_camera;
    unsigned active, ordinary_drawing, drawing, camera_open;
    unsigned helper_camera_open, failed;
} state;

static void schedule_clear(Schedule* schedule)
{
    free(schedule->camera_nodes);
    free(schedule->events);
    free(schedule->visits);
    free(schedule->selected_nodes);
    memset(schedule, 0, sizeof(*schedule));
}

static int refuse(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int bytes_equal(const void* left, const void* right, size_t size)
{
    return !size || memcmp(left, right, size) == 0;
}

static int source_failure(const char* message, char* error, size_t error_size)
{
    uint64_t world = state.world;
    uint64_t interval = state.interval;
    /* error belongs to the caller. Unwind only writes a separate ignored
     * buffer, so a leaf message==error remains valid throughout cleanup. */
    const int preserve_buffer = message == error;
    state.failed = 1;
    if (state.drawing || state.ordinary_drawing) {
        char ignored[1];
        (void) slippi_particle_presentation_draw_end(ignored, sizeof(ignored));
    }
    if (interval && world) {
        (void) slippi_particle_presentation_interval_end(world, interval);
    }
    schedule_clear(&state.schedule);
    state.interval = 0;
    state.ordinary_drawing = 0;
    state.drawing = state.camera_open = state.helper_camera_open = 0;
    state.open_camera = NULL;
    state.event_cursor = 0;
    if (preserve_buffer) return 0;
    return refuse(error, error_size, message);
}

/* Use only after a leaf has refused. Preserve its actual first diagnostic;
 * the generic wrapper label is a fallback for leaves without a message. */
static int source_leaf_failure(const char* fallback, char* error,
                               size_t error_size)
{
    return source_failure(error && error_size && error[0] ? error : fallback,
                          error, error_size);
}

static NodeProof node_proof(const HSD_GObj* object)
{
    NodeProof proof;
    memset(&proof, 0, sizeof(proof));
    proof.object = object;
    proof.next = object->next_gx;
    proof.previous = object->prev_gx;
    proof.process = object->proc;
    proof.render_callback = object->render_cb;
    proof.hsd_object = object->hsd_obj;
    proof.user_data = object->user_data;
    proof.user_data_remove = object->user_data_remove_func;
    proof.gxlink_priorities = object->gxlink_prios;
    proof.classifier = object->classifier;
    proof.p_link = object->p_link;
    proof.gx_link = object->gx_link;
    proof.p_priority = object->p_priority;
    proof.render_priority = object->render_priority;
    proof.object_kind = object->obj_kind;
    proof.user_data_kind = object->user_data_kind;
    if (object->proc) {
        proof.process_gobj = object->proc->gobj;
        proof.process_child = object->proc->child;
        proof.process_callback = object->proc->on_invoke;
        proof.process_link = object->proc->s_link;
    }
    return proof;
}

/* Published only by un_802FEFAC; invalidated by the actual GObj destroy
 * branch before the CObj release. This is not CPU-restored source state. */
static struct {
    const HSD_GObj* camera;
    const void* cobj;
} ifcoget_receipt;

static int cobj_is_canonical(const HSD_GObj* camera)
{
    const HSD_CObj* cobj;
    if (!camera || !camera->hsd_obj ||
        camera->obj_kind != HSD_GObj_CameraKind)
        return 0;
    cobj = (const HSD_CObj*) camera->hsd_obj;
    return HSD_OBJECT_METHOD(cobj) == HSD_CLASS_INFO(&hsdCObj);
}

int slippi_particle_source_ifcoget_constructing(void)
{
    if (melee_web_cpu_interval_active() || ifcoget_receipt.camera) {
        state.failed = 1;
        return 0;
    }
    return 1;
}

int slippi_particle_source_ifcoget_created(HSD_GObj* camera)
{
    if (melee_web_cpu_interval_active() || ifcoget_receipt.camera ||
        !cobj_is_canonical(camera) ||
        camera->classifier != HSD_GOBJ_CLASS_CAMERA ||
        camera->p_link != 21 || camera->p_priority != 0 ||
        camera->gx_link != SLIPPI_PARTICLE_GX_LINK_MAX + 1 ||
        camera->render_priority != 9 ||
        camera->render_cb != HSD_GObj_803910D8 ||
        camera->gxlink_prios != 0x8400) {
        state.failed = 1;
        return 0;
    }
    ifcoget_receipt.camera = camera;
    ifcoget_receipt.cobj = camera->hsd_obj;
    return 1;
}

int slippi_particle_source_gobj_retiring(const HSD_GObj* object)
{
    if (object != ifcoget_receipt.camera) return 1;
    if (melee_web_cpu_interval_active()) {
        state.failed = 1;
        return 0;
    }
    memset(&ifcoget_receipt, 0, sizeof(ifcoget_receipt));
    return 1;
}

static void source_base(SlippiParticlePassSource* source,
                        HSD_GObj* main_camera,
                        GObj_RenderFunc main_callback,
                        HSD_GObjEvent main_process_callback,
                        unsigned x398_b3, unsigned live_bound)
{
    HSD_GObj* hud_camera;
    HSD_GObj* nametag_camera;
    memset(source, 0, sizeof(*source));
    source->main_camera = main_camera;
    source->main_cobj = main_camera->hsd_obj;
    source->main_callback = main_callback;
    source->main_process_callback = main_process_callback;
    source->main_x398_b3 = x398_b3;
    slippi_particle_source_hud_identity(&hud_camera,
                                        &source->hud_callback,
                                        &source->hud_hidden);
    slippi_particle_source_nametag_identity(&nametag_camera,
                                            &source->nametag_callback);
    source->hud_camera = hud_camera;
    source->nametag_camera = nametag_camera;
    source->ifcoget_camera = ifcoget_receipt.camera;
    source->ifcoget_cobj = ifcoget_receipt.cobj;
    source->ifcoget_callback = HSD_GObj_803910D8;
    source->background_flash_camera = melee_web_bg_flash_camera_owner();
    source->background_flash_callback = HSD_GObj_803910D8;
    source->magnify_callback = slippi_particle_source_magnify_callback();
    source->particle_callback = efLib_render_callback;
    source->model_manager_process = efLib_particles_proc_main;
    source->aux_manager_process = efLib_particles_proc_aux;
    source->gx_heads = HSD_GObjGXLinkHead;
    source->gx_tails = HSD_GObj_804D7820;
    source->gx_slot_count = SLIPPI_PARTICLE_GX_LINK_SLOTS;
    source->gx_link_max = HSD_GObjLibInitData.gx_link_max;
    source->live_gobj_bound = live_bound;
    /* This value records the verified source caller's SCREEN intent. The
     * planner does not observe or claim a current GPU render pass. */
    source->screen_render_pass = 1;
}

#ifndef MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET
#define MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET (64u * 1024u)
#endif
#if MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET < 2048u
#error "Particle source diagnostic budget must reserve detail and final records"
#endif
#define SOURCE_DIAG_FINAL_BUDGET 1024u
#define SOURCE_DIAG_STATUS_BUDGET \
    (MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET > 4096u \
         ? MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET / 4u - 1024u \
         : MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET / 4u)
#define SOURCE_DIAG_DETAIL_BUDGET \
    (MELEE_WEB_PARTICLE_SOURCE_DIAG_BUDGET - SOURCE_DIAG_STATUS_BUDGET - \
     SOURCE_DIAG_FINAL_BUDGET)
#define SOURCE_DIAG_LINE_CAP 512u

typedef struct SourceDiagnostic {
    size_t detail_bytes, status_bytes, final_bytes;
    size_t camera_rows, gx_rows;
    size_t camera_visited, gx_visited, gx_lists, gx_lists_walked;
    uint64_t union_mask;
    unsigned overflow, invalid_chain, unavailable, invalid_mask, output_error;
} SourceDiagnostic;

static int source_diag_line(SourceDiagnostic* diagnostic, unsigned bucket,
                            const char* format, ...)
{
    char line[SOURCE_DIAG_LINE_CAP];
    size_t* used;
    size_t limit;
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (length < 0 || (size_t) length >= sizeof(line)) {
        diagnostic->overflow = 1;
        return 0;
    }
    if (bucket == 0) {
        used = &diagnostic->detail_bytes;
        limit = SOURCE_DIAG_DETAIL_BUDGET;
    } else if (bucket == 1) {
        used = &diagnostic->status_bytes;
        limit = SOURCE_DIAG_STATUS_BUDGET;
    } else {
        used = &diagnostic->final_bytes;
        limit = SOURCE_DIAG_FINAL_BUDGET;
    }
    if (*used > limit || (size_t) length + 1 > limit - *used) {
        diagnostic->overflow = 1;
        return 0;
    }
    if (fwrite(line, 1, (size_t) length, stdout) != (size_t) length ||
        fputc('\n', stdout) == EOF) {
        diagnostic->output_error = 1;
        return 0;
    }
    *used += (size_t) length + 1;
    return 1;
}

/* Read-only first-refusal evidence. Topology walks remain bounded by the
 * live allocator count; output has its own fixed cap and cannot claim complete
 * when rows are omitted. GX rows describe authored-mask topology, not draws. */
static void source_camera_diagnostic(uint64_t world, uint64_t interval,
                                     const SlippiParticlePassSource* source,
                                     const HSD_GObj* failed_camera)
{
    SourceDiagnostic diagnostic = { 0 };
    const size_t live_bound = gobj_alloc_data.used;
    const unsigned gx_max = HSD_GObjLibInitData.gx_link_max;
    const unsigned camera_link = gx_max + 1;
    const HSD_GObj* camera_head;
    const HSD_GObj* camera_tail;
    const HSD_GObj* object;
    const HSD_GObj* previous = NULL;
    size_t camera_rows = 0, camera_visited = 0;
    unsigned camera_invalid = 0, camera_tail_reached = 0;
    unsigned link;

    (void) source_diag_line(
        &diagnostic, 0,
        "MELEE_PARTICLE_SOURCE_DIAG_V1 event=begin boundary=bind-camera-refusal "
        "world=%" PRIu64 " interval=%" PRIu64 " bound=%zu gxmax=%u "
        "failed_owner=%p failed_entry=0x%" PRIxPTR " failed_link=%u "
        "failed_priority=%u failed_kind=%u failed_mask=0x%016" PRIx64,
        world, interval, live_bound, gx_max, (const void*) failed_camera,
        (uintptr_t) failed_camera->render_cb, (unsigned) failed_camera->gx_link,
        (unsigned) failed_camera->render_priority,
        (unsigned) failed_camera->obj_kind, failed_camera->gxlink_prios);
    if (!live_bound || !source->gx_heads || !source->gx_tails || gx_max > 63 ||
        source->gx_link_max != gx_max ||
        (size_t) gx_max + 2 > source->gx_slot_count ||
        (size_t) gx_max + 2 > SLIPPI_PARTICLE_GX_LINK_SLOTS) {
        diagnostic.unavailable = 1;
        goto finish;
    }

    camera_head = source->gx_heads[camera_link];
    camera_tail = source->gx_tails[camera_link];
    if ((camera_head == NULL) != (camera_tail == NULL)) camera_invalid = 1;
    object = camera_head;
    while (object) {
        NodeProof proof;
        unsigned canonical;
        if (camera_visited >= live_bound) {
            camera_invalid = 1;
            break;
        }
        proof = node_proof(object);
        if (proof.gx_link != camera_link || proof.previous != previous)
            camera_invalid = 1;
        diagnostic.union_mask |= proof.gxlink_priorities;
        canonical = cobj_is_canonical(object) != 0;
        if (source_diag_line(
                &diagnostic, 0,
                "MELEE_PARTICLE_SOURCE_DIAG_V1 event=camera idx=%zu owner=%p "
                "next=%p prev=%p cobj=%p camera_kind=%u canonical_cobj=%u "
                "classifier=0x%04x p_link=%u p_priority=%u gx_link=%u "
                "render_priority=%u obj_kind=%u mask=0x%016" PRIx64 " "
                "render_entry=0x%" PRIxPTR " process_gobj=%p "
                "process_entry=0x%" PRIxPTR,
                camera_visited, (const void*) proof.object,
                (const void*) proof.next, (const void*) proof.previous,
                proof.hsd_object, (unsigned) (object->obj_kind == HSD_GObj_CameraKind),
                canonical, (unsigned) proof.classifier, (unsigned) proof.p_link,
                (unsigned) proof.p_priority, (unsigned) proof.gx_link,
                (unsigned) proof.render_priority, (unsigned) proof.object_kind,
                proof.gxlink_priorities, (uintptr_t) proof.render_callback,
                (const void*) proof.process_gobj,
                (uintptr_t) proof.process_callback))
            ++camera_rows;
        ++camera_visited;
        previous = object;
        if (camera_invalid) break;
        object = (const HSD_GObj*) proof.next;
    }
    camera_tail_reached = previous == camera_tail;
    if (!camera_tail_reached || source->gx_heads[camera_link] != camera_head ||
        source->gx_tails[camera_link] != camera_tail)
        camera_invalid = 1;
    if (camera_invalid) diagnostic.invalid_chain = 1;
    diagnostic.camera_visited = camera_visited;
    diagnostic.camera_rows = camera_rows;
    (void) source_diag_line(
        &diagnostic, 1,
        "MELEE_PARTICLE_SOURCE_DIAG_V1 event=camera-end visited=%zu rows=%zu "
        "bound=%zu tail_reached=%u topology_complete=%u records_complete=%u",
        camera_visited, camera_rows, live_bound, camera_tail_reached,
        !camera_invalid, camera_rows == camera_visited && !diagnostic.overflow);

    {
        uint64_t allowed_mask = gx_max == 63
            ? UINT64_MAX : ((UINT64_C(1) << (gx_max + 1)) - 1);
        if (diagnostic.union_mask & ~allowed_mask)
            diagnostic.invalid_mask = 1;
    }
    for (link = 0; link <= gx_max; ++link) {
        const HSD_GObj* head;
        const HSD_GObj* tail;
        const HSD_GObj* current;
        const HSD_GObj* link_previous = NULL;
        size_t visited = 0, rows = 0;
        unsigned invalid = 0, tail_reached;
        if (!(diagnostic.union_mask & (UINT64_C(1) << link))) continue;
        ++diagnostic.gx_lists;
        head = source->gx_heads[link];
        tail = source->gx_tails[link];
        if ((head == NULL) != (tail == NULL)) invalid = 1;
        current = head;
        while (current) {
            NodeProof proof;
            if (visited >= live_bound) {
                invalid = 1;
                break;
            }
            proof = node_proof(current);
            if (proof.gx_link != link || proof.previous != link_previous)
                invalid = 1;
            if (source_diag_line(
                    &diagnostic, 0,
                    "MELEE_PARTICLE_SOURCE_DIAG_V1 event=gx-node link=%u idx=%zu "
                    "owner=%p next=%p prev=%p classifier=0x%04x obj_kind=%u "
                    "p_link=%u p_priority=%u render_priority=%u attached=%p "
                    "render_entry=0x%" PRIxPTR " process_gobj=%p "
                    "process_child=%p process_link=%u process_entry=0x%" PRIxPTR,
                    link, visited, (const void*) proof.object,
                    (const void*) proof.next, (const void*) proof.previous,
                    (unsigned) proof.classifier, (unsigned) proof.object_kind,
                    (unsigned) proof.p_link, (unsigned) proof.p_priority,
                    (unsigned) proof.render_priority, proof.hsd_object,
                    (uintptr_t) proof.render_callback,
                    (const void*) proof.process_gobj,
                    (const void*) proof.process_child, (unsigned) proof.process_link,
                    (uintptr_t) proof.process_callback))
                ++rows;
            ++visited;
            link_previous = current;
            if (invalid) break;
            current = (const HSD_GObj*) proof.next;
        }
        tail_reached = link_previous == tail;
        if (!tail_reached || source->gx_heads[link] != head ||
            source->gx_tails[link] != tail)
            invalid = 1;
        if (invalid) diagnostic.invalid_chain = 1;
        ++diagnostic.gx_lists_walked;
        diagnostic.gx_visited += visited;
        diagnostic.gx_rows += rows;
        (void) source_diag_line(
            &diagnostic, 1,
            "MELEE_PARTICLE_SOURCE_DIAG_V1 event=gx-link link=%u head=%p tail=%p "
            "visited=%zu rows=%zu bound=%zu tail_reached=%u topology_complete=%u "
            "records_complete=%u invalid_chain=%u",
            link, (const void*) head, (const void*) tail, visited, rows,
            live_bound, tail_reached, !invalid,
            rows == visited && !diagnostic.overflow, invalid);
    }

finish:
    {
        unsigned complete = !diagnostic.overflow && !diagnostic.invalid_chain &&
            !diagnostic.unavailable && !diagnostic.invalid_mask &&
            !diagnostic.output_error &&
            diagnostic.camera_rows == diagnostic.camera_visited &&
            diagnostic.gx_rows == diagnostic.gx_visited &&
            diagnostic.gx_lists == diagnostic.gx_lists_walked;
        (void) source_diag_line(
            &diagnostic, 2,
            "MELEE_PARTICLE_SOURCE_DIAG_V1 event=end complete=%u overflow=%u "
            "invalid_chain=%u unavailable=%u invalid_mask=%u output_error=%u "
            "bound=%zu camera_visited=%zu camera_rows=%zu gx_union=0x%016" PRIx64 " "
            "gx_lists=%zu gx_lists_walked=%zu gx_visited=%zu gx_rows=%zu",
            complete, diagnostic.overflow, diagnostic.invalid_chain,
            diagnostic.unavailable, diagnostic.invalid_mask,
            diagnostic.output_error, live_bound, diagnostic.camera_visited,
            diagnostic.camera_rows, diagnostic.union_mask, diagnostic.gx_lists,
            diagnostic.gx_lists_walked, diagnostic.gx_visited, diagnostic.gx_rows);
        (void) fflush(stdout);
    }
}

static int bind_camera_role(HSD_GObj* camera,
                            SlippiParticlePassSource* source,
                            unsigned* main_seen,
                            SlippiParticleCameraKind* kind, char* error,
                            size_t error_size)
{
    if (!camera->render_cb) return 1;
    if (!cobj_is_canonical(camera))
        return refuse(error, error_size,
                      "Source camera is not a live CameraKind with base CObj class");
    if (camera == source->main_camera) {
        if (camera->render_cb != source->main_callback)
            return refuse(error, error_size,
                          "Main camera callback differs from private source identity");
        ++*main_seen;
        *kind = SLIPPI_PARTICLE_CAMERA_MAIN_TAIL;
        return 1;
    }
    if (camera == source->hud_camera) {
        if (camera->render_cb != source->hud_callback ||
            camera->gxlink_prios != 0xD00)
            return refuse(error, error_size,
                          "HUD camera identity, callback or exact GX schedule changed");
        source->hud_cobj = camera->hsd_obj;
        *kind = SLIPPI_PARTICLE_CAMERA_HUD;
        return 1;
    }
    if (camera == source->ifcoget_camera) {
        if (camera->hsd_obj != source->ifcoget_cobj ||
            camera->classifier != HSD_GOBJ_CLASS_CAMERA ||
            camera->p_link != 21 || camera->p_priority != 0 ||
            camera->gx_link != SLIPPI_PARTICLE_GX_LINK_MAX + 1 ||
            camera->render_priority != 9 ||
            camera->render_cb != HSD_GObj_803910D8 ||
            camera->gxlink_prios != 0x8400)
            return refuse(error, error_size,
                          "IfCoGet constructor owner or exact registration changed");
        *kind = SLIPPI_PARTICLE_CAMERA_IFCOGET_ZERO;
        return 1;
    }
    if (camera == source->nametag_camera) {
        if (camera->render_cb != source->nametag_callback ||
            camera->gxlink_prios != 0x200)
            return refuse(error, error_size,
                          "Nametag camera identity, callback or exact GX9 schedule changed");
        source->nametag_cobj = camera->hsd_obj;
        *kind = SLIPPI_PARTICLE_CAMERA_NAMETAG_ZERO;
        return 1;
    }
    if (camera == source->background_flash_camera) {
        if (camera->render_cb != source->background_flash_callback ||
            camera->gxlink_prios != 0x10000)
            return refuse(error, error_size,
                          "Background flash owner, callback or exact GX16 schedule changed");
        source->background_flash_cobj = camera->hsd_obj;
        *kind = SLIPPI_PARTICLE_CAMERA_BACKGROUND_FLASH;
        return 1;
    }
    if (camera->render_cb == grDisplay_801C5F60) {
        if (source->ground_camera && source->ground_camera != camera)
            return refuse(error, error_size,
                          "Multiple ground camera callbacks exceed the admitted route");
        source->ground_camera = camera;
        source->ground_cobj = camera->hsd_obj;
        source->ground_callback = grDisplay_801C5F60;
        if (camera->gxlink_prios != 0x8)
            return refuse(error, error_size,
                          "Ground camera mask differs from source GX3 schedule");
        *kind = SLIPPI_PARTICLE_CAMERA_GROUND_ZERO;
        return 1;
    }
    if (source->magnify_callback &&
        camera->render_cb == source->magnify_callback) {
        if (source->magnify_camera && source->magnify_camera != camera)
            return refuse(error, error_size,
                          "Multiple magnify camera callbacks exceed the admitted route");
        source->magnify_camera = camera;
        source->magnify_cobj = camera->hsd_obj;
        if (camera->gxlink_prios != 0x10)
            return refuse(error, error_size,
                          "Magnify camera mask differs from source GX4 schedule");
        *kind = SLIPPI_PARTICLE_CAMERA_MAGNIFY_ZERO;
        return 1;
    }
    {
        char diagnostic[192];
        (void) snprintf(
            diagnostic, sizeof(diagnostic),
            "Source camera callback has no verified particle schedule "
            "(owner=%p callback=0x%" PRIxPTR " gx_link=%u "
            "render_priority=%u obj_kind=%u gxlink_prios=0x%" PRIx64 ")",
            (void*) camera, (uintptr_t) camera->render_cb,
            (unsigned) camera->gx_link, (unsigned) camera->render_priority,
            (unsigned) camera->obj_kind, camera->gxlink_prios);
        diagnostic[sizeof(diagnostic) - 1] = '\0';
        return refuse(error, error_size, diagnostic);
    }
}

static int source_roles_unchanged(const SlippiParticlePassSource* source)
{
    HSD_GObj* hud_camera = NULL;
    HSD_GObj* nametag_camera = NULL;
    GObj_RenderFunc hud_callback = NULL;
    GObj_RenderFunc nametag_callback = NULL;
    unsigned hud_hidden = 0;
    slippi_particle_source_hud_identity(&hud_camera, &hud_callback,
                                       &hud_hidden);
    slippi_particle_source_nametag_identity(&nametag_camera,
                                           &nametag_callback);
    return ifcoget_receipt.camera == source->ifcoget_camera &&
           ifcoget_receipt.cobj == source->ifcoget_cobj &&
           hud_camera == source->hud_camera &&
           hud_callback == source->hud_callback &&
           hud_hidden == source->hud_hidden &&
           nametag_camera == source->nametag_camera &&
           nametag_callback == source->nametag_callback &&
           melee_web_bg_flash_camera_owner() ==
               source->background_flash_camera &&
           slippi_particle_source_magnify_callback() ==
               source->magnify_callback;
}

static int append_visits(Schedule* schedule, size_t* capacity,
                         const SlippiParticlePassVisit* visits, size_t count,
                         size_t maximum)
{
    size_t needed, next_capacity;
    SlippiParticlePassVisit* resized;
    if (count > SIZE_MAX - schedule->visit_count) return 0;
    needed = schedule->visit_count + count;
    if (needed > maximum) return 0;
    if (needed > *capacity) {
        next_capacity = *capacity ? *capacity : 16;
        while (next_capacity < needed) {
            if (next_capacity > maximum / 2) {
                next_capacity = maximum;
                break;
            }
            next_capacity *= 2;
        }
        if (next_capacity > SIZE_MAX / sizeof(*resized)) return 0;
        resized = realloc(schedule->visits, next_capacity * sizeof(*resized));
        if (!resized) return 0;
        schedule->visits = resized;
        *capacity = next_capacity;
    }
    if (count) memcpy(schedule->visits + schedule->visit_count, visits,
                      count * sizeof(*visits));
    schedule->visit_count = needed;
    return 1;
}

static int snapshot_selected_links(Schedule* schedule,
                                   const SlippiParticlePassSource* source,
                                   char* error, size_t error_size)
{
    unsigned links[] = { SLIPPI_PARTICLE_GX_MODEL, SLIPPI_PARTICLE_GX_AUX,
                         10, 11, 15 };
    unsigned index;
    size_t total = 0;
    for (index = 0; index < 5; ++index) {
        unsigned link = links[index];
        const HSD_GObj* object;
        const HSD_GObj* previous = NULL;
        size_t count = 0;
        schedule->link_first[index] = total;
        if (!(schedule->selected_links & (1u << link))) continue;
        for (object = source->gx_heads[link]; object; object = object->next_gx) {
            if (count++ >= source->live_gobj_bound || object->gx_link != link ||
                object->prev_gx != previous)
                return refuse(error, error_size,
                              "Selected GX link changed during schedule preflight");
            if (object->render_cb && (link == 10 || link == 11 || link == 15)) {
                if (!(link == 15 ? object->render_cb == HSD_GObj_JObjCallback
                                : hud_display_callback_is_known(link, object->render_cb)))
                    return refuse(error, error_size,
                                  "HUD GX10/GX11 link contains an unverified callback");
                if (link == 10) {
                    const HSD_LObj* light = object->hsd_obj;
                    if (object->obj_kind != HSD_GObj_LightKind ||
                        !light || HSD_OBJECT_METHOD(light) !=
                            HSD_CLASS_INFO(&hsdLObj))
                        return refuse(error, error_size,
                                      "HUD GX10 callback does not own a base LObj");
                } else if (!slippi_profile_particle_pass_jobj_display_safe(
                               object, error, error_size)) {
                    return 0;
                }
            }
            ++total;
            previous = object;
        }
        if (source->gx_tails[link] != previous)
            return refuse(error, error_size,
                          "Selected GX link tail changed during schedule preflight");
        schedule->link_count[index] = count;
    }
    if (total > SIZE_MAX / sizeof(*schedule->selected_nodes))
        return refuse(error, error_size,
                      "Selected GX link snapshot size overflowed");
    schedule->selected_nodes = total
        ? calloc(total, sizeof(*schedule->selected_nodes)) : NULL;
    if (total && !schedule->selected_nodes)
        return refuse(error, error_size,
                      "Selected GX link snapshot allocation failed");
    schedule->selected_node_count = total;
    for (index = 0; index < 5; ++index) {
        unsigned link = links[index];
        const HSD_GObj* object;
        size_t offset = schedule->link_first[index];
        if (!(schedule->selected_links & (1u << link))) continue;
        for (object = source->gx_heads[link]; object; object = object->next_gx)
            schedule->selected_nodes[offset++] = node_proof(object);
    }
    return 1;
}

static int build_schedule(Schedule* output, uint64_t world,
                          uint64_t interval, char* error,
                          size_t error_size)
{
    SlippiParticlePassSource base;
    HSD_GObj* main_camera = NULL;
    GObj_RenderFunc main_callback = NULL;
    HSD_GObjEvent main_process = NULL;
    unsigned main_x398_b3 = 0;
    unsigned main_seen = 0;
    size_t live_bound = gobj_alloc_data.used;
    size_t per_camera_capacity, max_visits, camera_count = 0;
    size_t visit_capacity = 0;
    SlippiParticlePassVisit* one_visits = NULL;
    HSD_GObj* node;
    HSD_GObj* previous = NULL;

    memset(output, 0, sizeof(*output));
    if (!world || !interval || !live_bound ||
        HSD_GObjLibInitData.gx_link_max != SLIPPI_PARTICLE_GX_LINK_MAX ||
        !HSD_GObjGXLinkHead || !HSD_GObj_804D7820)
        return refuse(error, error_size,
                      "Source GObj allocator or retail GX link layout is unavailable");
    if (live_bound > SIZE_MAX / 3 ||
        live_bound * 3 > SIZE_MAX / sizeof(*one_visits))
        return refuse(error, error_size,
                      "Live GObj bound overflows per-camera schedule storage");
    per_camera_capacity = live_bound * 3;
    if (live_bound > SIZE_MAX / live_bound ||
        live_bound * live_bound > SIZE_MAX / 3)
        return refuse(error, error_size,
                      "Live GObj bound overflows complete camera schedule storage");
    max_visits = live_bound * live_bound * 3;
    if (max_visits > SIZE_MAX / sizeof(*output->visits) ||
        live_bound > SIZE_MAX / sizeof(*output->camera_nodes) ||
        live_bound > SIZE_MAX / sizeof(*output->events))
        return refuse(error, error_size,
                      "Live GObj bound overflows source schedule allocation");

    slippi_particle_source_main_camera_identity(
        &main_camera, &main_callback, &main_process, &main_x398_b3);
    if (!main_camera || !main_callback || !main_process ||
        !cobj_is_canonical(main_camera))
        return refuse(error, error_size,
                      "Private main camera source identity is unavailable");
    source_base(&base, main_camera, main_callback, main_process,
                main_x398_b3, (unsigned) live_bound);
    if (!base.gx_heads[SLIPPI_PARTICLE_GX_LINK_MAX + 1] &&
        base.gx_tails[SLIPPI_PARTICLE_GX_LINK_MAX + 1])
        return refuse(error, error_size,
                      "Camera callback chain head and tail disagree");

    output->camera_nodes = calloc(live_bound, sizeof(*output->camera_nodes));
    output->events = calloc(live_bound, sizeof(*output->events));
    one_visits = calloc(per_camera_capacity, sizeof(*one_visits));
    if (!output->camera_nodes || !output->events || !one_visits) {
        free(one_visits);
        return refuse(error, error_size,
                      "Source camera schedule allocation failed");
    }
    output->world = world;
    output->interval = interval;
    output->main_x398_b3 = main_x398_b3;
    output->hud_hidden = base.hud_hidden;

    for (node = base.gx_heads[SLIPPI_PARTICLE_GX_LINK_MAX + 1]; node;
         node = node->next_gx) {
        SlippiParticlePassSource camera_source = base;
        CameraEvent event;
        size_t count = 0;
        unsigned mask = 0;
        SlippiParticleCameraKind kind = SLIPPI_PARTICLE_CAMERA_MAIN_TAIL;
        if (camera_count >= live_bound ||
            node->gx_link != SLIPPI_PARTICLE_GX_LINK_MAX + 1 ||
            node->prev_gx != previous) {
            free(one_visits);
            return refuse(error, error_size,
                          "Original camera callback chain exceeds allocator topology");
        }
        output->camera_nodes[camera_count++] = node_proof(node);
        previous = node;
        if (!node->render_cb) continue;
        memset(&event, 0, sizeof(event));
        event.proof = node_proof(node);
        if (!bind_camera_role(node, &camera_source, &main_seen, &kind,
                              error, error_size)) {
            source_camera_diagnostic(world, interval, &base, node);
            free(one_visits);
            return 0;
        }
        if (!slippi_profile_particle_pass_plan_with_mask(
                node, &camera_source, one_visits, per_camera_capacity,
                &count, &mask, error, error_size)) {
            free(one_visits);
            return 0;
        }
        if (count > max_visits - output->visit_count ||
            !append_visits(output, &visit_capacity, one_visits, count,
                           max_visits)) {
            free(one_visits);
            return refuse(error, error_size,
                          "Complete source particle schedule exceeds its allocator bound");
        }
        event.visit_first = output->visit_count - count;
        event.visit_count = count;
        event.kind = kind;
        output->events[output->event_count++] = event;
        output->selected_links |= mask;
        if (kind == SLIPPI_PARTICLE_CAMERA_HUD)
            output->selected_links |= (1u << 10) | (1u << 11);
        if (kind == SLIPPI_PARTICLE_CAMERA_IFCOGET_ZERO)
            output->selected_links |= (1u << 10) | (1u << 15);
    }
    free(one_visits);
    output->camera_node_count = camera_count;
    if (previous != base.gx_tails[SLIPPI_PARTICLE_GX_LINK_MAX + 1])
        return refuse(error, error_size,
                      "Original camera callback chain tail disagrees with topology");
    if (main_seen != 1)
        return refuse(error, error_size,
                      "Private main camera is not unique in the source callback chain");
    if (!source_roles_unchanged(&base))
        return refuse(error, error_size,
                      "Source camera role identity or HUD hidden state changed during preflight");
    if (!snapshot_selected_links(output, &base, error, error_size)) return 0;
    if (error && error_size) error[0] = '\0';
    return 1;
}

static int schedule_equal(const Schedule* left, const Schedule* right)
{
    return left->world == right->world && left->interval == right->interval &&
        left->main_x398_b3 == right->main_x398_b3 &&
        left->hud_hidden == right->hud_hidden &&
        left->selected_links == right->selected_links &&
        left->camera_node_count == right->camera_node_count &&
        left->event_count == right->event_count &&
        left->visit_count == right->visit_count &&
        left->selected_node_count == right->selected_node_count &&
        bytes_equal(left->camera_nodes, right->camera_nodes,
                    left->camera_node_count * sizeof(*left->camera_nodes)) &&
        bytes_equal(left->events, right->events,
                    left->event_count * sizeof(*left->events)) &&
        bytes_equal(left->visits, right->visits,
                    left->visit_count * sizeof(*left->visits)) &&
        bytes_equal(left->selected_nodes, right->selected_nodes,
                    left->selected_node_count * sizeof(*left->selected_nodes)) &&
        bytes_equal(left->link_first, right->link_first, sizeof(left->link_first)) &&
        bytes_equal(left->link_count, right->link_count, sizeof(left->link_count));
}

int slippi_particle_source_world_begin(uint64_t world_generation)
{
    if (!world_generation || state.world || state.drawing ||
        !slippi_particle_presentation_world_begin(world_generation))
        return 0;
    schedule_clear(&state.schedule);
    memset(&state, 0, sizeof(state));
    state.world = world_generation;
    state.active = 1;
    return 1;
}

int slippi_particle_source_world_end(uint64_t world_generation)
{
    int result;
    int wrapper_ok;
    if (!state.active || state.world != world_generation || state.drawing ||
        state.ordinary_drawing)
        return 0;
    wrapper_ok = !state.failed;
    result = slippi_particle_presentation_world_end(world_generation);
    schedule_clear(&state.schedule);
    memset(&state, 0, sizeof(state));
    return result && wrapper_ok;
}

int slippi_particle_source_prepare(uint64_t world_generation,
                                   uint64_t interval_token, char* error,
                                   size_t error_size)
{
    Schedule candidate;
    uint64_t active_token = melee_web_cpu_interval_token();
    if (!state.active || state.failed || world_generation != state.world ||
        state.drawing || state.ordinary_drawing)
        return source_failure("Particle source preparation owner refused",
                              error, error_size);
    schedule_clear(&state.schedule);
    state.interval = 0;
    if (!interval_token && !melee_web_cpu_interval_active())
        return slippi_particle_presentation_prepare(NULL, 0, world_generation,
                                                    0, error, error_size);
    if (!interval_token || interval_token != active_token ||
        !melee_web_cpu_interval_admit(interval_token))
        return source_failure("Particle source interval token is stale or failed",
                              error, error_size);
    state.interval = interval_token;
    memset(&candidate, 0, sizeof(candidate));
    if (!build_schedule(&candidate, world_generation, interval_token,
                        error, error_size)) {
        schedule_clear(&candidate);
        return source_failure(error && error_size && error[0]
                                  ? error
                                  : "Complete source schedule preflight refused",
                              error, error_size);
    }
    if (!slippi_particle_presentation_prepare(
            candidate.visits, candidate.visit_count, world_generation,
            interval_token, error, error_size)) {
        schedule_clear(&candidate);
        state.interval = interval_token;
        return source_leaf_failure("Particle presentation preparation refused",
                              error, error_size);
    }
    state.schedule = candidate;
    state.event_cursor = 0;
    state.failed = 0;
    return 1;
}

int slippi_particle_source_draw_begin(uint64_t world_generation,
                                      uint64_t interval_token, char* error,
                                      size_t error_size)
{
    Schedule current;
    if (!interval_token && !melee_web_cpu_interval_active()) {
        if (state.failed || state.drawing || state.ordinary_drawing ||
            (state.active && world_generation != state.world) ||
            (!state.active && world_generation != 0))
            return source_failure("Ordinary particle draw owner refused",
                                  error, error_size);
        schedule_clear(&state.schedule);
        state.interval = 0;
        if (!state.active && !world_generation) return 1;
        if (!slippi_particle_presentation_draw_begin(
                world_generation, 0, error, error_size))
            return source_leaf_failure("Ordinary particle draw admission refused",
                                  error, error_size);
        state.ordinary_drawing = 1;
        return 1;
    }
    if (!state.active || state.failed || state.world != world_generation ||
        state.interval != interval_token || !interval_token ||
        !melee_web_cpu_interval_admit(interval_token))
        return source_failure("Particle source draw has no admitted schedule",
                              error, error_size);
    memset(&current, 0, sizeof(current));
    if (!build_schedule(&current, world_generation, interval_token,
                        error, error_size)) {
        schedule_clear(&current);
        return source_leaf_failure("Source topology changed before original draw",
                              error, error_size);
    }
    if (!schedule_equal(&state.schedule, &current)) {
        schedule_clear(&current);
        return source_failure("Source callback or particle schedule changed before draw",
                              error, error_size);
    }
    schedule_clear(&current);
    if (!slippi_particle_presentation_draw_begin(
            world_generation, interval_token, error, error_size))
        return source_leaf_failure("Particle presentation draw admission refused",
                              error, error_size);
    state.ordinary_drawing = 0;
    state.drawing = 1;
    state.event_cursor = 0;
    return 1;
}

int slippi_particle_source_camera_begin(const HSD_GObj* camera,
                                        char* error, size_t error_size)
{
    CameraEvent* event;
    if (state.ordinary_drawing ||
        (!state.active && !melee_web_cpu_interval_active()))
        return 1;
    if (!state.drawing || state.camera_open ||
        state.event_cursor >= state.schedule.event_count)
        return source_failure("Unexpected source camera begin", error,
                              error_size);
    event = &state.schedule.events[state.event_cursor];
    if (camera != event->proof.object ||
        node_proof(camera).render_callback != event->proof.render_callback)
        return source_failure("Source camera order differs from preflight",
                              error, error_size);
    if (event->kind == SLIPPI_PARTICLE_CAMERA_HUD) {
        HSD_GObj* hud_camera = NULL;
        GObj_RenderFunc hud_callback = NULL;
        unsigned hud_hidden = 0;
        slippi_particle_source_hud_identity(&hud_camera, &hud_callback,
                                            &hud_hidden);
        if (hud_camera != camera ||
            hud_callback != event->proof.render_callback ||
            hud_hidden != state.schedule.hud_hidden)
            return source_failure("HUD camera owner or hidden state changed before its source callback",
                                  error, error_size);
    }
    state.open_camera = camera;
    state.camera_open = 1;
    if (event->visit_count) {
        if (!slippi_particle_presentation_camera_begin(camera, error,
                                                       error_size))
            return source_leaf_failure("Particle camera begin refused", error,
                                  error_size);
        state.helper_camera_open = 1;
    }
    return 1;
}

int slippi_particle_source_camera_end(const HSD_GObj* camera,
                                      char* error, size_t error_size)
{
    if (state.ordinary_drawing ||
        (!state.active && !melee_web_cpu_interval_active()))
        return 1;
    if (!state.drawing || !state.camera_open ||
        state.event_cursor >= state.schedule.event_count ||
        camera != state.open_camera)
        return source_failure("Unexpected source camera end", error,
                              error_size);
    if (state.helper_camera_open &&
        !slippi_particle_presentation_camera_end(camera, error, error_size))
        return source_leaf_failure("Particle camera did not consume its visits",
                              error, error_size);
    state.helper_camera_open = 0;
    state.camera_open = 0;
    state.open_camera = NULL;
    ++state.event_cursor;
    return 1;
}

int slippi_particle_source_draw_end(char* error, size_t error_size)
{
    if (state.ordinary_drawing) {
        state.ordinary_drawing = 0;
        return 1;
    }
    if (!state.active && !melee_web_cpu_interval_active()) return 1;
    int complete = state.drawing && !state.camera_open &&
                   state.event_cursor == state.schedule.event_count;
    int result = slippi_particle_presentation_draw_end(error, error_size);
    state.drawing = state.camera_open = state.helper_camera_open = 0;
    state.open_camera = NULL;
    state.event_cursor = 0;
    schedule_clear(&state.schedule);
    if (!complete || !result) {
        state.failed = 1;
        if (state.interval)
            (void) slippi_particle_presentation_interval_end(
                state.world, state.interval);
        state.interval = 0;
        if (error && error_size && !error[0])
            snprintf(error, error_size,
                     "Original source draw did not consume the complete camera chain");
        return 0;
    }
    return 1;
}

int slippi_particle_source_reset_after_restore(
    SlippiParticlePresentationAnchor anchor)
{
    int result = slippi_particle_presentation_reset_after_restore(anchor);
    schedule_clear(&state.schedule);
    state.interval = 0;
    state.event_cursor = 0;
    state.open_camera = NULL;
    state.ordinary_drawing = 0;
    state.drawing = state.camera_open = state.helper_camera_open = 0;
    state.failed = result ? 0 : 1;
    return result;
}

int slippi_particle_source_interval_end(uint64_t world_generation,
                                       uint64_t interval_token)
{
    int result;
    if (!state.active || state.world != world_generation) return 0;
    if (state.drawing) {
        char ignored[1];
        (void) slippi_particle_presentation_draw_end(ignored, sizeof(ignored));
    }
    result = slippi_particle_presentation_interval_end(world_generation,
                                                       interval_token);
    schedule_clear(&state.schedule);
    state.interval = 0;
    state.event_cursor = 0;
    state.open_camera = NULL;
    state.ordinary_drawing = 0;
    state.drawing = state.camera_open = state.helper_camera_open = 0;
    if (!result) state.failed = 1;
    return result;
}

#endif
