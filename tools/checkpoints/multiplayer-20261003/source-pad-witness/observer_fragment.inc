/* Unapplied diagnostic fragment for src/gameplay_menu_browser.cpp.
 * Compile only with MELEE_WEB_SOURCE_PAD_APPLICATION_TRACE in a dedicated
 * source-input witness target. The normal target has no extra calls or state.
 */
#if defined(MELEE_WEB_SOURCE_PAD_APPLICATION_TRACE)
namespace melee_web::source_pad_application_trace {
constexpr uint32_t kMaxRows = 256;
constexpr uint32_t kConsumerFighterInput = 1;
struct ConsumerRow {
    uint32_t player_index = 0;
    uint32_t player_slot = 0;
    uint32_t controller_port = 0;
    uint32_t consumer_kind = kConsumerFighterInput; // Fighter::input
    int32_t fighter_kind = -1;
    int32_t motion_id = -1;
    float source_stick[2] = {};
    float source_triggers = 0.0f;
    uint32_t held_buttons = 0;
    uint32_t pressed_buttons = 0;
    uint32_t released_buttons = 0;
};
struct Row {
    uint64_t host_sample = 0; // MeleeWebInputSnapshot.samples; host-only identity.
    uint32_t source_frame_before = 0;
    uint32_t source_frame_after = 0;
    uint32_t consumer_count = 0;
    uint8_t step_returned = 0;
    uint8_t overflow = 0;
    uint8_t reserved[2] = {};
    PADStatus raw[PAD_MAX_CONTROLLERS] = {};
    ConsumerRow consumers[MELEE_WEB_MENU_MAX_PLAYERS] = {};
};
static Row rows[kMaxRows] = {};
static uint32_t row_count = 0;
static bool overflow = false;

inline size_t begin(uint64_t host_sample, uint32_t source_frame_before,
                   const PADStatus* final_sample, uint32_t consumer_count)
{
    if (!final_sample || consumer_count == 0 ||
        consumer_count > MELEE_WEB_MENU_MAX_PLAYERS) return kMaxRows;
    if (row_count == kMaxRows) { overflow = true; return kMaxRows; }
    const size_t index = row_count++;
    Row& row = rows[index];
    row = {};
    row.host_sample = host_sample;
    row.source_frame_before = source_frame_before;
    row.consumer_count = consumer_count;
    /* Copy the post-override sample. This observer never writes sample/raw. */
    for (uint32_t port = 0; port < PAD_MAX_CONTROLLERS; ++port)
        row.raw[port] = final_sample[port];
    return index;
}

inline void finish(size_t index, uint32_t source_frame_after,
                   const melee_web::GameplayMatchSession& match)
{
    if (index >= row_count) return;
    Row& row = rows[index];
    row.source_frame_after = source_frame_after;
    for (uint32_t player = 0; player < row.consumer_count; ++player) {
        const MeleeWebMatchStats stats = match.player_stats(player);
        ConsumerRow& consumer = row.consumers[player];
        consumer.player_index = player;
        consumer.player_slot = stats.player_slot;
        /* GameplayMatchSession::Storage::begin verifies this source mapping. */
        consumer.controller_port = stats.player_slot ? stats.player_slot - 1u : player;
        consumer.fighter_kind = stats.fighter_kind;
        consumer.motion_id = stats.motion_id;
        consumer.source_stick[0] = stats.source_stick[0];
        consumer.source_stick[1] = stats.source_stick[1];
        consumer.source_triggers = stats.source_triggers;
        consumer.held_buttons = stats.held_buttons;
        consumer.pressed_buttons = stats.pressed_buttons;
        consumer.released_buttons = stats.released_buttons;
    }
    row.step_returned = 1;
}

inline void abort(size_t index)
{
    if (index < row_count) rows[index].step_returned = 0;
}

inline const Row* read(size_t index)
{
    return index < row_count ? &rows[index] : nullptr;
}
inline uint32_t size() { return row_count; }
inline bool overflowed() { return overflow; }
} // namespace melee_web::source_pad_application_trace
#endif

/* Replace exactly the existing `match->tick(sample);` call in the match branch
 * with this opt-in wrapper. The call and source order are unchanged. */
#if defined(MELEE_WEB_SOURCE_PAD_APPLICATION_TRACE)
const size_t source_pad_trace_index =
    melee_web::source_pad_application_trace::begin(
        input->samples, match->source_frames(), sample,
        asset_selection.player_count != 0
            ? asset_selection.player_count
            : static_cast<uint32_t>(
                  melee_web_menu_active_player_count(&asset_selection.start)));
try {
    match->tick(sample);
    melee_web::source_pad_application_trace::finish(
        source_pad_trace_index, match->source_frames(), *match);
} catch (...) {
    melee_web::source_pad_application_trace::abort(source_pad_trace_index);
    throw;
}
#else
match->tick(sample);
#endif
