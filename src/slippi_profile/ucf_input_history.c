/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slippi_profile/ucf_input_history.h"

#include <limits.h>
#include <string.h>

#if CHAR_BIT != 8
#error "UCF input history requires 8-bit bytes"
#endif

_Static_assert(sizeof(uint8_t) == 1, "uint8_t must be one byte");
_Static_assert(SLIPPI_UCF_INPUT_HISTORY_SLOT_COUNT *
                   SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES ==
                   SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES,
               "history rows must cover the exact payload");

bool slippi_profile_ucf_input_history_init_fresh(
    SlippiUcfInputHistory *owner, uint64_t owner_token)
{
    if (owner == NULL || owner_token == 0) {
        return false;
    }

    memset(owner->payload, 0, sizeof(owner->payload));
    owner->owner_token = owner_token;
    return true;
}

void slippi_profile_ucf_input_history_dispose(SlippiUcfInputHistory *owner)
{
    if (owner == NULL) {
        return;
    }

    memset(owner->payload, 0, sizeof(owner->payload));
    owner->owner_token = 0;
}

bool slippi_profile_ucf_input_history_read_row(
    const SlippiUcfInputHistory *owner, int32_t processed_pad_slot,
    uint8_t *out_row, size_t out_size)
{
    if (owner == NULL || owner->owner_token == 0 || out_row == NULL ||
        out_size != SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES ||
        processed_pad_slot < 0 ||
        processed_pad_slot >= SLIPPI_UCF_INPUT_HISTORY_SLOT_COUNT) {
        return false;
    }

    const size_t offset = (size_t) processed_pad_slot *
                          SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES;
    memcpy(out_row, owner->payload + offset,
           SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES);
    return true;
}

bool slippi_profile_ucf_input_history_capture(
    const SlippiUcfInputHistory *owner, uint8_t *out_payload,
    size_t payload_size, uint32_t *out_format_version,
    uint64_t *out_owner_token)
{
    if (owner == NULL || owner->owner_token == 0 || out_payload == NULL ||
        payload_size != SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES ||
        out_format_version == NULL || out_owner_token == NULL) {
        return false;
    }

    memcpy(out_payload, owner->payload, sizeof(owner->payload));
    *out_format_version = SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION;
    *out_owner_token = owner->owner_token;
    return true;
}

bool slippi_profile_ucf_input_history_restore(
    SlippiUcfInputHistory *owner, uint32_t format_version,
    uint64_t snapshot_owner_token, const uint8_t *payload,
    size_t payload_size)
{
    if (owner == NULL || owner->owner_token == 0 || payload == NULL ||
        payload_size != SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES ||
        format_version != SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION ||
        snapshot_owner_token == 0 || snapshot_owner_token != owner->owner_token) {
        return false;
    }

    memcpy(owner->payload, payload, sizeof(owner->payload));
    return true;
}
