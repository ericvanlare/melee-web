/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_TEXP_STAGE_SCRATCH_H
#define SLIPPI_PROFILE_TEXP_STAGE_SCRATCH_H

#include <string.h>

#include <sysdolphin/baselib/texp.h>
#include <sysdolphin/baselib/tobj.h>

/* Resolve only map and coordinate; the immediate HSD_SetupTevStage call
 * consumes the full copied descriptor. Copy the entire representation
 * before replacing those two fields so source padding and authored bytes stay
 * untouched. */
static inline void slippi_profile_texp_stage_scratch(
    HSD_TevDesc* stage, const HSD_TExpTevDesc* node)
{
    memcpy(stage, &node->desc, sizeof(*stage));
    if (node->tobj != NULL) {
        stage->map = node->tobj->id;
        stage->coord = node->tobj->coord;
    }
}

#endif
