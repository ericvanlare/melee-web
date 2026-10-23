/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "logical_frame_clock.h"
int slippi_logical_clock_transition(uint32_t phase,unsigned event,SlippiLogicalClockTransition* output)
{
    SlippiLogicalClockTransition next={phase,0};
    if(!output||phase>SLIPPI_CLOCK_COMPLETED)return 0;
    switch(event){
    case SLIPPI_CLOCK_NEXT_PRE:
        if(phase==SLIPPI_CLOCK_PENDING){next.phase=SLIPPI_CLOCK_COMPLETED;next.advance=1;}
        break;
    case SLIPPI_CLOCK_POST:next.phase=SLIPPI_CLOCK_PENDING;break;
    case SLIPPI_CLOCK_PRESENT:
        if(phase==SLIPPI_CLOCK_COLD)return 0;
        if(phase==SLIPPI_CLOCK_PENDING){next.phase=SLIPPI_CLOCK_COMPLETED;next.advance=1;}
        break;
    default:return 0;
    }
    *output=next;return 1;
}
