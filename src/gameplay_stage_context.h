#ifndef MELEE_WEB_GAMEPLAY_STAGE_CONTEXT_H
#define MELEE_WEB_GAMEPLAY_STAGE_CONTEXT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* CPU descriptors own no HSD objects. This first boundary accepts source ambient
 * and infinite lights only; animation, custom classes and WObj constraints reject. */
typedef struct MeleeWebStageLightDesc {
    uint32_t source_offset;
    uint16_t flags, attenuation_flags;
    uint8_t color[4], has_position, has_interest, has_shininess;
    float position[3], interest[3], shininess;
} MeleeWebStageLightDesc;
typedef struct MeleeWebStageLights MeleeWebStageLights;
MeleeWebStageLights* melee_web_stage_lights_create(const MeleeWebStageLightDesc* lights,
    uint32_t count, char* error, size_t error_size);
/* Records a checked first-match lookup from the source map_head table. Must be
 * set before publication; found=0 means a validated absence, not an omission. */
int melee_web_stage_lights_set_override(MeleeWebStageLights*, uint32_t index,
    int found, uint8_t flags, char*, size_t);
/* Source lookup adapter: 0 means this descriptor is outside the owned context. */
int melee_web_stage_lights_lookup_override(void* descriptor, int* found, uint8_t* flags);
/* Actual LightList** for original Ground/lb consumers. Storage survives until
 * destroy. Publication is explicitly scoped; this does not initialize Ground. */
void* melee_web_stage_lights_descriptors(MeleeWebStageLights* lights);
/* Runs original lb_80011AC4, attaches real LObj to a destructible GObj.
 * This isolated constructor does not apply Ground scaling or overrides. */
int melee_web_stage_lights_load(MeleeWebStageLights*, char*, size_t);
int melee_web_stage_lights_stats(MeleeWebStageLights*, uint32_t* count,
    uint16_t* flags, uint8_t* rgba, uint32_t capacity, char*, size_t);
int melee_web_stage_lights_attach(MeleeWebStageLights*, char*, size_t);
int melee_web_stage_lights_detach(MeleeWebStageLights*, char*, size_t);
int melee_web_stage_lights_destroy(MeleeWebStageLights*, char*, size_t);
#ifdef __cplusplus
}
#endif
#endif
