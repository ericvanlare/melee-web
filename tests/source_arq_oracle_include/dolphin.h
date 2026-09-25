#ifndef MELEE_WEB_SOURCE_ARQ_DOLPHIN_H
#define MELEE_WEB_SOURCE_ARQ_DOLPHIN_H

#include <dolphin/types.h>
#include <dolphin/ar.h>

/* This is a controlled single-thread oracle boundary. It models the source
 * interrupt mask and deliberately makes completion pumping an external step;
 * it does not claim hardware interrupt timing. */
static int melee_web_source_arq_interrupt_masked;

static inline int OSDisableInterrupts(void)
{
    const int previous_enabled = !melee_web_source_arq_interrupt_masked;
    melee_web_source_arq_interrupt_masked = 1;
    return previous_enabled;
}

static inline void OSRestoreInterrupts(int enabled)
{
    if (enabled != 0 && enabled != 1) __builtin_trap();
    melee_web_source_arq_interrupt_masked = !enabled;
}

static inline int melee_web_source_arq_interrupts_masked(void)
{
    return melee_web_source_arq_interrupt_masked;
}

#define ASSERTLINE(line, condition) \
    do { if (!(condition)) { (void)(line); __builtin_trap(); } } while (0)

#endif
