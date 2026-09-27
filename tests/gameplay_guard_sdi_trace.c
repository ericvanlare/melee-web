/* Actual shield SDI callbacks, not a replacement gameplay implementation.
 * The arithmetic oracle follows the separately verified GALE01r2 instructions.
 */
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftCommon/ftCo_Guard.h>
#include <sysdolphin/baselib/gobj.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static ftCommonData common;
ftCommonData* p_ftCommonData = &common;
static Fighter fighter;
static HSD_GObj object;

static float word(uint32_t value) { float f; memcpy(&f, &value, 4); return f; }
static uint32_t bits(float f) { uint32_t value; memcpy(&value, &f, 4); return value; }

static Vec3 expected(int post_hitlag, int active)
{
    Vec3 result = fighter.cur_pos;
    if (active) {
        // Two fmuls round before the final X/Y fmadds. Do not reassociate.
        volatile float distance = fighter.input.lstick[0].x *
            (post_hitlag ? common.x4BC : common.sdi_pos_scale);
        volatile float scale = common.x4C0 * distance;
        result.x = fmaf(fighter.coll_data.floor.normal.y, scale, result.x);
        result.y = fmaf(-fighter.coll_data.floor.normal.x, scale, result.y);
    }
    return result;
}

int main(void)
{
    object.user_data = &fighter;
    // Authored PlCo.dat fields 4B0/4B4/4B8/4BC/4C0.
    common.sdi_min_stick_mag = 0.7f;
    common.sdi_stick_window = 4;
    common.sdi_pos_scale = 6.0f;
    common.x4BC = 3.0f;
    common.x4C0 = word(0x3f28f5c3);

    // Retained original B second-match tick8353 reduction. FD's authored
    // normalized floor is 3f7fffff, NOT the real number one. These are test
    // operands only; neither captures nor expected values feed the runtime.
    fighter.ground_or_air = GA_Ground;
    fighter.allow_sdi = 1;
    fighter.input.lstick[0].x = word(0xbf7e0000);
    fighter.cur_pos.x = word(0x41826d1b);
    fighter.cur_pos.y = word(0x38d1b717);
    fighter.coll_data.floor.normal.x = word(0x80000000);
    fighter.coll_data.floor.normal.y = word(0x3f7fffff);
    Vec3 oracle = expected(0, 1);
    assert(bits(oracle.x) == 0x4145fcc5);
    ftCo_80093240(&object);
    printf("shield SDI boundary x=%08x original=4145fcc5\n", bits(fighter.cur_pos.x));
    unsigned differences = bits(fighter.cur_pos.x) != bits(oracle.x);
    unsigned cases = 0, active_cases = 0;
    for (unsigned i = 0; i < 512; ++i) {
        for (int post = 0; post < 2; ++post) {
            memset(&fighter, 0, sizeof(fighter));
            fighter.ground_or_air = i % 11 == 0 ? GA_Air : GA_Ground;
            fighter.allow_sdi = i % 13 != 0;
            fighter.x670_timer_lstick_tilt_x = i % 7;
            fighter.input.lstick[0].x = i % 9 == 0 ? 0.5f :
                (i & 1 ? -1.0f : 1.0f) * (0.75f + (i % 31) / 128.0f);
            fighter.cur_pos.x = word(0x41234567 + i * 7919);
            fighter.cur_pos.y = -word(0x3fa98765 + i * 3571);
            fighter.cur_pos.z = 3.0f;
            fighter.coll_data.floor.normal.x = i % 3 ? 0.6f : word(0x80000000);
            fighter.coll_data.floor.normal.y = i % 3 ? 0.8f : word(0x3f7fffff);
            int active = fighter.ground_or_air == GA_Ground &&
                fabsf(fighter.input.lstick[0].x) >= common.sdi_min_stick_mag &&
                (post || (fighter.allow_sdi &&
                 fighter.x670_timer_lstick_tilt_x < common.sdi_stick_window));
            const unsigned timer = fighter.x670_timer_lstick_tilt_x;
            oracle = expected(post, active);
            if (post) ftCo_800932DC(&object); else ftCo_80093240(&object);
            differences += bits(fighter.cur_pos.x) != bits(oracle.x) ||
                bits(fighter.cur_pos.y) != bits(oracle.y) ||
                bits(fighter.cur_pos.z) != bits(oracle.z) ||
                fighter.x670_timer_lstick_tilt_x != (!post && active ? 254U : timer);
            ++cases; active_cases += active;
        }
    }
    printf("shield SDI cases=%u active=%u source_differences=%u\n",
           cases, active_cases, differences);
    return differences != 0;
}
