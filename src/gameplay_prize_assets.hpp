#pragma once

#include "dat_menu_support.hpp"
#include "dat_scene.hpp"
#include "dat_sis.hpp"
#include "dat_trophy_data.hpp"
#include "gameplay_world.hpp"

#include <cstdint>
#include <memory>

namespace melee_web {

// Owns the exact archive roots consumed by the original Prize mode.  The
// source callbacks resolve these names through the heap archive registry:
// IfPrize.usd/ScInfPrize_scene_data, SdPrize.usd/SIS_PrizeData, the seven
// TyDatai roots used by Toy_803124BC, and the two card roots used by
// lbCardGame_SetupArchive.  Construct this owner after an SDK world starts;
// close it only after every Prize scene and mode GObj has been destroyed.
class GameplayPrizeAssets {
public:
    explicit GameplayPrizeAssets(const RuntimeFiles&);
    GameplayPrizeAssets(const RuntimeFiles&, RuntimeArchiveCache&);
    ~GameplayPrizeAssets();
    GameplayPrizeAssets(const GameplayPrizeAssets&) = delete;
    GameplayPrizeAssets& operator=(const GameplayPrizeAssets&) = delete;

    [[nodiscard]] const DatScene& scene() const;
    [[nodiscard]] const DatSis& sis() const;
    [[nodiscard]] const DatTrophyData& trophy_data() const;
    [[nodiscard]] const DatMenuSupport& card_icons() const;
    [[nodiscard]] const DatMenuSupport& card_scene() const;

    // Source archive handles and HSD consumers must be gone before close().
    void close();
    void verify() const;
    void verify_immutable_archives() const;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
