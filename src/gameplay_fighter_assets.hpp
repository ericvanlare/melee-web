#pragma once
#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.hpp"
#include "native_dat.hpp"
#include "dat_material_animation.hpp"
namespace melee_web {
// Full ownership chain for one published costume. Construct after original
// common initialization; destroy all source Fighter GObjs before close/destruction.
// Up to six Fighters retain independent animation slots and command cursors.
class GameplayFighterAssets {
public:
    GameplayFighterAssets(std::shared_ptr<const DatArchive> fighter,
        std::shared_ptr<const DatArchive> costume,std::span<const uint8_t> animation,
        const FighterCostume& identity);
    ~GameplayFighterAssets();
    GameplayFighterAssets(const GameplayFighterAssets&)=delete;
    GameplayFighterAssets& operator=(const GameplayFighterAssets&)=delete;
    void close();
    uint32_t live_fighters() const noexcept;
    uint32_t unresolved_fields() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
