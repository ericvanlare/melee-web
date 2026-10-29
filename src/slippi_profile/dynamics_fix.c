/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dynamics_fix.h"

static void refresh_subtree(HSD_JObj *jobj);

static void refresh_one(HSD_JObj *jobj)
{
    if (jobj == NULL) {
        return;
    }

    if ((jobj->flags & JOBJ_MTX_DIRTY) != 0 &&
        (jobj->flags & JOBJ_USER_DEF_MTX) == 0)
    {
        HSD_JObjSetupMatrixSub(jobj);
    }

    refresh_subtree(jobj->child);
}

static void refresh_subtree(HSD_JObj *jobj)
{
    for (; jobj != NULL; jobj = jobj->next) {
        refresh_one(jobj);
    }
}

void slippi_profile_refresh_fighter_matrices(HSD_JObj *root)
{
    refresh_subtree(root);
}

void slippi_profile_refresh_display_root_matrices(HSD_JObj *root)
{
    /* Refresh the root and child-descendant sibling chains. The root's next
     * sibling belongs to another top-level owner and is deliberately excluded. */
    refresh_one(root);
}

void slippi_profile_refresh_ground_matrices(HSD_JObj *root)
{
    slippi_profile_refresh_display_root_matrices(root);
}
