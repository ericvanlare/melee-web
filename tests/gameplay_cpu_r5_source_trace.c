#include "gameplay_cpu_r5_carry.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Keep the test translation unit on the host-compatible side of the source
 * ABI. random_wrapper.c includes the existing source compatibility header and
 * the untouched random.c body; these declarations only cross that link. */
extern uint32_t* seed_ptr;
extern float HSD_Randf(void);

static uint32_t parse_word(const char* text)
{
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 0);
    if (!text[0] || !end || *end || value > UINT32_MAX)
        abort();
    return (uint32_t)value;
}

static int expected_low(uint32_t value)
{
    const int byte = (int)(value & 0xffu);
    return byte < 128 ? byte : byte - 256;
}

int main(int argc, char** argv)
{
    if (argc != 4)
        return 2;
    const MeleeWebCpuSourceGlobalBinding seed = {
        .source_word = parse_word(argv[1]),
        .global_address = parse_word(argv[2]),
        .profile_version = MELEE_WEB_CPU_SOURCE_PROFILE_VERSION,
        .global_id = MELEE_WEB_CPU_GLOBAL_SEED_PTR,
        .independently_derived = 1,
    };
    const MeleeWebCpuSourceFighterIdentity fighter = {
        .low_byte = (int8_t)expected_low(parse_word(argv[3])),
        .host_owner = 1,
        .world_generation = 1,
        .allocation_generation = 1,
        .live = 1,
    };
    char error[160];
    MeleeWebCpuR5Carry carry = {0};
    MeleeWebCpuR5Token token = {0};
    if (!melee_web_cpu_r5_begin(&carry, &fighter, &token, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 3;
    }

    const uint32_t before = *seed_ptr;
    (void)HSD_Randf();
    if (*seed_ptr == before) {
        fputs("original HSD_Randf did not update seed_ptr\n", stderr);
        return 4;
    }
    /* This is the source call-site adapter boundary: after the audited
     * HSD_Randf call, the DOL/SDA profile publishes seed_ptr's source value.
     * It does not inspect the host address of seed_ptr. */
    if (!melee_web_cpu_r5_publish_seed_global(&carry, &token, &seed, error,
                                              sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 5;
    }
    int8_t x = 0, y = 0;
    if (!melee_web_cpu_r5_resolve_skipped(&carry, &token, &x, &y, error,
                                         sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 6;
    }
    if (x != expected_low(seed.source_word) || y != fighter.low_byte) {
        fputs("source seed/fighter carry did not reach the consumer bytes\n", stderr);
        return 7;
    }
    puts("original random source -> cpu carry: passed");
    return 0;
}
