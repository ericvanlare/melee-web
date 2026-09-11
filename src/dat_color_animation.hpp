#pragma once
#include "dat_commands.hpp"
#include "gameplay_common_context.h"
namespace melee_web {
// Owns original ColorOverlay row tables and their validated command programs.
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
