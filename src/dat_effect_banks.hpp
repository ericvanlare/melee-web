#pragma once
#include "native_dat.hpp"
#include "gameplay_effect_banks.h"
#include <string_view>
namespace melee_web {
// Owns checked particle headers/tables and preserved command/image bytes.
// Caller must detach before destroying the owner. Static EF model entries
// remain a separate readiness boundary and are never filled with placeholders.
class DatEffectBanks {
public:
    DatEffectBanks(std::shared_ptr<const DatArchive>, std::string_view exact_symbol, uint32_t bank,
                   std::vector<NativeDatSourceRegion> = {});
    DatEffectBanks(std::shared_ptr<const DatArchive>, std::string_view command_symbol,
                   std::string_view texture_symbol,uint32_t bank,
                   std::vector<NativeDatSourceRegion> = {});
    ~DatEffectBanks();
    DatEffectBanks(const DatEffectBanks&)=delete;
    DatEffectBanks& operator=(const DatEffectBanks&)=delete;
    MeleeWebEffectBank* bank() const noexcept;
    MeleeWebEffectBank* alias(uint32_t bank);
private:
    NativeDatArena arena_;
    MeleeWebEffectBank* bank_=nullptr;
    std::vector<std::pair<uint32_t,MeleeWebEffectBank*>> aliases_;
};
}
