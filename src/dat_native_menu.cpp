#include "dat_native_menu.hpp"
#include "dat_native_animation.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "dat_texture.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/sobjlib.h>
#include <sysdolphin/baselib/tobj.h>
#include <melee/sc/types.h>
#pragma GCC diagnostic pop
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <string_view>
namespace melee_web {
namespace { void require(bool value,const char* message){if(!value)throw DatError(message);} }
struct DatNativeMenu::Storage {
    // CSSSceneModels + CSSAnimSet[] and the SSS table share this source prefix.
    struct Scene {
        HSD_CObjDesc* camera;
        HSD_LightDesc* light0;
        HSD_LightDesc* light1;
        HSD_FogDesc* fog;
        StaticModelDesc models[20];
    } scene{};
#ifdef __wasm__
    static_assert(offsetof(Scene,models)==0x10);
    static_assert(sizeof(StaticModelDesc)==0x10);
#endif
    std::shared_ptr<const DatArchive> archive;
    std::vector<std::shared_ptr<void>> memory;
    std::map<uint32_t,HSD_CObjDesc*> cameras;
    std::map<uint32_t,HSD_LightDesc*> lights;
    std::set<uint32_t> active;
    std::map<uint32_t,HSD_LightAnim*> light_animations_by_offset;
    std::set<uint32_t> active_light_animations;
    std::map<std::string,void*,std::less<>> exports;
    std::vector<std::unique_ptr<HSD_LightAnim>> light_animations;
    std::vector<std::unique_ptr<HSD_SObjDesc>> sobjs;
    std::vector<std::unique_ptr<HSD_ImageDesc>> images;
    std::vector<std::unique_ptr<HSD_Tlut>> tluts;
    std::vector<std::unique_ptr<SceneDesc::LightList>> light_lists;
    std::vector<std::unique_ptr<DatNativeJoint>> graphs;
    std::vector<MeleeWebNativeJoint*> natives;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    unsigned count=0;
    ~Storage(){for(auto* native:natives)if(!melee_web_native_joint_destroy(native,nullptr,0))std::terminate();}
    template<class T>T* make(size_t n=1){require(n<=65536,"Native menu allocation budget");auto p=std::shared_ptr<T[]>(new T[n]{});auto* out=p.get();memory.emplace_back(p,out);return out;}
    void record(uint32_t o,size_t n){require(!(o&3),"Native menu record alignment");(void)archive->range(o,n);require(n<=archive->next_target_offset(o)-o,"Native menu record crosses referenced region");}
    uint32_t pointer(uint32_t slot,size_t n){auto p=archive->pointer(slot,n);require(bool(p),"Native menu required pointer missing");return *p;}
    const DatPublicSymbol& public_symbol(std::string_view name) const {
        for(const auto& symbol:archive->public_symbols())if(symbol.name==name)return symbol;
        throw DatError("Required original menu public export is missing: "+std::string(name));
    }
    uint32_t export_offset(std::string_view name) const { return public_symbol(name).data_offset; }
    void publish(std::string name,void* data) {
        require(data!=nullptr,"Original menu export did not hydrate to an owned descriptor");
        require(exports.emplace(std::move(name),data).second,"Duplicate original menu export");
    }
    float number(uint32_t o){float f=archive->f32(o);require(std::isfinite(f),"Nonfinite native menu scalar");return f;}
    Vec3* vector(uint32_t o){record(o,12);auto* v=make<Vec3>();v->x=number(o);v->y=number(o+4);v->z=number(o+8);return v;}
    HSD_WObjDesc* world(std::optional<uint32_t> o){if(!o)return nullptr;record(*o,20);require(!archive->pointer(*o)&&!archive->pointer(*o+16),"Native menu WObj class/constraints unsupported");auto* w=make<HSD_WObjDesc>();w->pos=*vector(*o+4);return w;}
    HSD_CObjDesc* camera(uint32_t o){
        if(cameras.contains(o))return cameras.at(o);require(cameras.size()<32,"Native menu camera count exceeds budget");record(o,48);require(!archive->pointer(o),"Custom menu camera class unsupported");auto* d=make<HSD_CObjDesc>();auto& c=d->common;
        c.flags=archive->be16(o+4);c.projection_type=archive->be16(o+6);require(c.projection_type>=1&&c.projection_type<=3,"Invalid menu camera projection");
        c.viewport={int16_t(archive->be16(o+8)),int16_t(archive->be16(o+10)),int16_t(archive->be16(o+12)),int16_t(archive->be16(o+14))};
        c.scissor={archive->be16(o+16),archive->be16(o+18),archive->be16(o+20),archive->be16(o+22)};
        c.eyepos=world(archive->pointer(o+24,20));c.interest=world(archive->pointer(o+28,20));c.roll=number(o+32);if(auto p=archive->pointer(o+36,12))c.up_vector=vector(*p);
        c.nnear=number(o+40);c.ffar=number(o+44);require(c.nnear>0&&c.ffar>c.nnear,"Invalid menu camera clip range");
        if(c.projection_type==1){record(o,56);d->perspective.fov=number(o+48);d->perspective.aspect=number(o+52);require(d->perspective.fov>0&&d->perspective.fov<180&&d->perspective.aspect>0,"Invalid menu perspective");}
        else{record(o,64);d->frustum.top=number(o+48);d->frustum.bottom=number(o+52);d->frustum.left=number(o+56);d->frustum.right=number(o+60);require(d->frustum.top!=d->frustum.bottom&&d->frustum.left!=d->frustum.right,"Degenerate menu projection");}
        cameras[o]=d;return d;
    }
    HSD_LightDesc* light(uint32_t o){
        if(lights.contains(o))return lights.at(o);require(lights.size()+active.size()<256,"Native menu light count exceeds budget");record(o,28);require(active.insert(o).second,"Native light descriptor cycle");require(!archive->pointer(o),"Custom native light class unsupported");auto* d=make<HSD_LightDesc>();d->flags=archive->be16(o+8);d->attnflags=archive->be16(o+10);
        const unsigned type=d->flags&LOBJ_TYPE_MASK;std::memcpy(&d->color,archive->range(o+12,4).data(),4);d->position=world(archive->pointer(o+16,20));d->interest=world(archive->pointer(o+20,20));
        const auto parameters=archive->pointer(o+24,4);
        if(type==LOBJ_POINT||type==LOBJ_SPOT){require(parameters.has_value(),"Native menu positional-light parameters missing");const auto base=*parameters;const bool raw=type==LOBJ_POINT?(d->attnflags&LOBJ_LIGHT_ATTN)!=0:d->attnflags!=0;
            if(raw){record(base,24);auto* value=make<HSD_LightAttn>();value->a0=number(base);value->a1=number(base+4);value->a2=number(base+8);value->k0=number(base+12);value->k1=number(base+16);value->k2=number(base+20);d->u.attn=value;}
            else if(type==LOBJ_POINT){record(base,12);auto* value=make<HSD_LightPointDesc>();value->ref_br=number(base);value->ref_dist=number(base+4);value->dist_func=archive->be32(base+8);require(value->dist_func<=GX_DA_STEEP,"Invalid native menu point-light function");d->u.point=value;}
            else{record(base,20);auto* value=make<HSD_LightSpotDesc>();value->cutoff=number(base);value->spot_func=archive->be32(base+4);value->ref_br=number(base+8);value->ref_dist=number(base+12);value->dist_func=archive->be32(base+16);require(value->spot_func<=GX_SP_RING2&&value->dist_func<=GX_DA_STEEP,"Invalid native menu spot-light function");d->u.spot=value;}
        }else if(parameters){d->u.shininess=make<float>();*d->u.shininess=number(*parameters);}
        if(auto p=archive->pointer(o+4,28))d->next=light(*p);active.erase(o);lights[o]=d;return d;
    }
    HSD_LightAnim* light_animation(uint32_t o){
        if(light_animations_by_offset.contains(o))return light_animations_by_offset.at(o);require(light_animations.size()+active_light_animations.size()<256,"Native menu light-animation count exceeds budget");record(o,16);require(active_light_animations.insert(o).second,"Native menu light-animation cycle");auto owner=std::make_unique<HSD_LightAnim>();auto* d=owner.get();
        if(auto p=archive->pointer(o,16))d->next=light_animation(*p);
        require(!archive->pointer(o+4),"Active native menu light AObj animation is unsupported");
        require(!archive->pointer(o+8,8),"Native menu light-position animation is unsupported");
        require(!archive->pointer(o+12,8),"Native menu light-interest animation is unsupported");
        light_animations.push_back(std::move(owner));light_animations_by_offset[o]=d;active_light_animations.erase(o);return d;
    }
    template<class Callback>std::vector<uint32_t> pointer_table(uint32_t o,size_t minimum,Callback&& callback,const char* message){
        const uint32_t end=archive->next_target_offset(o);require(end>o&&end-o>=4&&(end-o)%4==0,message);std::vector<uint32_t> values;
        for(size_t i=0;i<256;i++){const uint32_t slot=o+4*static_cast<uint32_t>(i);require(slot+4<=end,message);const auto target=archive->pointer(slot,minimum);if(!target){require(slot+4==end,message);return values;}values.push_back(*target);callback(*target);}
        throw DatError(message);
    }
    SceneDesc::LightList** light_list(uint32_t table){
        std::vector<SceneDesc::LightList*> values;
        pointer_table(table,8,[&](uint32_t root){record(root,8);auto item=std::make_unique<SceneDesc::LightList>();item->desc=light(pointer(root,28));
            if(auto animations=archive->pointer(root+4,4)){std::vector<HSD_LightAnim*> anims;pointer_table(*animations,16,[&](uint32_t animation){anims.push_back(light_animation(animation));},"Native menu light-animation table is invalid");auto** list=make<HSD_LightAnim*>(anims.size()+1);std::copy(anims.begin(),anims.end(),list);item->anims=list;}
            values.push_back(item.get());light_lists.push_back(std::move(item));},"Native menu light-list table is invalid");
        require(!values.empty(),"Native menu light-list table is empty");auto** result=make<SceneDesc::LightList*>(values.size()+1);std::copy(values.begin(),values.end(),result);return result;
    }
    DatNativeJoint* static_joint(std::string_view base){
        auto graph=std::make_unique<DatNativeJoint>(archive,export_offset(std::string(base)+"_joint"));char error[256]{};auto* native=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native,error[0]?error:"Original menu native joint hydration failed");natives.push_back(native);
        auto* joint=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native,error,sizeof(error)));require(joint,error[0]?error:"Original menu native joint descriptor is missing");publish(std::string(base)+"_joint",joint);
        auto* result=graph.get();graphs.push_back(std::move(graph));return result;
    }
    void static_model(std::string_view base){
        auto* graph=static_joint(base);
        const auto animation_name=std::string(base)+"_animjoint";auto animation=std::make_unique<DatNativeAnimation>(archive,export_offset(animation_name),graph->graph());auto* animation_descriptor=static_cast<HSD_AnimJoint*>(animation->descriptor());publish(animation_name,animation_descriptor);animations.push_back(std::move(animation));
        const auto material_name=std::string(base)+"_matanim_joint";auto material=std::make_unique<DatMaterialAnimation>(archive,export_offset(material_name),graph->graph(),TextureIndexValidation::DispatchedValues);auto* material_descriptor=static_cast<HSD_MatAnimJoint*>(material->descriptor());publish(material_name,material_descriptor);materials.push_back(std::move(material));
        const auto shape_name=std::string(base)+"_shapeanim_joint";auto shape=std::make_unique<DatShapeAnimation>(archive,export_offset(shape_name),graph->graph());publish(shape_name,shape->descriptor());shapes.push_back(std::move(shape));
    }
    void title_mark(){
        const auto root=export_offset("TitleMark_sobjdesc");record(root,8);const auto image_offset=pointer(root,24);const auto palette_offset=archive->pointer(root+4,16);const auto source_image=read_dat_texture_image(*archive,image_offset);
        auto image_desc=std::make_unique<HSD_ImageDesc>();image_desc->image_ptr=const_cast<uint8_t*>(source_image.bytes.data());image_desc->width=source_image.width;image_desc->height=source_image.height;image_desc->format=static_cast<GXTexFmt>(source_image.format);image_desc->mipmap=source_image.mipmap;image_desc->minLOD=source_image.min_lod;image_desc->maxLOD=source_image.max_lod;
        auto sobj=std::make_unique<HSD_SObjDesc>();sobj->image=image_desc.get();
        const bool indexed=source_image.format==8||source_image.format==9||source_image.format==10;
        if(indexed){require(palette_offset.has_value(),"Title mark indexed image has no authored TLUT");const auto palette=read_dat_texture_palette(*archive,*palette_offset,source_image);auto tlut=std::make_unique<HSD_Tlut>();tlut->lut=const_cast<uint8_t*>(palette.bytes.data());tlut->fmt=static_cast<GXTlutFmt>(palette.format);tlut->tlut_name=palette.source_name;tlut->n_entries=palette.entries;sobj->tlut=tlut.get();tluts.push_back(std::move(tlut));}
        else require(!palette_offset,"Title mark nonindexed image unexpectedly has a TLUT");
        images.push_back(std::move(image_desc));publish("TitleMark_sobjdesc",sobj.get());sobjs.push_back(std::move(sobj));
    }
    void build_flat(NativeMenuKind kind){
        static constexpr std::array<std::string_view,20> main_models={"MenMainBack_Top","MenMainPanel_Top","MenMainConTop_Top","MenMainCursor_Top","MenMainConRl_Top","MenMainCursorRl_Top","MenMainNmRl_Top","MenMainCursorTr01_Top","MenMainCursorTr02_Top","MenMainCursorTr03_Top","MenMainCursorTr04_Top","MenMainCursorRl01_Top","MenMainCursorRl02_Top","MenMainCursorRl03_Top","MenMainCursorRl04_Top","MenMainCursorRl05_Top","MenMainConIs_Top","MenMainCursorIs_Top","MenMainConSs_Top","MenMainCursorSs_Top"};
        static constexpr std::array<std::string_view,2> title_models={"TtlMoji_Top","TtlBg_Top"};
        if(kind==NativeMenuKind::Main){for(const auto name:main_models)static_model(name);count=static_cast<unsigned>(main_models.size());
            // mnVibration_Init loads these four roots directly from MnMaAll.usd
            // when the original Settings > Rumble route opens.
            for(const auto name:{"MenMainConVi_Top","MenMainCtlVi_Top","MenMainOnoffVi_Top"})static_model(name);
            static_joint("MenMainCursorVi_Top");
            // mnDeflicker_8024A6C4 loads the authored display setting when
            // Settings opens the original Display page.
            static_model("MenMainConDf_Top");
            publish("ScMenMain_cam_int1_camera",camera(export_offset("ScMenMain_cam_int1_camera")));
            publish("ScMenMain_scene_lights",light_list(export_offset("ScMenMain_scene_lights")));
            publish("ScMenMain_fog",fog(export_offset("ScMenMain_fog")));
        }else{
            for(const auto name:title_models)static_model(name);count=static_cast<unsigned>(title_models.size());
            publish("ScTitle_cam_int1_camera",camera(export_offset("ScTitle_cam_int1_camera")));
            publish("ScTitle_scene_lights",light_list(export_offset("ScTitle_scene_lights")));
            publish("ScTitle_fog",fog(export_offset("ScTitle_fog")));
            title_mark();
        }
    }
    HSD_FogDesc* fog(uint32_t o){record(o,20);auto* d=make<HSD_FogDesc>();d->type=archive->be32(o);require(d->type<=15,"Invalid source fog type");d->start=number(o+8);d->end=number(o+12);std::memcpy(&d->color,archive->range(o+16,4).data(),4);
        if(auto p=archive->pointer(o+4,68)){record(*p,68);auto* f=make<HSD_FogAdjDesc>();f->center=archive->be16(*p);f->width=archive->be16(*p+2);for(unsigned i=0;i<16;i++)f->mtx[i/4][i%4]=number(*p+4+4*i);d->fogadjdesc=f;}return d;}
};
DatNativeMenu::DatNativeMenu(std::shared_ptr<const DatArchive> archive,NativeMenuKind kind)
    :storage_(std::make_unique<Storage>()){
    auto& s=*storage_;require(bool(archive),"Native menu archive missing");s.archive=archive;
    if(kind==NativeMenuKind::Main||kind==NativeMenuKind::Title){s.build_flat(kind);return;}
    const auto name=kind==NativeMenuKind::Characters?"MnSelectChrDataTable":"MnSelectStageDataTable";
    std::optional<uint32_t> root;
    for(const auto& symbol:archive->public_symbols())if(symbol.name==name)root=symbol.data_offset;
    require(root.has_value(),"Native menu public scene table missing");
    s.count=kind==NativeMenuKind::Characters?9:12;
    s.record(*root,16+16*s.count);
    s.scene.camera=s.camera(s.pointer(*root,48));
    s.scene.light0=s.light(s.pointer(*root+4,28));
    s.scene.light1=s.light(s.pointer(*root+8,28));
    s.scene.fog=s.fog(s.pointer(*root+12,20));
    for(unsigned i=0;i<s.count;i++){
        const uint32_t row=*root+16+16*i;auto& out=s.scene.models[i];
        auto graph=std::make_unique<DatNativeJoint>(archive,s.pointer(row,64));
        char error[256];auto* native=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native,error);s.natives.push_back(native);
        out.joint=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native,error,sizeof(error)));require(out.joint,error);
        if(auto p=archive->pointer(row+4,20)){
            auto animation=std::make_unique<DatNativeAnimation>(archive,*p,graph->graph());
            out.animjoint=static_cast<HSD_AnimJoint*>(animation->descriptor());s.animations.push_back(std::move(animation));
        }
        if(auto p=archive->pointer(row+8,12)){
            auto animation=std::make_unique<DatMaterialAnimation>(archive,*p,graph->graph(),TextureIndexValidation::DispatchedValues);
            out.matanim_joint=static_cast<HSD_MatAnimJoint*>(animation->descriptor());s.materials.push_back(std::move(animation));
        }
        if(auto p=archive->pointer(row+12,12)){
            auto animation=std::make_unique<DatShapeAnimation>(archive,*p,graph->graph());
            out.shapeanim_joint=animation->descriptor();s.shapes.push_back(std::move(animation));
        }
        s.graphs.push_back(std::move(graph));
    }
}
DatNativeMenu::~DatNativeMenu()=default;
void* DatNativeMenu::descriptor()const noexcept{return &storage_->scene;}
unsigned DatNativeMenu::model_count()const noexcept{return storage_->count;}
void* DatNativeMenu::export_data(std::string_view name)const noexcept{const auto it=storage_->exports.find(name);return it==storage_->exports.end()?nullptr:it->second;}
}
