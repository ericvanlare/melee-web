/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_TEST_JOBJ_STUB_H
#define SLIPPI_PROFILE_TEST_JOBJ_STUB_H

#include <stddef.h>
#include <stdint.h>

/* Minimal test shape for the helper's traversal contract. This deliberately
 * does not model the HSD object ABI or replace an HSD/JObj runtime service. */
typedef struct HSD_JObj {
    struct HSD_JObj *next;
    struct HSD_JObj *child;
    uint32_t flags;
} HSD_JObj;

#define JOBJ_MTX_DIRTY (1u << 6)
#define JOBJ_USER_DEF_MTX (1u << 23)

void HSD_JObjSetupMatrixSub(HSD_JObj *jobj);

#endif
