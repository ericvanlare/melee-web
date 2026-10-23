#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <melee/ft/kinds/ftCommon/forward.h>
#include <melee/ft/kinds/ftMario/forward.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <new>

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

    Harness()
        : fixture(make_fixture()),
          archive(std::make_shared<const melee_web::DatArchive>(fixture.file())),
          store1(archive, fighter_runtime_test::mario(), fixture.container),
          store2(archive, fighter_runtime_test::mario(), fixture.container)
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

void verify_round_trip()
{
    Harness h;
    h.prepare();
    const State before = state(h);
    auto* interval = melee_web_action_interval_capture();
    check(interval != nullptr, "Capture rejected the healthy two-store source fixture");
    check(melee_web_action_interval_begin_record(interval) == 1,
          "Recording phase did not begin on the captured owner state");
    h.load(h.p1, ftMr_SM_SpecialN, 0);
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
    h.load(h.p2, ftCo_SM_AttackS4, 1);
    if (melee_web_action_interval_finish_replay(interval) != 1) {
        throw std::runtime_error("Same-row replay refused with interval failure " +
            std::to_string(melee_web_action_interval_failure_code(interval)));
    }
    check(same(state(h), final), "Replay did not restore exact FigaTree and source-row pointer identities");
    check(melee_web_action_interval_test_clip_destroy_count() == before_replay_destroy,
          "A retained pre-interval native Clip was destroyed during replay");
    const auto before_finished_disarm = melee_web_action_interval_test_clip_destroy_count();
    melee_web_action_interval_disarm(interval);
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_OK &&
          melee_web_action_interval_test_clip_destroy_count() == before_finished_disarm,
          "Disarming a finished interval changed its successful result or released a lease");
    melee_web_action_interval_destroy(interval);
    check(melee_web_action_interval_test_clip_destroy_count() > before_replay_destroy,
          "The pre-interval native Clip lease did not release during capsule cleanup");
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
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
          "Event-storage allocation failure was not reported");
    check(melee_web_action_interval_admit(interval) == 0,
          "An incomplete selection trace reached restore admission");
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
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED,
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

void verify_unexpected_replay_selection_fails_the_experiment()
{
    Harness h;
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
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH,
          "Unexpected replay selection did not latch the mismatch reason");
    melee_web_action_interval_disarm(interval);
    check(melee_web_action_interval_failure_code(interval) == MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH,
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

int main()
{
    try {
        static_assert(ftMr_SM_SpecialN < 303 && ftCo_SM_AttackS4 < 303);
        verify_round_trip();
        verify_capture_failure_is_read_only();
        verify_event_allocation_failure_refuses_admission();
        verify_seal_allocation_failure_refuses_admission();
        verify_changed_binding_refuses_without_restore_writes();
        verify_cache_hook_configuration_is_part_of_admission_identity();
        verify_disarm_keeps_leases_until_source_owner_retirement();
        verify_unexpected_replay_selection_fails_the_experiment();
        std::cout << "ActionStore interval actual-source controls passed; Mario action rows "
                  << ftMr_SM_SpecialN << " and " << ftCo_SM_AttackS4
                  << " are distinct from Fighter motion-state IDs.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ActionStore interval control failed: " << error.what() << '\n';
        return 1;
    }
}
