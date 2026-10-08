#ifndef MELEE_WEB_HIT_TRANSITION_PROBE_H
#define MELEE_WEB_HIT_TRANSITION_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct HSD_GObj;
struct Fighter;
struct HitCapsule;
struct HurtCapsule;
void melee_web_hit_probe_cursor(size_t cursor);
void melee_web_hit_probe_scheduler_return(void);
unsigned melee_web_hit_probe_begin(struct HSD_GObj*, unsigned kind,
    int requested_motion, unsigned flags, float frame, float speed, float blend,
    size_t log_count, int log_kind);
void melee_web_hit_probe_log(unsigned invocation, size_t index, int entity_kind,
    int fighter_kind, struct HSD_GObj* source, const void* hit, const void* hurt,
    float x, float y, float z, float damage, size_t hit_bytes);
void melee_web_hit_probe_end(struct HSD_GObj*, unsigned invocation);
/* Selected slot0 attacker / slot1 receiver only; original routines are never called here. */
unsigned melee_web_hit_probe_pass_begin(struct HSD_GObj* receiver);
void melee_web_hit_probe_pass_end(unsigned pass);
unsigned melee_web_hit_probe_pair_begin(unsigned pass, struct HSD_GObj* attacker,
    unsigned encounter, int self_seen);
void melee_web_hit_probe_pair_end(unsigned pair);
void melee_web_hit_probe_candidate(unsigned pair, unsigned index, const struct HitCapsule* hit);
unsigned melee_web_hit_probe_geometry_begin(const struct HitCapsule*, const struct HurtCapsule*,
    const void* matrix, int mode, float attacker_scale, float receiver_scale, float z);
void melee_web_hit_probe_geometry_end(unsigned geometry, int result);
void melee_web_hit_probe_geometry_inner(const void* hit_start,
    const void* hit_end, const void* hurt_start,
    const void* hurt_end, const void* matrix, float hit_radius,
    float hurt_radius, float broadphase_scale);
unsigned melee_web_hit_probe_producer_begin(struct Fighter* attacker, const struct HitCapsule*,
    struct Fighter* receiver, const void* hurt, size_t log0, size_t log1);
void melee_web_hit_probe_producer_branch(unsigned producer, unsigned branch, size_t log0, size_t log1);
void melee_web_hit_probe_producer_end(unsigned producer, int result, size_t log0, size_t log1);
#ifdef __cplusplus
}
#endif
#endif
