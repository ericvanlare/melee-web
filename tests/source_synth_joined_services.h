#ifndef MELEE_WEB_SOURCE_SYNTH_JOINED_SERVICES_H
#define MELEE_WEB_SOURCE_SYNTH_JOINED_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Fixture-only entry point.  The four driver values and SRAM bytes are
 * independently derived by the caller.  HSD_SynthInit owns the AXInit call;
 * this provider never performs an earlier AXInit. */
int melee_web_source_synth_joined_run(
    int dsp_size, int voices, int stream_size, int bank_size,
    const unsigned char* sram_settings, unsigned sram_length,
    const char* mode);

/* Split boundaries are exported for focused callers that need to inspect the
 * source AR/ARQ state between preparation, Synth entry, and deferred pumps. */
int melee_web_source_synth_joined_prepare(void);
int melee_web_source_synth_joined_begin_synth(
    int dsp_size, int voices, int stream_size, int bank_size,
    const unsigned char* sram_settings, unsigned sram_length);
int melee_web_source_synth_joined_pump(void);
/* Read original AR allocator state after the joined source startup. */
void melee_web_source_synth_aram_state(unsigned* stack, unsigned* free_blocks);

#ifdef __cplusplus
}
#endif
#endif
