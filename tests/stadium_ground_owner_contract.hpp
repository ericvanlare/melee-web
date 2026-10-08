#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace melee_web::test::stadium_ground {

struct SourceMarkerBinding {
    uint16_t joint_index = 0;
    uint16_t marker_id = 0;
};

struct SourceMarkerRow {
    uint32_t root_offset = 0;
    std::vector<SourceMarkerBinding> bindings;
};

struct SourceMarkerSelection {
    size_t authored_pair_count = 0;
    size_t matched_row_count = 0;
    std::vector<SourceMarkerBinding> map_bindings;
};

/* Ground_801C34AC selects the first marker-reference row by the exact
 * authored root pointer. Callers validate every row and its joint indices
 * against that row's resolved graph before applying this filter. */
inline SourceMarkerSelection select_source_markers_for_root(
    std::span<const SourceMarkerRow> rows, uint32_t selected_root)
{
    SourceMarkerSelection result;
    bool selected = false;
    for (const auto& row : rows) {
        result.authored_pair_count += row.bindings.size();
        if (selected || row.root_offset != selected_root) continue;
        selected = true;
        ++result.matched_row_count;
        result.map_bindings.insert(result.map_bindings.end(),
                                   row.bindings.begin(), row.bindings.end());
    }
    return result;
}

} // namespace melee_web::test::stadium_ground
