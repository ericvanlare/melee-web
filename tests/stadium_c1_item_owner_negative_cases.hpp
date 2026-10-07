#pragma once

#include <array>

namespace stadium_c1_item_owner {

// Exercises the exact diagnostic guards and production public-data/color
// decoders with synthetic DATs. The caller supplies the script rows so the
// retained-context trace can run the capacity guard against parsed ALDYakuAll
// pointers while still using an explicitly synthetic capacity of one.
void run_synthetic_negative_cases(const std::array<void*, 8>& scripts);

} // namespace stadium_c1_item_owner
