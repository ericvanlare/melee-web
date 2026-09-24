#include "gameplay_cpu_r5_carry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail(const char* message)
{
    fprintf(stderr, "cpu-r5-carry: %s\n", message);
    return 1;
}

static int expected_low(uint32_t value)
{
    const int byte = (int)(value & 0xffu);
    return byte < 128 ? byte : byte - 256;
}

int main(void)
{
    /* These are synthetic relocated source identities, not retail captures. */
    const MeleeWebCpuSourceGlobalBinding seed = {
        .source_word = 0x811230a4u,
        .global_address = 0x81234010u,
        .profile_version = MELEE_WEB_CPU_SOURCE_PROFILE_VERSION,
        .global_id = MELEE_WEB_CPU_GLOBAL_SEED_PTR,
        .independently_derived = 1,
    };
    const MeleeWebCpuSourceFighterIdentity fighter = {
        .source_word = 0x80abc140u,
        .world_generation = 17,
        .live = 1,
    };
    char error[160];
    int8_t x = 91, y = -37;
    MeleeWebCpuR5Carry carry = {0};
    MeleeWebCpuR5Token token = {0};

    if (!melee_web_cpu_r5_begin(&carry, &fighter, &token, error, sizeof(error)))
        return fail(error);
    if (melee_web_cpu_r5_resolve_skipped(&carry, &token, &x, &y, error,
                                         sizeof(error)))
        return fail("unknown r5 was accepted before the source RNG boundary");
    if (x != 91 || y != -37)
        return fail("failed unresolved read changed the output bytes");

    if (!melee_web_cpu_r5_publish_seed_global(&carry, &token, &seed, error,
                                              sizeof(error)))
        return fail(error);
    if (!melee_web_cpu_r5_preserve(&carry, &token, error, sizeof(error)))
        return fail(error);
    if (!melee_web_cpu_r5_resolve_skipped(&carry, &token, &x, &y, error,
                                         sizeof(error)))
        return fail(error);
    if (x != expected_low(seed.source_word) || y != expected_low(fighter.source_word))
        return fail("proven source low-byte carry differs from its source words");

    const MeleeWebCpuWord old_r5 = melee_web_cpu_r5_word(&carry);
    if (!melee_web_cpu_r5_set_zero(&carry, &token, error, sizeof(error)))
        return fail(error);
    if (melee_web_cpu_r5_resolve_skipped(&carry, &token, &x, &y, error,
                                         sizeof(error)))
        return fail("explicit neutral zero was admitted as the seed route");
    if (melee_web_cpu_r5_word(&carry).kind != MELEE_WEB_CPU_WORD_ZERO)
        return fail("explicit zero lost its typed provenance");
    if (!melee_web_cpu_r5_mark_unknown(&carry, &token, error, sizeof(error)))
        return fail(error);
    if (melee_web_cpu_r5_word(&carry).known)
        return fail("unsupported clobber was not invalidated");

    if (!melee_web_cpu_r5_publish_seed_global(&carry, &token, &seed, error,
                                              sizeof(error)))
        return fail(error);
    MeleeWebCpuR5Token stale_token = token;
    stale_token.world_generation += 1;
    if (melee_web_cpu_r5_preserve(&carry, &stale_token, error, sizeof(error)))
        return fail("stale world generation was accepted");
    if (melee_web_cpu_r5_word(&carry).carry_lifetime != old_r5.carry_lifetime)
        return fail("lifetime changed during a rejected stale operation");

    if (!melee_web_cpu_r5_end(&carry, &token, error, sizeof(error)))
        return fail(error);
    if (melee_web_cpu_r5_word(&carry).known ||
        melee_web_cpu_r30_word(&carry).known)
        return fail("normal teardown retained a source word");

    const MeleeWebCpuSourceFighterIdentity replacement = {
        .source_word = fighter.source_word,
        .world_generation = fighter.world_generation,
        .live = 1,
    };
    MeleeWebCpuR5Token replacement_token = {0};
    if (!melee_web_cpu_r5_begin(&carry, &replacement, &replacement_token,
                                error, sizeof(error)))
        return fail(error);
    if (carry.carry_lifetime == old_r5.carry_lifetime)
        return fail("fighter lifetime was reused after teardown");
    if (melee_web_cpu_r5_publish_seed_global(&carry, &token, &seed, error,
                                             sizeof(error)))
        return fail("old token was accepted after same-world fighter reuse");
    if (!melee_web_cpu_r5_publish_seed_global(&carry, &replacement_token, &seed,
                                              error, sizeof(error)))
        return fail(error);
    melee_web_cpu_r5_discard(&carry);
    if (melee_web_cpu_r5_end(&carry, &replacement_token, error, sizeof(error)))
        return fail("discarded fighter lifetime accepted a later end");

    MeleeWebCpuSourceGlobalBinding bad = seed;
    bad.independently_derived = 0;
    MeleeWebCpuR5Carry rejected = {0};
    MeleeWebCpuR5Token rejected_token = {0};
    if (!melee_web_cpu_r5_begin(&rejected, &fighter, &rejected_token, error,
                                sizeof(error)))
        return fail(error);
    if (melee_web_cpu_r5_publish_seed_global(&rejected, &rejected_token, &bad,
                                             error, sizeof(error)))
        return fail("unattested seed binding was accepted");

    puts("cpu r5 carry trace: passed");
    return 0;
}
