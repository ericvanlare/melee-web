#ifndef MELEE_WEB_CPU_OBSERVATION_H
#define MELEE_WEB_CPU_OBSERVATION_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Read-only match diagnostics. CPU outputs never feed the input recipe or
 * the simulation. Whole-session capture is opt-in through a browser callback. */
int melee_web_cpu_observation_available(void);
void melee_web_cpu_observation_begin(const uint8_t setup[0x138], size_t frames,
                                     int source_drawing);
/* One-shot native diagnostic opt-in. This only emits read-only HITLAG_AUDIT
 * lines; it is never part of the accepted input plan or simulation state. */
void melee_web_cpu_observation_enable_hitlag_audit(void);
void melee_web_cpu_observation_tick(size_t index);
void melee_web_cpu_observation_draw(size_t index);
void melee_web_cpu_observation_preparation_draw(void);
void melee_web_cpu_observation_end(size_t frames);

/* Read-only, one-cursor trace of the Arrow shield-collision arithmetic. */
enum {
    MELEE_WEB_LB_COLL_BRANCH_NEAR_ZERO = 0,
    MELEE_WEB_LB_COLL_BRANCH_QUADRATIC = 1
};
typedef struct {
    uintptr_t a_address, matrix_address, b_address, c_address;
    uintptr_t d_address, e_address, angle_address;
    float a[3], matrix[12], b[3], c[3];
    float radius, distance_offset;
    float transformed_radius[3], transformed_origin[3];
    float diff_cb[3], diff_ba[3];
    float distance, offset_distance, dot_diff_cb, n0, ba_dot, n1, scale;
    uint8_t branch; /* n0, ba_dot and n1 are valid only on the quadratic branch. */
    float normalize_e[3], normal[3], collision_position[3], angle;
} MeleeWebLbCollProbe;
int melee_web_cpu_observation_lb_collision_probe_active(void);
void melee_web_cpu_observation_lb_collision_probe(const MeleeWebLbCollProbe* probe);
#ifdef __cplusplus
}
#endif
#endif
