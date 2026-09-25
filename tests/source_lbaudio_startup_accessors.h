/* Read-only accessors appended to the original lbAudioAx translation unit.
 *
 * This header must be included after lbaudio_ax.c.  Its symbols intentionally
 * refer to the source translation unit's private authored state; no source
 * state is changed by the accessor.  Function-local AR/FX objects are exposed
 * through the boundary hooks in the generated wrapper, because C has no way
 * to name those locals from a separate translation unit.
 */
#ifndef MELEE_WEB_SOURCE_LBAUDIO_STARTUP_ACCESSORS_H
#define MELEE_WEB_SOURCE_LBAUDIO_STARTUP_ACCESSORS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
static_assert(sizeof(void*) == 4, "lbAudioAx accessor requires Wasm32");
static_assert(sizeof(int) == 4, "lbAudioAx source int ABI changed");
#else
_Static_assert(sizeof(void*) == 4, "lbAudioAx accessor requires Wasm32");
_Static_assert(sizeof(int) == 4, "lbAudioAx source int ABI changed");
#endif

typedef struct {
    uint32_t ar_stack_address;
    uint32_t ar_stack_entries;
    uint32_t fx_count;
    uint32_t fx_addresses[2];
    uint32_t fx_sizes[2];
    uint32_t fx_success[2];
    uint32_t driver_call_count;
    uint32_t driver_voices;
    uint32_t driver_priority;
    uint32_t driver_sample_rate;
    uint32_t driver_aram_size;
    uint32_t bank_call_count;
    uint32_t bank_call_sizes[3];
    int32_t bank_descriptor_sizes[3];
    int32_t bank_total_size;
    int32_t sfx_state_counters[4];
    int32_t bookkeeping[4][0x38];
} MeleeWebSourceLBAudioSnapshot;

unsigned melee_web_source_lbaudio_snapshot(
    MeleeWebSourceLBAudioSnapshot* out);

#ifdef MELEE_WEB_LBAUDIO_ACCESSOR_IMPLEMENTATION
_Static_assert(sizeof(lbl_804337C4) / sizeof(lbl_804337C4[0]) == 0x38,
               "source SFX bookkeeping extent changed");
_Static_assert(sizeof(lbl_804338A4) / sizeof(lbl_804338A4[0]) == 0x38,
               "source SFX bookkeeping extent changed");
_Static_assert(sizeof(lbl_80433984) / sizeof(lbl_80433984[0]) == 0x38,
               "source SFX bookkeeping extent changed");
_Static_assert(sizeof(lbl_80433A64) / sizeof(lbl_80433A64[0]) == 0x38,
               "source SFX bookkeeping extent changed");

/* Returns zero for a null output pointer and one after copying the source
 * snapshot.  The destination is caller-owned; authored source objects are
 * read only. */
unsigned melee_web_source_lbaudio_snapshot(
    MeleeWebSourceLBAudioSnapshot* out)
{
    unsigned i;
    if (out == 0) {
        return 0;
    }

    out->ar_stack_address = melee_web_source_lbaudio_ar_stack_address;
    out->ar_stack_entries = melee_web_source_lbaudio_ar_stack_entries;
    out->fx_count = melee_web_source_lbaudio_fx_count;
    for (i = 0; i < 2; i++) {
        out->fx_addresses[i] = melee_web_source_lbaudio_fx_addresses[i];
        out->fx_sizes[i] = melee_web_source_lbaudio_fx_sizes[i];
        out->fx_success[i] = melee_web_source_lbaudio_fx_success[i];
    }
    out->driver_call_count = melee_web_source_lbaudio_driver_call_count;
    out->driver_voices = melee_web_source_lbaudio_driver_voices;
    out->driver_priority = melee_web_source_lbaudio_driver_priority;
    out->driver_sample_rate = melee_web_source_lbaudio_driver_sample_rate;
    out->driver_aram_size = melee_web_source_lbaudio_driver_aram_size;
    out->bank_call_count = melee_web_source_lbaudio_bank_call_count;
    for (i = 0; i < 3; i++) {
        out->bank_call_sizes[i] = melee_web_source_lbaudio_bank_call_sizes[i];
    }
    out->bank_descriptor_sizes[0] = lbl_804D643C;
    out->bank_descriptor_sizes[1] = lbl_804D6440;
    out->bank_descriptor_sizes[2] = lbl_804D6444;
    out->bank_total_size = lbl_804D6438;
    out->sfx_state_counters[0] = lbl_804D3878;
    out->sfx_state_counters[1] = lbl_804D6448;
    out->sfx_state_counters[2] = lbl_804D644C;
    out->sfx_state_counters[3] = lbl_804D6450;
    for (i = 0; i < 0x38; i++) {
        out->bookkeeping[0][i] = lbl_804337C4[i];
        out->bookkeeping[1][i] = lbl_804338A4[i];
        out->bookkeeping[2][i] = lbl_80433984[i];
        out->bookkeeping[3][i] = lbl_80433A64[i];
    }
    return 1;
}
#endif

#ifdef __cplusplus
}
#endif

#endif
