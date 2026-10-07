#include "stadium_c1_e8_call_observer.h"

#include "gameplay_archive_sections.h"
#include "gameplay_source_files.h"

#include <string.h>

extern int __real_melee_web_source_file_size(const char*, size_t*);
extern void* __real_melee_web_archive_sections_open_preloaded(const char*);
extern void* __real_melee_web_archive_sections_public(void*, const char*);

static MeleeWebStadiumE8CallObservation observed;
static int active;

static int name_contains_grps(const char* name)
{
    return name != NULL && strstr(name, "GrPs") != NULL;
}

static void record_name(char* destination, const char* name,
                        uint32_t* mismatches)
{
    const size_t length = strnlen(name, MELEE_WEB_STADIUM_E8_FILENAME_CAPACITY);
    if (length == MELEE_WEB_STADIUM_E8_FILENAME_CAPACITY) {
        ++*mismatches;
        return;
    }
    if (destination[0] == '\0') {
        memcpy(destination, name, length + 1);
    } else if (strcmp(destination, name) != 0) {
        ++*mismatches;
    }
}

int melee_web_stadium_e8_call_observer_begin(void)
{
    if (active) return 0;
    memset(&observed, 0, sizeof(observed));
    active = 1;
    return 1;
}

int melee_web_stadium_e8_call_observer_end(
    MeleeWebStadiumE8CallObservation* observation)
{
    if (!active || !observation) return 0;
    active = 0;
    *observation = observed;
    return 1;
}

int __wrap_melee_web_source_file_size(const char* name, size_t* size)
{
    const int result = __real_melee_web_source_file_size(name, size);
    if (active && name_contains_grps(name)) {
        ++observed.source_size_calls;
        record_name(observed.source_size_name, name,
                    &observed.source_size_name_mismatches);
        if (result) {
            ++observed.source_size_successes;
            observed.source_size_bytes = *size;
        }
    }
    return result;
}

void* __wrap_melee_web_archive_sections_open_preloaded(const char* filename)
{
    void* const result =
        __real_melee_web_archive_sections_open_preloaded(filename);
    if (active && name_contains_grps(filename)) {
        ++observed.typed_open_calls;
        record_name(observed.typed_open_name, filename,
                    &observed.typed_open_name_mismatches);
        if (result) ++observed.typed_open_successes;
        if (observed.typed_archive_handle &&
            observed.typed_archive_handle != result)
            ++observed.typed_handle_mismatches;
        observed.typed_archive_handle = result;
    }
    return result;
}

void* __wrap_melee_web_archive_sections_public(void* archive,
                                               const char* symbol)
{
    void* const result = __real_melee_web_archive_sections_public(archive,
                                                                  symbol);
    if (!active || archive != observed.typed_archive_handle || !symbol)
        return result;

#define RECORD_PUBLIC(name, field) \
    if (strcmp(symbol, name) == 0) { \
        ++observed.field##_calls; \
        observed.field##_value = result; \
        return result; \
    }
    if (strcmp(symbol, "map_head") == 0) {
        ++observed.map_head_calls;
        observed.map_head_archive = archive;
        observed.map_head_value = result;
        return result;
    }
    RECORD_PUBLIC("coll_data", coll_data)
    RECORD_PUBLIC("grGroundParam", ground_param)
    RECORD_PUBLIC("itemdata", itemdata)
    RECORD_PUBLIC("ALDYakuAll", ald_yaku_all)
    RECORD_PUBLIC("map_ptcl", map_ptcl)
    RECORD_PUBLIC("map_texg", map_texg)
    RECORD_PUBLIC("yakumono_param", yakumono_param)
    RECORD_PUBLIC("map_plit", map_plit)
    RECORD_PUBLIC("quake_model_set", quake_model_set)
#undef RECORD_PUBLIC
    ++observed.other_public_calls;
    return result;
}
