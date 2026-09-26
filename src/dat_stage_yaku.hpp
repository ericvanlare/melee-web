#pragma once

#include "dat_archive.hpp"
#include "dat_item_commands.hpp"
#include <array>
#include <cstdint>
#include <memory>

namespace melee_web {

// Owns the source ALDYakuAll pointer table and each checked native command
// graph that Ground_801C0800 installs into the Random article's state rows.
class DatStageYaku {
public:
    DatStageYaku(std::shared_ptr<const DatArchive>, std::uint32_t root);
    DatStageYaku(const DatStageYaku&) = delete;
    DatStageYaku& operator=(const DatStageYaku&) = delete;

    void* native_data() noexcept { return scripts_.data(); }
    const std::array<void*, 8>& scripts() const noexcept { return scripts_; }

private:
    std::shared_ptr<const DatArchive> archive_;
    DatItemCommands commands_;
    std::array<void*, 8> scripts_{};
};

} // namespace melee_web
