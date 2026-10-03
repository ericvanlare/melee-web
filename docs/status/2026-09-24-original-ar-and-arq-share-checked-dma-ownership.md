# Original AR and ARQ share checked DMA ownership

**Compiled / Source identified / Native traced** for the [AR service fixture](../SOURCE_AUDIO_AR_SERVICES.md).
Original size probing and deferred ARQ completion execute through checked cache,
interrupt-mask and SDK context services. The [scoped receipt](../evidence/source-audio-ar-services-v1.json)
records negative ownership controls. The later Synth fixture above joins this
owner; full-session equivalence remains open; this component does not establish PCM or timing.
