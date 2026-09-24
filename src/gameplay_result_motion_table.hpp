#pragma once

#include <cstdint>
#include <string_view>

namespace melee_web {

// Authored lbl_803D53A8 in gm_1601.c. Each admitted Results fighter kind maps
// to its GmRstM result-motion archive and the source public root that archive
// publishes. The table is shared by the Results asset owner and the scene
// asset descriptor so both request and resolve the same authored names; an
// empty result means the kind has no admitted Results demo archive.
struct ResultMotionArchiveSpec {
    std::string_view archive;
    std::string_view root;
};

[[nodiscard]] ResultMotionArchiveSpec
result_motion_archive_spec(std::uint32_t fighter_kind);

} // namespace melee_web