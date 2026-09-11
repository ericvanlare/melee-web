#include "gameplay_ps_math.h"

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
