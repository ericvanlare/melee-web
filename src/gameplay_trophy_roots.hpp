#pragma once

#include "dat_trophy_data.hpp"
#include "gameplay_archive_sections.h"

#include <melee/ft/forward.h>
#include <melee/ty/types.h>

#include <cstdint>
#include <span>
#include <vector>

namespace melee_web {

// Portable TyDatai decoding stays separate from the native source ABI. Each
// locale archive gets its own copies because lbArchive_LoadSymbols resolves
// the archive filename and the table contents are not interchangeable.
class GameplayTrophyRoots {
public:
    explicit GameplayTrophyRoots(const DatTrophyData& data)
    {
        trophy_models_ = models(data.init_model_table());
        trophy_models_d_ = models(data.init_model_d_table());
        trophy_names_.reserve(data.model_sort_table().size());
        for (const auto& row : data.model_sort_table())
            trophy_names_.push_back({row.x0, row.x2, row.x4, row.x6, row.x8, row.xa});
        trophy_display_ = displays(data.display_model_table());
        trophy_display_us_ = displays(data.display_model_us_table());
        trophy_exp_different_.assign(data.exp_different_table().begin(),
                                     data.exp_different_table().end());
        trophy_no_get_us_.assign(data.no_get_us_table().begin(),
                                 data.no_get_us_table().end());
    }

    void append_symbols(const char* filename,
                        std::vector<MeleeWebArchiveSymbol>& symbols) const
    {
        symbols.push_back({filename, "tyInitModelTbl",
                           const_cast<TrophyData*>(trophy_models_.data())});
        symbols.push_back({filename, "tyInitModelDTbl",
                           const_cast<TrophyData*>(trophy_models_d_.data())});
        symbols.push_back({filename, "tyModelSortTbl",
                           const_cast<ToyNameData*>(trophy_names_.data())});
        symbols.push_back({filename, "tyExpDifferentTbl",
                           const_cast<std::int16_t*>(trophy_exp_different_.data())});
        symbols.push_back({filename, "tyNoGetUsTbl",
                           const_cast<std::int16_t*>(trophy_no_get_us_.data())});
        symbols.push_back({filename, "tyDisplayModelTbl",
                           const_cast<TyDspEntry*>(trophy_display_.data())});
        symbols.push_back({filename, "tyDisplayModelUsTbl",
                           const_cast<TyDspEntry*>(trophy_display_us_.data())});
    }

private:
    static std::vector<TrophyData> models(std::span<const DatTrophyEntry> entries)
    {
        std::vector<TrophyData> result;
        result.reserve(entries.size());
        for (const auto& row : entries)
            result.push_back({row.id, row.x04, row.x08, row.x0c, row.x10,
                              row.x14, row.x18, row.x1c, row.x20, row.x21,
                              row.x22, row.x23});
        return result;
    }

    static std::vector<TyDspEntry> displays(
        std::span<const DatTrophyDisplayEntry> entries)
    {
        std::vector<TyDspEntry> result;
        result.reserve(entries.size());
        for (const auto& row : entries)
            result.push_back({row.x00, row.x04, row.x05,
                              {row.pad06, row.pad07}, row.x08, row.x0c});
        return result;
    }

    std::vector<TrophyData> trophy_models_, trophy_models_d_;
    std::vector<ToyNameData> trophy_names_;
    std::vector<TyDspEntry> trophy_display_, trophy_display_us_;
    std::vector<std::int16_t> trophy_exp_different_, trophy_no_get_us_;
};

} // namespace melee_web
