/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_PROFILE_JOBJ_DIAGNOSTIC_H
#define MELEE_WEB_SLIPPI_PROFILE_JOBJ_DIAGNOSTIC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MELEE_WEB_JOBJ_DIAG_SITE_TARGET = 1,
    MELEE_WEB_JOBJ_DIAG_SITE_UPDATE_32,
    MELEE_WEB_JOBJ_DIAG_SITE_UPDATE_33,
    MELEE_WEB_JOBJ_DIAG_SITE_UPDATE_34,
    MELEE_WEB_JOBJ_DIAG_SITE_UPDATE_35,
    MELEE_WEB_JOBJ_DIAG_SITE_MAKE_ENTRY,
    MELEE_WEB_JOBJ_DIAG_SITE_MAKE_EXIT,
    MELEE_WEB_JOBJ_DIAG_SITE_SETUP_ENTRY,
    MELEE_WEB_JOBJ_DIAG_SITE_SETUP_EXIT,
    MELEE_WEB_JOBJ_DIAG_SITE_IK1_WRITE,
    MELEE_WEB_JOBJ_DIAG_SITE_IK2_WRITE,
    MELEE_WEB_JOBJ_DIAG_SITE_REFRESH_VISIT,
    MELEE_WEB_JOBJ_DIAG_SITE_COUNT = MELEE_WEB_JOBJ_DIAG_SITE_REFRESH_VISIT
};

enum {
    MELEE_WEB_JOBJ_DIAG_STAGE_CAPTURE = 1,
    MELEE_WEB_JOBJ_DIAG_STAGE_SOURCE_STEP,
    MELEE_WEB_JOBJ_DIAG_STAGE_SOURCE_STEP_DONE,
    MELEE_WEB_JOBJ_DIAG_STAGE_PRESENT_DRAW,
    MELEE_WEB_JOBJ_DIAG_STAGE_PRESENT_DRAW_DONE,
    MELEE_WEB_JOBJ_DIAG_STAGE_CORRECTION_STEP,
    MELEE_WEB_JOBJ_DIAG_STAGE_CORRECTION_STEP_DONE,
    MELEE_WEB_JOBJ_DIAG_STAGE_COMMAND
};

typedef struct MeleeWebJObjDiagnosticContext {
    uint32_t status, command, frame, frame_valid, stage;
    uint32_t reserved;
    uint64_t sequence;
} MeleeWebJObjDiagnosticContext;

typedef struct MeleeWebJObjDiagnosticSample {
    uint32_t matrix[12], rotate[4], scale[3], translate[3], flags;
    uintptr_t class_info, parent;
} MeleeWebJObjDiagnosticSample;

typedef struct MeleeWebJObjDiagnosticTicket {
    uint32_t status, command, frame, frame_valid, stage, site;
    uint32_t source_address, row_source, row_requested, object_bytes;
    uint64_t sequence, current_host, row_host, allocation_generation;
    MeleeWebJObjDiagnosticSample before;
} MeleeWebJObjDiagnosticTicket;

#define MELEE_WEB_JOBJ_DIAG_CAPTURE_SOURCE_ADDRESS 0x80c2d680u
#define MELEE_WEB_JOBJ_DIAG_OBJECT_BYTES 0x88u

/* Implemented by the private two-tick test adapter. Return 0 outside the
 * armed interval; 1 reserves one bounded output record; 2/3 reserve the
 * explicit per-bucket/global overflow record. This never affects admission. */
int melee_web_synctest_cpu_jobj_diag_reserve(
    uint32_t site, MeleeWebJObjDiagnosticContext *out);
int melee_web_synctest_cpu_jobj_diag_enabled(void);

/* Private diagnostic entry points. They only inspect an object after its
 * pointer and full extent resolve through the current source-allocation rows. */
void melee_web_profile_jobj_diag_arm(void);
int melee_web_profile_jobj_diag_begin(
    const void *object, uint32_t object_bytes, uint32_t site,
    MeleeWebJObjDiagnosticTicket *out);
void melee_web_profile_jobj_diag_record(
    const MeleeWebJObjDiagnosticTicket *ticket, uint32_t event,
    const void *object, const uint32_t input_bits[3], uintptr_t root,
    uint32_t detail);
void melee_web_profile_jobj_diag_visit(const void *object, const void *root);

#ifdef __cplusplus
}
#endif

#endif
