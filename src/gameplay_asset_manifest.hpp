#pragma once

#include "gameplay_menu_host.h"
#include "gameplay_menu.h"

#include <string>
#include <vector>

namespace melee_web {

// Logical RuntimeFiles names for the complete CSS/SSS owner. The result is
// deterministic, deduplicated, and contains generated inputs explicitly.
[[nodiscard]] std::vector<std::string> menu_asset_names();
// CSS OnExit can preload any selectable fighter's voice group. This registry
// therefore covers every admitted fighter, even when it was not in the last match.
[[nodiscard]] std::vector<std::string> menu_audio_bank_names();

// Logical RuntimeFiles names for one checked source VS selection. This is a
// read-only descriptor: it does not select music, consume RNG, or write the
// selection payload. Unsupported selections throw DatError.
[[nodiscard]] std::vector<std::string>
match_asset_names(const MeleeWebMenuMatchSelection& selection);

#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
// Private diagnostic preparation only. This descriptor does not admit
// Stadium to the ordinary match registry or construct a match owner.
[[nodiscard]] std::vector<std::string>
stadium_c1a_asset_names(const MeleeWebMenuMatchSelection& selection);
#endif

// Complete menu plus selected-fighter/stage assets needed to enter one
// source-selected Opening VS state. The preview is read-only source data; it
// is not a synthetic StartMeleeData payload.
[[nodiscard]] std::vector<std::string>
opening_match_asset_names(const MeleeWebOpeningPreview& preview);

// Complete menu/audio scope plus the exact authored THP stream selected by
// one Opening-mode movie state. VS states require opening_match_asset_names.
[[nodiscard]] std::vector<std::string>
opening_state_asset_names(unsigned state_id);

// Logical RuntimeFiles names for the original Results scene that follows one
// checked source VS selection. It repeats the match compatibility checks and
// adds the authored GmRst roots, per-fighter result-motion archives and the
// seven authored victory themes. It is a read-only descriptor.
[[nodiscard]] std::vector<std::string>
results_asset_names(const MeleeWebMenuMatchSelection& selection);

// Logical RuntimeFiles names for the original Prize (unlock notification)
// scene. Prize runs its own source world, so this descriptor is complete on
// its own: the authored IfPrize/SdPrize roots, the trophy and card roots, the
// menu audio banks and the three authored s_info voice streams.
[[nodiscard]] std::vector<std::string> prize_asset_names();

} // namespace melee_web
