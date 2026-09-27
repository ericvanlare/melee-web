#pragma once

#include "dat_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace melee_web {

// Typed owner for lbAudioAx's authored language/group lookup table. The
// source uses its sentinel-terminated int rows to preserve the selected sound
// effect candidates and their original order.
class DatAudioLoadData {
public:
    explicit DatAudioLoadData(std::shared_ptr<const DatArchive>);
    ~DatAudioLoadData();
    DatAudioLoadData(const DatAudioLoadData&) = delete;
    DatAudioLoadData& operator=(const DatAudioLoadData&) = delete;

    [[nodiscard]] void* descriptor() noexcept;
    [[nodiscard]] std::size_t group_count() const noexcept;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
