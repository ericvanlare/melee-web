#ifndef MELEE_WEB_GAMEPLAY_FMA_DIAGNOSTIC_H
#define MELEE_WEB_GAMEPLAY_FMA_DIAGNOSTIC_H

#include <stddef.h>
#include <stdint.h>

enum MeleeWebFmaCollisionSite {
    MELEE_WEB_FMA_RADIUS = 0,
    MELEE_WEB_FMA_SEGMENT_DOT_XY,
    MELEE_WEB_FMA_SEGMENT_DOT_Z,
    MELEE_WEB_FMA_HURT_LENGTH_XY,
    MELEE_WEB_FMA_HURT_LENGTH_Z,
    MELEE_WEB_FMA_HIT_START_DOT_XY,
    MELEE_WEB_FMA_HIT_START_DOT_Z,
    MELEE_WEB_FMA_HURT_START_DOT_XY,
    MELEE_WEB_FMA_HURT_START_DOT_Z,
    MELEE_WEB_FMA_CLOSEST_DENOMINATOR,
    MELEE_WEB_FMA_PARALLEL_START_X,
    MELEE_WEB_FMA_PARALLEL_START_Z,
    MELEE_WEB_FMA_PARALLEL_END_X,
    MELEE_WEB_FMA_PARALLEL_END_Z,
    MELEE_WEB_FMA_ENDPOINT_START_DENOM_X,
    MELEE_WEB_FMA_ENDPOINT_START_DENOM_Z,
    MELEE_WEB_FMA_ENDPOINT_START_DOT_X,
    MELEE_WEB_FMA_ENDPOINT_START_DOT_Z,
    MELEE_WEB_FMA_ENDPOINT_END_DENOM_X,
    MELEE_WEB_FMA_ENDPOINT_END_DENOM_Z,
    MELEE_WEB_FMA_ENDPOINT_END_DOT_X,
    MELEE_WEB_FMA_ENDPOINT_END_DOT_Z,
    MELEE_WEB_FMA_HIT_PARAM_NUMERATOR,
    MELEE_WEB_FMA_HURT_PARAM_NUMERATOR,
    MELEE_WEB_FMA_HURT_CLOSEST_Y,
    MELEE_WEB_FMA_HURT_CLOSEST_Z,
    MELEE_WEB_FMA_HURT_CLOSEST_X,
    MELEE_WEB_FMA_HIT_CLOSEST_X,
    MELEE_WEB_FMA_HIT_CLOSEST_Y,
    MELEE_WEB_FMA_HIT_CLOSEST_Z,
    MELEE_WEB_FMA_CLOSEST_DISTANCE_X,
    MELEE_WEB_FMA_CLOSEST_DISTANCE_Z,
    MELEE_WEB_FMA_LOCAL_DISTANCE_X,
    MELEE_WEB_FMA_LOCAL_DISTANCE_Z,
    MELEE_WEB_FMA_CONTACT_X,
    MELEE_WEB_FMA_CONTACT_Y,
    MELEE_WEB_FMA_CONTACT_Z,
    MELEE_WEB_FMA_COLLISION_SITE_COUNT
};

#ifdef MELEE_WEB_FMA_COLLISION_DIAGNOSTIC
float melee_web_fma_collision(unsigned site, float a, float b, float c);
#define MELEE_WEB_FMA_COLLISION_ADD(site, a, b, c) \
    melee_web_fma_collision((site), (a), (b), (c))
#define MELEE_WEB_FMA_COLLISION_SUB(site, a, b, c) \
    melee_web_fma_collision((site), (a), (b), -((c)))
#else
#define MELEE_WEB_FMA_COLLISION_ADD(site, a, b, c) ((a) * (b) + (c))
#define MELEE_WEB_FMA_COLLISION_SUB(site, a, b, c) ((a) * (b) - (c))
#endif

#ifdef MELEE_WEB_FMA_VELOCITY_Y_DIAGNOSTIC
float melee_web_fma_velocity_y(float multiplier, float delta, float base);
#define MELEE_WEB_FMA_VELOCITY_Y(multiplier, delta, base) \
    melee_web_fma_velocity_y((multiplier), (delta), (base))
#else
#define MELEE_WEB_FMA_VELOCITY_Y(multiplier, delta, base) \
    ((multiplier) * (delta) + (base))
#endif

void melee_web_fma_diagnostic_cursor(uint32_t cursor);
void melee_web_fma_diagnostic_finish_cursor(void);
void melee_web_fma_diagnostic_append_json(char* output, size_t capacity,
                                          size_t* used);

#endif
