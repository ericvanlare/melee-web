#ifndef MELEE_WEB_GAMEPLAY_HOOKSHOT_MATH_H
#define MELEE_WEB_GAMEPLAY_HOOKSHOT_MATH_H
#include <dolphin/ppc_math.h>

/* GALE01r2 inlines the distance helper differently from its standalone body:
 * y*y, fmadds(x,x,...), fmadds(z,z,...), then three double fnmsub refinements.
 * Keep this separate from it_802A3C98, whose out-of-line sum is unfused. */
static inline float melee_web_hookshot_normalize(const Vec3* a, const Vec3* b, Vec3* v)
{
    v->x = a->x - b->x;
    v->y = a->y - b->y;
    v->z = a->z - b->z;
    float length = fmaf(v->z, v->z, fmaf(v->x, v->x, v->y * v->y));
    if (length > 0.0f) {
        double guess = frsqrte((double)length);
        for (unsigned i = 0; i < 3; ++i)
            guess = (0.5 * guess) * fma(-(double)length, guess * guess, 3.0);
        volatile float rounded = (float)((double)length * guess);
        length = rounded;
    }
    const float inverse = length == 0.0f ? 0.0f : (float)(1.0 / (double)length);
    v->x *= inverse;
    v->y *= inverse;
    v->z *= inverse;
    return length;
}
#endif
