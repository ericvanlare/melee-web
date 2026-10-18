/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dynamics_fix.h"

static void refresh_subtree(HSD_JObj *jobj)
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
    refresh_subtree(jobj->next);
}

void slippi_profile_refresh_fighter_matrices(HSD_JObj *root)
{
    refresh_subtree(root);
}
