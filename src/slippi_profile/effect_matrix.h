/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_EFFECT_MATRIX_H
#define SLIPPI_PROFILE_EFFECT_MATRIX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Refresh dirty matrices in validated EF model root subtrees after a live
 * match scheduler tick, without submitting a draw. */
int slippi_profile_effect_matrices_post_step(char* error,
                                              size_t error_size);

#ifdef __cplusplus
}
#endif

#endif
