#pragma once
#include "dat_commands.hpp"
#include <array>
namespace melee_web {
class GameplayActionStore {
public:
    GameplayActionStore(std::shared_ptr<const DatArchive>, const FighterCostume&,
                        std::span<const uint8_t> container);
    ~GameplayActionStore();
    GameplayActionStore(const GameplayActionStore&) = delete;
    GameplayActionStore& operator=(const GameplayActionStore&) = delete;
    // Unbind before the Fighter is freed; the store retains every published stream.
    void bind(Fighter*);
    void unbind();
    void* action_rows() const;
    void* blend_rows() const;
    void* wait_choices() const;
    const DatFighterRuntime& runtime() const noexcept { return *runtime_; }
    uint32_t selected_motion(unsigned slot) const;
private:
    struct Clip;
    static int select(void*, int, unsigned, FigaTree**, void**, char*, size_t);
    std::shared_ptr<const DatFighterRuntime> runtime_;
    DatFighterAnimationStore store_;
    std::unique_ptr<DatCommands> commands_;
    std::unique_ptr<MeleeWebNativeActionRows, decltype(&melee_web_action_rows_destroy)> rows_;
    std::array<std::shared_ptr<Clip>, 2> active_;
    std::array<uint32_t, 2> motions_{UINT32_MAX, UINT32_MAX};
    Fighter* fighter_ = nullptr;
};
}
