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
static int audio_active=1;

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
    return audio_active && audio == callback_audio;
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

static void check_quiescent_unchanged(MeleeWebAudio* audio,int expected)
{
    unsigned char before[sizeof(state)];
    memcpy(before,&state,sizeof(state));
    const int calls_before=host_call_count();
    CHECK(melee_web_audio_bank_transport_quiescent_for(audio)==expected);
    CHECK(memcmp(before,&state,sizeof(state))==0);
    CHECK(host_call_count()==calls_before);
}

static void ssm_callback(int request, int args, void* buffer, int cancelled)
{
    (void)request;
    (void)args;
    (void)buffer;
    CHECK(!cancelled);
    callback_quiescent = melee_web_audio_bank_transport_quiescent_for(
        callback_audio);
    check_quiescent_unchanged(callback_audio,0);
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

    /* Supported match audio retains resident banks without starting SSM.
     * Every logical field must be empty; neither predicate branch mutates it. */
    check_quiescent_unchanged(callback_audio,1);
    check_quiescent_unchanged(NULL,0);
    check_quiescent_unchanged(wrong_audio,0);
    audio_active=0;check_quiescent_unchanged(callback_audio,0);audio_active=1;
    Transfer held={.next=NULL,.id=REQUEST_BASE+1,.file=123,.type=0x23,.priority=1,
        .cancelled=0,.source=32,.dest=(uintptr_t)relocation_bytes,.size=32,
        .callback=ssm_callback,.args=registry};
#define POISON_ABSENT(statement) do { \
    statement;check_quiescent_unchanged(callback_audio,0); \
    memset(&state,0,sizeof(state));check_quiescent_unchanged(callback_audio,1); \
} while(0)
    POISON_ABSENT(state.audio=wrong_audio);
    POISON_ABSENT(state.registry=registry);
    POISON_ABSENT(state.auxiliary=relocation_bytes);
    POISON_ABSENT(state.size=1);
    POISON_ABSENT(state.next_id=REQUEST_BASE);
    POISON_ABSENT(state.pending=1);
    POISON_ABSENT(state.requests=&held);
    POISON_ABSENT(state.mapping_count=1);
    POISON_ABSENT(state.pumping=1);
    POISON_ABSENT(state.configured=1);
    for(size_t i=0;i<sizeof(state.sem_path);++i)POISON_ABSENT(state.sem_path[i]=1);
    for(size_t i=0;i<MELEE_WEB_AUDIO_RESIDENCY_MAX_ASSETS;++i){
        POISON_ABSENT(state.mappings[i].file=1);
        POISON_ABSENT(state.mappings[i].base=BANK_BASE);
        POISON_ABSENT(state.mappings[i].size=32);
    }
#undef POISON_ABSENT
    puts("Actual SSM pristine absent transport accepted only live audio; every scalar, path byte and mapping field poison refused without state or service mutation");


    CHECK(melee_web_audio_bank_transport_begin(callback_audio, registry,
        "fixture.sem", error, sizeof(error)));
    CHECK(melee_web_audio_bank_transport_quiescent_for(callback_audio));
    audio_active=0;check_quiescent_unchanged(callback_audio,0);audio_active=1;
    check_quiescent_unchanged(callback_audio,1);
    state.pending=1;check_quiescent_unchanged(callback_audio,0);state.pending=0;
    state.requests=&held;check_quiescent_unchanged(callback_audio,0);state.requests=NULL;
    Transfer held_second=held;
    held_second.id=REQUEST_BASE+2;held_second.priority=0;
    held_second.dest=(uintptr_t)(relocation_bytes+32);held.next=&held_second;
    unsigned char held_before[sizeof(held)],second_before[sizeof(held_second)];
    unsigned char pending_destination_before[sizeof(relocation_bytes)];
    memcpy(held_before,&held,sizeof(held));memcpy(second_before,&held_second,sizeof(held_second));
    memcpy(pending_destination_before,relocation_bytes,sizeof(relocation_bytes));
    state.requests=&held;state.pending=2;
    check_quiescent_unchanged(callback_audio,0);
    CHECK(memcmp(held_before,&held,sizeof(held))==0);
    CHECK(memcmp(second_before,&held_second,sizeof(held_second))==0);
    CHECK(memcmp(pending_destination_before,relocation_bytes,sizeof(relocation_bytes))==0);
    state.requests=NULL;state.pending=0;

    state.pumping=1;check_quiescent_unchanged(callback_audio,0);state.pumping=0;

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
    /* A refusal must not consume requests, IDs or original pending destinations. */
    Transfer* const queue_before=state.requests;
    unsigned char request_before[sizeof(Transfer)],destination_before[sizeof(relocation_bytes)];
    memcpy(request_before,state.requests,sizeof(Transfer));
    memcpy(destination_before,state.auxiliary,sizeof(relocation_bytes));
    check_quiescent_unchanged(callback_audio,0);
    check_quiescent_unchanged(wrong_audio,0);
    CHECK(state.requests==queue_before);
    CHECK(memcmp(request_before,state.requests,sizeof(Transfer))==0);
    CHECK(memcmp(destination_before,state.auxiliary,sizeof(relocation_bytes))==0);

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
    check_quiescent_unchanged(callback_audio,1);
    puts("Actual SSM same-owner predicate accepted idle state and refused foreign/inactive/queued/pumping owners; an enqueue-and-complete resident request was stopped before ID or queue mutation");
    return 0;
}
