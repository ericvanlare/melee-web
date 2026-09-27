#include "gameplay_compat.h"
#include "gameplay_save_profile.h"

#include <melee/gm/gmmain_lib.h>
#include <melee/gm/gm_1601.h>
#include <melee/gm/gm_16F1.h>
#include <melee/gm/types.h>
#include <melee/gm/forward.h>
#include <melee/lb/lblanguage.h>
#include <melee/mn/mnname.h>
#include <melee/ty/toy.h>
#include <melee/ty/forward.h>

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
    unsigned char card_profile[MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    unsigned char card_profile_after[MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    unsigned char fresh_card_profile[MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    unsigned char toy_before[sizeof(struct ToyRuntimeAggregate)];
    unsigned char toy_after[sizeof(struct ToyRuntimeAggregate)];
    MeleeWebSaveProfilePreferences fresh_preferences;
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
    memcpy(GetPersistentNameData(0)->namedata, "SAVE", 5);
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
    {
        struct PlayerInitData defaults;
        struct NameTagDataBank* banks =
            (struct NameTagDataBank*) gmMainLib_8015CC4C();
        gm_SetupPlayerDefaults(&defaults);
        check(defaults.nametag == GM_NAMETAG_NONE,
              "Original player defaults did not select the no-name sentinel");
        for (size_t bank = 0; bank < 7; ++bank) {
            for (size_t name = 0; name < 19; ++name) {
                check(banks[bank].inner[name].namedata[0] == '\0',
                      "Fresh source name entry was not empty");
            }
        }
        check_error(melee_web_save_profile_owner_snapshot_card_data(
                        profile, fresh_card_profile, sizeof(fresh_card_profile),
                        error, sizeof(error)), error);
        check_error(melee_web_save_profile_owner_capture_preferences(
                        profile, &fresh_preferences, error, sizeof(error)), error);
        check(fresh_preferences.item_frequency == 2 &&
                  fresh_preferences.item_mask == UINT64_MAX &&
                  fresh_preferences.saved_language == LANG_US &&
                  !memcmp(fresh_preferences.rumble_enabled,
                          (const uint8_t[4]) { 1, 1, 1, 1 }, 4),
              "Fresh source preferences do not match the original initializer");
        /* The first Personal snapshot has no stored browser profile to fall
         * back on. Simulate temporary startup writes and require the captured
         * source defaults to reproduce the original fresh card data. */
        save->x1CB0.item_freq = 0xFF;
        save->x1CB0.item_mask = UINT64_C(0x0102040810204080);
        save->x1CB0.rumble_enabled[0] = 0;
        save->x1CB0.rumble_enabled[1] = 0;
        save->x1CB0.saved_language = LANG_JP;
        check_error(melee_web_save_profile_owner_snapshot_card_data_with_preferences(
                        profile, &fresh_preferences, card_profile_after,
                        sizeof(card_profile_after), error, sizeof(error)), error);
        check(!memcmp(fresh_card_profile, card_profile_after,
                      sizeof(fresh_card_profile)),
              "First fresh Personal snapshot persisted temporary startup preferences");
        check_error(melee_web_save_profile_owner_apply_card_data(
                        profile, fresh_card_profile, sizeof(fresh_card_profile),
                        error, sizeof(error)), error);
        check(!IsNameValid(GM_NAMETAG_NONE) &&
                  GetNameText(GM_NAMETAG_NONE) == NULL,
              "Fresh no-name sentinel resolved to a saved name row");
        memcpy(GetPersistentNameData(0)->namedata, "ALFA", 5);
        memcpy(GetPersistentNameData(1)->namedata, "BETA", 5);
        struct PlayerInitData players[GM_MAX_PLAYERS];
        gm_SetupAllPlayerDefaults(players);
        players[0].slot_type = Gm_PKind_Human;
        players[1].slot_type = Gm_PKind_Cpu;
        players[0].nametag = 0;
        players[1].nametag = 1;
        check(IsNameValid(players[0].nametag) &&
                  IsNameValid(players[1].nametag) &&
                  strcmp(GetNameText(players[0].nametag), "ALFA") == 0 &&
                  strcmp(GetNameText(players[1].nametag), "BETA") == 0 &&
                  players[2].slot_type == Gm_PKind_NA &&
                  players[2].nametag == GM_NAMETAG_NONE,
              "Independent saved name selections did not resolve their own profile rows");
    }
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
    check_error(melee_web_save_profile_owner_initialize_everything(
                    profile, error, sizeof(error)), error);
    check(!gm_801721EC(),
          "Everything baseline retained transient new-completion notifications");
    check(gm_80164ABC() && gm_80164600(),
          "Everything baseline did not unlock the source character/stage tables");
    check(save->x1A68 == ((UINT64_C(1) << 51) - 1) &&
              gmMainLib_8015CF94(),
          "Everything baseline did not complete all 51 events and the final-event condition");
    check(save->trophy_count == TY_TROPHY_COUNT,
          "Everything baseline did not award the complete source trophy table");
    for (size_t trophy = 0; trophy < TY_TROPHY_COUNT; ++trophy)
        check((save->trophy_flags[trophy] & 0x8000) != 0 &&
                  (save->trophy_flags[trophy] & 0x00FF) == 1,
              "Everything baseline trophy flags do not match the original award routine");
    check((save->x186C & 0x0F) == 0x0F && (save->x186C & 0xF0) == 0,
          "Everything baseline did not derive only the four source-supported feature bits");
    check(gmMainLib_8015EDC8()->x4 && gmMainLib_8015EDC8()->x5 &&
              gmMainLib_8015EDC8()->x6,
          "Everything baseline did not derive all authored roster/stage completion flags");
    for (int selkind = 0; selkind < SELKIND_COUNT; ++selkind) {
        const u8 ckind = gm_SelKindToCKind((u8) selkind);
        const u16 clear_ids[] = {
            gm_80160474(ckind, GM_CLASSIC),
            gm_80160474(ckind, GM_ADVENTURE),
            gm_80160474(ckind, GM_ALLSTAR),
        };
        for (size_t mode = 0; mode < sizeof(clear_ids) / sizeof(clear_ids[0]); ++mode)
                check(gmMainLib_8015DA90(clear_ids[mode]) != 0,
                      "Everything baseline omitted a source-mapped 1P reward from the persisted ledger");
    }
    {
        size_t completed_challenges = 0;
        for (int challenge = 0; challenge < 0x100; ++challenge) {
            const int excluded = challenge == 9 || challenge == 0x29 ||
                challenge == 0x42 || challenge == 0x43 ||
                challenge == 0xB9 || challenge == 0xC9 || challenge == 0xCA;
            check((gmMainLib_8015DADC(challenge) != 0) == !excluded,
                  "Everything baseline challenge flags diverged from the source inventory");
            completed_challenges += !excluded;
        }
        check(completed_challenges == 249 && gmMainLib_8015D8D8(0x123),
              "Everything baseline omitted the source all-challenges award");
    }
    check_error(melee_web_save_profile_owner_restore_default(
                    profile, error, sizeof(error)), error);
    check(save->unlocked_characers_bitmask == 0 && save->x186A == 0 &&
              save->x1A68 == 0 && save->trophy_count == 0 && save->x186C == 0,
          "restoring a fresh Personal profile retained completed baseline progress");
    check_error(melee_web_save_profile_owner_snapshot_card_data(
                    profile, card_profile_after, sizeof(card_profile_after),
                    error, sizeof(error)), error);
    if (memcmp(fresh_card_profile, card_profile_after, sizeof(fresh_card_profile))) {
        for (size_t byte = 0; byte < sizeof(fresh_card_profile); ++byte) {
            if (fresh_card_profile[byte] != card_profile_after[byte]) {
                fprintf(stderr, "fresh profile differs at 0x%zx: %02x != %02x\n",
                        byte, fresh_card_profile[byte], card_profile_after[byte]);
                break;
            }
        }
    }
    check(!memcmp(fresh_card_profile, card_profile_after, sizeof(fresh_card_profile)),
          "restoring Personal did not restore the exact fresh source card profile");
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
    {
        struct NameTagDataBank* banks =
            (struct NameTagDataBank*) gmMainLib_8015CC4C();
        save->time_matches = 0x12345678;
        save->x1A68 = INT64_C(0x0123456789ABCDEF);
        save->x1F2C[0].fighter_kos[0] = 0x2345;
        save->x1F2C[0].x7C.b0 = 1;
        save->x1F2C[0].x7C.b789 = 5;
        banks[0].inner[0].sd_count = 0x1357;
        banks[6].inner[18].victories = 0x2468;
        check_error(melee_web_save_profile_owner_snapshot_card_data(
                        profile, card_profile, sizeof(card_profile), error,
                        sizeof(error)), error);
        check(card_profile[0] == 0x07 && card_profile[1] == 0xFF &&
                  card_profile[2] == 0x01 && card_profile[3] == 0xC0 &&
                  card_profile[0x1B0] == 0x12 &&
                  card_profile[0x1B1] == 0x34 &&
                  card_profile[0x1B2] == 0x56 &&
                  card_profile[0x1B3] == 0x78 &&
                  card_profile[0x200] == 0x01 &&
                  card_profile[0x207] == 0xEF,
              "card profile snapshot did not encode source scalars as big-endian");
        save->time_matches = 0;
        save->x1A68 = 0;
        save->x1F2C[0].fighter_kos[0] = 0;
        save->x1F2C[0].x7C.b0 = 0;
        save->x1F2C[0].x7C.b789 = 0;
        banks[0].inner[0].sd_count = 0;
        banks[6].inner[18].victories = 0;
        check_error(melee_web_save_profile_owner_apply_card_data(
                        profile, card_profile, sizeof(card_profile), error,
                        sizeof(error)), error);
        check(save->time_matches == 0x12345678 &&
                  save->x1A68 == INT64_C(0x0123456789ABCDEF) &&
                  save->x1F2C[0].fighter_kos[0] == 0x2345 &&
                  save->x1F2C[0].x7C.b0 == 1 &&
                  save->x1F2C[0].x7C.b789 == 5 &&
                  banks[0].inner[0].sd_count == 0x1357 &&
                  banks[6].inner[18].victories == 0x2468,
              "card profile import did not restore persistent fields and all bank rows");
        check_error(melee_web_save_profile_owner_snapshot_card_data(
                        profile, card_profile_after, sizeof(card_profile_after),
                        error, sizeof(error)), error);
        check(!memcmp(card_profile, card_profile_after, sizeof(card_profile)),
              "card profile snapshot/import was not byte-stable");
    }
    {
        unsigned char source_rules[0x18] = { 0 };
        unsigned char source_save[0x55E8] = { 0 };
        source_rules[0x00] = 0x01;
        source_rules[0x01] = 0x34;
        source_rules[0x02] = 0x00;
        source_rules[0x03] = 0x02;
        source_rules[0x04] = 0x03;
        source_rules[0x14] = 0xFF;
        source_rules[0x15] = 0xFF;
        source_rules[0x16] = 0xFF;
        source_rules[0x17] = 0xF9; /* signed unk_14 = -7 */
        source_save[0x00] = 0x07; source_save[0x01] = 0xFF;
        source_save[0x02] = 0x01; source_save[0x03] = 0xC0;
        source_save[0x04] = 0x04;
        source_save[0x448] = 0x03; /* item frequency in gmm_x1CB0 */
        source_save[0x450] = 0x11; source_save[0x451] = 0x22;
        source_save[0x458] = 0; source_save[0x459] = 1;
        source_save[0x45A] = 0; source_save[0x45B] = 1;
        source_save[0x45E] = LANG_JP;
        source_save[0x460] = 0x00; source_save[0x461] = 0x00;
        source_save[0x462] = 0x01; source_save[0x463] = 0xC0;
        source_save[0x468] = 0x00; source_save[0x469] = 0x05;
        source_save[0x06C4] = 0x12; source_save[0x06C5] = 0x34;
        source_save[0x06C4 + 0x7A] = 0x80;
        source_save[0x06C4 + 0x7C] = 0x81;
        source_save[0x06C4 + 0x7D] = 0x40;
        source_save[0x06C4 + 0x7E] = 0x11;
        source_save[0x06C4 + 0x7F] = 0x22;
        check_error(melee_web_save_profile_owner_apply_reference_context(
                        profile, source_rules, source_save, error, sizeof(error)), error);
        check(gmMainLib_GetGameRules()->mode == 0 &&
                  gmMainLib_GetGameRules()->stock_count == 3 &&
                  gmMainLib_GetGameRules()->unk_14 == -7,
              "first-CSS GameRules source endian translation failed");
        check(save->unlocked_characers_bitmask == 0x07FF &&
                  save->x186A == 0x01C0 && save->x186C == 4 &&
                  save->x1CB0.item_freq == 3 &&
                  save->x1CB0.item_mask == 0x1122000000000000ULL &&
                  save->x1CB0.stage_mask == 0x01C0 && save->trophy_count == 5 &&
                  save->x1F2C[0].fighter_kos[0] == 0x1234 &&
                  save->x1F2C[0].x7A.u8 == 0x80 &&
                  save->x1F2C[0].x7C.b0 == 1 &&
                  save->x1F2C[0].x7C.b789 == 5 &&
                  save->x1F2C[0].x7C.x7E == 0x1122,
              "first-CSS SaveData source endian translation failed");
        {
            MeleeWebSaveProfilePreferences imported_preferences;
            check_error(melee_web_save_profile_owner_capture_preferences(
                            profile, &imported_preferences, error,
                            sizeof(error)), error);
            check(imported_preferences.item_frequency == 3 &&
                      imported_preferences.item_mask ==
                          UINT64_C(0x1122000000000000) &&
                      imported_preferences.saved_language == LANG_JP &&
                      !memcmp(imported_preferences.rumble_enabled,
                              (const uint8_t[4]) { 0, 1, 0, 1 }, 4),
                  "Imported source preference capture lost a typed field");

            /* Model the browser's runtime-only overrides after import. A
             * source progress counter changes at the same time, so the
             * snapshot must restore preferences without freezing progress. */
            save->x1CB0.item_freq = 0xFF;
            save->x1CB0.item_mask = UINT64_MAX;
            save->x1CB0.rumble_enabled[0] = 1;
            save->x1CB0.rumble_enabled[1] = 1;
            save->x1CB0.saved_language = LANG_US;
            save->x1A50 = 0x10203040;
            check_error(melee_web_save_profile_owner_snapshot_card_data_with_preferences(
                            profile, &imported_preferences, card_profile_after,
                            sizeof(card_profile_after), error, sizeof(error)), error);
            check(card_profile_after[0x448] == 3 &&
                      !memcmp(card_profile_after + 0x450,
                              (const uint8_t[8]) { 0x11, 0x22, 0, 0, 0, 0, 0, 0 }, 8) &&
                      !memcmp(card_profile_after + 0x458,
                              (const uint8_t[4]) { 0, 1, 0, 1 }, 4) &&
                      card_profile_after[0x45E] == LANG_JP,
                  "Runtime overrides replaced imported preferences in the card snapshot");
            check(!memcmp(card_profile_after + 0x1E8,
                          (const uint8_t[4]) { 0x10, 0x20, 0x30, 0x40 }, 4),
                  "Preference snapshot overlay froze a changed source progress counter");
        }
    }
    mutate_original_profile();
    check_error(melee_web_save_profile_owner_live(profile, error, sizeof(error)), error);
    check(!melee_web_save_profile_owner_destroy(profile, error, sizeof(error)),
          "active owner was destroyed");
    check_error(melee_web_save_profile_owner_deactivate(profile, error, sizeof(error)),
                error);
    check(!memcmp(source_root, source_before, sizeof(source_before)),
          "full original profile backing was not restored");
    check(strcmp(GetPersistentNameData(0)->namedata, "SAVE") == 0,
          "Existing saved name bytes were not restored with the source profile");
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
