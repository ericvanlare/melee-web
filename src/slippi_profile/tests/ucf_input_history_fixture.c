/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <slippi_profile/ucf_input_history.h>

_Static_assert(CHAR_BIT == 8, "fixture requires 8-bit bytes");
_Static_assert(SLIPPI_UCF_INPUT_HISTORY_SLOT_COUNT == 4, "four processed PAD rows");
_Static_assert(SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES == 12, "12-byte row stride");
_Static_assert(SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES == 48, "48-byte payload");

static const uint64_t OWNER_ONE = UINT64_C(0x1122334455667788);
static const uint64_t OWNER_TWO = UINT64_C(0x8877665544332211);

static void make_seed(uint8_t seed[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES])
{
    const uint8_t samples[4][8] = {
        {0x00, 0x7f, 0x80, 0xff, 0x01, 0x02, 0x03, 0x04},
        {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
        {0x90, 0x81, 0x72, 0x63, 0x54, 0x45, 0x36, 0x27},
        {0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87},
    };
    const uint8_t cursors[4] = {0, 1, 2, 3};
    const uint8_t counters[4] = {1, 2, 254, 255};

    memset(seed, 0, SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES);
    for (size_t slot = 0; slot < 4; ++slot) {
        const size_t row = slot * SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES;
        memcpy(seed + row, samples[slot], sizeof(samples[slot]));
        seed[row + 8] = cursors[slot];
        seed[row + 9] = counters[slot];
        seed[row + 10] = (uint8_t) (0xa1 + slot * 2);
        seed[row + 11] = (uint8_t) (0xa2 + slot * 2);
    }
}

static void expect_capture(const SlippiUcfInputHistory *owner,
                           const uint8_t expected[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES],
                           uint64_t expected_owner)
{
    uint8_t payload[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
    uint32_t version = UINT32_C(0xdeadbeef);
    uint64_t owner_token = UINT64_C(0xdeadbeefdeadbeef);
    memset(payload, 0x5a, sizeof(payload));
    assert(slippi_profile_ucf_input_history_capture(
        owner, payload, sizeof(payload), &version, &owner_token));
    assert(version == SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION);
    assert(owner_token == expected_owner);
    assert(memcmp(payload, expected, sizeof(payload)) == 0);
}

static void expect_owner_unchanged(
    const SlippiUcfInputHistory *owner,
    const uint8_t expected[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES],
    uint64_t expected_owner)
{
    expect_capture(owner, expected, expected_owner);
}

static void print_snapshot_hex(const uint8_t payload[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES])
{
    fputs("SNAPSHOT|", stdout);
    for (size_t i = 0; i < SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES; ++i) {
        printf("%02x", (unsigned) payload[i]);
    }
    putchar('\n');
}

static void test_initialization_and_row_reads(void)
{
    SlippiUcfInputHistory owner;
    memset(&owner, 0xa5, sizeof(owner));
    assert(slippi_profile_ucf_input_history_init_fresh(&owner, OWNER_ONE));

    uint8_t zero[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES] = {0};
    expect_capture(&owner, zero, OWNER_ONE);

    for (int32_t slot = 0; slot < 4; ++slot) {
        uint8_t guarded[14];
        memset(guarded, 0xc7, sizeof(guarded));
        assert(slippi_profile_ucf_input_history_read_row(
            &owner, slot, guarded + 1, SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES));
        assert(guarded[0] == 0xc7 && guarded[13] == 0xc7);
        for (size_t i = 1; i <= 12; ++i) assert(guarded[i] == 0);
    }

    uint8_t rejected[14];
    memset(rejected, 0x6d, sizeof(rejected));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, -1, rejected + 1, 12));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 4, rejected + 1, 12));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 0, rejected + 1, 0));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 0, rejected + 1, 11));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 0, rejected + 1, 13));
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 0, NULL, 12));
    assert(!slippi_profile_ucf_input_history_read_row(NULL, 0, rejected + 1, 12));
    for (size_t i = 0; i < sizeof(rejected); ++i) assert(rejected[i] == 0x6d);

    SlippiUcfInputHistory untouched;
    memset(&untouched, 0x39, sizeof(untouched));
    uint8_t before[sizeof(untouched)];
    memcpy(before, &untouched, sizeof(before));
    assert(!slippi_profile_ucf_input_history_init_fresh(&untouched, 0));
    assert(memcmp(&untouched, before, sizeof(before)) == 0);
    assert(!slippi_profile_ucf_input_history_init_fresh(NULL, OWNER_ONE));
    slippi_profile_ucf_input_history_dispose(NULL);
    slippi_profile_ucf_input_history_dispose(&owner);
}

static void test_snapshot_round_trip_and_guards(void)
{
    SlippiUcfInputHistory owner;
    SlippiUcfInputHistory other_owner;
    memset(&owner, 0, sizeof(owner));
    memset(&other_owner, 0, sizeof(other_owner));
    assert(slippi_profile_ucf_input_history_init_fresh(&owner, OWNER_ONE));
    assert(slippi_profile_ucf_input_history_init_fresh(&other_owner, OWNER_TWO));

    uint8_t seed[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
    uint8_t misaligned_input[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES + 2];
    make_seed(seed);
    misaligned_input[0] = 0x3c;
    memcpy(misaligned_input + 1, seed, sizeof(seed));
    misaligned_input[sizeof(misaligned_input) - 1] = 0xc3;
    assert(slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        misaligned_input + 1, sizeof(seed)));
    assert(misaligned_input[0] == 0x3c);
    assert(misaligned_input[sizeof(misaligned_input) - 1] == 0xc3);
    expect_owner_unchanged(&owner, seed, OWNER_ONE);

    for (int32_t slot = 0; slot < 4; ++slot) {
        uint8_t row[SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES];
        const size_t offset = (size_t) slot * SLIPPI_UCF_INPUT_HISTORY_ROW_BYTES;
        assert(slippi_profile_ucf_input_history_read_row(
            &owner, slot, row, sizeof(row)));
        assert(memcmp(row, seed + offset, sizeof(row)) == 0);
    }

    uint8_t capture_guarded[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES + 2];
    memset(capture_guarded, 0xb6, sizeof(capture_guarded));
    uint32_t version = UINT32_C(0xdeadbeef);
    uint64_t token = UINT64_C(0xdeadbeefdeadbeef);
    assert(slippi_profile_ucf_input_history_capture(
        &owner, capture_guarded + 1, sizeof(seed), &version, &token));
    assert(version == SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION);
    assert(token == OWNER_ONE);
    assert(memcmp(capture_guarded + 1, seed, sizeof(seed)) == 0);
    assert(capture_guarded[0] == 0xb6);
    assert(capture_guarded[sizeof(capture_guarded) - 1] == 0xb6);
    print_snapshot_hex(capture_guarded + 1);

    uint8_t changed[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
    memcpy(changed, seed, sizeof(changed));
    changed[0] ^= 0xff;
    changed[47] ^= 0x5a;
    uint8_t oversized_changed[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES + 1];
    memcpy(oversized_changed, changed, sizeof(changed));
    oversized_changed[sizeof(changed)] = 0x9d;
    assert(slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        changed, sizeof(changed)));
    expect_owner_unchanged(&owner, changed, OWNER_ONE);
    assert(slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        seed, sizeof(seed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);

    uint8_t untouched_owner[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES] = {0};
    expect_owner_unchanged(&other_owner, untouched_owner, OWNER_TWO);
    assert(!slippi_profile_ucf_input_history_restore(
        &other_owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        seed, sizeof(seed)));
    expect_owner_unchanged(&other_owner, untouched_owner, OWNER_TWO);

    const size_t bad_lengths[] = {0, 47, 49};
    for (size_t i = 0; i < sizeof(bad_lengths) / sizeof(bad_lengths[0]); ++i) {
        const uint8_t *input = bad_lengths[i] == 49 ? oversized_changed : changed;
        assert(!slippi_profile_ucf_input_history_restore(
            &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
            input, bad_lengths[i]));
        expect_owner_unchanged(&owner, seed, OWNER_ONE);
    }
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, 0, OWNER_ONE, changed, sizeof(changed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION + 1, OWNER_ONE,
        changed, sizeof(changed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, 0, changed, sizeof(changed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_TWO,
        changed, sizeof(changed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        NULL, sizeof(seed)));
    expect_owner_unchanged(&owner, seed, OWNER_ONE);
    assert(!slippi_profile_ucf_input_history_restore(
        NULL, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        changed, sizeof(changed)));

    uint8_t capture_untouched[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
    memset(capture_untouched, 0x5e, sizeof(capture_untouched));
    uint8_t capture_before[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES];
    memset(capture_before, 0x5e, sizeof(capture_before));
    uint32_t version_before = UINT32_C(0x13579bdf);
    uint64_t token_before = UINT64_C(0x0123456789abcdef);
    uint8_t capture_extended[SLIPPI_UCF_INPUT_HISTORY_PAYLOAD_BYTES + 2];
    memset(capture_extended, 0x5e, sizeof(capture_extended));
    uint8_t capture_extended_before[sizeof(capture_extended)];
    memcpy(capture_extended_before, capture_extended, sizeof(capture_extended));
    version = version_before;
    token = token_before;
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_untouched, 0, &version, &token));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(version == version_before && token == token_before);
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_untouched, 47, &version, &token));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(version == version_before && token == token_before);
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_extended + 1, 49, &version, &token));
    assert(memcmp(capture_extended, capture_extended_before,
                  sizeof(capture_extended)) == 0);
    assert(version == version_before && token == token_before);
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, NULL, sizeof(seed), &version, &token));
    assert(version == version_before && token == token_before);
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_untouched, sizeof(seed), NULL, &token));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(token == token_before);
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_untouched, sizeof(seed), &version, NULL));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(version == version_before);
    assert(!slippi_profile_ucf_input_history_capture(
        NULL, capture_untouched, sizeof(seed), &version, &token));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(version == version_before && token == token_before);

    slippi_profile_ucf_input_history_dispose(&owner);
    assert(!slippi_profile_ucf_input_history_read_row(&owner, 0, capture_untouched, 12));
    assert(!slippi_profile_ucf_input_history_restore(
        &owner, SLIPPI_UCF_INPUT_HISTORY_FORMAT_VERSION, OWNER_ONE,
        seed, sizeof(seed)));
    memset(capture_untouched, 0x5e, sizeof(capture_untouched));
    version = version_before;
    token = token_before;
    assert(!slippi_profile_ucf_input_history_capture(
        &owner, capture_untouched, sizeof(seed), &version, &token));
    assert(memcmp(capture_untouched, capture_before, sizeof(capture_untouched)) == 0);
    assert(version == version_before && token == token_before);
    slippi_profile_ucf_input_history_dispose(&other_owner);
}

int main(void)
{
    test_initialization_and_row_reads();
    test_snapshot_round_trip_and_guards();
    puts("PASS ucf-input-history: 48-byte snapshots, four rows, transactional guards");
    return 0;
}
