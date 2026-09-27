#ifndef MELEE_WEB_GAMEPLAY_PS_MATH_H
#define MELEE_WEB_GAMEPLAY_PS_MATH_H

#include <dolphin/mtx.h>

/* Scalar expansions of the original paired-single SDK routines, with their
 * rounding boundaries preserved. Wired through the gameplay SDK aliases. */
void melee_web_ps_mtx_concat(const Mtx a, const Mtx b, Mtx out);
void melee_web_ps_mtx_quat(Mtx out, const Quaternion* q);
void melee_web_ps_mtx_mult_vec(const Mtx m, const Vec* src, Vec* dst);
u32 melee_web_ps_mtx_inverse(const Mtx src, Mtx out);
void melee_web_ps_vec_normalize(const Vec* src, Vec* out);
void melee_web_ps_vec_cross(const Vec* a, const Vec* b, Vec* out);
void melee_web_mtx_look_at(Mtx out, const Vec* eye, const Vec* up,
                           const Vec* target);

#endif
