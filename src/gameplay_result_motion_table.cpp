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
    case FTKIND_CAPTAIN: return {"GmRstMCa.dat", "ftDemoResultMotionFileCaptain"};
    case FTKIND_DONKEY:  return {"GmRstMDk.dat", "ftDemoResultMotionFileDonkey"};
    case FTKIND_GANON:   return {"GmRstMGn.dat", "ftDemoResultMotionFileGanon"};
    case FTKIND_KOOPA:   return {"GmRstMKp.dat", "ftDemoResultMotionFileKoopa"};
    case FTKIND_LUIGI:   return {"GmRstMLg.dat", "ftDemoResultMotionFileLuigi"};
    case FTKIND_MEWTWO:  return {"GmRstMMt.dat", "ftDemoResultMotionFileMewtwo"};
    case FTKIND_PIKACHU: return {"GmRstMPk.dat", "ftDemoResultMotionFilePikachu"};
    case FTKIND_PICHU:   return {"GmRstMPc.dat", "ftDemoResultMotionFilePichu"};
    case FTKIND_PURIN:   return {"GmRstMPr.dat", "ftDemoResultMotionFilePurin"};
    default:             return {};
    }
}

} // namespace melee_web
