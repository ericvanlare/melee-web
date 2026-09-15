#pragma once
#include "dat_archive.hpp"
#include <memory>
namespace melee_web {
// Owns source item model/state/command graphs, published into an existing
// registration identity only after complete validation. The current native
// article schemas cover Mario Fire/Cape, Dr. Mario Vitamin/Sheet and the shared Fox/Falco laser,
// blaster and illusion/phantasm family, plus Yoshi's Story's Heiho article.
class DatItemArticle {
public:
    DatItemArticle(std::shared_ptr<const DatArchive>,uint32_t root,uint32_t kind,void* registered_article);
    ~DatItemArticle();
    DatItemArticle(const DatItemArticle&)=delete;
    DatItemArticle& operator=(const DatItemArticle&)=delete;
    uint32_t state_count()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
