#ifndef MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_DOLPHIN_H
#define MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_DOLPHIN_H

#include <dolphin/types.h>
#include <dolphin/os.h>
#include <dolphin/ar.h>

#ifdef __cplusplus
struct MeleeWebAudioArDspCell {
    unsigned index;
    operator u16() const;
    MeleeWebAudioArDspCell& operator=(u16 value);
};

struct MeleeWebAudioArDsp {
    MeleeWebAudioArDspCell operator[](unsigned index) { return {index}; }
};

extern MeleeWebAudioArDsp melee_web_audio_ar_dsp;
extern "C" {
int melee_web_audio_ar_disable_interrupts(void);
void melee_web_audio_ar_restore_interrupts(int enabled);
void melee_web_audio_ar_set_handler(int exception,
                                    void (*handler)(short, OSContext*));
OSInterruptMask melee_web_audio_ar_unmask(OSInterruptMask mask);
void melee_web_audio_ar_clear_context(void* context);
void melee_web_audio_ar_set_context(void* context);
void melee_web_audio_ar_flush(void* address, u32 length);
void melee_web_audio_ar_invalidate(void* address, u32 length);
void* melee_web_audio_ar_physical(u32 address);
void melee_web_audio_ar_abort(const char* message);
void melee_web_audio_ar_report(const char* format, ...);
}

#undef __DSPRegs
#undef DSP_ARAM_DMA_MM_HI
#undef DSP_ARAM_DMA_MM_LO
#undef DSP_ARAM_DMA_ARAM_HI
#undef DSP_ARAM_DMA_ARAM_LO
#undef DSP_ARAM_DMA_SIZE_HI
#undef DSP_ARAM_DMA_SIZE_LO
#undef OS_BUS_CLOCK
#define __DSPRegs melee_web_audio_ar_dsp
#define DSP_ARAM_DMA_MM_HI 16
#define DSP_ARAM_DMA_MM_LO 17
#define DSP_ARAM_DMA_ARAM_HI 18
#define DSP_ARAM_DMA_ARAM_LO 19
#define DSP_ARAM_DMA_SIZE_HI 20
#define DSP_ARAM_DMA_SIZE_LO 21
#define OS_BUS_CLOCK 162000000u
#define OSDisableInterrupts melee_web_audio_ar_disable_interrupts
#define OSRestoreInterrupts melee_web_audio_ar_restore_interrupts
#define __OSSetInterruptHandler melee_web_audio_ar_set_handler
#define __OSUnmaskInterrupts melee_web_audio_ar_unmask
#define OSClearContext melee_web_audio_ar_clear_context
#define OSSetCurrentContext melee_web_audio_ar_set_context
#define DCFlushRange melee_web_audio_ar_flush
#define DCInvalidateRange melee_web_audio_ar_invalidate
#define OSPhysicalToUncached melee_web_audio_ar_physical
#define ASSERTMSGLINE(line, condition, message) \
    do { if (!(condition)) melee_web_audio_ar_abort(message); } while (0)
#define ASSERTMSGLINEV(line, condition, ...) \
    do { if (!(condition)) melee_web_audio_ar_abort("assertion"); } while (0)
#define ASSERTLINE(line, condition) \
    do { if (!(condition)) melee_web_audio_ar_abort("assertion"); } while (0)

#define OSReport melee_web_audio_ar_report
#endif

#endif
