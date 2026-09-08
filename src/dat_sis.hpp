#pragma once
#include "dat_archive.hpp"
#include <memory>
#include <string_view>

namespace melee_web {
// SIS roots combine two font byte arrays with a table of bytecode strings.
// Keep bytecode in its original big-endian representation; only the pointer
// table is hydrated. The original SIS interpreter owns text/layout behavior.
class DatSis {
public:
    DatSis(std::shared_ptr<const DatArchive>, std::string_view symbol);
    ~DatSis();
    DatSis(const DatSis&) = delete;
    DatSis& operator=(const DatSis&) = delete;
    void* descriptor() const noexcept;
    unsigned entry_count() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
