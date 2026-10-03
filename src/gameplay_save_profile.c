#include "gameplay_save_profile.h"
#include "gameplay_compat.h"

#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gm_16AE.h>
#include <melee/gm/forward.h>
#include <melee/gm/types.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/toy.h>
#include <melee/ty/forward.h>
#include <melee/ty/types.h>

extern GameRules gmMainLib_803D4A48;
extern int melee_web_toy_profile_begin(void);
extern int melee_web_toy_profile_end(void);
/* Source cache sentinel set by Toy_803124BC after its locale archive roots
 * have been loaded. Keep the archive dependency explicit at this boundary. */
extern void* _Toy_sbss_804D6ED0;

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
    unsigned char* fresh_state_snapshot;
    enum_t saved_language;
    enum_t saved_saved_language;
    enum_t fresh_language;
    enum_t fresh_saved_language;
    int active;
    int default_initialized;
    int everything_initialized;
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

static void write_be16(unsigned char* bytes, u16 value)
{
    bytes[0] = (unsigned char) (value >> 8);
    bytes[1] = (unsigned char) value;
}

static void write_be32(unsigned char* bytes, u32 value)
{
    bytes[0] = (unsigned char) (value >> 24);
    bytes[1] = (unsigned char) (value >> 16);
    bytes[2] = (unsigned char) (value >> 8);
    bytes[3] = (unsigned char) value;
}

static void write_be64(unsigned char* bytes, u64 value)
{
    write_be32(bytes, (u32) (value >> 32));
    write_be32(bytes + 4, (u32) value);
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

static void encode16(unsigned char* target, const unsigned char* source)
{
    u16 value;
    memcpy(&value, source, sizeof(value));
    write_be16(target, value);
}

static void encode32(unsigned char* target, const unsigned char* source)
{
    u32 value;
    memcpy(&value, source, sizeof(value));
    write_be32(target, value);
}

static void encode64(unsigned char* target, const unsigned char* source)
{
    u64 value;
    memcpy(&value, source, sizeof(value));
    write_be64(target, value);
}

static void decode_name_tag_bank(struct NameTagDataBank* target,
                                 const unsigned char source[0x1F2C])
{
    unsigned char* bytes = (unsigned char*) target;
    memcpy(bytes, source, 0x1F2C);
    for (size_t tag = 0; tag < 19; ++tag) {
        const size_t base = tag * sizeof(struct NameTagData);
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

static void encode_name_tag_bank(unsigned char target[0x1F2C],
                                 const struct NameTagDataBank* source)
{
    const unsigned char* bytes = (const unsigned char*) source;
    memcpy(target, bytes, 0x1F2C);
    for (size_t tag = 0; tag < 19; ++tag) {
        const size_t base = tag * sizeof(struct NameTagData);
        for (size_t i = 0; i < 120; ++i)
            encode16(target + base + i * 2, bytes + base + i * 2);
        encode16(target + base + 0xF0, bytes + base + 0xF0);
        for (size_t offset = 0xF4; offset <= 0x104; offset += 4)
            encode32(target + base + offset, bytes + base + offset);
        for (size_t offset = 0x108; offset <= 0x10E; offset += 2)
            encode16(target + base + offset, bytes + base + offset);
        for (size_t offset = 0x110; offset <= 0x130; offset += 4)
            encode32(target + base + offset, bytes + base + offset);
        for (size_t i = 0; i < SELKIND_COUNT; ++i)
            encode32(target + base + 0x134 + i * 4,
                    bytes + base + 0x134 + i * 4);
    }
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
    /* gmMainLib_8015D450 consumes one best-score word for each selectable
     * character, despite the generated ABI view naming only four words. */
    for (size_t i = 0; i < SELKIND_COUNT; ++i)
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
    /* The source event table has one best-score word for each of its 0x33
     * authored events; the generated ABI view names only its first four. */
    for (size_t i = 0; i < 0x33; ++i)
        decode32(bytes + 0x0208 + i * 4, source + 0x0208 + i * 4);
    for (size_t i = 0; i < 3; ++i) {
        decode32(bytes + 0x02D8 + i * 4, source + 0x02D8 + i * 4);
        decode32(bytes + 0x02E4 + i * 4, source + 0x02E4 + i * 4);
    }
    /* gm_8015DA40/DA90 address all 300 source reward-ledger bits. */
    for (size_t i = 0; i < 10; ++i)
        decode32(bytes + 0x02F0 + i * 4, source + 0x02F0 + i * 4);
    /* gm_8017297C owns the complete 0x42-entry achievement timestamp
     * table at x1B80.  The generated struct names the first four entries and
     * leaves the authored tail as padding, but those tail words are still
     * persistent card data and must use the source card's endian encoding. */
    for (size_t i = 0; i < 0x42; ++i)
        decode32(bytes + 0x0318 + i * 4, source + 0x0318 + i * 4);
    /* gm_8015DAB4/DADC consume the complete 256-entry challenge inventory. */
    for (size_t i = 0; i < 8; ++i)
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

static void encode_save_data(unsigned char target[0x55E8],
                             const struct gmm_x1868* source)
{
    const unsigned char* bytes = (const unsigned char*) source;
    memcpy(target, bytes, 0x55E8);

    encode16(target + 0x0000, bytes + 0x0000);
    encode16(target + 0x0002, bytes + 0x0002);
    encode32(target + 0x0008, bytes + 0x0008);
    encode32(target + 0x000C, bytes + 0x000C);
    encode32(target + 0x0010, bytes + 0x0010);
    encode32(target + 0x0014, bytes + 0x0014);
    encode32(target + 0x0018, bytes + 0x0018);
    encode32(target + 0x001C, bytes + 0x001C);
    encode32(target + 0x0028, bytes + 0x0028);
    encode32(target + 0x002C, bytes + 0x002C);
    for (size_t offset = 0x0030; offset <= 0x0044; offset += 4)
        encode32(target + offset, bytes + offset);
    for (size_t i = 0; i < SELKIND_COUNT; ++i)
        encode16(target + 0x0048 + i * 2, bytes + 0x0048 + i * 2);
    for (size_t i = 0; i < SELKIND_COUNT; ++i)
        encode32(target + 0x007C + i * 4, bytes + 0x007C + i * 4);
    for (size_t i = 0; i < SELKIND_COUNT; ++i) {
        encode32(target + 0x00E0 + i * 4, bytes + 0x00E0 + i * 4);
        encode32(target + 0x0144 + i * 4, bytes + 0x0144 + i * 4);
    }

    encode32(target + 0x01A8, bytes + 0x01A8);
    for (size_t offset = 0x01B0; offset <= 0x01FC; offset += 4)
        encode32(target + offset, bytes + offset);
    encode64(target + 0x0200, bytes + 0x0200);
    for (size_t i = 0; i < 0x33; ++i)
        encode32(target + 0x0208 + i * 4, bytes + 0x0208 + i * 4);
    for (size_t i = 0; i < 3; ++i) {
        encode32(target + 0x02D8 + i * 4, bytes + 0x02D8 + i * 4);
        encode32(target + 0x02E4 + i * 4, bytes + 0x02E4 + i * 4);
    }
    for (size_t i = 0; i < 10; ++i)
        encode32(target + 0x02F0 + i * 4, bytes + 0x02F0 + i * 4);
    /* Keep the full source-bounded achievement timestamp table symmetric with
     * decode_save_data(); the tail is authored SaveData despite its padding
     * declaration in the generated ABI view. */
    for (size_t i = 0; i < 0x42; ++i)
        encode32(target + 0x0318 + i * 4, bytes + 0x0318 + i * 4);
    for (size_t i = 0; i < 8; ++i)
        encode32(target + 0x0420 + i * 4, bytes + 0x0420 + i * 4);

    encode64(target + 0x0450, bytes + 0x0450);
    encode32(target + 0x0460, bytes + 0x0460);
    encode16(target + 0x0468, bytes + 0x0468);
    encode16(target + 0x046A, bytes + 0x046A);
    for (size_t i = 0; i < TY_TROPHY_COUNT; ++i)
        encode16(target + 0x046C + i * 2, bytes + 0x046C + i * 2);

    for (size_t fighter = 0; fighter < SELKIND_COUNT; ++fighter) {
        const size_t base = offsetof(struct gmm_x1868, x1F2C) +
                            fighter * sizeof(struct FighterData);
        for (size_t i = 0; i < SELKIND_COUNT; ++i)
            encode16(target + base + i * 2, bytes + base + i * 2);
        encode16(target + base + 0x34, bytes + base + 0x34);
        for (size_t offset = 0x38; offset <= 0x48; offset += 4)
            encode32(target + base + offset, bytes + base + offset);
        for (size_t offset = 0x4C; offset <= 0x52; offset += 2)
            encode16(target + base + offset, bytes + base + offset);
        for (size_t offset = 0x54; offset <= 0x74; offset += 4)
            encode32(target + base + offset, bytes + base + offset);
        {
            const struct FighterData* fighter_data =
                (const struct FighterData*) (bytes + base);
            const u16 flags =
                ((u16) fighter_data->x7C.b0 << 15) |
                ((u16) fighter_data->x7C.b1 << 14) |
                ((u16) fighter_data->x7C.b2 << 13) |
                ((u16) fighter_data->x7C.b3 << 12) |
                ((u16) fighter_data->x7C.b4 << 11) |
                ((u16) fighter_data->x7C.b5 << 10) |
                ((u16) fighter_data->x7C.b6 << 9) |
                ((u16) fighter_data->x7C.b789 << 6) |
                ((u16) fighter_data->x7C.b10_to_12 << 3) |
                (u16) fighter_data->x7C.b13_to_15;
            write_be16(target + base + 0x7C, flags);
        }
        encode16(target + base + 0x7E, bytes + base + 0x7E);
        for (size_t offset = 0x84; offset <= 0x9C; offset += 4)
            encode32(target + base + offset, bytes + base + offset);
        encode16(target + base + 0xA0, bytes + base + 0xA0);
        encode16(target + base + 0xA2, bytes + base + 0xA2);
        encode32(target + base + 0xA4, bytes + base + 0xA4);
        encode32(target + base + 0xA8, bytes + base + 0xA8);
    }
    for (size_t bank = 0; bank < 2; ++bank) {
        const size_t base = offsetof(struct gmm_x1868, x2FF8) +
                            bank * sizeof(struct NameTagDataBank);
        encode_name_tag_bank(target + base,
            (const struct NameTagDataBank*) (bytes + base));
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

int melee_web_save_profile_owner_snapshot_card_data(
    const MeleeWebSaveProfileOwner* candidate, uint8_t* output,
    size_t output_size, char* error, size_t error_size)
{
    unsigned char save_data[0x55E8];
    struct NameTagDataBank* banks;
    if (!melee_web_save_profile_owner_live(candidate, error, error_size) ||
        !output || output_size != MELEE_WEB_SAVE_PROFILE_CARD_BYTES)
        return fail(error, error_size,
                    "Save snapshot requires the exact original card-manifest extent");
    banks = (struct NameTagDataBank*) gmMainLib_8015CC4C();
    if ((unsigned char*) banks !=
        (unsigned char*) candidate->source_global +
            offsetof(struct gmm_x0, thing) +
            offsetof(struct gmm_x1868, x2FF8))
        return fail(error, error_size,
                    "Original seven name-bank rows moved from their owned backing");
    encode_save_data(save_data, candidate->source_save);
    memcpy(output, save_data, MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES);
    for (size_t bank = 0; bank < MELEE_WEB_SAVE_PROFILE_CARD_BANK_COUNT; ++bank)
        encode_name_tag_bank(output + MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES +
                                 bank * MELEE_WEB_SAVE_PROFILE_NAME_BANK_BYTES,
                             &banks[bank]);
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
}

int melee_web_save_profile_owner_capture_preferences(
    const MeleeWebSaveProfileOwner* candidate,
    MeleeWebSaveProfilePreferences* preferences, char* error,
    size_t error_size)
{
    const struct gmm_x1CB0* source;
    if (!melee_web_save_profile_owner_live(candidate, error, error_size) ||
        !preferences)
        return fail(error, error_size,
                    "Save preference capture requires the live source owner and output");
    source = &candidate->source_save->x1CB0;
    preferences->item_frequency = source->item_freq;
    preferences->item_mask = source->item_mask;
    memcpy(preferences->rumble_enabled, source->rumble_enabled,
           sizeof(preferences->rumble_enabled));
    preferences->deflicker = source->deflicker;
    preferences->saved_language = source->saved_language;
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
}

int melee_web_save_profile_owner_snapshot_card_data_with_preferences(
    const MeleeWebSaveProfileOwner* candidate,
    const MeleeWebSaveProfilePreferences* preferences, uint8_t* output,
    size_t output_size, char* error, size_t error_size)
{
    const size_t base = offsetof(struct gmm_x1868, x1CB0);
    const size_t frequency = base + offsetof(struct gmm_x1CB0, item_freq);
    const size_t mask = base + offsetof(struct gmm_x1CB0, item_mask);
    const size_t rumble = base + offsetof(struct gmm_x1CB0, rumble_enabled);
    const size_t deflicker = base + offsetof(struct gmm_x1CB0, deflicker);
    const size_t language = base + offsetof(struct gmm_x1CB0, saved_language);
    if (!preferences || frequency >= MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES ||
        mask + sizeof(preferences->item_mask) >
            MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES ||
        rumble + sizeof(preferences->rumble_enabled) >
            MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES ||
        deflicker >= MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES ||
        language >= MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES)
        return fail(error, error_size,
                    "Source save preference fields exceed the card SaveData extent");
    if (!melee_web_save_profile_owner_snapshot_card_data(
            candidate, output, output_size, error, error_size))
        return 0;

    /* Scene startup supplies browser-supported runtime defaults for these
     * original settings. Overlay only those typed preference fields onto the
     * live snapshot so progress and every other source byte remain current. */
    output[frequency] = preferences->item_frequency;
    write_be64(output + mask, (u64) preferences->item_mask);
    memcpy(output + rumble, preferences->rumble_enabled,
           sizeof(preferences->rumble_enabled));
    output[deflicker] = preferences->deflicker;
    output[language] = preferences->saved_language;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_apply_card_data(
    MeleeWebSaveProfileOwner* candidate, const uint8_t* input,
    size_t input_size, char* error, size_t error_size)
{
    unsigned char save_data[0x55E8];
    struct NameTagDataBank* banks;
    if (!melee_web_save_profile_owner_live(candidate, error, error_size) ||
        !input || input_size != MELEE_WEB_SAVE_PROFILE_CARD_BYTES)
        return fail(error, error_size,
                    "Imported save does not match the original card-manifest extent");
    banks = (struct NameTagDataBank*) gmMainLib_8015CC4C();
    if ((unsigned char*) banks !=
        (unsigned char*) candidate->source_global +
            offsetof(struct gmm_x0, thing) +
            offsetof(struct gmm_x1868, x2FF8))
        return fail(error, error_size,
                    "Original seven name-bank rows moved from their owned backing");
    /* Preserve fresh-source defaults for SaveData bytes beyond the original
     * card manifest's 0x1790-byte extent, then install the imported prefix. */
    encode_save_data(save_data, candidate->source_save);
    memcpy(save_data, input, MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES);
    decode_save_data(candidate->source_save, save_data);
    for (size_t bank = 0; bank < MELEE_WEB_SAVE_PROFILE_CARD_BANK_COUNT; ++bank)
        decode_name_tag_bank(&banks[bank],
            input + MELEE_WEB_SAVE_PROFILE_CARD_SAVE_BYTES +
                bank * MELEE_WEB_SAVE_PROFILE_NAME_BANK_BYTES);
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
    candidate->fresh_state_snapshot = malloc(
        MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES + sizeof(struct ToyRuntimeAggregate));
    if (!candidate->fresh_state_snapshot)
        return fail(error, error_size,
                    "Cannot retain the original fresh profile for mode isolation");
    memcpy(candidate->fresh_state_snapshot, candidate->source_global,
           MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    memcpy(candidate->fresh_state_snapshot + MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES,
           &melee_web_toy_state, sizeof(struct ToyRuntimeAggregate));
    candidate->fresh_language = lbLang_GetLanguageSetting();
    candidate->fresh_saved_language = lbLang_GetSavedLanguage();
    candidate->default_initialized = 1;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_initialize_everything(
    MeleeWebSaveProfileOwner* candidate, char* error, size_t error_size)
{
    int selkind;
    int event;
    int trophy;
    int challenge;

    if (!melee_web_save_profile_owner_live(candidate, error, error_size))
        return 0;
    if (!candidate->default_initialized)
        return fail(error, error_size,
                    "Everything unlocked requires a fresh original profile");
    if (candidate->everything_initialized)
        return fail(error, error_size,
                    "Everything unlocked baseline is already initialized");
    if (!_Toy_sbss_804D6ED0)
        return fail(error, error_size,
                    "Everything unlocked requires initialized original TyDatai tables");

    /* These source routines use the authored eleven-entry unlock tables;
     * there is no guessed mask width or asset availability implication. */
    gm_80164F18();
    gm_8016468C();
    (void) fn_80173510();
    (void) fn_801735F0();
    (void) fn_8017367C();

    /* The 51 event-clear flags are the exact range consumed by the source's
     * all-event completion check (gm_8017335C and gm_801721EC_2). Leave the
     * four event score/high-score words at their fresh, valid defaults. */
    for (event = 0; event < 0x33; ++event)
        gmMainLib_8015CEB4(event);
    /* The source grants this separate boolean only for the final event's
     * three-stock clear. A completed baseline includes that known condition. */
    gmMainLib_8015CF84();

    /* The retail mode-clear path maps each selectable character and each of
     * Classic, Adventure and All-Star to source challenge IDs. Calling that
     * path records those clears without inventing match counts or scores.
     * The source routine also mirrors Zelda/Sheik's shared selectable slot. */
    for (selkind = 0; selkind < SELKIND_COUNT; ++selkind) {
        const u8 ckind = gm_SelKindToCKind((u8) selkind);
        fn_80173834(ckind, GM_CLASSIC, false);
        fn_80173834(ckind, GM_ADVENTURE, false);
        fn_80173834(ckind, GM_ALLSTAR, false);
    }

    /* The result path records challenge completions through fn_8016F140.
     * Its source inventory is 0..255; gm_80173EEC's own complete-all test
     * explicitly excludes these seven IDs because they are not required
     * completion entries. Record every other authored challenge, then let
     * that original aggregate routine derive the 0x123 all-challenges award. */
    for (challenge = 0; challenge < 0x100; ++challenge) {
        if (challenge != 9 && challenge != 0x29 && challenge != 0x42 &&
            challenge != 0x43 && challenge != 0xB9 && challenge != 0xC9 &&
            challenge != 0xCA)
            fn_8016F140(challenge);
    }
    gm_80173EEC();

    /* Trophy IDs and the ownership counter are maintained by the original
     * award routine. The source table declares exactly TY_TROPHY_COUNT IDs. */
    for (trophy = 0; trophy < TY_TROPHY_COUNT; ++trophy)
        Toy_SetUnlockState(trophy, true);

    /* These original debug-unlock helpers are also used together by
     * gmMainLib_8015FA34: they mark source-bounded unlock notifications and
     * the 300-slot trophy reward ledger as claimed. The latter ledger lives
     * in SaveData's authored padding span; the source loop defines its bound. */
    gm_8017297C();
    gm_801741FC();

    /* Recompute the four named feature bits from the source unlock tables.
     * Do not copy the debug path's raw 0xFF, whose remaining bits are unknown. */
    gm_80172898(0xFFFFU);
    /* fn_8016F140() records completed challenges through the original source
     * award path, which also sets session-only pending trophy notifications in
     * gmm_x0's transient block. The completed baseline has already included
     * those rewards; clear only that transient block with Melee's own reset
     * routine so Title does not replay newly-created unlock notices. */
    gm_80172174();
    if (!aliases_match(candidate, error, error_size)) return 0;
    candidate->everything_initialized = 1;
    return ok(error, error_size);
}

int melee_web_save_profile_owner_restore_default(
    MeleeWebSaveProfileOwner* candidate, char* error, size_t error_size)
{
    if (!melee_web_save_profile_owner_live(candidate, error, error_size))
        return 0;
    if (!candidate->default_initialized || !candidate->fresh_state_snapshot)
        return fail(error, error_size,
                    "Original fresh profile snapshot is unavailable");
    memcpy(candidate->source_global, candidate->fresh_state_snapshot,
           MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES);
    memcpy(&melee_web_toy_state,
           candidate->fresh_state_snapshot + MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES,
           sizeof(struct ToyRuntimeAggregate));
    lbLang_SetLanguageSetting(candidate->fresh_language);
    lbLang_SetSavedLanguage(candidate->fresh_saved_language);
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
}

int melee_web_save_profile_owner_initialize_menu_roster(
    MeleeWebSaveProfileOwner* candidate, uint16_t stages, char* error,
    size_t error_size)
{
    uint16_t* source_characters;
    uint16_t expected_characters;

    if (!melee_web_save_profile_owner_live(candidate, error, error_size))
        return 0;
    if (!candidate->default_initialized)
        return fail(error, error_size,
                    "Original menu roster requires a fresh source profile");
    if (!aliases_match(candidate, error, error_size))
        return 0;
    source_characters = gmMainLib_GetUnlockedCharactersBitmaskPtr();
    if (source_characters !=
        &candidate->source_save->unlocked_characers_bitmask)
        return fail(error, error_size,
                    "Original menu roster alias does not point into SaveData");
    if (NUM_UNLOCKABLE_CHARACTERS >= 16)
        return fail(error, error_size,
                    "Original unlockable-character table exceeds its source mask");

    /* Match the source debug-unlock initialization used for a fully open
     * roster. These companion routines consume the authored 0x42 unlock
     * notifications and 0x12C reward-ledger entries. Title still runs its
     * ordinary checks; the initialized profile has no stale unlock/award
     * work for those checks to route to Challenger Approach. */
    gm_80164F18();
    expected_characters = (uint16_t) ((1U << NUM_UNLOCKABLE_CHARACTERS) - 1U);
    if (*source_characters != expected_characters)
        return fail(error, error_size,
                    "Original character unlock routine disagreed with its authored table bound");
    if (!melee_web_save_profile_owner_set_roster(
            candidate, *source_characters, stages, error, error_size))
        return 0;
    gm_8017297C();
    gm_801741FC();
    return aliases_match(candidate, error, error_size) && ok(error, error_size);
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
    free(candidate->fresh_state_snapshot);
    candidate->fresh_state_snapshot = NULL;
    candidate->default_initialized = 0;
    candidate->everything_initialized = 0;
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
