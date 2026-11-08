/* SPDX-License-Identifier: GPL-3.0-or-later */

int melee_web_save_profile_owner_initialize_fresh_online(
    MeleeWebSaveProfileOwner* candidate, char* error, size_t error_size)
{
    if (!melee_web_save_profile_owner_live(candidate, error, error_size))
        return 0;
    if (!candidate->default_initialized || candidate->everything_initialized ||
        candidate->online_initialized)
        return fail(error, error_size,
                    "Fresh online baseline requires an unused original default profile");
    if (!aliases_match(candidate, error, error_size))
        return 0;
    gm_8016468C();
    candidate->online_initialized = 1;
    return ok(error, error_size);
}
