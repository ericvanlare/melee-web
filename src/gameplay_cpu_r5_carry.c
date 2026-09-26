#include "gameplay_cpu_r5_carry.h"

#include <stdio.h>
#include <string.h>

static int fail(char* error, size_t size, const char* message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

static int success(char* error, size_t size)
{
    if (error && size) error[0] = '\0';
    return 1;
}

static int source_mem1_word(uint32_t value)
{
    return value >= 0x80000000u && value < 0x81800000u && (value & 3u) == 0;
}

static int signed_low_byte(uint32_t value)
{
    const int byte = (int)(value & 0xffu);
    return byte < 128 ? byte : byte - 256;
}

static void clear_word(MeleeWebCpuWord* word)
{
    memset(word, 0, sizeof(*word));
    word->kind = MELEE_WEB_CPU_WORD_UNKNOWN;
}

static int owner_matches(const MeleeWebCpuR5Carry* carry,
                         const MeleeWebCpuR5Token* token, char* error,
                         size_t size)
{
    if (!carry || !carry->active || !token || !token->valid)
        return fail(error, size, "CPU carry has no live source fighter");
    if (!token->host_owner || carry->host_owner != token->host_owner)
        return fail(error, size, "CPU carry host owner changed");
    if (!token->world_generation || carry->world_generation != token->world_generation)
        return fail(error, size, "CPU carry world generation is stale");
    if (!token->allocation_generation ||
        carry->allocation_generation != token->allocation_generation)
        return fail(error, size, "CPU carry allocation generation is stale");
    if (!token->carry_lifetime || carry->carry_lifetime != token->carry_lifetime)
        return fail(error, size, "CPU carry lifetime token is stale");
    if (!carry->r30.known || carry->r30.kind != MELEE_WEB_CPU_WORD_SOURCE_FIGHTER ||
        carry->r30.host_owner != token->host_owner ||
        carry->r30.world_generation != token->world_generation ||
        carry->r30.allocation_generation != token->allocation_generation ||
        carry->r30.carry_lifetime != carry->carry_lifetime)
        return fail(error, size, "CPU carry has no current live r30 fighter identity");
    return 1;
}

static void set_word(MeleeWebCpuWord* word, int8_t low_byte,
                     uint64_t host_owner, uint64_t world_generation,
                     uint64_t allocation_generation, uint64_t carry_lifetime,
                     MeleeWebCpuWordKind kind, MeleeWebCpuGlobalId source_id)
{
    word->low_byte = low_byte;
    word->host_owner = host_owner;
    word->world_generation = world_generation;
    word->allocation_generation = allocation_generation;
    word->carry_lifetime = carry_lifetime;
    word->known = 1;
    word->kind = (uint8_t)kind;
    word->source_id = (uint8_t)source_id;
    memset(word->reserved, 0, sizeof(word->reserved));
}

int melee_web_cpu_r5_begin(MeleeWebCpuR5Carry* carry,
                           const MeleeWebCpuSourceFighterIdentity* fighter,
                           MeleeWebCpuR5Token* token,
                           char* error, size_t size)
{
    if (!carry || !fighter || !token)
        return fail(error, size, "CPU carry requires a fighter identity and owner token");
    if (carry->active)
        return fail(error, size, "CPU carry already has a live source fighter");
    if (!fighter->live || !fighter->host_owner || !fighter->world_generation ||
        !fighter->allocation_generation ||
        carry->carry_lifetime == UINT64_MAX)
        return fail(error, size, "CPU carry requires a live fighter allocation identity");

    ++carry->carry_lifetime;
    carry->host_owner = fighter->host_owner;
    carry->world_generation = fighter->world_generation;
    carry->allocation_generation = fighter->allocation_generation;
    carry->active = 1;
    memset(token, 0, sizeof(*token));
    token->host_owner = fighter->host_owner;
    token->world_generation = fighter->world_generation;
    token->allocation_generation = fighter->allocation_generation;
    token->carry_lifetime = carry->carry_lifetime;
    token->valid = 1;
    clear_word(&carry->r5);
    set_word(&carry->r30, fighter->low_byte, fighter->host_owner,
             fighter->world_generation, fighter->allocation_generation,
             carry->carry_lifetime,
             MELEE_WEB_CPU_WORD_SOURCE_FIGHTER,
             MELEE_WEB_CPU_GLOBAL_NONE);
    return success(error, size);
}

int melee_web_cpu_r5_publish_seed_global(
    MeleeWebCpuR5Carry* carry, const MeleeWebCpuR5Token* token,
    const MeleeWebCpuSourceGlobalBinding* binding, char* error, size_t size)
{
    if (!carry || !binding)
        return fail(error, size, "CPU carry requires a seed global binding");
    if (!owner_matches(carry, token, error, size))
        return 0;
    if (binding->profile_version != MELEE_WEB_CPU_SOURCE_PROFILE_VERSION ||
        binding->global_id != MELEE_WEB_CPU_GLOBAL_SEED_PTR ||
        !binding->independently_derived || !source_mem1_word(binding->source_word) ||
        !source_mem1_word(binding->global_address))
        return fail(error, size, "Seed binding is not an independently derived pinned source global");
    set_word(&carry->r5, (int8_t)signed_low_byte(binding->source_word),
             carry->host_owner, carry->world_generation,
             carry->allocation_generation, carry->carry_lifetime,
             MELEE_WEB_CPU_WORD_SOURCE_GLOBAL,
             MELEE_WEB_CPU_GLOBAL_SEED_PTR);
    return success(error, size);
}

int melee_web_cpu_r5_preserve(MeleeWebCpuR5Carry* carry,
                              const MeleeWebCpuR5Token* token, char* error,
                              size_t size)
{
    if (!owner_matches(carry, token, error, size))
        return 0;
    return success(error, size);
}

int melee_web_cpu_r5_set_zero(MeleeWebCpuR5Carry* carry,
                              const MeleeWebCpuR5Token* token, char* error,
                              size_t size)
{
    if (!owner_matches(carry, token, error, size))
        return 0;
    set_word(&carry->r5, 0, token->host_owner, token->world_generation,
             token->allocation_generation, carry->carry_lifetime,
             MELEE_WEB_CPU_WORD_ZERO, MELEE_WEB_CPU_GLOBAL_NONE);
    return success(error, size);
}

int melee_web_cpu_r5_mark_unknown(MeleeWebCpuR5Carry* carry,
                                  const MeleeWebCpuR5Token* token,
                                  char* error, size_t size)
{
    if (!owner_matches(carry, token, error, size))
        return 0;
    clear_word(&carry->r5);
    return success(error, size);
}

int melee_web_cpu_r5_resolve_skipped(const MeleeWebCpuR5Carry* carry,
                                     const MeleeWebCpuR5Token* token,
                                     int8_t* stick_x, int8_t* stick_y,
                                     char* error, size_t size)
{
    if (!stick_x || !stick_y)
        return fail(error, size, "CPU carry requires two stick outputs");
    if (!owner_matches(carry, token, error, size))
        return 0;
    if (!carry->r5.known || carry->r5.kind != MELEE_WEB_CPU_WORD_SOURCE_GLOBAL ||
        carry->r5.source_id != MELEE_WEB_CPU_GLOBAL_SEED_PTR ||
        carry->r5.host_owner != token->host_owner ||
        carry->r5.world_generation != token->world_generation ||
        carry->r5.allocation_generation != token->allocation_generation ||
        carry->r5.carry_lifetime != carry->carry_lifetime)
        return fail(error, size, "Skipped CPU conversion requires a current proven seed-global r5 carry");
    *stick_x = carry->r5.low_byte;
    *stick_y = carry->r30.low_byte;
    return success(error, size);
}

int melee_web_cpu_r5_end(MeleeWebCpuR5Carry* carry,
                         const MeleeWebCpuR5Token* token, char* error,
                         size_t size)
{
    if (!owner_matches(carry, token, error, size))
        return 0;
    carry->active = 0;
    carry->host_owner = 0;
    carry->world_generation = 0;
    carry->allocation_generation = 0;
    clear_word(&carry->r5);
    clear_word(&carry->r30);
    return success(error, size);
}

void melee_web_cpu_r5_discard(MeleeWebCpuR5Carry* carry)
{
    if (!carry)
        return;
    carry->active = 0;
    carry->host_owner = 0;
    carry->world_generation = 0;
    carry->allocation_generation = 0;
    clear_word(&carry->r5);
    clear_word(&carry->r30);
}

MeleeWebCpuWord melee_web_cpu_r5_word(const MeleeWebCpuR5Carry* carry)
{
    MeleeWebCpuWord result;
    memset(&result, 0, sizeof(result));
    result.kind = MELEE_WEB_CPU_WORD_UNKNOWN;
    if (carry)
        result = carry->r5;
    return result;
}

MeleeWebCpuWord melee_web_cpu_r30_word(const MeleeWebCpuR5Carry* carry)
{
    MeleeWebCpuWord result;
    memset(&result, 0, sizeof(result));
    result.kind = MELEE_WEB_CPU_WORD_UNKNOWN;
    if (carry)
        result = carry->r30;
    return result;
}
