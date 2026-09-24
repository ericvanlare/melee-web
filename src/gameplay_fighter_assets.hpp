#pragma once
#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.hpp"
#include "native_dat.hpp"
#include "dat_material_animation.hpp"
namespace melee_web {
// Full ownership chain for one fighter kind and its published costumes. Construct after original
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
    void add_costume(std::shared_ptr<const DatArchive>,const FighterCostume&);
    // Install the source GmRstM* result archive before creating result demo
    // Fighters. Ordinary match action stores remain on Pl*AJ data.
    void set_result_demo_archive(std::shared_ptr<const DatArchive>);
    void close();
    uint32_t live_fighters() const noexcept;
    uint32_t unresolved_fields() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
