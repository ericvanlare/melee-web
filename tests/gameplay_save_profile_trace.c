#include "gameplay_compat.h"
#include "gameplay_save_profile.h"

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/types.h>
#include <melee/lb/lblanguage.h>
#include <melee/ty/toy.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void check(int condition, const char* message)
{
    if (!condition) {
        fputs(message, stderr);
        fputc('\n', stderr);
        exit(1);
    }
}

static void check_error(int condition, const char* error)
{
    check(condition, error && error[0] ? error : "source save/profile operation failed");
}

static void mutate_original_profile(void)
{
    struct gmm_x1868* save = gmMainLib_GetSaveData();
    uint32_t* achievement_timestamps =
        (uint32_t*) ((unsigned char*) save + offsetof(struct gmm_x1868, x1B80));
    uint32_t* trophy_claimed =
        (uint32_t*) ((unsigned char*) save + offsetof(struct gmm_x1868, x1B58));
    uint32_t* transient_trophy_pending =
        (uint32_t*) gmMainLib_8015CCE4();

    save->x1B40[0] |= 1U << 27;
    save->x1B4C[0] |= 1U << 27;
    achievement_timestamps[27] = 0x12345678;
    trophy_claimed[159 / 32] |= 1U << (159 % 32);
    save->trophy_flags[159] = 0xBEEF;
    transient_trophy_pending[159 / 32] |= 1U << (159 % 32);
    transient_trophy_pending[1] = 0x87654321;
}

int main(void)
{
    char error[256] = { 0 };
    unsigned char source_before[MELEE_WEB_SAVE_PROFILE_SOURCE_BYTES];
    unsigned char toy_before[sizeof(struct ToyRuntimeAggregate)];
    unsigned char toy_after[sizeof(struct ToyRuntimeAggregate)];
    MeleeWebSaveProfileOwner* profile;
    struct gmm_x1868* save;
    struct gmm_x0* source_root = gmMainLib_GetProfileRoot();

    check(gmMainLib_804D3EE0 == source_root,
          "original save root is not the authored first profile object");
    check(gmMainLib_GetSaveData() == &source_root->thing,
          "original save getter does not point into the authored root");
    check(gmMainLib_8015CCE4() == (void*) ((unsigned char*) source_root + 0x44),
          "original transient getter does not point into the authored root");

    /* Language distribution lives in the source root while the saved menu
     * language lives in the typed preferences row. Seed both outside the
     * owner so default initialization must switch to US and deactivation must
     * restore the caller's JP state. */
    lbLang_SetLanguageSetting(LANG_JP);
    lbLang_SetSavedLanguage(LANG_JP);

    /* The source Toy table is one of the linker-adjacent views owned by the
     * profile boundary. Seed two entries before capture so the real
     * gmMainLib_8015F600 -> Toy_80311960 path has observable mutations. */
    Toy_804A284C[5] = 0xBEEF;
    Toy_804A284C[6] = 0xCAFE;
    memcpy(source_before, source_root, sizeof(source_before));
    memcpy(toy_before, &melee_web_toy_state, sizeof(toy_before));

    profile = melee_web_save_profile_owner_create(error, sizeof(error));
    check_error(profile != NULL, error);
    gmMainLib_804D3EE0 = NULL;
    check(!melee_web_save_profile_owner_activate(profile, error, sizeof(error)),
          "profile activation accepted a moved source root");
    gmMainLib_804D3EE0 = source_root;
    check_error(melee_web_save_profile_owner_activate(profile, error, sizeof(error)), error);
    check(!melee_web_toy_profile_begin(),
          "Toy profile state allowed a second owner while save owner was active");

    check_error(melee_web_save_profile_owner_initialize_default(profile, error,
                                                                  sizeof(error)),
                error);
    save = gmMainLib_GetSaveData();
    check(save->x1B40[0] == 0 && save->x1B4C[0] == 0,
          "original fresh-profile init retained achievement flags");
    check(save->x1CB0.item_freq == 2 && save->x1CB0.deflicker == 1 &&
              save->x1CB0.stage_mask == UINT32_MAX,
          "original packed profile preferences were not copied");
    check(save->x1CB0.saved_language == LANG_US &&
              lbLang_GetSavedLanguage() == LANG_US,
          "original saved language was not initialized to US");
    for (size_t port = 0; port < 4; ++port)
        check(save->x1CB0.rumble_enabled[port] == 1,
              "original packed rumble preference was not initialized");
    {
        struct NameTagDataBank* banks =
            (struct NameTagDataBank*) gmMainLib_8015CC4C();
        for (size_t bank = 0; bank < 7; ++bank) {
            check(banks[bank].inner[0].x1A2 == 5 &&
                      banks[bank].inner[18].x1A2 == 5 &&
                      banks[bank].inner[0].rumble_enabled &&
                      banks[bank].inner[18].rumble_enabled,
                  "original F600 name-bank sequence did not initialize all seven banks");
        }
    }
    check(Toy_804A284C[5] == 0 && Toy_804A284C[6] == 0,
          "original fresh-profile init did not mutate the Toy table through F600");
    check(!melee_web_save_profile_owner_initialize_default(profile, error,
                                                            sizeof(error)),
          "original fresh-profile init was allowed twice");

    check_error(melee_web_save_profile_owner_set_roster(profile, 0x07FF, 0x01C0,
                                                         error, sizeof(error)),
                error);
    check(save->unlocked_characers_bitmask == 0x07FF && save->x186A == 0x01C0,
          "explicit authored roster setup was not applied");
    check(save->x1B40[0] == 0 && save->x1B4C[0] == 0,
          "roster setup seeded achievement flags");
    mutate_original_profile();
    check_error(melee_web_save_profile_owner_live(profile, error, sizeof(error)), error);
    check(!melee_web_save_profile_owner_destroy(profile, error, sizeof(error)),
          "active owner was destroyed");
    check_error(melee_web_save_profile_owner_deactivate(profile, error, sizeof(error)),
                error);
    check(!memcmp(source_root, source_before, sizeof(source_before)),
          "full original profile backing was not restored");
    memcpy(toy_after, &melee_web_toy_state, sizeof(toy_after));
    check(!memcmp(toy_after, toy_before, sizeof(toy_after)),
          "full linker-adjacent Toy aggregate was not restored");
    check(lbLang_GetLanguageSetting() == LANG_JP &&
              lbLang_GetSavedLanguage() == LANG_JP,
          "source distribution or saved language was not restored");
    check_error(melee_web_save_profile_owner_destroy(profile, error, sizeof(error)), error);

    /* Exercise the helper directly after the owner has released it. A live
     * animation alias must block both capture and restoration. */
    check(melee_web_toy_profile_begin(), "Toy profile helper did not capture after release");
    memcpy(toy_before, &melee_web_toy_state, sizeof(toy_before));
    Toy_804A284C[5] = 0x1357;
    check(melee_web_toy_profile_end(), "Toy profile helper did not restore after release");
    check(!memcmp(&melee_web_toy_state, toy_before, sizeof(toy_before)),
          "Toy profile helper did not restore its full aggregate");

    memcpy(toy_before, &melee_web_toy_state, sizeof(toy_before));
    melee_web_toy_state.anim.gobj = (void*) (uintptr_t) 1;
    check(!melee_web_toy_profile_begin(),
          "Toy profile helper accepted a live animation alias");
    memcpy(&melee_web_toy_state, toy_before, sizeof(toy_before));

    puts("Original F600 profile init and Toy aggregate lifetime passed");
    return 0;
}
