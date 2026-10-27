#include "gameplay_source_files.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

/* Exercise the production EM_JS bridge through the C pump.  The real browser
 * supplies these window functions; this deterministic Node fixture supplies
 * the same staged-range contract and leaves the C owner responsible for the
 * destination copy and temporary allocation. */
EM_JS(void, install_source_stream_bridge, (), {
    globalThis.window = globalThis;
    const pending = new Map();
    window.menuStartSourceRead = (request, name, offset, size) => {
        if (name !== 'movies/MvOpen.mth' || offset !== 64 || size !== 32)
            return false;
        pending.set(request, {size, ready: true});
        return true;
    };
    window.menuSourceReadStatus = request => {
        const item = pending.get(request);
        return item ? (item.ready ? 1 : 0) : -1;
    };
    window.menuSourceReadTake = request => {
        const item = pending.get(request);
        if (!item || !item.ready) return 0;
        const pointer = Module._malloc(item.size);
        for (let index = 0; index < item.size; ++index)
            HEAPU8[pointer + index] = (0xd0 + index) & 0xff;
        pending.delete(request);
        return pointer;
    };
    window.menuSourceReadDiscard = request => { pending.delete(request); };
});
#endif

#if defined(__EMSCRIPTEN__)
enum { bridge_completion_count = 1 };
#else
enum { bridge_completion_count = 0 };
#endif

static unsigned completions;
static int completion_request;
static int completion_cancelled;
static const void* take_name_from_callback;
static const char* callback_taken_name;

static void read_complete(int request, int args, void* buffer, int cancelled)
{
    assert(args == 0);
    assert(buffer == NULL);
    if (take_name_from_callback)
        callback_taken_name =
            melee_web_source_file_take_name(take_name_from_callback);
    ++completions;
    completion_request = request;
    completion_cancelled = cancelled;
}

int main(void)
{
    static const uint8_t bytes[] = {0x48, 0x53, 0x44, 0x00, 0x31, 0x32};
    static uint8_t rounded_bytes[35];
    static uint8_t rounded_tail[29];
    uint8_t movie_bytes[64];
    uint8_t small_external_bytes[64];
    for (size_t i = 0; i < sizeof(rounded_bytes); ++i)
        rounded_bytes[i] = (uint8_t)(0x20 + i);
    for (size_t i = 0; i < sizeof(rounded_tail); ++i)
        rounded_tail[i] = (uint8_t)(0xa0 + i);
    for (size_t i = 0; i < sizeof(movie_bytes); ++i)
        movie_bytes[i] = (uint8_t)(i * 3 + 7);
    for (size_t i = 0; i < sizeof(small_external_bytes); ++i)
        small_external_bytes[i] = (uint8_t)(0x60 + i);
    char name[] = "LbRf.dat";
    const MeleeWebSourceFileInput input[] = {
        {name, bytes, sizeof(bytes), NULL, 0},
        {"zero-length.bin", NULL, 0, NULL, 0},
        {"movie.thp", movie_bytes, sizeof(movie_bytes), NULL, 0},
    };
    char error[128];
    size_t size = 0;

    assert(melee_web_source_files_external_set("movies/MvOpen.mth", 32u * 1024u * 1024u,
                                                error, sizeof(error)));
    assert(melee_web_source_files_external_set("movies/Small.dat",
        sizeof(small_external_bytes), error, sizeof(error)));
    assert(!melee_web_source_file_size("LbRf.dat", &size));
    MeleeWebSourceFileScope* scope = melee_web_source_files_begin(
        input, sizeof(input) / sizeof(input[0]), error, sizeof(error));
    assert(scope && error[0] == '\0');
    assert(melee_web_source_files_current_scope() == scope);
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
    assert(streamed_entry >= MELEE_WEB_SOURCE_FILE_ENTRY_BASE + 3);
    assert(melee_web_source_file_entry_owned(streamed_entry));
    const int small_external_entry =
        melee_web_source_file_entry("/movies/Small.dat");
    assert(small_external_entry >= MELEE_WEB_SOURCE_FILE_ENTRY_BASE + 3);
    assert(small_external_entry != streamed_entry &&
           melee_web_source_file_entry_owned(small_external_entry));
    assert(melee_web_source_file_size("movies/MvOpen.mth", &size) &&
           size == 32u * 1024u * 1024u);
    assert(!melee_web_source_file_copy("movies/MvOpen.mth", copy, &size));
    assert(melee_web_source_files_active());
    _Alignas(32) uint8_t movie_output[64] = {0};
    assert(melee_web_source_file_region_register(movie_output,
                                                 sizeof(movie_output),
                                                 error, sizeof(error)));
    /* A live registered destination is not the exact RuntimeFiles scope. */
    assert(!melee_web_source_files_quiescent_for(
        (MeleeWebSourceFileScope*)(void*)movie_output,error,sizeof(error)));
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
    assert(!melee_web_source_files_quiescent_for(scope,error,sizeof(error)));
    assert(!melee_web_source_file_region_unregister(movie_output,
                                                     sizeof(movie_output),
                                                     error, sizeof(error)));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(!melee_web_source_file_drive_busy());
    assert(completions == 1 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(movie_output, movie_bytes, 32) == 0);
    assert(melee_web_source_file_take_name(movie_output) == NULL);

    /* lbFile_800164A4 selects 0x23 when its destination is a Wasm pointer.
     * Keep the registered destination exact; the service must not turn that
     * pointer-kind distinction into permission to write arbitrary memory. */
    int refused_request = 0x12345;
    assert(!melee_web_source_file_request(movie_entry, 32,
        (uintptr_t)(movie_output + sizeof(movie_output)), 32, 0x23, 1,
        read_complete, NULL, &refused_request, error, sizeof(error)));
    assert(refused_request == 0x12345);
    assert(!melee_web_source_file_request(movie_entry, 32,
        (uintptr_t)(movie_output + 32), 32, 0x22, 1,
        read_complete, NULL, &refused_request, error, sizeof(error)));
    assert(refused_request == 0x12345);
    assert(!melee_web_source_file_request(movie_entry + 8, 32,
        (uintptr_t)(movie_output + 32), 32, 0x23, 1,
        read_complete, NULL, &refused_request, error, sizeof(error)));
    assert(refused_request == 0x12345);
    /* The six-byte LbRf fixture models a logical file whose DVD request is
     * rounded to 32 bytes by lbFile_800164A4. Until its exact disc-tail bytes
     * are represented by the source entry, refuse the padded transfer rather
     * than reading beyond the RuntimeFiles payload or inventing a tail. */
    const int short_entry = melee_web_source_file_entry("/LbRf.dat");
    assert(short_entry >= MELEE_WEB_SOURCE_FILE_ENTRY_BASE &&
           melee_web_source_file_entry_owned(short_entry));
    uint8_t preserved_short[32];
    memcpy(preserved_short, movie_output, sizeof(preserved_short));
    refused_request = 0x12345;
    assert(!melee_web_source_file_request(short_entry, 0,
        (uintptr_t)movie_output, 32, 0x23, 1, read_complete, NULL,
        &refused_request, error, sizeof(error)));
    assert(refused_request == 0x12345 &&
           memcmp(preserved_short, movie_output, sizeof(preserved_short)) == 0 &&
           completions == 1 && !melee_web_source_file_drive_busy());
    assert(completions == 1); /* Requests never complete inline. */
    assert(melee_web_source_file_request(movie_entry, 32,
        (uintptr_t)(movie_output + 32), 32, 0x23, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(completions == 1 && melee_web_source_file_drive_busy());
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));
    assert(!melee_web_source_file_region_unregister(movie_output,
        sizeof(movie_output), error, sizeof(error)));
    assert(!melee_web_source_files_pump_for(
        (MeleeWebSourceFileScope*)(uintptr_t)1, error, sizeof(error)));
    assert(completions == 1 && melee_web_source_file_drive_busy());
    assert(!melee_web_source_files_quiescent_for(scope,error,sizeof(error)));
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    assert(completions == 2 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(movie_output + 32, movie_bytes + 32, 32) == 0);
    assert(melee_web_source_file_take_name(movie_output + 32) == NULL);
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));
    assert(melee_web_source_files_quiescent_for(scope,error,sizeof(error)));

    /* A complete async transfer publishes the same exact source-name receipt
     * as the synchronous copy path, before invoking its callback. */
    assert(melee_web_source_file_request(movie_entry, 0,
        (uintptr_t)movie_output, sizeof(movie_output), 0x23, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    take_name_from_callback = movie_output;
    callback_taken_name = NULL;
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    take_name_from_callback = NULL;
    assert(completions == 3 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(movie_output, movie_bytes, sizeof(movie_output)) == 0);
    assert(callback_taken_name &&
           strcmp(callback_taken_name, "movie.thp") == 0);
    assert(melee_web_source_file_take_name(movie_output) == NULL);

    /* Accepted writes invalidate any older sync receipt for overlapping
     * bytes. A partial copy never reissues that receipt. */
    assert(melee_web_source_file_copy("LbRf.dat", movie_output, &size));
    assert(size == sizeof(bytes));
    assert(melee_web_source_file_request(movie_entry, 32,
        (uintptr_t)movie_output, 32, 0x23, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(melee_web_source_file_take_name(movie_output) == NULL);
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    assert(completions == 4 && completion_request == request &&
           !completion_cancelled);
    assert(melee_web_source_file_take_name(movie_output) == NULL);

    /* Cancellation clears a preceding sync receipt and does not manufacture
     * one for the incomplete full-file request. */
    assert(melee_web_source_file_copy("LbRf.dat", movie_output, &size));
    assert(size == sizeof(bytes));
    assert(melee_web_source_file_request(movie_entry, 0,
        (uintptr_t)movie_output, sizeof(movie_output), 0x23, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_take_name(movie_output) == NULL);
    assert(melee_web_source_file_cancel(request, 0, NULL, NULL));
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    assert(completions == 5 && completion_request == request &&
           completion_cancelled);
    assert(melee_web_source_file_take_name(movie_output) == NULL);

    /* A successful complete external transfer records the exact file only
     * after supplied bytes are present, and exposes the receipt before the
     * callback. */
    _Alignas(32) uint8_t small_external_output[64] = {0};
    assert(melee_web_source_file_region_register(small_external_output,
        sizeof(small_external_output), error, sizeof(error)));
    assert(melee_web_source_file_request(small_external_entry, 0,
        (uintptr_t)small_external_output, sizeof(small_external_output),
        0x23, 1, read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_supply(request, small_external_bytes,
        sizeof(small_external_bytes), error, sizeof(error)));
    take_name_from_callback = small_external_output;
    callback_taken_name = NULL;
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    take_name_from_callback = NULL;
    assert(completions == 6 && completion_request == request &&
           !completion_cancelled);
    assert(memcmp(small_external_output, small_external_bytes,
                  sizeof(small_external_output)) == 0);
    assert(callback_taken_name &&
           strcmp(callback_taken_name, "movies/Small.dat") == 0);
    assert(melee_web_source_file_take_name(small_external_output) == NULL);
    assert(melee_web_source_file_region_unregister(small_external_output,
        sizeof(small_external_output), error, sizeof(error)));

#if defined(__EMSCRIPTEN__)
    install_source_stream_bridge();
    _Alignas(32) uint8_t bridge_output[64] = {0};
    assert(melee_web_source_file_region_register(bridge_output,
                                                 sizeof(bridge_output),
                                                 error, sizeof(error)));
    assert(melee_web_source_file_request(streamed_entry, 64,
        (uintptr_t)bridge_output, 32, 0x21, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(completions == 7 && completion_request == request &&
           !completion_cancelled);
    for (size_t i = 0; i < 32; ++i)
        assert(bridge_output[i] == (uint8_t)(0xd0 + i));
    assert(melee_web_source_file_region_unregister(bridge_output,
        sizeof(bridge_output), error, sizeof(error)));
#endif

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
    assert(!melee_web_source_file_supply(request, streamed_bytes,
        sizeof(streamed_bytes) - 1, error, sizeof(error)));
    assert(streamed_output[0] == 0);
    assert(melee_web_source_file_cancel(request, 0, NULL, NULL));
    assert(melee_web_source_files_pump_for(scope,error,sizeof(error)));
    assert(completions == 7 + bridge_completion_count &&
           completion_request == request && completion_cancelled);
    assert(melee_web_source_file_take_name(streamed_output) == NULL);

    assert(melee_web_source_file_request(streamed_entry, 32,
        (uintptr_t)streamed_output, sizeof(streamed_bytes), 0x21, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_supply(request, streamed_bytes,
        sizeof(streamed_bytes), error, sizeof(error)));
    assert(!melee_web_source_file_supply(request, streamed_bytes,
        sizeof(streamed_bytes), error, sizeof(error)));
    assert(melee_web_source_files_pump(error, sizeof(error)));
    assert(completions == 8 + bridge_completion_count &&
           completion_request == request &&
           !completion_cancelled);
    assert(memcmp(streamed_output, streamed_bytes, sizeof(streamed_bytes)) == 0);
    assert(melee_web_source_file_take_name(streamed_output) == NULL);
    assert(melee_web_source_file_region_unregister(streamed_output,
        sizeof(streamed_output), error, sizeof(error)));
    assert(melee_web_source_file_region_unregister(movie_output,
                                                    sizeof(movie_output),
                                                    error, sizeof(error)));
    assert(melee_web_source_files_end(scope, error, sizeof(error)));
    assert(melee_web_source_files_current_scope() == NULL);
    assert(melee_web_source_files_external_clear(error, sizeof(error)));
    assert(!melee_web_source_file_size("LbRf.dat", &size));
    assert(!melee_web_source_files_end(scope, error, sizeof(error)));

    const MeleeWebSourceFileInput duplicate[] = {
        {"same.dat", bytes, sizeof(bytes), NULL, 0},
        {"same.dat", bytes, sizeof(bytes), NULL, 0},
    };
    assert(!melee_web_source_files_begin(duplicate, 2, error, sizeof(error)));
    assert(strstr(error, "Duplicate exact") != NULL);

    /* Tail metadata must describe exactly the bytes between the logical end
     * and the next 32-byte transfer boundary. It is authenticated data, not
     * padding that the service may synthesize. */
    const MeleeWebSourceFileInput wrong_tail_length[] = {
        {"rounded.dat", rounded_bytes, sizeof(rounded_bytes),
         rounded_tail, sizeof(rounded_tail) - 1},
    };
    assert(!melee_web_source_files_begin(wrong_tail_length, 1, error,
                                         sizeof(error)));
    assert(strstr(error, "transfer tail") != NULL);
    const MeleeWebSourceFileInput missing_tail_pointer[] = {
        {"rounded.dat", rounded_bytes, sizeof(rounded_bytes),
         NULL, sizeof(rounded_tail)},
    };
    assert(!melee_web_source_files_begin(missing_tail_pointer, 1, error,
                                         sizeof(error)));
    const MeleeWebSourceFileInput overflowing_tail_pointer[] = {
        {"rounded.dat", rounded_bytes, sizeof(rounded_bytes),
         (const uint8_t*)(uintptr_t)(UINTPTR_MAX - sizeof(rounded_tail) + 1),
         sizeof(rounded_tail)},
    };
    assert(!melee_web_source_files_begin(overflowing_tail_pointer, 1, error,
                                         sizeof(error)));
    const MeleeWebSourceFileInput overflowing_logical_extent[] = {
        {"too-large.dat", (const uint8_t*)(uintptr_t)1,
         SIZE_MAX - 30u, rounded_tail, 1},
    };
    assert(!melee_web_source_files_begin(overflowing_logical_extent, 1,
                                         error, sizeof(error)));

    const MeleeWebSourceFileInput rounded_input[] = {
        {"rounded.dat", rounded_bytes, sizeof(rounded_bytes),
         rounded_tail, sizeof(rounded_tail)},
    };
    MeleeWebSourceFileScope* rounded_scope = melee_web_source_files_begin(
        rounded_input, 1, error, sizeof(error));
    assert(rounded_scope && error[0] == '\0');
    assert(melee_web_source_file_size("rounded.dat", &size) &&
           size == sizeof(rounded_bytes));
    _Alignas(32) uint8_t rounded_output[64];
    memset(rounded_output, 0x5a, sizeof(rounded_output));
    assert(melee_web_source_file_region_register(rounded_output,
        sizeof(rounded_output), error, sizeof(error)));
    assert(melee_web_source_file_copy("rounded.dat", rounded_output, &size));
    assert(size == sizeof(rounded_bytes));
    assert(memcmp(rounded_output, rounded_bytes, sizeof(rounded_bytes)) == 0);
    assert(rounded_output[sizeof(rounded_bytes)] == 0x5a);
    assert(strcmp(melee_web_source_file_take_name(rounded_output),
                  "rounded.dat") == 0);

    const int rounded_entry = melee_web_source_file_entry("rounded.dat");
    assert(rounded_entry >= MELEE_WEB_SOURCE_FILE_ENTRY_BASE &&
           melee_web_source_file_entry_owned(rounded_entry));
    const unsigned completions_before_rounded = completions;
    uint8_t preserved_rounded[sizeof(rounded_output)];
    memset(rounded_output, 0x6b, sizeof(rounded_output));
    memcpy(preserved_rounded, rounded_output, sizeof(preserved_rounded));
    int refused_rounded = 0x54321;
    assert(!melee_web_source_file_request(rounded_entry, 64,
        (uintptr_t)rounded_output, 32, 0x23, 1, read_complete, NULL,
        &refused_rounded, error, sizeof(error)));
    assert(refused_rounded == 0x54321 &&
           memcmp(preserved_rounded, rounded_output,
                  sizeof(preserved_rounded)) == 0 &&
           completions == completions_before_rounded);
    assert(melee_web_source_file_request(rounded_entry, 0,
        (uintptr_t)rounded_output, 32, 0x23, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    take_name_from_callback = rounded_output;
    callback_taken_name = NULL;
    assert(melee_web_source_files_pump_for(rounded_scope, error,
                                           sizeof(error)));
    take_name_from_callback = NULL;
    assert(completions == completions_before_rounded + 1 &&
           completion_request == request && !completion_cancelled &&
           callback_taken_name == NULL);
    assert(melee_web_source_file_take_name(rounded_output) == NULL);

    assert(melee_web_source_file_request(rounded_entry, 32,
        (uintptr_t)(rounded_output + 32), 32, 0x23, 1, read_complete, NULL,
        &request, error, sizeof(error)));
    take_name_from_callback = rounded_output + 32;
    callback_taken_name = NULL;
    assert(melee_web_source_files_pump_for(rounded_scope, error,
                                           sizeof(error)));
    take_name_from_callback = NULL;
    assert(completions == completions_before_rounded + 2 &&
           completion_request == request && !completion_cancelled &&
           callback_taken_name == NULL);
    assert(memcmp(rounded_output, rounded_bytes, sizeof(rounded_bytes)) == 0);
    assert(memcmp(rounded_output + sizeof(rounded_bytes), rounded_tail,
                  sizeof(rounded_tail)) == 0);
    assert(melee_web_source_file_take_name(rounded_output + 32) == NULL);

    assert(melee_web_source_file_copy("rounded.dat", rounded_output, &size));
    assert(melee_web_source_file_request(rounded_entry, 0,
        (uintptr_t)rounded_output, sizeof(rounded_output), 0x23, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    take_name_from_callback = rounded_output;
    callback_taken_name = NULL;
    assert(melee_web_source_files_pump_for(rounded_scope, error,
                                           sizeof(error)));
    take_name_from_callback = NULL;
    assert(completions == completions_before_rounded + 3 &&
           completion_request == request && !completion_cancelled);
    assert(memcmp(rounded_output, rounded_bytes, sizeof(rounded_bytes)) == 0);
    assert(memcmp(rounded_output + sizeof(rounded_bytes), rounded_tail,
                  sizeof(rounded_tail)) == 0);
    assert(callback_taken_name &&
           strcmp(callback_taken_name, "rounded.dat") == 0);
    assert(melee_web_source_file_take_name(rounded_output) == NULL);

    assert(melee_web_source_file_copy("rounded.dat", rounded_output, &size));
    assert(melee_web_source_file_request(rounded_entry, 0,
        (uintptr_t)rounded_output, sizeof(rounded_output), 0x23, 1,
        read_complete, NULL, &request, error, sizeof(error)));
    assert(melee_web_source_file_cancel(request, 0, NULL, NULL));
    assert(melee_web_source_files_pump_for(rounded_scope, error,
                                           sizeof(error)));
    assert(completions == completions_before_rounded + 4 &&
           completion_request == request && completion_cancelled);
    assert(melee_web_source_file_take_name(rounded_output) == NULL);
    assert(melee_web_source_file_region_unregister(rounded_output,
        sizeof(rounded_output), error, sizeof(error)));
    assert(melee_web_source_files_end(rounded_scope, error, sizeof(error)));
    puts("source RuntimeFiles exact DVD reads, ownership, cancellation and lifecycle trace: passed");
    return 0;
}
