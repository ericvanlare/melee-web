#pragma once

#include "dat_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace melee_web {

// Hydrates the exact public level table requested when the retail Main menu
// enters. This owns the authored event records; it does not provide the
// separate gameplay services used after selecting an Event mode.
class DatEventMenuData {
public:
    explicit DatEventMenuData(std::shared_ptr<const DatArchive>);
    ~DatEventMenuData();
    DatEventMenuData(const DatEventMenuData&) = delete;
    DatEventMenuData& operator=(const DatEventMenuData&) = delete;

    [[nodiscard]] void* descriptor() const noexcept;
    [[nodiscard]] std::size_t level_count() const noexcept;
    [[nodiscard]] std::span<const std::uint8_t> source_bytes() const noexcept;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
