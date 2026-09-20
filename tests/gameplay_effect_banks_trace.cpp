#include "dat_effect_banks.hpp"
#include "dat_effect_entries.hpp"
#include "hsd_native_joint.h"
#include "gameplay_bootstrap.h"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <sysdolphin/baselib/particle.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/spline.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>
#include <melee/ef/types.h>
#include <melee/ef/efdata.h>
extern EF_DAT_Entry efAsync_DatEntries[51];
extern u32* hsd_804D0948[65];
extern HSD_PSFormGroup** psFormGroupArray[65];
extern HSD_Particle* hsd_804D0908[16];
}
#pragma GCC diagnostic pop
#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <functional>
using Bytes=std::vector<uint8_t>;
static char error[256];
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(std::string(why)+": "+error);}
static void put32(Bytes& b,uint32_t o,uint32_t v){for(unsigned i=0;i<4;++i)b.at(o+i)=uint8_t(v>>(24-8*i));}
static Bytes fixture(){
    Bytes b(32+0xe0+8+8+2);put32(b,0,uint32_t(b.size()));put32(b,4,0xe0);put32(b,8,2);put32(b,12,1);
    auto put=[&](uint32_t o,uint32_t v){put32(b,32+o,v);};
    put(0,0x20);put(4,0x80);put(0x20,0x00420001);put(0x24,1000);put(0x28,1);put(0x2c,0x10);
    put(0x34,0x00010010);put(0x38,1);b[32+0x6c]=0xfe;
    put(0x80,1);put(0x84,0x20);put(0xa0,1);put(0xac,8);put(0xb0,8);put(0xb8,0x40);
    std::fill(b.begin()+32+0xc0,b.begin()+32+0xe0,0x73);
    put32(b,32+0xe0,0);put32(b,32+0xe0+4,4);b[32+0xe0+16]='r';return b;
}
static Bytes palette_fixture(uint32_t format_word){
    // One C4 image and its palette, each in a bounded 32-byte region.
    auto b=fixture();b.resize(32+0x100+8+8+2);
    put32(b,0,uint32_t(b.size()));put32(b,4,0x100);
    put32(b,32+0xa4,8);put32(b,32+0xa8,format_word);
    put32(b,32+0xb4,1);put32(b,32+0xbc,0x60);
    std::fill(b.begin()+32+0xe0,b.begin()+32+0x100,0x55);
    std::fill(b.begin()+32+0x100,b.end(),0);
    put32(b,32+0x100,0);put32(b,32+0x104,4);b[32+0x110]='r';
    return b;
}
static void palette_format_word(){
    const uint32_t authored=0x01000002; // Exact Captain particle group6 word.
    melee_web::DatEffectBanks owner(
        std::make_shared<melee_web::DatArchive>(palette_fixture(authored)),"r",1);
    check(melee_web_gameplay_startup(4U*1024U*1024U,error,sizeof(error)),"palette source startup");
    check(melee_web_effect_bank_attach(owner.bank(),error,sizeof(error)),"palette source registration");
    const auto* group=psTexGroupArray[1][0];
    check(group->tlutfmt==authored&&static_cast<u8>(group->tlutfmt)==2,
          "full authored palette word and original psdisp low-byte format preserved");
    check(group->texTable[0][0]==0x73&&group->texTable[1][0]==0x55,
          "indexed particle image and palette retain independent bounded bytes");
    check(melee_web_effect_bank_detach(owner.bank(),error,sizeof(error)),"palette source detach");
    check(melee_web_gameplay_shutdown(error,sizeof(error)),"palette source teardown");
    for(const uint32_t invalid:{0x01000003U,0x010000ffU}){
        bool rejected=false;
        try{melee_web::DatEffectBanks bad(
            std::make_shared<melee_web::DatArchive>(palette_fixture(invalid)),"r",1);}
        catch(const melee_web::DatError&){rejected=true;}
        check(rejected,"invalid consumed palette format rejected despite upper metadata");
    }
    std::cout<<"Original particle palette low-byte format and complete source word passed\n";
}
static void registration(std::shared_ptr<const melee_web::DatArchive> archive,const char* symbol,bool real){
    melee_web::DatEffectBanks owner(archive,symbol,1);archive.reset();
    MeleeWebEffectBankStats stats{};
    check(melee_web_effect_bank_stats(owner.bank(),&stats,error,sizeof(error)),"decoded bank stats");
    check(stats.first_command==1000&&stats.command_count==(real?14U:1U),"source command range");
    check(stats.texture_groups==(real?6U:1U)&&stats.images==(real?21U:1U)&&stats.palettes==(real?8U:0U),"source texture/palette counts");
    check(!stats.particle_bank_ready&&!stats.effect_entries_ready,"no premature bank/effect publication");
    const auto saved_ref=hsd_804D0948[1];const auto saved_tex=psTexGroupArray[1];const auto saved_form=psNumCmdList[1];
    const auto saved_cmd=ptclref_804D0E5C[1];const int saved_count=psCmdListArray[1];
    HSD_PSFormGroup** saved_groups=psFormGroupArray[1];
    const auto saved_alias=ptclref_804D0E5C[30];
    const auto saved_alias_tex=psTexGroupArray[30];
    for(unsigned pass=0;pass<2;++pass){
        check(melee_web_gameplay_startup(4U*1024U*1024U,error,sizeof(error)),"source world startup");
        check(melee_web_effect_bank_attach(owner.bank(),error,sizeof(error)),"original psInitDataBankLoad");
        auto* alias=owner.alias(30);
        check(!melee_web_effect_bank_has_command(30,1000),"unpublished authored dependency rejected");
        check(melee_web_effect_bank_attach(alias,error,sizeof(error)),"second source bank registration");
        check(melee_web_effect_bank_has_command(30,1000)&&
              !melee_web_effect_bank_has_command(30,999)&&
              !melee_web_effect_bank_has_command(30,1000+stats.command_count),"authored dependency range checked");
        check(ptclref_804D0E5C[30]==ptclref_804D0E5C[1]&&psTexGroupArray[30]==psTexGroupArray[1],
              "bank alias shares exact command and texture pointers");
        check(!melee_web_effect_bank_attach(owner.bank(),error,sizeof(error)),"duplicate bank owner rejected");
        check(psCmdListArray[1]==int(1000+stats.command_count)&&ptclref_804D0E5C[1][1000]->life==16,"original biased command lookup");
        check((ptclref_804D0E5C[1][1000]->kind&0x0e000000)==0x08000000,"original Locate kind fixup retained");
        check(psTexGroupArray[1][0]->num==(real?8U:1U),"original texture group lookup");
        check(melee_web_effect_bank_stats(owner.bank(),&stats,error,sizeof(error))&&stats.particle_bank_ready&&!stats.effect_entries_ready,
              "bank readiness remains separate from unhydrated EF entries");
        if(!real)check(psTexGroupArray[1][0]->texTable[0][0]==0x73,"owned tiled image bytes");
        HSD_Particle active{};hsd_804D0908[0]=&active;
        check(!melee_web_effect_bank_detach(owner.bank(),error,sizeof(error)),"live particle prevents bank release");hsd_804D0908[0]=nullptr;
        hsd_804D78E0=1;check(!melee_web_effect_bank_detach(owner.bank(),error,sizeof(error)),"live generator prevents bank release");hsd_804D78E0=0;
        check(melee_web_effect_bank_detach(owner.bank(),error,sizeof(error)),"bank detach");
        check(ptclref_804D0E5C[30]!=saved_alias,"alias remains registered independently");
        check(melee_web_effect_bank_detach(alias,error,sizeof(error)),"alias detach");
        check(ptclref_804D0E5C[30]==saved_alias&&psTexGroupArray[30]==saved_alias_tex,"alias restored");
        check(hsd_804D0948[1]==saved_ref&&psTexGroupArray[1]==saved_tex&&psNumCmdList[1]==saved_form&&
              ptclref_804D0E5C[1]==saved_cmd&&psCmdListArray[1]==saved_count&&psFormGroupArray[1]==saved_groups,"all original bank globals restored");
        check(melee_web_gameplay_shutdown(error,sizeof(error)),"source world shutdown");
    }
}
static void effect_entries(std::shared_ptr<const melee_web::DatArchive> archive,bool model_only=false){
    const unsigned bank=model_only?6:1;
    // efSync_Spawn's Link spin attacks consume both 6000/6001 and the
    // attached child effects 6002/6003, not just the first model pair.
    const unsigned count=model_only?4:2;
    melee_web::DatEffectEntries owner(archive,model_only?"effLinkDataTable":"effMarioDataTable",bank,count);
    archive.reset();check(owner.entry_count()==count&&!owner.entries_ready(),"decoded effect entries remain unpublished");
    auto* descriptors=reinterpret_cast<EF_EffectDesc*>(static_cast<uint8_t*>(owner.table())+8);
    check(descriptors[0].lifetime==(model_only?80:13)&&descriptors[1].lifetime==(model_only?80:160),"original effect lifetimes");
    check(model_only?owner.bank()==nullptr:owner.bank()!=nullptr,"Source particle bank presence is preserved");
    const auto previous_particles=ptclref_804D0E5C[bank];
    auto previous=efAsync_DatEntries[bank].data;
    for(unsigned pass=0;pass<2;++pass){
        check(melee_web_gameplay_startup(8U*1024U*1024U,error,sizeof(error)),"effect world startup");
        check(melee_web_native_world_enable(error,sizeof(error)),"effect native class initialization");
        check(owner.load(error,sizeof(error))&&owner.entries_ready(),"original efAsync_LoadSync publishes native entries");
        check(efAsync_DatEntries[bank].data==descriptors,"original effect lookup pointer");
        if(model_only)check(ptclref_804D0E5C[bank]==previous_particles,"Model-only table leaves particle registration untouched");
        check(!owner.load(error,sizeof(error)),"duplicate effect publication rejected");
        for(unsigned i=0;i<count;++i){
            if(model_only)check(descriptors[i].lifetime==80,"all four source Link effect lifetimes");
            const auto& d=descriptors[i].model_desc;
            auto* joint=HSD_JObjLoadJoint(d.joint);check(joint!=nullptr,"original effect model load");
            HSD_JObjAddAnimAll(joint,d.animjoint,d.matanim_joint,d.shapeanim_joint);
            for(float frame: {0.0f,1.0f,6.0f,12.0f}){HSD_JObjReqAnimAll(joint,frame);HSD_JObjAnimAll(joint);}
            HSD_JObjRemoveAll(joint);
        }
        efLib_EffectCount=1;check(!owner.detach(error,sizeof(error)),"live source effect prevents release");efLib_EffectCount=0;
        check(owner.detach(error,sizeof(error))&&!owner.entries_ready(),"effect scope detach");
        check(efAsync_DatEntries[bank].data==previous,"original effect lookup restored");
        check(melee_web_gameplay_shutdown(error,sizeof(error)),"effect world shutdown");
    }
    std::cout<<(model_only?"Local Link effects: ":"Local Mario effects: ")<<count<<" native model entries and animation graphs; original LoadSync/evaluation/restart passed\n";
}
static void common_entries(std::shared_ptr<const melee_web::DatArchive> archive){
    melee_web::DatEffectEntries owner(archive,"effCommonDataTable",0,47,true);archive.reset();
    check(owner.entry_count()==47,"all common source effect descriptors hydrated");
    auto* entries=reinterpret_cast<EF_EffectDesc*>(static_cast<char*>(owner.table())+8);
    for(unsigned pass=0;pass<2;pass++){
        check(melee_web_gameplay_startup(8U*1024U*1024U,error,sizeof(error)),"common path world startup");
        check(melee_web_native_world_enable(error,sizeof(error)),"common path descriptor ID context");
        auto& d=entries[36].model_desc;
        HSD_JObj* joint=HSD_JObjLoadJoint(d.joint);check(joint,"load original spline effect model");
        HSD_JObjAddAnimAll(joint,d.animjoint,nullptr,nullptr);
        HSD_JObj* path=nullptr;HSD_JObj* spline=nullptr;
        std::function<void(HSD_JObj*)> visit=[&](HSD_JObj* j){for(;j;j=j->next){
            if(j->flags&JOBJ_SPLINE)spline=j;
            if(j->aobj&&j->aobj->hsd_obj)path=j;
            visit(j->child);
        }};visit(joint);
        check(path&&spline&&path->aobj->hsd_obj==reinterpret_cast<HSD_Obj*>(spline),"original AObj resolves exact loaded spline identity");
        const auto* curve=spline->u.spline;check(curve&&curve->numcv>1,"original runtime retains checked control points");
        float positions[2][3];
        for(unsigned endpoint=0;endpoint<2;endpoint++){
            HSD_ObjData value{};value.fv=float(endpoint);JObjUpdateFunc(path,4,&value);
            positions[endpoint][0]=HSD_JObjGetTranslationX(path);
            positions[endpoint][1]=HSD_JObjGetTranslationY(path);
            positions[endpoint][2]=HSD_JObjGetTranslationZ(path);
            for(float x:positions[endpoint])check(std::isfinite(x),"original PATH output finite");
        }
        check(std::memcmp(positions[0],positions[1],sizeof(positions[0]))!=0,"original PATH moves along actual spline");
        HSD_JObjRemoveAll(joint);
        check(melee_web_gameplay_shutdown(error,sizeof(error)),"common path world teardown");
    }
    std::cout<<"Common47 descriptors and original PATH reference/evaluation/restart passed\n";
}
int main(int argc,char** argv){
    try{
        registration(std::make_shared<melee_web::DatArchive>(fixture()),"r",false);
        palette_format_word();
        for(unsigned mutation=0;mutation<5;++mutation){
            auto bytes=fixture();
            switch(mutation){
            case 0:put32(bytes,32+0x20,0x00440001);break;
            case 1:put32(bytes,32+0x2c,0xfffffff0);break;
            case 2:put32(bytes,32+0x30,1);break;
            case 3:put32(bytes,32+0xb8,0x60);break;
            case 4:put32(bytes,32+0x3c,0x7fc00000);break;
            }
            bool rejected=false;
            try{melee_web::DatEffectBanks invalid(std::make_shared<melee_web::DatArchive>(bytes),"r",1);}
            catch(const melee_web::DatError&){rejected=true;}
            check(rejected,"malformed particle bank rejected before publication");
        }
        if(argc==3&&std::strcmp(argv[1],"--common")==0){
            std::ifstream file(argv[2],std::ios::binary);check(bool(file),"open owned common effect archive");
            Bytes bytes((std::istreambuf_iterator<char>(file)),{});
            common_entries(std::make_shared<melee_web::DatArchive>(bytes));
        }else if(argc==3&&std::string(argv[1])=="--link"){
            std::ifstream file(argv[2],std::ios::binary);check(bool(file),"open Link effect archive");
            Bytes bytes((std::istreambuf_iterator<char>(file)),{});
            effect_entries(std::make_shared<melee_web::DatArchive>(bytes),true);
        }else if(argc==2){
            std::ifstream file(argv[1],std::ios::binary);check(bool(file),"open optional effect archive");
            Bytes bytes((std::istreambuf_iterator<char>(file)),{});
            registration(std::make_shared<melee_web::DatArchive>(bytes),"effMarioDataTable",true);
            effect_entries(std::make_shared<melee_web::DatArchive>(bytes));
            std::cout<<"Local Mario particle bank:14 commands,6 groups,21 images,8 palettes; original registration/restart passed\n";
        }else check(argc==1,"unexpected effect bank trace arguments");
        std::cout<<"Original particle bank ownership, bounds, readiness and restoration passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
