#pragma once
#include "native_dat.h"
#include "dat_archive.hpp"
#include <memory>
#include <vector>
namespace melee_web {
struct NativeDatSourceRegion {
    std::uint32_t source_data_address = 0;
    std::shared_ptr<const DatArchive> archive;
};

// GALE01r2's common-item DAT is loaded at this retail data-section address.
// Source probes tie 0x80A8812A to ItCo.usd's exact palette bytes at data offset
// 0x264A0A; callers map the owned archive through this generic address window.
inline constexpr std::uint32_t gale01r2_itco_data_address = 0x80823720;

class NativeDatArena {
public:
    explicit NativeDatArena(std::shared_ptr<const DatArchive>);
    NativeDatArena(std::shared_ptr<const DatArchive>,
                   std::vector<NativeDatSourceRegion>);
    ~NativeDatArena();
    NativeDatArena(const NativeDatArena&) = delete;
    NativeDatArena& operator=(const NativeDatArena&) = delete;
    const MeleeWebNativeDat* reader() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
