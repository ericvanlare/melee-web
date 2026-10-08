#pragma once
#include "dat_native_animation.hpp"
#include "dat_stage_yaku.hpp"
#include "native_dat.hpp"
#include "gameplay_stage_map.h"
#include "gameplay_archive_sections.h"
#include "gameplay_stage_profile.h"
#include <cstdint>
#include <span>
#include <string_view>
namespace melee_web {
struct DatNativeMapExternalReference {
    uint32_t entry_index;
    // Pointer-field byte offset within the authored 0x34-byte map entry.
    uint32_t field_offset;
    std::string_view symbol;
};
enum class DatNativeMapFlagKind { LocalMaterial, ExternalNull, Null };
struct DatNativeMapFlagExpectation {
    uint32_t index;
    DatNativeMapFlagKind kind;
    uint32_t target_offset;
    std::string_view symbol;
};

// A map owner is intentionally weaker than a complete stage profile. Callers
// must describe the authored entry/animation counts and exactly which rows are
// resident in this archive; every other row must retain a checked external
// joint identity under ResolveNull.
struct DatNativeMapContract {
    uint32_t entry_count;
    std::span<const uint8_t> animation_consumer_counts;
    std::span<const uint32_t> resident_entry_ids;
    std::span<const DatNativeMapExternalReference> external_references;
    // Explicit per-row flag consumers preserve map-only auxiliaries while
    // allowing the complete-stage contract to retain its marker-row behavior.
    std::span<const uint32_t> animation_flag_entry_ids;
    // One source-ordered expectation per authored flagged-object table slot.
    std::span<const DatNativeMapFlagExpectation> flagged_objects;
};

// Owns the vectors referenced by a map-contract view. The compatibility policy
// reproduces the current complete-stage contract; authored profiles provide
// every ownership identity explicitly.
struct DatNativeStageMapContractData {
    uint32_t entry_count = 0;
    std::vector<uint8_t> animation_consumer_counts;
    std::vector<uint32_t> resident_entry_ids;
    std::vector<DatNativeMapExternalReference> external_references;
    std::vector<uint32_t> animation_flag_entry_ids;
    std::vector<DatNativeMapFlagExpectation> flagged_objects;
    DatNativeMapContract view() const noexcept;
};

class DatStage;
DatNativeStageMapContractData dat_native_stage_map_contract_from_profile(
    const MeleeWebStageProfile&, const DatArchive&, const DatStage&);

// Owns only the checked source map graph. It does not publish the map or
// imply that a complete stage, stage callbacks, or world services exist.
class DatNativeMap {
public:
    DatNativeMap(std::shared_ptr<const DatArchive>, const DatNativeMapContract&);
    ~DatNativeMap();
    DatNativeMap(const DatNativeMap&)=delete;
    DatNativeMap& operator=(const DatNativeMap&)=delete;
    void* map_head()const noexcept;
    // Borrows the canonical IMAGE already present in this map's texture graph.
    // Missing graph membership throws; validity ends with this owner's lifetime.
    void* image_descriptor(uint32_t source_offset)const;
    /* Return a borrowed checked native MapCollData view owned by this
     * DatNativeMap. Decoded lazily because marker-only maps need no collision
     * archive; the pointer is valid only through this owner's lifetime. */
    void* collision();
    std::span<const uint32_t> source_light_counts()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

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
    void* random_item_scripts()const noexcept;
    void set_particle_roots(void* commands,void* textures);
    void set_quake_model(void* descriptor);
    const std::vector<MeleeWebMapLightOverride>& light_overrides()const noexcept;
    /* Exact null-terminated LightList row counts, in authored map-entry order. */
    std::span<const uint32_t> source_light_counts()const noexcept;
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
