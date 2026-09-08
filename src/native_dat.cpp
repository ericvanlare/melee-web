#include "native_dat.hpp"
#include <cstdlib>
#include <vector>
namespace melee_web {
struct NativeDatArena::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::vector<void*> allocations;
    size_t bytes = 0;
    MeleeWebNativeDat api{};
    ~Storage() { for (auto* p : allocations) std::free(p); }
};
NativeDatArena::NativeDatArena(std::shared_ptr<const DatArchive> archive)
    : storage_(std::make_unique<Storage>()) {
    if (!archive) throw DatError("Native descriptor archive is null");
    auto& s = *storage_; s.archive = std::move(archive);
    s.api.context = &s;
    s.api.word = [](void* c,uint32_t o) {
        const auto& a = *static_cast<Storage*>(c)->archive;
        if (o % 4) throw DatError("Native word is unaligned");
        if (a.has_relocation(o)) throw DatError("Native scalar contains a relocation at " + std::to_string(o));
        return a.be32(o);
    };
    s.api.half = [](void* c,uint32_t o) {
        const auto& a = *static_cast<Storage*>(c)->archive;
        if (o % 2) throw DatError("Native halfword is unaligned");
        if (a.has_relocation(o & ~3U)) throw DatError("Native halfword contains a relocation at " + std::to_string(o));
        auto b = a.range(o,2); return uint16_t((uint16_t(b[0])<<8)|b[1]);
    };
    s.api.byte = [](void* c,uint32_t o) {
        const auto& a = *static_cast<Storage*>(c)->archive;
        if (a.has_relocation(o & ~3U)) throw DatError("Native byte contains a relocation at " + std::to_string(o));
        return a.range(o,1)[0];
    };
    s.api.pointer = [](void* c,uint32_t o,size_t n) {
        return static_cast<Storage*>(c)->archive->pointer(o,n).value_or(UINT32_MAX);
    };
    s.api.region = [](void* c,uint32_t o,size_t n)->const void* {
        const auto& a = *static_cast<Storage*>(c)->archive;
        const auto b = a.range(o,n);
        if (n > a.next_target_offset(o)-o) throw DatError("Native descriptor crosses referenced region at " + std::to_string(o));
        return b.data();
    };
    s.api.allocate = [](void* c,size_t n,size_t width)->void* {
        auto& a = *static_cast<Storage*>(c);
        constexpr size_t limit = 32U*1024U*1024U;
        if (!width || n > (limit-a.bytes)/width || a.allocations.size() >= 65536)
            throw DatError("Native descriptor allocation budget exceeded");
        if (!n) return nullptr;
        void* p = std::calloc(n,width);
        if (!p) throw DatError("Native descriptor allocation failed");
        try { a.allocations.push_back(p); } catch (...) { std::free(p); throw; }
        a.bytes += n*width; return p;
    };
    s.api.reject = [](void*,const char* why) { throw DatError(why); };
}
NativeDatArena::~NativeDatArena() = default;
const MeleeWebNativeDat* NativeDatArena::reader() const noexcept { return &storage_->api; }
}
