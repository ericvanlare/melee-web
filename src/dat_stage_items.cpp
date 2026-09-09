#include "dat_stage_items.hpp"
#include "gameplay_article_data.h"
#include <algorithm>
#include <set>

namespace melee_web {
namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw DatError(message);
}
}

struct DatStageItems::Storage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    std::vector<MeleeWebStageItemDesc> items;
    std::vector<std::unique_ptr<DatItemArticle>> articles;
    explicit Storage(std::shared_ptr<const DatArchive> value)
        : archive(value), arena(std::move(value)) {}
};

DatStageItems::DatStageItems(std::shared_ptr<const DatArchive> archive,
                             const char* symbol)
    : storage_(std::make_unique<Storage>(archive))
{
    require(bool(archive) && symbol, "Stage items require an owned archive and symbol");
    const auto root = std::find_if(archive->public_symbols().begin(),
        archive->public_symbols().end(), [&](const auto& entry) { return entry.name == symbol; });
    if (root == archive->public_symbols().end()) return;
    const uint32_t end = archive->next_target_offset(root->data_offset);
    std::set<int32_t> kinds;
    for (uint32_t slot = root->data_offset; ; slot += 4) {
        require(slot + 4 <= end && storage_->items.size() < 30,
                "Stage item pointer list is unterminated or exceeds its source table");
        const auto pair = archive->pointer(slot, 8);
        if (!pair) break;
        require(!archive->has_relocation(*pair), "Stage item kind is a pointer");
        const int32_t kind = static_cast<int32_t>(archive->be32(*pair));
        require(kinds.insert(kind).second, "Stage item kind is duplicated");
        const auto article_root = archive->pointer(*pair + 4, 24);
        require(bool(article_root), "Stage item Article root is null");
        uint32_t unresolved = 0;
        void* article = melee_web_article_decode(storage_->arena.reader(),
                                                  *article_root, &unresolved);
        require(article, "Stage item registration Article decode failed");
        auto hydrated = std::make_unique<DatItemArticle>(archive, *article_root,
                                                        uint32_t(kind), article);
        require(!melee_web_article_unresolved(article),
                "Stage item Article graph remains incomplete after hydration");
        storage_->items.push_back({kind, article});
        storage_->articles.push_back(std::move(hydrated));
    }
}

DatStageItems::~DatStageItems() = default;
std::span<const MeleeWebStageItemDesc> DatStageItems::items() const noexcept
{
    return storage_->items;
}

}
