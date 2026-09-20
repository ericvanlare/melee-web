#include "gameplay_fighter_assets.h"
#include "gameplay_action_store.hpp"
#include "dat_item_commands.hpp"
#include "fighter_runtime_fixture.hpp"
#include <cstdio>
#include <iostream>
using namespace fighter_runtime_test;
extern "C" {
MeleeWebFighterAssetScope* assets_test_begin(void*,void*,void*,void*,MeleeWebFighterAssetBind,MeleeWebFighterAssetUnbind);
int assets_test_restored(void);
int assets_test_item_commands(void);
int assets_test_nullable_material(void*,void*,void*,MeleeWebFighterAssetBind,MeleeWebFighterAssetUnbind);
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
        check(assets_test_item_commands(),"Item loop and resize words match source bitfield consumers");
        auto script=[](std::initializer_list<uint32_t> words){
            Bytes data(words.size()*4);size_t at=0;
            for(auto word:words){put32(data,at,word);at+=4;}
            return DatArchive(pack(data,{},"script"));
        };
        DatItemCommands item_commands;
        check(item_commands.decode(script({(3U<<26)|4,(1U<<26)|1,4U<<26,0}),0)!=nullptr,
              "Bounded original item loop decodes");
        for(const auto& invalid:{script({3U<<26,4U<<26,0}),script({4U<<26,0}),
                                script({(3U<<26)|1,0}),script({(3U<<26)|1,(3U<<26)|1,4U<<26,4U<<26,0})}){
            bool rejected=false;try{DatItemCommands bad;bad.decode(invalid,0);}catch(const DatError&){rejected=true;}
            check(rejected,"Item decoder rejects zero, unmatched, unfinished and over-capacity loops");
        }
        FighterFixture f;put32(f.data,f.command_a,0);f.unlink(f.command_a+4);put32(f.data,f.command_b,0);
        auto archive=std::make_shared<const DatArchive>(f.file());
        for(unsigned restart=0;restart<2;++restart) {
            // Link has no randomized Wait table. Preserve null through the
            // native row owner and fighter publication, including restart.
            if(restart){f.unlink(0x24);put32(f.data,0x24,0);archive=std::make_shared<const DatArchive>(f.file());}
            Context context;
            for(auto& store:context.stores)store=std::make_unique<GameplayActionStore>(archive,mario(),f.container);
            auto& tables=*context.stores[0];
            check(assets_test_nullable_material(tables.action_rows(),tables.blend_rows(),&context,Context::bind,Context::unbind),
                  "Source-authored null material publishes all costumes and restores ownership; required or fabricated material is rejected");
            check(restart?tables.wait_choices()==nullptr:tables.wait_choices()!=nullptr,
                  "Native Wait table preserves source nullability");
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
