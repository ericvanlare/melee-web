#pragma once
#include "dat_native_stage.hpp"
#include <array>
#include <memory>
#include <span>
#include <vector>
namespace melee_web {
// A typed archive-owner prerequisite, not a source transformation/retirement
// controller. Only map_head is hydrated; all other real public names stay NULL.
class StadiumFormAssets {
public:
    using Archives = std::array<std::shared_ptr<const DatArchive>,4>;
    StadiumFormAssets(Archives, std::span<const MeleeWebMapLightOverride> base_overrides,
                      std::span<const uint32_t> base_light_counts);
    ~StadiumFormAssets();
    StadiumFormAssets(const StadiumFormAssets&)=delete;
    StadiumFormAssets& operator=(const StadiumFormAssets&)=delete;
    const std::vector<MeleeWebArchiveSymbol>& public_symbols() const noexcept;
    const std::vector<MeleeWebMapLightOverride>& light_overrides() const noexcept;
    std::span<const uint32_t> source_light_counts() const noexcept;
    const DatNativeMap& map(unsigned form_ordinal) const;
    // Explicit publication. One atomic scope registration retains the existing
    // global symbol budget/refusals. Does not open a source archive handle.
    void publish();
    // Only after source handles and Ground users have retired. Refusal retains
    // every owner; no close/destruction or fallback cleanup is performed.
    void close();
private:
    Archives archives_;
    std::array<std::unique_ptr<DatNativeMap>,4> maps_;
    std::vector<MeleeWebArchiveSymbol> symbols_;
    std::vector<MeleeWebMapLightOverride> overrides_;
    std::array<uint32_t,10> light_counts_{};
    MeleeWebArchiveSections* scope_=nullptr;
};
DatNativeStageMapContractData stadium_form_map_contract(
    const DatArchive&, unsigned form_ordinal);
}
