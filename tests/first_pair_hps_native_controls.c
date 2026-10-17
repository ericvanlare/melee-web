#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); \
        exit(86); \
    } \
} while (0)

static jmp_buf native_abort_context;
_Noreturn static void fixture_abort(void);
#define abort fixture_abort

/* Include the pinned implementation in this control TU so the fixture can
 * hold its private inner MeleeWebIo owner without adding a production hook. */
#include "gameplay_audio_stream.c"
#undef abort

_Noreturn static void fixture_abort(void)
{
    longjmp(native_abort_context, 1);
}

#define EXPECT_NATIVE_ABORT(statement) do { \
    if (setjmp(native_abort_context) == 0) { \
        statement; \
        CHECK(0 && "expected native source refusal"); \
    } \
} while (0)

static MeleeWebAudio* test_audio;
static int outer_callback_seen;
static int outer_callback_quiescent;
static int inner_callback_seen;
static int inner_callback_quiescent;
static unsigned source_stop_calls;
static unsigned lb_begin_calls;
static unsigned lb_end_calls;
static uint8_t hps_header_destinations[3][128];

EM_JS(void, install_host_guard, (), {
        const host = ({calls: [], failures: [], refusal: null});
        host.menuFirstPairNativeFileOperation = operation => {
            host.calls.push(operation);
            if (host.refusal === operation) {
                host.failures.push(operation);
                return false;
            }
            return true;
        };
        host.menuFirstPairNativeFileOperationFailure = detail => {
            host.failures.push(String(detail));
        };
        globalThis.window = host;

});

EM_JS(int, host_call_count, (), {return globalThis.window.calls.length;
});

EM_JS(int, host_failure_count, (), {return globalThis.window.failures.length;
});

EM_JS(int, host_last_call_is, (const char* operation), {
        const calls = globalThis.window.calls;
        return calls.length && calls[calls.length - 1] === UTF8ToString(operation) ? 1 : 0;

});

EM_JS(void, host_refuse, (const char* operation), {globalThis.window.refusal = UTF8ToString(operation);
});

static void outer_callback(int request, int args, void* buffer, int cancelled)
{
    (void)request;
    (void)args;
    CHECK(buffer != NULL);
    CHECK(!cancelled);
    CHECK(active != NULL);
    CHECK(active->pumping);
    CHECK(!active->requests);
    CHECK(melee_web_io_quiescent(active->io));
    outer_callback_quiescent = melee_web_audio_stream_quiescent_for(test_audio);
    outer_callback_seen = 1;
}

static void inner_callback(uint64_t request, MeleeWebIoResult result,
                           size_t transferred, void* user)
{
    (void)request;
    (void)user;
    CHECK(result == MELEE_WEB_IO_COMPLETE);
    CHECK(transferred == 32);
    CHECK(active != NULL);
    CHECK(!active->pumping);
    CHECK(!active->requests);
    CHECK(!melee_web_io_quiescent(active->io));
    inner_callback_quiescent = melee_web_audio_stream_quiescent_for(test_audio);
    inner_callback_seen = 1;
}

/* Minimal collaborators around the actual stream implementation. They only
 * supply an active audio identity and original source lifecycle callbacks. */
int melee_web_audio_is_active(MeleeWebAudio* audio) { return audio == test_audio; }
void melee_web_audio_stream_source_bind(u32 base, u32 count, void** headers)
{
    (void)base;
    (void)count;
    for (unsigned i = 0; i < 3; ++i) headers[i] = hps_header_destinations[i];
}
int melee_web_audio_stream_source_loading(void) { return 0; }
void melee_web_audio_stream_source_stop(void) { ++source_stop_calls; }
void melee_web_audio_stream_lb_begin(void) { ++lb_begin_calls; }
void melee_web_audio_stream_lb_end(void) { ++lb_end_calls; }

int melee_web_audio_bank_transport_active(void) { return 0; }
int melee_web_audio_bank_transport_busy(void) { return 0; }
int melee_web_audio_bank_transport_path(const char* path) { (void)path; return -1; }
int melee_web_audio_bank_transport_file(int file) { (void)file; return 0; }
int melee_web_audio_bank_transport_request(int file, uintptr_t src, uintptr_t dest,
    size_t size, int type, int priority, HSD_DevComCallback callback, void* args)
{
    (void)file; (void)src; (void)dest; (void)size; (void)type;
    (void)priority; (void)callback; (void)args; return 0;
}
int melee_web_audio_bank_transport_cancel(int request, u32 flags,
    HSD_DevComCallback callback, void* args)
{
    (void)request; (void)flags; (void)callback; (void)args; return 0;
}

int melee_web_source_files_active(void) { return 0; }
int melee_web_source_file_drive_busy(void) { return 0; }
int melee_web_source_file_entry(const char* path) { (void)path; return -1; }
int melee_web_source_file_entry_owned(int entry) { (void)entry; return 0; }
int melee_web_source_file_request(int entry, uintptr_t offset,
    uintptr_t destination, size_t size, int type, int priority,
    MeleeWebSourceFileCallback callback, void* args, int* request,
    char* error, size_t error_size)
{
    (void)entry; (void)offset; (void)destination; (void)size; (void)type;
    (void)priority; (void)callback; (void)args; (void)request;
    (void)error; (void)error_size; return 0;
}
int melee_web_source_file_cancel(int request, uint32_t flags,
    MeleeWebSourceFileCallback callback, void* args)
{
    (void)request; (void)flags; (void)callback; (void)args; return 0;
}

int main(int argc, char** argv)
{
    static union { max_align_t align; unsigned char bytes[sizeof(void*)]; } audio_storage;
    static union { max_align_t align; unsigned char bytes[sizeof(void*)]; } other_storage;
    static const uint8_t file_bytes[128] = {0x48, 0x50, 0x53};
    static const uint8_t block_payload[32] = {0};
    MeleeWebAudioStreamBlock block = {0};
    MeleeWebAudioStreamInput input = {0};
    char error[256] = {0};
    uint32_t completed = 0;
    uint64_t inner_request = 0;

    CHECK(argc >= 1);
    CHECK(argv != NULL);
    CHECK(argv[0] != NULL);
    if (argc == 2 && (strcmp(argv[1], "--fail-check") == 0 ||
                      strcmp(argv[1], "--force-check-failure") == 0)) {
        fputs("argv transport control received --fail-check\n", stderr);
        CHECK(0 && "intentional CHECK failure sentinel");
    }
    CHECK(argc == 1);

    install_host_guard();
    CHECK(melee_web_first_pair_file_guard_enabled == 0);
    CHECK(melee_web_first_pair_file_operation("native.hps.begin"));
    CHECK(host_call_count() == 0);

    test_audio = (MeleeWebAudio*)audio_storage.bytes;
    MeleeWebAudio* wrong_audio = (MeleeWebAudio*)other_storage.bytes;
    CHECK(wrong_audio != test_audio);

    block.offset = 0;
    block.size = 32;
    block.end = 1;
    block.payload[0] = block_payload;
    input.path = "/audio/quiescence-control.hps";
    input.bytes = file_bytes;
    input.size = sizeof(file_bytes);
    input.rate = 32000;
    input.channels = 1;
    input.blocks = &block;
    input.count = 1;

    CHECK(melee_web_first_pair_file_guard_arm());
    host_refuse("native.hps.begin");
    MeleeWebAudioStream* stream = melee_web_audio_stream_begin(
        test_audio, &input, error, sizeof(error));
    CHECK(stream == NULL);
    CHECK(active == NULL);
    CHECK(host_last_call_is("native.hps.begin"));
    CHECK(host_failure_count() == 1);
    CHECK(source_stop_calls == 0 && lb_begin_calls == 0 && lb_end_calls == 0);

    host_refuse("");
    stream = melee_web_audio_stream_begin(test_audio, &input, error, sizeof(error));
    CHECK(stream != NULL);
    CHECK(active == stream);
    CHECK(lb_begin_calls == 1);
    CHECK(melee_web_audio_stream_quiescent_for(test_audio));
    CHECK(!melee_web_audio_stream_quiescent_for(wrong_audio));

    const uint32_t id_before_refused_request = active->next_id;
    CHECK(active->requests == NULL);
    host_refuse("native.hps.request");
    EXPECT_NATIVE_ABORT((void)HSD_DevComRequest(1, 0, 0, 128,
        0x22, 0, outer_callback, NULL));
    CHECK(active->next_id == id_before_refused_request);
    CHECK(active->requests == NULL);
    CHECK(host_last_call_is("native.hps.request"));
    CHECK(host_failure_count() == 2);
    host_refuse("");

    /* HSD_DevComRequest is the real HPS queue path. While the outer queue is
     * nonempty, inner I/O is still idle, isolating the queued-request gate. */
    CHECK(HSD_DevComRequest(1, 0, 0, 128,
        0x22, 0, outer_callback, NULL) >= 0);
    CHECK(active->requests != NULL);
    CHECK(!active->pumping);
    CHECK(melee_web_io_quiescent(active->io));
    CHECK(!melee_web_audio_stream_quiescent_for(test_audio));

    /* File selection used to pump first. A refusal must leave the pending
     * HPS request and owner state untouched. */
    Request* const selected_request = active->requests;
    const unsigned selected_file = active->selected_file;
    const uint32_t completed_before_selection = active->completed;
    host_refuse("native.hps.select-file");
    EXPECT_NATIVE_ABORT((void)DVDConvertPathToEntrynum(input.path));
    CHECK(active->requests == selected_request);
    CHECK(!active->pumping);
    CHECK(melee_web_io_pending(active->io) == 0);
    CHECK(active->selected_file == selected_file);
    CHECK(active->completed == completed_before_selection);
    CHECK(host_last_call_is("native.hps.select-file"));
    CHECK(host_failure_count() == 3);
    host_refuse("");

    const int cancellation_id = active->requests->id;
    const int cancellation_flag = active->requests->cancelled;
    host_refuse("native.hps.cancel");
    EXPECT_NATIVE_ABORT((void)HSD_DevComCancelEx(cancellation_id, 3,
        outer_callback, NULL));
    CHECK(active->requests == selected_request);
    CHECK(active->requests->cancelled == cancellation_flag);
    CHECK(host_last_call_is("native.hps.cancel"));
    CHECK(host_failure_count() == 4);
    host_refuse("");

    host_refuse("native.hps.pump");
    CHECK(!melee_web_audio_stream_pump_for(test_audio, error, sizeof(error)));
    CHECK(active->requests == selected_request);
    CHECK(!active->pumping);
    CHECK(melee_web_io_pending(active->io) == 0);
    CHECK(host_last_call_is("native.hps.pump"));
    CHECK(host_failure_count() == 5);
    host_refuse("");

    /* The actual HPS pump removes the queued request, completes its real inner
     * I/O, then calls the supplied callback with only the outer pump active. */
    CHECK(melee_web_audio_stream_pump_for(test_audio, error, sizeof(error)));
    CHECK(outer_callback_seen);
    CHECK(!outer_callback_quiescent);
    CHECK(melee_web_audio_stream_quiescent_for(test_audio));

    /* Directly queue on the HPS owner's actual private inner I/O. The outer
     * HPS queue/pump remain idle, isolating both inner queue and callback-time
     * pumping refusal through the production HPS predicate. */
    MeleeWebIoEndpoint source = {MELEE_WEB_IO_FILE, active->files[0].handle, 0};
    MeleeWebIoEndpoint destination = {MELEE_WEB_IO_AUXILIARY, active->aux, 0};
    host_refuse("native.hps.io-submit");
    CHECK(!melee_web_io_submit(active->io, source, destination, 32,
        inner_callback, NULL, &inner_request, error, sizeof(error)));
    CHECK(melee_web_io_pending(active->io) == 0);
    CHECK(host_last_call_is("native.hps.io-submit"));
    CHECK(host_failure_count() == 6);
    host_refuse("");
    CHECK(melee_web_io_submit(active->io, source, destination, 32,
        inner_callback, NULL, &inner_request, error, sizeof(error)));
    CHECK(!active->pumping);
    CHECK(!active->requests);
    CHECK(melee_web_io_pending(active->io) == 1);
    CHECK(!melee_web_audio_stream_quiescent_for(test_audio));
    completed = 99;
    host_refuse("native.hps.io-pump");
    CHECK(!melee_web_io_pump(active->io, 1, &completed, error, sizeof(error)));
    CHECK(completed == 99);
    CHECK(melee_web_io_pending(active->io) == 1);
    /* Opaque inner I/O remains queued; subsequent successful actual pump
     * below proves refusal did not leave its pumping state latched. */
    CHECK(!melee_web_io_quiescent(active->io));
    CHECK(host_last_call_is("native.hps.io-pump"));
    CHECK(host_failure_count() == 7);
    host_refuse("");
    CHECK(melee_web_io_pump(active->io, 1, &completed, error, sizeof(error)));
    CHECK(completed == 1);
    CHECK(inner_callback_seen);
    CHECK(!inner_callback_quiescent);
    CHECK(melee_web_audio_stream_quiescent_for(test_audio));

    host_refuse("native.hps.end");
    CHECK(!melee_web_audio_stream_end(stream, error, sizeof(error)));
    CHECK(active == stream);
    CHECK(source_stop_calls == 0 && lb_end_calls == 0);
    CHECK(host_last_call_is("native.hps.end"));
    CHECK(host_failure_count() == 8);
    host_refuse("");
    CHECK(melee_web_audio_stream_end(stream, error, sizeof(error)));
    CHECK(active == NULL);
    CHECK(source_stop_calls == 1 && lb_end_calls == 1);
    puts("Actual HPS guard refused pre-allocation request, pre-pump selection, cancellation, queued completion, inner submit/pump and owner release; exact-owner idle and ordinary-default-off controls passed");
    return 0;
}
