#pragma once
#include "dat_effect_banks.hpp"
namespace melee_web {
// Owns actual EF table+StaticModelDesc/JObj/AnimJoint/MatAnimJoint descriptors.
// Registry publication is separate. Keep this owner alive until every source
// effect instance, archive symbol registration and bank publication is removed.
class DatEffectEntries {
public:
    DatEffectEntries(std::shared_ptr<const DatArchive>,std::string_view exact_symbol,
                     uint32_t bank,uint32_t source_entry_count,bool particle_descriptors=false,
                     std::vector<NativeDatSourceRegion> = {});
    ~DatEffectEntries();
    DatEffectEntries(const DatEffectEntries&)=delete;
    DatEffectEntries& operator=(const DatEffectEntries&)=delete;
    // Registers the original source filename/symbol and executes efAsync_LoadSync.
    // Detach after all effect instances are removed, before source shutdown.
    bool load(char* error,size_t error_size);
    // Publish checked assets before an original scene owns efLib_Init and
    // efAsync_LoadSync. Require the actual source load after scene entry.
    bool publish_for_source(char* error,size_t error_size);
    bool verify_source_load(char* error,size_t error_size);
    bool detach(char* error,size_t error_size);
    bool entries_ready()const noexcept;
    void* table()const noexcept;
    uint32_t entry_count()const noexcept;
    MeleeWebEffectBank* bank()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
