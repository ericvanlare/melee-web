#pragma once
#include "dat_native_animation.hpp"
#include "native_dat.hpp"
#include "gameplay_stage_map.h"
namespace melee_web {
// Owns native Final Destination map descriptors and every source-selected
// animation. Does not publish them or imply particle execution is initialized.
class DatNativeStage {
public:
    explicit DatNativeStage(std::shared_ptr<const DatArchive>);
    ~DatNativeStage();
    DatNativeStage(const DatNativeStage&)=delete;
    DatNativeStage& operator=(const DatNativeStage&)=delete;
    void* map_head()const noexcept;
    void* yakumono()const noexcept;
    const std::vector<MeleeWebMapLightOverride>& light_overrides()const noexcept;
    const std::vector<DatParticleEvent>& particle_events()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
