#include "gameplay_audio_residency.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void check(int value, const char* message)
{
    if (!value) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures += 1;
    }
}

static void word(unsigned char* bytes, size_t offset, unsigned value)
{
    bytes[offset] = (unsigned char)(value >> 24);
    bytes[offset + 1] = (unsigned char)(value >> 16);
    bytes[offset + 2] = (unsigned char)(value >> 8);
    bytes[offset + 3] = (unsigned char)value;
}

static void half(unsigned char* bytes, size_t offset, unsigned value)
{
    bytes[offset] = (unsigned char)(value >> 8);
    bytes[offset + 1] = (unsigned char)value;
}

static size_t make_ssm(unsigned char* bytes, size_t capacity,
                       unsigned payload_size, unsigned end_nibble,
                       unsigned base_id)
{
    const size_t header_bytes = 16 + 8 + 64 - 16;
    const size_t payload_offset = 96;
    const size_t total = payload_offset + payload_size;
    size_t i;
    check(capacity >= total, "synthetic SSM output capacity");
    memset(bytes, 0, capacity);
    word(bytes, 0, (unsigned)header_bytes);
    word(bytes, 4, payload_size);
    word(bytes, 8, 1);
    word(bytes, 12, base_id);
    word(bytes, 16, 1);
    word(bytes, 20, 32000);
    half(bytes, 24, 0);
    half(bytes, 26, 0);
    word(bytes, 28, 0);
    word(bytes, 32, end_nibble);
    word(bytes, 36, 2);
    for (i = 0; i < payload_size; ++i) {
        bytes[payload_offset + i] = (unsigned char)(0xa0U + (i & 0x1fU));
    }
    return total;
}

static void check_local_asset(MeleeWebAudioResidency* residency,
                              const char* filename, int entry, char* error,
                              size_t error_size)
{
    FILE* file = fopen(filename, "rb");
    unsigned char* bytes;
    long length;
    size_t byte_count;
    const char* basename;
    char source_path[MELEE_WEB_AUDIO_RESIDENCY_MAX_PATH + 1];
    MeleeWebAudioResidencyInfo info;
    unsigned char prefix[16];
    unsigned char* payload_copy;
    size_t payload_offset;

    check(file != NULL, "local SSM asset opens");
    if (file == NULL) {
        return;
    }
    check(fseek(file, 0, SEEK_END) == 0, "local SSM asset seeks to end");
    length = ftell(file);
    check(length >= 16, "local SSM asset has a complete header");
    if (length < 16) {
        fclose(file);
        return;
    }
    check(fseek(file, 0, SEEK_SET) == 0, "local SSM asset seeks to start");
    byte_count = (size_t)length;
    bytes = (unsigned char*)malloc(byte_count);
    check(bytes != NULL, "local SSM asset allocates its source bytes");
    if (bytes == NULL) {
        fclose(file);
        return;
    }
    check(fread(bytes, 1, byte_count, file) == byte_count,
          "local SSM asset reads completely");
    fclose(file);
    basename = strrchr(filename, '/');
    basename = basename == NULL ? filename : basename + 1;
    check(snprintf(source_path, sizeof(source_path), "/audio/us/%s", basename) > 0 &&
              strlen(source_path) <= MELEE_WEB_AUDIO_RESIDENCY_MAX_PATH,
          "local SSM path maps to the exact source path");
    check(melee_web_audio_residency_register(
              residency,
              &(MeleeWebAudioResidencyAsset){source_path, bytes, byte_count,
                                             entry},
              error, error_size),
          "extracted SSM passes native descriptor validation");
    check(melee_web_audio_residency_info(residency, entry, &info, error,
                                         error_size) &&
              info.payload_bytes != 0 && info.sample_count != 0 &&
              info.channel_count != 0,
          "extracted SSM publishes nonempty typed readiness");
    payload_offset = ((size_t)info.header_bytes + 16U + 31U) & ~(size_t)31U;
    payload_copy = (unsigned char*)malloc(info.payload_bytes);
    check(payload_copy != NULL, "extracted SSM allocates a payload read buffer");
    check(melee_web_audio_residency_read(residency, entry, 0, prefix,
                                         sizeof(prefix), error, error_size) &&
              payload_copy != NULL &&
              melee_web_audio_residency_read(residency, entry, payload_offset,
                                             payload_copy, info.payload_bytes,
                                             error, error_size),
          "extracted SSM supports bounded header and payload reads");
    free(payload_copy);
    free(bytes);
}

int main(int argc, char** argv)
{
    unsigned char valid[192];
    unsigned char malformed[192];
    unsigned char header[32];
    unsigned char payload[32];
    const size_t valid_size = make_ssm(valid, sizeof(valid), 32, 18, 1000);
    MeleeWebAudioResidencyAsset asset = {
        "/audio/us/nr_select.ssm", valid, valid_size, 42};
    MeleeWebAudioResidency* residency;
    MeleeWebAudioResidencyInfo info;
    char error[256];
    int entry = -1;

    residency = melee_web_audio_residency_create(error, sizeof(error));
    check(residency != NULL, "registry allocates without source hooks");
    check(melee_web_audio_residency_register(residency, &asset, error,
                                             sizeof(error)),
          "valid SSM registers after complete descriptor validation");
    check(melee_web_audio_residency_find_path(
              residency, asset.path, &entry, &info, error, sizeof(error)) &&
              entry == 42 && info.path != NULL &&
              strcmp(info.path, asset.path) == 0 && info.header_bytes == 72 &&
              info.payload_bytes == 32 && info.sample_count == 1 &&
              info.base_sample_id == 1000 && info.channel_count == 1,
          "exact path lookup returns typed SSM metadata");
    check(!melee_web_audio_residency_find_path(
              residency, "/audio/nr_select.ssm", &entry, &info, error,
              sizeof(error)),
          "unregistered language alias is rejected");
    check(melee_web_audio_residency_find_path(
              residency, asset.path, &entry, NULL, error, sizeof(error)) &&
              entry == 42,
          "transport can resolve an exact path without metadata allocation");

    check(melee_web_audio_residency_read(residency, 42, 0, header, 32, error,
                                         sizeof(error)) &&
              header[0] == 0 && header[3] == 72 && header[4] == 0 &&
              header[7] == 32 && header[11] == 1 && header[15] == 0xe8,
          "header read preserves original big-endian source bytes");
    check(melee_web_audio_residency_read(residency, 42, 96, payload, 32,
                                         error, sizeof(error)) &&
              payload[0] == 0xa0 && payload[31] == 0xbf,
          "sample read preserves raw DSP payload bytes");
    check(!melee_web_audio_residency_read(residency, 42, 120, payload, 16,
                                          error, sizeof(error)),
          "out-of-bounds sample read is rejected");
    check(!melee_web_audio_residency_read(residency, 99, 0, header, 1, error,
                                          sizeof(error)),
          "unknown source entry is rejected");

    memcpy(malformed, valid, sizeof(malformed));
    malformed[0] = 0;
    check(!melee_web_audio_residency_register(
              residency,
              &(MeleeWebAudioResidencyAsset){"/audio/us/short-header.ssm",
                                             malformed, sizeof(malformed), 43},
              error, sizeof(error)),
          "invalid header extent is rejected before descriptor reads");
    memcpy(malformed, valid, sizeof(malformed));
    word(malformed, 4, 64);
    check(!melee_web_audio_residency_register(
              residency,
              &(MeleeWebAudioResidencyAsset){"/audio/us/short-payload.ssm",
                                             malformed, valid_size, 44},
              error, sizeof(error)),
          "payload extent mismatch is rejected");
    memcpy(malformed, valid, sizeof(malformed));
    word(malformed, 32, 64);
    check(!melee_web_audio_residency_register(
              residency,
              &(MeleeWebAudioResidencyAsset){"/audio/us/bad-address.ssm",
                                             malformed, valid_size, 45},
              error, sizeof(error)),
          "out-of-range DSP address is rejected");

    if (argc > 1) {
        MeleeWebAudioResidency* local =
            melee_web_audio_residency_create(error, sizeof(error));
        check(local != NULL, "local SSM registry allocates");
        for (entry = 1; local != NULL && entry < argc; ++entry) {
            check_local_asset(local, argv[entry], 100 + entry, error,
                              sizeof(error));
        }
        check(local != NULL &&
                  melee_web_audio_residency_destroy(local, error, sizeof(error)),
              "local SSM registry destroys after bounded reads");
    }

    check(melee_web_audio_residency_destroy(residency, error, sizeof(error)),
          "registry destroys after source has no outstanding reads");
    if (failures != 0) {
        return 1;
    }
    puts("Audio SSM registry path, descriptor, and bounded transport checks passed");
    return 0;
}
