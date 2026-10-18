/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SLIPPI_PROFILE_UCF_INPUT_HISTORY_H
#define SLIPPI_PROFILE_UCF_INPUT_HISTORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SLIPPI_UCF_INPUT_HISTORY_SLOT_COUNT = 4,
    SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES = 12,
    SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES = 48,
    SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION = 1,
};

/*
 * One isolated owner of the authored 48-byte history payload. The owner token
 * is caller-provided metadata and is not part of the payload or snapshot bytes.
 * Do not serialize this struct: capture/restore operate on the payload array
 * and fieldwise version/token values only.
 */
typedef struct SlippiUcfInputHistory {
    uint64_t owner_token;
    uint8_t payload[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
} SlippiUcfInputHistory;

/*
 * Initialize a fresh, not-currently-owned object for one session. The caller
 * supplies a unique nonzero token and guarantees it is not reused for another
 * owner while snapshots may exist. This is not a reset or reinitialization API.
 * Invalid arguments leave the object unchanged.
 */
bool slippi_profile_ucf_input_history_init_fresh(
    SlippiUcfInputHistory *owner, uint64_t owner_token);

/* End the owner lifetime, zeroing the payload and invalidating its token. */
void slippi_profile_ucf_input_history_dispose(SlippiUcfInputHistory *owner);

/*
 * Read one processed-PAD row as exactly 12 raw bytes. Slot is signed and must
 * be in [0, 4). The output byte range must not overlap the owner object.
 */
bool slippi_profile_ucf_input_history_read_row(
    const SlippiUcfInputHistory *owner, int32_t processed_pad_slot,
    uint8_t *out_row, size_t out_size);

/*
 * Capture the canonical 48 payload bytes and fieldwise envelope metadata.
 * Payload and metadata outputs must be nonnull, correctly aligned for their C
 * types, and pairwise disjoint from each other and the owner. Byte buffers may
 * be unaligned. Invalid arguments leave all outputs unchanged.
 */
bool slippi_profile_ucf_input_history_capture(
    const SlippiUcfInputHistory *owner, uint8_t *out_payload,
    size_t payload_size, uint32_t *out_format_version,
    uint64_t *out_owner_token);

/*
 * Restore only an exact compatible snapshot. The input bytes must not overlap
 * the owner object; they may be unaligned. Invalid arguments leave the owner
 * unchanged. Every payload byte, including reserved bytes, is copied exactly.
 */
bool slippi_profile_ucf_input_history_restore(
    SlippiUcfInputHistory *owner, uint32_t format_version,
    uint64_t snapshot_owner_token, const uint8_t *payload,
    size_t payload_size);

#ifdef __cplusplus
}
#endif

#endif
