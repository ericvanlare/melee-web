/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dynamics_fix.h"

#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED && \
    defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#include "jobj_diagnostic.h"
#endif

static void refresh_subtree(HSD_JObj *jobj, HSD_JObj *root);

static void refresh_one(HSD_JObj *jobj, HSD_JObj *root)
{
    if (jobj == NULL) {
        return;
    }

#if defined(MELEE_WEB_CPU_INTERVAL_ENABLED) && MELEE_WEB_CPU_INTERVAL_ENABLED && \
    defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && \
    MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
    melee_web_profile_jobj_diag_visit(jobj, root);
#endif

    if ((jobj->flags & JOBJ_MTX_DIRTY) != 0 &&
        (jobj->flags & JOBJ_USER_DEF_MTX) == 0)
    {
        HSD_JObjSetupMatrixSub(jobj);
    }

    refresh_subtree(jobj->child, root);
}

static void refresh_subtree(HSD_JObj *jobj, HSD_JObj *root)
{
    for (; jobj != NULL; jobj = jobj->next) {
        refresh_one(jobj, root);
    }
}

void slippi_profile_refresh_fighter_matrices(HSD_JObj *root)
{
    refresh_subtree(root, root);
}

void slippi_profile_refresh_ground_matrices(HSD_JObj *root)
{
    /* HSD_JObjDispAll starts at the Ground GObj root and follows children;
     * its top-level next sibling is outside this GObj's display traversal. */
    refresh_one(root, root);
}
