#ifndef MELEE_WEB_GAMEPLAY_AUDIO_RESIDENCY_H
#define MELEE_WEB_GAMEPLAY_AUDIO_RESIDENCY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS = 55,
    MELEE_WEB_AUDIO_RESIDENCY_MAX_PATH = 255,
};

typedef struct MeleeWebAudioResidency MeleeWebAudioResidency;

/* This boundary owns validated source bytes and exact DVD path identity. The
 * original HSD synth owns request scheduling, source-bank capacity/accounting,
 * sample publication, and unload. No load queue or parallel bank state is
 * hidden here. */
typedef struct MeleeWebAudioResidencyInfo {
    const char* path;
    int entry_number;
    uint32_t header_bytes;
    uint32_t payload_bytes;
    uint32_t sample_count;
    uint32_t base_sample_id;
    uint32_t channel_count;
} MeleeWebAudioResidencyInfo;

typedef struct MeleeWebAudioResidencyAsset {
    const char* path;
    const uint8_t* bytes;
    size_t byte_count;
    int entry_number;
} MeleeWebAudioResidencyAsset;

MeleeWebAudioResidency* melee_web_audio_residency_create(char* error,
                                                          size_t error_size);

/* The caller must stop the original synth and release its source-bank
 * references before destroying the registry, so no future DevCom request can
 * refer to these borrowed bytes. */
int melee_web_audio_residency_destroy(MeleeWebAudioResidency*, char*, size_t);

/* register borrows asset bytes and copies the exact path. The bytes must stay
 * unchanged until destroy. Registration validates the complete original SSM
 * header and every DSP channel descriptor. */
int melee_web_audio_residency_register(MeleeWebAudioResidency*,
                                       const MeleeWebAudioResidencyAsset*,
                                       char*, size_t);

/* Resolution is deliberately exact: no language aliases, basename fallback,
 * or guessed archive paths are accepted. */
int melee_web_audio_residency_find_path(const MeleeWebAudioResidency*,
                                        const char* path, int* entry_number,
                                        MeleeWebAudioResidencyInfo*, char*,
                                        size_t);

int melee_web_audio_residency_info(const MeleeWebAudioResidency*,
                                   int entry_number,
                                   MeleeWebAudioResidencyInfo*, char*, size_t);

/* Read source bytes for the existing HSD_DevComRequest bridge. Header reads
 * preserve big-endian source bytes; the original synth hook converts its
 * typed header fields before invoking the source header callback. Sample
 * reads preserve raw DSP bytes for the original sample publication path. */
int melee_web_audio_residency_read(const MeleeWebAudioResidency*, int entry_number,
                                   size_t source_offset, void* destination,
                                   size_t byte_count, char*, size_t);

#ifdef __cplusplus
}
#endif

#endif
