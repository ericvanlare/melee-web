/* Camera corner and position vectors exercising GALE01r2 lbVector_Rotate
 * instruction order. Full camera behavior is separately retail-compared. */
#include "melee/lb/lbvector.h"
#include "gameplay_trig.h"
#include <stdio.h>

static void show(const Vec3* vector)
{
    printf("%08x %08x %08x\n", melee_web_trig_bits(vector->x),
           melee_web_trig_bits(vector->y), melee_web_trig_bits(vector->z));
}

int main(void)
{
    Vec3 corner = { 0, 0, -1 };
    const float half_fov = melee_web_trig_from_bits(0x3e860a92);
    lbVector_Rotate(&corner, 1, half_fov);
    show(&corner);
    lbVector_Rotate(&corner, 2, half_fov);
    show(&corner);
    for (int axis = 1; axis <= 4; axis *= 2) {
        Vec3 vector = {
            melee_web_trig_from_bits(0x42a5daa7),
            melee_web_trig_from_bits(0x4242396c),
            melee_web_trig_from_bits(0x43321630),
        };
        lbVector_Rotate(&vector, axis, -half_fov);
        show(&vector);
    }
    return 0;
}
