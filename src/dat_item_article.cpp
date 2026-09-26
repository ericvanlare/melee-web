#include "dat_item_article.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "dat_item_commands.hpp"
#include "native_dat.hpp"
#include "gameplay_article_data.h"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop
#include <cmath>
#include <cstddef>
#include <cstring>
namespace melee_web {
namespace {
void require(bool c,const char* message){if(!c)throw DatError(message);}
void destroy(MeleeWebNativeJoint* p){if(p&&!melee_web_native_joint_destroy(p,nullptr,0))std::terminate();}
struct ArticleSchema { uint32_t special_bytes, state_count; bool special_required; };
ArticleSchema schema(uint32_t kind)
{
    switch(kind) {
    case It_Kind_Mario_Fire:
    case It_Kind_Kirby_MarioFire:return {20,1,true};
    // Luigi's fireball uses the shared five-float source type selectively:
    // its callback consumes x0, x4 and xC, while x8/x10 belong to Mario's
    // distinct fireball record. The authored Luigi region is four floats.
    case It_Kind_Luigi_Fire:return {16,1,true};
    // Koopa's Flame uses six source floats, one serialized state row, and
    // the source-valid null-joint ItemModelDesc form.
    case It_Kind_Koopa_Flame:return {24,1,true};
    // Ness's authored scalar extents come from the PlNs.dat article regions.
    // PK Fire's own record is two floats; the pillar record adds the scale.
    // PK Flush carries the eleven-float itFlashAttributes record with three
    // serialized animation rows, PK Thunder the five-float
    // itPKThunderAttributes record. The four Thunder trails share one
    // single-float record, the bat keeps one authored scalar, and the Yo-Yo's
    // 0x5C itYoyoAttributes record ends at its three source joint/material
    // pointers with no serialized state table.
    case It_Kind_Ness_PKFire:return {8,1,true};
    case It_Kind_Ness_PKFire_Flame:return {12,1,true};
    case It_Kind_Ness_PKFlush:return {0x2C,3,true};
    case It_Kind_Ness_PKThunder:return {0x14,1,true};
    case It_Kind_Ness_PKThunder1:
    case It_Kind_Ness_PKThunder2:
    case It_Kind_Ness_PKThunder3:
    case It_Kind_Ness_PKThunder4:return {4,1,true};
    case It_Kind_Ness_PKFlush_Explode:return {0x14,1,true};
    case It_Kind_Ness_Bat:return {4,1,true};
    case It_Kind_Ness_Yoyo:return {0x5C,0,true};
    // Peach's authored scalar extents come from the PlPe.dat article regions.
    // The Bomber explosion has no special record, two command-only state rows
    // and the source-valid null-joint ItemModelDesc form. The vegetable's
    // itPeachTurnipAttributes record is its lifetime float, the authored
    // turnip-type count and eight {odds, damage} pairs; the parasol and Toad
    // keep one authored unread scalar word each; the spore record is the
    // four-float itPeachToadSporeAttributes with one command-only state row.
    case It_Kind_Peach_Explode:return {0,2,false};
    case It_Kind_Peach_Turnip:return {0x48,3,true};
    case It_Kind_Peach_Parasol:return {4,2,true};
    case It_Kind_Peach_Toad:return {4,2,true};
    case It_Kind_Peach_ToadSpore:return {0x10,1,true};
    // Mewtwo's Disable is the authored two-float itMDisableAttributes record
    // (lifetime and horizontal velocity) with one serialized state row.
    case It_Kind_Mewtwo_Disable:return {8,1,true};
    // Shadow Ball's serialized special record is twelve words; the doldecomp
    // header's x30..x3C tail is not serialized by the disc record. Ten
    // serialized animation rows back the eighteen callback states.
    case It_Kind_Mewtwo_ShadowBall:return {0x30,10,true};
    // Seven original pill motion states select six serialized animation rows,
    // including the throw/catch sequences used by Dr. Mario's taunt.
    case It_Kind_DrMario_Vitamin:return {20,6,true};
    // Thunder has three source callbacks, but the callback state IDs are
    // [-1, 0, 0] and the DAT carries one serialized animation descriptor.
    case It_Kind_Pikachu_Thunder:
    case It_Kind_Pichu_Thunder:return {12,1,true};
    // Game & Watch's article records carry one source-owned joint descriptor
    // pointer (except Chef's typed 0x74-byte record); serialize the callback
    // state rows individually from it_3F2F.c's source tables.
    case It_Kind_GameWatch_Greenhouse:return {4,4,true};
    case It_Kind_GameWatch_Manhole:return {4,1,true};
    case It_Kind_GameWatch_Fire:return {4,1,true};
    case It_Kind_GameWatch_Parachute:return {4,2,true};
    case It_Kind_GameWatch_Turtle:return {4,2,true};
    case It_Kind_GameWatch_Breath:return {4,2,true};
    case It_Kind_GameWatch_Judge:return {4,1,true};
    case It_Kind_GameWatch_Panic:return {4,2,true};
    case It_Kind_GameWatch_Chef:return {0x74,2,true};
    // The copied chef uses the same authored attributes/state layout, but its
    // Article and model are owned by PlKbCpGw.dat rather than PlGw.dat.
    case It_Kind_Kirby_GameWatchChef:return {0x74,2,true};
    // The copy Pan Article has a source-owned visibility/render descriptor but
    // no serialized animation rows; its callback table is the source C table.
    case It_Kind_Kirby_GameWatchChefPan:return {4,0,true};
    case It_Kind_GameWatch_Rescue:return {4,2,true};
    // Kirby's ftKb_Init_OnLoad Article order. Cutter Beam has four float
    // scalars, Hammer and Unk2 have no special record. The PlKb DAT's Unk1
    // Article authors only its consumed lifetime float; Unk2's common ItemAttr
    // begins immediately at the following word and is a separate root.
    case It_Kind_Kirby_CBeam:return {0x10,1,true};
    case It_Kind_Kirby_Hammer:return {0,1,false};
    case It_Kind_Unk1:return {4,1,true};
    case It_Kind_Unk2:return {0,1,false};
    // Popo's serialized source state arrays are one row for Ice and Blizzard,
    // and absent for the GumStrings Article. Their special records retain the
    // exact itCharItems.h layouts, including GumStrings' two source Joints.
    case It_Kind_IceClimber_Ice:return {0x34,1,true};
    // Kirby's copied Ice shot uses itClimbersice.c with the same authored
    // itClimbersIceAttributes and state row, but has a distinct ItemKind.
    case It_Kind_Kirby_IceClimberIce:return {0x34,1,true};
    case It_Kind_IceClimber_Blizzard:return {0x14,1,true};
    case It_Kind_IceClimber_GumStrings:return {0x2C,0,true};
    // Samus's four source Article roots are registered in ftSs_Init_OnLoad.
    // These are serialized descriptor rows (not callback state counts): Bomb
    // carries two, Charge Shot nine, Missile four, and Grapple Beam none.
    case It_Kind_Samus_Bomb:return {0x1C,2,true};
    case It_Kind_Samus_Charge:
    case It_Kind_Kirby_SamusCharge:return {0x20,9,true};
    case It_Kind_Samus_Missile:return {0x40,4,true};
    case It_Kind_Samus_GBeam:return {0xB0,0,true};
    // Yoshi's Egg Throw and Star use their exact float pairs with two and
    // one serialized animation rows. Egg Lay's spawning callback supplies
    // its attributes directly, and its Article owns the x48[3] joint root.
    case It_Kind_Yoshi_EggThrow:return {8,2,true};
    case It_Kind_Yoshi_Star:return {8,1,true};
    case It_Kind_Yoshi_EggLay:return {0,0,false};
    // Zelda's Din Fire projectile/explosion consume their exact authored
    // twelve-float and five-float source records.
    case It_Kind_Zelda_DinFire:return {0x30,2,true};
    case It_Kind_Zelda_DinFire_Explode:return {0x14,1,true};
    // Sheik's Needle, held Needle, smoke and Chain use the source item tables;
    // the Chain retains its two model joints in its 0x6C attribute record.
    case It_Kind_Seak_NeedleThrow:return {0x0C,5,true};
    case It_Kind_Seak_NeedleHeld:return {4,1,true};
    case It_Kind_Seak_Vanish:return {0,1,false};
    case It_Kind_Seak_Chain:return {0x6C,0,true};
    case It_Kind_Pikachu_TJolt_Ground:
    case It_Kind_Pichu_TJolt_Ground:return {16,2,true};
    case It_Kind_Pikachu_TJolt_Air:
    case It_Kind_Pichu_TJolt_Air:return {4,1,true};
    case It_Kind_Mario_Cape:
    case It_Kind_DrMario_Sheet:return {4,2,true};
    case It_Kind_Fox_Laser:
    case It_Kind_Falco_Laser:
    case It_Kind_Kirby_FoxLaser:return {40,2,true};
    case It_Kind_Fox_Blaster:
    case It_Kind_Falco_Blaster:
    case It_Kind_Kirby_FoxBlaster:return {40,9,true};
    case It_Kind_Fox_Illusion:
    case It_Kind_Falco_Phantasm:return {8,3,true};
    case It_Kind_Heiho:return {24,3,true};
    // Serialized animation descriptors, not ItemStateTable callback counts.
    // Bomb also selects row 3 directly for its fuse; Hookshot uses only -1
    // animation IDs. Link and Young Link retain distinct ItemKinds.
    case It_Kind_Link_Bomb:
    case It_Kind_CLink_Bomb:return {0x34,4,true};
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return {0x64,3,true};
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return {0x60,0,true};
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return {0x2c,1,true};
    case It_Kind_Link_Bow:
    case It_Kind_CLink_Bow:return {8,6,false};
    case It_Kind_CLink_Milk:return {4,2,false};
    default:throw DatError("Item kind has no checked native article schema");
    }
}
bool pointer_field(uint32_t kind,uint32_t offset)
{
    switch(kind) {
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return offset==0x54||offset==0x58||offset==0x5c;
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return offset==0x44||offset==0x48||
        (offset>=0x4c&&offset<0x64&&((offset-0x4c)%4==0));
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return offset==0x24||offset==0x28;
    case It_Kind_GameWatch_Greenhouse:
    case It_Kind_GameWatch_Manhole:
    case It_Kind_GameWatch_Fire:
    case It_Kind_GameWatch_Parachute:
    case It_Kind_GameWatch_Turtle:
    case It_Kind_GameWatch_Breath:
    case It_Kind_GameWatch_Judge:
    case It_Kind_GameWatch_Panic:
    case It_Kind_GameWatch_Chef:
    case It_Kind_Kirby_GameWatchChef:
    case It_Kind_Kirby_GameWatchChefPan:
    case It_Kind_GameWatch_Rescue:return offset==0;
    // The Yo-Yo's serialized record ends with the original HSD_Joint string,
    // HSD_Joint yoyo and HSD_MatAnimJoint material pointers. The asset owner
    // hydrates them into native descriptors below.
    case It_Kind_Ness_Yoyo:return offset==0x50||offset==0x54||offset==0x58;
    case It_Kind_IceClimber_GumStrings:return offset==0x24||offset==0x28;
    case It_Kind_Seak_Chain:return offset==0x64||offset==0x68;
    case It_Kind_Samus_GBeam:return offset>=0x64&&offset<=0xAC&&((offset-0x64)%4==0);
    default:return false;
    }
}
bool float_field(uint32_t kind,uint32_t offset)
{
    if(pointer_field(kind,offset))return false;
    switch(kind) {
    case It_Kind_Link_Bomb:
    case It_Kind_CLink_Bomb:return offset!=0x0&&offset!=0xc&&offset!=0x10;
    case It_Kind_Link_Boomerang:
    case It_Kind_CLink_Boomerang:return offset>=0xc&&offset<0x44;
    case It_Kind_Link_HShot:
    case It_Kind_CLink_HShot:return offset!=0xc&&offset!=0x2c;
    case It_Kind_Link_Arrow:
    case It_Kind_CLink_Arrow:return true;
    // The Yo-Yo record keeps its original integer words (string segment
    // counts at 0x00..0x08, rotation frames at 0x40..0x4C) beside the float
    // scalars at 0x0C..0x3C.
    case It_Kind_Ness_Yoyo:return offset>=0xC&&offset<=0x3C;
    case It_Kind_IceClimber_Ice:return offset<0x20||offset==0x24||offset==0x28;
    case It_Kind_Kirby_IceClimberIce:return offset<0x20||offset==0x24||offset==0x28;
    case It_Kind_IceClimber_Blizzard:return true;
    case It_Kind_IceClimber_GumStrings:return offset==0x8||offset==0xC||offset==0x14;
    case It_Kind_Seak_Chain:return offset>=0x10&&offset<=0x60;
    case It_Kind_Samus_Charge:return offset!=0x4;
    case It_Kind_Kirby_SamusCharge:return offset!=0x4;
    case It_Kind_Samus_GBeam:return offset!=0xC&&offset!=0x34;
    default:return true;
    }
}
void write_native_pointer(void* destination,std::size_t offset,void* value)
{
    require(sizeof(void*)==4,"Native item descriptor requires the 32-bit gameplay target");
    const auto address=reinterpret_cast<std::uintptr_t>(value);
    require(address<=UINT32_MAX,"Native item descriptor pointer exceeds source width");
    const auto encoded=static_cast<uint32_t>(address);
    std::memcpy(static_cast<uint8_t*>(destination)+offset,&encoded,sizeof(encoded));
}
uint8_t reverse_source_bit_order(uint8_t value)
{
    value=static_cast<uint8_t>(((value&0x55U)<<1U)|((value&0xAAU)>>1U));
    value=static_cast<uint8_t>(((value&0x33U)<<2U)|((value&0xCCU)>>2U));
    return static_cast<uint8_t>((value<<4U)|(value>>4U));
}
void* hydrate_gamewatch_visibility(const MeleeWebNativeDat* r,
                                   const DatArchive& archive,
                                   uint32_t source, uint32_t joint_count)
{
    struct NativeVisibility {
        uint16_t x0, pad2;
        uint8_t* x4;
        uint16_t x8, padA;
        uint8_t* xC;
        int32_t x10, x14;
        uint8_t x18, pad19[3];
        float x1C, x20, x24, x28, x2C, x30, x34, x38;
    };
    static_assert(sizeof(void*) == 4 && sizeof(NativeVisibility) == 0x3C);
    /* Article.x4 points at the relocated self-pointer in x10. That pointer
     * names a source-endian descriptor; item rendering consumes its two
     * count/index lists, flags and collision vectors as native fields. */
    (void) archive.range(source, sizeof(NativeVisibility));
    auto* vars = static_cast<NativeVisibility*>(
        r->allocate(r->context, 1, sizeof(NativeVisibility)));
    auto copy_indices = [&](uint32_t count_offset, uint32_t pointer_offset,
                            uint16_t& count, uint8_t*& indices) {
        count = r->half(r->context, source + count_offset);
        require(count <= 100 && count <= joint_count,
                "Game & Watch visibility list exceeds its source model");
        const uint32_t target = r->pointer(r->context, source + pointer_offset, count);
        if (count == 0) {
            require(target == UINT32_MAX,
                    "Empty Game & Watch visibility list has a source pointer");
            indices = NULL;
            return;
        }
        require(target != UINT32_MAX,
                "Game & Watch visibility list pointer is missing");
        (void) archive.range(target, count);
        indices = static_cast<uint8_t*>(
            r->allocate(r->context, count, sizeof(uint8_t)));
        for (uint16_t i = 0; i < count; ++i) {
            indices[i] = r->byte(r->context, target + i);
            require(indices[i] < joint_count,
                    "Game & Watch visibility bone exceeds its source model");
        }
    };
    copy_indices(0, 4, vars->x0, vars->x4);
    copy_indices(8, 12, vars->x8, vars->xC);
    require(r->pointer(r->context, source + 0x10, sizeof(NativeVisibility)) == source,
            "Game & Watch visibility descriptor lost its authored self-pointer");
    write_native_pointer(vars, offsetof(NativeVisibility, x10), vars);
    vars->x14 = static_cast<int32_t>(r->word(r->context, source + 0x14));
    // UnkFlagStruct's b0..b7 are MSB-first in the PPC source ABI. This build's
    // native C bitfields are LSB-first, so map the authored bit identities,
    // not merely the byte value.
    vars->x18 = reverse_source_bit_order(r->byte(r->context, source + 0x18));
    constexpr uint32_t collision_offsets[]{0x1C,0x20,0x24,0x28,
                                            0x2C,0x30,0x34,0x38};
    float* collision_fields[]{&vars->x1C,&vars->x20,&vars->x24,&vars->x28,
                              &vars->x2C,&vars->x30,&vars->x34,&vars->x38};
    for(size_t i=0;i<8;++i) {
        const uint32_t offset=collision_offsets[i];
        if(archive.has_relocation(source+offset)) {
            // The copied Pan's authored render record uses relocation-backed
            // words in this tail. Source bitfield order says its collision
            // overlay is disabled; retain each pointer identity for fidelity,
            // and reject a future source record that makes that overlay live.
            require((vars->x18&0x03U)==0,
                    "Game & Watch collision pointer is live as a source scalar");
            const auto target=archive.pointer(source+offset,0);
            require(target.has_value(),
                    "Game & Watch collision pointer target is missing");
            void* identity=const_cast<uint8_t*>(archive.range(*target,0).data());
            write_native_pointer(collision_fields[i],0,identity);
            continue;
        }
        const uint32_t bits=r->word(r->context,source+offset);
        float value;
        std::memcpy(&value,&bits,sizeof(value));
        require(std::isfinite(value),
                "Game & Watch visibility scalar is nonfinite");
        *collision_fields[i]=value;
    }
    return vars;
}
}
struct DatItemArticle::Storage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    std::unique_ptr<DatNativeJoint> model;
    std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy)> native{nullptr,destroy};
    void* model_joint=nullptr;
    std::vector<std::unique_ptr<DatNativeJoint>> special_models;
    using SpecialNative = std::unique_ptr<MeleeWebNativeJoint,decltype(&destroy)>;
    std::vector<SpecialNative> special_native;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    // Samus's itSamusGrappleAttributes stores HSD_AnimJoint**,
    // HSD_MatAnimJoint**, and HSD_ShapeAnimJoint**. Preserve those source
    // pointer-to-pointer owners for every item-user lifetime.
    std::vector<std::unique_ptr<void*>> indirect_animation_slots;
    DatItemCommands commands;
    std::unique_ptr<MeleeWebItemStateDesc[]> states;
    uint32_t count=0;
    explicit Storage(std::shared_ptr<const DatArchive> a):archive(a),arena(std::move(a)){}
};
DatItemArticle::DatItemArticle(std::shared_ptr<const DatArchive> archive,uint32_t root,uint32_t kind,void* article)
    :storage_(std::make_unique<Storage>(archive))
{
    require(bool(archive)&&article,"Item article requires owned source and registered identity");
    const auto article_schema=schema(kind);
    auto& s=*storage_;const auto& a=*archive;char error[256];
    auto record=[&](uint32_t at,size_t size){
        if((at&3)||size>a.next_target_offset(at)-at)
            throw DatError("Item kind "+std::to_string(kind)+" descriptor at "+std::to_string(at)+
                           " crosses source region (requested "+std::to_string(size)+" bytes)");
        (void)a.range(at,size);
    };
    auto record_special=[&](uint32_t at,size_t size){
        // Bomb/Missile command and state roots can target words inside their
        // source-declared itSamus*Attributes records. DAT target intervals
        // therefore are not allocation extents for these four known source
        // structs; keep this exception restricted to the exact pinned schemas
        // and still require the complete bytes to exist in the archive.
        const bool samus=kind==It_Kind_Samus_Bomb||kind==It_Kind_Samus_Charge||
            kind==It_Kind_Samus_Missile||kind==It_Kind_Samus_GBeam;
        if(samus) (void)a.range(at,size);
        else record(at,size);
    };
    auto pointer=[&](uint32_t at,size_t size){
        auto p=a.pointer(at,size);
        if(!p) {
            throw DatError("Required item descriptor missing at DAT slot " +
                           std::to_string(at) + " (" + std::to_string(size) +
                           " bytes, item kind " + std::to_string(kind) + ")");
        }
        record(*p,size);return *p;
    };
    record(root,24);const uint32_t special_size=article_schema.special_bytes;
    std::optional<uint32_t> special_at;
    if(special_size) {
        special_at=a.pointer(root+4,special_size);
        require(special_at||!article_schema.special_required,"Required item special attributes missing");
        if(special_at)record_special(*special_at,special_size);
    }
    const auto* reader=s.arena.reader();
    void* special=special_size&&special_at?
        reader->allocate(reader->context,special_size/4,4):nullptr;
    std::optional<uint32_t> gamewatch_visibility_source;
    uint32_t first_special_word=0;
    if(kind==It_Kind_Heiho&&special_at){
        require(sizeof(void*)==4,"Heiho Article hydration requires the 32-bit gameplay target");
        const uint32_t scalar_at=pointer(*special_at,4);
        require(!a.has_relocation(scalar_at),"Heiho threshold scalar is relocated");
        auto* scalar=static_cast<int32_t*>(reader->allocate(reader->context,1,sizeof(int32_t)));
        const uint32_t value=a.be32(scalar_at);std::memcpy(scalar,&value,4);
        require(*scalar>=0&&*scalar<=10000,"Heiho threshold scalar is outside source bounds");
        std::memcpy(special,&scalar,sizeof(scalar));
        first_special_word=4;
    }
    if(special_at) for(uint32_t i=first_special_word;i<special_size;i+=4){
        if(pointer_field(kind,i))continue;
        const auto scalar_at=*special_at+i;
        require(!a.has_relocation(scalar_at),"Item special scalar is relocated");
        uint32_t value=a.be32(scalar_at);
        if(float_field(kind,i))require(std::isfinite(a.f32(scalar_at)),"Item special scalar is nonfinite");
        std::memcpy(static_cast<uint8_t*>(special)+i,&value,4);
    }
    const bool gamewatch_item=kind==It_Kind_GameWatch_Greenhouse||
        kind==It_Kind_GameWatch_Manhole||kind==It_Kind_GameWatch_Fire||
        kind==It_Kind_GameWatch_Parachute||kind==It_Kind_GameWatch_Turtle||
        kind==It_Kind_GameWatch_Breath||kind==It_Kind_GameWatch_Judge||
        kind==It_Kind_GameWatch_Panic||kind==It_Kind_GameWatch_Chef||
        kind==It_Kind_Kirby_GameWatchChef||
        kind==It_Kind_Kirby_GameWatchChefPan||kind==It_Kind_GameWatch_Rescue;
    if(gamewatch_item) {
        require(special_at.has_value(),"Game & Watch Article has no special record");
        const auto payload=a.pointer(*special_at,0x3C);
        require(payload.has_value(),"Game & Watch Article special payload is missing");
        gamewatch_visibility_source=*payload;
    }
    uint32_t at=pointer(root+16,16);
    const auto model_root=a.pointer(at,64);
    const bool null_model=!model_root;
    const uint32_t bones=a.be32(at+4);const int32_t attach=int32_t(a.be32(at+8));
    const uint8_t flags=a.range(at+12,1)[0];
    require(!a.has_relocation(at+4)&&!a.has_relocation(at+8)&&!a.has_relocation(at+12),"Item model scalar is relocated");
    MeleeWebNativeGraph empty_graph{};
    const MeleeWebNativeGraph* graph_ptr=&empty_graph;
    void* joint=nullptr;
    std::vector<void*> descriptors;
    if(null_model) {
        /* item.c copies this authored null to xC8_joint; Item_802680CC then
         * creates the source identity JObj. Keep the model descriptor and
         * its scalar fields while requiring the zero-bone form below. */
        require(bones==0&&attach==0,
                "Null item model has nonzero bone or attachment fields");
    } else {
        record(*model_root,64);
        s.model=std::make_unique<DatNativeJoint>(archive,*model_root);
        graph_ptr=&s.model->graph();
        const auto& graph=*graph_ptr;
        require((bones==0||bones==graph.joint_count)&&graph.joint_count<=140,
                "Item model bone count does not match native topology");
        require(attach>=0&&uint32_t(attach)<graph.joint_count,
                "Item attachment bone is outside native model");
        s.native.reset(melee_web_native_joint_hydrate(&graph,error,sizeof(error)));
        require(bool(s.native),error);
        joint=melee_web_native_joint_descriptor(s.native.get(),error,sizeof(error));
        require(joint,error);
        s.model_joint=joint;
        for(uint32_t j=0;j<graph.joint_count;j++){
            void* d=melee_web_native_joint_descriptor_at(s.native.get(),j,graph.joints[j].source_offset,error,sizeof(error));require(d,error);descriptors.push_back(d);
        }
    }
    const auto& graph=*graph_ptr;
    if(gamewatch_visibility_source) {
        void* visibility=hydrate_gamewatch_visibility(reader,a,
            *gamewatch_visibility_source,graph.joint_count);
        write_native_pointer(special,0,visibility);
    }

    auto special_joint=[&](uint32_t offset)->DatNativeJoint* {
        require(bool(special_at),"Item special joint has no special attributes");
        const auto target=pointer(*special_at+offset,64);
        auto model=std::make_unique<DatNativeJoint>(archive,target);
        Storage::SpecialNative owner(nullptr,destroy);owner.reset(melee_web_native_joint_hydrate(
            &model->graph(),error,sizeof(error)));require(bool(owner),error);
        void* descriptor=melee_web_native_joint_descriptor(owner.get(),error,sizeof(error));require(descriptor,error);
        write_native_pointer(special,offset,descriptor);
        DatNativeJoint* result=model.get();s.special_models.push_back(std::move(model));
        s.special_native.push_back(std::move(owner));return result;
    };
    auto optional_special_joint=[&](uint32_t offset)->DatNativeJoint* {
        require(bool(special_at),"Optional item special joint has no special attributes");
        if(!a.pointer(*special_at+offset,4))return nullptr;
        return special_joint(offset);
    };
    auto hydrate_joint_descriptors=[&](const MeleeWebNativeGraph& value,MeleeWebNativeJoint* owner) {
        std::vector<void*> result;result.reserve(value.joint_count);
        for(uint32_t j=0;j<value.joint_count;j++) {
            void* descriptor=melee_web_native_joint_descriptor_at(owner,j,value.joints[j].source_offset,error,sizeof(error));
            require(descriptor,error);result.push_back(descriptor);
        }
        return result;
    };
    auto special_animation=[&](uint32_t slot,const MeleeWebNativeGraph& value,
                               MeleeWebNativeJoint* owner) {
        if(auto target=a.pointer(*special_at+slot,20)) {
            auto descriptors=hydrate_joint_descriptors(value,owner);
            auto animation=std::make_unique<DatNativeAnimation>(archive,*target,value,
                DatNativeAnimationPolicy::Transforms,descriptors);
            void* descriptor=animation->descriptor();require(descriptor,"Item animation descriptor is null");
            write_native_pointer(special,slot,descriptor);s.animations.push_back(std::move(animation));
        }
    };
    auto special_material=[&](uint32_t slot,const MeleeWebNativeGraph& value) {
        if(auto target=a.pointer(*special_at+slot,12)) {
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*target,value);
            void* descriptor=animation->descriptor();require(descriptor,"Item material descriptor is null");
            write_native_pointer(special,slot,descriptor);s.materials.push_back(std::move(animation));
        }
    };
    auto special_shape=[&](uint32_t slot,const MeleeWebNativeGraph& value) {
        if(auto target=a.pointer(*special_at+slot,12)) {
            auto animation=std::make_unique<DatShapeAnimation>(archive,*target,value);
            void* descriptor=animation->descriptor();require(descriptor,"Item shape descriptor is null");
            write_native_pointer(special,slot,descriptor);s.shapes.push_back(std::move(animation));
        }
    };
    auto indirect_special_animation=[&](uint32_t slot,const MeleeWebNativeGraph& value,
                                         MeleeWebNativeJoint* owner,unsigned type) {
        require(bool(special_at),"Indirect item animation has no special attributes");
        if(auto target=a.pointer(*special_at+slot,20)) {
            auto descriptors=hydrate_joint_descriptors(value,owner);
            void* descriptor=nullptr;
            if(type==0) {
                auto animation=std::make_unique<DatNativeAnimation>(archive,*target,value,
                    DatNativeAnimationPolicy::Transforms,descriptors);
                descriptor=animation->descriptor();s.animations.push_back(std::move(animation));
            } else if(type==1) {
                auto animation=std::make_unique<DatMaterialAnimation>(archive,*target,value);
                descriptor=animation->descriptor();s.materials.push_back(std::move(animation));
            } else {
                auto animation=std::make_unique<DatShapeAnimation>(archive,*target,value);
                descriptor=animation->descriptor();s.shapes.push_back(std::move(animation));
            }
            require(descriptor,"Indirect item animation descriptor is null");
            auto cell=std::make_unique<void*>(descriptor);
            write_native_pointer(special,slot,cell.get());
            s.indirect_animation_slots.push_back(std::move(cell));
        }
    };
    if(kind==It_Kind_Link_HShot||kind==It_Kind_CLink_HShot) {
        for(const auto offset:{0x54U,0x58U,0x5cU}) (void)special_joint(offset);
    } else if(kind==It_Kind_Link_Arrow||kind==It_Kind_CLink_Arrow) {
        for(const auto offset:{0x24U,0x28U}) (void)special_joint(offset);
    } else if(kind==It_Kind_Link_Boomerang||kind==It_Kind_CLink_Boomerang) {
        auto first=special_joint(0x44),second=special_joint(0x48);
        MeleeWebNativeJoint* first_native=nullptr;MeleeWebNativeJoint* second_native=nullptr;
        /* Find the matching native owners by the stable model graph pointer. */
        for(size_t i=0;i<s.special_models.size();++i) {
            if(s.special_models[i].get()==first)first_native=s.special_native[i].get();
            if(s.special_models[i].get()==second)second_native=s.special_native[i].get();
        }
        require(first_native&&second_native,"Boomerang special joint owner is missing");
        special_animation(0x4c,first->graph(),first_native);
        special_material(0x50,first->graph());special_shape(0x54,first->graph());
        special_animation(0x58,second->graph(),second_native);
        special_material(0x5c,second->graph());special_shape(0x60,second->graph());
    } else if(kind==It_Kind_Ness_Yoyo) {
        /* it_802BE65C loads the string and yoyo joints from the special
         * record and it_802BE5D8 attaches the material animation to the
         * loaded yoyo joint, so the material binds that joint's graph. */
        auto string_joint=special_joint(0x50);
        auto yoyo_joint=special_joint(0x54);
        (void)string_joint;
        special_material(0x58,yoyo_joint->graph());
    } else if(kind==It_Kind_IceClimber_GumStrings) {
        (void)special_joint(0x24);
        (void)special_joint(0x28);
    } else if(kind==It_Kind_Seak_Chain) {
        (void)special_joint(0x64);
        (void)special_joint(0x68);
    } else if(kind==It_Kind_Samus_GBeam) {
        const uint32_t joints[]={0x64,0x68,0x6C,0x70};
        const uint32_t anims[]={0x74,0x80,0x8C,0x98};
        const uint32_t mats[]={0x78,0x84,0x90,0x9C};
        const uint32_t shapes[]={0x7C,0x88,0x94,0xA0};
        for(unsigned i=0;i<4;i++) {
            auto model=optional_special_joint(joints[i]);
            if(!model) {
                require(!a.pointer(*special_at+anims[i])&&!a.pointer(*special_at+mats[i])&&
                        !a.pointer(*special_at+shapes[i]),
                        "Grapple Beam animation points to an absent source joint");
                continue;
            }
            MeleeWebNativeJoint* owner=nullptr;
            for(size_t j=0;j<s.special_models.size();j++)
                if(s.special_models[j].get()==model)owner=s.special_native[j].get();
            require(owner,"Grapple Beam joint owner is missing");
            indirect_special_animation(anims[i],model->graph(),owner,0);
            indirect_special_animation(mats[i],model->graph(),owner,1);
            indirect_special_animation(shapes[i],model->graph(),owner,2);
        }
        // The final triplet animates the Article's own model graph.
        indirect_special_animation(0xA4,graph,s.native.get(),0);
        indirect_special_animation(0xA8,graph,s.native.get(),1);
        indirect_special_animation(0xAC,graph,s.native.get(),2);
    }
    s.count=article_schema.state_count;
    if(s.count){at=pointer(root+12,s.count*16);s.states=std::make_unique<MeleeWebItemStateDesc[]>(s.count);}
    else require(!a.pointer(root+12),"Animation-free item has an unexpected state descriptor table");
    for(uint32_t i=0;i<s.count;i++){
        const uint32_t row=at+16*i;auto& state=s.states[i];
        if(auto p=a.pointer(row,20)){
            require(!null_model,"Null item model has an animation descriptor");
            // Item animation containers use the original particle callback
            // channel for source effects (Mario's fireball and cape each carry
            // one). Preserve those packed event tracks so the source HSD
            // updater can dispatch them after the matching effect bank lives.
            auto animation=std::make_unique<DatNativeAnimation>(archive,*p,graph,DatNativeAnimationPolicy::ParticleDescriptors,descriptors);
            state.animation=animation->descriptor();s.animations.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+4,12)){
            require(!null_model,"Null item model has a material descriptor");
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*p,graph);
            state.material=animation->descriptor();s.materials.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+8,12)){
            require(!null_model,"Null item model has a shape descriptor");
            auto animation=std::make_unique<DatShapeAnimation>(archive,*p,graph);
            state.shape=animation->descriptor();s.shapes.push_back(std::move(animation));
        }
        if(auto p=a.pointer(row+12,4))state.commands=s.commands.decode(a,*p);
    }
    require(melee_web_article_publish(reader,article,special,s.states.get(),s.count,joint,bones,attach,flags,
                                      null_model?1:0,error,sizeof(error)),error);
}
DatItemArticle::~DatItemArticle()=default;
uint32_t DatItemArticle::state_count()const noexcept{return storage_->count;}
void* DatItemArticle::model_joint_descriptor()const noexcept{return storage_->model_joint;}
}
