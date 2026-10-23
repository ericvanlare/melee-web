/* Actual source clock and flow owner control. The host-side PAD/audio leaves
 * are explicit fixtures; clock state and transitions come from gm_1A45.c. */
#include "../src/gameplay_match_flow.c"
#ifndef MELEE_WEB_TEST_CLOCK_SOURCE
#define MELEE_WEB_TEST_CLOCK_SOURCE "../build/gameplay-source/src/melee/gm/gm_1A45.c"
#endif
#include MELEE_WEB_TEST_CLOCK_SOURCE

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint64_t fixture_generation = 9;
static u64 prior_mask = 0x12345678ULL;
uint64_t melee_web_gameplay_generation(void) { return fixture_generation; }
int melee_web_match_end_state(void) { return 0; }
int melee_web_match_source_result(void) { return OUTCOME_NONE; }

HSD_GObjLibInitDataType HSD_GObjLibInitData = {0};
HSD_GObj *HSD_GObj_804D7814, *HSD_GObj_804D7818, *HSD_GObj_804D781C;
HSD_PadStatus HSD_PadMasterStatus[4];

int melee_web_controller_map_begin(void) { return 1; }
int melee_web_controller_map_end(void) { return 1; }
int lb_80019A30(int channel) { return channel == 0 || channel == 1; }
void gm_EvaluateAllControllerInputs(void) {}
void lbAudioAx_80027DF8(void) {}
void gm_801677C0(struct gm_801677C0_s* state)
{
    state->x0 = state->x1 = state->x2 = state->x3 = 0;
    state->unk_20 = state->unk_28 = 0;
}

static melee_source_bool unsupported_callback(void) { assert(0); return false; }
static unsigned frame_calls;
static MeleeWebMatchFlowSnapshot* callback_snapshot;

typedef struct ClockOwnerBytes {
    unsigned char record[sizeof(gm_80479D58)];
    unsigned char saved[sizeof(melee_source_clock_saved)];
    unsigned char flow[sizeof(MeleeWebMatchFlow)];
    const void* hsd_mask_owner;
    u64 process_mask;
    u64* saved_mask_ptr;
    u64 saved_mask_value;
    u64 lifetime;
    int owner, prepared, in_callback;
} ClockOwnerBytes;

static ClockOwnerBytes owner_bytes(const MeleeWebMatchFlow* flow)
{
    ClockOwnerBytes bytes = {0};
    memcpy(bytes.record, &gm_80479D58, sizeof(gm_80479D58));
    memcpy(bytes.saved, &melee_source_clock_saved, sizeof(melee_source_clock_saved));
    memcpy(bytes.flow, flow, sizeof(*flow));
    bytes.hsd_mask_owner = HSD_GObjLibInitData.unk_2;
    bytes.process_mask = melee_source_clock_process_mask;
    bytes.saved_mask_ptr = melee_source_clock_saved_mask_ptr;
    bytes.saved_mask_value = melee_source_clock_saved_mask;
    bytes.lifetime = melee_source_clock_lifetime;
    bytes.owner = melee_source_clock_owner;
    bytes.prepared = melee_source_clock_prepared;
    bytes.in_callback = melee_source_clock_in_callback;
    return bytes;
}

static void assert_owner_bytes_equal(const ClockOwnerBytes* before,
                                     const MeleeWebMatchFlow* flow)
{
    ClockOwnerBytes after = owner_bytes(flow);
    assert(memcmp(before->record, after.record, sizeof(after.record)) == 0);
    assert(memcmp(before->saved, after.saved, sizeof(after.saved)) == 0);
    assert(memcmp(before->flow, after.flow, sizeof(after.flow)) == 0);
    assert(before->hsd_mask_owner == after.hsd_mask_owner);
    assert(before->process_mask == after.process_mask);
    assert(before->saved_mask_ptr == after.saved_mask_ptr);
    assert(before->saved_mask_value == after.saved_mask_value);
    assert(before->lifetime == after.lifetime);
    assert(before->owner == after.owner && before->prepared == after.prepared &&
           before->in_callback == after.in_callback);
}

static void assert_flow_admission_refused_without_mutation(
    MeleeWebMatchFlow* flow, MeleeWebMatchFlowSnapshot* snapshot)
{
    char error[160];
    ClockOwnerBytes before = owner_bytes(flow);
    unsigned char flow_snapshot_before[sizeof(*snapshot)];
    unsigned char clock_snapshot_before[sizeof(*snapshot->source_clock)];
    memcpy(flow_snapshot_before, snapshot, sizeof(flow_snapshot_before));
    memcpy(clock_snapshot_before, snapshot->source_clock, sizeof(clock_snapshot_before));
    assert(!melee_web_match_flow_snapshot_admit(flow, snapshot, error, sizeof(error)));
    assert_owner_bytes_equal(&before, flow);
    assert(memcmp(flow_snapshot_before, snapshot, sizeof(flow_snapshot_before)) == 0);
    assert(memcmp(clock_snapshot_before, snapshot->source_clock,
                  sizeof(clock_snapshot_before)) == 0);
}

static void assert_flow_capture_refused_without_mutation(MeleeWebMatchFlow* flow)
{
    char error[160];
    ClockOwnerBytes before = owner_bytes(flow);
    assert(!melee_web_match_flow_snapshot_capture(flow, error, sizeof(error)));
    assert_owner_bytes_equal(&before, flow);
}

static void assert_source_admission_refused_without_mutation(
    MeleeWebMatchFlow* flow, const MeleeWebSourceClockSnapshot* snapshot,
    const void* identity, uint64_t epoch, uint64_t generation)
{
    ClockOwnerBytes before = owner_bytes(flow);
    unsigned char snapshot_before[sizeof(*snapshot)];
    memcpy(snapshot_before, snapshot, sizeof(snapshot_before));
    assert(!melee_web_source_clock_snapshot_admit(
        snapshot, identity, epoch, generation));
    assert_owner_bytes_equal(&before, flow);
    assert(memcmp(snapshot_before, snapshot, sizeof(snapshot_before)) == 0);
}

int melee_web_match_source_frame(void)
{
    ++frame_calls;
    if (callback_snapshot) {
        char error[160];
        ClockOwnerBytes before = owner_bytes(owner);
        assert(!melee_web_match_flow_snapshot_admit(
            owner, callback_snapshot, error, sizeof(error)));
        assert(!melee_web_match_flow_snapshot_capture(
            owner, error, sizeof(error)));
        assert_owner_bytes_equal(&before, owner);
    }
    return 1;
}

int main(void)
{
    char error[160];
    MeleeWebMatchFlow flow = {fixture_generation, 1, 0, 0};
    owner = &flow;
    owner_epoch = 7;
    HSD_GObjLibInitData.unk_2 = &prior_mask;

    assert(melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH));
    assert(gm_80479D58.unk_10.x4[0] == NULL &&
           gm_80479D58.unk_10.x4[1] == NULL);
    puts("Normal source-clock begin preserves the NULL callback pair");
    assert(melee_source_clock_lifetime != 0);
    assert(gm_801A4BA8() == 0 && gm_801A4BB8() == 0);

    MeleeWebMatchFlowSnapshot* snapshot =
        melee_web_match_flow_snapshot_capture(&flow, error, sizeof(error));
    assert(snapshot);
    callback_snapshot = snapshot;
    assert(melee_web_match_flow_snapshot_admit(&flow, snapshot, error, sizeof(error)));

    /* Prepared/callback and scheduler traversal states refuse without changing
     * any source-owned bytes or the flow owner. */
    ClockOwnerBytes before = owner_bytes(&flow);
    melee_source_clock_prepared = 1;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    assert_flow_capture_refused_without_mutation(&flow);
    assert(melee_source_clock_prepared == 1);
    melee_source_clock_prepared = 0;
    assert_owner_bytes_equal(&before, &flow);

    melee_source_clock_in_callback = 1;
    before = owner_bytes(&flow);
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    assert_flow_capture_refused_without_mutation(&flow);
    assert(melee_source_clock_in_callback == 1);
    melee_source_clock_in_callback = 0;

    HSD_GObj marker = {0};
    HSD_GObj** callback_slots[] = {
        &HSD_GObj_804D781C, &HSD_GObj_804D7818, &HSD_GObj_804D7814,
    };
    for (unsigned i = 0; i < 3; ++i) {
        *callback_slots[i] = &marker;
        assert_flow_admission_refused_without_mutation(&flow, snapshot);
        assert_flow_capture_refused_without_mutation(&flow);
        *callback_slots[i] = NULL;
    }

    /* Source-supported callback, pause, and request topology is required. */
    melee_source_bool (*saved_callback)(void) = gm_80479D58.unk_10.x4[0];
    gm_80479D58.unk_10.x4[0] = fn_801A46F4;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    assert_flow_capture_refused_without_mutation(&flow);
    gm_80479D58.unk_10.x4[0] = saved_callback;
    assert(gm_80479D58.unk_10.x4[0] == saved_callback);

    /* Even a legitimate second profile cannot replace captured topology. */
    gm_801A4B1C();
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    gm_801A4B08(NULL, NULL);
    assert(melee_web_match_flow_snapshot_admit(&flow, snapshot, error, sizeof(error)));
    gm_801A4B50(1);
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    gm_801A4B50(0);

    gm_80479D58.unk_10.x0 |= 1U;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    gm_80479D58.unk_10.x0 &= (u8) ~1U;
    assert((gm_80479D58.unk_10.x0 & 1U) == 0);

    gm_80479D58.unk_C = 1;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    gm_80479D58.unk_C = 0;
    assert(gm_80479D58.unk_C == 0);

    gm_801A4B08(unsupported_callback, unsupported_callback);
    assert_flow_capture_refused_without_mutation(&flow);
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    gm_801A4B08(NULL, NULL);

    /* Changed active or saved ownership is refused. The deliberately invalid
     * saved pointer proves admission compares identity without dereferencing it. */
    u64* active_owner = HSD_GObjLibInitData.unk_2;
    HSD_GObjLibInitData.unk_2 = &melee_source_clock_process_mask + 1;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    assert_flow_capture_refused_without_mutation(&flow);
    HSD_GObjLibInitData.unk_2 = active_owner;
    assert(HSD_GObjLibInitData.unk_2 == active_owner);

    u64* original_saved_pointer = melee_source_clock_saved_mask_ptr;
    melee_source_clock_saved_mask_ptr = (u64*) (uintptr_t) 1;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    melee_source_clock_saved_mask_ptr = original_saved_pointer;
    assert(melee_source_clock_saved_mask_ptr == original_saved_pointer);

    u64 original_saved_mask = melee_source_clock_saved_mask;
    melee_source_clock_saved_mask ^= 1;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    melee_source_clock_saved_mask = original_saved_mask;
    assert(melee_source_clock_saved_mask == original_saved_mask);

    u32 saved_counter = melee_source_clock_saved.unk_0;
    ++melee_source_clock_saved.unk_0;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    melee_source_clock_saved.unk_0 = saved_counter;
    assert(melee_source_clock_saved.unk_0 == saved_counter);

    /* A real source pre/post/present advances the captured typed record and
     * process mask while stable owner/topology/lifetime still admits. */
    assert(melee_web_match_flow_pre(&flow, error, sizeof(error)));
    assert(frame_calls == 1 && flow.source_valid && melee_source_clock_prepared);
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    assert(melee_web_match_flow_post(&flow, error, sizeof(error)));
    assert(!melee_source_clock_prepared);
    assert(gm_801A4BA8() == 1 && gm_801A4BB8() == 1);
    assert(melee_web_match_flow_present(&flow, error, sizeof(error)));
    assert(gm_80479D58.unk_4 == 1);
    assert(melee_web_match_flow_snapshot_admit(&flow, snapshot, error, sizeof(error)));

    /* Identity/generation/epoch mismatches refuse without touching either
     * owner. The actual source generation remains the only live-world check. */
    MeleeWebMatchFlow other_flow = flow;
    assert_source_admission_refused_without_mutation(
        &flow, snapshot->source_clock, &other_flow, owner_epoch, fixture_generation);
    assert_source_admission_refused_without_mutation(
        &flow, snapshot->source_clock, NULL, owner_epoch, fixture_generation);
    assert_source_admission_refused_without_mutation(
        &flow, snapshot->source_clock, &flow, owner_epoch + 1, fixture_generation);
    assert_source_admission_refused_without_mutation(
        &flow, snapshot->source_clock, &flow, owner_epoch, fixture_generation + 1);
    ++owner_epoch;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    --owner_epoch;
    fixture_generation++;
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    fixture_generation--;

    /* Keep the pre-existing flow-only restore semantics: it does not rewind
     * the actual source clock candidate. */
    flow.source_valid = 0;
    flow.complete = 1;
    assert(melee_web_match_flow_snapshot_restore(&flow, snapshot, error, sizeof(error)));
    assert(flow.source_valid == 1 && flow.complete == 0);
    assert(gm_801A4BA8() == 1 && gm_801A4BB8() == 1 && gm_80479D58.unk_4 == 1);

    /* Rebegin may reuse the static clock and saved-mask addresses. Its
     * nonrewinding lifetime tag still refuses the old flow snapshot. */
    assert(melee_web_source_clock_end());
    assert(HSD_GObjLibInitData.unk_2 == &prior_mask && prior_mask == 0x12345678ULL);
    assert(melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH));
    assert(gm_80479D58.unk_10.x4[0] == NULL &&
           gm_80479D58.unk_10.x4[1] == NULL);
    before = owner_bytes(&flow);
    assert_flow_admission_refused_without_mutation(&flow, snapshot);
    callback_snapshot = NULL;
    melee_web_match_flow_snapshot_destroy(snapshot);
    assert(melee_web_source_clock_end());
    assert(HSD_GObjLibInitData.unk_2 == &prior_mask);

    /* Separately exercise the exact second profile installed by the real
     * source setter. It is not injected into the normal-profile positive. */
    gm_801A4B1C();
    assert(melee_web_source_clock_begin(MELEE_WEB_SOURCE_CLOCK_MATCH));
    MeleeWebMatchFlowSnapshot* source_pair =
        melee_web_match_flow_snapshot_capture(&flow, error, sizeof(error));
    assert(source_pair);
    assert(melee_web_match_flow_snapshot_admit(&flow, source_pair, error, sizeof(error)));
    melee_web_match_flow_snapshot_destroy(source_pair);
    assert(melee_web_source_clock_end());

    puts("Actual source-clock and flow-owner controls passed");
    return 0;
}
