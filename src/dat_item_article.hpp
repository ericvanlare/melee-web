#pragma once
#include "dat_archive.hpp"
#include <memory>
namespace melee_web {
// Owns source item model/state/command graphs, published into an existing
// registration identity only after complete validation. The current native
// article schemas cover the Random Pokémon stage-state table, Mario Fire/Cape,
// Dr. Mario Vitamin/Sheet, the Fox/Falco and Kirby-copy laser/blaster/illusion
// family, Game & Watch and Kirby-copy articles, Ice Climbers and Kirby-copy
// articles, Samus's Bomb/Charge Shot/Missile/Grapple Beam, Yoshi's Story's
// Heiho article, Zelda/Sheik articles, and Link/Young Link's articles.
class DatItemArticle {
public:
    DatItemArticle(std::shared_ptr<const DatArchive>,uint32_t root,uint32_t kind,void* registered_article);
    ~DatItemArticle();
    DatItemArticle(const DatItemArticle&)=delete;
    DatItemArticle& operator=(const DatItemArticle&)=delete;
    uint32_t state_count()const noexcept;
    // Yoshi's fourth ftData x48 entry aliases Egg Lay's Article model root.
    // Expose that owned descriptor so both source consumers retain one graph.
    void* model_joint_descriptor()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
