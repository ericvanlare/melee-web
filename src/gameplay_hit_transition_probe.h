#ifndef MELEE_WEB_HIT_TRANSITION_PROBE_H
#define MELEE_WEB_HIT_TRANSITION_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct HSD_GObj;
void melee_web_hit_probe_cursor(size_t cursor);
void melee_web_hit_probe_scheduler_return(void);
unsigned melee_web_hit_probe_begin(struct HSD_GObj*, unsigned kind,
    int requested_motion, unsigned flags, float frame, float speed, float blend,
    size_t log_count, int log_kind);
void melee_web_hit_probe_log(unsigned invocation, size_t index, int entity_kind,
    int fighter_kind, struct HSD_GObj* source, const void* hit, const void* hurt,
    float x, float y, float z, float damage, size_t hit_bytes);
void melee_web_hit_probe_end(struct HSD_GObj*, unsigned invocation);
#ifdef __cplusplus
}
#endif
#endif
