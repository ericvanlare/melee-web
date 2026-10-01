#include "native_dat.hpp"
#include <cstdlib>
#include <utility>
#include <vector>
namespace melee_web {
struct NativeDatArena::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::vector<NativeDatSourceRegion> source_regions;
    std::vector<void*> allocations;
    size_t bytes = 0;
    MeleeWebNativeDat api{};
    ~Storage() { for (auto* p : allocations) std::free(p); }
};
NativeDatArena::NativeDatArena(std::shared_ptr<const DatArchive> archive)
    : NativeDatArena(std::move(archive), {})
{
}

NativeDatArena::NativeDatArena(std::shared_ptr<const DatArchive> archive,
                               std::vector<NativeDatSourceRegion> source_regions)
    : storage_(std::make_unique<Storage>()) {
    if (!archive) throw DatError("Native descriptor archive is null");
    auto& s = *storage_; s.archive = std::move(archive);
    s.source_regions = std::move(source_regions);
    for (std::size_t i = 0; i < s.source_regions.size(); ++i) {
        const auto& region = s.source_regions[i];
        if (!region.archive || region.archive->data().empty() ||
            region.source_data_address > UINT32_MAX - region.archive->data().size())
            throw DatError("Native source archive address region is invalid");
        const std::uint64_t first = region.source_data_address;
        const std::uint64_t last = first + region.archive->data().size();
        for (std::size_t j = 0; j < i; ++j) {
            const auto& prior = s.source_regions[j];
            const std::uint64_t prior_first = prior.source_data_address;
            const std::uint64_t prior_last = prior_first + prior.archive->data().size();
            if (first < prior_last && prior_first < last)
                throw DatError("Native source archive address regions overlap");
        }
    }
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
    s.api.source_region = [](void* c,uint32_t address,size_t n)->const void* {
        const auto& regions = static_cast<Storage*>(c)->source_regions;
        for (const auto& region : regions) {
            if (address < region.source_data_address) continue;
            const auto offset = std::size_t(address - region.source_data_address);
            const auto bytes = region.archive->data();
            if (offset > bytes.size() || n > bytes.size() - offset) continue;
            try {
                if (n > region.archive->next_target_offset(static_cast<std::uint32_t>(offset)) - offset)
                    continue;
                return region.archive->range(static_cast<std::uint32_t>(offset), n).data();
            } catch (const DatError&) {
                continue;
            }
        }
        return nullptr;
    };
    s.api.extent = [](void* c,uint32_t o) {
        const auto& a = *static_cast<Storage*>(c)->archive;
        (void)a.range(o,1);
        return a.next_target_offset(o)-o;
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
