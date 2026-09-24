#ifndef MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_H
#define MELEE_WEB_SOURCE_AUDIO_AR_SERVICES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct OSContext;
typedef void (*MeleeWebSourceAudioArInterruptHandler)(short, struct OSContext*);

typedef enum MeleeWebSourceAudioArTransferType {
    MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM = 0,
    MELEE_WEB_SOURCE_AUDIO_AR_ARAM_TO_MRAM = 1,
} MeleeWebSourceAudioArTransferType;

/* This provider intentionally implements only the source startup profile
 * already validated by the ARInit oracle. Its mode-4 mirror rules must not be
 * reused for arbitrary hardware sizes. */
#define MELEE_WEB_SOURCE_AUDIO_AR_DECLARED_ARAM_LENGTH 0x01000000u
#define MELEE_WEB_SOURCE_AUDIO_AR_DECLARED_ARAM_BUS_LENGTH 0x04000000u

typedef struct MeleeWebSourceAudioArPlatform {
    void* user;
    uint16_t (*dsp_read)(void* user, unsigned index);
    void (*dsp_write)(void* user, unsigned index, uint16_t value);
    int (*disable_interrupts)(void* user);
    void (*restore_interrupts)(void* user, int enabled);
    void (*set_dma_busy)(void* user, int busy);
    unsigned char* (*resolve_mainmem)(
        void* user, uint32_t address, uint32_t length,
        MeleeWebSourceAudioArTransferType type, uint64_t* generation);
    void (*set_interrupt_handler)(void* user, int exception,
                                  MeleeWebSourceAudioArInterruptHandler handler);
    void (*dispatch_interrupt)(void* user, int exception);
    /* Publish the AR completion status through the shared DSP/interrupt
     * owner before dispatch_interrupt invokes the original AR handler. */
    void (*raise_interrupt)(void* user, int exception);
    void (*clear_context)(void* user, struct OSContext* context);
    void (*set_current_context)(void* user, struct OSContext* context);
    void (*flush_range)(void* user, void* address, uint32_t length);
    void (*invalidate_range)(void* user, void* address, uint32_t length);
    void* (*physical_to_uncached)(void* user, uint32_t address);
    void (*abort)(void* user, const char* message);
} MeleeWebSourceAudioArPlatform;

typedef enum MeleeWebSourceAudioArPhase {
    /* ARInit's private size probes wait for each transfer in source code. */
    MELEE_WEB_SOURCE_AUDIO_AR_SYNC = 0,
    /* ARStartDMA returns with the transfer owned by the explicit pump. */
    MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED = 1,
} MeleeWebSourceAudioArPhase;

typedef struct MeleeWebSourceAudioArService {
    MeleeWebSourceAudioArPlatform platform;
    unsigned char* aram;
    uint32_t aram_length;
    uint32_t aram_bus_length;
    unsigned char* pending_mainmem;
    uint64_t pending_generation;
    uint32_t pending_type;
    uint32_t pending_mainmem_address;
    uint32_t pending_aram_address;
    uint32_t pending_length;
    uint32_t aram_mode;
    MeleeWebSourceAudioArPhase phase;
    unsigned initialized;
    unsigned pending;
    unsigned pumping;
    unsigned failed;
    uint32_t submitted_count;
} MeleeWebSourceAudioArService;

/* Call with a zero-initialized service. An initialized owner must be shut down
 * before reuse; every resolved span supplies a nonzero lifetime generation. */
int melee_web_source_audio_ar_initialize(
    MeleeWebSourceAudioArService* service,
    const MeleeWebSourceAudioArPlatform* platform,
    unsigned char* aram, uint32_t aram_length, uint32_t aram_bus_length);
void melee_web_source_audio_ar_shutdown(MeleeWebSourceAudioArService* service);

uint16_t melee_web_source_audio_ar_dsp_read(
    MeleeWebSourceAudioArService* service, unsigned index);
void melee_web_source_audio_ar_dsp_write(
    MeleeWebSourceAudioArService* service, unsigned index, uint16_t value);
int melee_web_source_audio_ar_set_phase(
    MeleeWebSourceAudioArService* service,
    MeleeWebSourceAudioArPhase phase);

/* In SYNC phase, the source __ARWaitForDMA poll completes the transfer. In
 * DEFERRED phase, busy remains asserted until this explicit pump; the pump
 * copies bytes, raises the shared AR completion status, and dispatches the
 * original AR interrupt handler. */
int melee_web_source_audio_ar_pump(MeleeWebSourceAudioArService* service);
int melee_web_source_audio_ar_pending(
    const MeleeWebSourceAudioArService* service);
uint32_t melee_web_source_audio_ar_submitted_count(
    const MeleeWebSourceAudioArService* service);

#ifdef __cplusplus
}
#endif

#endif
