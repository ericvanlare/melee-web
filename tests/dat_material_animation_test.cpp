#include "dat_material_animation.hpp"
#include <algorithm>
#include <bit>
#include <iostream>
#include <stdexcept>
using Bytes=std::vector<uint8_t>;
static void check(bool ok,const char* why) { if(!ok) throw std::runtime_error(why); }
static void put16(Bytes& b,uint32_t o,uint16_t v) { b.at(o)=uint8_t(v>>8);b.at(o+1)=uint8_t(v); }
static void put32(Bytes& b,uint32_t o,uint32_t v) { for(unsigned i=0;i<4;++i)b.at(o+i)=uint8_t(v>>(24-8*i)); }
struct Fixture {
    Bytes data=Bytes(320); std::vector<uint32_t> relocations;
    void link(uint32_t s,uint32_t t) {put32(data,s,t);relocations.push_back(s);}
    Fixture() {
        link(8,12); link(20,28); link(36,52); link(40,68); put32(data,48,2U<<16);
        put32(data,56,std::bit_cast<uint32_t>(10.0f)); link(60,76);
        link(68,100);link(72,124);
        put32(data,80,4);data[88]=1;data[89]=0x85;link(92,96);
        data[96]=0x11;data[97]=0;data[98]=1;data[99]=32;
        link(100,256);put32(data,104,0x00080008);
        link(124,288);put32(data,128,0x00080008);
    }
    std::shared_ptr<const melee_web::DatArchive> archive() const {
        const auto table=32+uint32_t(data.size()),pub=table+uint32_t(relocations.size()*4);
        Bytes b(pub+10);put32(b,0,uint32_t(b.size()));put32(b,4,uint32_t(data.size()));
        put32(b,8,uint32_t(relocations.size()));put32(b,12,1);
        std::copy(data.begin(),data.end(),b.begin()+32);
        for(unsigned i=0;i<relocations.size();++i)put32(b,table+4*i,relocations[i]);
        b[pub+8]='r';return std::make_shared<melee_web::DatArchive>(b);
    }
};
struct SharedTextureFixture : Fixture {
    SharedTextureFixture() {
        data.resize(512);
        // Two independent texture AObjs use the same authored FObj bytes.
        // The texture descriptors and AObjs remain distinct source records.
        link(28, 220); // first TexAnim -> second TexAnim
        put32(data, 224, 1); // second TexAnim source texture ID
        link(228, 380); // second TexAnim -> second AObj
        link(232, 68); // share the image table, not the descriptor
        put32(data, 240, 2U << 16); // two images, no palettes
        link(388, 76); // second AObj -> the same FObj as the first AObj
    }
};
struct SharedMaterialFixture : Fixture {
    SharedMaterialFixture() {
        data.resize(512);
        // Two material AObjs use the same authored FObj. The DObj chain is
        // separate from the texture chain above so both local cycle guards
        // are exercised independently.
        link(12, 340);  // first DObj -> second DObj
        link(16, 360);  // first DObj -> first material AObj
        link(344, 380); // second DObj -> second material AObj
        put32(data, 424, 4); // shared material FObj stream length
        put32(data, 428, std::bit_cast<uint32_t>(10.0f));
        data[432] = 10; data[433] = 0x85;
        link(436, 440); // shared material FObj -> stream
        std::copy(data.begin() + 96, data.begin() + 100, data.begin() + 440);
        link(368, 420); // first AObj -> shared FObj
        link(388, 420); // second AObj -> shared FObj
    }
};
struct PairedFixture : Fixture {
    PairedFixture() {
        // Make both image and palette tables explicit and give the two
        // diagonal pairs different capacities: image[0] selects a zero texel
        // with a one-entry palette, while image[1] selects index one with a
        // two-entry palette. The off-diagonal image[1]/palette[0] pair is
        // intentionally invalid.
        data.resize(1024);
        put32(data,48,0x00020002); // two image and two palette entries
        put32(data,108,9); put32(data,132,9); // C8 images
        put32(data,100,704); put32(data,124,768);
        data[768]=1; // visible texel in image[1]
        link(44,400); // HSD_TexAnim::tluttbl
        link(400,448); link(404,464);
        link(448,544); put16(data,460,1); // palette[0]: one entry
        link(464,576); put16(data,476,2); // palette[1]: two entries
        link(76,160); // append a synchronized TCLT FObj after TIMG
        put32(data,164,4); data[172]=10; data[173]=0x85; data[174]=0x50; // unused slope format differs
        link(176,192);
        std::copy(data.begin()+96,data.begin()+100,data.begin()+192);
    }
};
int main() {
    try {
        MeleeWebNativeJointDesc joint{}; joint.child=joint.next=UINT32_MAX;
        MeleeWebNativeDObjDesc dobj{}; dobj.next=dobj.pobj=UINT32_MAX;
        MeleeWebNativeTextureDesc texture{};
        MeleeWebNativeMaterialDesc material{};material.textures=&texture;material.material.texture_count=1;
        MeleeWebNativeGraph model{&joint,&dobj,nullptr,&material,1,1,0,1,0};
        Fixture f;auto archive=f.archive();
        melee_web::DatMaterialAnimation valid(archive,0,model);archive.reset();
        check(valid.descriptor()&&valid.texture_animation_count()==1&&valid.image_count()==2,"owned native animation graph");
        auto rejected=[&](Fixture bad) {
            bool failed=false;
            try {melee_web::DatMaterialAnimation invalid(bad.archive(),0,model);}catch(const melee_web::DatError&){failed=true;}
            check(failed,"malformed material animation must reject before native evaluation");
        };
        auto rejected_for=[&](const auto& bad, const auto& checked_model) {
            bool failed=false;
            try {melee_web::DatMaterialAnimation invalid(bad.archive(),0,checked_model);}catch(const melee_web::DatError&){failed=true;}
            check(failed,"malformed material animation must reject its local animation cycle");
        };
        MeleeWebNativeTextureDesc textures[2]{};
        textures[0].source_id=0; textures[1].source_id=1;
        MeleeWebNativeMaterialDesc multi_material=material;
        multi_material.textures=textures; multi_material.material.texture_count=2;
        MeleeWebNativeGraph texture_model{&joint,&dobj,nullptr,&multi_material,1,1,0,1,0};
        SharedTextureFixture shared_texture;
        melee_web::DatMaterialAnimation texture_alias(shared_texture.archive(),0,texture_model);
        check(texture_alias.texture_animation_count()==2,
              "independent texture AObjs may share one authored FObj");
        SharedTextureFixture texture_cycle; texture_cycle.link(76,76);
        rejected_for(texture_cycle,texture_model);

        MeleeWebNativeDObjDesc material_dobjs[2]{};
        material_dobjs[0].next=1; material_dobjs[0].pobj=UINT32_MAX;
        material_dobjs[1].next=UINT32_MAX; material_dobjs[1].pobj=UINT32_MAX;
        MeleeWebNativeGraph material_model{&joint,material_dobjs,nullptr,&material,1,2,0,1,0};
        SharedMaterialFixture shared_material;
        melee_web::DatMaterialAnimation material_alias(shared_material.archive(),0,material_model);
        check(material_alias.descriptor() && material_alias.texture_animation_count()==1,
              "independent material AObjs may share one authored FObj");
        SharedMaterialFixture material_cycle; material_cycle.link(420,420);
        rejected_for(material_cycle,material_model);
        for(unsigned channel=2;channel<=9;channel++){
            Fixture numeric;numeric.data[88]=channel;numeric.data[96]=0x12;
            melee_web::DatMaterialAnimation transform(numeric.archive(),0,model);
            check(transform.texture_animation_count()==1,"numeric texture transforms and blend permit checked interpolation");
        }
        Fixture unsupported_texture;unsupported_texture.data[88]=11;rejected(unsupported_texture);
        Fixture alpha;
        alpha.relocations.erase(std::remove(alpha.relocations.begin(),alpha.relocations.end(),20),alpha.relocations.end());
        put32(alpha.data,20,0);alpha.link(16,52);alpha.data[88]=10;
        melee_web::DatMaterialAnimation native_alpha(alpha.archive(),0,model);
        check(native_alpha.descriptor()&&native_alpha.texture_animation_count()==0,"native material alpha owns numeric stream without texture tables");
        Fixture unsupported_alpha=alpha;unsupported_alpha.data[88]=11;rejected(unsupported_alpha);
        Fixture color=alpha;color.data[88]=9;
        melee_web::DatMaterialAnimation native_color(color.archive(),0,model);
        check(native_color.descriptor(),"bounded material RGB channel");
        Fixture color_overflow=color;color_overflow.data[99]=64;
        melee_web::DatMaterialAnimation runtime_checked_color(color_overflow.archive(),0,model);
        Fixture color_interpolation=color;color_interpolation.data[96]=0x12;
        melee_web::DatMaterialAnimation interpolated_color(color_interpolation.archive(),0,model);
        check(interpolated_color.descriptor()&&runtime_checked_color.descriptor(),"original runtime guards validate produced color values");
        Fixture broken_alpha=alpha;put32(broken_alpha.data,80,2);rejected(broken_alpha);
        Fixture bad;bad.data[99]=64;rejected(bad); // table index2 into two entries
        melee_web::DatMaterialAnimation dispatch_guarded(bad.archive(),0,model,
            melee_web::TextureIndexValidation::DispatchedValues);
        check(dispatch_guarded.image_count()==2 && bad.data[99]==64,
              "dispatch-checked policy preserves encoded unselected values and exact table capacity");
        bad=Fixture();bad.data[96]=0x12;rejected(bad); // interpolated index could overshoot
        bad=Fixture();put32(bad.data,80,2);rejected(bad); // packet advertises two values
        bad=Fixture();put32(bad.data,32,1);rejected(bad); // missing texture ID
        bad=Fixture();put32(bad.data,48,0);rejected(bad); // absent image table count
        bad=Fixture();put32(bad.data,84,0x7fc00000);rejected(bad); // nonfinite start
        bad=Fixture();bad.link(0,0);rejected(bad); // relocated-zero cycle
        bad=Fixture();bad.link(24,52);rejected(bad); // active render channel
        // Menu graphs exceed the earlier fighter-only 140-node gate. Keep
        // the same 256-node bound as the native joint descriptor owner.
        auto menu_tree=[&](unsigned count){
            Fixture tree;tree.data=Bytes(count*12);tree.relocations.clear();
            std::vector<MeleeWebNativeJointDesc> nodes(count);
            for(unsigned i=0;i<count;i++){
                nodes[i].child=i+1<count?i+1:UINT32_MAX;
                nodes[i].next=nodes[i].dobj=UINT32_MAX;
                if(i+1<count)tree.link(i*12,(i+1)*12);
            }
            MeleeWebNativeGraph graph{nodes.data(),nullptr,nullptr,nullptr,count,0,0,0,0};
            melee_web::DatMaterialAnimation animation(tree.archive(),0,graph);
            check(animation.descriptor(),"menu material tree retains every node");
        };
        menu_tree(173);menu_tree(256);
        bool excessive_tree=false;
        try{menu_tree(257);}catch(const melee_web::DatError&){excessive_tree=true;}
        check(excessive_tree,"native joint budget still bounds menu material trees");
        // A legal animation may alias the same large image/palette through many
        // table entries. Validate its texels once, retaining all pair checks.
        Fixture repeated;
        repeated.data.resize(0x104020);
        repeated.relocations.erase(std::remove_if(repeated.relocations.begin(),repeated.relocations.end(),
            [](uint32_t slot){return slot==40;}),repeated.relocations.end());
        repeated.link(40,0x200);repeated.link(44,0x600);put32(repeated.data,48,0x01000100);
        for(unsigned i=0;i<256;i++){
            repeated.link(0x200+4*i,0xa00);repeated.link(0x600+4*i,0xa20);
        }
        repeated.link(0xa00,0x4000);put32(repeated.data,0xa04,0x04000400);
        put32(repeated.data,0xa08,9); // 1024x1024 CI8, one checked image
        repeated.link(0xa20,0x104000);put32(repeated.data,0xa2c,1U<<16);
        melee_web::DatMaterialAnimation repeated_tables(repeated.archive(),0,model);
        check(repeated_tables.image_count()==256,"aliased image tables stay within actual validation work budget");
        repeated.data[0x4000]=1;rejected(repeated); // still rejects invalid indices

        // Matching TIMG/TCLT metadata and encoded streams activate the proven
        // diagonal check. A divergent TCLT stream must fall back to Cartesian
        // validation, and a bad selected diagonal texel remains rejected.
        PairedFixture paired;
        melee_web::DatMaterialAnimation synchronized(paired.archive(),0,model);
        check(synchronized.texture_animation_count()==1,
              "synchronized index/palette tracks use diagonal validation");
        PairedFixture divergent;
        divergent.data[195]=0; // TCLT selects a different second index
        rejected(divergent);
        PairedFixture out_of_range;
        put16(out_of_range.data,476,1); // selected image[1] index one is invalid
        rejected(out_of_range);
        std::cout<<"owned material animation topology/index/bounds checks passed\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
