#pragma once
#include "gameplay_action_store.h"
#include "dat_commands.hpp"
#include <array>
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
}
