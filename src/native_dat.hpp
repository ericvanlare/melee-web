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
    // Checkpoints cover only this arena's decoded descriptor allocations. They
    // do not cover other DAT wrappers, HSD runtime objects, or a rendered world.
    // The owner and allocation topology must still be identical at restoration.
    class Checkpoint {
    public:
        ~Checkpoint();
        Checkpoint(Checkpoint&&) noexcept;
        Checkpoint& operator=(Checkpoint&&) noexcept;
        Checkpoint(const Checkpoint&) = delete;
        Checkpoint& operator=(const Checkpoint&) = delete;
        std::size_t bytes() const noexcept;
    private:
        friend class NativeDatArena;
        struct Data;
        explicit Checkpoint(std::unique_ptr<Data>);
        std::unique_ptr<Data> data_;
    };

    explicit NativeDatArena(std::shared_ptr<const DatArchive>);
    NativeDatArena(std::shared_ptr<const DatArchive>,
                   std::vector<NativeDatSourceRegion>);
    ~NativeDatArena();
    NativeDatArena(const NativeDatArena&) = delete;
    NativeDatArena& operator=(const NativeDatArena&) = delete;
    const MeleeWebNativeDat* reader() const noexcept;
    [[nodiscard]] Checkpoint checkpoint() const;
    [[nodiscard]] bool can_restore(const Checkpoint&) const noexcept;
    // Validates every region before writing any bytes. No allocation occurs on
    // the commit path; a topology/owner mismatch leaves all regions unchanged.
    [[nodiscard]] bool restore(const Checkpoint&) noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
