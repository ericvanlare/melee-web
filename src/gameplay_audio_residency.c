#include "gameplay_audio_residency.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESIDENCY_FILE_MAX (64U * 1024U * 1024U)

typedef struct Asset {
    char path[MELEE_WEB_AUDIO_RESIDENCY_MAX_PATH + 1];
    const uint8_t* bytes;
    size_t byte_count;
    int entry_number;
    uint32_t header_bytes;
    uint32_t payload_bytes;
    uint32_t sample_count;
    uint32_t base_sample_id;
    uint32_t channel_count;
} Asset;

struct MeleeWebAudioResidency {
    Asset assets[MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS];
    uint32_t asset_count;
};

static int fail(char* error, size_t error_size, const char* message)
{
    if (error != NULL && error_size != 0) {
        snprintf(error, error_size, "%s", message);
    }
    return 0;
}

static void clear_error(char* error, size_t error_size)
{
    if (error != NULL && error_size != 0) {
        error[0] = '\0';
    }
}

static int range_ok(size_t size, size_t offset, size_t length)
{
    return offset <= size && length <= size - offset;
}

static uint16_t read_be16(const uint8_t* bytes, size_t offset)
{
    return (uint16_t)(((uint16_t)bytes[offset] << 8) | bytes[offset + 1]);
}

static uint32_t read_be32(const uint8_t* bytes, size_t offset)
{
    return ((uint32_t)bytes[offset] << 24) |
           ((uint32_t)bytes[offset + 1] << 16) |
           ((uint32_t)bytes[offset + 2] << 8) |
           bytes[offset + 3];
}

static int round_up_32(size_t value, size_t* rounded)
{
    if (value > SIZE_MAX - 31U) {
        return 0;
    }
    *rounded = (value + 31U) & ~(size_t)31U;
    return 1;
}

static int parse_ssm(const MeleeWebAudioResidencyAsset* input, Asset* output,
                     char* error, size_t error_size)
{
    const uint8_t* bytes = input->bytes;
    const size_t size = input->byte_count;
    uint32_t header_bytes;
    uint32_t payload_bytes;
    uint32_t sample_count;
    uint32_t base_sample_id;
    size_t header_end;
    size_t payload_offset;
    size_t cursor = 16;
    uint32_t channels = 0;
    uint32_t sample;

    if (size < 16 || size > RESIDENCY_FILE_MAX) {
        return fail(error, error_size, "SSM file header is truncated");
    }
    header_bytes = read_be32(bytes, 0);
    payload_bytes = read_be32(bytes, 4);
    sample_count = read_be32(bytes, 8);
    base_sample_id = read_be32(bytes, 12);
    if (header_bytes > (uint32_t)(size - 16U)) {
        return fail(error, error_size, "SSM header extent exceeds file");
    }
    header_end = (size_t)header_bytes + 16U;
    if (!round_up_32(header_end, &payload_offset)) {
        return fail(error, error_size, "SSM payload alignment overflows");
    }
    if (header_bytes < 16 || sample_count == 0 ||
        sample_count > 4096 || sample_count > UINT32_MAX - base_sample_id) {
        return fail(error, error_size, "SSM header or sample ID range is invalid");
    }
    if (payload_offset > size || payload_bytes == 0 ||
        payload_bytes != size - payload_offset) {
        return fail(error, error_size, "SSM payload extent is invalid");
    }

    for (sample = 0; sample < sample_count; ++sample) {
        uint32_t voices;
        uint32_t rate;
        uint32_t voice;
        if (!range_ok(header_end, cursor, 8)) {
            return fail(error, error_size, "SSM sample header crosses its extent");
        }
        voices = read_be32(bytes, cursor);
        rate = read_be32(bytes, cursor + 4);
        cursor += 8;
        if (voices == 0 || voices > 2 || rate == 0 || rate > 192000 ||
            voices > (header_end - cursor) / 64U) {
            return fail(error, error_size,
                        "SSM sample channel descriptor is invalid");
        }
        channels += voices;
        for (voice = 0; voice < voices; ++voice, cursor += 64) {
            const uint16_t loop = read_be16(bytes, cursor);
            const uint16_t format = read_be16(bytes, cursor + 2);
            const uint32_t loop_nibble = read_be32(bytes, cursor + 4);
            const uint32_t end_nibble = read_be32(bytes, cursor + 8);
            const uint32_t current_nibble = read_be32(bytes, cursor + 12);
            const uint16_t gain = read_be16(bytes, cursor + 48);
            const uint16_t predictor_scale = read_be16(bytes, cursor + 50);
            const uint16_t loop_predictor_scale = read_be16(bytes, cursor + 56);
            const uint64_t payload_nibbles = (uint64_t)payload_bytes * 2U;

            if (loop > 1 || format != 0 || gain != 0 ||
                (predictor_scale & 0xff80U) != 0 ||
                (loop_predictor_scale & 0xff80U) != 0 ||
                current_nibble > end_nibble || end_nibble >= payload_nibbles ||
                current_nibble % 16U < 2 || end_nibble % 16U < 2 ||
                (loop && (loop_nibble < current_nibble ||
                          loop_nibble > end_nibble))) {
                return fail(error, error_size,
                            "SSM DSP channel descriptor is invalid");
            }
        }
    }
    if (cursor != header_end) {
        return fail(error, error_size, "SSM header has unexplained trailing bytes");
    }

    output->bytes = input->bytes;
    output->byte_count = input->byte_count;
    output->entry_number = input->entry_number;
    output->header_bytes = header_bytes;
    output->payload_bytes = payload_bytes;
    output->sample_count = sample_count;
    output->base_sample_id = base_sample_id;
    output->channel_count = channels;
    return 1;
}

static int find_asset(const MeleeWebAudioResidency* residency, const char* path)
{
    uint32_t i;
    for (i = 0; i < residency->asset_count; ++i) {
        if (strcmp(residency->assets[i].path, path) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_entry(const MeleeWebAudioResidency* residency, int entry_number)
{
    uint32_t i;
    for (i = 0; i < residency->asset_count; ++i) {
        if (residency->assets[i].entry_number == entry_number) {
            return (int)i;
        }
    }
    return -1;
}

static void fill_info(const Asset* asset, MeleeWebAudioResidencyInfo* info)
{
    info->path = asset->path;
    info->entry_number = asset->entry_number;
    info->header_bytes = asset->header_bytes;
    info->payload_bytes = asset->payload_bytes;
    info->sample_count = asset->sample_count;
    info->base_sample_id = asset->base_sample_id;
    info->channel_count = asset->channel_count;
}

MeleeWebAudioResidency* melee_web_audio_residency_create(char* error,
                                                          size_t error_size)
{
    MeleeWebAudioResidency* result =
        (MeleeWebAudioResidency*)calloc(1, sizeof(*result));
    if (result == NULL) {
        fail(error, error_size, "Audio residency allocation failed");
        return NULL;
    }
    clear_error(error, error_size);
    return result;
}

int melee_web_audio_residency_destroy(MeleeWebAudioResidency* residency,
                                      char* error, size_t error_size)
{
    if (residency == NULL) {
        return fail(error, error_size, "Audio residency is not active");
    }
    free(residency);
    clear_error(error, error_size);
    return 1;
}

int melee_web_audio_residency_register(MeleeWebAudioResidency* residency,
                                       const MeleeWebAudioResidencyAsset* input,
                                       char* error, size_t error_size)
{
    Asset parsed;
    size_t path_length;
    if (residency == NULL || input == NULL || input->path == NULL ||
        input->bytes == NULL || input->entry_number < 0) {
        return fail(error, error_size, "Audio residency asset is incomplete");
    }
    path_length = strlen(input->path);
    if (path_length == 0 ||
        path_length > MELEE_WEB_AUDIO_RESIDENCY_MAX_PATH ||
        find_asset(residency, input->path) >= 0 ||
        find_entry(residency, input->entry_number) >= 0) {
        return fail(error, error_size,
                    "Audio residency path or entry is duplicated");
    }
    if (residency->asset_count >= MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS) {
        return fail(error, error_size,
                    "Audio residency asset capacity is exhausted");
    }
    memset(&parsed, 0, sizeof(parsed));
    if (!parse_ssm(input, &parsed, error, error_size)) {
        return 0;
    }
    memcpy(parsed.path, input->path, path_length + 1);
    residency->assets[residency->asset_count++] = parsed;
    clear_error(error, error_size);
    return 1;
}

int melee_web_audio_residency_find_path(const MeleeWebAudioResidency* residency,
                                        const char* path, int* entry_number,
                                        MeleeWebAudioResidencyInfo* info,
                                        char* error, size_t error_size)
{
    const int index = residency == NULL || path == NULL
                          ? -1
                          : find_asset(residency, path);
    if (index < 0 || entry_number == NULL) {
        return fail(error, error_size, "Audio residency path is unknown");
    }
    if (info != NULL) {
        fill_info(&residency->assets[index], info);
    }
    *entry_number = residency->assets[index].entry_number;
    clear_error(error, error_size);
    return 1;
}

int melee_web_audio_residency_info(const MeleeWebAudioResidency* residency,
                                   int entry_number,
                                   MeleeWebAudioResidencyInfo* info,
                                   char* error, size_t error_size)
{
    const int index = residency == NULL ? -1 : find_entry(residency, entry_number);
    if (index < 0 || info == NULL) {
        return fail(error, error_size, "Audio residency entry is unknown");
    }
    fill_info(&residency->assets[index], info);
    clear_error(error, error_size);
    return 1;
}

int melee_web_audio_residency_read(const MeleeWebAudioResidency* residency,
                                   int entry_number, size_t source_offset,
                                   void* destination, size_t byte_count,
                                   char* error, size_t error_size)
{
    const int index = residency == NULL ? -1 : find_entry(residency, entry_number);
    const Asset* asset;
    if (index < 0) {
        return fail(error, error_size, "Audio residency entry is unknown");
    }
    if (byte_count != 0 && destination == NULL) {
        return fail(error, error_size, "Audio residency read destination is null");
    }
    asset = &residency->assets[index];
    if (!range_ok(asset->byte_count, source_offset, byte_count)) {
        return fail(error, error_size, "Audio residency read exceeds source bytes");
    }
    if (byte_count != 0) {
        memcpy(destination, asset->bytes + source_offset, byte_count);
    }
    clear_error(error, error_size);
    return 1;
}
