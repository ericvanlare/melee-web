#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
            __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static jmp_buf unavailable_context;

/* Include the actual SSM transport so this fixture can hold its private queue,
 * audio identity and callback-in-progress flag without production hooks. */
#include "gameplay_audio_bank_transport.c"

_Noreturn void melee_web_platform_unavailable(const char* operation)
{
    (void)operation;
    longjmp(unavailable_context, 1);
}

#define EXPECT_UNAVAILABLE(statement) do { \
    if (setjmp(unavailable_context) == 0) { \
        statement; \
        CHECK(0 && "expected guarded SSM refusal"); \
    } \
} while (0)

static MeleeWebAudio* callback_audio;
static int callback_seen;
static int callback_quiescent;

EM_JS(int, host_call_count, (), {return globalThis.window.calls.length;});

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

EM_JS(void, host_refuse, (const char* operation), {globalThis.window.refusal = UTF8ToString(operation);
});

EM_JS(int, host_failure_count, (), {return globalThis.window.failures.length;
});

EM_JS(int, host_last_call_is, (const char* operation), {
        const calls = globalThis.window.calls;
        return calls.length && calls[calls.length - 1] === UTF8ToString(operation) ? 1 : 0;

});

int melee_web_audio_is_active(MeleeWebAudio* audio)
{
    return audio == callback_audio;
}

int melee_web_audio_stream_owned(MeleeWebAudio* audio)
{
    (void)audio;
    return 0;
}

AXVPB* __AXGetStackHead(u32 priority)
{
    (void)priority;
    return NULL;
}

int melee_web_audio_residency_info(const MeleeWebAudioResidency* registry,
    int entry_number, MeleeWebAudioResidencyInfo* info,
    char* error, size_t error_size)
{
    (void)registry;
    (void)entry_number;
    (void)info;
    (void)error;
    (void)error_size;
    return 0;
}

int melee_web_audio_lb_scope_begin(void) { return 1; }
int melee_web_audio_lb_scope_end(void) { return 1; }
void melee_web_audio_source_banks_begin(u32 base, u32 size)
{
    (void)base;
    (void)size;
}

static void ssm_callback(int request, int args, void* buffer, int cancelled)
{
    (void)request;
    (void)args;
    (void)buffer;
    CHECK(!cancelled);
    callback_quiescent = melee_web_audio_bank_transport_quiescent_for(
        callback_audio);
    callback_seen = 1;
}

int main(void)
{
    static union { max_align_t align; unsigned char bytes[sizeof(void*)]; } audio_storage;
    static union { max_align_t align; unsigned char bytes[sizeof(void*)]; } wrong_storage;
    static union { max_align_t align; unsigned char bytes[sizeof(void*)]; } registry_storage;
    static unsigned char relocation_bytes[64];
    char error[256] = {0};

    callback_audio = (MeleeWebAudio*)audio_storage.bytes;
    MeleeWebAudio* const wrong_audio = (MeleeWebAudio*)wrong_storage.bytes;
    MeleeWebAudioResidency* const registry =
        (MeleeWebAudioResidency*)registry_storage.bytes;
    CHECK(callback_audio != wrong_audio);

    install_host_guard();
    CHECK(melee_web_first_pair_file_guard_enabled == 0);
    CHECK(melee_web_first_pair_file_operation("native.ssm.begin"));
    CHECK(host_call_count() == 0);
    CHECK(melee_web_first_pair_file_guard_arm());

    CHECK(melee_web_audio_bank_transport_begin(callback_audio, registry,
        "fixture.sem", error, sizeof(error)));
    CHECK(melee_web_audio_bank_transport_quiescent_for(callback_audio));
    CHECK(!melee_web_audio_bank_transport_quiescent_for(NULL));
    CHECK(!melee_web_audio_bank_transport_quiescent_for(wrong_audio));
    melee_web_audio_bank_transport_configure(sizeof(relocation_bytes));
    CHECK(state.configured);
    CHECK(state.audio == callback_audio);

    /* Exercise a resident SSM operation that queues and drains to an idle
     * endpoint. Its next invocation inside a leg must still be stopped at the
     * actual HSD_DevComRequest owner before ID/queue mutation. */
    state.mapping_count = 1;
    state.mappings[0] = (Mapping){0, BANK_BASE, 32};
    for (unsigned i = 0; i < 32; ++i) state.auxiliary[i] = (unsigned char)(i + 1);
    const uint32_t next_before = state.next_id;
    host_refuse("native.ssm.request");
    EXPECT_UNAVAILABLE((void)melee_web_audio_bank_transport_request(
        0, BANK_BASE, BANK_BASE + 32, 32, 0x1B, 1, ssm_callback, NULL));
    CHECK(state.next_id == next_before);
    CHECK(state.pending == 0 && state.requests == NULL);
    CHECK(melee_web_audio_bank_transport_quiescent_for(callback_audio));
    CHECK(host_last_call_is("native.ssm.request"));
    CHECK(host_failure_count() == 1);

    host_refuse("");
    const int request = melee_web_audio_bank_transport_request(
        0, BANK_BASE, BANK_BASE + 32, 32, 0x1B, 1, ssm_callback, NULL);
    CHECK(request == (int)(REQUEST_BASE + 1));
    CHECK(state.pending == 1 && state.requests != NULL);
    CHECK(!melee_web_audio_bank_transport_quiescent_for(callback_audio));
    CHECK(!melee_web_audio_bank_transport_quiescent_for(wrong_audio));
    melee_web_audio_bank_transport_pump();
    CHECK(callback_seen);
    CHECK(!callback_quiescent);
    CHECK(state.pending == 0 && state.requests == NULL);
    CHECK(!state.pumping);
    CHECK(state.mappings[0].base == BANK_BASE + 32);
    CHECK(melee_web_audio_bank_transport_quiescent_for(callback_audio));

    /* Repeat the request while the host guard simulates an active leg. The
     * queue would later complete, but admission has already been refused. */
    const uint32_t leg_next_before = state.next_id;
    host_refuse("native.ssm.request");
    EXPECT_UNAVAILABLE((void)melee_web_audio_bank_transport_request(
        0, BANK_BASE + 32, BANK_BASE, 32, 0x1B, 1, ssm_callback, NULL));
    CHECK(state.next_id == leg_next_before);
    CHECK(state.pending == 0 && state.requests == NULL);
    CHECK(melee_web_audio_bank_transport_quiescent_for(callback_audio));
    CHECK(host_last_call_is("native.ssm.request"));
    CHECK(host_failure_count() == 2);

    free(state.auxiliary);
    memset(&state, 0, sizeof(state));
    puts("Actual SSM same-owner predicate accepted idle state and refused absent/wrong/queued/pumping owners; an enqueue-and-complete resident request was stopped before ID or queue mutation");
    return 0;
}
