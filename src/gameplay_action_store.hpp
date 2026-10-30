#pragma once
#include "gameplay_action_store.h"
#include "dat_commands.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <tuple>
#include <vector>
namespace melee_web {
enum class GameplayActionStorePolicy : std::uint8_t {
    Legacy,
    ResidentOrdinaryActions,
};

class GameplayActionStore {
public:
    GameplayActionStore(std::shared_ptr<const DatArchive>, const FighterCostume&,
                        std::span<const uint8_t> container,
                        std::shared_ptr<const DatArchive> result_motion = {},
                        std::shared_ptr<const DatArchive> nana_popo_archive = {},
                        const FighterCostume* nana_popo_identity = nullptr,
                        std::span<const uint8_t> nana_popo_container = {},
                        GameplayActionStorePolicy policy = GameplayActionStorePolicy::Legacy);
    ~GameplayActionStore();
    GameplayActionStore(const GameplayActionStore&) = delete;
    GameplayActionStore& operator=(const GameplayActionStore&) = delete;
    // Unbind before the Fighter is freed; the store retains every published stream.
    void bind(Fighter*);
    void unbind();
    void* action_rows() const;
    void* blend_rows() const;
    void* demo_action_rows() const;
    void* demo_blend_rows() const;
    void* wait_choices() const;
    const DatFighterRuntime& runtime() const noexcept { return *runtime_; }
    uint32_t selected_motion(unsigned slot) const;
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
    struct ResidentCatalogTestRow {
        std::uint32_t representative_motion = UINT32_MAX;
        const void* clip = nullptr;
        const DatAnimation* animation = nullptr;
        const FigaTree* tree = nullptr;
    };
    [[nodiscard]] bool interval_test_resident_catalog_ready() const noexcept;
    [[nodiscard]] bool interval_test_resident_catalog_row(
        std::uint32_t motion, ResidentCatalogTestRow* out) const noexcept;
    void interval_test_disable_cache_hooks();
    void interval_test_change_cache_hooks();
    void interval_test_restore_cache_hooks();
    void interval_test_change_cache_owner();
    void interval_test_restore_cache_owner();
    int interval_test_try_select(int motion, unsigned slot);
#endif
    // Operand/graph readiness only; original effects and sound still require
    // their actual world providers when the source executor reaches them.
    bool command_ready(uint32_t motion) const noexcept { return command_motions_.contains(motion); }
    const std::set<uint32_t>& command_motions() const noexcept { return command_motions_; }
private:
    friend struct ::MeleeWebActionInterval;
    struct Clip;
    static int select(void*, int, unsigned, FigaTree**, void**, char*, size_t);
    static int transfer(void*, void*, Fighter*, Fighter*, int, unsigned,
                        FigaTree**, void**, char*, size_t);
    int select_from(GameplayActionStore& source, int motion, unsigned slot,
                    FigaTree**, void**, char*, size_t);
    std::shared_ptr<const DatFighterRuntime> runtime_;
    DatFighterAnimationStore store_;
    GameplayActionStorePolicy policy_ = GameplayActionStorePolicy::Legacy;
    bool resident_catalog_ready_ = false;
    std::vector<std::shared_ptr<Clip>> resident_clips_;
    std::shared_ptr<DatCommands> commands_;
    std::set<uint32_t> command_motions_;
    std::shared_ptr<MeleeWebNativeActionRows> rows_;
    // Result demo rows point at complete nested HSD archives in GmRstM*.dat.
    // They must remain a separate source from the ordinary Pl*AJ container.
    std::shared_ptr<const DatArchive> result_motion_;
    // ftData_80085FD4 selects Popo's row whenever a non-demo Nana row has no
    // animation archive. Keep that source identity and its command graph alive.
    std::unique_ptr<GameplayActionStore> nana_popo_fallback_;
    std::map<uint32_t, DatSelectedAction> result_actions_;
    std::shared_ptr<MeleeWebNativeActionRows> result_rows_;
    std::vector<MeleeWebActionRow> result_row_specs_;
    std::array<std::shared_ptr<Clip>, 2> active_;
    std::array<uint32_t, 2> motions_{UINT32_MAX, UINT32_MAX};
    Fighter* fighter_ = nullptr;
    uint64_t binding_generation_ = 0;
    uint32_t binding_slot_ = UINT32_MAX;

    [[nodiscard]] DatSelectedAction select_source_action(uint32_t motion, bool result_domain,
        std::shared_ptr<MeleeWebNativeActionRows>* identity_rows,
        std::shared_ptr<DatCommands>* command_owner);
};

/* Private bounded history anchor for one confirmed PRE. The caller owns
 * source-frame admission, replay-to-END, and receipt copying. A restore enters
 * Correcting in the same incarnation; only the selected sealed endpoint may
 * start the next forward incarnation. At most two PRE restores and 256 events
 * per predicted/corrected tape are retained; overflow fails closed. */
class GameplayActionStoreHistory {
public:
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
    struct CacheStringBankTestLayout {
        // live, before, after, corrected-after, immutable PRE, restore-0, restore-1
        std::uintptr_t symbol_data[7]{};
        std::size_t symbol_bytes[7]{};
        std::size_t capacity[7]{};
        std::size_t catalog_max_symbol_bytes = 0;
    };
    struct EventTapeTestLayout {
        std::size_t predicted_size = 0, corrected_size = 0;
        std::size_t predicted_capacity = 0, corrected_capacity = 0;
    };
#endif
    static std::unique_ptr<GameplayActionStoreHistory> capture();
    ~GameplayActionStoreHistory();
    GameplayActionStoreHistory(const GameplayActionStoreHistory&) = delete;
    GameplayActionStoreHistory& operator=(const GameplayActionStoreHistory&) = delete;

    // Starts the first forward incarnation from PRE, or the next one from the
    // last copied/closed selected endpoint. Returns zero on phase refusal.
    [[nodiscard]] std::uint64_t begin_incarnation() noexcept;
    [[nodiscard]] bool seal_incarnation(std::uint64_t) noexcept;
    [[nodiscard]] bool seal_corrected_incarnation(std::uint64_t) noexcept;

    // Endpoint access is read-only. close_endpoint_batch is the host's
    // acknowledgement that this selected endpoint's complete admitted batch
    // has been copied into its retained receipt before the next begin.
    [[nodiscard]] std::uint32_t endpoint_admit() const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointHeader endpoint_header() const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointBinding endpoint_binding(std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointSlot endpoint_slot(std::uint32_t,
                                                              std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointCache endpoint_cache(std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointEvent endpoint_event(std::uint32_t) const noexcept;
    [[nodiscard]] bool close_endpoint_batch(std::uint64_t) noexcept;

    // Strict no-event PRE readers for initial capture, the armed first
    // incarnation, and the same PRE after successful restore. These are
    // separate roles; generic endpoint admission remains phase-bound to
    // selected recording endpoints.
    [[nodiscard]] std::uint32_t history_captured_pre_admit() const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointHeader history_captured_pre_header() const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointBinding history_captured_pre_binding(
        std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointSlot history_captured_pre_slot(
        std::uint32_t, std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointCache history_captured_pre_cache(
        std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointEvent history_captured_pre_event(
        std::uint32_t) const noexcept;
    [[nodiscard]] std::uint32_t history_armed_pre_admit(
        std::uint64_t incarnation) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointHeader history_armed_pre_header(
        std::uint64_t incarnation) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointBinding history_armed_pre_binding(
        std::uint64_t incarnation, std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointSlot history_armed_pre_slot(
        std::uint64_t incarnation, std::uint32_t, std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointCache history_armed_pre_cache(
        std::uint64_t incarnation, std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointEvent history_armed_pre_event(
        std::uint64_t incarnation, std::uint32_t) const noexcept;
    [[nodiscard]] std::uint32_t history_restored_pre_admit(
        std::uint64_t incarnation, std::uint64_t restore_count) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointHeader history_restored_pre_header(
        std::uint64_t incarnation, std::uint64_t restore_count) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointBinding history_restored_pre_binding(
        std::uint64_t incarnation, std::uint64_t restore_count,
        std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointSlot history_restored_pre_slot(
        std::uint64_t incarnation, std::uint64_t restore_count,
        std::uint32_t, std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointCache history_restored_pre_cache(
        std::uint64_t incarnation, std::uint64_t restore_count,
        std::uint32_t) const noexcept;
    [[nodiscard]] ::MeleeWebActionEndpointEvent history_restored_pre_event(
        std::uint64_t incarnation, std::uint64_t restore_count,
        std::uint32_t) const noexcept;

    // Preparation selects a capture-preallocated, catalog-sized swap bank.
    // The final pure admission is repeated after all aggregate owners prepare;
    // restore_pre then consumes that exact ticket, restoring PRE and entering
    // Correcting for the same incarnation without allocation, deallocation,
    // callbacks, or Fighter-field writes.
    [[nodiscard]] bool prepare_restore(std::uint64_t) noexcept;
    [[nodiscard]] bool admit_restore(std::uint64_t) const noexcept;
    [[nodiscard]] bool restore_pre(std::uint64_t) noexcept;

    [[nodiscard]] std::uint32_t failure_code() const noexcept;
    [[nodiscard]] std::uint32_t active_failure() const noexcept;
    [[nodiscard]] bool retire_admit() const noexcept;
    [[nodiscard]] bool retire_confirmed() noexcept;
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
    [[nodiscard]] bool interval_test_cache_string_layout(
        std::uint32_t cache_index, std::uint32_t entry_index,
        CacheStringBankTestLayout* out) const noexcept;
    [[nodiscard]] bool interval_test_event_tape_layout(EventTapeTestLayout* out) const noexcept;
#endif

    // Cleanup-only detach for both successful and failed runs. Revokes this
    // handle's routing but retains its PRE/displaced leases and owner exclusion
    // until destruction; capture remains refused while this handle is retained.
    void disarm() noexcept;

private:
    explicit GameplayActionStoreHistory(::MeleeWebActionInterval*) noexcept;
    ::MeleeWebActionInterval* capsule_ = nullptr;
};
}
