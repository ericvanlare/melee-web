#include "gameplay_ps_math.h"
#include "gameplay_fres.h"

#include <math.h>
#include <string.h>

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
