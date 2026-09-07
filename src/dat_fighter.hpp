#pragma once

#include "dat_archive.hpp"
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace melee_web {

struct DatFighterVariant {
    uint32_t descriptor_offset = 0;
    std::vector<uint8_t> dobj_indices;
};
struct DatFighterGroup {
    std::vector<DatFighterVariant> variants;
};
struct DatFighterRepresentation {
    uint32_t descriptor_offset = 0;
    std::vector<DatFighterGroup> groups;
};

// Checked ftData +8 -> FtPartsDesc visibility metadata. Costume/model identity
// must be supplied from original source costume tables: DAT files do not carry
// that binding. This object copies metadata and does not retain archive spans.
class DatFighterParts {
public:
    DatFighterParts(const DatArchive& archive, const std::string& ftdata_symbol,
                    uint32_t costume_index);
    uint32_t root_offset = 0, descriptor_offset = 0, costume_index = 0, model_count = 0;
    // 0 normal; 1 shadow/reflection; 2 separate metal DObj list; 3 extra main-list
    // representation. Original runtime slot4 is not supplied by FtPartsDesc.
    std::array<std::optional<DatFighterRepresentation>, 4> representations;

    // Viewer selection of the sole normal variant in each group. This is not
    // a substitute for fighter initialization/action visibility commands.
    // Reject ambiguous groups until an explicit selector is implemented.
    // Unlisted DObjs retain HSD's default visibility; alternate representations
    // are hidden. Returned indices are checked against the model's preorder
    // DObj count, matching ftParts_80074194 rather than PObj/mesh numbering.
    [[nodiscard]] std::vector<uint32_t> normal_dobj_indices(uint32_t dobj_count) const;
};

} // namespace melee_web
