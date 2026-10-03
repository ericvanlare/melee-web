/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * This fixture-only helper is adapted from the pinned
 * slippi-ssbm-asm/Online/Core/InitOnlinePlay.asm source at
 * d0a5df91c9b535b3c711880a4156769c2b3e799e7e72bc9d071be9fd90394067.
 * The immediate reset follows lines 133-136; the per-frame rotate/add follows
 * FN_SyncRNG lines 286-300. Keep this file under the pinned ASM repository's
 * GPL-3.0-only terms and outside the root MIT fixture source.
 */
#include <cstdint>

extern "C" uint32_t slippi_rng_profile_reset_seed(uint32_t offset)
{
    return offset;
}

extern "C" uint32_t slippi_rng_profile_sync_seed(uint32_t global_frame, uint32_t offset)
{
    const uint32_t rotated=(global_frame << 16) | (global_frame >> 16);
    return rotated + offset;
}
