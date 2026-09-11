#ifndef MELEE_WEB_GAMEPLAY_PS_MATH_H
#define MELEE_WEB_GAMEPLAY_PS_MATH_H

#include <dolphin/mtx.h>

/* The original PSMTXConcat arithmetic is a PowerPC paired-single sequence,
 * rather than an ordinary C matrix multiply. Keep this helper isolated until
 * the gameplay SDK compatibility boundary wires it into a source alias. */
void melee_web_ps_mtx_concat(const Mtx a, const Mtx b, Mtx out);

#endif
