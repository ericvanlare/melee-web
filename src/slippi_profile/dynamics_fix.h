/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MELEE_WEB_SLIPPI_DYNAMICS_FIX_H
#define MELEE_WEB_SLIPPI_DYNAMICS_FIX_H

#include <sysdolphin/baselib/jobj.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve dirty, non-user-matrix fighter JObjs without a render traversal. */
void slippi_profile_refresh_fighter_matrices(HSD_JObj *root);

#ifdef __cplusplus
}
#endif

#endif
