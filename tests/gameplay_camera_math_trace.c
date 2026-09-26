#include "gameplay_ps_math.h"

#include <dolphin/ppc_math.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Independent source-instruction oracle.  Keep the original register lanes
 * explicit instead of calling the production ppc_rsqrte wrapper.  The vector
 * operations follow GALE01r2 PSVECNormalize/PSVECCrossProduct; the LookAt
 * scalar sums follow C_MTXLookAt at 80342734. */
static double oracle_round_fc_25(double value)
{
    int exponent;
    const double sign = value < 0.0 ? -1.0 : 1.0;
    const double mantissa = frexp(fabs(value), &exponent);
    const double scaled = mantissa * 33554432.0; /* 2^25 */
    uint64_t lower = (uint64_t)scaled;
    if (scaled - (double)lower >= 0.5)
        ++lower; /* PPC's positive FC operand ties round upward. */
    return sign * ldexp((double)lower / 33554432.0, exponent);
}

static float oracle_rsqrte(float value, int round_fc_operand)
{
    /* ppc_rsqrte is frsqrte followed by one Newton-Raphson step.  The true
     * path rounds the estimate operand before its square; naming the
     * intermediate registers here makes both paths visible while avoiding
     * the production helper symbol. */
    const double estimate = frsqrte((double)value);
    const double multiplier =
        round_fc_operand ? oracle_round_fc_25(estimate) : estimate;
    float nwork0 = (float)(estimate * multiplier);
    const float nwork1 = (float)(estimate * 0.5);
    nwork0 = fmaf(-nwork0, value, 3.0f);
    return nwork0 * nwork1;
}

static void oracle_normalize_mode(const Vec* src, Vec* out,
                                  int round_fc_operand)
{
    const float lane_x = src->x * src->x;
    const float lane_z = fmaf(src->z, src->z, lane_x);
    const float lane_y = src->y * src->y;
    const float sqsum = lane_z + lane_y;
    const float rsqrt = oracle_rsqrte(sqsum, round_fc_operand);
    const float lane_out_x = src->x * rsqrt;
    const float lane_out_y = src->y * rsqrt;
    const float lane_out_z = src->z * rsqrt;
    out->x = lane_out_x;
    out->y = lane_out_y;
    out->z = lane_out_z;
}

static void oracle_normalize(const Vec* src, Vec* out)
{
    oracle_normalize_mode(src, out, 1);
}

static void oracle_normalize_old_estimate_square(const Vec* src, Vec* out)
{
    oracle_normalize_mode(src, out, 0);
}

static void oracle_cross(const Vec* a, const Vec* b, Vec* out)
{
    const float lane_x_subtrahend = b->y * a->z;
    const float lane_x = fmaf(a->y, b->z, -lane_x_subtrahend);
    const float lane_y_subtrahend = b->x * a->z;
    const float lane_y = -fmaf(a->x, b->z, -lane_y_subtrahend);
    const float lane_z_subtrahend = b->y * a->x;
    const float lane_z = -fmaf(a->y, b->x, -lane_z_subtrahend);
    out->x = lane_x;
    out->y = lane_y;
    out->z = lane_z;
}

static void oracle_look_at(Mtx out, const Vec* eye, const Vec* up,
                           const Vec* target)
{
    Vec look = {eye->x - target->x, eye->y - target->y, eye->z - target->z};
    Vec right, vertical;
    oracle_normalize(&look, &look);
    oracle_cross(up, &look, &right);
    oracle_normalize(&right, &right);
    oracle_cross(&look, &right, &vertical);
    const Vec rows[3] = {right, vertical, look};
    for (int row = 0; row < 3; ++row) {
        out[row][0] = rows[row].x;
        out[row][1] = rows[row].y;
        out[row][2] = rows[row].z;
        out[row][3] = -((eye->z * rows[row].z) +
                        ((eye->x * rows[row].x) + (eye->y * rows[row].y)));
    }
}

/* Negative control: ordinary scalar source replacements must not accidentally
 * pass the paired-single bit test. */
static void fallback_normalize(const Vec* src, Vec* out)
{
    const float length = sqrtf(src->x * src->x + src->y * src->y + src->z * src->z);
    out->x = src->x / length;
    out->y = src->y / length;
    out->z = src->z / length;
}

static void fallback_cross(const Vec* a, const Vec* b, Vec* out)
{
    Vec result = {a->y * b->z - a->z * b->y,
                  a->z * b->x - a->x * b->z,
                  a->x * b->y - a->y * b->x};
    *out = result;
}

static void fallback_look_at(Mtx out, const Vec* eye, const Vec* up,
                             const Vec* target)
{
    Vec look = {eye->x - target->x, eye->y - target->y, eye->z - target->z};
    Vec right, vertical;
    fallback_normalize(&look, &look);
    fallback_cross(up, &look, &right);
    fallback_normalize(&right, &right);
    fallback_cross(&look, &right, &vertical);
    const Vec rows[3] = {right, vertical, look};
    for (int row = 0; row < 3; ++row) {
        out[row][0] = rows[row].x;
        out[row][1] = rows[row].y;
        out[row][2] = rows[row].z;
        out[row][3] = -((eye->z * rows[row].z) +
                        ((eye->x * rows[row].x) + (eye->y * rows[row].y)));
    }
}

/* Generated finite corpus: varied signs, exponents, and mantissas. */
static const Vec vector_corpus[64] = {
    {-0x1.a0d212abe3f52p+2f, -0x1.c9e4ba75df6b7p+2f, 0x1.977eb4268073cp+1f},
    {0x1.7c982cfca6678p+2f, 0x1.1d5b650ffe9d2p+3f, 0x1.4f6c371a51b92p+3f},
    {-0x1.7bb60551001f0p-1f, 0x1.2377ff181c418p+3f, -0x1.d6938fc04e5c0p+1f},
    {-0x1.c914c40e0cc11p+2f, 0x1.3ccc54c8cca3ap+2f, -0x1.4734d28745fc6p+3f},
    {-0x1.eddf50a2c716cp+2f, 0x1.56b341f3c5f98p+3f, -0x1.97f0ab3261600p+2f},
    {0x1.471404f432278p+2f, 0x1.266e8fd3cb37ap+2f, 0x1.29c688da83220p+1f},
    {0x1.337d753bea444p+3f, -0x1.032e0b8f50f60p-1f, -0x1.b588a0a82a9e0p+0f},
    {-0x1.81022049e5205p+2f, -0x1.1f356b99b3db2p+2f, -0x1.1a970a68d10ccp+1f},
    {0x1.9b50caf680874p+2f, -0x1.9f8dd5667afc0p+0f, -0x1.029584a38de46p+2f},
    {-0x1.3f7212a926a57p+2f, 0x1.4bee09de59fc8p+3f, -0x1.0694874fffe4dp+3f},
    {0x1.d37de90d330f8p+1f, -0x1.941fd7d16f071p+2f, 0x1.cacfc5b6d5d44p+1f},
    {-0x1.246a351bb5500p+1f, -0x1.18ce3416f9a60p+0f, -0x1.dc4e6fa23d1c0p-2f},
    {-0x1.b113980badc03p+2f, 0x1.466e67fdf86a6p+3f, 0x1.19ebdc7296158p+3f},
    {0x1.2467dbefbaa0cp+3f, 0x1.452b3590c055cp+1f, -0x1.62fbd1e1210c4p+1f},
    {0x1.6a96073b0f1b8p+2f, 0x1.da4b34c079ee8p+1f, -0x1.6bf94a4934967p+2f},
    {-0x1.28e491db831f8p+3f, 0x1.541fa67bc9e08p+2f, -0x1.ccd8160fef5a0p+2f},
    {-0x1.7eebf665e89e0p-2f, -0x1.f53ed411169c3p+2f, 0x1.267e125f1ec38p+3f},
    {0x1.1e88f356a0decp+3f, 0x1.c1a6bd0c55eacp+2f, 0x1.115ff39889b68p+3f},
    {-0x1.39e82ddc5c162p+3f, -0x1.07043f30ba8fcp+1f, -0x1.fddd3bea12732p+1f},
    {0x1.e12700022a078p+2f, -0x1.05e7a2cc57280p+0f, -0x1.9dbfd8db7edd4p+2f},
    {0x1.1e7fe4f25209ap+3f, -0x1.70c6f783fe5e4p+2f, 0x1.8e0172f6fdc78p+1f},
    {-0x1.2cbeea3a8528ep+3f, -0x1.00323db7e67f6p+3f, -0x1.e2c709b511c80p-3f},
    {0x1.ad18932dfc1f0p+1f, -0x1.009390401add0p+3f, -0x1.2e64d57504a63p+2f},
    {-0x1.6ae36b41e77f4p+2f, -0x1.07d13440ea575p+3f, 0x1.2e763cc1ae73ap+3f},
    {-0x1.f163b2aae0fd8p+0f, -0x1.cf3bab41dcf98p+0f, -0x1.8bf121549bbd4p+1f},
    {-0x1.42372716140f9p+3f, 0x1.4736e65c5d152p+3f, 0x1.08dc17964fcccp+3f},
    {0x1.2baef7fbccee0p-1f, -0x1.8afccf1411f20p+2f, 0x1.2e3fd97faf0a0p+3f},
    {-0x1.1cbb2a411489bp+2f, 0x1.a06543e16b290p+1f, 0x1.744dd42bb9690p+0f},
    {-0x1.add3727af7118p+1f, 0x1.3ff9739f2717cp+2f, -0x1.be0928ea98b16p+1f},
    {0x1.dfcc9f5d7cd78p+2f, 0x1.4dd160372e9d4p+2f, -0x1.4c062ccd8513cp+3f},
    {-0x1.1607a02d1a19fp+2f, -0x1.e415b75a34f70p-1f, -0x1.dd5362e676e5fp+2f},
    {0x1.4a489f090a5d4p+2f, -0x1.4167e052cdebap+3f, -0x1.31294d52bf16dp+2f},
    {-0x1.131746af2a071p+3f, -0x1.517a85c6c37d0p+0f, -0x1.271b88a2163ccp+1f},
    {-0x1.76aae310ef3a8p+1f, 0x1.c21434dc4ff50p+0f, -0x1.ac7fe03c560fdp+2f},
    {-0x1.3f5a8f8e1c918p+3f, 0x1.5534d092e7958p+0f, -0x1.186982bf88560p-1f},
    {-0x1.26a4535bfc864p+3f, -0x1.643d6bfde4d51p+2f, 0x1.33a4cbbed3d48p+2f},
    {-0x1.8a2b4b5d05810p+0f, 0x1.d15223622bff0p-1f, -0x1.ad750ce3a9b10p-1f},
    {0x1.15bac885d3740p+3f, -0x1.1e6c0ffbc6c62p+2f, -0x1.01e1ab4be2704p+3f},
    {0x1.b418dbcfe5570p+1f, -0x1.46c26a44a92bep+3f, 0x1.2d857792eebd8p+2f},
    {-0x1.64cda425b5523p+2f, 0x1.e9d8818092540p+2f, 0x1.206dfd572374cp+2f},
    {0x1.5452df44f41a8p+3f, -0x1.532a6bc8f5688p+0f, 0x1.823ea4d14a610p+0f},
    {0x1.3130782a91f5cp+1f, -0x1.2444f840d0f4ap+2f, -0x1.e135d43e25228p+1f},
    {0x1.87eebfaf40950p+1f, 0x1.7740c59a26aa0p+2f, -0x1.452a983d7a864p+3f},
    {0x1.a3a9f1c2c413cp+2f, 0x1.527e2d1e7e74cp+2f, -0x1.639a47c84869cp+1f},
    {-0x1.4a3e7acec9b9cp+2f, 0x1.c57f599555428p+0f, -0x1.5815dcfed58e0p+3f},
    {-0x1.3a9bc22ff1716p+2f, 0x1.722f93d40ef0cp+2f, -0x1.df9dd7418fcf2p+2f},
    {-0x1.30db5dd431890p+0f, 0x1.58662e5c1f0ccp+2f, -0x1.a2948e95fc154p+1f},
    {0x1.f74a0349766f0p+2f, 0x1.18aec8fd7e7dap+3f, -0x1.7ee31bb878db0p+1f},
    {0x1.7c578268d1f90p-1f, -0x1.1cffbb693639dp+2f, 0x1.f36181f39ed68p+0f},
    {-0x1.54dd0df3e3643p+3f, 0x1.acf29beb65ae0p+1f, -0x1.d12225e184168p+2f},
    {-0x1.ecef3fc925926p+2f, 0x1.4820ca8179b0cp+3f, 0x1.109e295958460p+2f},
    {0x1.59d7d9cde3982p+3f, 0x1.c4d33bcc5e298p+0f, -0x1.2ccd673a7fad2p+2f},
    {-0x1.79406391b7200p-1f, -0x1.51d41b421e446p+3f, 0x1.2bd7c7de92a68p+1f},
    {0x1.2fcb237dd4d4ep+3f, 0x1.2f14e5af67200p-2f, -0x1.f3281f8eb673cp+2f},
    {0x1.6be3572aba41cp+2f, -0x1.9dfe872e95ab0p-1f, -0x1.a83097806ccf8p+1f},
    {-0x1.e518269816fd8p+2f, 0x1.8ef20a80c7864p+2f, 0x1.6fc53024b04a0p+0f},
    {0x1.32508b8b7f688p+0f, 0x1.1002070058fdep+3f, -0x1.12015d14d17e2p+2f},
    {-0x1.1dd3eced3ff87p+3f, -0x1.182ef485e076ep+3f, -0x1.84ed14c5786b6p+1f},
    {0x1.6f71639875924p+2f, -0x1.6bff44237873bp+2f, 0x1.b3f4f8a317e08p+2f},
    {0x1.63ca602a65000p-2f, -0x1.39199f7ad1fcbp+2f, -0x1.7bc3699f1b6b4p+2f},
    {0x1.32e7cc9cd4b70p+1f, 0x1.28bbb427242f6p+2f, -0x1.3567e0673d778p+1f},
    {0x1.0d59db15d0442p+2f, -0x1.f2cdaca315680p-1f, -0x1.36a4b55cd6838p+3f},
    {0x1.b2e16a5323d48p+2f, 0x1.9cc8befebb870p+2f, -0x1.1fd9012b2133ap+2f},
    {0x1.476bb365c2f90p+3f, -0x1.6dbb47c97e1e3p+2f, -0x1.6767891d94abcp+1f},
};
static int same_vec(const Vec* a, const Vec* b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static int same_mtx(const Mtx a, const Mtx b)
{
    return memcmp(a, b, sizeof(Mtx)) == 0;
}

static Mtx allocated_inverse;
static unsigned allocations;

void* HSD_MtxAlloc(void)
{
    ++allocations;
    return &allocated_inverse;
}

void HSD_WObjGetPosition(HSD_WObj* object, Vec3* position)
{
    *position = object->pos;
}

static void check_vectors(unsigned* fallback_differences)
{
    const unsigned corpus_count = sizeof(vector_corpus) / sizeof(vector_corpus[0]);
    unsigned normalize_fallback_differences = 0;
    unsigned cross_fallback_differences = 0;
    unsigned old_estimate_differences = 0;
    for (unsigned i = 0; i < corpus_count; ++i) {
        Vec expected, actual, alias;
        oracle_normalize(&vector_corpus[i], &expected);
        Vec old_estimate;
        oracle_normalize_old_estimate_square(&vector_corpus[i], &old_estimate);
        old_estimate_differences += !same_vec(&expected, &old_estimate);
        melee_web_ps_vec_normalize(&vector_corpus[i], &actual);
        assert(same_vec(&expected, &actual));
        Vec fallback;
        fallback_normalize(&vector_corpus[i], &fallback);
        normalize_fallback_differences += !same_vec(&expected, &fallback);
        if (i < 4) {
            alias = vector_corpus[i];
            melee_web_ps_vec_normalize(&alias, &alias);
            assert(same_vec(&expected, &alias));
        }
    }
    for (unsigned i = 0; i < corpus_count; ++i) {
        const unsigned paired = (37u * i + 11u) % corpus_count;
        Vec expected, actual, alias_a, alias_b;
        oracle_cross(&vector_corpus[i], &vector_corpus[paired], &expected);
        melee_web_ps_vec_cross(&vector_corpus[i], &vector_corpus[paired], &actual);
        assert(same_vec(&expected, &actual));
        Vec fallback;
        fallback_cross(&vector_corpus[i], &vector_corpus[paired], &fallback);
        cross_fallback_differences += !same_vec(&expected, &fallback);
        if (i < 2) {
            alias_a = vector_corpus[i];
            melee_web_ps_vec_cross(&alias_a, &vector_corpus[paired], &alias_a);
            assert(same_vec(&expected, &alias_a));
            alias_b = vector_corpus[paired];
            melee_web_ps_vec_cross(&vector_corpus[i], &alias_b, &alias_b);
            assert(same_vec(&expected, &alias_b));
        }
    }
    {
        const Vec normalize_control = vector_corpus[0];
        Vec expected, fallback;
        oracle_normalize(&normalize_control, &expected);
        fallback_normalize(&normalize_control, &fallback);
        assert(!same_vec(&expected, &fallback));
        ++normalize_fallback_differences;
    }
    {
        const Vec cross_control_a = vector_corpus[0];
        const Vec cross_control_b = vector_corpus[11];
        Vec expected, fallback;
        oracle_cross(&cross_control_a, &cross_control_b, &expected);
        fallback_cross(&cross_control_a, &cross_control_b, &fallback);
        assert(!same_vec(&expected, &fallback));
        ++cross_fallback_differences;
    }
    assert(normalize_fallback_differences > 0);
    assert(cross_fallback_differences > 0);
    assert(old_estimate_differences > 0);
    *fallback_differences += normalize_fallback_differences +
                             cross_fallback_differences + old_estimate_differences;
}

static void oracle_roll_zero_up(const Vec* eye_position, const Vec* target,
                                Vec* up)
{
    Vec eye = {target->x - eye_position->x, target->y - eye_position->y,
               target->z - eye_position->z};
    Vec v0;
    oracle_normalize(&eye, &eye);
    if (1.0f - fabsf(eye.y) < 0.0001f) {
        v0.x = sqrtf(eye.y * eye.y + eye.z * eye.z);
        v0.y = eye.y * (-eye.x / v0.x);
        v0.z = eye.z * (-eye.x / v0.x);
    } else {
        v0.y = sqrtf(eye.x * eye.x + eye.z * eye.z);
        v0.x = eye.x * (-eye.y / v0.y);
        v0.z = eye.z * (-eye.y / v0.y);
    }
    /* C_MTXRotAxisRad(..., -0) followed by C_MTXMultVecSR is the identity
     * for this finite synthetic vector.  The source then VECNormalizes v1. */
    oracle_normalize(&v0, up);
}

static void check_camera(unsigned* fallback_differences)
{
    const Vec eye = {13.125f, -4.75f, 27.5f};
    const Vec up = {0.125f, 1.0f, -0.0625f};
    const Vec target = {-2.25f, 3.5f, 1.75f};
    Mtx expected_view, actual_view, expected_inverse;
    oracle_look_at(expected_view, &eye, &up, &target);
    melee_web_mtx_look_at(actual_view, &eye, &up, &target);
    assert(same_mtx(expected_view, actual_view));
    Mtx fallback_view;
    fallback_look_at(fallback_view, &eye, &up, &target);
    *fallback_differences += !same_mtx(expected_view, fallback_view);

    assert(melee_web_ps_mtx_inverse(expected_view, expected_inverse));
    HSD_CObj camera = {0};
    HSD_WObj eye_object = {0};
    HSD_WObj target_object = {0};
    eye_object.pos = eye;
    target_object.pos = target;
    camera.eyepos = &eye_object;
    camera.interest = &target_object;
    camera.u.up = up;
    camera.flags = (1u << 30) | 1u;
    allocations = 0;
    MtxPtr actual_inverse = HSD_CObjGetInvViewingMtxPtr(&camera);
    assert(allocations == 1);
    assert(same_mtx(camera.view_mtx, expected_view));
    assert(same_mtx(actual_inverse, expected_inverse));
    assert((camera.flags & ((1u << 30) | (1u << 31))) == 0);
    camera.view_mtx[0][0] += 1.0f;
    assert(HSD_CObjGetInvViewingMtxPtrDirect(&camera) == actual_inverse);
    assert(allocations == 1);
    assert(same_mtx(actual_inverse, expected_inverse));
}

static void check_roll_zero_camera(void)
{
    const Vec eye = {-8.375f, 6.25f, 19.875f};
    const Vec target = {2.625f, -1.75f, -3.5f};
    Vec expected_eye = {target.x - eye.x, target.y - eye.y, target.z - eye.z};
    Vec actual_eye;
    Vec expected_up;
    Vec actual_up;
    oracle_normalize(&expected_eye, &expected_eye);
    oracle_roll_zero_up(&eye, &target, &expected_up);

    HSD_CObj camera = {0};
    HSD_WObj eye_object = {0};
    HSD_WObj target_object = {0};
    eye_object.pos = eye;
    target_object.pos = target;
    camera.eyepos = &eye_object;
    camera.interest = &target_object;
    camera.u.roll = 0.0f;
    camera.flags = 1u << 30;

    assert(HSD_CObjGetEyeVector(&camera, &actual_eye) == 0);
    assert(same_vec(&expected_eye, &actual_eye));
    assert(HSD_CObjGetUpVector(&camera, &actual_up) == 0);
    assert(same_vec(&expected_up, &actual_up));

    Mtx expected_view, expected_inverse;
    oracle_look_at(expected_view, &eye, &expected_up, &target);
    assert(melee_web_ps_mtx_inverse(expected_view, expected_inverse));
    allocations = 0;
    MtxPtr actual_inverse = HSD_CObjGetInvViewingMtxPtr(&camera);
    assert(allocations == 1);
    assert(same_mtx(camera.view_mtx, expected_view));
    assert(same_mtx(actual_inverse, expected_inverse));
    assert((camera.flags & ((1u << 30) | (1u << 31))) == 0);
}

int main(void)
{
    unsigned fallback_differences = 0;
    check_vectors(&fallback_differences);
    check_camera(&fallback_differences);
    check_roll_zero_camera();
    assert(fallback_differences > 0);
    printf("camera-math vectors=64 cross=64 aliases=8 cameras=2 fallback_differences=%u\n",
           fallback_differences);
}
