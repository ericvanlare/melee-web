#include "source_audio_ar_services.h"

#include <stdlib.h>
#include <string.h>

enum {
    kDspStatus = 5,
    kDspDmaMainHigh = 16,
    kDspDmaMainLow = 17,
    kDspDmaAramHigh = 18,
    kDspDmaAramLow = 19,
    kDspDmaSizeHigh = 20,
    kDspDmaSizeLow = 21,
    kDmaBusy = 0x200,
    kAramModeRegister = 9,
    kProbeAliasMode = 4,
    kProbeAliasWindow = 0x00400000u,
};

static int valid_range(uint32_t base, uint32_t length,
                       uint32_t address, uint32_t extent)
{
    const uint64_t end = (uint64_t)address + extent;
    return extent && (uint64_t)address >= base && end <= (uint64_t)base + length;
}

static void fail(MeleeWebSourceAudioArService* service, const char* message)
{
    if (!service) return;
    service->failed = 1;
    if (service->platform.abort)
        service->platform.abort(service->platform.user, message);
    /* A diagnostic callback is not an error transport. If it returns, stop
     * rather than letting source code observe a fabricated register result. */
    abort();
}

static unsigned char* resolve_mainmem(MeleeWebSourceAudioArService* service,
                                      uint32_t address, uint32_t length,
                                      MeleeWebSourceAudioArTransferType type, uint64_t* generation)
{
    if (!service->platform.resolve_mainmem)
        return NULL;
    return service->platform.resolve_mainmem(
        service->platform.user, address, length, type, generation);
}

static int copy_pending(MeleeWebSourceAudioArService* service)
{
    uint64_t generation = 0;
    unsigned char* mainmem = resolve_mainmem(
        service, service->pending_mainmem_address, service->pending_length,
        (MeleeWebSourceAudioArTransferType)service->pending_type, &generation);
    if (!mainmem || mainmem != service->pending_mainmem ||
        !generation || generation != service->pending_generation) {
        fail(service, "AR DMA borrowed span lifetime changed before completion");
        return 0;
    }
    if (!valid_range(0, service->aram_bus_length,
                                 service->pending_aram_address,
                                 service->pending_length)) {
        fail(service, "AR DMA span is outside the declared owned context");
        return 0;
    }
    if (service->pending_type != MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM &&
        service->pending_type != MELEE_WEB_SOURCE_AUDIO_AR_ARAM_TO_MRAM) {
        fail(service, "AR DMA direction is unsupported");
        return 0;
    }
    if ((uint64_t)service->pending_aram_address + service->pending_length <=
        service->aram_length) {
        if (service->pending_type == MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM) {
            memcpy(service->aram + service->pending_aram_address, mainmem,
                   service->pending_length);
            /* Rev-B probing uses mode 4 to mirror the low 4 MiB at +4 MiB.
             * This is the same declared profile behavior as the ARInit
             * oracle; it is not a generic modulo mapping. */
            if (service->aram_mode == kProbeAliasMode &&
                service->pending_aram_address < kProbeAliasWindow &&
                (uint64_t)service->pending_aram_address + service->pending_length <=
                    kProbeAliasWindow &&
                (uint64_t)service->pending_aram_address + kProbeAliasWindow +
                    service->pending_length <= service->aram_length) {
                memcpy(service->aram + kProbeAliasWindow +
                           service->pending_aram_address,
                       mainmem, service->pending_length);
            }
        } else {
            memcpy(mainmem, service->aram + service->pending_aram_address,
                   service->pending_length);
        }
    } else if (service->phase == MELEE_WEB_SOURCE_AUDIO_AR_SYNC &&
               service->pending_aram_address >= service->aram_length) {
        /* A declared 16 MiB profile has no expansion. ARInit deliberately
         * probes absent expansion and leaves the destination unchanged. */
        return 1;
    } else {
        fail(service, "AR DMA crosses the declared physical ARAM boundary");
        return 0;
    }
    return 1;
}

static void clear_pending(MeleeWebSourceAudioArService* service)
{
    service->pending = 0;
    service->pending_mainmem = NULL;
    service->pending_generation = 0;
    service->pending_mainmem_address = 0;
    service->pending_aram_address = 0;
    service->pending_length = 0;
    if (service->platform.set_dma_busy)
        service->platform.set_dma_busy(service->platform.user, 0);
}

static void submit_after_dma_register(MeleeWebSourceAudioArService* service)
{
    const uint32_t main_address =
        ((uint32_t)service->platform.dsp_read(service->platform.user,
                                              kDspDmaMainHigh) << 16) |
        service->platform.dsp_read(service->platform.user, kDspDmaMainLow);
    const uint32_t aram_address =
        ((uint32_t)service->platform.dsp_read(service->platform.user,
                                              kDspDmaAramHigh) << 16) |
        service->platform.dsp_read(service->platform.user, kDspDmaAramLow);
    const uint32_t size_high = service->platform.dsp_read(
        service->platform.user, kDspDmaSizeHigh);
    const uint32_t length = ((size_high & 0x03ffu) << 16) |
        service->platform.dsp_read(service->platform.user, kDspDmaSizeLow);
    const uint32_t type = (size_high & 0x8000u)
        ? MELEE_WEB_SOURCE_AUDIO_AR_ARAM_TO_MRAM
        : MELEE_WEB_SOURCE_AUDIO_AR_MRAM_TO_ARAM;
    if (service->pending) {
        fail(service, "AR DMA submitted while a prior transfer is pending");
        return;
    }
    if (service->failed) return;
    if (service->phase != MELEE_WEB_SOURCE_AUDIO_AR_SYNC &&
        service->phase != MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED) {
        fail(service, "AR DMA submitted in an unsupported source phase");
        return;
    }
    uint64_t generation = 0;
    unsigned char* mainmem = resolve_mainmem(
        service, main_address, length,
        (MeleeWebSourceAudioArTransferType)type, &generation);
    if (!length || (main_address & 31u) || (aram_address & 31u) ||
        (length & 31u) || !mainmem || !generation ||
        !valid_range(0, service->aram_bus_length, aram_address, length)) {
        fail(service, "AR DMA register span is invalid");
        return;
    }
    if (service->phase == MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED &&
        !valid_range(0, service->aram_length, aram_address, length)) {
        fail(service, "deferred AR DMA is outside physical ARAM");
        return;
    }
    service->pending_mainmem = mainmem;
    service->pending_generation = generation;
    service->pending_type = type;
    service->pending_mainmem_address = main_address;
    service->pending_aram_address = aram_address;
    service->pending_length = length;
    if (service->aram_mode != 3 && service->aram_mode != 4) {
        fail(service, "AR DMA uses an unsupported declared ARAM mode");
        return;
    }
    if (service->aram_mode == kProbeAliasMode &&
        aram_address < kProbeAliasWindow &&
        (uint64_t)aram_address + length > kProbeAliasWindow) {
        fail(service, "AR DMA crosses the declared mode-4 mirror boundary");
        return;
    }
    service->pending = 1;
    ++service->submitted_count;
    if (service->platform.set_dma_busy)
        service->platform.set_dma_busy(service->platform.user, 1);

}

int melee_web_source_audio_ar_initialize(
    MeleeWebSourceAudioArService* service,
    const MeleeWebSourceAudioArPlatform* platform,
    unsigned char* aram, uint32_t aram_length, uint32_t aram_bus_length)
{
    if (!service || !platform || !platform->dsp_read || !platform->dsp_write ||
        !platform->disable_interrupts || !platform->restore_interrupts ||
        !platform->set_dma_busy || !platform->resolve_mainmem ||
        !platform->set_interrupt_handler || !platform->dispatch_interrupt ||
        !platform->raise_interrupt ||
        !platform->abort || !aram ||
        aram_length != MELEE_WEB_SOURCE_AUDIO_AR_DECLARED_ARAM_LENGTH ||
        aram_bus_length != MELEE_WEB_SOURCE_AUDIO_AR_DECLARED_ARAM_BUS_LENGTH)
        return -1;
    if (service->initialized) {
        fail(service, "AR service reinitialized before shutdown");
        return -2;
    }
    memset(service, 0, sizeof(*service));
    service->platform = *platform;
    service->aram = aram;
    service->aram_length = aram_length;
    service->aram_bus_length = aram_bus_length;
    service->phase = MELEE_WEB_SOURCE_AUDIO_AR_SYNC;
    service->initialized = 1;
    return 0;
}

void melee_web_source_audio_ar_shutdown(MeleeWebSourceAudioArService* service)
{
    if (!service) return;
    if (service->pending || service->pumping) {
        fail(service, "AR service shutdown while transfer is pending");
        return;
    }
    memset(service, 0, sizeof(*service));
}

uint16_t melee_web_source_audio_ar_dsp_read(
    MeleeWebSourceAudioArService* service, unsigned index)
{
    uint16_t result;
    if (!service) abort();
    if (!service->initialized || !service->platform.dsp_read) {
        fail(service, "AR DSP read without initialized service");
        return 0;
    }
    if (service->failed) fail(service, "AR DSP read after service failure");
    result = service->platform.dsp_read(service->platform.user, index);
    if (index == kDspStatus && service->pending && (result & kDmaBusy) &&
        service->phase == MELEE_WEB_SOURCE_AUDIO_AR_SYNC) {
        /* This is the source __ARWaitForDMA completion boundary. Deferred ARQ
         * transfers never enter this path; their busy bit remains set until
         * the explicit completion pump. */
        if (copy_pending(service) && !service->failed) {
            clear_pending(service);
            result = service->platform.dsp_read(service->platform.user, index);
        }
    }
    return result;
}

void melee_web_source_audio_ar_dsp_write(
    MeleeWebSourceAudioArService* service, unsigned index, uint16_t value)
{
    if (!service) abort();
    if (!service->initialized || !service->platform.dsp_write) {
        fail(service, "AR DSP write without initialized service");
        return;
    }
    if (service->failed) fail(service, "AR DSP write after service failure");
    service->platform.dsp_write(service->platform.user, index, value);
    if (index == kAramModeRegister)
        service->aram_mode = value & 0x0fu;
    if (index == kDspDmaSizeLow)
        submit_after_dma_register(service);
}

int melee_web_source_audio_ar_set_phase(
    MeleeWebSourceAudioArService* service,
    MeleeWebSourceAudioArPhase phase)
{
    if (!service || !service->initialized) {
        if (service) fail(service, "AR phase changed without service");
        return -2;
    }
    if (service->failed) return -9;
    if (phase != MELEE_WEB_SOURCE_AUDIO_AR_SYNC &&
        phase != MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED) {
        fail(service, "AR phase is unsupported");
        return -1;
    }
    if (service->pending || service->pumping) {
        fail(service, "AR phase changed while DMA is pending");
        return -3;
    }
    service->phase = phase;
    return 0;
}

int melee_web_source_audio_ar_pump(MeleeWebSourceAudioArService* service)
{
    int previous;
    if (!service || !service->initialized) return -2;
    if (service->failed) return -9;
    if (!service->pending) return -4;
    if (service->pumping) return -5;
    if (service->phase != MELEE_WEB_SOURCE_AUDIO_AR_DEFERRED) return -6;
    service->pumping = 1;
    previous = service->platform.disable_interrupts
        ? service->platform.disable_interrupts(service->platform.user) : 0;
    if (!previous) {
        service->pumping = 0;
        if (service->platform.restore_interrupts)
            service->platform.restore_interrupts(service->platform.user, previous);
        return -8;
    }
    if (!copy_pending(service)) {
        service->pumping = 0;
        if (service->platform.restore_interrupts)
            service->platform.restore_interrupts(service->platform.user, previous);
        return -7;
    }
    clear_pending(service);
    if (service->failed) {
        service->pumping = 0;
        if (service->platform.restore_interrupts)
            service->platform.restore_interrupts(service->platform.user, previous);
        return -7;
    }
    service->platform.raise_interrupt(service->platform.user, 6);
    if (service->failed) {
        if (service->platform.restore_interrupts)
            service->platform.restore_interrupts(service->platform.user, previous);
        service->pumping = 0;
        return -7;
    }
    if (!service->platform.dispatch_interrupt) {
        fail(service, "AR service has no platform interrupt dispatcher");
    } else {
        /* The platform owner dispatches the handler installed by original
         * ARInit. The provider never calls the ARQ request callback directly. */
        service->platform.dispatch_interrupt(service->platform.user, 6);
    }
    if (service->platform.restore_interrupts)
        service->platform.restore_interrupts(service->platform.user, previous);
    service->pumping = 0;
    return 0;
}

int melee_web_source_audio_ar_pending(
    const MeleeWebSourceAudioArService* service)
{
    return service && service->initialized && service->pending;
}

uint32_t melee_web_source_audio_ar_submitted_count(
    const MeleeWebSourceAudioArService* service)
{
    return service ? service->submitted_count : 0;
}
