#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.hpp"
#include "fighter_runtime_fixture.hpp"
#include <cstdio>
#include <iostream>
using namespace fighter_runtime_test;
extern "C" {
MeleeWebFighterAssetScope* assets_test_begin(void*,void*,void*,void*,MeleeWebFighterAssetBind,MeleeWebFighterAssetUnbind);
int assets_test_restored(void);
Fighter* assets_test_construct_storage(void);
int assets_test_load(Fighter*,int);
void assets_test_destroy_storage(Fighter*);
}
struct Context {
    std::array<std::unique_ptr<GameplayActionStore>,2> stores;
    std::array<Fighter*,2> fighters{};
    static int bind(void* ptr,Fighter* fp,void** rows,void** blends,char* error,size_t size) {
        auto& self=*static_cast<Context*>(ptr);
        try {
            for(size_t i=0;i<2;++i)if(!self.fighters[i]) {
                self.stores[i]->bind(fp);self.fighters[i]=fp;
                *rows=self.stores[i]->action_rows();*blends=self.stores[i]->blend_rows();return 1;
            }
            throw DatError("Test context has no spare fighter");
        }catch(const std::exception&e){if(error&&size)std::snprintf(error,size,"%s",e.what());return 0;}
    }
    static void unbind(void* ptr,Fighter* fp) {
        auto& self=*static_cast<Context*>(ptr);
        for(size_t i=0;i<2;++i)if(self.fighters[i]==fp){self.stores[i]->unbind();self.fighters[i]=nullptr;return;}
    }
};
int main() {
    try {
        FighterFixture f;put32(f.data,f.command_a,0);f.unlink(f.command_a+4);put32(f.data,f.command_b,0);
        auto archive=std::make_shared<const DatArchive>(f.file());
        for(unsigned restart=0;restart<2;++restart) {
            Context context;
            for(auto& store:context.stores)store=std::make_unique<GameplayActionStore>(archive,mario(),f.container);
            auto& tables=*context.stores[0];
            auto* scope=assets_test_begin(tables.action_rows(),tables.blend_rows(),tables.wait_choices(),&context,Context::bind,Context::unbind);
            check(scope!=nullptr,"Scoped source ftData publication");
            Fighter* a=assets_test_construct_storage();Fighter* b=assets_test_construct_storage();
            check(melee_web_fighter_assets_live(scope)==2,"Automatic B10 bindings own independent Fighters");
            char error[160];check(!melee_web_fighter_assets_end(scope,error,sizeof(error)),"Asset teardown rejects live source Fighters");
            check(assets_test_load(a,2) && assets_test_load(b,2),"Original loader reaches both independent owned action stores");
            assets_test_destroy_storage(a);
            check(melee_web_fighter_assets_live(scope)==1 && assets_test_load(b,2),"Unloading one Fighter preserves other's streams");
            assets_test_destroy_storage(b);
            check(melee_web_fighter_assets_end(scope,error,sizeof(error)) && assets_test_restored(),"Final source unload permits exact global restoration");
        }
        std::cout<<"Scoped fighter asset publication, original B10/loaders, independent teardown and restart: passed\n";
    }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
}
