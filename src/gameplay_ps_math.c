#include "gameplay_ps_math.h"
#include "gameplay_fres.h"

#include <dolphin/ppc_math.h>
#include <math.h>
#include <string.h>

void melee_web_ps_vec_normalize(const Vec* src, Vec* out)
{
    /* GALE01r2 PSVECNormalize (80342DB8): z*z is fused into the
     * rounded x*x lane, then the rounded y*y lane is added. */
    const float x = src->x, y = src->y, z = src->z;
    const float sum = fmaf(z, z, x * x) + y * y;
    const double estimate = frsqrte((double) sum);
    /* frsqrte retains more than binary32 precision. The following fmuls
     * rounds its FC operand to 25 significant bits before multiplication;
     * rounding only the product gives a different Newton step at ties.
     * A reciprocal-square-root estimate of a binary32 value cannot be a
     * binary64 subnormal, so this fixed mask covers its entire finite range. */
    uint64_t estimate_word;
    memcpy(&estimate_word, &estimate, sizeof(estimate_word));
    estimate_word = (estimate_word & UINT64_C(0xfffffffff8000000)) +
                    (estimate_word & UINT64_C(0x08000000));
    double multiplier;
    memcpy(&multiplier, &estimate_word, sizeof(multiplier));
    const float square = (float) (estimate * multiplier);
    const float half = (float) (estimate * 0.5);
    const float correction = -fmaf(square, sum, -3.0f);
    const float scale = correction * half;
    out->x = x * scale;
    out->y = y * scale;
    out->z = z * scale;
}

void melee_web_ps_vec_cross(const Vec* a, const Vec* b, Vec* out)
{
    /* GALE01r2 PSVECCrossProduct (80342E58). The second and third
     * lanes negate after the fused subtraction has rounded. */
    const Vec result = {
        fmaf(a->y, b->z, -(b->y * a->z)),
        -fmaf(a->x, b->z, -(b->x * a->z)),
        -fmaf(a->y, b->x, -(b->y * a->x))
    };
    *out = result;
}

void melee_web_mtx_look_at(Mtx out, const Vec* eye, const Vec* up,
                           const Vec* target)
{
    /* C_MTXLookAt in the owned executable (80342734) calls the PS
     * vector routines, then uses separate scalar products and sums. */
    Vec look = {eye->x - target->x, eye->y - target->y, eye->z - target->z};
    Vec right, vertical;
    melee_web_ps_vec_normalize(&look, &look);
    melee_web_ps_vec_cross(up, &look, &right);
    melee_web_ps_vec_normalize(&right, &right);
    melee_web_ps_vec_cross(&look, &right, &vertical);
    const Vec rows[3] = {right, vertical, look};
    for (int row = 0; row < 3; ++row) {
        out[row][0] = rows[row].x;
        out[row][1] = rows[row].y;
        out[row][2] = rows[row].z;
        out[row][3] = -((eye->z * rows[row].z) +
                       ((eye->x * rows[row].x) + (eye->y * rows[row].y)));
    }
}

void melee_web_ps_mtx_mult_vec(const Mtx m, const Vec* src, Vec* dst)
{
    const float x = src->x, y = src->y, z = src->z;
    float result[3];
    for (int row = 0; row < 3; ++row) {
        /* GALE01r2 80342AA8: ps_mul rounds x/y lanes separately,
         * ps_madd adds z/translation, then ps_sum0 adds the lanes. */
        float even = fmaf(m[row][2], z, m[row][0] * x);
        float odd = fmaf(m[row][3], 1.0f, m[row][1] * y);
        result[row] = even + odd;
    }
    dst->x = result[0];
    dst->y = result[1];
    dst->z = result[2];
}

void melee_web_ps_mtx_concat(const Mtx a, const Mtx b, Mtx out)
{
    Mtx tempMtx;

    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 4; ++column) {
            /* PSMTXConcat starts each paired-single lane with an ordinary
             * single-precision multiply. The next two instructions are
             * fused multiply-adds in the original order. */
            float accum = b[0][column] * a[row][0];
            accum = fmaf(b[1][column], a[row][1], accum);
            accum = fmaf(b[2][column], a[row][2], accum);
            if (column >= 2)
                accum = fmaf(column == 3 ? 1.0f : 0.0f, a[row][3], accum);
            tempMtx[row][column] = accum;
        }
    }

    /* The retail routine permits output aliasing either input. */
    memcpy(out, tempMtx, sizeof(tempMtx));
}

u32 melee_web_ps_mtx_inverse(const Mtx src, Mtx out)
{
    /* GALE01r2 PSMTXInverse (80342320): each cofactor starts with a
     * rounded multiply, followed by ps_msub. Keep the two original lanes
     * together so their determinant and translation order stays explicit. */
    const float a = src[0][0], b = src[0][1], c = src[0][2];
    const float d = src[1][0], e = src[1][1], f = src[1][2];
    const float g = src[2][0], h = src[2][1], i = src[2][2];
    const float tx = src[0][3], ty = src[1][3], tz = src[2][3];
    const float co0[2] = {fmaf(e, i, -(h * f)),
                          fmaf(f, g, -(i * d))};
    const float co1[2] = {fmaf(h, c, -(b * i)),
                          fmaf(i, a, -(c * g))};
    const float co2[2] = {fmaf(b, f, -(e * c)),
                          fmaf(c, d, -(f * a))};
    const float co3 = fmaf(d, h, -(e * g));
    const float co4 = fmaf(b, g, -(a * h));
    const float co5 = fmaf(a, e, -(b * d));
    const float det = fmaf(g, co2[0], fmaf(d, co1[0], a * co0[0]));
    if (det == 0.0f)
        return 0; /* The original leaves the destination untouched. */

    const float estimate = melee_web_fres(det);
    const float twice = estimate + estimate;
    const float square = estimate * estimate;
    const float reciprocal = -fmaf(det, square, -twice);
    Mtx result = {
        {co0[0] * reciprocal, co1[0] * reciprocal, co2[0] * reciprocal, 0},
        {co0[1] * reciprocal, co1[1] * reciprocal, co2[1] * reciprocal, 0},
        {co3 * reciprocal, co4 * reciprocal, co5 * reciprocal, 0}
    };
    for (int row = 0; row < 3; ++row)
        result[row][3] = -fmaf(result[row][2], tz,
                              fmaf(result[row][1], ty, result[row][0] * tx));
    /* All source words are consumed before stores, including in-place use. */
    memcpy(out, result, sizeof(result));
    return 1;
}

/* Scalar expansion of GALE01r2 PSMTXQuat, retaining every rounded
 * multiply, fused endpoint and reciprocal-estimate refinement. */
void melee_web_ps_mtx_quat(Mtx out, const Quaternion* q)
{
    const float x = q->x, y = q->y, z = q->z, w = q->w;
    const float xx = x * x, yy = y * y, zz = z * z;
    const float xx_zz = fmaf(z, z, xx);
    const float yy_ww = fmaf(w, w, yy);
    const float norm = xx_zz + yy_ww;
    const float estimate = melee_web_fres(norm);
    float scale = -fmaf(norm, estimate, -2.0f);
    scale = estimate * scale;
    scale = scale * 2.0f;

    const float yw = y * w, xw = x * w, zw = z * w;
    const float xy_zw = fmaf(x, y, zw);
    const float xy_mzw = fmaf(x, y, -zw);
    const float xz_yw = fmaf(x, z, yw);
    const float yz_xw = fmaf(y, z, xw);
    const float xz_myw = -fmaf(yw, 2.0f, -xz_yw);
    const float yz_mxw = -fmaf(xw, 2.0f, -yz_xw);
    Mtx m = {
        {-fmaf(zz + yy, scale, -1.0f), xy_mzw * scale, xz_yw * scale, 0.0f},
        {xy_zw * scale, -fmaf(xx_zz, scale, -1.0f), yz_mxw * scale, 0.0f},
        {xz_myw * scale, yz_xw * scale, -fmaf(xx + yy, scale, -1.0f), 0.0f}
    };
    memcpy(out, m, sizeof(m));
}
