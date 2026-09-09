#pragma once

#include "dat_item_article.hpp"
#include "gameplay_stage_items.h"
#include "native_dat.hpp"
#include <memory>
#include <span>
#include <vector>

namespace melee_web {

/* Owns a stage archive's null-terminated GroundItemData list and every native
 * Article graph it names. Registration into source globals is a separate,
 * explicitly scoped C boundary. */
class DatStageItems {
public:
    DatStageItems(std::shared_ptr<const DatArchive>, const char* symbol = "itemdata");
    ~DatStageItems();
    DatStageItems(const DatStageItems&) = delete;
    DatStageItems& operator=(const DatStageItems&) = delete;
    std::span<const MeleeWebStageItemDesc> items() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

}
