/* Include the production unit to observe its private translation owner. */
#include <melee/cm/camera.c>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

CameraUnkGlobals cm_803BCCA0;
static int training;
static float minimum_depth, maximum_depth;
melee_source_bool gm_8016B41C(void) { return training; }
float Stage_GetCamZoomRate(void) { return minimum_depth; }
float Stage_GetCamMaxDepth(void) { return maximum_depth; }
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

int main(void)
{
    unsigned mismatches = 0, old_differences = 0;
    cm_803BCCA0.x54 = 0.9f;
    cm_803BCCA0.x58 = 1.1f;
    cm_803BCCA0.x5C = 2.3f;
    cm_803BCCA0.x60 = 3.7f;
    cm_803BCCA0.xE8 = 0.75f;
    for (unsigned n = 0; n < 512; ++n) {
        CameraBounds bounds = {0};
        CameraTransformState state = {0};
        training = n & 1;
        minimum_depth = 80.0f;
        maximum_depth = n % 7 == 0 ? 80.0005f : 300.0f;
        bounds.z_pos = 100.0f + (float)n / 3.3f;
        state.fov = 30.0f + (float)(n % 5) * 0.5f;
        cm_80452C68.xA4 = -0.037f + (float)n / 891.0f;
        cm_80452C68.xA8 = 0.029f - (float)n / 991.0f;
        cm_80452C68.xAC = 1.125f;
        cm_80452C68.x2BC = 0.875f;
        float x = cm_80452C68.xA4 * cm_80452C68.xAC;
        float y = cm_80452C68.xA8 * cm_80452C68.xAC;
        x *= 10.0f; y *= 10.0f;
        if (training) { x *= cm_803BCCA0.xE8; y *= cm_803BCCA0.xE8; }
        x *= cm_80452C68.x2BC; y *= cm_80452C68.x2BC;
        float h = bounds.z_pos * tanf(0.5f * (0.017453292f * state.fov));
        float sx = cm_803BCB64.aspect * (h / (0.5f * 640.0f));
        float sy = h / (0.5f * 480.0f);
        float width = maximum_depth - minimum_depth;
        float ratio = width < 0.001f ? 0.5f : (bounds.z_pos - minimum_depth) / width;
        float dx = cm_803BCCA0.x60 - cm_803BCCA0.x58;
        float dy = cm_803BCCA0.x5C - cm_803BCCA0.x54;
        /* Owned 8002A230/234: fmadds, then separate 8002A238/23C fmuls. */
        float expected_x = fmaf(ratio, dx, cm_803BCCA0.x58) * (x * sx);
        float expected_y = fmaf(ratio, dy, cm_803BCCA0.x54) * (y * sy);
        volatile float split_x = ratio * dx, split_y = ratio * dy;
        float old_x = (split_x + cm_803BCCA0.x58) * (x * sx);
        float old_y = (split_y + cm_803BCCA0.x54) * (y * sy);
        old_differences += bits(old_x) != bits(expected_x) || bits(old_y) != bits(expected_y);
        Camera_8002A0C0(&bounds, &state);
        mismatches += bits(cm_80452C68.translation.x) != bits(expected_x) ||
                      bits(cm_80452C68.translation.y) != bits(expected_y);
        assert(bits(cm_80452C68.xA4) == 0 && bits(cm_80452C68.xA8) == 0);
        assert(cm_80452C68.xAC == 1.125f && cm_80452C68.x2BC == 0.875f);
    }
    assert(old_differences > 0);
    printf("camera shake cases=512 source_differences=%u split_differences=%u\n",
           mismatches, old_differences);
    return mismatches != 0;
}
