#pragma once
#include "dat_archive.hpp"
#include "dat_item_registry.h"
#include <array>
namespace melee_web {
/* Validates the exact original character-item registry table. Article offsets
 * remain explicit references for the native typed graph hydrator, never cast
 * to host pointers or replaced with empty records. */
class DatItemRegistry {
public:
    explicit DatItemRegistry(const DatArchive&);
    uint32_t root_offset=0, table_offset=0;
    std::array<std::optional<uint32_t>,MELEE_WEB_ITEM_REGISTRY_COUNT> articles;
};
}
