#ifndef MELEE_WEB_SOURCE_ARAM_DOLPHIN_H
#define MELEE_WEB_SOURCE_ARAM_DOLPHIN_H

#include <dolphin/types.h>

#include <stddef.h>
#include <stdlib.h>

struct OSContext {
    u8 bytes[64];
};

static u16 oracle_dsp_regs[64];
static u8 oracle_uncached_bytes[64];
#define __DSPRegs oracle_dsp_regs

#define DSP_ARAM_DMA_MM_HI 16
#define DSP_ARAM_DMA_MM_LO 17
#define DSP_ARAM_DMA_ARAM_HI 18
#define DSP_ARAM_DMA_ARAM_LO 19
#define DSP_ARAM_DMA_SIZE_HI 20
#define DSP_ARAM_DMA_SIZE_LO 21
#define OS_BUS_CLOCK 162000000u

#define ASSERTMSGLINE(line, condition, message) \
    do { if (!(condition)) { (void)(line); (void)(message); abort(); } } while (0)
#define ASSERTMSGLINEV(line, condition, ...) \
    do { if (!(condition)) { (void)(line); abort(); } } while (0)

// This serial component fixture has no emulated threads or interrupt source.
// Only ARAlloc/ARFree use this explicit critical-section environment; hardware
// initialization and callbacks below are outside the fixture and abort.
static inline int OSDisableInterrupts(void) { return 0; }
static inline void OSRestoreInterrupts(int enabled) { (void)enabled; }

static inline void __OSSetInterruptHandler(int exception, void (*handler)(short, struct OSContext*))
{
    (void)exception;
    (void)handler;
    abort();
}
static inline void __OSUnmaskInterrupts(u32 mask)
{
    (void)mask;
    abort();
}
static inline void OSClearContext(struct OSContext* context)
{
    (void)context;
    abort();
}
static inline void OSSetCurrentContext(struct OSContext* context)
{
    (void)context;
    abort();
}
static inline void DCFlushRange(void* address, u32 length)
{
    (void)address;
    (void)length;
    abort();
}
static inline void DCInvalidateRange(void* address, u32 length)
{
    (void)address;
    (void)length;
    abort();
}
static inline void* oracle_physical_to_uncached(u32 address)
{
    (void)address;
    abort();
    return oracle_uncached_bytes;
}
#define OSPhysicalToUncached(address) oracle_physical_to_uncached(address)

static inline void OSReport(const char* format, ...)
{
    (void)format;
}

#endif
