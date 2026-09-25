/* Included only in the original DevCom translation unit by the joined probe.
 * Expose authored relay objects without using captured source addresses. */
#ifndef MELEE_WEB_SOURCE_SYNTH_JOINED_ACCESSORS_H
#define MELEE_WEB_SOURCE_SYNTH_JOINED_ACCESSORS_H
#include <stdint.h>
#include <stdlib.h>

_Static_assert(sizeof(void*) == 4, "DevCom descriptor requires Wasm32");
_Static_assert(sizeof(HSD_DevCom_804C6330_bufs) / sizeof(HSD_DevCom_804C6330_bufs[0]) == 2,
               "DevCom authored relay count changed");
_Static_assert(sizeof(HSD_DevCom_804C6330_bufs[0]) == DEVCOM_BUF_SIZE,
               "DevCom authored relay extent changed");
unsigned melee_web_source_devcom_spans(uint32_t* addresses, uint32_t* lengths,
                                       unsigned capacity)
{
    const unsigned count = sizeof(HSD_DevCom_804C6330_bufs) /
                           sizeof(HSD_DevCom_804C6330_bufs[0]);
    if (!addresses && !lengths && capacity == 0) return count;
    if (!addresses || !lengths || capacity != count) abort();
    for (unsigned i = 0; i < count; ++i) {
        addresses[i] = (uint32_t)(uintptr_t)HSD_DevCom_804C6330_bufs[i];
        lengths[i] = sizeof(HSD_DevCom_804C6330_bufs[i]);
        if (addresses[i] & (ARQ_DMA_ALIGNMENT - 1)) abort();
    }
    return count;
}
#endif
