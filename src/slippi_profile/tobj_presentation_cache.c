/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tobj_presentation_cache.h"
#include <sysdolphin/baselib/class.h>
#include <sysdolphin/baselib/debug.h>
#include <stdlib.h>
extern HSD_TObjInfo hsdTObj;

typedef struct ClassProof {
    HSD_ClassInfo* identity;
    HSD_ClassInfo* parent;
    void (*info_init)(void);
    HSD_Class* (*alloc)(HSD_ClassInfo*);
    int (*init)(HSD_Class*);
    void (*release)(HSD_Class*);
    void (*destroy)(HSD_Class*);
    void (*amnesia)(HSD_ClassInfo*);
    int16_t extent, info_size;
} ClassProof;

typedef struct DescriptorProof {
    HSD_ClassInfo* identity;
    HSD_ClassInfo* parent;
    void (*info_init)(void);
    HSD_Class* (*alloc)(HSD_ClassInfo*);
    int (*init)(HSD_Class*);
    void (*release)(HSD_Class*);
    void (*destroy)(HSD_Class*);
    void (*amnesia)(HSD_ClassInfo*);
    int (*load)(HSD_TObj*, HSD_TObjDesc*);
    void (*make_mtx)(HSD_TObj*);
    __typeof__(((HSD_TObjInfo*) 0)->make_texp) make_texp;
    int16_t extent, info_size;
    uint32_t valid;
} DescriptorProof;

static ClassProof object_proof, base_proof;
static DescriptorProof proof;
static uint64_t owner_world, interval_serial, active_token;
static uint32_t first_failure;
/* Read by retained post-abort diagnostics; the first failure is immutable. */
static volatile uint32_t first_failure_line, first_failure_detail;

enum {
    OWNER_MISSING = 1,
    OWNER_DESCRIPTOR = 2,
    OWNER_WRAP = 4,
    OWNER_REBIND = 6,
    OWNER_AMNESIA = 7,
    OWNER_INTERVAL = 8,
    OWNER_ROLE = 9,
    OWNER_WORLD = 10,
    OWNER_LOAD = 11,
    OWNER_CUSTOM = 12,
};

static ClassProof capture_class(HSD_ClassInfo* c) {
    ClassProof p = {c, c->head.parent, c->head.info_init, c->alloc, c->init,
                    c->release, c->destroy, c->amnesia, c->head.obj_size,
                    c->head.info_size};
    return p;
}

static int same_class(ClassProof p) {
    HSD_ClassInfo* c = p.identity;
    return c && (c->head.flags & 1) && c->head.parent == p.parent &&
           c->head.obj_size == p.extent && c->head.info_size == p.info_size &&
           c->head.info_init == p.info_init && c->alloc == p.alloc &&
           c->init == p.init && c->release == p.release &&
           c->destroy == p.destroy && c->amnesia == p.amnesia;
}

static void fail_at(uint32_t why, uint32_t line, uint32_t detail) {
    if (!first_failure) {
        first_failure = why;
        first_failure_line = line;
        first_failure_detail = detail;
    }
}

#define fail(why) fail_at((why), (uint32_t) __LINE__, 0)

static int descriptor_valid(void) {
    HSD_ClassInfo* c = proof.identity;
    return proof.valid && same_class(object_proof) && same_class(base_proof) &&
           c == (HSD_ClassInfo*) &hsdTObj && (c->head.flags & 1) &&
           c->head.parent == proof.parent && proof.parent == &hsdObj &&
           c->head.obj_size == proof.extent && proof.extent == sizeof(HSD_TObj) &&
           c->head.info_size == proof.info_size &&
           proof.info_size == sizeof(HSD_TObjInfo) &&
           c->head.info_init == proof.info_init && c->alloc == proof.alloc &&
           c->init == proof.init && c->release == proof.release &&
           c->destroy == proof.destroy && c->amnesia == proof.amnesia &&
           hsdTObj.load == proof.load && hsdTObj.make_mtx == proof.make_mtx &&
           hsdTObj.make_texp == proof.make_texp;
}

static void panic_owner(uint32_t why) {
    fail(why);
    OSReport("FIRST_FAILURE_CONTEXT={\"code\":%u,\"line\":%u,\"detail\":%u}\n",
             (unsigned) first_failure, (unsigned) first_failure_line,
             (unsigned) first_failure_detail);
    HSD_Panic(__FILE__, __LINE__, "Unsupported Slippi TObj presentation owner");
    abort(); /* A provider returning from panic cannot become silent success. */
}

int slippi_tobj_world_begin(uint64_t world) {
    if (!world || owner_world || active_token) {
        fail(OWNER_WORLD);
        return 0;
    }
    owner_world = world;
    first_failure = first_failure_line = first_failure_detail = 0;
    if (proof.valid && !descriptor_valid()) {
        fail(OWNER_DESCRIPTOR);
        return 0;
    }
    return 1;
}

void slippi_tobj_world_end(uint64_t world) {
    if (!world || world != owner_world) {
        fail(OWNER_WORLD);
        return;
    }
    if (active_token) fail(OWNER_INTERVAL);
    active_token = 0;
    owner_world = 0;
}

void slippi_tobj_descriptor_attest(void* info) {
    HSD_ClassInfo* c = (HSD_ClassInfo*) info;
    if (proof.valid) {
        if (c != (HSD_ClassInfo*) &hsdTObj || !descriptor_valid())
            fail(OWNER_DESCRIPTOR);
        return;
    }
    if (first_failure || c != (HSD_ClassInfo*) &hsdTObj ||
        !(c->head.flags & 1) || c->head.parent != &hsdObj ||
        c->head.obj_size != sizeof(HSD_TObj) ||
        c->head.info_size != sizeof(HSD_TObjInfo) || !(hsdObj.head.flags & 1) ||
        hsdObj.head.parent != &hsdClass ||
        hsdObj.head.obj_size != sizeof(HSD_Obj) ||
        hsdObj.head.info_size != sizeof(HSD_ObjInfo) ||
        !(hsdClass.head.flags & 1) || hsdClass.head.parent != NULL ||
        hsdClass.head.obj_size != sizeof(HSD_Class) ||
        hsdClass.head.info_size != sizeof(HSD_ClassInfo)) {
        fail(OWNER_DESCRIPTOR);
        return;
    }
    /* Publish only from the real canonical TObj class initializer. */
    object_proof = capture_class(&hsdObj);
    base_proof = capture_class(&hsdClass);
    proof = (DescriptorProof){
        c, c->head.parent, c->head.info_init, c->alloc, c->init,
        c->release, c->destroy, c->amnesia, hsdTObj.load, hsdTObj.make_mtx,
        hsdTObj.make_texp, c->head.obj_size, c->head.info_size, 1};
}

void slippi_tobj_selected(void* object, void* info) {
    if (!owner_world) {
        fail(OWNER_WORLD);
        return;
    }
    if (first_failure) return;
    if (!object || info != (void*) &hsdTObj) {
        fail(OWNER_CUSTOM);
        return;
    }
    if (!descriptor_valid()) fail(OWNER_DESCRIPTOR);
}

void slippi_tobj_loaded(void* object) {
    if (!owner_world) {
        fail(OWNER_WORLD);
        return;
    }
    if (first_failure) return;
    if (!object || !descriptor_valid()) {
        fail(OWNER_DESCRIPTOR);
        return;
    }
    HSD_TObj* tobj = (HSD_TObj*) object;
    if (tobj->parent.parent.class_info != (HSD_ClassInfo*) &hsdTObj) {
        fail(OWNER_CUSTOM);
        return;
    }
    switch (tobj_coord(tobj)) {
    case TEX_COORD_UV:
    case TEX_COORD_REFLECTION:
    case TEX_COORD_SHADOW:
    case TEX_COORD_TOON:
        return;
    default:
        fail_at(OWNER_ROLE, (uint32_t) __LINE__, tobj_coord(tobj));
        return;
    }
}

void slippi_tobj_rebound(void* object, void* previous, void* next) {
    (void) object;
    if (!owner_world || first_failure) return;
    if (previous == (void*) &hsdTObj || next == (void*) &hsdTObj) fail(OWNER_REBIND);
}

void slippi_tobj_amnesia(void* info) {
    if (info != (void*) &hsdTObj) return;
    proof.valid = 0;
    /* Ordinary scene teardown forgets the class library before world_end. A
     * live world alone is not an active correction interval. */
    if (active_token) fail(OWNER_AMNESIA);
}

void slippi_tobj_unsupported_write(void* object) {
    (void) object;
    if (owner_world) fail(OWNER_ROLE);
}

/* Called only by the original TObjSetupMtx leaf. It does not establish Load
 * provenance for arbitrary raw allocations or expose a general scratch API. */
int slippi_tobj_managed_require(void* object) {
    if (!owner_world) panic_owner(OWNER_WORLD);
    if (first_failure) panic_owner(OWNER_MISSING);
    if (!object || !descriptor_valid()) panic_owner(OWNER_DESCRIPTOR);
    HSD_TObj* tobj = (HSD_TObj*) object;
    if (tobj->parent.parent.class_info != (HSD_ClassInfo*) &hsdTObj ||
        HSD_TOBJ_METHOD(tobj)->make_mtx != proof.make_mtx)
        panic_owner(OWNER_CUSTOM);
    switch (tobj_coord(tobj)) {
    case TEX_COORD_UV:
    case TEX_COORD_REFLECTION:
        return 1;
    case TEX_COORD_SHADOW:
    case TEX_COORD_TOON:
        return 0; /* The original body owns these coordinate modes. */
    default:
        fail_at(OWNER_ROLE, (uint32_t) __LINE__, tobj_coord(tobj));
        panic_owner(OWNER_ROLE);
    }
}

SlippiTObjAnchor slippi_tobj_anchor_capture(void) {
    SlippiTObjAnchor a = {0};
    if (!owner_world || active_token || first_failure ||
        interval_serial == UINT64_MAX) {
        fail(OWNER_INTERVAL);
        return a;
    }
    if (!descriptor_valid()) {
        fail(OWNER_DESCRIPTOR);
        return a;
    }
    active_token = ++interval_serial;
    a.world = owner_world;
    a.token = active_token;
    return a;
}

int slippi_tobj_anchor_admit(SlippiTObjAnchor a) {
    if (first_failure) return 0;
    if (!descriptor_valid()) {
        fail(OWNER_DESCRIPTOR);
        return 0;
    }
    return a.world == owner_world && a.token != 0 && a.token == active_token &&
           a.serial == 0 && a.count == 0 && a.reserved == 0;
}

void slippi_tobj_anchor_disarm(SlippiTObjAnchor a) {
    if (a.token && a.token == active_token && a.world == owner_world)
        active_token = 0;
}

uint32_t slippi_tobj_failure(void) { return first_failure; }
