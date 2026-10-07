#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct StadiumScreenImageView {
    void* image;
    void* texture;
    void* material;
    uint32_t references;
} StadiumScreenImageView;
int stadium_screen_image_view(void* map, uint32_t entry, void* image,
                              StadiumScreenImageView* view);
void* stadium_screen_source_public(void* handle, const char* name);
typedef struct StadiumLiveImageQuery {
    void* texture;
    void* material;
} StadiumLiveImageQuery;
StadiumLiveImageQuery stadium_screen_live_image_query(void* view, void* image,
                                                     void* material_sentinel);
void* stadium_screen_map_entry_joint(void* map, uint32_t entry);
size_t stadium_screen_stage_info_size(void);
int stadium_screen_stage_info_snapshot(void* output, size_t size);
int stadium_screen_stage_is_empty(void);
#ifdef __cplusplus
}
#endif
