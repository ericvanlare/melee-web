#pragma once
#include "dat_fighter_runtime.hpp"
#include "gameplay_action_store.h"
#include <map>
namespace melee_web {
// Native conversion of the exact bounded Wait command graph. All non-control
// operands use canonical numeric fields; branches require DAT relocations.
class DatCommands {
public:
    DatCommands(std::shared_ptr<const DatArchive>, std::span<const uint32_t> roots);
    ~DatCommands();
    DatCommands(const DatCommands&) = delete;
    DatCommands& operator=(const DatCommands&) = delete;
    void* at(uint32_t offset) const;
    size_t word_count() const noexcept { return indices_.size(); }
private:
    std::map<uint32_t, size_t> indices_;
    void* native_ = nullptr;
};
}
