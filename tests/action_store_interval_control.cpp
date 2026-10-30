#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftMario/forward.h>
#include <melee/lb/lbanim.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
std::atomic<bool> watch_allocations{false};
std::atomic<bool> fail_next_allocation{false};
std::atomic<std::uint64_t> watched_allocations{0};
std::atomic<std::uint64_t> watched_deallocations{0};

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
std::uint32_t be32(const fighter_runtime_test::Bytes& bytes, std::size_t at)
{
    return (std::uint32_t(bytes.at(at)) << 24) | (std::uint32_t(bytes.at(at + 1)) << 16) |
        (std::uint32_t(bytes.at(at + 2)) << 8) | bytes.at(at + 3);
}
void set_action(fighter_runtime_test::FighterFixture& fixture, std::uint32_t id,
                std::uint32_t container_offset)
{
    const std::uint32_t bytes = be32(fixture.container, container_offset);
    fixture.link(fixture.row(id), fixture.name);
    fighter_runtime_test::put32(fixture.data, fixture.row(id) + 4, container_offset);
    fighter_runtime_test::put32(fixture.data, fixture.row(id) + 8, bytes);
    fighter_runtime_test::put32(fixture.data, fixture.row(id) + 16, id ^ 0x1234U);
    fixture.data[fixture.blends + id * 2] = std::uint8_t(id);
    fixture.data[fixture.blends + id * 2 + 1] = std::uint8_t(id + 1);
}
fighter_runtime_test::FighterFixture make_fixture()
{
    fighter_runtime_test::FighterFixture fixture;
    set_action(fixture, 14, 0);
    set_action(fixture, std::uint32_t(ftMr_SM_SpecialN), 128);
    set_action(fixture, std::uint32_t(ftCo_SM_AttackS4), 256);
    set_action(fixture, 8, 256);
    /* This control isolates animation/clip ownership. Make the fixture's
     * supported command roots source-valid terminal rows; command execution
     * remains covered by the existing native ActionStore controls. */
    fighter_runtime_test::put32(fixture.data, fixture.command_a, 0);
    fixture.unlink(fixture.command_a + 4);
    fighter_runtime_test::put32(fixture.data, fixture.command_b, 0);
    return fixture;
}
constexpr auto history_layout_action = std::uint32_t(ftMr_SM_SpecialN);
constexpr std::uint32_t history_layout_action_c = 15;
const std::string history_layout_long_symbol =
    "action_store_history_symbol_longer_than_small_string_storage";
const std::string history_layout_action_c_symbol = "history_layout_action_c";
fighter_runtime_test::FighterFixture make_history_layout_fixture()
{
    auto fixture = make_fixture();
    const std::uint32_t symbol = static_cast<std::uint32_t>(fixture.data.size());
    fixture.data.insert(fixture.data.end(), history_layout_long_symbol.begin(),
                        history_layout_long_symbol.end());
    fixture.data.push_back(0);

    animation_test::Fixture animation;
    const auto clip = fighter_runtime_test::pack(animation.data, animation.slots,
        history_layout_long_symbol, animation_test::Fixture::root);
    constexpr std::uint32_t offset = 512;
    fixture.container.resize(offset + clip.size());
    std::copy(clip.begin(), clip.end(), fixture.container.begin() + offset);
    set_action(fixture, history_layout_action, offset);
    fixture.link(fixture.row(history_layout_action), symbol);

    const std::uint32_t c_symbol = static_cast<std::uint32_t>(fixture.data.size());
    fixture.data.insert(fixture.data.end(), history_layout_action_c_symbol.begin(),
                        history_layout_action_c_symbol.end());
    fixture.data.push_back(0);
    animation_test::Fixture animation_c;
    const auto clip_c = fighter_runtime_test::pack(animation_c.data, animation_c.slots,
        history_layout_action_c_symbol, animation_test::Fixture::root);
    const auto offset_c = static_cast<std::uint32_t>(
        (std::size_t(offset) + clip.size() + 31U) & ~std::size_t(31U));
    fixture.container.resize(std::max<std::size_t>(fixture.container.size(),
                                                    offset_c + clip_c.size()));
    std::copy(clip_c.begin(), clip_c.end(), fixture.container.begin() + offset_c);
    set_action(fixture, history_layout_action_c, offset_c);
    fixture.link(fixture.row(history_layout_action_c), c_symbol);
    return fixture;
}

struct Harness {
    fighter_runtime_test::FighterFixture fixture;
    std::shared_ptr<const melee_web::DatArchive> archive;
    struct FighterStorage {
        alignas(8) std::array<std::byte, 0x23ec> bytes{};
        Fighter* fighter() noexcept { return reinterpret_cast<Fighter*>(bytes.data()); }
    } p1_storage, p2_storage;
    Fighter* p1 = p1_storage.fighter();
    Fighter* p2 = p2_storage.fighter();
    melee_web::GameplayActionStore store1, store2;

    explicit Harness(melee_web::GameplayActionStorePolicy policy =
                         melee_web::GameplayActionStorePolicy::Legacy)
        : Harness(make_fixture(), policy) {}
    Harness(fighter_runtime_test::FighterFixture supplied,
            melee_web::GameplayActionStorePolicy policy)
        : fixture(std::move(supplied)),
          archive(std::make_shared<const melee_web::DatArchive>(fixture.file())),
          store1(archive, fighter_runtime_test::mario(), fixture.container,
                 {}, {}, nullptr, {}, policy),
          store2(archive, fighter_runtime_test::mario(), fixture.container,
                 {}, {}, nullptr, {}, policy)
    {
        store1.bind(p1);
        store2.bind(p2);
        melee_web_action_interval_test_set_fighter_table(p1, store1.action_rows());
        melee_web_action_interval_test_set_fighter_table(p2, store2.action_rows());
    }
    ~Harness()
    {
        store1.unbind();
        store2.unbind();
    }
    void load(Fighter* fighter, int motion, unsigned slot)
    {
        melee_web_action_load(fighter, fighter, motion, slot);
    }
    void prepare()
    {
        constexpr auto special_n_row = std::uint32_t(ftMr_SM_SpecialN);
        constexpr auto attack_s4_row = std::uint32_t(ftCo_SM_AttackS4);
        check(special_n_row < fighter_runtime_test::mario().motion_count &&
              attack_s4_row < fighter_runtime_test::mario().motion_count,
              "Source submotion enum exceeds the canonical Mario action-row bound");
        load(p1, 14, 0);
        load(p1, int(attack_s4_row), 0);
        load(p1, 14, 0);
        load(p2, 14, 1);
        load(p2, int(special_n_row), 1);
        load(p2, 14, 1);
        check(store1.selected_motion(0) == 14 && store2.selected_motion(1) == 14,
              "Fixture did not establish the original active motion rows");
    }
    void prepare_wait_only()
    {
        check(14 < fighter_runtime_test::mario().motion_count,
              "Wait14 exceeds the canonical Mario action-row bound");
        load(p1, 14, 0);
        load(p2, 14, 1);
        check(store1.selected_motion(0) == 14 && store2.selected_motion(1) == 14,
              "Fixture did not establish the Wait14-only prestate");
    }
};

struct State {
    std::uint32_t p1_motion, p2_motion;
    FigaTree* p1_tree, *p2_tree;
    void* p1_identity, *p2_identity;
};
State state(const Harness& h)
{
    MeleeWebActionFighterClipState p1{}, p2{};
    check(melee_web_action_fighter_clip_state(h.p1, &p1) &&
          melee_web_action_fighter_clip_state(h.p2, &p2),
          "Could not read actual source Fighter clip fields");
    return {h.store1.selected_motion(0), h.store2.selected_motion(1),
        p1.trees[0], p2.trees[1], p1.identities[0], p2.identities[1]};
}
bool same(const State& a, const State& b)
{
    return a.p1_motion == b.p1_motion && a.p2_motion == b.p2_motion &&
        a.p1_tree == b.p1_tree && a.p2_tree == b.p2_tree &&
        a.p1_identity == b.p1_identity && a.p2_identity == b.p2_identity;
}

struct HistoryEndpointCopy {
    MeleeWebActionEndpointHeader header{};
    std::vector<MeleeWebActionEndpointBinding> bindings;
    std::vector<MeleeWebActionEndpointSlot> slots;
    std::vector<MeleeWebActionEndpointCache> caches;
    std::vector<MeleeWebActionEndpointEvent> events;
};

HistoryEndpointCopy copy_history_endpoint(
    const melee_web::GameplayActionStoreHistory& history)
{
    check(history.endpoint_admit() == MELEE_WEB_ACTION_ENDPOINT_OK,
          "Retained ActionStore endpoint failed its complete admission");
    HistoryEndpointCopy copy;
    copy.header = history.endpoint_header();
    check(copy.header.status == MELEE_WEB_ACTION_ENDPOINT_OK &&
              copy.header.binding_count == 6 && copy.header.slots_per_binding == 2,
          "Retained ActionStore endpoint header is incomplete");
    copy.bindings.reserve(copy.header.binding_count);
    copy.slots.reserve(copy.header.binding_count * copy.header.slots_per_binding);
    copy.caches.reserve(copy.header.cache_count);
    copy.events.reserve(copy.header.event_count);
    for (std::uint32_t i = 0; i < copy.header.binding_count; ++i) {
        auto binding = history.endpoint_binding(i);
        check(binding.status == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Retained ActionStore endpoint binding refused");
        copy.bindings.push_back(binding);
        for (std::uint32_t slot = 0; slot < copy.header.slots_per_binding; ++slot) {
            auto row = history.endpoint_slot(i, slot);
            check(row.status == MELEE_WEB_ACTION_ENDPOINT_OK,
                  "Retained ActionStore endpoint slot refused");
            copy.slots.push_back(row);
        }
    }
    for (std::uint32_t i = 0; i < copy.header.cache_count; ++i) {
        auto cache = history.endpoint_cache(i);
        check(cache.status == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Retained ActionStore endpoint cache refused");
        copy.caches.push_back(cache);
    }
    for (std::uint32_t i = 0; i < copy.header.event_count; ++i) {
        auto event = history.endpoint_event(i);
        check(event.status == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Retained ActionStore endpoint event refused");
        copy.events.push_back(event);
    }
    return copy;
}

enum class HistoryPreView { Captured, Armed, Restored };
HistoryEndpointCopy copy_history_pre(
    const melee_web::GameplayActionStoreHistory& history, HistoryPreView view,
    std::uint64_t incarnation = 0, std::uint64_t restore_count = 0)
{
    const auto admit = [&] {
        switch (view) {
            case HistoryPreView::Captured: return history.history_captured_pre_admit();
            case HistoryPreView::Armed: return history.history_armed_pre_admit(incarnation);
            case HistoryPreView::Restored:
                return history.history_restored_pre_admit(incarnation, restore_count);
        }
        return std::uint32_t(MELEE_WEB_ACTION_ENDPOINT_INVALID);
    };
    check(admit() == MELEE_WEB_ACTION_ENDPOINT_OK,
          "Strict ActionStore PRE role failed its complete admission");
    HistoryEndpointCopy copy;
    switch (view) {
        case HistoryPreView::Captured:
            copy.header = history.history_captured_pre_header(); break;
        case HistoryPreView::Armed:
            copy.header = history.history_armed_pre_header(incarnation); break;
        case HistoryPreView::Restored:
            copy.header = history.history_restored_pre_header(incarnation, restore_count); break;
    }
    check(copy.header.status == MELEE_WEB_ACTION_ENDPOINT_OK &&
              copy.header.view == MELEE_WEB_ACTION_ENDPOINT_PRE &&
              copy.header.provenance == MELEE_WEB_ACTION_ENDPOINT_REFERENCE &&
              copy.header.event_count == 0 && copy.header.binding_count == 6 &&
              copy.header.slots_per_binding == 2,
          "Strict ActionStore PRE header advertised a non-PRE or event-bearing image");
    copy.bindings.reserve(copy.header.binding_count);
    copy.slots.reserve(copy.header.binding_count * copy.header.slots_per_binding);
    copy.caches.reserve(copy.header.cache_count);
    for (std::uint32_t i = 0; i < copy.header.binding_count; ++i) {
        const auto binding = view == HistoryPreView::Captured ?
            history.history_captured_pre_binding(i) : view == HistoryPreView::Armed ?
            history.history_armed_pre_binding(incarnation, i) :
            history.history_restored_pre_binding(incarnation, restore_count, i);
        check(binding.status == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Strict ActionStore PRE binding refused");
        copy.bindings.push_back(binding);
        for (std::uint32_t slot = 0; slot < copy.header.slots_per_binding; ++slot) {
            const auto row = view == HistoryPreView::Captured ?
                history.history_captured_pre_slot(i, slot) : view == HistoryPreView::Armed ?
                history.history_armed_pre_slot(incarnation, i, slot) :
                history.history_restored_pre_slot(incarnation, restore_count, i, slot);
            check(row.status == MELEE_WEB_ACTION_ENDPOINT_OK,
                  "Strict ActionStore PRE slot refused");
            copy.slots.push_back(row);
        }
    }
    for (std::uint32_t i = 0; i < copy.header.cache_count; ++i) {
        const auto cache = view == HistoryPreView::Captured ?
            history.history_captured_pre_cache(i) : view == HistoryPreView::Armed ?
            history.history_armed_pre_cache(incarnation, i) :
            history.history_restored_pre_cache(incarnation, restore_count, i);
        check(cache.status == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Strict ActionStore PRE cache refused");
        copy.caches.push_back(cache);
    }
    const auto event = view == HistoryPreView::Captured ?
        history.history_captured_pre_event(0) : view == HistoryPreView::Armed ?
        history.history_armed_pre_event(incarnation, 0) :
        history.history_restored_pre_event(incarnation, restore_count, 0);
    check(event.status == MELEE_WEB_ACTION_ENDPOINT_PHASE && event.index == 0 &&
              event.kind == 0 && event.clip == 0,
          "Strict ActionStore PRE role exposed or fabricated an event row");
    check(admit() == MELEE_WEB_ACTION_ENDPOINT_OK,
          "Strict ActionStore PRE role changed during its getter batch");
    return copy;
}

template<class T>
bool same_pod(const T& left, const T& right)
{ return std::memcmp(&left, &right, sizeof(T)) == 0; }

void verify_retained_pre_action_history()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    Harness h(Policy::ResidentOrdinaryActions);
    h.prepare_wait_only();
    const State baseline = state(h);
    auto history = melee_web::GameplayActionStoreHistory::capture();
    check(bool(history), "Retained PRE history capture refused the resident fixture");
    check(history->history_captured_pre_admit() == MELEE_WEB_ACTION_ENDPOINT_OK &&
              history->history_restored_pre_admit(1, 1) == MELEE_WEB_ACTION_ENDPOINT_PHASE,
          "Captured PRE role or premature restored PRE role had the wrong phase admission");
    auto captured_pre = copy_history_pre(*history, HistoryPreView::Captured);
    melee_web::GameplayActionStoreHistory::EventTapeTestLayout tape_layout{};
    check(history->interval_test_event_tape_layout(&tape_layout) &&
              tape_layout.predicted_size == 0 && tape_layout.corrected_size == 0 &&
              tape_layout.predicted_capacity >= 256 && tape_layout.corrected_capacity >= 256,
          "History tapes were not both preallocated to the explicit diagnostic event budget");
    auto incarnation = history->begin_incarnation();
    check(incarnation == 1, "Retained PRE history did not start its first incarnation");
    check(history->history_armed_pre_admit(incarnation) == MELEE_WEB_ACTION_ENDPOINT_OK &&
              history->history_armed_pre_admit(incarnation + 1) ==
                  MELEE_WEB_ACTION_ENDPOINT_PHASE,
          "Armed PRE role did not admit only the first active incarnation");
    const auto armed_pre = copy_history_pre(
        *history, HistoryPreView::Armed, incarnation);
    check(same_pod(captured_pre.header, armed_pre.header) &&
              captured_pre.bindings.size() == armed_pre.bindings.size() &&
              captured_pre.slots.size() == armed_pre.slots.size() &&
              captured_pre.caches.size() == armed_pre.caches.size(),
          "Armed PRE image dimensions differed from captured PRE");
    for (std::size_t i = 0; i < captured_pre.bindings.size(); ++i)
        check(same_pod(captured_pre.bindings[i], armed_pre.bindings[i]),
              "Armed PRE binding differed from captured PRE");
    for (std::size_t i = 0; i < captured_pre.slots.size(); ++i)
        check(same_pod(captured_pre.slots[i], armed_pre.slots[i]),
              "Armed PRE slot differed from captured PRE");
    for (std::size_t i = 0; i < captured_pre.caches.size(); ++i)
        check(same_pod(captured_pre.caches[i], armed_pre.caches[i]),
              "Armed PRE cache differed from captured PRE");
    auto pre = copy_history_endpoint(*history);
    check(pre.header.view == MELEE_WEB_ACTION_ENDPOINT_PRE && pre.events.empty(),
          "First retained-history batch was not the immutable PRE witness");
    check(same_pod(captured_pre.header, pre.header) &&
              captured_pre.bindings.size() == pre.bindings.size() &&
              captured_pre.slots.size() == pre.slots.size() &&
              captured_pre.caches.size() == pre.caches.size(),
          "Captured PRE reader disagreed with the existing recording PRE endpoint dimensions");
    for (std::size_t i = 0; i < pre.bindings.size(); ++i)
        check(same_pod(captured_pre.bindings[i], pre.bindings[i]),
              "Captured PRE binding differed from the existing PRE endpoint");
    for (std::size_t i = 0; i < pre.slots.size(); ++i)
        check(same_pod(captured_pre.slots[i], pre.slots[i]),
              "Captured PRE slot differed from the existing PRE endpoint");
    for (std::size_t i = 0; i < pre.caches.size(); ++i)
        check(same_pod(captured_pre.caches[i], pre.caches[i]),
              "Captured PRE cache differed from the existing PRE endpoint");

    h.load(h.p1, ftMr_SM_SpecialN, 0);
    check(history->seal_incarnation(incarnation),
          "Predicted ActionStore endpoint did not seal for retained correction");
    auto predicted = copy_history_endpoint(*history);
    check(predicted.header.view == MELEE_WEB_ACTION_ENDPOINT_END &&
              predicted.header.event_count > 0,
          "Predicted ActionStore endpoint lost its source event tape");
    check(history->prepare_restore(incarnation) && history->admit_restore(incarnation),
          "Predicted ActionStore endpoint did not publish a restore ticket");
    check(!history->admit_restore(incarnation + 1) &&
              !history->restore_pre(incarnation + 1),
          "A stale retained-history ticket was accepted");

    const State predicted_state = state(h);
    watched_allocations = 0;
    watched_deallocations = 0;
    watch_allocations = true;
    const bool restored = history->restore_pre(incarnation);
    watch_allocations = false;
    check(restored && watched_allocations == 0 && watched_deallocations == 0,
          "Retained PRE ActionStore commit allocated or deallocated storage");
    const State after_action_restore = state(h);
    check(h.store1.selected_motion(0) == 14 && h.store2.selected_motion(1) == 14 &&
              after_action_restore.p1_tree == predicted_state.p1_tree &&
              after_action_restore.p1_identity == predicted_state.p1_identity,
          "Retained PRE ActionStore commit changed Fighter-owned payload fields");
    check(!history->admit_restore(incarnation) && !history->restore_pre(incarnation),
          "The same retained PRE restore ticket committed twice");

    MeleeWebActionFighterClipState p1_pre{}, p2_pre{};
    check(melee_web_action_fighter_clip_state(h.p1, &p1_pre) &&
              melee_web_action_fighter_clip_state(h.p2, &p2_pre),
          "Could not read Fighter payload for the corrected ActionStore incarnation");
    p1_pre.trees[0] = baseline.p1_tree;
    p1_pre.identities[0] = baseline.p1_identity;
    p2_pre.trees[1] = baseline.p2_tree;
    p2_pre.identities[1] = baseline.p2_identity;
    melee_web_action_interval_test_set_fighter_clip_state(h.p1, &p1_pre);
    melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_pre);
    check(same(state(h), baseline),
          "Later payload restore did not rejoin the exact ActionStore PRE endpoint");
    check(history->endpoint_admit() == MELEE_WEB_ACTION_ENDPOINT_PHASE &&
              history->history_restored_pre_admit(incarnation + 1, 1) ==
                  MELEE_WEB_ACTION_ENDPOINT_PHASE,
          "Generic endpoint or stale-incarnation reader admitted restored PRE");
    check(history->history_restored_pre_admit(incarnation, 0) ==
              MELEE_WEB_ACTION_ENDPOINT_PHASE &&
              history->history_restored_pre_admit(incarnation, 2) ==
                  MELEE_WEB_ACTION_ENDPOINT_PHASE,
          "Restored PRE role accepted a stale or future cumulative restore count");
    auto restored_pre = copy_history_pre(
        *history, HistoryPreView::Restored, incarnation, 1);
    check(same_pod(captured_pre.header, restored_pre.header) &&
              captured_pre.bindings.size() == restored_pre.bindings.size() &&
              captured_pre.slots.size() == restored_pre.slots.size() &&
              captured_pre.caches.size() == restored_pre.caches.size(),
          "Restored PRE reader did not expose the same immutable PRE dimensions");
    for (std::size_t i = 0; i < captured_pre.bindings.size(); ++i)
        check(same_pod(captured_pre.bindings[i], restored_pre.bindings[i]),
              "Restored PRE binding differed from its captured PRE bytes");
    for (std::size_t i = 0; i < captured_pre.slots.size(); ++i)
        check(same_pod(captured_pre.slots[i], restored_pre.slots[i]),
              "Restored PRE slot differed from its captured PRE bytes");
    for (std::size_t i = 0; i < captured_pre.caches.size(); ++i)
        check(same_pod(captured_pre.caches[i], restored_pre.caches[i]),
              "Restored PRE cache differed from its captured PRE bytes");

    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(history->active_failure() == MELEE_WEB_ACTION_INTERVAL_OK &&
              history->seal_corrected_incarnation(incarnation),
          "Corrected ActionStore branch failed to seal in the same incarnation");
    auto selected = copy_history_endpoint(*history);
    check(selected.header.provenance == MELEE_WEB_ACTION_ENDPOINT_CORRECTED &&
              selected.header.event_count > 0 &&
              history->close_endpoint_batch(incarnation),
          "Corrected selected endpoint was not fully copied and closed");

    auto next = history->begin_incarnation();
    check(next == 2 && h.store2.selected_motion(1) == ftCo_SM_AttackS4,
          "Next forward incarnation did not begin at the selected corrected endpoint");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    check(history->seal_incarnation(next),
          "Second predicted ActionStore endpoint did not seal");
    auto second_predicted = copy_history_endpoint(*history);
    check(second_predicted.header.event_count >= selected.header.event_count,
          "Forward ActionStore continuation discarded its selected prefix tape");
    check(history->prepare_restore(next) && history->admit_restore(next),
          "Second incarnation failed to prepare the independent PRE restore bank");
    watched_allocations = 0;
    watched_deallocations = 0;
    watch_allocations = true;
    const bool restored_again = history->restore_pre(next);
    watch_allocations = false;
    check(restored_again && watched_allocations == 0 && watched_deallocations == 0,
          "Second retained PRE restore allocated or deallocated storage");
    check(h.store1.selected_motion(0) == 14 && h.store2.selected_motion(1) == 14,
          "Second retained PRE restore did not return ActionStore slots to the original anchor");

    check(melee_web_action_fighter_clip_state(h.p1, &p1_pre) &&
              melee_web_action_fighter_clip_state(h.p2, &p2_pre),
          "Could not read second corrected-branch Fighter payload");
    p1_pre.trees[0] = baseline.p1_tree;
    p1_pre.identities[0] = baseline.p1_identity;
    p2_pre.trees[1] = baseline.p2_tree;
    p2_pre.identities[1] = baseline.p2_identity;
    melee_web_action_interval_test_set_fighter_clip_state(h.p1, &p1_pre);
    melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_pre);
    h.load(h.p1, ftCo_SM_AttackS4, 0);
    check(history->seal_corrected_incarnation(next),
          "Second corrected ActionStore branch failed to seal");
    auto selected_again = copy_history_endpoint(*history);
    check(selected_again.header.provenance == MELEE_WEB_ACTION_ENDPOINT_CORRECTED &&
              history->close_endpoint_batch(next),
          "Second corrected endpoint receipt was not closed");

    auto final_incarnation = history->begin_incarnation();
    check(final_incarnation == 3,
          "Bounded ActionStore history did not permit its final forward incarnation");
    h.load(h.p2, ftMr_SM_SpecialN, 1);
    check(history->seal_incarnation(final_incarnation),
          "Final selected ActionStore forward endpoint did not seal");
    (void)copy_history_endpoint(*history);
    check(history->close_endpoint_batch(final_incarnation) &&
              !history->prepare_restore(final_incarnation) &&
              history->retire_admit() && history->retire_confirmed(),
          "History exceeded its two-restore budget or refused final retirement");
}

void verify_retained_pre_action_history_refusals()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        const State before = state(h);
        auto history = melee_web::GameplayActionStoreHistory::capture();
        check(bool(history), "Could not capture unexpected-selection history control");
        const auto incarnation = history->begin_incarnation();
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        check(history->seal_incarnation(incarnation),
              "Unexpected-selection history control did not seal");
        const State sealed = state(h);
        check(h.store2.interval_test_try_select(ftCo_SM_AttackS4, 1) == 0 &&
                  same(state(h), sealed) && !history->admit_restore(incarnation) &&
                  history->failure_code() == MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED,
              "Unexpected post-seal action selection was not terminal and write-free");
        history->disarm();
        check(history->failure_code() == MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED &&
                  !history->begin_incarnation() && !history->prepare_restore(incarnation) &&
                  !history->admit_restore(incarnation) &&
                  !melee_web_action_interval_capture() &&
                  !melee_web::GameplayActionStoreHistory::capture(),
              "Disarming failed history revived it, lost its failure, or released owner exclusion");
        (void)before;
    }
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        auto history = melee_web::GameplayActionStoreHistory::capture();
        check(bool(history), "Could not capture history for cleanup disarm control");
        history->disarm();
        check(history->failure_code() == MELEE_WEB_ACTION_INTERVAL_DISARMED &&
                  !history->begin_incarnation() && !melee_web_action_interval_capture() &&
                  !melee_web::GameplayActionStoreHistory::capture(),
              "Cleanup disarm did not detach routing while retaining live-owner exclusion");
        history.reset();
        check(bool(melee_web::GameplayActionStoreHistory::capture()),
              "Destroying disarmed history did not release its owner exclusion");
    }
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        auto history = melee_web::GameplayActionStoreHistory::capture();
        check(bool(history), "Could not capture stale-binding history control");
        const auto incarnation = history->begin_incarnation();
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        check(history->seal_incarnation(incarnation) && history->prepare_restore(incarnation),
              "Stale-binding history control did not prepare its ticket");
        h.store2.unbind();
        const State after_unbind = state(h);
        check(!history->admit_restore(incarnation) && !history->restore_pre(incarnation) &&
                  state(h).p2_motion == after_unbind.p2_motion &&
                  state(h).p2_tree == after_unbind.p2_tree &&
                  history->failure_code() == MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
              "Stale ActionStore generation was dereferenced or restored through its ticket");
    }
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        const State before = state(h);
        fail_next_allocation = true;
        auto history = melee_web::GameplayActionStoreHistory::capture();
        check(!history && same(state(h), before),
              "Failed retained PRE capture changed live ActionStore state");
    }
}

void verify_history_event_budget_refusal()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    Harness h(Policy::ResidentOrdinaryActions);
    h.prepare_wait_only();
    auto history = melee_web::GameplayActionStoreHistory::capture();
    check(bool(history), "Could not capture history for the bounded event-budget control");
    const auto incarnation = history->begin_incarnation();
    check(incarnation == 1, "Event-budget control could not begin its first incarnation");
    melee_web::GameplayActionStoreHistory::EventTapeTestLayout tape{};
    std::size_t selections = 0;
    while (selections < 256 && history->active_failure() == MELEE_WEB_ACTION_INTERVAL_OK) {
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        ++selections;
        check(history->interval_test_event_tape_layout(&tape),
              "Event-budget control could not inspect the private tape");
        if (tape.predicted_size == 256) break;
    }
    check(tape.predicted_size == 256 && tape.predicted_capacity >= 256 &&
              tape.corrected_size == 0 && tape.corrected_capacity >= 256 &&
              history->active_failure() == MELEE_WEB_ACTION_INTERVAL_OK,
          "History event tape did not fill its complete preallocated capacity before refusal");
    check(selections == 128,
          "The budget control did not retain the expected two authored events per selection");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    check(history->failure_code() == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED &&
              !history->seal_incarnation(incarnation) &&
              history->endpoint_admit() != MELEE_WEB_ACTION_ENDPOINT_OK &&
              history->interval_test_event_tape_layout(&tape) &&
              tape.predicted_size == 256 && tape.predicted_capacity >= 256,
          "History event overflow was dropped, grew the tape, or published an incomplete endpoint");
    std::cout << "ActionStore private history event budget exercised: used=256 capacity="
              << tape.predicted_capacity << "; next authored selection refused.\n";
    history->disarm();
}

/* This is an actual-source ownership probe over the same synthetic DAT corpus
 * as the controls below. Fixed witnesses avoid allocating while the branch
 * under test is constructing or retaining source-borrowed clips. */
constexpr std::size_t gate_max_nodes = 8;
constexpr std::size_t gate_max_tracks = 16;
constexpr std::size_t gate_max_stream_bytes = 64;
constexpr std::size_t gate_fighter_bytes = 0x23ec;
struct GateTrackWitness {
    std::uintptr_t stream = 0;
    std::uint16_t length = 0, start = 0;
    std::uint8_t object_type = 0, frac_value = 0, frac_slope = 0;
    std::array<std::uint8_t, gate_max_stream_bytes> bytes{};
};
struct GateTreeWitness {
    std::uint32_t motion = 0;
    std::uintptr_t tree = 0, row_identity = 0, nodes = 0, tracks = 0;
    std::uint32_t type = 0, flags = 0, frames_bits = 0;
    std::size_t node_count = 0, track_count = 0;
    std::array<std::int8_t, gate_max_nodes> node_values{};
    std::array<GateTrackWitness, gate_max_tracks> track_values{};
};
struct GateLayoutWitness {
    std::uintptr_t harness = 0, p1 = 0, p2 = 0, p1_storage = 0, p2_storage = 0;
    std::uintptr_t store1 = 0, store2 = 0, archive = 0;
    std::uintptr_t fixture_data = 0, fixture_container = 0;
    std::uintptr_t p1_runtime = 0, p2_runtime = 0, p1_rows = 0, p2_rows = 0;
};
struct GateResult {
    bool corrected = false;
    GateLayoutWitness layout{};
    GateTreeWitness pre_p1{}, pre_p2{}, branch_a{}, branch_a_after_restore{};
    GateTreeWitness branch_a_after_owner_retirement{}, branch_a_after_store_destruction{}, endpoint_p2{};
    std::array<std::uint8_t, gate_fighter_bytes> pre_p1_payload{}, pre_p2_payload{};
    std::uint64_t creates_before_branch = 0, destroys_before_branch = 0;
    std::uint64_t creates_after_a = 0, creates_after_endpoint = 0, destroys_after_a = 0;
    std::uint64_t destroys_after_restore = 0, destroys_after_disarm = 0, destroys_after_endpoint = 0;
    std::uint64_t destroys_after_owner_retirement = 0, destroys_after_store_destruction = 0;
    std::uint64_t destroys_after_capsule_destruction = 0;
    bool a_lease_unchanged_after_restore = false, a_lease_unchanged_after_disarm = false;
    bool a_lease_unchanged_after_owner_retirement = false, a_lease_unchanged_after_store_destruction = false;
};
GateResult gate_result;

GateTreeWitness gate_tree(FigaTree* tree, void* row_identity, std::uint32_t motion)
{
    check(tree && tree->nodes && tree->tracks && row_identity,
          "Source clip witness is missing its tree, node, track, or row identity");
    GateTreeWitness witness{};
    witness.motion = motion;
    witness.tree = reinterpret_cast<std::uintptr_t>(tree);
    witness.row_identity = reinterpret_cast<std::uintptr_t>(row_identity);
    witness.nodes = reinterpret_cast<std::uintptr_t>(tree->nodes);
    witness.tracks = reinterpret_cast<std::uintptr_t>(tree->tracks);
    witness.type = std::uint32_t(tree->type);
    witness.flags = tree->flags;
    witness.frames_bits = std::bit_cast<std::uint32_t>(tree->frames);
    std::size_t total_tracks = 0;
    bool found_sentinel = false;
    for (std::size_t i = 0; i <= gate_max_nodes; ++i) {
        const std::int8_t value = tree->nodes[i];
        if (value == -1) { found_sentinel = true; break; }
        check(i < gate_max_nodes && value >= 0 && value <= 9,
              "Synthetic source tree exceeded the bounded node witness");
        witness.node_values[i] = value;
        ++witness.node_count;
        total_tracks += std::size_t(value);
    }
    check(found_sentinel && total_tracks > 0 && total_tracks <= gate_max_tracks,
          "Synthetic source tree exceeded the bounded track witness");
    witness.track_count = total_tracks;
    for (std::size_t i = 0; i < total_tracks; ++i) {
        const FigaTrack& track = tree->tracks[i];
        check(track.ad_head && track.length > 0 && track.length <= gate_max_stream_bytes,
              "Synthetic source stream exceeded the bounded byte witness");
        GateTrackWitness& output = witness.track_values[i];
        output.stream = reinterpret_cast<std::uintptr_t>(track.ad_head);
        output.length = track.length;
        output.start = track.startframe;
        output.object_type = track.obj_type;
        output.frac_value = track.frac_value;
        output.frac_slope = track.frac_slope;
        std::memcpy(output.bytes.data(), track.ad_head, track.length);
    }
    return witness;
}
GateTreeWitness gate_slot(Fighter* fighter, unsigned slot, std::uint32_t motion)
{
    MeleeWebActionFighterClipState clip{};
    check(melee_web_action_fighter_clip_state(fighter, &clip),
          "Could not read source Fighter clip fields for the retention gate");
    return gate_tree(clip.trees[slot], clip.identities[slot], motion);
}
bool same_gate_tree(const GateTreeWitness& a, const GateTreeWitness& b)
{
    if (a.motion != b.motion || a.tree != b.tree || a.row_identity != b.row_identity ||
        a.nodes != b.nodes || a.tracks != b.tracks || a.type != b.type ||
        a.flags != b.flags || a.frames_bits != b.frames_bits ||
        a.node_count != b.node_count || a.track_count != b.track_count) return false;
    for (std::size_t i = 0; i < a.node_count; ++i)
        if (a.node_values[i] != b.node_values[i]) return false;
    for (std::size_t i = 0; i < a.track_count; ++i) {
        const GateTrackWitness& x = a.track_values[i];
        const GateTrackWitness& y = b.track_values[i];
        if (x.stream != y.stream || x.length != y.length || x.start != y.start ||
            x.object_type != y.object_type || x.frac_value != y.frac_value ||
            x.frac_slope != y.frac_slope) return false;
        for (std::size_t j = 0; j < x.length; ++j)
            if (x.bytes[j] != y.bytes[j]) return false;
    }
    return true;
}
void gate_ptr(std::uintptr_t pointer)
{
    std::cout << "\"0x" << std::hex << pointer << std::dec << "\"";
}
void gate_bytes(const std::uint8_t* bytes, std::size_t count)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::cout << '"';
    for (std::size_t i = 0; i < count; ++i) {
        std::cout << digits[bytes[i] >> 4] << digits[bytes[i] & 0xf];
    }
    std::cout << '"';
}
void print_gate_tree(const char* name, const GateTreeWitness& w)
{
    std::cout << '"' << name << "\":{";
    std::cout << "\"motion\":" << w.motion << ",\"tree\":"; gate_ptr(w.tree);
    std::cout << ",\"row_identity\":"; gate_ptr(w.row_identity);
    std::cout << ",\"nodes_ptr\":"; gate_ptr(w.nodes);
    std::cout << ",\"tracks_ptr\":"; gate_ptr(w.tracks);
    std::cout << ",\"type\":" << w.type << ",\"flags\":" << w.flags
              << ",\"frames_bits\":" << w.frames_bits << ",\"node_values\":[";
    for (std::size_t i = 0; i < w.node_count; ++i) {
        if (i) std::cout << ',';
        std::cout << int(w.node_values[i]);
    }
    std::cout << "],\"tracks\":[";
    for (std::size_t i = 0; i < w.track_count; ++i) {
        if (i) std::cout << ',';
        const GateTrackWitness& track = w.track_values[i];
        std::cout << "{\"stream\":"; gate_ptr(track.stream);
        std::cout << ",\"length\":" << track.length << ",\"start\":" << track.start
                  << ",\"object_type\":" << unsigned(track.object_type)
                  << ",\"frac_value\":" << unsigned(track.frac_value)
                  << ",\"frac_slope\":" << unsigned(track.frac_slope)
                  << ",\"bytes\":";
        gate_bytes(track.bytes.data(), track.length);
        std::cout << '}';
    }
    std::cout << "]}";
}

struct AllocationDelta {
    std::uint64_t allocations = 0;
    std::uint64_t deallocations = 0;
};
struct AllocationWatch {
    AllocationWatch()
    {
        watched_allocations = 0;
        watched_deallocations = 0;
        watch_allocations = true;
    }
    ~AllocationWatch() { watch_allocations = false; }
    AllocationDelta finish()
    {
        watch_allocations = false;
        return {watched_allocations.load(), watched_deallocations.load()};
    }
};
template<class Fn>
AllocationDelta measure_allocations(Fn&& operation)
{
    AllocationWatch watch;
    operation();
    return watch.finish();
}
bool zero_allocations(AllocationDelta delta)
{ return delta.allocations == 0 && delta.deallocations == 0; }
bool same_allocations(AllocationDelta a, AllocationDelta b)
{ return a.allocations == b.allocations && a.deallocations == b.deallocations; }
AllocationDelta& operator+=(AllocationDelta& a, const AllocationDelta& b)
{
    a.allocations += b.allocations;
    a.deallocations += b.deallocations;
    return a;
}
using CacheCapacityRows = std::vector<std::array<std::size_t, 7>>;
CacheCapacityRows snapshot_cache_capacities(
    const melee_web::GameplayActionStoreHistory& history, std::uint32_t cache_count)
{
    CacheCapacityRows rows;
    rows.reserve(std::size_t(cache_count) * 2);
    for (std::uint32_t cache = 0; cache < cache_count; ++cache) {
        for (std::uint32_t entry = 0; entry < 2; ++entry) {
            melee_web::GameplayActionStoreHistory::CacheStringBankTestLayout layout{};
            check(history.interval_test_cache_string_layout(cache, entry, &layout),
                  "Could not inspect the seven ActionStore string banks");
            for (std::size_t bank = 0; bank < std::size(layout.capacity); ++bank)
                if (layout.capacity[bank] < layout.catalog_max_symbol_bytes)
                    throw std::runtime_error("Cache " + std::to_string(cache) +
                        " entry " + std::to_string(entry) + " bank " +
                        std::to_string(bank) + " capacity " +
                        std::to_string(layout.capacity[bank]) + " below its catalog maximum " +
                        std::to_string(layout.catalog_max_symbol_bytes));
            rows.emplace_back();
            std::copy(std::begin(layout.capacity), std::end(layout.capacity), rows.back().begin());
        }
    }
    return rows;
}
void verify_cache_capacities(const CacheCapacityRows& rows, std::size_t maximum_symbol_bytes)
{
    check(!rows.empty(), "History cache layout had no live cache entries");
    for (std::size_t row_index = 0; row_index < rows.size(); ++row_index)
        for (std::size_t bank = 0; bank < rows[row_index].size(); ++bank)
            if (rows[row_index][bank] < maximum_symbol_bytes)
                throw std::runtime_error("ActionStore string bank " + std::to_string(bank) +
                    " at cache row " + std::to_string(row_index) + " has capacity " +
                    std::to_string(rows[row_index][bank]) + ", expected " +
                    std::to_string(maximum_symbol_bytes));
}
void verify_cache_capacities_stable(const CacheCapacityRows& captured,
                                    const CacheCapacityRows& current)
{
    check(captured.size() == current.size(), "ActionStore cache layout dimensions changed");
    for (std::size_t row = 0; row < captured.size(); ++row)
        check(captured[row] == current[row],
              "A live, snapshot, or swap string bank changed capacity after capture");
}
void verify_history_symbol_in_bank(
    const melee_web::GameplayActionStoreHistory& history, std::uint32_t cache_count,
    std::size_t bank_index, std::size_t symbol_bytes)
{
    check(bank_index < 7, "Invalid private cache bank index in history test");
    bool found = false;
    for (std::uint32_t cache = 0; cache < cache_count; ++cache) {
        for (std::uint32_t entry = 0; entry < 2; ++entry) {
            melee_web::GameplayActionStoreHistory::CacheStringBankTestLayout layout{};
            check(history.interval_test_cache_string_layout(cache, entry, &layout),
                  "Could not inspect cache bytes after a source selection");
            found = found || layout.symbol_bytes[bank_index] == symbol_bytes;
        }
    }
    check(found, "The selected long authored key was absent from its expected ActionStore bank");
}
void print_capacity_rows(const char* name, const CacheCapacityRows& rows)
{
    std::cout << '"' << name << "\":[";
    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (row) std::cout << ',';
        std::cout << '[';
        for (std::size_t bank = 0; bank < rows[row].size(); ++bank) {
            if (bank) std::cout << ',';
            std::cout << rows[row][bank];
        }
        std::cout << ']';
    }
    std::cout << ']';
}
void print_allocation_delta(const AllocationDelta& delta)
{
    std::cout << "{\"allocations\":" << delta.allocations
              << ",\"deallocations\":" << delta.deallocations << '}';
}
void print_history_cache_semantics(const HistoryEndpointCopy& endpoint)
{
    std::cout << "\"final_cache\":[";
    for (std::size_t cache_index = 0; cache_index < endpoint.caches.size(); ++cache_index) {
        if (cache_index) std::cout << ',';
        const auto& cache = endpoint.caches[cache_index];
        std::cout << "{\"index\":" << cache.index << ",\"next\":" << cache.next
                  << ",\"root\":"; gate_ptr(cache.root);
        std::cout << ",\"owner\":"; gate_ptr(cache.owner);
        std::cout << ",\"cache\":"; gate_ptr(cache.cache);
        std::cout << ",\"runtime\":"; gate_ptr(cache.runtime);
        std::cout << ",\"interval_owner\":"; gate_ptr(cache.interval_owner);
        std::cout << ",\"hit_callback\":"; gate_ptr(cache.hit_callback);
        std::cout << ",\"miss_replay_callback\":"; gate_ptr(cache.miss_replay_callback);
        std::cout << ",\"miss_record_callback\":"; gate_ptr(cache.miss_record_callback);
        std::cout << ",\"entries\":[";
        for (std::size_t entry_index = 0; entry_index < 2; ++entry_index) {
            if (entry_index) std::cout << ',';
            const auto& entry = cache.entries[entry_index];
            check(!entry.symbol_bytes || entry.symbol_data,
                  "Selected ActionStore cache row lost its transport bytes");
            std::cout << "{\"offset\":" << entry.offset << ",\"archive_bytes\":"
                      << entry.archive_bytes << ",\"native_action\":" << entry.native_action
                      << ",\"animation\":"; gate_ptr(entry.animation);
            const char* symbol_data = entry.symbol_bytes ?
                reinterpret_cast<const char*>(static_cast<std::uintptr_t>(entry.symbol_data)) : "";
            const std::string_view symbol(symbol_data, entry.symbol_bytes);
            std::cout << ",\"symbol\":\"";
            for (const char ch : symbol) {
                if (ch == '"' || ch == '\\') std::cout << '\\';
                std::cout << ch;
            }
            std::cout << "\"}";
        }
        std::cout << "]}";
    }
    std::cout << ']';
}

void run_history_layout_gate(bool repeat)
{
    constexpr std::size_t op_a1 = 0;
    constexpr std::size_t op_a1_corrected = 1;
    constexpr std::size_t op_b_predicted = 2;
    constexpr std::size_t op_c_predicted = 3;
    constexpr std::size_t op_a2_predicted = 4;
    constexpr std::size_t op_a2_corrected_prefix = 5;
    constexpr std::size_t op_b_corrected = 6;
    constexpr std::size_t op_c_corrected = 7;
    constexpr std::size_t op_a2_corrected = 8;
    static constexpr const char* operation_names[] = {
        "A1_predicted", "A1_corrected", "B2_predicted", "C2_predicted",
        "A2_predicted", "A2_corrected_prefix", "B2_corrected", "C2_corrected",
        "A2_corrected"};
    using Policy = melee_web::GameplayActionStorePolicy;
    auto fixture = make_history_layout_fixture();
    const auto fixture_archive = std::make_shared<const melee_web::DatArchive>(fixture.file());
    check(std::find_if(fixture_archive->public_symbols().begin(),
                       fixture_archive->public_symbols().end(),
                       [](const auto& symbol) {
                           return symbol.name == fighter_runtime_test::mario().fighter_symbol;
                       }) != fixture_archive->public_symbols().end(),
          "Long-key fixture omitted the canonical Fighter public symbol");
    const melee_web::DatFighterRuntime fixture_runtime(
        fixture_archive, fighter_runtime_test::mario());
    for (const auto motion : {14U, history_layout_action, std::uint32_t(ftCo_SM_AttackS4),
                              8U, history_layout_action_c}) {
        const auto& action = fixture_runtime.action(motion);
        const auto nested = melee_web::DatArchive(fixture_runtime.archive_actions().slice(
            fixture.container, motion));
        if (std::find_if(nested.public_symbols().begin(), nested.public_symbols().end(),
                         [&](const auto& symbol) { return symbol.name == action.symbol; }) ==
            nested.public_symbols().end()) {
            std::string symbols;
            for (const auto& symbol : nested.public_symbols()) symbols += " [" + symbol.name + "]";
            throw std::runtime_error("Fixture action " + std::to_string(motion) + " (" +
                                     action.symbol + ") is absent from its nested archive symbols:" + symbols);
        }
    }
    auto harness = std::make_unique<Harness>(
        std::move(fixture), Policy::ResidentOrdinaryActions);
    Harness& h = *harness;
    h.prepare_wait_only();
    MeleeWebActionFighterClipState pre_p1{}, pre_p2{};
    check(melee_web_action_fighter_clip_state(h.p1, &pre_p1) &&
              melee_web_action_fighter_clip_state(h.p2, &pre_p2),
          "Could not retain exact Fighter clip fields before long-key history");

    std::size_t maximum_symbol_bytes = 0;
    for (const auto& row : h.store1.runtime().actions())
        maximum_symbol_bytes = std::max(maximum_symbol_bytes, row.symbol.size());
    check(maximum_symbol_bytes == history_layout_long_symbol.size(),
          "Long-key fixture is not the exact maximum symbol in the complete runtime catalog");

    auto history = melee_web::GameplayActionStoreHistory::capture();
    check(bool(history), "Could not capture history for the catalog-sized string-bank gate");
    const auto captured_header = history->history_captured_pre_header();
    check(captured_header.status == MELEE_WEB_ACTION_ENDPOINT_OK &&
              captured_header.cache_count > 0,
          "Long-key history capture did not publish its captured-PRE cache dimensions");
    const std::uint32_t cache_count = captured_header.cache_count;
    CacheCapacityRows captured_capacities = snapshot_cache_capacities(*history, cache_count);
    verify_cache_capacities(captured_capacities, maximum_symbol_bytes);
    CacheCapacityRows current_capacities = captured_capacities;
    std::array<AllocationDelta, 9> operations{};
    std::array<bool, 9> operation_present{};
    std::vector<GateTreeWitness> trees(9);
    AllocationDelta maintenance{};
    const auto clips_created = melee_web_action_interval_test_clip_create_count();
    const auto clips_destroyed = melee_web_action_interval_test_clip_destroy_count();

    bool phase_ok = false;
    std::uint64_t first = 0;
    auto delta = measure_allocations([&] { first = history->begin_incarnation(); });
    maintenance += delta;
    check(first == 1 && zero_allocations(delta),
          "First bounded history incarnation allocated or failed to begin");

    auto select = [&](std::size_t op, std::uint32_t motion) {
        operations[op] = measure_allocations([&] { h.load(h.p1, int(motion), 0); });
        operation_present[op] = true;
        trees[op] = gate_slot(h.p1, 0, motion);
        current_capacities = snapshot_cache_capacities(*history, cache_count);
        verify_cache_capacities(current_capacities, maximum_symbol_bytes);
        verify_cache_capacities_stable(captured_capacities, current_capacities);
    };
    select(op_a1, history_layout_action);
    verify_history_symbol_in_bank(*history, cache_count, 0, history_layout_long_symbol.size());

    delta = measure_allocations([&] { phase_ok = history->seal_incarnation(first); });
    maintenance += delta;
    check(phase_ok && zero_allocations(delta),
          "Predicted endpoint bank capture allocated or failed to seal");
    verify_history_symbol_in_bank(*history, cache_count, 2, history_layout_long_symbol.size());
    auto first_selected = copy_history_endpoint(*history);
    check(first_selected.header.event_count > 0,
          "First long-key endpoint omitted its selected cache/action events");
    delta = measure_allocations([&] { phase_ok = history->close_endpoint_batch(first); });
    maintenance += delta;
    check(phase_ok && zero_allocations(delta),
          "Predicted endpoint close allocated or failed");

    if (repeat) {
        delta = measure_allocations([&] {
            phase_ok = history->prepare_restore(first) && history->admit_restore(first);
        });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "First retained-PRE ticket preparation allocated or refused");
        delta = measure_allocations([&] { phase_ok = history->restore_pre(first); });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "First retained-PRE commit allocated or refused");
        melee_web_action_interval_test_set_fighter_clip_state(h.p1, &pre_p1);
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &pre_p2);
        check(history->history_restored_pre_admit(first, 1) == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Restored-PRE role failed after the fixture restored its source payload");
        select(op_a1_corrected, history_layout_action);
        check(same_allocations(operations[op_a1], operations[op_a1_corrected]),
              "Identical long-key A selection changed host allocation/free deltas after one restore");
        delta = measure_allocations([&] {
            phase_ok = history->seal_corrected_incarnation(first);
        });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "Corrected endpoint bank capture allocated or failed to seal");
        verify_history_symbol_in_bank(*history, cache_count, 3, history_layout_long_symbol.size());
        auto selected = copy_history_endpoint(*history);
        delta = measure_allocations([&] { phase_ok = history->close_endpoint_batch(first); });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta) &&
                  selected.header.provenance == MELEE_WEB_ACTION_ENDPOINT_CORRECTED,
              "Corrected endpoint close allocated or selected the wrong provenance");
    }

    std::uint64_t second = 0;
    delta = measure_allocations([&] { second = history->begin_incarnation(); });
    maintenance += delta;
    check(second == 2 && zero_allocations(delta),
          "Second bounded history incarnation allocated or failed to begin");
    select(op_b_predicted, std::uint32_t(ftCo_SM_AttackS4));
    select(op_c_predicted, history_layout_action_c);
    select(op_a2_predicted, history_layout_action);
    delta = measure_allocations([&] { phase_ok = history->seal_incarnation(second); });
    maintenance += delta;
    check(phase_ok && zero_allocations(delta),
          "Second predicted endpoint bank capture allocated or failed to seal");
    verify_history_symbol_in_bank(*history, cache_count, 2, history_layout_long_symbol.size());
    auto second_predicted = copy_history_endpoint(*history);
    delta = measure_allocations([&] { phase_ok = history->close_endpoint_batch(second); });
    maintenance += delta;
    check(phase_ok && zero_allocations(delta),
          "Second predicted endpoint close allocated or failed");

    if (repeat) {
        delta = measure_allocations([&] {
            phase_ok = history->prepare_restore(second) && history->admit_restore(second);
        });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "Second retained-PRE ticket preparation allocated or refused");
        delta = measure_allocations([&] { phase_ok = history->restore_pre(second); });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "Second retained-PRE commit allocated or refused");
        melee_web_action_interval_test_set_fighter_clip_state(h.p1, &pre_p1);
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &pre_p2);
        check(history->history_restored_pre_admit(second, 2) == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Second restore did not rejoin the same immutable PRE source state");
        select(op_a2_corrected_prefix, history_layout_action);
        select(op_b_corrected, std::uint32_t(ftCo_SM_AttackS4));
        select(op_c_corrected, history_layout_action_c);
        select(op_a2_corrected, history_layout_action);
        check(same_allocations(operations[op_a1], operations[op_a1_corrected]) &&
                  same_allocations(operations[op_a1], operations[op_a2_corrected_prefix]) &&
                  same_allocations(operations[op_b_predicted], operations[op_b_corrected]) &&
                  same_allocations(operations[op_c_predicted], operations[op_c_corrected]) &&
                  same_allocations(operations[op_a2_predicted], operations[op_a2_corrected]),
              "A repeated identical source selection changed its allocation/free deltas");
        delta = measure_allocations([&] {
            phase_ok = history->seal_corrected_incarnation(second);
        });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta),
              "Second corrected endpoint bank capture allocated or failed to seal");
        verify_history_symbol_in_bank(*history, cache_count, 3, history_layout_long_symbol.size());
        auto selected = copy_history_endpoint(*history);
        delta = measure_allocations([&] { phase_ok = history->close_endpoint_batch(second); });
        maintenance += delta;
        check(phase_ok && zero_allocations(delta) &&
                  selected.header.provenance == MELEE_WEB_ACTION_ENDPOINT_CORRECTED,
              "Second corrected endpoint close allocated or selected the wrong provenance");
    } else {
        check(second_predicted.header.event_count > 0,
              "Clean history lane lost its selected source events");
    }

    // Both branches already copied and closed their selected endpoint above.
    auto final_endpoint = copy_history_endpoint(*history);
    delta = measure_allocations([&] {
        phase_ok = history->retire_admit() && history->retire_confirmed();
    });
    maintenance += delta;
    check(phase_ok && zero_allocations(delta),
          "Confirmed ActionStore history retirement allocated or freed storage");
    current_capacities = snapshot_cache_capacities(*history, cache_count);
    verify_cache_capacities(current_capacities, maximum_symbol_bytes);
    verify_cache_capacities_stable(captured_capacities, current_capacities);
    check(melee_web_action_interval_test_clip_create_count() == clips_created &&
              melee_web_action_interval_test_clip_destroy_count() == clips_destroyed,
          "Resident ActionStore selection created or destroyed a native Clip after capture");
    check(zero_allocations(maintenance),
          "History begin/seal/prepare/restore/close/retire changed host allocator state");

    std::cout << "{\"scenario\":\"catalog-sized ActionStore string banks with one retained PRE\",\"lane\":\""
              << (repeat ? "history-layout-repeat" : "history-layout-clean")
              << "\",\"maximum_catalog_symbol_bytes\":" << maximum_symbol_bytes
              << ",\"event_tape_capacity\":256,\"maintenance\":";
    print_allocation_delta(maintenance);
    std::cout << ",\"operations\":{";
    for (std::size_t i = 0; i < operations.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '\"' << operation_names[i] << "\":";
        if (operation_present[i]) print_allocation_delta(operations[i]);
        else std::cout << "null";
    }
    std::cout << "},\"trees\":{";
    bool first_tree = true;
    for (std::size_t i = 0; i < trees.size(); ++i) if (operation_present[i]) {
        if (!first_tree) std::cout << ',';
        print_gate_tree(operation_names[i], trees[i]);
        first_tree = false;
    }
    std::cout << "},\"capacities\":{";
    print_capacity_rows("captured", captured_capacities);
    std::cout << ',';
    print_capacity_rows("final", current_capacities);
    std::cout << "},";
    print_history_cache_semantics(final_endpoint);
    std::cout << ",\"final_event_count\":" << final_endpoint.header.event_count
              << ",\"native_clip_create_delta\":0,\"native_clip_destroy_delta\":0}\n";
}

void run_retention_gate(bool corrected)
{
    gate_result = GateResult{};
    gate_result.corrected = corrected;
    constexpr auto action_a = std::uint32_t(ftMr_SM_SpecialN);
    constexpr auto action_b = std::uint32_t(ftCo_SM_AttackS4);
    static_assert(action_a < 303 && action_b < 303 && action_a != action_b);
    MeleeWebActionInterval* interval = nullptr;
    {
        Harness h(melee_web::GameplayActionStorePolicy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not capture and begin the common pre-branch ActionStore interval");

        /* This prestate is collected identically in both fresh Wasm instances,
         * after the same capsule/journal setup and before either lane branches. */
        GateLayoutWitness& layout = gate_result.layout;
        layout.harness = reinterpret_cast<std::uintptr_t>(&h);
        layout.p1 = reinterpret_cast<std::uintptr_t>(h.p1);
        layout.p2 = reinterpret_cast<std::uintptr_t>(h.p2);
        layout.p1_storage = reinterpret_cast<std::uintptr_t>(h.p1_storage.bytes.data());
        layout.p2_storage = reinterpret_cast<std::uintptr_t>(h.p2_storage.bytes.data());
        layout.store1 = reinterpret_cast<std::uintptr_t>(&h.store1);
        layout.store2 = reinterpret_cast<std::uintptr_t>(&h.store2);
        layout.archive = reinterpret_cast<std::uintptr_t>(h.archive.get());
        layout.fixture_data = reinterpret_cast<std::uintptr_t>(h.fixture.data.data());
        layout.fixture_container = reinterpret_cast<std::uintptr_t>(h.fixture.container.data());
        layout.p1_runtime = reinterpret_cast<std::uintptr_t>(&h.store1.runtime());
        layout.p2_runtime = reinterpret_cast<std::uintptr_t>(&h.store2.runtime());
        layout.p1_rows = reinterpret_cast<std::uintptr_t>(h.store1.action_rows());
        layout.p2_rows = reinterpret_cast<std::uintptr_t>(h.store2.action_rows());
        gate_result.pre_p1 = gate_slot(h.p1, 0, 14);
        gate_result.pre_p2 = gate_slot(h.p2, 1, 14);
        std::memcpy(gate_result.pre_p1_payload.data(), h.p1_storage.bytes.data(), gate_fighter_bytes);
        std::memcpy(gate_result.pre_p2_payload.data(), h.p2_storage.bytes.data(), gate_fighter_bytes);
        MeleeWebActionFighterClipState baseline_p2{};
        check(melee_web_action_fighter_clip_state(h.p2, &baseline_p2),
              "Could not save the modeled prestate Fighter payload");
        gate_result.creates_before_branch = melee_web_action_interval_test_clip_create_count();
        gate_result.destroys_before_branch = melee_web_action_interval_test_clip_destroy_count();

        if (corrected) {
            h.load(h.p2, int(action_a), 1);
            check(h.store2.selected_motion(1) == action_a,
                  "Predicted branch did not select authored action A");
            gate_result.branch_a = gate_slot(h.p2, 1, action_a);
            gate_result.creates_after_a = melee_web_action_interval_test_clip_create_count();
            gate_result.destroys_after_a = melee_web_action_interval_test_clip_destroy_count();
            check(gate_result.creates_after_a == gate_result.creates_before_branch,
                  "Resident action selection allocated a native Clip after preparation");
            check(melee_web_action_interval_seal_record(interval) == 1 &&
                  melee_web_action_interval_admit(interval) == 1 &&
                  melee_web_action_interval_admit_corrected(interval) == 1 &&
                  melee_web_action_interval_restore_corrected(interval) == 1,
                  "ActionStore capsule could not enter admitted corrected recording from A");
            gate_result.destroys_after_restore = melee_web_action_interval_test_clip_destroy_count();
            melee_web_action_interval_test_set_fighter_clip_state(h.p2, &baseline_p2);
            check(h.store2.selected_motion(1) == 14 &&
                  gate_slot(h.p2, 1, 14).tree == gate_result.pre_p2.tree &&
                  gate_slot(h.p2, 1, 14).row_identity == gate_result.pre_p2.row_identity,
                  "Modeled later Fighter-payload owner did not restore Wait14 pointer fields");
            gate_result.branch_a_after_restore = gate_tree(
                reinterpret_cast<FigaTree*>(gate_result.branch_a.tree),
                reinterpret_cast<void*>(gate_result.branch_a.row_identity), action_a);
            gate_result.a_lease_unchanged_after_restore =
                same_gate_tree(gate_result.branch_a, gate_result.branch_a_after_restore);
        } else {
            /* The clean lane goes through the same capture/begin setup, then
             * retires an empty journal before its first post-boundary load. */
            melee_web_action_interval_disarm(interval);
            check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED,
                  "Clean lane could not disarm its empty pre-branch journal");
            gate_result.destroys_after_disarm = melee_web_action_interval_test_clip_destroy_count();
        }

        /* Both lanes take the same source cache-hit then cache-miss path to B.
         * In the corrected lane this stays inside the distinct correction tape. */
        h.load(h.p2, 14, 1);
        h.load(h.p2, int(action_b), 1);
        check(h.store2.selected_motion(1) == action_b,
              "Authoritative branch did not select authored action B");
        if (corrected) {
            check(melee_web_action_interval_active_failure(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
                  melee_web_action_interval_seal_corrected(interval) == 1 &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
                  "Corrected B selection did not seal as its separate source tape");
        }
        gate_result.endpoint_p2 = gate_slot(h.p2, 1, action_b);
        gate_result.creates_after_endpoint = melee_web_action_interval_test_clip_create_count();
        gate_result.destroys_after_endpoint = melee_web_action_interval_test_clip_destroy_count();
        check(gate_result.creates_after_endpoint == gate_result.creates_before_branch,
              "Resident action selection allocated a native Clip after preparation");
        if (corrected) {
            melee_web_action_interval_disarm(interval);
            check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED,
                  "Corrected-branch journal did not disarm after its B endpoint was sealed");
            gate_result.destroys_after_disarm = melee_web_action_interval_test_clip_destroy_count();
            gate_result.a_lease_unchanged_after_disarm =
                gate_result.destroys_after_a == gate_result.destroys_after_restore &&
                gate_result.destroys_after_restore == gate_result.destroys_after_disarm &&
                gate_result.a_lease_unchanged_after_restore;
            check(gate_result.a_lease_unchanged_after_disarm,
                  "Restore or disarm released/changed the speculative A source lease");
        }

        /* Disarm first, retire the real source stores, then destroy the
         * capsule. The A tree is still readable only while the capsule leases
         * its displaced post-A slot/cache. */
        h.store1.unbind();
        h.store2.unbind();
        gate_result.destroys_after_owner_retirement = melee_web_action_interval_test_clip_destroy_count();
        if (corrected) {
            gate_result.branch_a_after_owner_retirement = gate_tree(
                reinterpret_cast<FigaTree*>(gate_result.branch_a.tree),
                reinterpret_cast<void*>(gate_result.branch_a.row_identity), action_a);
            gate_result.a_lease_unchanged_after_owner_retirement =
                same_gate_tree(gate_result.branch_a, gate_result.branch_a_after_owner_retirement);
            check(gate_result.a_lease_unchanged_after_owner_retirement,
                  "Source-store retirement invalidated the capsule-retained A stream");
        }
    }
    gate_result.destroys_after_store_destruction = melee_web_action_interval_test_clip_destroy_count();
    if (corrected) {
        gate_result.branch_a_after_store_destruction = gate_tree(
            reinterpret_cast<FigaTree*>(gate_result.branch_a.tree),
            reinterpret_cast<void*>(gate_result.branch_a.row_identity), action_a);
        gate_result.a_lease_unchanged_after_store_destruction =
            same_gate_tree(gate_result.branch_a, gate_result.branch_a_after_store_destruction);
        check(gate_result.a_lease_unchanged_after_store_destruction,
              "Store destruction invalidated the capsule-retained A stream");
    }
    melee_web_action_interval_destroy(interval);
    gate_result.destroys_after_capsule_destruction = melee_web_action_interval_test_clip_destroy_count();
    if (corrected) {
        check(gate_result.destroys_after_capsule_destruction > gate_result.destroys_after_store_destruction,
              "Destroying the capsule did not release its retained A Clip lease");
    }
}
void print_retention_gate()
{
    const GateResult& r = gate_result;
    const GateLayoutWitness& l = r.layout;
    std::cout << "{\"scenario\":\"synthetic DAT corpus through production ActionStore/decoder/Clip path\",\"lane\":\""
              << (r.corrected ? "gate-b" : "gate-a") << "\",\"layout\":{";
    std::cout << "\"harness\":"; gate_ptr(l.harness);
    std::cout << ",\"p1\":"; gate_ptr(l.p1); std::cout << ",\"p2\":"; gate_ptr(l.p2);
    std::cout << ",\"p1_storage\":"; gate_ptr(l.p1_storage);
    std::cout << ",\"p2_storage\":"; gate_ptr(l.p2_storage);
    std::cout << ",\"store1\":"; gate_ptr(l.store1); std::cout << ",\"store2\":"; gate_ptr(l.store2);
    std::cout << ",\"archive\":"; gate_ptr(l.archive);
    std::cout << ",\"fixture_data\":"; gate_ptr(l.fixture_data);
    std::cout << ",\"fixture_container\":"; gate_ptr(l.fixture_container);
    std::cout << ",\"p1_runtime\":"; gate_ptr(l.p1_runtime); std::cout << ",\"p2_runtime\":"; gate_ptr(l.p2_runtime);
    std::cout << ",\"p1_rows\":"; gate_ptr(l.p1_rows); std::cout << ",\"p2_rows\":"; gate_ptr(l.p2_rows);
    std::cout << "},\"actions\":{\"A\":{\"motion\":" << ftMr_SM_SpecialN
              << ",\"container_offset\":128},\"B\":{\"motion\":" << ftCo_SM_AttackS4
              << ",\"container_offset\":256}},\"prestate\":{";
    print_gate_tree("p1", r.pre_p1); std::cout << ','; print_gate_tree("p2", r.pre_p2);
    std::cout << ",\"p1_payload_hex\":"; gate_bytes(r.pre_p1_payload.data(), r.pre_p1_payload.size());
    std::cout << ",\"p2_payload_hex\":"; gate_bytes(r.pre_p2_payload.data(), r.pre_p2_payload.size());
    std::cout << "},\"branch_a\":";
    if (r.corrected) { std::cout << '{'; print_gate_tree("witness", r.branch_a); std::cout << '}'; }
    else std::cout << "null";
    std::cout << ",\"branch_a_after_restore\":";
    if (r.corrected) { std::cout << '{'; print_gate_tree("witness", r.branch_a_after_restore); std::cout << '}'; }
    else std::cout << "null";
    std::cout << ",\"branch_a_after_owner_retirement\":";
    if (r.corrected) { std::cout << '{'; print_gate_tree("witness", r.branch_a_after_owner_retirement); std::cout << '}'; }
    else std::cout << "null";
    std::cout << ",\"branch_a_after_store_destruction\":";
    if (r.corrected) { std::cout << '{'; print_gate_tree("witness", r.branch_a_after_store_destruction); std::cout << '}'; }
    else std::cout << "null";
    std::cout << ",\"endpoint_p2\":{";
    print_gate_tree("witness", r.endpoint_p2);
    std::cout << "},\"lifecycle\":{\"creates_before_branch\":" << r.creates_before_branch
              << ",\"destroys_before_branch\":" << r.destroys_before_branch
              << ",\"creates_after_a\":" << r.creates_after_a
              << ",\"creates_after_endpoint\":" << r.creates_after_endpoint
              << ",\"destroys_after_a\":" << r.destroys_after_a
              << ",\"destroys_after_restore\":" << r.destroys_after_restore
              << ",\"destroys_after_disarm\":" << r.destroys_after_disarm
              << ",\"destroys_after_endpoint\":" << r.destroys_after_endpoint
              << ",\"destroys_after_owner_retirement\":" << r.destroys_after_owner_retirement
              << ",\"destroys_after_store_destruction\":" << r.destroys_after_store_destruction
              << ",\"destroys_after_capsule_destruction\":" << r.destroys_after_capsule_destruction
              << ",\"a_lease_unchanged_after_restore\":" << (r.a_lease_unchanged_after_restore ? "true" : "false")
              << ",\"a_lease_unchanged_after_disarm\":" << (r.a_lease_unchanged_after_disarm ? "true" : "false")
              << ",\"a_lease_unchanged_after_owner_retirement\":"
              << (r.a_lease_unchanged_after_owner_retirement ? "true" : "false")
              << ",\"a_lease_unchanged_after_store_destruction\":"
              << (r.a_lease_unchanged_after_store_destruction ? "true" : "false") << "}}\n";
}

void verify_round_trip(melee_web::GameplayActionStorePolicy policy)
{
    Harness h(policy);
    h.prepare();
    const State before = state(h);
    auto* interval = melee_web_action_interval_capture();
    check(melee_web_action_interval_active_failure(nullptr)==MELEE_WEB_ACTION_INTERVAL_CAPTURE_REFUSED,"Null active guard accepted");
    check(interval&&melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,"Captured nonactive phase accepted");
    check(interval != nullptr, "Capture rejected the healthy two-store source fixture");
    check(melee_web_action_interval_begin_record(interval) == 1,
          "Recording phase did not begin on the captured owner state");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_OK,
          "Healthy intermediate recording/replay was treated as a final endpoint");
    h.store1.interval_test_change_cache_owner();
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED,
          "Intermediate guard accepted changed cache owner");
    h.store1.interval_test_restore_cache_owner();
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_seal_record(interval) == 1,
          "The exact two-row source selection sequence did not seal");
    const State final = state(h);
    const auto before_restore_create = melee_web_action_interval_test_clip_create_count();
    const auto before_restore_destroy = melee_web_action_interval_test_clip_destroy_count();
    check(melee_web_action_interval_admit(interval) == 1, "Recorded owner/cache state was not admitted");
    watched_allocations = 0;
    watched_deallocations = 0;
    watch_allocations = true;
    const int restored = melee_web_action_interval_restore(interval);
    watch_allocations = false;
    check(restored == 1, "Prevalidated action-store restore failed");
    check(watched_allocations == 0 && watched_deallocations == 0,
          "Restore allocated or deallocated C++ storage after admission");
    check(melee_web_action_interval_test_clip_create_count() == before_restore_create &&
          melee_web_action_interval_test_clip_destroy_count() == before_restore_destroy,
          "Restore created or released a native FigaTree lease");
    const State after_action_restore = state(h);
    check(h.store1.selected_motion(0) == 14 && h.store2.selected_motion(1) == 14 &&
          after_action_restore.p1_tree == final.p1_tree &&
          after_action_restore.p2_tree == final.p2_tree,
          "ActionStore restore wrote Fighter-owned clip fields before their payload owner");
    /* Aggregate commit restores this source-owned payload only after the
     * ActionStore's private active slots and cache have committed. */
    MeleeWebActionFighterClipState p1_before{}, p2_before{};
    check(melee_web_action_fighter_clip_state(h.p1, &p1_before) &&
          melee_web_action_fighter_clip_state(h.p2, &p2_before),
          "Could not read Fighter payload before the later owner restore");
    p1_before.trees[0] = before.p1_tree; p1_before.identities[0] = before.p1_identity;
    p2_before.trees[1] = before.p2_tree; p2_before.identities[1] = before.p2_identity;
    melee_web_action_interval_test_set_fighter_clip_state(h.p1, &p1_before);
    melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_before);
    const State after_payload_restore = state(h);
    check(after_payload_restore.p1_tree == before.p1_tree &&
          after_payload_restore.p2_tree == before.p2_tree &&
          after_payload_restore.p1_identity == before.p1_identity &&
          after_payload_restore.p2_identity == before.p2_identity,
          "Later source-payload owner did not restore exact baseline Fighter clip fields");
    const auto before_replay_destroy = melee_web_action_interval_test_clip_destroy_count();
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_OK,
          "Healthy intermediate recording/replay was treated as a final endpoint");
    h.store1.interval_test_change_cache_owner();
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED,
          "Intermediate guard accepted changed cache owner");
    h.store1.interval_test_restore_cache_owner();
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    if (melee_web_action_interval_finish_replay(interval) != 1) {
        throw std::runtime_error("Same-row replay refused with interval failure " +
            std::to_string(melee_web_action_interval_failure_code(interval)));
    }
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,"Finished interval accepted as active");
    check(same(state(h), final), "Replay did not restore exact FigaTree and source-row pointer identities");
    check(melee_web_action_interval_test_clip_destroy_count() == before_replay_destroy,
          "A retained pre-interval native Clip was destroyed during replay");
    check(melee_web_action_interval_matches_finished(interval)==1,"Finished live endpoint refused");
    h.store1.interval_test_disable_cache_hooks();
    check(melee_web_action_interval_matches_finished(interval)==0 &&
          melee_web_action_interval_failure_code(interval)==MELEE_WEB_ACTION_INTERVAL_OK,
          "Finished hook drift was accepted or changed legacy status");
    h.store1.interval_test_restore_cache_hooks();
    h.store1.interval_test_change_cache_owner();
    check(melee_web_action_interval_matches_finished(interval)==0,"Finished cache owner drift accepted");
    h.store1.interval_test_restore_cache_owner();
    check(melee_web_action_interval_matches_finished(interval)==1,"Restored finished identity refused");
    MeleeWebActionFighterClipState finished_clip{};
    check(melee_web_action_fighter_clip_state(h.p1,&finished_clip),"Finished Fighter clip state unavailable");
    auto changed_clip=finished_clip;changed_clip.trees[0]=nullptr;
    melee_web_action_interval_test_set_fighter_clip_state(h.p1,&changed_clip);
    check(melee_web_action_interval_matches_finished(interval)==0,"Finished Fighter clip drift accepted");
    melee_web_action_interval_test_set_fighter_clip_state(h.p1,&finished_clip);
    check(melee_web_action_interval_matches_finished(interval)==1,"Restored Finished Fighter identity refused");
    const auto before_finished_disarm = melee_web_action_interval_test_clip_destroy_count();
    melee_web_action_interval_disarm(interval);
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
          melee_web_action_interval_test_clip_destroy_count() == before_finished_disarm,
          "Disarming a finished interval changed its successful result or released a lease");
    h.store1.unbind();
    check(melee_web_action_interval_matches_finished(interval)==0,"Retired Finished binding accepted");
    h.store1.bind(h.p1);
    check(melee_web_action_interval_matches_finished(interval)==0&&
          melee_web_action_interval_failure_code(interval)==MELEE_WEB_ACTION_INTERVAL_OK,
          "Recreated Finished binding admitted or legacy status changed");
    melee_web_action_interval_destroy(interval);
    if (policy == melee_web::GameplayActionStorePolicy::ResidentOrdinaryActions) {
        check(melee_web_action_interval_test_clip_destroy_count() == before_replay_destroy,
              "Resident catalog ownership released a Clip during capsule cleanup");
    } else {
        check(melee_web_action_interval_test_clip_destroy_count() > before_replay_destroy,
              "The pre-interval native Clip lease did not release during capsule cleanup");
    }
}

void verify_resident_action_catalog()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    auto fixture = make_fixture();
    auto archive = std::make_shared<const melee_web::DatArchive>(fixture.file());
    auto runtime = std::make_shared<const melee_web::DatFighterRuntime>(
        archive, fighter_runtime_test::mario());
    melee_web::DatFighterAnimationStore decoded(runtime, fixture.container);
    decoded.prepare_resident_native_actions();
    const auto wait = decoded.select_native_action(14);
    const auto special = decoded.select_native_action(ftMr_SM_SpecialN);
    const auto decoded_attack = decoded.select_native_action(ftCo_SM_AttackS4);
    const auto decoded_alias = decoded.select_native_action(8);
    check(wait.animation && special.animation && decoded_attack.animation &&
              decoded_attack.animation == decoded_alias.animation && wait.action.motion_id == 14 &&
              special.action.motion_id == ftMr_SM_SpecialN &&
              decoded_attack.action.motion_id == ftCo_SM_AttackS4 &&
              decoded_alias.action.motion_id == 8,
          "Resident decoder changed exact row identity or failed to retain the full animation key");
    bool repeated_prepare_refused = false;
    try { decoded.prepare_resident_native_actions(); }
    catch (const melee_web::DatError&) { repeated_prepare_refused = true; }
    check(repeated_prepare_refused,
          "Resident decoder accepted repeated catalog preparation");

    Harness h(Policy::ResidentOrdinaryActions);
    constexpr auto alias_motion = std::uint32_t(ftCo_SM_AttackS4);
    check(h.store1.interval_test_resident_catalog_ready() &&
              h.store2.interval_test_resident_catalog_ready(),
          "Explicit resident policy did not finish both catalogs before binding");
    check(h.store1.runtime().actions().size() == fighter_runtime_test::mario().motion_count,
          "Resident catalog omitted authored ordinary rows");

    melee_web::GameplayActionStore::ResidentCatalogTestRow resident_attack{}, resident_alias{};
    check(h.store1.interval_test_resident_catalog_row(alias_motion, &resident_attack) &&
              h.store1.interval_test_resident_catalog_row(8, &resident_alias),
          "Resident catalog did not expose the authored alias entries");
    check(resident_attack.clip == resident_alias.clip &&
              resident_attack.tree == resident_alias.tree &&
              resident_attack.animation == resident_alias.animation &&
              resident_attack.representative_motion == 8,
          "Resident animation alias did not choose its first authored Clip representative");
    melee_web::GameplayActionStore::ResidentCatalogTestRow empty{};
    check(h.store1.interval_test_resident_catalog_row(0, &empty) && empty.clip &&
              !empty.animation && !empty.tree,
          "Resident empty action did not preserve its null-tree source semantics");

    const auto creates_before_selection = melee_web_action_interval_test_clip_create_count();
    h.prepare_wait_only();
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    h.load(h.p1, int(alias_motion), 0);
    h.load(h.p1, 14, 0);
    h.load(h.p1, 8, 0);
    h.load(h.p1, 0, 0);
    check(melee_web_action_interval_test_clip_create_count() == creates_before_selection &&
              h.store1.selected_motion(0) == 0,
          "Resident selection allocated a native Clip after the prebind preparation");

    bool results_refused = false;
    try {
        melee_web::GameplayActionStore unsupported(
            h.archive, fighter_runtime_test::mario(), h.fixture.container,
            h.archive, {}, nullptr, {}, Policy::ResidentOrdinaryActions);
    } catch (const melee_web::DatError& error) {
        results_refused = std::string(error.what()).find("Results motion domain") != std::string::npos;
    }
    check(results_refused,
          "Resident private policy did not refuse a Results domain before binding");
    bool fallback_refused = false;
    try {
        melee_web::GameplayActionStore unsupported(
            h.archive, fighter_runtime_test::mario(), h.fixture.container,
            {}, h.archive, &fighter_runtime_test::mario(), h.fixture.container,
            Policy::ResidentOrdinaryActions);
    } catch (const melee_web::DatError& error) {
        fallback_refused = std::string(error.what()).find("fallback ownership") != std::string::npos;
    }
    check(fallback_refused,
          "Resident private policy admitted Nana/Popo fallback ownership");
}

template<class T> void endpoint_refusal_zero(const T& result, uint32_t status)
{
    T expected{}; expected.status = status;
    check(result.status == status && std::memcmp(&result, &expected, sizeof(T)) == 0,
          "Endpoint refusal returned metadata or the wrong status");
}
void verify_action_endpoint_getters()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    endpoint_refusal_zero(melee_web_action_interval_endpoint_header(nullptr), MELEE_WEB_ACTION_ENDPOINT_INVALID);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_binding(nullptr, 0), MELEE_WEB_ACTION_ENDPOINT_INVALID);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_slot(nullptr, 0, 0), MELEE_WEB_ACTION_ENDPOINT_INVALID);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_cache(nullptr, 0), MELEE_WEB_ACTION_ENDPOINT_INVALID);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_event(nullptr, 0), MELEE_WEB_ACTION_ENDPOINT_INVALID);
    check(melee_web_action_interval_endpoint_admit(nullptr) == MELEE_WEB_ACTION_ENDPOINT_INVALID,
          "Null endpoint admission succeeded");
    Harness h(Policy::ResidentOrdinaryActions); h.prepare_wait_only();
    MeleeWebActionFighterClipState before_p2{};
    check(melee_web_action_fighter_clip_state(h.p2, &before_p2), "Endpoint source baseline unavailable");
    auto* interval = melee_web_action_interval_capture();
    check(interval, "Endpoint capsule capture failed");
    endpoint_refusal_zero(melee_web_action_interval_endpoint_header(interval), MELEE_WEB_ACTION_ENDPOINT_PHASE);
    check(melee_web_action_interval_begin_record(interval) == 1, "Endpoint record failed");
    auto read_batch = [&](uint32_t expected_view, uint32_t expected_provenance) {
        const State before = state(h);
        const auto failure_before = melee_web_action_interval_failure_code(interval);
        watched_allocations = 0; watched_deallocations = 0; watch_allocations = true;
        fail_next_allocation = true;
        check(melee_web_action_interval_endpoint_admit(interval) == MELEE_WEB_ACTION_ENDPOINT_OK,
              "Actual endpoint full admission failed");
        const auto header = melee_web_action_interval_endpoint_header(interval);
        check(header.status == 0 && header.schema == 1 && header.view == expected_view &&
                  header.provenance == expected_provenance && header.binding_count == 6 &&
                  header.slots_per_binding == 2 && header.cache_count == 2,
              "Endpoint header counts/provenance drifted");
        uint32_t live = 0, action_events = 0;
        for (uint32_t i = 0; i < header.binding_count; ++i) {
            const auto binding = melee_web_action_interval_endpoint_binding(interval, i);
            MeleeWebActionBindingSnapshot actual{};
            check(melee_web_action_binding_snapshot(i, &actual) && binding.status == 0 &&
                      binding.index == i && binding.generation == actual.generation &&
                      binding.fighter == reinterpret_cast<std::uintptr_t>(actual.fighter) &&
                      binding.store == reinterpret_cast<std::uintptr_t>(actual.context),
                  "Endpoint binding differs from actual source binding");
            live += binding.present;
            for (uint32_t slot = 0; slot < 2; ++slot) {
                const auto value = melee_web_action_interval_endpoint_slot(interval, i, slot);
                check(value.status == 0 && value.binding == i && value.slot == slot && value.present == binding.present,
                      "Endpoint slot identity drifted");
                if (!binding.present) continue;
                auto* fighter = actual.fighter;
                MeleeWebActionFighterClipState clips{};
                check(melee_web_action_fighter_clip_state(fighter, &clips) &&
                          value.tree == reinterpret_cast<std::uintptr_t>(clips.trees[slot]) &&
                          value.row_identity == reinterpret_cast<std::uintptr_t>(clips.identities[slot]),
                      "Endpoint slot lost actual source tree/row identity");
                if (value.clip) {
                    const auto& runtime = fighter == h.p1 ? h.store1.runtime() : h.store2.runtime();
                    const auto& row = runtime.action(value.motion);
                    check(value.symbol_bytes == row.symbol.size() &&
                              (!value.symbol_bytes || std::memcmp(reinterpret_cast<const void*>(std::uintptr_t(value.symbol_data)),
                                                                  row.symbol.data(), value.symbol_bytes) == 0),
                          "Endpoint slot string transport changed authored key bytes");
                }
            }
        }
        check(live == 2, "Endpoint lost a live source binding");
        for (uint32_t i = 0; i < header.cache_count; ++i) {
            const auto cache = melee_web_action_interval_endpoint_cache(interval, i);
            check(cache.status == 0 && cache.index == i && cache.next < 2 && cache.root &&
                      cache.owner == cache.root && cache.cache && cache.runtime &&
                      cache.interval_owner == cache.owner && cache.hit_callback &&
                      cache.miss_replay_callback && cache.miss_record_callback,
                  "Endpoint logical cache owner/hooks lost exact identity");
            for (const auto& entry : cache.entries)
                check(!entry.animation || (entry.native_action == 1 && entry.symbol_bytes && entry.symbol_data),
                      "Endpoint cache entry transport is incomplete");
        }
        for (uint32_t i = 0; i < header.event_count; ++i) {
            const auto event = melee_web_action_interval_endpoint_event(interval, i);
            check(event.status == 0 && event.index == i && event.kind <= MELEE_WEB_ACTION_ENDPOINT_ACTION_SELECTION,
                  "Endpoint event kind/order/extent drifted");
            if (event.kind == MELEE_WEB_ACTION_ENDPOINT_ACTION_SELECTION) {
                ++action_events;
                check(event.source && event.destination && event.source_generation && event.destination_generation &&
                          event.source_table && event.row_owner && event.clip && event.command_owner &&
                          !event.result_domain && event.slot < 2,
                      "Endpoint action event lost source/domain/row/command provenance");
                const auto& row = h.store2.runtime().action(event.motion);
                check(event.offset == row.container_offset && event.archive_bytes == row.archive_bytes &&
                          event.symbol_bytes == row.symbol.size() &&
                          (!event.symbol_bytes || std::memcmp(reinterpret_cast<const void*>(std::uintptr_t(event.symbol_data)),
                                                              row.symbol.data(), event.symbol_bytes) == 0),
                      "Endpoint actual requested action row changed");
            } else {
                check(event.cache && event.runtime && event.row && event.animation &&
                          event.cache_hit == (event.kind == MELEE_WEB_ACTION_ENDPOINT_CACHE_HIT),
                      "Endpoint cache event lost exact key identity");
            }
        }
        check((expected_view == MELEE_WEB_ACTION_ENDPOINT_PRE) == (header.event_count == 0) &&
                  (expected_view == MELEE_WEB_ACTION_ENDPOINT_PRE || action_events != 0),
              "Endpoint selected the wrong reference/corrected journal");
        endpoint_refusal_zero(melee_web_action_interval_endpoint_binding(interval, 6), MELEE_WEB_ACTION_ENDPOINT_INDEX);
        endpoint_refusal_zero(melee_web_action_interval_endpoint_slot(interval, 0, 2), MELEE_WEB_ACTION_ENDPOINT_INDEX);
        endpoint_refusal_zero(melee_web_action_interval_endpoint_cache(interval, header.cache_count), MELEE_WEB_ACTION_ENDPOINT_INDEX);
        endpoint_refusal_zero(melee_web_action_interval_endpoint_event(interval, header.event_count), MELEE_WEB_ACTION_ENDPOINT_INDEX);
        check(melee_web_action_interval_endpoint_admit(interval) == 0,
              "Endpoint post-batch owner admission failed");
        const bool allocation_guard_unused = fail_next_allocation.exchange(false);
        watch_allocations = false;
        check(allocation_guard_unused && watched_allocations == 0 && watched_deallocations == 0 &&
                  same(state(h), before) && melee_web_action_interval_failure_code(interval) == failure_before,
              "Endpoint getter allocated, released, mutated source state or latched a failure");
    };
    read_batch(MELEE_WEB_ACTION_ENDPOINT_PRE, MELEE_WEB_ACTION_ENDPOINT_REFERENCE);
    h.load(h.p2, ftMr_SM_SpecialN, 1);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    h.load(h.p2, 14, 1);
    check(melee_web_action_interval_seal_record(interval) == 1, "Endpoint reference seal failed");
    read_batch(MELEE_WEB_ACTION_ENDPOINT_END, MELEE_WEB_ACTION_ENDPOINT_REFERENCE);
    check(melee_web_action_interval_restore_corrected(interval) == 1, "Endpoint corrected restore failed");
    melee_web_action_interval_test_set_fighter_clip_state(h.p2, &before_p2);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_header(interval), MELEE_WEB_ACTION_ENDPOINT_PHASE);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_seal_corrected(interval) == 1, "Endpoint corrected seal failed");
    read_batch(MELEE_WEB_ACTION_ENDPOINT_END, MELEE_WEB_ACTION_ENDPOINT_CORRECTED);
    melee_web_action_interval_disarm(interval);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_header(interval), MELEE_WEB_ACTION_ENDPOINT_PHASE);
    h.store1.unbind(); h.store2.unbind(); melee_web_action_interval_destroy(interval);
}

void verify_corrected_recording()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    constexpr auto predicted_action = std::uint32_t(ftMr_SM_SpecialN);
    constexpr auto corrected_action = std::uint32_t(ftCo_SM_AttackS4);
    MeleeWebActionInterval* interval = nullptr;
    State corrected_endpoint{};
    GateTreeWitness predicted_lease{};
    std::uint64_t creates_before = 0, destroys_before = 0;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        const State baseline = state(h);
        MeleeWebActionFighterClipState p2_baseline{};
        check(melee_web_action_fighter_clip_state(h.p2, &p2_baseline),
              "Could not capture corrected-branch Fighter payload baseline");
        creates_before = melee_web_action_interval_test_clip_create_count();
        destroys_before = melee_web_action_interval_test_clip_destroy_count();
        interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin predicted action branch for corrected recording");
        h.load(h.p2, int(predicted_action), 1);
        check(h.store2.selected_motion(1) == predicted_action &&
                  melee_web_action_interval_seal_record(interval) == 1 &&
                  melee_web_action_interval_admit_corrected(interval) == 1,
              "Predicted authored action did not seal for corrected recording admission");
        predicted_lease = gate_slot(h.p2, 1, predicted_action);
        check(melee_web_action_interval_restore_corrected(interval) == 1 &&
                  h.store2.selected_motion(1) == 14,
              "Corrected recording did not restore the original ActionStore prestate");
        /* The interval owns ActionStore slots/cache only. Model the later
         * source-payload owner before taking the corrected branch. */
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_baseline);
        check(same(state(h), baseline),
              "Corrected branch did not begin from the original Fighter payload");

        /* Wait14 is an actual cache hit, then the new B row is a miss. Both
         * and their source selection keys belong to the fresh corrected tape. */
        h.load(h.p2, 14, 1);
        check(melee_web_action_interval_active_failure(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Corrected cache-hit event was not accepted");
        h.load(h.p2, int(corrected_action), 1);
        check(h.store2.selected_motion(1) == corrected_action &&
                  corrected_action != predicted_action &&
                  melee_web_action_interval_active_failure(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Corrected branch did not record a different authored action after its cache miss");
        check(melee_web_action_interval_seal_corrected(interval) == 1 &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Fresh corrected hit/miss/action tape did not seal independently of predicted A");
        corrected_endpoint = state(h);
        check(melee_web_action_interval_test_clip_create_count() == creates_before &&
                  melee_web_action_interval_test_clip_destroy_count() == destroys_before,
              "Resident corrected recording created or released a Clip");

        // No post-seal action may execute, even if a later A->B->A cycle
        // would otherwise restore identical endpoint values. Use the actual
        // checked callback; original void source load must still abort on0.
        MeleeWebActionBindingSnapshot binding{};
        check(melee_web_action_binding_for_context(&h.store2, &binding) == 1,
              "Sealed correction lost its actual binding");
        for (int motion : {int(predicted_action), int(corrected_action)}) {
            auto* tree = reinterpret_cast<FigaTree*>(std::uintptr_t(1));
            void* identity = reinterpret_cast<void*>(std::uintptr_t(2));
            char error[256]{};
            check(binding.select(binding.context, motion, 1, &tree, &identity,
                                 error, sizeof(error)) == 0 && error[0] &&
                      tree == reinterpret_cast<FigaTree*>(std::uintptr_t(1)) &&
                      identity == reinterpret_cast<void*>(std::uintptr_t(2)) &&
                      same(state(h), corrected_endpoint) &&
                      melee_web_action_interval_failure_code(interval) ==
                          MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,
                  "Sealed correction permitted unjournaled action or lost sticky refusal");
        }
        check(melee_web_action_interval_seal_corrected(interval) == 0 &&
                  melee_web_action_interval_failure_code(interval) ==
                      MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,
              "Post-seal refusal resumed corrected recording");
        melee_web_action_interval_disarm(interval);
        h.store1.unbind();
        h.store2.unbind();
    }
    const auto destroys_after_owner_retirement = melee_web_action_interval_test_clip_destroy_count();
    const auto predicted_after_owner_retirement = gate_tree(
        reinterpret_cast<FigaTree*>(predicted_lease.tree),
        reinterpret_cast<void*>(predicted_lease.row_identity), predicted_action);
    check(same_gate_tree(predicted_lease, predicted_after_owner_retirement),
          "Predicted borrowed tree was not retained through source-owner retirement");
    melee_web_action_interval_destroy(interval);
    check(melee_web_action_interval_test_clip_destroy_count() > destroys_after_owner_retirement,
          "Corrected and predicted Clip leases did not release after source-owner retirement");
    check(corrected_endpoint.p2_motion == corrected_action,
          "Corrected endpoint witness lost the different authored action key");
}

void verify_confirmed_action_retirement()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    constexpr auto action_a = std::uint32_t(ftMr_SM_SpecialN);
    constexpr auto action_b = std::uint32_t(ftCo_SM_AttackS4);
    static_assert(action_a < 303 && action_b < 303 && action_a != action_b);
    check(!melee_web_action_interval_retire_admit(nullptr) &&
              !melee_web_action_interval_retire_confirmed(nullptr),
          "Null ActionStore retirement was admitted");

    std::uint64_t resident_destroys = 0;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        MeleeWebActionFighterClipState baseline_p1{}, baseline_p2{};
        check(melee_web_action_fighter_clip_state(h.p1, &baseline_p1) &&
                  melee_web_action_fighter_clip_state(h.p2, &baseline_p2),
              "Could not save live Fighter fields before confirmed retirement");
        melee_web::GameplayActionStore::ResidentCatalogTestRow s1_attack{}, s1_alias{}, s1_empty{}, s2_attack{};
        check(h.store1.interval_test_resident_catalog_row(action_b, &s1_attack) &&
                  h.store1.interval_test_resident_catalog_row(8, &s1_alias) &&
                  h.store1.interval_test_resident_catalog_row(0, &s1_empty) &&
                  h.store2.interval_test_resident_catalog_row(action_b, &s2_attack) &&
                  s1_attack.clip == s1_alias.clip && s1_attack.tree == s1_alias.tree &&
                  s1_attack.animation == s1_alias.animation && s1_empty.clip &&
                  !s1_empty.animation && !s1_empty.tree && s1_attack.clip != s2_attack.clip,
              "Could not pin exact source catalog Clip/alias/empty identities before retirement");
        auto* interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval),
              "Could not begin corrected retirement fixture");
        const State pre = state(h);
        check(!melee_web_action_interval_retire_admit(interval) &&
                  !melee_web_action_interval_retire_confirmed(interval) &&
                  melee_web_action_interval_active_failure(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
                  same(state(h), pre),
              "Recording phase retirement refusal changed the live ActionStore interval");

        // Actual source transfer, aliased authored row, and null-tree empty
        // row all remain represented in the retained action tapes.
        melee_web_action_load(h.p2, h.p1, int(action_b), 1);
        h.load(h.p1, 0, 0);
        h.load(h.p2, 8, 1);
        const auto predicted = gate_slot(h.p2, 1, 8);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
                  melee_web_action_interval_admit(interval) == 1 &&
                  melee_web_action_interval_retire_admit(interval) == 1,
              "Sealed resident transfer/alias/empty-row tape did not prove catalog ownership");
        check(melee_web_action_interval_admit_corrected(interval) == 1 &&
                  melee_web_action_interval_restore_corrected(interval) == 1,
              "Resident predicted tape could not restore before corrected retirement");
        melee_web_action_interval_test_set_fighter_clip_state(h.p1, &baseline_p1);
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &baseline_p2);
        check(same(state(h), pre),
              "Modeled source payload did not restore the ActionStore prestate before correction");
        h.load(h.p2, 14, 1);
        h.load(h.p2, 0, 1);
        melee_web_action_load(h.p2, h.p1, int(action_b), 1);
        check(melee_web_action_interval_seal_corrected(interval) == 1 &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Corrected resident transfer tape did not seal cleanly");

        const State endpoint = state(h);
        const auto endpoint_tree = gate_slot(h.p2, 1, action_b);
        std::vector<std::byte> p1_bytes(gate_fighter_bytes), p2_bytes(gate_fighter_bytes);
        std::memcpy(p1_bytes.data(), h.p1_storage.bytes.data(), p1_bytes.size());
        std::memcpy(p2_bytes.data(), h.p2_storage.bytes.data(), p2_bytes.size());
        const auto creates_before_retire = melee_web_action_interval_test_clip_create_count();
        resident_destroys = melee_web_action_interval_test_clip_destroy_count();
        watched_allocations = 0;
        watched_deallocations = 0;
        watch_allocations = true;
        const int retirement_admitted = melee_web_action_interval_retire_admit(interval);
        const int retirement_detached = melee_web_action_interval_retire_confirmed(interval);
        const int repeated_admission = melee_web_action_interval_retire_admit(interval);
        const int repeated_detach = melee_web_action_interval_retire_confirmed(interval);
        watch_allocations = false;
        check(retirement_admitted == 1 && retirement_detached == 1 &&
                  repeated_admission == 0 && repeated_detach == 0 &&
                  watched_allocations == 0 && watched_deallocations == 0 &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
                  melee_web_action_interval_active_failure(interval) == MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,
              "Confirmed retirement was not a zero-allocation, one-time detach");
        melee_web_action_interval_disarm(interval);
        check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
                  same(state(h), endpoint),
              "Generic disarm after confirmed retirement latched failure or changed source state");
        melee_web_action_interval_destroy(interval);
        melee_web::GameplayActionStore::ResidentCatalogTestRow s1_attack_after{}, s1_alias_after{}, s1_empty_after{}, s2_attack_after{};
        check(melee_web_action_interval_test_clip_destroy_count() == resident_destroys &&
                  melee_web_action_interval_test_clip_create_count() == creates_before_retire &&
                  same(state(h), endpoint) &&
                  same_gate_tree(endpoint_tree, gate_slot(h.p2, 1, action_b)) &&
                  h.store1.interval_test_resident_catalog_row(action_b, &s1_attack_after) &&
                  h.store1.interval_test_resident_catalog_row(8, &s1_alias_after) &&
                  h.store1.interval_test_resident_catalog_row(0, &s1_empty_after) &&
                  h.store2.interval_test_resident_catalog_row(action_b, &s2_attack_after) &&
                  s1_attack.clip == s1_attack_after.clip && s1_attack.animation == s1_attack_after.animation &&
                  s1_attack.tree == s1_attack_after.tree &&
                  s1_alias.clip == s1_alias_after.clip && s1_alias.animation == s1_alias_after.animation &&
                  s1_alias.tree == s1_alias_after.tree &&
                  s1_empty.clip == s1_empty_after.clip && !s1_empty_after.animation && !s1_empty_after.tree &&
                  s2_attack.clip == s2_attack_after.clip && s2_attack.animation == s2_attack_after.animation &&
                  s2_attack.tree == s2_attack_after.tree &&
                  std::memcmp(p1_bytes.data(), h.p1_storage.bytes.data(), p1_bytes.size()) == 0 &&
                  std::memcmp(p2_bytes.data(), h.p2_storage.bytes.data(), p2_bytes.size()) == 0,
              "Destroying a confirmed capsule changed a live Fighter/Clip tree or released catalog ownership");

        // A second capsule on the same source owners proves the first detach
        // removed the one global callback route and did not retain its tape.
        auto* next = melee_web_action_interval_capture();
        check(next && melee_web_action_interval_begin_record(next) == 1,
              "A fresh ActionStore capsule could not start after confirmed retirement");
        h.load(h.p2, int(action_a), 1);
        check(melee_web_action_interval_seal_record(next) == 1 &&
                  melee_web_action_interval_admit(next) == 1 &&
                  melee_web_action_interval_restore(next) == 1,
              "Sequential capsule could not enter exact replay from its resident tape");
        h.load(h.p2, int(action_a), 1);
        check(melee_web_action_interval_finish_replay(next) == 1 &&
                  melee_web_action_interval_retire_admit(next) == 1 &&
                  melee_web_action_interval_retire_confirmed(next) == 1 &&
                  melee_web_action_interval_failure_code(next) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Finished exact-replay capsule could not retire from its terminal phase");
        melee_web_action_interval_destroy(next);
        check(melee_web_action_interval_test_clip_destroy_count() == resident_destroys,
              "Sequential capsule destruction released a Clip still owned by the resident catalog");
    }
    check(melee_web_action_interval_test_clip_destroy_count() > resident_destroys,
          "Final source-owner teardown did not release resident Clip storage");

    // A valid Clip owned by another live source catalog must not authorize
    // retirement of this source's event tape, even with both stores alive.
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        auto* interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin provenance-refusal control");
        h.load(h.p2, int(action_a), 1);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
                  melee_web_action_interval_retire_admit(interval) == 1 &&
                  melee_web_action_interval_test_rehome_first_selection_clip(interval) == 1,
              "Could not prepare wrong-source resident Clip provenance control");
        const State endpoint = state(h);
        check(!melee_web_action_interval_retire_admit(interval) &&
                  !melee_web_action_interval_retire_confirmed(interval) &&
                  same(state(h), endpoint) &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Wrong-source Clip provenance refusal mutated or failed the sealed capsule");
        melee_web_action_interval_disarm(interval);
        check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED,
              "Ordinary disarm semantics changed after retirement refusal");
        h.store1.unbind(); h.store2.unbind();
        melee_web_action_interval_destroy(interval);
    }

    // Legacy mode remains ineligible even when its sealed endpoint is clean.
    {
        Harness h(Policy::Legacy);
        h.prepare_wait_only();
        auto* interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin Legacy retirement refusal control");
        h.load(h.p2, int(action_a), 1);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
                  !melee_web_action_interval_retire_admit(interval) &&
                  !melee_web_action_interval_retire_confirmed(interval) &&
                  melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Legacy source storage entered proof-backed confirmed retirement");
        melee_web_action_interval_disarm(interval);
        h.store1.unbind(); h.store2.unbind();
        melee_web_action_interval_destroy(interval);
    }

    // A changed six-row binding ledger refuses before it can inspect a stale
    // Store pointer, while leaving the original phase/failure state untouched.
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        auto* interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin stale-binding retirement refusal control");
        h.load(h.p2, int(action_a), 1);
        check(melee_web_action_interval_seal_record(interval) == 1,
              "Stale-binding interval did not seal before unbind");
        check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK,
              "Fresh sealed interval reported a preexisting failure before stale-binding check");
        h.store2.unbind();
        const auto stale_status = melee_web_action_interval_failure_code(interval);
        check(!melee_web_action_interval_retire_admit(interval) &&
                  !melee_web_action_interval_retire_confirmed(interval) &&
                  stale_status == MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED &&
                  melee_web_action_interval_failure_code(interval) == stale_status,
              "Stale binding retirement refusal changed phase or latched an additional failure");
        h.store1.unbind();
        melee_web_action_interval_destroy(interval);
    }
}

void verify_corrected_recording_failure_refuses_publication()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    constexpr auto predicted_action = std::uint32_t(ftMr_SM_SpecialN);
    constexpr auto corrected_action = std::uint32_t(ftCo_SM_AttackS4);
    MeleeWebActionInterval* interval = nullptr;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        const State baseline = state(h);
        MeleeWebActionFighterClipState p2_baseline{};
        check(melee_web_action_fighter_clip_state(h.p2, &p2_baseline),
              "Could not capture allocation-failure Fighter baseline");
        interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin corrected event-allocation control");
        h.load(h.p2, int(predicted_action), 1);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
                  melee_web_action_interval_admit_corrected(interval) == 1 &&
                  melee_web_action_interval_restore_corrected(interval) == 1,
              "Could not restore predicted action before corrected event-allocation control");
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_baseline);
        check(same(state(h), baseline),
              "Allocation-failure branch did not restore the original Fighter payload");

        // The original void action loader aborts on callback refusal. Exercise
        // its actual checked binding callback directly for expected negatives;
        // production fail-loud semantics stay unchanged.
        auto refused_load = [&](int motion) {
            MeleeWebActionBindingSnapshot binding{};
            check(melee_web_action_binding_for_context(&h.store2, &binding) == 1 &&
                      binding.fighter == h.p2 && binding.select,
                  "Expected-refusal control lost its actual source binding");
            auto* tree = reinterpret_cast<FigaTree*>(std::uintptr_t(1));
            void* identity = reinterpret_cast<void*>(std::uintptr_t(2));
            char error[256]{};
            check(binding.select(binding.context, motion, 1, &tree, &identity,
                                 error, sizeof(error)) == 0 && error[0] &&
                      tree == reinterpret_cast<FigaTree*>(std::uintptr_t(1)) &&
                      identity == reinterpret_cast<void*>(std::uintptr_t(2)),
                  "Failed actual selection callback published outputs or hid refusal");
        };
        melee_web_action_interval_test_fail_next_event_push();
        refused_load(int(corrected_action));
        check(same(state(h), baseline) &&
                  melee_web_action_interval_active_failure(interval) ==
                      MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
              "Corrected event allocation failure published a borrowed Fighter pointer");
        refused_load(int(predicted_action));
        check(same(state(h), baseline) &&
                  melee_web_action_interval_failure_code(interval) ==
                      MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
              "Selection resumed after terminal corrected event allocation failure");
        check(melee_web_action_interval_seal_corrected(interval) == 0,
              "Failed corrected event tape was accepted at seal");
        refused_load(int(corrected_action));
        check(same(state(h), baseline) &&
                  melee_web_action_interval_failure_code(interval) ==
                      MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
              "Selection resumed after failed corrected seal entered its terminal phase");
        melee_web_action_interval_disarm(interval);
        h.store1.unbind();
        h.store2.unbind();
    }
    melee_web_action_interval_destroy(interval);
}

void verify_corrected_admission_refusals()
{
    using Policy = melee_web::GameplayActionStorePolicy;
    constexpr auto predicted_action = std::uint32_t(ftMr_SM_SpecialN);
    MeleeWebActionInterval* captured_phase = nullptr;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        const State before = state(h);
        captured_phase = melee_web_action_interval_capture();
        check(captured_phase && !melee_web_action_interval_admit_corrected(captured_phase) &&
                  !melee_web_action_interval_restore_corrected(captured_phase) && same(state(h), before) &&
                  melee_web_action_interval_failure_code(captured_phase) == MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,
              "Corrected restore accepted the wrong Captured phase or wrote source state");
        melee_web_action_interval_disarm(captured_phase);
        h.store1.unbind();
        h.store2.unbind();
    }
    melee_web_action_interval_destroy(captured_phase);

    MeleeWebActionInterval* stale_binding = nullptr;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        stale_binding = melee_web_action_interval_capture();
        check(stale_binding && melee_web_action_interval_begin_record(stale_binding) == 1,
              "Could not begin stale-binding corrected-admission control");
        h.load(h.p1, int(predicted_action), 0);
        check(melee_web_action_interval_seal_record(stale_binding) == 1,
              "Could not seal stale-binding corrected-admission control");
        h.store1.unbind();
        h.store1.bind(h.p1);
        melee_web_action_interval_test_set_fighter_table(h.p1, h.store1.action_rows());
        const State before = state(h);
        check(!melee_web_action_interval_admit_corrected(stale_binding) &&
                  !melee_web_action_interval_restore_corrected(stale_binding) && same(state(h), before) &&
                  melee_web_action_interval_failure_code(stale_binding) ==
                      MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
              "Corrected restore crossed a stale binding generation or wrote source state");
        melee_web_action_interval_disarm(stale_binding);
        h.store1.unbind();
        h.store2.unbind();
    }
    melee_web_action_interval_destroy(stale_binding);

    MeleeWebActionInterval* repeated = nullptr;
    {
        Harness h(Policy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        repeated = melee_web_action_interval_capture();
        check(repeated && melee_web_action_interval_begin_record(repeated) == 1,
              "Could not begin repeat-correction control");
        h.load(h.p1, int(predicted_action), 0);
        check(melee_web_action_interval_seal_record(repeated) == 1 &&
                  melee_web_action_interval_admit_corrected(repeated) == 1 &&
                  melee_web_action_interval_restore_corrected(repeated) == 1,
              "Could not enter the one-shot corrected phase for repeat control");
        h.load(h.p1, 14, 0);
        check(melee_web_action_interval_seal_corrected(repeated) == 1,
              "Could not seal the first corrected phase for repeat control");
        const State before = state(h);
        check(!melee_web_action_interval_admit_corrected(repeated) &&
                  !melee_web_action_interval_restore_corrected(repeated) && same(state(h), before) &&
                  melee_web_action_interval_failure_code(repeated) == MELEE_WEB_ACTION_INTERVAL_BAD_PHASE,
              "A second corrected recording was admitted or mutated source state");
        melee_web_action_interval_disarm(repeated);
        h.store1.unbind();
        h.store2.unbind();
    }
    melee_web_action_interval_destroy(repeated);
}

void verify_corrected_retired_store_refusal()
{
    MeleeWebActionInterval* interval = nullptr;
    {
        Harness h(melee_web::GameplayActionStorePolicy::ResidentOrdinaryActions);
        h.prepare_wait_only();
        interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Retired-store correction setup failed");
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        check(melee_web_action_interval_seal_record(interval) == 1,
              "Retired-store predicted endpoint did not seal");
    } // Binding rows retire and the actual C++ stores are destroyed here.
    const auto status_before_retirement_refusal = melee_web_action_interval_failure_code(interval);
    check(status_before_retirement_refusal == MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED &&
              !melee_web_action_interval_retire_admit(interval) &&
              !melee_web_action_interval_retire_confirmed(interval) &&
              melee_web_action_interval_failure_code(interval) == status_before_retirement_refusal,
          "Pure retirement refusal after Store destruction changed the capsule failure status");
    check(melee_web_action_interval_endpoint_admit(interval) == MELEE_WEB_ACTION_ENDPOINT_OWNER,
          "Endpoint full admission accepted retired stores");
    endpoint_refusal_zero(melee_web_action_interval_endpoint_binding(interval, 0), MELEE_WEB_ACTION_ENDPOINT_OWNER);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_slot(interval, 0, 0), MELEE_WEB_ACTION_ENDPOINT_OWNER);
    endpoint_refusal_zero(melee_web_action_interval_endpoint_cache(interval, 0), MELEE_WEB_ACTION_ENDPOINT_OWNER);
    check(melee_web_action_interval_admit_corrected(interval) == 0 &&
              melee_web_action_interval_restore_corrected(interval) == 0 &&
              melee_web_action_interval_failure_code(interval) ==
                  MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
          "Corrected admission dereferenced retired store or lost binding refusal");
    melee_web_action_interval_disarm(interval);
    melee_web_action_interval_destroy(interval);
}

void verify_active_binding_guard()
{
    Harness h;h.prepare();auto* interval=melee_web_action_interval_capture();
    check(interval&&melee_web_action_interval_begin_record(interval)==1,"Active binding guard setup");
    h.store1.unbind();
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
          "Active interval accepted retired live binding");
    h.store1.bind(h.p1);
    check(melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
          "Active interval accepted reused binding address after generation change");
    melee_web_action_interval_disarm(interval);melee_web_action_interval_destroy(interval);
}
void verify_capture_failure_is_read_only()
{
    Harness h;
    h.prepare();
    const State before = state(h);
    fail_next_allocation = true;
    auto* interval = melee_web_action_interval_capture();
    fail_next_allocation = false;
    check(interval == nullptr, "Injected capsule allocation failure unexpectedly captured");
    check(same(state(h), before), "Capture allocation refusal changed the source owner state");
}

void verify_event_allocation_failure_refuses_admission()
{
    Harness h;
    h.prepare();
    auto* interval = melee_web_action_interval_capture();
    check(interval && melee_web_action_interval_begin_record(interval) == 1,
          "Could not begin allocation-failure control");
    melee_web_action_interval_test_fail_next_event_push();
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_seal_record(interval) == 0,
          "A failed baseline event allocation was accepted");
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED &&
          melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
          "Event-storage allocation failure was not reported");
    check(melee_web_action_interval_admit(interval) == 0 &&
              !melee_web_action_interval_retire_admit(interval) &&
              !melee_web_action_interval_retire_confirmed(interval) &&
              melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
          "An incomplete selection trace reached replay or retirement admission");
    melee_web_action_interval_destroy(interval);
}

void verify_seal_allocation_failure_refuses_admission()
{
    Harness h;
    h.prepare();
    auto* interval = melee_web_action_interval_capture();
    check(interval && melee_web_action_interval_begin_record(interval) == 1,
          "Could not begin seal-allocation control");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    const State baseline = state(h);
    melee_web_action_interval_test_fail_next_seal_capture();
    check(melee_web_action_interval_seal_record(interval) == 0,
          "A failed final-state capture was accepted");
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED &&
          melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
          "Seal allocation failure was not reported as an incomplete record");
    check(melee_web_action_interval_admit(interval) == 0 && same(state(h), baseline),
          "Failed seal capture changed the source owner or reached restore admission");
    melee_web_action_interval_destroy(interval);
}

void verify_changed_binding_refuses_without_restore_writes()
{
    Harness h;
    h.prepare();
    auto* interval = melee_web_action_interval_capture();
    check(interval && melee_web_action_interval_begin_record(interval) == 1,
          "Could not begin binding-generation control");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_seal_record(interval) == 1,
          "Binding-generation control trace did not seal");
    h.store1.unbind();
    h.store1.bind(h.p1);
    melee_web_action_interval_test_set_fighter_table(h.p1, h.store1.action_rows());
    const State after_rebind = state(h);
    check(melee_web_action_interval_admit(interval) == 0,
          "A reused binding address and changed generation were accepted");
    check(melee_web_action_interval_restore(interval) == 0,
          "Restore crossed an unbind/rebind generation boundary");
    check(same(state(h), after_rebind), "Refused stale-binding restore changed the live owner state");
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED,
          "Changed binding did not expose the refusal reason");
    melee_web_action_interval_destroy(interval);
}

void verify_cache_hook_configuration_is_part_of_admission_identity()
{
    for (const unsigned mutation : std::array<unsigned, 3>{0, 1, 2}) {
        Harness h;
        h.prepare();
        auto* interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin cache-hook identity control");
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        h.load(h.p2, ftCo_SM_AttackS4, 1);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
              melee_web_action_interval_admit(interval) == 1,
              "Could not seal cache-hook identity control");
        const State before = state(h);
        if (mutation == 0) h.store1.interval_test_disable_cache_hooks();
        else if (mutation == 1) h.store1.interval_test_change_cache_hooks();
        else h.store1.interval_test_change_cache_owner();
        check(melee_web_action_interval_admit(interval) == 0,
              "A changed ActionStore cache hook owner/configuration was admitted");
        check(melee_web_action_interval_restore(interval) == 0 && same(state(h), before),
              "A changed ActionStore cache hook owner/configuration bypassed restore validation");
        check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED,
              "Cache hook owner/configuration mismatch did not retain its refusal reason");
        if (mutation == 2) h.store1.interval_test_restore_cache_owner();
        else h.store1.interval_test_restore_cache_hooks();
        melee_web_action_interval_destroy(interval);
    }
}

void verify_disarm_keeps_leases_until_source_owner_retirement()
{
    MeleeWebActionInterval* interval = nullptr;
    std::uint64_t before_disarm = 0;
    {
        Harness h;
        h.prepare();
        const State before = state(h);
        interval = melee_web_action_interval_capture();
        check(interval && melee_web_action_interval_begin_record(interval) == 1,
              "Could not begin disarm lease-order control");
        h.load(h.p1, ftMr_SM_SpecialN, 0);
        h.load(h.p2, ftCo_SM_AttackS4, 1);
        check(melee_web_action_interval_seal_record(interval) == 1 &&
              melee_web_action_interval_admit(interval) == 1 &&
              melee_web_action_interval_restore(interval) == 1,
              "Could not reach the active replay phase for disarm lease-order control");
        const State after_restore = state(h);
        watched_allocations = 0;
        watched_deallocations = 0;
        watch_allocations = true;
        before_disarm = melee_web_action_interval_test_clip_destroy_count();
        melee_web_action_interval_disarm(interval);
        watch_allocations = false;
        check(watched_allocations == 0 && watched_deallocations == 0 &&
              melee_web_action_interval_test_clip_destroy_count() == before_disarm,
              "Disarm allocated, freed, or released a native Clip lease");
        check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED &&
              melee_web_action_interval_begin_record(interval) == 0 &&
              melee_web_action_interval_restore(interval) == 0 &&
              melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED,
              "Disarmed unfinished interval was reusable or lost its terminal status");
        check(same(state(h), after_restore), "Disarm wrote live Fighter or ActionStore state");

        /* Model the aggregate's later Fighter-payload owner before taking a
         * second capsule: ActionStore restore intentionally leaves these
         * fields at the baseline-final values until that owner commits. */
        MeleeWebActionFighterClipState p1_payload{}, p2_payload{};
        check(melee_web_action_fighter_clip_state(h.p1, &p1_payload) &&
              melee_web_action_fighter_clip_state(h.p2, &p2_payload),
              "Could not read Fighter fields for the later payload-owner control");
        p1_payload.trees[0] = before.p1_tree;
        p1_payload.identities[0] = before.p1_identity;
        p2_payload.trees[1] = before.p2_tree;
        p2_payload.identities[1] = before.p2_identity;
        melee_web_action_interval_test_set_fighter_clip_state(h.p1, &p1_payload);
        melee_web_action_interval_test_set_fighter_clip_state(h.p2, &p2_payload);
        check(same(state(h), before),
              "Later payload-owner control did not restore both Fighter clip fields before probe capture");

        auto* probe = melee_web_action_interval_capture();
        check(probe && melee_web_action_interval_begin_record(probe) == 1,
              "Disarm did not clear the exact active capsule before owner cleanup");
        melee_web_action_interval_disarm(probe);
        melee_web_action_interval_destroy(probe);
        check(melee_web_action_interval_test_clip_destroy_count() == before_disarm,
              "Probe capsule cleanup released leases retained by the disarmed capsule");

        h.load(h.p1, 14, 0);
        check(same(state(h), before) &&
              melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_DISARMED,
              "Ordinary source selection after disarm was intercepted or altered the capsule status");
        h.store1.unbind();
        h.store2.unbind();
        check(melee_web_action_interval_test_clip_destroy_count() == before_disarm,
              "Source-owner retirement released Clips still leased by the capsule");
    }
    check(melee_web_action_interval_test_clip_destroy_count() == before_disarm,
          "Source-owner destruction released leases before the capsule was destroyed");
    melee_web_action_interval_destroy(interval);
    check(melee_web_action_interval_test_clip_destroy_count() > before_disarm,
          "Final capsule destruction did not release its retained Clip leases");
}

void verify_unexpected_replay_selection_fails_the_experiment(
    melee_web::GameplayActionStorePolicy policy)
{
    Harness h(policy);
    h.prepare();
    auto* interval = melee_web_action_interval_capture();
    check(interval && melee_web_action_interval_begin_record(interval) == 1,
          "Could not begin replay-mismatch control");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_seal_record(interval) == 1 &&
          melee_web_action_interval_admit(interval) == 1,
          "Could not seal the expected replay sequence");
    check(melee_web_action_interval_restore(interval) == 1,
          "Could not restore before mismatched source replay");
    h.load(h.p1, 8, 0);
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    check(melee_web_action_interval_finish_replay(interval) == 0,
          "Unexpected source row sequence was accepted as a completed replay");
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH &&
          melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH,
          "Unexpected replay selection did not latch the mismatch reason");
    melee_web_action_interval_disarm(interval);
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH &&
          melee_web_action_interval_active_failure(interval)==MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH,
          "Disarm replaced the original replay mismatch with cleanup status");
    melee_web_action_interval_destroy(interval);
}
} // namespace

void* operator new(std::size_t size)
{
    if (fail_next_allocation.exchange(false)) throw std::bad_alloc();
    if (watch_allocations) ++watched_allocations;
    if (void* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{ try { return ::operator new(size); } catch (...) { return nullptr; } }
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{ return ::operator new(size, tag); }
void operator delete(void* pointer) noexcept
{
    if (watch_allocations && pointer) ++watched_deallocations;
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { ::operator delete(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { ::operator delete(pointer); }

int main(int argc, char** argv)
{
    if (argc == 2 && (std::strcmp(argv[1], "history-layout-clean") == 0 ||
                      std::strcmp(argv[1], "history-layout-repeat") == 0)) {
        try {
            run_history_layout_gate(std::strcmp(argv[1], "history-layout-repeat") == 0);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "ActionStore history layout gate failed: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && (std::strcmp(argv[1], "gate-a") == 0 ||
                      std::strcmp(argv[1], "gate-b") == 0)) {
        try {
            /* Both lane names have equal length. No lane-specific output or
             * formatting allocation occurs until after endpoint capture. */
            run_retention_gate(std::strcmp(argv[1], "gate-b") == 0);
            print_retention_gate();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << "ActionStore retention gate failed: " << error.what() << '\n';
            return 1;
        }
    }
    try {
        static_assert(ftMr_SM_SpecialN < 303 && ftCo_SM_AttackS4 < 303);
        verify_round_trip(melee_web::GameplayActionStorePolicy::Legacy);
        verify_round_trip(melee_web::GameplayActionStorePolicy::ResidentOrdinaryActions);
        verify_retained_pre_action_history();
        verify_retained_pre_action_history_refusals();
        verify_history_event_budget_refusal();
        verify_resident_action_catalog();
        verify_action_endpoint_getters();
        verify_corrected_recording();
        verify_confirmed_action_retirement();
        verify_corrected_recording_failure_refuses_publication();
        verify_corrected_admission_refusals();
        verify_corrected_retired_store_refusal();
        verify_active_binding_guard();
        verify_capture_failure_is_read_only();
        verify_event_allocation_failure_refuses_admission();
        verify_seal_allocation_failure_refuses_admission();
        verify_changed_binding_refuses_without_restore_writes();
        verify_cache_hook_configuration_is_part_of_admission_identity();
        verify_disarm_keeps_leases_until_source_owner_retirement();
        verify_unexpected_replay_selection_fails_the_experiment(
            melee_web::GameplayActionStorePolicy::Legacy);
        verify_unexpected_replay_selection_fails_the_experiment(
            melee_web::GameplayActionStorePolicy::ResidentOrdinaryActions);
        std::cout << "ActionStore interval actual-source controls passed; Mario action rows "
                  << ftMr_SM_SpecialN << " and " << ftCo_SM_AttackS4
                  << " are distinct from Fighter motion-state IDs.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ActionStore interval control failed: " << error.what() << '\n';
        return 1;
    }
}
