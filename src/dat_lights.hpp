#pragma once
#include "dat_archive.hpp"
#include "gameplay_stage_context.h"
namespace melee_web {
// Looks up one source identity using the original first-match ordering. Every
// examined 8-byte record is bounded; a missing identity rejects if the declared
// source count would cross into a different referenced allocation. Does not
// claim the entire map_head override table has been decoded.
std::optional<uint8_t> read_dat_light_override(const DatArchive&, uint32_t light_offset);
class DatLights {
public:
    explicit DatLights(const DatArchive&, const std::string& symbol = "map_plit");
    uint32_t root_offset = 0;
    std::vector<MeleeWebStageLightDesc> lights;
};
}
