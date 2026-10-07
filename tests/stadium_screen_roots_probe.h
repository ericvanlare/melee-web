#pragma once
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
#ifdef __cplusplus
}
#endif
