#ifndef MELEE_WEB_HSD_TEXTURE_BOUNDS_H
#define MELEE_WEB_HSD_TEXTURE_BOUNDS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* The original TObj owns its animation binding and releases it with the
 * object. Keep table capacities outside original HSD struct/ABI layouts. */
void melee_web_texture_bounds_bind(const void* object, uint32_t images, uint32_t palettes);
void melee_web_texture_bounds_remove(const void* object);
int melee_web_texture_index_valid(const void* object, uint32_t channel, float value);
uint32_t melee_web_texture_bounds_live(void);
#ifdef __cplusplus
}
#endif
#endif
