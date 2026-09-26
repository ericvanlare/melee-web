#include "dat_stage_yaku.hpp"

#include <utility>

namespace melee_web {
namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw DatError(message);
}
}

DatStageYaku::DatStageYaku(std::shared_ptr<const DatArchive> archive,
                           std::uint32_t root)
    : archive_(std::move(archive))
{
    require(bool(archive_), "Stage Yaku requires its owned source archive");
    require((root & 3U) == 0, "ALDYakuAll table is unaligned");

    // ItemStateArray has eight source descriptors. Ground starts at index one
    // and follows ALDYakuAll until its null sentinel, so accept at most those
    // eight entries plus the terminating pointer.
    // The source loop begins at row one; row zero is a reserved slot and its
    // null value must not terminate the table walk.
    if (const auto first = archive_->pointer(root, 1))
        scripts_[0] = commands_.decode(*archive_, *first);

    bool terminated = false;
    for (std::uint32_t index = 1; index <= scripts_.size(); ++index) {
        const std::uint32_t slot = root + index * 4;
        const auto entry = archive_->pointer(slot, 1);
        if (!entry) {
            terminated = true;
            break;
        }
        require(index < scripts_.size(),
                "ALDYakuAll exceeds the Random article's eight state rows");
        scripts_[index] = commands_.decode(*archive_, *entry);
    }
    require(terminated, "ALDYakuAll lacks a bounded null terminator");
}

} // namespace melee_web
