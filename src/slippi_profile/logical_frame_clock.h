/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_LOGICAL_FRAME_CLOCK_H
#define SLIPPI_LOGICAL_FRAME_CLOCK_H
#include <stdint.h>
enum { SLIPPI_CLOCK_COLD=0, SLIPPI_CLOCK_PENDING=1, SLIPPI_CLOCK_COMPLETED=2 };
enum { SLIPPI_CLOCK_NEXT_PRE=1, SLIPPI_CLOCK_POST=2, SLIPPI_CLOCK_PRESENT=3 };
typedef struct SlippiLogicalClockTransition { uint32_t phase, advance; } SlippiLogicalClockTransition;
/* Pure decision only. Invalid input leaves output untouched; no clock writes. */
int slippi_logical_clock_transition(uint32_t phase, unsigned event,
                                   SlippiLogicalClockTransition* output);
#endif
