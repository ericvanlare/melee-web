#ifndef MELEE_WEB_TEST_STADIUM_C1_E8_CALL_OBSERVER_H
#define MELEE_WEB_TEST_STADIUM_C1_E8_CALL_OBSERVER_H

#include <stddef.h>
#include <stdint.h>

/* Filenames entering the source file service are bounded at 4096 bytes. */
#define MELEE_WEB_STADIUM_E8_FILENAME_CAPACITY 4097

typedef struct MeleeWebStadiumE8CallObservation {
    uint32_t source_size_calls;
    uint32_t source_size_successes;
    uint32_t source_size_name_mismatches;
    char source_size_name[MELEE_WEB_STADIUM_E8_FILENAME_CAPACITY];
    size_t source_size_bytes;

    uint32_t typed_open_calls;
    uint32_t typed_open_successes;
    uint32_t typed_open_name_mismatches;
    uint32_t typed_handle_mismatches;
    char typed_open_name[MELEE_WEB_STADIUM_E8_FILENAME_CAPACITY];
    void* typed_archive_handle;

    uint32_t map_head_calls;
    uint32_t coll_data_calls;
    uint32_t ground_param_calls;
    uint32_t itemdata_calls;
    uint32_t ald_yaku_all_calls;
    uint32_t map_ptcl_calls;
    uint32_t map_texg_calls;
    uint32_t yakumono_param_calls;
    uint32_t map_plit_calls;
    uint32_t quake_model_set_calls;
    uint32_t other_public_calls;
    void* map_head_archive;
    void* map_head_value;
    void* coll_data_value;
    void* ground_param_value;
    void* itemdata_value;
    void* ald_yaku_all_value;
    void* map_ptcl_value;
    void* map_texg_value;
    void* yakumono_param_value;
    void* map_plit_value;
    void* quake_model_set_value;
} MeleeWebStadiumE8CallObservation;

#ifdef __cplusplus
extern "C" {
#endif

int melee_web_stadium_e8_call_observer_begin(void);
int melee_web_stadium_e8_call_observer_end(
    MeleeWebStadiumE8CallObservation* observation);

/* Linker-wrap entry points. The diagnostic target wraps source calls to these
 * existing APIs; synthetic tests call the same wrappers with fake delegates. */
int __wrap_melee_web_source_file_size(const char* name, size_t* size);
void* __wrap_melee_web_archive_sections_open_preloaded(const char* filename);
void* __wrap_melee_web_archive_sections_public(void* archive,
                                               const char* symbol);

#ifdef __cplusplus
}
#endif

#endif
