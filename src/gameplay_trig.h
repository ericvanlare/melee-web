#ifndef MELEE_WEB_GAMEPLAY_TRIG_H
#define MELEE_WEB_GAMEPLAY_TRIG_H

/* Host boundary for MSL/trigf.c. Keep its original polynomial and explicit
 * single-precision fused operations; host libm is a different approximation.
 * These conversions reproduce fctiwz and the following 32-bit slwi, including
 * out-of-range inputs, without undefined C casts or signed overflow. */
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>

static inline uint32_t melee_web_trig_bits(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static inline float melee_web_trig_from_bits(uint32_t bits)
{
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static inline int32_t melee_web_trig_integer(float value)
{
    if (isnan(value) || value <= -2147483648.0f)
        return INT32_MIN;
    if (value >= 2147483648.0f)
        return INT32_MAX;
    return (int32_t) value;
}

static inline float melee_web_trig_twice(int32_t value)
{
    uint32_t bits = (uint32_t) value << 1;
    int32_t doubled;
    memcpy(&doubled, &bits, sizeof(doubled));
    return (float) doubled;
}

#endif
