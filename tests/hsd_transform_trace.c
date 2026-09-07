/* Actual original HSD SRT and Aurora concatenation; no substituted math.
 * Analytic axis-turn cases distinguish rotation order and scale inheritance.
 */
#include "hsd_transform_bridge.h"
#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const float zero[3] = {0, 0, 0};
static const float one[3] = {1, 1, 1};
static const float half_pi = 1.57079632679489661923f;

static void expect_matrix(const MeleeWebJointTransform* actual, const float expected[3][4])
{
    for (unsigned row = 0; row < 3; ++row) {
        for (unsigned column = 0; column < 4; ++column) {
            assert(fabsf(actual->matrix[row][column] - expected[row][column]) < 0.00002f);
        }
    }
}

static void check_root_and_rotation_order(void)
{
    MeleeWebJointTransform result;
    char error[128];
    const float identity[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
    assert(melee_web_joint_transform(8, one, zero, zero, NULL, &result, error, sizeof error));
    assert(error[0] == 0 && !result.has_accumulated_scale);
    expect_matrix(&result, identity);

    /* Apply X then Z, with nonuniform scale before either rotation. */
    const float scale[3] = {2, 3, 4};
    const float rotation[3] = {half_pi, 0, half_pi};
    const float translation[3] = {5, 6, 7};
    const float expected[3][4] = {{0, 0, 4, 5}, {2, 0, 0, 6}, {0, 3, 0, 7}};
    assert(melee_web_joint_transform(8, scale, rotation, translation, NULL, &result, error, sizeof error));
    expect_matrix(&result, expected);

    /* Skeleton markers affect envelope matrix selection, not local SRT. */
    assert(melee_web_joint_transform(8 | 1 | 2 | 4, scale, rotation, translation,
                                    NULL, &result, error, sizeof error));
    expect_matrix(&result, expected);

    /* Y rotates positive X toward negative Z, before the positive Z turn. */
    const float yz_rotation[3] = {0, half_pi, half_pi};
    const float yz_expected[3][4] = {{0, -3, 0, 5}, {0, 0, 4, 6}, {-2, 0, 0, 7}};
    assert(melee_web_joint_transform(8, scale, yz_rotation, translation, NULL, &result, error, sizeof error));
    expect_matrix(&result, yz_expected);
}

static void check_scale_inheritance(void)
{
    MeleeWebJointTransform parent, child, grandchild;
    char error[128];
    const float parent_scale[3] = {2, 3, 4};
    const float parent_position[3] = {5, 6, 7};
    const float child_position[3] = {1, 2, 3};
    const float turn_z[3] = {0, 0, half_pi};
    assert(melee_web_joint_transform(8, parent_scale, zero, parent_position, NULL, &parent, error, sizeof error));
    assert(melee_web_joint_transform(8, one, turn_z, child_position, &parent, &child, error, sizeof error));
    const float classical[3][4] = {{0, -2, 0, 7}, {3, 0, 0, 12}, {0, 0, 4, 19}};
    expect_matrix(&child, classical);
    assert(!parent.has_accumulated_scale && !child.has_accumulated_scale);

    assert(melee_web_joint_transform(0, parent_scale, zero, parent_position, NULL, &parent, error, sizeof error));
    assert(parent.has_accumulated_scale && memcmp(parent.accumulated_scale, parent_scale, sizeof parent_scale) == 0);
    assert(melee_web_joint_transform(0, one, turn_z, child_position, &parent, &child, error, sizeof error));
    const float compensated[3][4] = {{0, -3, 0, 7}, {2, 0, 0, 12}, {0, 0, 4, 19}};
    expect_matrix(&child, compensated);
    assert(child.has_accumulated_scale && memcmp(child.accumulated_scale, parent_scale, sizeof parent_scale) == 0);

    /* Both child modes produce the same matrix here; their scale state must
     * nevertheless differ because it changes a grandchild's compensation. */
    const float child_scale[3] = {5, 6, 7};
    assert(melee_web_joint_transform(8, child_scale, turn_z, child_position, &parent, &child, error, sizeof error));
    assert(memcmp(child.accumulated_scale, parent_scale, sizeof parent_scale) == 0);
    assert(melee_web_joint_transform(0, one, turn_z, zero, &child, &grandchild, error, sizeof error));
    const float mixed[3][4] = {{-12, 0, 0, 7}, {0, -15, 0, 12}, {0, 0, 28, 19}};
    expect_matrix(&grandchild, mixed);
    assert(melee_web_joint_transform(0, child_scale, turn_z, child_position, &parent, &child, error, sizeof error));
    const float accumulated[3] = {10, 18, 28};
    assert(memcmp(child.accumulated_scale, accumulated, sizeof accumulated) == 0);
    assert(melee_web_joint_transform(0, one, turn_z, zero, &child, &grandchild, error, sizeof error));
    const float accumulated_expected[3][4] = {{-10, 0, 0, 7}, {0, -18, 0, 12}, {0, 0, 28, 19}};
    expect_matrix(&grandchild, accumulated_expected);

    MeleeWebJointTransform alias = child;
    assert(melee_web_joint_transform(0, one, turn_z, zero, &alias, &alias, error, sizeof error));
    assert(memcmp(&alias, &grandchild, sizeof alias) == 0);
}

static void check_rejections(void)
{
    char error[128];
    MeleeWebJointTransform output;
    memset(&output, 0x5a, sizeof output);
    const MeleeWebJointTransform before = output;
    const uint32_t unsupported[] = {0x20, 0x200, 0x1000, 0x2000, 0x4000,
                                   0x8000, 0x20000, 0x200000, 0x800000, 0x1000000,
                                   0x2000000, 0x80000000};
    for (size_t i = 0; i < sizeof unsupported / sizeof unsupported[0]; ++i) {
        assert(!melee_web_joint_transform(unsupported[i], one, zero, zero, NULL, &output, error, sizeof error));
        assert(strstr(error, "unsupported"));
    }
    float invalid[3] = {NAN, 1, 1};
    assert(!melee_web_joint_transform(0, invalid, zero, zero, NULL, &output, error, sizeof error));
    invalid[0] = INFINITY;
    assert(!melee_web_joint_transform(0, one, invalid, zero, NULL, &output, error, sizeof error));
    assert(!melee_web_joint_transform(0, one, zero, invalid, NULL, &output, error, sizeof error));
    assert(!melee_web_joint_transform(0, NULL, zero, zero, NULL, &output, error, sizeof error));
    assert(!melee_web_joint_transform(0, one, zero, zero, NULL, NULL, NULL, 0));

    MeleeWebJointTransform parent;
    assert(melee_web_joint_transform(0, one, zero, zero, NULL, &parent, error, sizeof error));
    parent.accumulated_scale[0] = 0;
    assert(!melee_web_joint_transform(0, one, zero, zero, &parent, &output, error, sizeof error));
    parent.accumulated_scale[0] = FLT_MAX;
    const float two[3] = {2, 2, 2};
    assert(!melee_web_joint_transform(0, two, zero, zero, &parent, &output, error, sizeof error));
    parent.has_accumulated_scale = 0;
    parent.matrix[0][0] = FLT_MAX;
    assert(!melee_web_joint_transform(8, two, zero, zero, &parent, &output, error, sizeof error));
    parent.matrix[0][0] = NAN;
    assert(!melee_web_joint_transform(8, one, zero, zero, &parent, &output, error, sizeof error));
    parent.has_accumulated_scale = 2;
    assert(!melee_web_joint_transform(8, one, zero, zero, &parent, &output, error, sizeof error));
    assert(memcmp(&output, &before, sizeof output) == 0);
}

static void check_view_matrix_overflow(void)
{
    MeleeWebJointTransform joint;
    char error[128];
    assert(melee_web_joint_transform(8, one, zero, zero, NULL, &joint, error, sizeof error));
    float camera[3][4] = {{2, 0, 0, 5}, {0, 3, 0, 6}, {0, 0, 4, 7}};
    float output[3][4];
    assert(melee_web_joint_view_matrix(camera, &joint, output, error, sizeof error));
    assert(error[0] == 0 && memcmp(camera, output, sizeof output) == 0);
    assert(melee_web_joint_view_matrix(camera, &joint, camera, error, sizeof error));
    assert(memcmp(camera, output, sizeof output) == 0);

    /* The accepted world bounds and camera can be finite even when their
     * product overflows: tiny local geometry plus a very large joint scale. */
    const float scale[3] = {1e38f, 1e38f, 1e38f};
    assert(melee_web_joint_transform(8, scale, zero, zero, NULL, &joint, error, sizeof error));
    camera[0][0] = camera[1][1] = camera[2][2] = 160;
    float before[3][4];
    memcpy(before, output, sizeof before);
    assert(!melee_web_joint_view_matrix(camera, &joint, output, error, sizeof error));
    assert(strstr(error, "nonfinite") && memcmp(output, before, sizeof output) == 0);
    camera[0][0] = NAN;
    assert(!melee_web_joint_view_matrix(camera, &joint, output, error, sizeof error));
    assert(memcmp(output, before, sizeof output) == 0);
}

static void check_normal_matrix(void)
{
    float matrix[3][4] = {{2, 0, 0, 5}, {0, 3, 0, 6}, {0, 0, 4, 7}};
    float output[3][4];
    const float expected[3][4] = {{.5f, 0, 0, 0}, {0, 1.f / 3.f, 0, 0}, {0, 0, .25f, 0}};
    char error[128];
    assert(melee_web_joint_normal_matrix(matrix, output, error, sizeof error));
    for (unsigned r = 0; r < 3; ++r) for (unsigned c = 0; c < 4; ++c)
        assert(fabsf(output[r][c] - expected[r][c]) < .00002f);
    assert(melee_web_joint_normal_matrix(matrix, matrix, error, sizeof error));
    assert(memcmp(matrix, output, sizeof output) == 0);
    matrix[0][0] = NAN;
    float before[3][4];
    memcpy(before, output, sizeof output);
    assert(!melee_web_joint_normal_matrix(matrix, output, error, sizeof error));
    assert(memcmp(before, output, sizeof output) == 0);
}

static void check_inspection_depth_order(void)
{
    float projection[4][4];
    char error[128];
    const float extent = 10, dimension = 10, distance = 21;
    assert(melee_web_inspection_projection(extent, dimension, distance,
                                           projection, error, sizeof error));
    assert(!error[0]);
    assert(fabsf(projection[0][0] - .12f) < .00001f);
    assert(fabsf(projection[1][1] - .16f) < .00001f);
    // The camera sits on +Z. Front and rear overlapping surfaces must retain
    // GX near=-1/far=0 ordering, before Aurora negates Z and maps LEQUAL to
    // GEQUAL. The former positive-Z projection let the rear surface win.
    const float front_view_z = -distance + dimension * .5f;
    const float rear_view_z = -distance - dimension * .5f;
    const float front_gx_z = projection[2][2] * front_view_z + projection[2][3];
    const float rear_gx_z = projection[2][2] * rear_view_z + projection[2][3];
    assert(front_gx_z < rear_gx_z && front_gx_z >= -1 && rear_gx_z <= 0);
    assert(fabsf(front_gx_z + .9f) < .00001f && fabsf(rear_gx_z + .1f) < .00001f);
    const float front_webgpu_depth = -front_gx_z, rear_webgpu_depth = -rear_gx_z;
    assert(front_webgpu_depth > rear_webgpu_depth);
    // Bounds too small to produce distinct float clipping planes must reject
    // before the SDK assertion, preserving the previous usable projection.
    float before[4][4];
    memcpy(before, projection, sizeof before);
    assert(!melee_web_inspection_projection(1e-30f, 1e-30f, 1,
                                            projection, error, sizeof error));
    assert(!melee_web_inspection_projection(0, dimension, distance,
                                            projection, error, sizeof error));
    assert(!melee_web_inspection_projection(10, FLT_MAX, FLT_MAX,
                                            projection, error, sizeof error));
    assert(memcmp(before, projection, sizeof before) == 0);
}

int main(void)
{
    check_root_and_rotation_order();
    check_scale_inheritance();
    check_rejections();
    check_view_matrix_overflow();
    check_normal_matrix();
    check_inspection_depth_order();
    puts("HSD original joint transform trace: passed");
    return 0;
}
