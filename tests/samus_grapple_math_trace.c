/* Synthetic scalar/chain regression, not a captured match or retail execution.
 * Compile the actual Samus, Link and HSD random translation units alongside it.
 * The oracle below transcribes the separately hash-bound GALE01r2 instructions;
 * it never calls the production normalizer or Link's fused inline adapter.
 */
#include <melee/it/kinds/itsamusgrapple.h>
#include <melee/it/kinds/itlinkhookshot.h>
#include <melee/mp/mplib.h>
#include <sysdolphin/baselib/random.h>
#include <dolphin/ppc_math.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned gravity_paths[3], clamp_paths[3], active_paths[4];
static unsigned split_differences;

static float word(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float single(double d) { volatile float f = (float)d; return f; }
static float adds(float a, float b) { return single((double)a + b); }
static float subs(float a, float b) { return single((double)a - b); }
static float muls(float a, float b) { return single((double)a * b); }
static float madds(float a, float c, float b)
{
    float fused = fmaf(a, c, b);
    split_differences += bits(fused) != bits(adds(muls(a, c), b));
    return fused;
}

/* This fixture selects no attached surface (line -1), matching mplib.c's
 * explicit false return. The real it_802A4454 still runs; its unreachable
 * moving-surface service fails loudly if entered. */
melee_source_bool mpLib_80054ED8(int line_id)
{
    assert(line_id == -1);
    return 0;
}
melee_source_bool mpGetSpeed(int line_id, Vec3* pos, Vec3* speed)
{
    (void)line_id; (void)pos; (void)speed;
    abort();
}

/* 802A3CD8..3CEC: three fmuls, then x²+y², then z²+(x²+y²).
 * 802A3CF8..3D40: frsqrte and three DOUBLE fnmsub refinements, frsp.
 * frsqrte is the existing SDK estimate primitive, shared with production;
 * this test does not independently validate that primitive's lookup table. */
static float ppc_normalize(const Vec3* a, const Vec3* b, Vec3* out)
{
    out->x = subs(a->x, b->x);
    out->y = subs(a->y, b->y);
    out->z = subs(a->z, b->z);
    float f2 = muls(out->x, out->x);
    float f3 = muls(out->z, out->z);
    float f1 = muls(out->y, out->y);
    f1 = adds(f2, f1);
    f1 = adds(f3, f1);
    if (f1 > 0.0f) {
        double estimate = frsqrte(f1);
        for (unsigned i = 0; i < 3; ++i) {
            double square = estimate * estimate;
            double half = 0.5 * estimate;
            double correction = -fma((double)f1, square, -3.0);
            estimate = half * correction;
        }
        f1 = single((double)f1 * estimate);
    }
    f2 = f1 == 0.0f ? 0.0f : single(1.0 / (double)f1);
    out->x = muls(out->x, f2);
    out->y = muls(out->y, f2);
    out->z = muls(out->z, f2);
    return f1;
}

static float ppc_randf(uint32_t* state)
{
    *state = *state * UINT32_C(214013) + UINT32_C(2531011);
    return (float)(*state >> 16) / 65536.0f;
}

/* 802B90A0 compares to DOUBLE 0.9; 90B4 is fnmsubs, 90C0 fmadds.
 * The caller's fsubs remains a separate single-precision operation. */
static float ppc_gravity(float velocity, uint32_t* state)
{
    if ((double)ppc_randf(state) <= 0.9) {
        ++gravity_paths[0];
        return 0.0f;
    }
    float random = ppc_randf(state);
    if (velocity < 0.0f) {
        ++gravity_paths[1];
        return -madds(0.6f, random, -velocity);
    }
    ++gravity_paths[2];
    return madds(0.6f, random, velocity);
}

static void ppc_place(Vec3* out, const Vec3* dir, float distance,
                      const Vec3* anchor)
{
    out->x = madds(dir->x, distance, anchor->x);
    out->y = madds(dir->y, distance, anchor->y);
    out->z = madds(dir->z, distance, anchor->z);
}

static void ppc_advance(ItemLink* link, uint32_t* state)
{
    link->vel.y = subs(link->vel.y, ppc_gravity(link->vel.y, state));
    link->pos.x = adds(link->pos.x, link->vel.x);
    link->pos.y = adds(link->pos.y, link->vel.y);
    link->pos.z = adds(link->pos.z, link->vel.z);
}

static void ppc_history(ItemLink* link)
{
    link->coll_data.last_pos = link->coll_data.cur_pos;
    link->coll_data.cur_pos = link->pos;
}

static void ppc_900c(ItemLink* link, Vec3* anchor,
                     itSamusGrappleAttributes* attr, float distance,
                     uint32_t* state)
{
    Vec3 direction;
    ppc_normalize(&link->pos, anchor, &direction);
    ppc_place(&link->pos, &direction, distance, anchor);
    for (ItemLink* previous = link->prev; previous; previous = link->prev) {
        ppc_advance(previous, state);
        /* 802B90E4 updates history BEFORE the segment constraint. */
        ppc_history(previous);
        float length = ppc_normalize(&previous->pos, &link->pos, &direction);
        unsigned branch = length > attr->x38 ? 0 : length < attr->x3C ? 1 : 2;
        ++clamp_paths[branch];
        if (branch != 2)
            ppc_place(&previous->pos, &direction,
                      branch == 0 ? attr->x38 : attr->x3C, &link->pos);
        link = previous;
    }
}

static int ppc_9fd4(ItemLink* link, Vec3* anchor,
                    itSamusGrappleAttributes* attr, uint32_t* state)
{
    assert(link->x1CC == -1); /* no surface; 802A4454 makes no change */
    for (ItemLink* next = link->next; next; next = link->next) {
        Vec3 direction;
        if (next->x2C_b0) {
            ppc_advance(next, state);
            float length = ppc_normalize(&next->pos, &link->pos, &direction);
            ++active_paths[length > attr->x38 ? 0 : 1];
            if (length > attr->x38)
                ppc_place(&next->pos, &direction, attr->x38, &link->pos);
            ppc_history(next); /* 802BA0D0: AFTER the constraint */
        } else {
            float length = ppc_normalize(anchor, &link->pos, &direction);
            ++active_paths[length > attr->x38 ? 2 : 3];
            if (length <= attr->x38)
                return 0;
            ppc_place(&next->pos, &direction, attr->x38, &link->pos);
            next->x2C_b0 = 1;
            next->coll_data.cur_pos = next->pos;
            next->coll_data.last_pos = next->pos;
        }
        link = next;
    }
    return 1;
}

static int same_vec(const char* group, unsigned n, unsigned index,
                    const char* field, Vec3 got, Vec3 expected, int report)
{
    float g[] = {got.x, got.y, got.z}, e[] = {expected.x, expected.y, expected.z};
    int same = 1;
    for (unsigned i = 0; i < 3; ++i) {
        if (bits(g[i]) == bits(e[i])) continue;
        if (report && same)
            printf("first %s case=%u link=%u %s.%c got=%08x expected=%08x\n",
                   group, n, index, field, "xyz"[i], bits(g[i]), bits(e[i]));
        same = 0;
    }
    return same;
}

static void make_chain(ItemLink* chain, unsigned n, unsigned mode)
{
    memset(chain, 0, 3 * sizeof(*chain));
    for (unsigned i = 0; i < 3; ++i) {
        ItemLink* forward = i == 2 ? NULL : &chain[i + 1];
        ItemLink* backward = i == 0 ? NULL : &chain[i - 1];
        chain[i].prev = mode < 4 ? forward : backward;
        chain[i].next = mode < 4 ? backward : forward;
        chain[i].pos = (Vec3){word(0x3f999980 + n + i * 17),
                              word(0x3fb33320 + n + i * 23),
                              word(0xbfaaaa80 + n + i * 29)};
        chain[i].vel = (Vec3){word(0x3d123450 + n),
                              (n & 1 ? -1.0f : 1.0f) * word(0x3e2aaaa0 + n),
                              word(0xbd987650 + n)};
        chain[i].coll_data.cur_pos = (Vec3){7.0f, 8.0f, 9.0f};
        chain[i].coll_data.last_pos = (Vec3){-7.0f, -8.0f, -9.0f};
        chain[i].x2C_b0 = mode < 6;
        chain[i].x2C_b1 = 1;
        chain[i].x2C_b2 = 1;
        chain[i].x1CC = -1;
    }
    if (mode == 0) chain[0].prev = NULL;
}

int main(void)
{
    unsigned total = 0, helper_differences = 0;
    const char* names[] = {"900c-anchor", "900c-max", "900c-min", "900c-free",
                          "9fd4-active-max", "9fd4-active-free",
                          "9fd4-activate", "9fd4-early-return"};
    for (unsigned mode = 0; mode < 8; ++mode) {
        unsigned differences = 0;
        for (unsigned n = 0; n < 128; ++n) {
            ItemLink actual[3], expected[3];
            make_chain(actual, n, mode);
            make_chain(expected, n, mode);
            Vec3 anchor = {word(0xbe923450 + n), word(0xc0c40000 + n),
                           word(0xc0380000 + n)};
            itSamusGrappleAttributes attr = {0};
            attr.x38 = (mode == 1 || mode == 4 || mode == 6) ? 0.3125f : 64.0f;
            attr.x3C = mode == 2 ? 12.0f : 0.0f;
            uint32_t expected_seed = UINT32_C(0x9e3779b9) * (n + 1);
            u32 actual_seed = expected_seed;
            seed_ptr = &actual_seed;
            Vec3 helper_actual, helper_expected;
            float helper_got = it_802A3C98(&actual[0].pos, &anchor, &helper_actual);
            float helper_want = ppc_normalize(&actual[0].pos, &anchor, &helper_expected);
            if (mode == 0 && n == 0)
                printf("minimal 900c input=3f999980/3fb33320/bfaaaa80 "
                       "anchor=be923450/c0c40000/c0380000 dist=3fa00001 "
                       "dir=%08x/%08x/%08x len=%08x\n",
                       bits(helper_expected.x), bits(helper_expected.y),
                       bits(helper_expected.z), bits(helper_want));
            helper_differences += bits(helper_got) != bits(helper_want) ||
                !same_vec("normalizer", n, 0, "dir", helper_actual, helper_expected,
                          helper_differences == 0);
            int got = 1, want = 1;
            if (mode < 4) {
                float distance = word(0x3fa00001 + n);
                it_802B900C(actual, &anchor, &attr, distance);
                ppc_900c(expected, &anchor, &attr, distance, &expected_seed);
            } else {
                got = it_802B9FD4(actual, &anchor, &attr);
                want = ppc_9fd4(expected, &anchor, &attr, &expected_seed);
            }
            assert(got == want && actual_seed == expected_seed);
            int same = 1;
            for (unsigned i = 0; i < 3; ++i) {
                assert(actual[i].x2C_b0 == expected[i].x2C_b0);
                assert(actual[i].x2C_b1 && actual[i].x2C_b2);
                same &= same_vec(names[mode], n, i, "pos", actual[i].pos,
                                 expected[i].pos, differences == 0 && same);
                same &= same_vec(names[mode], n, i, "vel", actual[i].vel,
                                 expected[i].vel, differences == 0 && same);
                same &= same_vec(names[mode], n, i, "cur", actual[i].coll_data.cur_pos,
                                 expected[i].coll_data.cur_pos, differences == 0 && same);
                same &= same_vec(names[mode], n, i, "last", actual[i].coll_data.last_pos,
                                 expected[i].coll_data.last_pos, differences == 0 && same);
            }
            differences += !same;
        }
        printf("%s cases=128 differences=%u\n", names[mode], differences);
        total += differences;
    }
    for (unsigned i = 0; i < 3; ++i) assert(gravity_paths[i] && clamp_paths[i]);
    for (unsigned i = 0; i < 4; ++i) assert(active_paths[i]);
    assert(split_differences > 0);
    printf("samus grapple cases=1024 source_differences=%u helper_differences=%u "
           "split_differences=%u gravity=%u/%u/%u\n", total, helper_differences,
           split_differences, gravity_paths[0], gravity_paths[1], gravity_paths[2]);
    return total != 0 || helper_differences != 0;
}
