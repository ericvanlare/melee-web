#pragma once
#include "dat_commands.hpp"
#include "gameplay_common_context.h"
namespace melee_web {
// Owns the exact common fighter ColorOverlay table and its validated programs.
class DatColorAnimation {
public:
    DatColorAnimation(std::shared_ptr<const DatArchive>,uint32_t root,size_t count);
    ~DatColorAnimation();
    const MeleeWebColorRow* table()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
