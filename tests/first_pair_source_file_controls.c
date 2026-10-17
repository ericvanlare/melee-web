#include <setjmp.h>
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

/* White-box inclusion exposes only the existing file-scope state to this
 * fixture. It does not add a production accessor or alter the owner. */
#include "gameplay_source_files.c"

_Noreturn void melee_web_platform_unavailable(const char* operation)
{
    (void)operation;
    longjmp(unavailable_context, 1);
}

#define EXPECT_UNAVAILABLE(statement) do { \
    if (setjmp(unavailable_context) == 0) { \
        statement; \
        CHECK(0 && "expected guarded source-file refusal"); \
    } \
} while (0)

static MeleeWebSourceFileScope* callback_scope;
static int callback_seen;
static int callback_quiescent;
static int callback_cancelled;

EM_JS(int, host_call_count, (), {return globalThis.window.calls.length;});

EM_JS(void, install_host_guard, (), {
        const host = ({calls: [], failures: [], refusal: null, discardCalls: 0});
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
        host.menuSourceReadDiscard = () => {
            host.discardCalls++;
            return true;
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

EM_JS(int, host_discard_count, (), {return globalThis.window.discardCalls;
});

static void source_callback(int request, int args, void* buffer, int cancelled)
{
    (void)request;
    (void)args;
    (void)buffer;
    callback_quiescent = melee_web_source_files_quiescent_for(callback_scope);
    callback_cancelled = cancelled;
    callback_seen = 1;
}

int main(void)
{
    static const uint8_t local_bytes[64] = {
        0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
        0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
        0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
        0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
        0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
        0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    };
    static const uint8_t external_bytes[32] = {
        0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
        0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
        0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
        0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    };
    const MeleeWebSourceFileInput scoped_inputs[] = {
        {"local.bin", local_bytes, sizeof(local_bytes)},
    };
    uint8_t destination[32];
    uint8_t copy_destination[64];
    char error[256] = {0};
    size_t copied_size = 777;
    int request = 12345;

    install_host_guard();
    CHECK(melee_web_first_pair_file_guard_enabled == 0);
    CHECK(melee_web_first_pair_file_operation("native.source.scope-begin"));
    CHECK(host_call_count() == 0);
    CHECK(melee_web_first_pair_file_guard_arm());

    host_refuse("native.source.external-set");
    CHECK(!melee_web_source_files_external_set("movie.thp", 64,
        error, sizeof(error)));
    CHECK(external_file_count == 0 && external_files == NULL);
    CHECK(host_last_call_is("native.source.external-set"));
    CHECK(host_failure_count() == 1);
    host_refuse("");
    CHECK(melee_web_source_files_external_set("movie.thp", 64,
        error, sizeof(error)));
    CHECK(external_file_count == 1);
    host_refuse("native.source.scope-begin");
    CHECK(melee_web_source_files_begin(scoped_inputs, sizeof(scoped_inputs) / sizeof(scoped_inputs[0]), error, sizeof(error)) == NULL);
    CHECK(active_scope == NULL);
    CHECK(host_last_call_is("native.source.scope-begin"));
    CHECK(host_failure_count() == 2);
    host_refuse("");

    MeleeWebSourceFileScope* scope = melee_web_source_files_begin(
        scoped_inputs, sizeof(scoped_inputs) / sizeof(scoped_inputs[0]), error, sizeof(error));
    CHECK(scope != NULL);
    CHECK(scope->count == 2);
    CHECK(!strcmp(scope->files[0].name, "local.bin") && !scope->files[0].external);
    CHECK(!strcmp(scope->files[1].name, "movie.thp") && scope->files[1].external);
    callback_scope = scope;
    CHECK(melee_web_source_files_quiescent_for(scope));
    CHECK(!melee_web_source_files_quiescent_for(NULL));
    CHECK(!melee_web_source_files_quiescent_for(
        (const MeleeWebSourceFileScope*)(uintptr_t)1));

    memset(destination, 0xcc, sizeof(destination));
    host_refuse("native.source.region-register");
    CHECK(!melee_web_source_file_region_register(destination,
        sizeof(destination), error, sizeof(error)));
    CHECK(scope->regions == NULL);
    CHECK(host_last_call_is("native.source.region-register"));
    CHECK(host_failure_count() == 3);
    host_refuse("");
    CHECK(melee_web_source_file_region_register(destination,
        sizeof(destination), error, sizeof(error)));
    SourceFileRegion* const owned_region = scope->regions;

    const int external_entry = melee_web_source_file_entry("movie.thp");
    CHECK(external_entry >= MELEE_WEB_SOURCE_FILE_ENTRY_BASE);
    CHECK(scope->request_count == 0 && scope->requests == NULL);
    const int next_id_before_refusal = next_source_request;
    host_refuse("native.source.request");
    CHECK(!melee_web_source_file_request(external_entry, 0,
        (uintptr_t)destination, sizeof(destination), 0x21, 1,
        source_callback, NULL, &request, error, sizeof(error)));
    CHECK(request == 12345);
    CHECK(next_source_request == next_id_before_refusal);
    CHECK(scope->request_count == 0 && scope->requests == NULL);
    CHECK(melee_web_source_files_quiescent_for(scope));
    CHECK(host_last_call_is("native.source.request"));
    CHECK(host_failure_count() == 4);
    host_refuse("");

    CHECK(melee_web_source_file_request(external_entry, 0,
        (uintptr_t)destination, sizeof(destination), 0x21, 1,
        source_callback, NULL, &request, error, sizeof(error)));
    SourceFileRequest* const queued = scope->requests;
    CHECK(queued != NULL && scope->request_count == 1);
    CHECK(!melee_web_source_files_quiescent_for(scope));

    host_refuse("native.source.supply");
    CHECK(!melee_web_source_file_supply(request, external_bytes,
        sizeof(external_bytes), error, sizeof(error)));
    CHECK(queued->stream_ready == 0);
    for (size_t i = 0; i < sizeof(destination); ++i) CHECK(destination[i] == 0xcc);
    CHECK(host_last_call_is("native.source.supply"));
    CHECK(host_failure_count() == 5);
    host_refuse("");

    CHECK(melee_web_source_file_supply(request, external_bytes,
        sizeof(external_bytes), error, sizeof(error)));
    CHECK(queued->stream_ready == 1);
    CHECK(memcmp(destination, external_bytes, sizeof(destination)) == 0);

    host_refuse("native.source.pump");
    CHECK(!melee_web_source_files_pump(error, sizeof(error)));
    CHECK(scope->pumping == 0 && scope->requests == queued);
    CHECK(scope->request_count == 1);
    CHECK(host_last_call_is("native.source.pump"));
    CHECK(host_failure_count() == 6);
    host_refuse("");

    CHECK(melee_web_source_files_pump(error, sizeof(error)));
    CHECK(callback_seen);
    CHECK(!callback_quiescent);
    CHECK(callback_cancelled == 0);
    CHECK(scope->request_count == 0 && scope->requests == NULL);
    CHECK(melee_web_source_files_quiescent_for(scope));

    /* A refused source cancel must not discard the actual host read or mutate
     * the native request before the host failure is latched. */
    request = 23456;
    CHECK(melee_web_source_file_request(external_entry, 0,
        (uintptr_t)destination, sizeof(destination), 0x21, 1,
        source_callback, NULL, &request, error, sizeof(error)));
    SourceFileRequest* const cancel_request = scope->requests;
    CHECK(cancel_request != NULL && !cancel_request->cancelled);
    host_refuse("native.source.cancel");
    EXPECT_UNAVAILABLE((void)melee_web_source_file_cancel(request, 1,
        source_callback, NULL));
    CHECK(scope->requests == cancel_request);
    CHECK(!cancel_request->cancelled);
    CHECK(host_discard_count() == 0);
    CHECK(host_last_call_is("native.source.cancel"));
    CHECK(host_failure_count() == 7);
    host_refuse("");
    CHECK(melee_web_source_file_cancel(request, 1, source_callback, NULL));
    CHECK(cancel_request->cancelled);
    CHECK(host_discard_count() == 1);
    CHECK(melee_web_source_files_pump(error, sizeof(error)));
    CHECK(callback_cancelled == 1);
    CHECK(!callback_quiescent);
    CHECK(scope->request_count == 0 && scope->requests == NULL);
    CHECK(melee_web_source_files_quiescent_for(scope));

    /* Copy refusal precedes both bytes and the copy-name ownership record. */
    memset(copy_destination, 0xaa, sizeof(copy_destination));
    copied_size = 777;
    CHECK(scope->copies == NULL);
    host_refuse("native.source.copy");
    EXPECT_UNAVAILABLE((void)melee_web_source_file_copy(
        "local.bin", copy_destination, &copied_size));
    CHECK(scope->copies == NULL);
    CHECK(copied_size == 777);
    for (size_t i = 0; i < sizeof(copy_destination); ++i)
        CHECK(copy_destination[i] == 0xaa);
    CHECK(host_last_call_is("native.source.copy"));
    CHECK(host_failure_count() == 8);
    host_refuse("");

    CHECK(melee_web_source_file_copy("local.bin", copy_destination,
        &copied_size));
    CHECK(copied_size == sizeof(copy_destination));
    CHECK(memcmp(copy_destination, local_bytes, sizeof(local_bytes)) == 0);
    SourceFileCopy* const copied_name = scope->copies;
    CHECK(copied_name != NULL);
    host_refuse("native.source.take-name");
    EXPECT_UNAVAILABLE((void)melee_web_source_file_take_name(copy_destination));
    CHECK(scope->copies == copied_name);
    CHECK(scope->copies->destination == copy_destination);
    CHECK(scope->copies->name == copied_name->name);
    CHECK(host_last_call_is("native.source.take-name"));
    CHECK(host_failure_count() == 9);
    host_refuse("");
    CHECK(strcmp(melee_web_source_file_take_name(copy_destination),
        "local.bin") == 0);
    CHECK(scope->copies == NULL);

    host_refuse("native.source.region-unregister");
    CHECK(!melee_web_source_file_region_unregister(destination,
        sizeof(destination), error, sizeof(error)));
    CHECK(scope->regions == owned_region);
    CHECK(host_last_call_is("native.source.region-unregister"));
    CHECK(host_failure_count() == 10);
    host_refuse("");
    CHECK(melee_web_source_file_region_unregister(destination,
        sizeof(destination), error, sizeof(error)));
    CHECK(scope->regions == NULL);

    host_refuse("native.source.scope-end");
    CHECK(!melee_web_source_files_end(scope, error, sizeof(error)));
    CHECK(active_scope == scope);
    CHECK(host_last_call_is("native.source.scope-end"));
    CHECK(host_failure_count() == 11);
    host_refuse("");
    CHECK(melee_web_source_files_end(scope, error, sizeof(error)));
    CHECK(active_scope == NULL);
    CHECK(!melee_web_source_files_quiescent_for(scope));

    host_refuse("native.source.external-clear");
    CHECK(!melee_web_source_files_external_clear(error, sizeof(error)));
    CHECK(external_file_count == 1 && external_files != NULL);
    CHECK(host_last_call_is("native.source.external-clear"));
    CHECK(host_failure_count() == 12);
    host_refuse("");
    CHECK(melee_web_source_files_external_clear(error, sizeof(error)));
    CHECK(external_file_count == 0 && external_files == NULL);

    puts("Actual source-file owner predicate and request/copy/take/region/scope guards passed: exact idle scope admitted; absent, wrong, queued and callback-pumping scopes refused; default-off source path made no host calls");
    return 0;
}
