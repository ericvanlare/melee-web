/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <melee/gr/forward.h>
#include <melee/lb/lbtime.h>
#include <sysdolphin/baselib/random.h>
#include <emscripten.h>

static void check_fresh_online_baseline(void)
{
    static unsigned char cards[2][MELEE_WEB_SAVE_PROFILE_CARD_BYTES];
    const u32 seeds[] = { 0x713C2568, 0x05EA629B };
    u32 previous_time = 0;
    u32 saved_seed = *seed_ptr;
    char error[256] = { 0 };

    for (size_t attempt = 0; attempt < 2; ++attempt) {
        MeleeWebSaveProfileOwner* profile;
        u32 current_time;

        if (attempt != 0)
            emscripten_sleep(1100);
        current_time = lbTime_GetTimeInSeconds();
        check(attempt == 0 || current_time != previous_time,
              "Fresh online samples must use distinct source times");
        previous_time = current_time;
        *seed_ptr = seeds[attempt];
        profile = melee_web_save_profile_owner_create(error, sizeof(error));
        check_error(profile != NULL, error);
        check_error(melee_web_save_profile_owner_activate(profile, error, sizeof(error)),
                    error);
        check(!melee_web_save_profile_owner_initialize_fresh_online(
                  profile, error, sizeof(error)),
              "Fresh online accepted an uninitialized default profile");
        check_error(melee_web_save_profile_owner_initialize_default(
                        profile, error, sizeof(error)), error);
        check(!gm_80164430(St_Kind_Last), "Original fresh profile already admitted FD");
        check_error(melee_web_save_profile_owner_initialize_fresh_online(
                        profile, error, sizeof(error)), error);
        check(gm_80164430(St_Kind_Last), "Original stage gate rejected fresh online FD");
        check(!melee_web_save_profile_owner_initialize_fresh_online(
                  profile, error, sizeof(error)),
              "Fresh online accepted a second initialization");
        check(!melee_web_save_profile_owner_initialize_everything(
                  profile, error, sizeof(error)),
              "Fresh online profile admitted Everything unlocked initialization");
        check(*seed_ptr == seeds[attempt], "Fresh online initialization changed the RNG");
        check_error(melee_web_save_profile_owner_snapshot_card_data(
                        profile, cards[attempt], sizeof(cards[attempt]),
                        error, sizeof(error)), error);
        check_error(melee_web_save_profile_owner_deactivate(profile, error, sizeof(error)),
                    error);
        check_error(melee_web_save_profile_owner_destroy(profile, error, sizeof(error)),
                    error);
    }
    check(!memcmp(cards[0], cards[1], sizeof(cards[0])),
          "Fresh online card bytes differed across source seeds and times");
    *seed_ptr = saved_seed;
}
