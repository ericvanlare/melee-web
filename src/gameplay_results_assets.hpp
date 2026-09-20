#pragma once

#include "dat_animation.hpp"
#include "dat_scene.hpp"
#include "dat_sis.hpp"
#include "fighter_binding.hpp"
#include "gameplay_world.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace melee_web {

// One result action is a complete nested HSD archive inside GmRstM*.dat.  It
// is deliberately retained separately from the ordinary Pl*AJ animation
// container: ftData_80085B98 installs these result bytes as a demo archive,
// and ftData_80085CD8/ftData_80085E50 parse the nested archive at selection.
struct GameplayResultDemoClip {
    std::uint32_t motion_id;
    std::string symbol;
    std::shared_ptr<const DatArchive> archive;
    std::shared_ptr<const DatAnimation> animation;
};

// Owns the original Results scene surface and the authored result demo clips
// for the selected source fighters. The source scene loader owns GmRst/SdRst
// and the result fighter loader resolves the gm_1601 GmRstM* root for each
// selected fighter. This class publishes only typed SceneDesc/SIS data and
// the real nested FigaTree clips; source scene integration remains the
// caller's responsibility.
class GameplayResultsAssets {
public:
    // These single-identity constructors are retained for existing focused
    // callers. The multi-identity constructors are used by Results sessions
    // that have more than one source fighter in the match data.
    GameplayResultsAssets(const RuntimeFiles&, const FighterCostume&);
    GameplayResultsAssets(const RuntimeFiles&, RuntimeArchiveCache&,
                          const FighterCostume&);
    GameplayResultsAssets(const RuntimeFiles&, std::span<const FighterCostume>);
    GameplayResultsAssets(const RuntimeFiles&, RuntimeArchiveCache&,
                          std::span<const FighterCostume>);
    ~GameplayResultsAssets();
    GameplayResultsAssets(const GameplayResultsAssets&) = delete;
    GameplayResultsAssets& operator=(const GameplayResultsAssets&) = delete;

    [[nodiscard]] const DatScene& panel_scene() const;
    [[nodiscard]] const DatScene& film_scene() const;
    [[nodiscard]] const DatSis& sis() const;
    [[nodiscard]] std::shared_ptr<const DatArchive>
    result_motion_archive() const noexcept;
    [[nodiscard]] std::span<const GameplayResultDemoClip> demo_clips() const noexcept;
    [[nodiscard]] const GameplayResultDemoClip& demo_clip(std::uint32_t motion_id) const;
    [[nodiscard]] std::shared_ptr<const DatArchive>
    result_motion_archive(std::uint32_t fighter_kind) const;
    [[nodiscard]] std::span<const GameplayResultDemoClip>
    demo_clips(std::uint32_t fighter_kind) const;
    [[nodiscard]] const GameplayResultDemoClip&
    demo_clip(std::uint32_t fighter_kind, std::uint32_t motion_id) const;

    // Must run after source GObjs and archive handles have been released.
    void close();
    void verify() const;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
