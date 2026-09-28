#include "gameplay_source_files.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned completions;
static int completion_request;
static int completion_cancelled;

static void read_complete(int request, int args, void* buffer, int cancelled)
{
    assert(args == 0);
    assert(buffer == NULL);
    ++completions;
    completion_request = request;
    completion_cancelled = cancelled;
}

int main(void)
{
    static const uint8_t bytes[] = {0x48, 0x53, 0x44, 0x00, 0x31, 0x32};
    uint8_t movie_bytes[64];
    for (size_t i = 0; i < sizeof(movie_bytes); ++i)
        movie_bytes[i] = (uint8_t)(i * 3 + 7);
    char name[] = "LbRf.dat";
    const MeleeWebSourceFileInput input[] = {
        {name, bytes, sizeof(bytes)},
        {"zero-length.bin", NULL, 0},
        {"movie.thp", movie_bytes, sizeof(movie_bytes)},
    };
    char error[128];
    size_t size = 0;

    assert(melee_web_source_files_external_set("movies/MvOpen.mth", 32u * 1024u * 1024u,
                                                error, sizeof(error)));
    assert(!melee_web_source_file_size("LbRf.dat", &size));
    MeleeWebSourceFileScope* scope = melee_web_source_files_begin(
        input, sizeof(input) / sizeof(input[0]), error, sizeof(error));
    assert(scope && error[0] == '\0');
    name[0] = 'X'; /* The exact lookup index owns its names. */

    assert(melee_web_source_file_size("LbRf.dat", &size));
    assert(size == sizeof(bytes));
    assert(melee_web_source_file_size("/LbRf.dat", &size));
    assert(size == sizeof(bytes));
    uint8_t copy[sizeof(bytes)] = {0};
    assert(melee_web_source_file_copy("/LbRf.dat", copy, &size));
    assert(size == sizeof(bytes) && memcmp(copy, bytes, sizeof(bytes)) == 0);
    assert(strcmp(melee_web_source_file_take_name(copy), "LbRf.dat") == 0);
    assert(melee_web_source_file_take_name(copy) == NULL);
    assert(melee_web_source_file_size("zero-length.bin", &size) && size == 0);
    assert(!melee_web_source_file_size("lbRf.dat", &size));
    assert(!melee_web_source_file_size("missing.dat", &size));
    assert(!melee_web_source_file_size("//LbRf.dat", &size));
    assert(!melee_web_source_file_size("/lbrf.dat", &size));
    assert(!melee_web_source_file_size("/missing/LbRf.dat", &size));
    assert(!melee_web_source_file_copy("LbRf.dat", NULL, &size));

    assert(!melee_web_source_files_begin(input, 3, error, sizeof(error)));
    assert(strstr(error, "already active") != NULL);

    const int movie_entry = melee_web_source_file_entry("/movie.thp");
    assert(movie_entry == MELEE_WEB_SOURCE_FILE_ENTRY_BASE + 2);
    assert(melee_web_source_file_entry_owned(movie_entry));
    assert(!melee_web_source_file_entry_owned(movie_entry + 8));
    const int streamed_entry = melee_web_source_file_entry("/movies/MvOpen.mth");
    assert(streamed_entry == MELEE_WEB_SOURCE_FILE_ENTRY_BASE + 3);
    assert(melee_web_source_file_entry_owned(streamed_entry));
    assert(melee_web_source_file_size("movies/MvOpen.mth", &size) &&
           size == 32u * 1024u * 1024u);
    assert(!melee_web_source_file_copy("movies/MvOpen.mth", copy, &size));
    assert(melee_web_source_files_active());
    _Alignas(32) uint8_t movie_output[64] = {0};
    assert(melee_web_source_file_region_register(movie_output,
                                                 sizeof(movie_output),
                                                 error, sizeof(error)));
    assert(!melee_web_source_file_region_register(movie_output + 32, 32,
                                                  error, sizeof(error)));
    int request = 0;
    assert(!melee_web_source_file_request(movie_entry, 0,
        (uintptr_t)(movie_output + 64), 32, 0x21, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(!melee_web_source_file_request(movie_entry, 1,
        (uintptr_t)movie_output, 32, 0x21, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(melee_web_source_file_request(movie_entry, 0,
        (uintptr_t)movie_output, 32, 0x21, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(request >= 0x50000000 && melee_web_source_file_drive_busy());
    assert(movie_output[0] == 0 && completions == 0);
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));
    assert(!melee_web_source_file_region_unregister(movie_output,
                                                     sizeof(movie_output),
                                                     error, sizeof(error)));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(!melee_web_source_file_drive_busy());
    assert(completions == 1 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(movie_output, movie_bytes, 32) == 0);

    _Alignas(32) uint8_t streamed_output[64] = {0};
    uint8_t streamed_bytes[32];
    for (size_t i = 0; i < sizeof(streamed_bytes); ++i)
        streamed_bytes[i] = (uint8_t)(0xa0 + i);
    assert(melee_web_source_file_region_register(streamed_output,
                                                 sizeof(streamed_output),
                                                 error, sizeof(error)));
    assert(!melee_web_source_file_request(streamed_entry, 0,
        (uintptr_t)streamed_output, MELEE_WEB_SOURCE_FILE_STREAM_MAX + 32u,
        0x21, 1, read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_request(streamed_entry, 32,
        (uintptr_t)streamed_output, sizeof(streamed_bytes), 0x21, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_drive_busy());
    assert(melee_web_source_file_supply(request, streamed_bytes,
        sizeof(streamed_bytes), error, sizeof(error)));
    assert(!melee_web_source_file_supply(request, streamed_bytes,
        sizeof(streamed_bytes), error, sizeof(error)));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(completions == 2 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(streamed_output, streamed_bytes, sizeof(streamed_bytes)) == 0);
    assert(melee_web_source_file_region_unregister(streamed_output,
        sizeof(streamed_output), error, sizeof(error)));

    assert(melee_web_source_file_request(movie_entry, 32,
        (uintptr_t)(movie_output + 32), 32, 0x21, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(melee_web_source_file_cancel(request, 0, NULL, NULL));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(completions == 3 && completion_request == request &&
           completion_cancelled);
    assert(movie_output[32] == 0);
    assert(melee_web_source_file_region_unregister(movie_output,
                                                    sizeof(movie_output),
                                                    error, sizeof(error)));
    assert(melee_web_source_files_end(scope, error, sizeof(error)));
    assert(melee_web_source_files_external_clear(error, sizeof(error)));
    assert(!melee_web_source_file_size("LbRf.dat", &size));
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));

    const MeleeWebSourceFileInput duplicate[] = {
        {"same.dat", bytes, sizeof(bytes)},
        {"same.dat", bytes, sizeof(bytes)},
    };
    assert(!melee_web_source_files_begin(duplicate, 2, error, sizeof(error)));
    assert(strstr(error, "Duplicate exact") != NULL);
    puts("source RuntimeFiles exact DVD reads, ownership, cancellation and lifecycle trace: passed");
    return 0;
}
