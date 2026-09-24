#pragma once
#include "dat_commands.hpp"
#include <array>
#include <map>
#include <set>
namespace melee_web {
class GameplayActionStore {
public:
    GameplayActionStore(std::shared_ptr<const DatArchive>, const FighterCostume&,
                        std::span<const uint8_t> container,
                        std::shared_ptr<const DatArchive> result_motion = {});
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
    // Operand/graph readiness only; original effects and sound still require
    // their actual world providers when the source executor reaches them.
    bool command_ready(uint32_t motion) const noexcept { return command_motions_.contains(motion); }
    const std::set<uint32_t>& command_motions() const noexcept { return command_motions_; }
private:
    struct Clip;
    static int select(void*, int, unsigned, FigaTree**, void**, char*, size_t);
    static int transfer(void*, void*, Fighter*, Fighter*, int, unsigned,
                        FigaTree**, void**, char*, size_t);
    int select_from(GameplayActionStore& source, int motion, unsigned slot,
                    FigaTree**, void**, char*, size_t);
    std::shared_ptr<const DatFighterRuntime> runtime_;
    DatFighterAnimationStore store_;
    std::shared_ptr<DatCommands> commands_;
    std::set<uint32_t> command_motions_;
    std::shared_ptr<MeleeWebNativeActionRows> rows_;
    // Result demo rows point at complete nested HSD archives in GmRstM*.dat.
    // They must remain a separate source from the ordinary Pl*AJ container.
    std::shared_ptr<const DatArchive> result_motion_;
    std::map<uint32_t, DatSelectedAction> result_actions_;
    std::shared_ptr<MeleeWebNativeActionRows> result_rows_;
    std::vector<MeleeWebActionRow> result_row_specs_;
    std::array<std::shared_ptr<Clip>, 2> active_;
    std::array<uint32_t, 2> motions_{UINT32_MAX, UINT32_MAX};
    Fighter* fighter_ = nullptr;

    [[nodiscard]] DatSelectedAction select_source_action(uint32_t motion, bool result_domain);
};
}
