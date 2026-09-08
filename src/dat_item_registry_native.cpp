#include "dat_item_registry_native.hpp"
#include "gameplay_article_data.h"
#include "native_dat.hpp"
#include <map>
namespace melee_web {
struct DatItemRegistryNative::Storage {
    NativeDatArena arena;
    std::array<void*,MELEE_WEB_ITEM_REGISTRY_COUNT> articles{};
    std::array<uint32_t,MELEE_WEB_ITEM_REGISTRY_COUNT> masks{};
    explicit Storage(std::shared_ptr<const DatArchive> a):arena(std::move(a)){}
};
DatItemRegistryNative::DatItemRegistryNative(std::shared_ptr<const DatArchive> archive)
    :storage_(std::make_unique<Storage>(archive)) {
    const DatItemRegistry source(*archive);
    std::map<uint32_t,std::pair<void*,uint32_t>> decoded;
    for(uint32_t i=0;i<source.articles.size();i++){
        if(!source.articles[i])continue;
        const uint32_t offset=*source.articles[i];
        auto found=decoded.find(offset);
        if(found==decoded.end()){
            uint32_t mask=0;void* p=melee_web_article_decode(storage_->arena.reader(),offset,&mask);
            if(!p)throw DatError("Native item Article decoder returned no root");
            found=decoded.emplace(offset,std::pair{p,mask}).first;
        }
        storage_->articles[i]=found->second.first;storage_->masks[i]=found->second.second;
    }
}
DatItemRegistryNative::~DatItemRegistryNative()=default;
void* const* DatItemRegistryNative::articles() const noexcept{return storage_->articles.data();}
const std::array<uint32_t,MELEE_WEB_ITEM_REGISTRY_COUNT>& DatItemRegistryNative::unresolved_masks() const noexcept{return storage_->masks;}
}
