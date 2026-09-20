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

} // namespace melee_web
