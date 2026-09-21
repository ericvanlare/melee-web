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
