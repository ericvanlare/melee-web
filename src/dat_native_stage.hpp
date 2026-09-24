#pragma once
#include "dat_native_animation.hpp"
#include "native_dat.hpp"
#include "gameplay_stage_map.h"
#include "gameplay_archive_sections.h"
namespace melee_web {
// Owns native source-stage map descriptors and every source-selected
// animation. Does not publish them or imply particle execution is initialized.
class DatNativeStage {
public:
    /* Retain the one-argument form for stage inspection tools; it selects the
     * existing Final Destination profile. Match startup should pass its
     * selected StKind to the profile-aware overload. */
    explicit DatNativeStage(std::shared_ptr<const DatArchive>);
    DatNativeStage(std::shared_ptr<const DatArchive>, int stage_kind);
    ~DatNativeStage();
    DatNativeStage(const DatNativeStage&)=delete;
    DatNativeStage& operator=(const DatNativeStage&)=delete;
    void* map_head()const noexcept;
    void* yakumono()const noexcept;
    const std::vector<MeleeWebMapLightOverride>& light_overrides()const noexcept;
    const std::vector<DatParticleEvent>& particle_events()const noexcept;
    const std::vector<MeleeWebArchiveSymbol>& public_symbols()const noexcept;
    /* Global map_plit uses the same checked HSD animation grammar as map
     * light descriptors. Borrows this owner's storage through LObj teardown. */
    void* light_animation_table(uint32_t source_offset);
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
