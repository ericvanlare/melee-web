#pragma once

#include "dat_archive.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace stadium_c1_item_owner {

template <class RuntimeFiles>
void require_retained_inputs(const RuntimeFiles& files)
{
    if (!files.contains("ItCo.usd") || !files.contains("GrPs.usd"))
        throw melee_web::DatError(
            "C1 item-state preflight requires retained ItCo.usd and GrPs.usd inputs");
}

inline void require_random_article(const std::optional<std::uint32_t>& source_root,
                                  const void* registered_article)
{
    if (!source_root)
        throw melee_web::DatError(
            "Original Random Pokémon Article root is absent from ItCo.usd");
    if (!registered_article)
        throw melee_web::DatError(
            "Original Random Pokémon Article registration is absent");
}

template <class ScriptRows>
void require_state_capacity(const ScriptRows& scripts, std::size_t state_count)
{
    for (std::size_t row = 1; row < scripts.size(); ++row) {
        if (scripts[row] && row >= state_count)
            throw melee_web::DatError(
                "ALDYakuAll consumer exceeds the authored Random Article state table");
    }
}

inline std::size_t checked_color_row_count(const melee_web::DatArchive& archive,
                                           std::uint32_t root)
{
    const auto extent = archive.next_target_offset(root) - root;
    if (!extent || extent % 8 || extent / 8 > 256)
        throw melee_web::DatError("Original ItCo color-animation extent is invalid");
    return extent / 8;
}

} // namespace stadium_c1_item_owner
