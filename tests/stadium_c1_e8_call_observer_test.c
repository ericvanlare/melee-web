#include "stadium_c1_e8_call_observer.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned size_delegate_calls;
static unsigned open_delegate_calls;
static unsigned public_delegate_calls;
static int delegate_size(const char* name, size_t* size)
{
    ++size_delegate_calls;
    if (!name || !size) return 0;
    if (strcmp(name, "/GrPs.usd") == 0) {
        *size = 0x1234;
        return 1;
    }
    if (strcmp(name, "GrPs.usd") == 0) {
        *size = 0x5678;
        return 1;
    }
    return 0;
}

static int archive_handle_a;
static int archive_handle_b;
static int public_value_map_head;
static int public_value_collision;

int __real_melee_web_source_file_size(const char* name, size_t* size)
{
    return delegate_size(name, size);
}

void* __real_melee_web_archive_sections_open_preloaded(const char* filename)
{
    ++open_delegate_calls;
    if (filename && strcmp(filename, "/GrPs.usd") == 0)
        return &archive_handle_a;
    if (filename && strcmp(filename, "GrPs.usd") == 0)
        return &archive_handle_b;
    return NULL;
}

void* __real_melee_web_archive_sections_public(void* archive,
                                               const char* symbol)
{
    ++public_delegate_calls;
    if (archive != &archive_handle_b || !symbol) return NULL;
    if (strcmp(symbol, "map_head") == 0) return &public_value_map_head;
    if (strcmp(symbol, "coll_data") == 0) return &public_value_collision;
    return NULL;
}

static int require(int condition, const char* message)
{
    if (condition) return 1;
    fprintf(stderr, "%s\n", message);
    return 0;
}

int main(void)
{
    size_t size = 0;
    int ok = 1;
    ok &= require(__wrap_melee_web_source_file_size("/GrPs.usd", &size) &&
                      size == 0x1234,
                  "Inactive size wrapper changed its delegated result");
    ok &= require(melee_web_stadium_e8_call_observer_begin(),
                  "Observer window did not begin");
    ok &= require(!melee_web_stadium_e8_call_observer_begin(),
                  "Observer accepted a nested window");
    ok &= require(__wrap_melee_web_source_file_size("/GrPs.usd", &size) &&
                      size == 0x1234,
                  "Root-path size wrapper changed its delegated result");
    ok &= require(__wrap_melee_web_source_file_size("GrPs.usd", &size) &&
                      size == 0x5678,
                  "Canonical-name size wrapper changed its delegated result");
    ok &= require(!__wrap_melee_web_source_file_size("Other.usd", &size),
                  "Unrelated source-size result changed");

    void* const first =
        __wrap_melee_web_archive_sections_open_preloaded("/GrPs.usd");
    void* const second =
        __wrap_melee_web_archive_sections_open_preloaded("GrPs.usd");
    ok &= require(first == &archive_handle_a && second == &archive_handle_b,
                  "Typed-open wrapper changed its delegated handles");
    ok &= require(__wrap_melee_web_archive_sections_public(
                      second, "map_head") == &public_value_map_head,
                  "map_head lookup wrapper changed its delegated pointer");
    ok &= require(__wrap_melee_web_archive_sections_public(
                      second, "coll_data") == &public_value_collision,
                  "coll_data lookup wrapper changed its delegated pointer");
    ok &= require(__wrap_melee_web_archive_sections_public(
                      second, "other") == NULL,
                  "Unrecognized lookup wrapper changed its delegated result");

    MeleeWebStadiumE8CallObservation observation;
    ok &= require(melee_web_stadium_e8_call_observer_end(&observation),
                  "Observer window did not end");
    ok &= require(!melee_web_stadium_e8_call_observer_end(&observation),
                  "Observer accepted an end without an active window");
    ok &= require(observation.source_size_calls == 2 &&
                      observation.source_size_successes == 2 &&
                      observation.source_size_name_mismatches == 1 &&
                      strcmp(observation.source_size_name, "/GrPs.usd") == 0 &&
                      observation.source_size_bytes == 0x5678,
                  "Observer normalized or lost the exact source-size names");
    ok &= require(observation.typed_open_calls == 2 &&
                      observation.typed_open_successes == 2 &&
                      observation.typed_open_name_mismatches == 1 &&
                      observation.typed_handle_mismatches == 1 &&
                      strcmp(observation.typed_open_name, "/GrPs.usd") == 0 &&
                      observation.typed_archive_handle == &archive_handle_b,
                  "Observer normalized or lost exact typed-open identities");
    ok &= require(observation.map_head_calls == 1 &&
                      observation.map_head_archive == &archive_handle_b &&
                      observation.map_head_value == &public_value_map_head &&
                      observation.coll_data_calls == 1 &&
                      observation.coll_data_value == &public_value_collision &&
                      observation.other_public_calls == 1,
                  "Observer lost delegated public lookup identities");
    ok &= require(size_delegate_calls == 4 && open_delegate_calls == 2 &&
                      public_delegate_calls == 3,
                  "One or more wrappers failed to delegate every call");
    return ok ? 0 : 1;
}
