#ifndef MELEE_WEB_GAMEPLAY_AUDIO_SILENT_CLOCK_H
#define MELEE_WEB_GAMEPLAY_AUDIO_SILENT_CLOCK_H

#include <stdint.h>

/* The public audio policy retains the AX source clock without retaining any
 * DSP state.  Ratios are unsigned 16.16 values; selector 2 is AX nearest and
 * advances exactly one source frame per output frame. */
static inline uint32_t melee_web_audio_silent_clock_advance(
    uint32_t* fraction, uint32_t ratio, uint8_t source_selector) {
    if (source_selector == 2) return 1;
    uint32_t phase = *fraction + ratio;
    uint32_t source_frames = phase >> 16;
    *fraction = phase & 0xffffu;
    return source_frames;
}

#endif
