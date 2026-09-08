#pragma once
#include "native_dat.h"
#include "dat_archive.hpp"
#include <memory>
namespace melee_web {
class NativeDatArena {
public:
    explicit NativeDatArena(std::shared_ptr<const DatArchive>);
    ~NativeDatArena();
    NativeDatArena(const NativeDatArena&) = delete;
    NativeDatArena& operator=(const NativeDatArena&) = delete;
    const MeleeWebNativeDat* reader() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
