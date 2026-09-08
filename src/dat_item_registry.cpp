#include "dat_item_registry.hpp"
#include <algorithm>
namespace melee_web {
namespace {
void region(const DatArchive& a,uint32_t p,uint32_t n){
    if(p%4)throw DatError("Item registry descriptor is unaligned");
    (void)a.range(p,n);
    if(n>a.next_target_offset(p)-p)throw DatError("Item registry descriptor crosses a referenced region");
}
}
DatItemRegistry::DatItemRegistry(const DatArchive& a){
    auto root=std::find_if(a.public_symbols().begin(),a.public_symbols().end(),[](const auto& r){return r.name=="itPublicData";});
    if(root==a.public_symbols().end())throw DatError("Item itPublicData root is missing");
    root_offset=root->data_offset;region(a,root_offset,24);
    auto p=a.pointer(root_offset+8,MELEE_WEB_ITEM_REGISTRY_COUNT*4);
    if(!p)throw DatError("Character item registry is null");
    table_offset=*p;region(a,*p,MELEE_WEB_ITEM_REGISTRY_COUNT*4);
    for(uint32_t i=0;i<articles.size();i++){
        articles[i]=a.pointer(*p+4*i,24);
        if(articles[i])region(a,*articles[i],24);
    }
}
}
