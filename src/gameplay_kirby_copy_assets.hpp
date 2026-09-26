#pragma once

#include "gameplay_world.hpp"

#include <memory>
#include <string>
#include <vector>

struct MeleeWebMenuMatchSelection;

namespace melee_web {

struct KirbyCopyArchiveRequirement {
    std::string filename;
    std::string symbol;
    unsigned fighter_kind = 0;
    bool costume_root = false;
};

struct KirbyCopyEffectRequirement {
    std::string filename;
    std::string symbol;
    unsigned effect_bank = 0;
};

// Derived only from ftkirbydata.c's source tables. The input kinds are the
// actual internal FighterKinds that Player_80031DC8 visits for this selection.
std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const std::vector<unsigned>& fighter_kinds);
std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const MeleeWebMenuMatchSelection&);
std::vector<KirbyCopyEffectRequirement>
kirby_copy_effect_requirements(const MeleeWebMenuMatchSelection&);

bool selection_uses_kirby(const MeleeWebMenuMatchSelection&);

// Owns mutable, source-relocated HSD archives and their registered public
// roots for the lifetime of Kirby's copy callbacks and any copied move.
class GameplayKirbyCopyAssets {
public:
    GameplayKirbyCopyAssets(const RuntimeFiles&,
                            const MeleeWebMenuMatchSelection&);
    ~GameplayKirbyCopyAssets();
    GameplayKirbyCopyAssets(const GameplayKirbyCopyAssets&) = delete;
    GameplayKirbyCopyAssets& operator=(const GameplayKirbyCopyAssets&) = delete;

    void activate();
    void close();

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
