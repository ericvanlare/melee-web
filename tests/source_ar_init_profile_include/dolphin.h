#ifndef MELEE_WEB_SOURCE_AR_INIT_PROFILE_DOLPHIN_H
#define MELEE_WEB_SOURCE_AR_INIT_PROFILE_DOLPHIN_H

#include <dolphin/ar.h>

#ifdef __cplusplus
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>

struct OSContext {
    u8 bytes[64];
};

extern "C" {
u16 melee_web_source_ar_init_dsp_read(unsigned index);
void melee_web_source_ar_init_dsp_write(unsigned index, u16 value);
int melee_web_source_ar_init_disable_interrupts(void);
void melee_web_source_ar_init_restore_interrupts(int enabled);
void melee_web_source_ar_init_set_handler(int exception, void (*handler)(short, struct OSContext*));
void melee_web_source_ar_init_unmask(u32 mask);
void melee_web_source_ar_init_clear_context(void* context);
void melee_web_source_ar_init_set_context(void* context);
void melee_web_source_ar_init_flush(void* address, u32 length);
void melee_web_source_ar_init_invalidate(void* address, u32 length);
void* melee_web_source_ar_init_physical(u32 address);
void melee_web_source_ar_init_abort(const char* message);
}

struct MeleeWebSourceArInitDspCell {
    unsigned index;
    operator u16() const
    {
        return melee_web_source_ar_init_dsp_read(index);
    }
    MeleeWebSourceArInitDspCell& operator=(u16 value)
    {
        melee_web_source_ar_init_dsp_write(index, value);
        return *this;
    }
};

struct MeleeWebSourceArInitDsp {
    MeleeWebSourceArInitDspCell operator[](unsigned index)
    {
        return {index};
    }
};

extern MeleeWebSourceArInitDsp melee_web_source_ar_init_dsp;

#define __DSPRegs melee_web_source_ar_init_dsp
#define DSP_ARAM_DMA_MM_HI 16
#define DSP_ARAM_DMA_MM_LO 17
#define DSP_ARAM_DMA_ARAM_HI 18
#define DSP_ARAM_DMA_ARAM_LO 19
#define DSP_ARAM_DMA_SIZE_HI 20
#define DSP_ARAM_DMA_SIZE_LO 21
#define OS_BUS_CLOCK 162000000u

#define ASSERTMSGLINE(line, condition, message) \
    do { if (!(condition)) { (void)(line); melee_web_source_ar_init_abort(message); } } while (0)
#define ASSERTMSGLINEV(line, condition, ...) \
    do { if (!(condition)) { (void)(line); melee_web_source_ar_init_abort("assertion"); } } while (0)

#define OSDisableInterrupts melee_web_source_ar_init_disable_interrupts
#define OSRestoreInterrupts melee_web_source_ar_init_restore_interrupts
#define __OSSetInterruptHandler melee_web_source_ar_init_set_handler
#define __OSUnmaskInterrupts melee_web_source_ar_init_unmask
#define OSClearContext melee_web_source_ar_init_clear_context
#define OSSetCurrentContext melee_web_source_ar_init_set_context
#define DCFlushRange melee_web_source_ar_init_flush
#define DCInvalidateRange melee_web_source_ar_init_invalidate
#define OSPhysicalToUncached melee_web_source_ar_init_physical

static inline void OSReport(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
}

#endif

#endif
