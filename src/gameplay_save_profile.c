#include "gameplay_save_profile.h"

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lblanguage.h>

extern GameRules gmMainLib_803D4A48;
extern int melee_web_toy_profile_begin(void);
extern int melee_web_toy_profile_end(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(union gmm_mainlib_profile_storage) == MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES,
               "The original two-root profile extent changed");
_Static_assert(sizeof(struct gmm_x0) == 0x8518,
               "The source global ABI changed; update its owner explicitly");
_Static_assert(offsetof(struct gmm_x0, thing) == 0x1868,
               "The generated source save root moved; update its owner explicitly");
_Static_assert(sizeof(struct gmm_x1868) == 0x55E8,
               "The save profile ABI changed; update its owner explicitly");
_Static_assert(offsetof(struct gmm_x1868, x1F2C) == 0x06C4,
               "The source fighter profile layout moved; update its decoder explicitly");
_Static_assert(offsetof(struct gmm_x1868, x2FF8) +
                   2 * sizeof(struct NameTagDataBank) <= sizeof(struct gmm_x1868),
               "The source name profile layout exceeds SaveData; update its decoder explicitly");

struct MeleeWebSaveProfileOwner {
    struct gmm_x0* source_global;
    struct gmm_x1868* source_save;
    unsigned char* source_transient;
    unsigned char* source_snapshot;
    enum_t saved_language;
    enum_t saved_saved_language;
    int active;
    int default_initialized;
};

static MeleeWebSaveProfileOwner* owner;

static int fail(char* error, size_t error_size, const char* message)
{
    if (error && error_size) snprintf(error, error_size, "%s", message);
    return 0;
}

static int ok(char* error, size_t error_size)
{
    if (error && error_size) error[0] = '\0';
    return 1;
}

static int source_aliases(struct gmm_x0** global, struct gmm_x1868** save,
                          unsigned char** transient, char* error,
                          size_t error_size)
{
    struct gmm_x0* current_global = gmMainLib_804D3EE0;
    if (current_global != gmMainLib_GetProfileRoot())
        return fail(error, error_size,
                    "Original save/profile root is not the owned backing");
    struct gmm_x1868* current_save = gmMainLib_GetSaveData();
    void* current_transient = gmMainLib_8015CCE4();
    if (!current_save || !current_transient)
        return fail(error, error_size,
                    "Original save/profile roots are not initialized");
    if (current_save != &current_global->thing ||
        current_transient != (unsigned char*) current_global + 0x44)
        return fail(error, error_size,
                    "Original save/profile aliases do not share the source root");
    if (global) *global = current_global;
    if (save) *save = current_save;
    if (transient) *transient = current_transient;
    return 1;
}

static int owned(const MeleeWebSaveProfileOwner* candidate, char* error,
                 size_t error_size)
{
    if (!candidate || candidate != owner)
        return fail(error, error_size, "Save/profile owner is not live");
    return 1;
}

static int aliases_match(const MeleeWebSaveProfileOwner* candidate, char* error,
                         size_t error_size)
{
    struct gmm_x0* global;
    struct gmm_x1868* save;
    unsigned char* transient;
    if (!source_aliases(&global, &save, &transient, error, error_size)) return 0;
    if (global != candidate->source_global || save != candidate->source_save ||
        transient != candidate->source_transient)
        return fail(error, error_size,
                    "Original save/profile root aliases changed");
    return 1;
}

MeleeWebSaveProfileOwner* melee_web_save_profile_owner_create(char* error,
                                                               size_t error_size)
{
    struct gmm_x0* global;
    struct gmm_x1868* save;
    unsigned char* transient;
    if (owner)
        return fail(error, error_size,
                    "Only one source save/profile owner may be live"), NULL;
    if (!source_aliases(&global, &save, &transient, error, error_size)) return NULL;
    owner = calloc(1, sizeof(*owner));
    if (!owner) {
        fail(error, error_size, "Cannot allocate source save/profile owner");
        return NULL;
    }
    owner->source_global = global;
    owner->source_save = save;
    owner->source_transient = transient;
    ok(error, error_size);
    return owner;
}

int melee_web_save_profile_owner_activate(MeleeWebSaveProfileOwner* candidate,
                                          char* error, size_t error_size)
{
    if (!owned(candidate, error, error_size)) return 0;
    if (candidate->active)
        return fail(error, error_size, "Source save/profile owner is already active");
    if (!aliases_match(candidate, error, error_size)) return 0;
    candidate->source_snapshot = malloc(MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    if (!candidate->source_snapshot) {
        return fail(error, error_size,
                    "Cannot allocate source save/profile snapshot");
    }
    memcpy(candidate->source_snapshot, candidate->source_global,
           MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    /* The distribution language is rooted in gmm_x0 while the saved menu
     * language is in SaveData preferences. Keep typed copies as part of the
     * owner contract as well as the full backing snapshot: initialization
     * below changes both values before any scene can run. */
    candidate->saved_language = lbLang_GetLanguageSetting();
    candidate->saved_saved_language = lbLang_GetSavedLanguage();
    if (!melee_web_toy_profile_begin()) {
        free(candidate->source_snapshot); candidate->source_snapshot = NULL;
        return fail(error, error_size, "Original Toy profile state is already owned or live");
    }
    candidate->active = 1;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_live(const MeleeWebSaveProfileOwner* candidate,
                                      char* error, size_t error_size)
{
    if (!owned(candidate, error, error_size) || !candidate->active) {
        if (candidate && candidate == owner && !candidate->active)
            fail(error, error_size, "Source save/profile owner is inactive");
        return 0;
    }
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
}

int melee_web_save_profile_owner_set_roster(MeleeWebSaveProfileOwner* candidate,
                                            uint16_t characters,
                                            uint16_t stages, char* error,
                                            size_t error_size)
{
    struct gmm_x0* global;
    struct gmm_x1868* save;
    unsigned char* transient;
    uint16_t* source_characters;
    uint16_t* source_stages;
    if (!melee_web_save_profile_owner_live(candidate, error, error_size)) return 0;
    if (!source_aliases(&global, &save, &transient, error, error_size) ||
        global != candidate->source_global || save != candidate->source_save ||
        transient != candidate->source_transient)
        return fail(error, error_size,
                    "Original save/profile root aliases changed");
    source_characters = gmMainLib_GetUnlockedCharactersBitmaskPtr();
    source_stages = gmMainLib_8015EDA4();
    if (source_characters != &save->unlocked_characers_bitmask ||
        source_stages != &save->x186A)
        return fail(error, error_size,
                    "Original roster aliases do not point into SaveData");
    *source_characters = characters;
    *source_stages = stages;
    return ok(error, error_size);
}

/* The observer publishes PowerPC guest bytes.  The browser build stores the
 * same generated source structs in the host's native endian order, so a raw
 * memcpy would make every u16/u32 profile field observe a byte-swapped value.
 * Keep this decoder beside the owner that knows the generated source layout;
 * callers only provide copied bytes and never a guest address. */
static u16 read_be16(const unsigned char* bytes)
{
    return (u16) bytes[0] << 8 | bytes[1];
}

static u32 read_be32(const unsigned char* bytes)
{
    return (u32) bytes[0] << 24 | (u32) bytes[1] << 16 |
           (u32) bytes[2] << 8 | bytes[3];
}

static u64 read_be64(const unsigned char* bytes)
{
    return (u64) read_be32(bytes) << 32 | read_be32(bytes + 4);
}

static void decode16(unsigned char* target, const unsigned char* source)
{
    u16 value = read_be16(source);
    memcpy(target, &value, sizeof(value));
}

static void decode32(unsigned char* target, const unsigned char* source)
{
    u32 value = read_be32(source);
    memcpy(target, &value, sizeof(value));
}

static void decode64(unsigned char* target, const unsigned char* source)
{
    u64 value = read_be64(source);
    memcpy(target, &value, sizeof(value));
}

static void decode_rules(GameRules* target, const unsigned char source[0x18])
{
    /* GameRules is byte fields through x13 and one asserted signed 32-bit
     * field at x14. */
    memcpy(target, source, 0x14);
    decode32((unsigned char*) target + 0x14, source + 0x14);
}

static void decode_save_data(struct gmm_x1868* target,
                             const unsigned char source[0x55E8])
{
    unsigned char* bytes = (unsigned char*) target;
    memcpy(bytes, source, 0x55E8);

    decode16(bytes + 0x0000, source + 0x0000);
    decode16(bytes + 0x0002, source + 0x0002);
    decode32(bytes + 0x0008, source + 0x0008);
    decode32(bytes + 0x000C, source + 0x000C);
    decode32(bytes + 0x0010, source + 0x0010);
    decode32(bytes + 0x0014, source + 0x0014);
    decode32(bytes + 0x0018, source + 0x0018);
    decode32(bytes + 0x001C, source + 0x001C);
    decode32(bytes + 0x0028, source + 0x0028);
    decode32(bytes + 0x002C, source + 0x002C);

    /* gmm_retval_EDBC at +0x30. */
    decode32(bytes + 0x0030, source + 0x0030);
    decode32(bytes + 0x0034, source + 0x0034);
    decode32(bytes + 0x0038, source + 0x0038);
    decode32(bytes + 0x003C, source + 0x003C);
    decode32(bytes + 0x0040, source + 0x0040);
    decode32(bytes + 0x0044, source + 0x0044);
    for (size_t i = 0; i < SELKIND_COUNT; ++i)
        decode16(bytes + 0x0048 + i * 2, source + 0x0048 + i * 2);
    for (size_t i = 0; i < 4; ++i)
        decode32(bytes + 0x007C + i * 4, source + 0x007C + i * 4);
    for (size_t i = 0; i < SELKIND_COUNT; ++i) {
        decode32(bytes + 0x00E0 + i * 4, source + 0x00E0 + i * 4);
        decode32(bytes + 0x0144 + i * 4, source + 0x0144 + i * 4);
    }

    /* The typed counters and persistent fields before the preferences block. */
    decode32(bytes + 0x01A8, source + 0x01A8);
    for (size_t offset = 0x01B0; offset <= 0x01FC; offset += 4)
        decode32(bytes + offset, source + offset);
    decode64(bytes + 0x0200, source + 0x0200);
    for (size_t i = 0; i < 4; ++i)
        decode32(bytes + 0x0208 + i * 4, source + 0x0208 + i * 4);
    for (size_t i = 0; i < 3; ++i) {
        decode32(bytes + 0x02D8 + i * 4, source + 0x02D8 + i * 4);
        decode32(bytes + 0x02E4 + i * 4, source + 0x02E4 + i * 4);
        decode32(bytes + 0x02F0 + i * 4, source + 0x02F0 + i * 4);
    }
    for (size_t i = 0; i < 4; ++i)
        decode32(bytes + 0x0318 + i * 4, source + 0x0318 + i * 4);
    for (size_t i = 0; i < 3; ++i)
        decode32(bytes + 0x0420 + i * 4, source + 0x0420 + i * 4);

    /* gmm_x1CB0 at +0x448. */
    decode64(bytes + 0x0450, source + 0x0450);
    decode32(bytes + 0x0460, source + 0x0460);
    decode16(bytes + 0x0468, source + 0x0468);
    decode16(bytes + 0x046A, source + 0x046A);
    for (size_t i = 0; i < TY_TROPHY_COUNT; ++i)
        decode16(bytes + 0x046C + i * 2, source + 0x046C + i * 2);

    /* FighterData and NameTagData use generated fixed layouts.  Decode every
     * scalar field, while leaving authored padding and byte/bit fields as
     * copied bytes. */
    for (size_t fighter = 0; fighter < SELKIND_COUNT; ++fighter) {
        const size_t base = offsetof(struct gmm_x1868, x1F2C) +
                            fighter * sizeof(struct FighterData);
        for (size_t i = 0; i < SELKIND_COUNT; ++i)
            decode16(bytes + base + i * 2, source + base + i * 2);
        decode16(bytes + base + 0x34, source + base + 0x34);
        for (size_t offset = 0x38; offset <= 0x48; offset += 4)
            decode32(bytes + base + offset, source + base + offset);
        for (size_t offset = 0x4C; offset <= 0x52; offset += 2)
            decode16(bytes + base + offset, source + base + offset);
        for (size_t offset = 0x54; offset <= 0x74; offset += 4)
            decode32(bytes + base + offset, source + base + offset);
        /* x7C is a PPC bitfield word.  Decoding it as a native u16 would
         * reverse b0..b15 on the little-endian browser target; map the
         * authored fields explicitly instead. */
        {
            const u16 flags = read_be16(source + base + 0x7C);
            unsigned char* field = bytes + base + 0x7C;
            struct FighterData* fighter_data =
                (struct FighterData*) (bytes + base);
            /* The generated little-endian layout stores this anonymous
             * bitfield word in the target's native order. */
            memcpy(field, &flags, sizeof(flags));
            fighter_data->x7C.b0 = (flags >> 15) & 1;
            fighter_data->x7C.b1 = (flags >> 14) & 1;
            fighter_data->x7C.b2 = (flags >> 13) & 1;
            fighter_data->x7C.b3 = (flags >> 12) & 1;
            fighter_data->x7C.b4 = (flags >> 11) & 1;
            fighter_data->x7C.b5 = (flags >> 10) & 1;
            fighter_data->x7C.b6 = (flags >> 9) & 1;
            fighter_data->x7C.b789 = (flags >> 6) & 7;
            fighter_data->x7C.b10_to_12 = (flags >> 3) & 7;
            fighter_data->x7C.b13_to_15 = flags & 7;
        }
        decode16(bytes + base + 0x7E, source + base + 0x7E);
        for (size_t offset = 0x84; offset <= 0x9C; offset += 4)
            decode32(bytes + base + offset, source + base + offset);
        decode16(bytes + base + 0xA0, source + base + 0xA0);
        decode16(bytes + base + 0xA2, source + base + 0xA2);
        decode32(bytes + base + 0xA4, source + base + 0xA4);
        decode32(bytes + base + 0xA8, source + base + 0xA8);
    }
    for (size_t bank = 0; bank < 2; ++bank) {
        const size_t bank_base = offsetof(struct gmm_x1868, x2FF8) +
                                 bank * 19 * sizeof(struct NameTagData);
        for (size_t tag = 0; tag < 19; ++tag) {
            const size_t base = bank_base + tag * sizeof(struct NameTagData);
            for (size_t i = 0; i < 120; ++i)
                decode16(bytes + base + i * 2, source + base + i * 2);
            decode16(bytes + base + 0xF0, source + base + 0xF0);
            for (size_t offset = 0xF4; offset <= 0x104; offset += 4)
                decode32(bytes + base + offset, source + base + offset);
            for (size_t offset = 0x108; offset <= 0x10E; offset += 2)
                decode16(bytes + base + offset, source + base + offset);
            for (size_t offset = 0x110; offset <= 0x130; offset += 4)
                decode32(bytes + base + offset, source + base + offset);
            for (size_t i = 0; i < SELKIND_COUNT; ++i)
                decode32(bytes + base + 0x134 + i * 4,
                         source + base + 0x134 + i * 4);
        }
    }
}

int melee_web_save_profile_owner_apply_reference_context(
    MeleeWebSaveProfileOwner* candidate, const uint8_t game_rules[0x18],
    const uint8_t save_data[0x55E8], char* error, size_t error_size)
{
    struct gmm_x0* global;
    struct gmm_x1868* save;
    unsigned char* transient;
    if (!melee_web_save_profile_owner_live(candidate, error, error_size) ||
        !game_rules || !save_data)
        return fail(error, error_size,
                    "First-CSS source profile context is unavailable");
    if (!source_aliases(&global, &save, &transient, error, error_size) ||
        global != candidate->source_global || save != candidate->source_save ||
        transient != candidate->source_transient)
        return fail(error, error_size,
                    "Original save/profile root aliases changed");
    decode_rules(gmMainLib_GetGameRules(), game_rules);
    decode_save_data(save, save_data);
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
}

int melee_web_save_profile_owner_initialize_default(
    MeleeWebSaveProfileOwner* candidate, char* error, size_t error_size)
{
    int i;

    if (!melee_web_save_profile_owner_live(candidate, error, error_size))
        return 0;
    if (candidate->default_initialized)
        return fail(error, error_size,
                    "Original default profile is already initialized");

    /* Own the complete original startup backing before the source's one-time
     * reset. Scene heap payloads are separate and are never cleared here. */
    memset(candidate->source_global, 0, MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    lbLang_SetLanguageSetting(LANG_US);
    lbLang_SetSavedLanguage(LANG_US);
    *gmMainLib_GetGameRules() = gmMainLib_803D4A48;
    for (i = 1; i < 9; ++i)
        gmMainLib_8015F600(i, 1);

    if (!aliases_match(candidate, error, error_size)) return 0;
    candidate->default_initialized = 1;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_deactivate(MeleeWebSaveProfileOwner* candidate,
                                            char* error, size_t error_size)
{
    if (!melee_web_save_profile_owner_live(candidate, error, error_size)) return 0;
    /* Do not restore into a replacement source object. The snapshot remains
     * allocated and the owner remains active so the caller can fail safely. */
    if (!melee_web_toy_profile_end())
        return fail(error, error_size, "Original Toy profile state cannot be restored");
    memcpy(candidate->source_global, candidate->source_snapshot,
           MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    lbLang_SetLanguageSetting(candidate->saved_language);
    lbLang_SetSavedLanguage(candidate->saved_saved_language);
    free(candidate->source_snapshot);
    candidate->source_snapshot = NULL;
    candidate->default_initialized = 0;
    candidate->active = 0;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_is_active(const MeleeWebSaveProfileOwner* candidate)
{
    return candidate && candidate == owner && candidate->active;
}

int melee_web_save_profile_owner_destroy(MeleeWebSaveProfileOwner* candidate,
                                         char* error, size_t error_size)
{
    if (!owned(candidate, error, error_size)) return 0;
    if (candidate->active)
        return fail(error, error_size,
                    "Deactivate source save/profile owner before destruction");
    if (candidate->source_snapshot)
        return fail(error, error_size,
                    "Source save/profile snapshot is inconsistent");
    owner = NULL;
    free(candidate);
    return ok(error, error_size);
}
