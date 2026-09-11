#ifndef MELEE_WEB_GAMEPLAY_PS_MATH_H
#define MELEE_WEB_GAMEPLAY_PS_MATH_H

#include <dolphin/mtx.h>

/* Scalar expansions of the original paired-single SDK routines, with their
 * rounding boundaries preserved. Wired through the gameplay SDK aliases. */
void melee_web_ps_mtx_concat(const Mtx a, const Mtx b, Mtx out);
void melee_web_ps_mtx_quat(Mtx out, const Quaternion* q);

#endif
