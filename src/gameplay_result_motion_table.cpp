#include "gameplay_compat.h"
#include "gameplay_result_motion_table.hpp"

#include <melee/ft/forward.h>

namespace melee_web {

ResultMotionArchiveSpec result_motion_archive_spec(std::uint32_t fighter_kind)
{
    switch (fighter_kind) {
    case FTKIND_MARIO:   return {"GmRstMMr.dat", "ftDemoResultMotionFileMario"};
    case FTKIND_DRMARIO: return {"GmRstMDr.dat", "ftDemoResultMotionFileDrmario"};
    case FTKIND_FOX:     return {"GmRstMFx.dat", "ftDemoResultMotionFileFox"};
    case FTKIND_FALCO:   return {"GmRstMFc.dat", "ftDemoResultMotionFileFalco"};
    case FTKIND_MARS:    return {"GmRstMMs.dat", "ftDemoResultMotionFileMars"};
    case FTKIND_EMBLEM:  return {"GmRstMFe.dat", "ftDemoResultMotionFileEmblem"};
    case FTKIND_LINK:    return {"GmRstMLk.dat", "ftDemoResultMotionFileLink"};
    case FTKIND_CLINK:   return {"GmRstMCl.dat", "ftDemoResultMotionFileClink"};
    default:             return {};
    }
}

} // namespace melee_web