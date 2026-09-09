#include "dat_native_stage.hpp"
#include "dat_material_animation.hpp"
#include "dat_stage.hpp"
#include "dat_lights.hpp"
#include "gameplay_stage_numeric.h"
#include "gameplay_stage_profile.h"
#include "gameplay_content.h"
#include "gameplay_compat.h"
#include "hsd_animation_bridge.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include "gameplay_stage_map_build.h"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/spline.h>
#include <sysdolphin/baselib/wobj.h>
#pragma GCC diagnostic pop
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <utility>
namespace melee_web {
namespace {void require(bool c,const char* m){if(!c)throw DatError(m);}}
struct DatNativeStage::Storage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    DatStage metadata;
    std::vector<std::shared_ptr<void>> memory;
    std::vector<std::unique_ptr<DatNativeJoint>> graphs;
    std::vector<MeleeWebNativeJoint*> native;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<DatParticleEvent> events;
    std::vector<MeleeWebMapLightOverride> overrides;
    std::map<uint32_t,HSD_Joint*> joints;
    std::map<uint32_t,HSD_CObjDesc*> cameras;
    std::map<uint32_t,std::vector<HSD_MObjDesc*>> material_descriptors;
    std::map<uint32_t,HSD_LightDesc*> lights;
    std::map<uint32_t,HSD_AObjDesc*> aobjects;
    std::map<uint32_t,HSD_Spline*> splines;
    std::map<uint32_t,HSD_LightAnim*> light_animations;
    std::set<uint32_t> active;
    MeleeWebMapInput map{};void* native_map=nullptr;void* yaku=nullptr;
    explicit Storage(std::shared_ptr<const DatArchive> a):archive(a),arena(a),metadata(*a){}
    ~Storage(){for(auto* h:native)if(!melee_web_native_joint_destroy(h,nullptr,0))std::terminate();}
    template<class T>T* make(size_t count=1){require(count<=65536,"Native stage allocation count exceeds budget");auto p=std::shared_ptr<T[]>(new T[count]{});auto* out=p.get();memory.emplace_back(p,out);return out;}
    void record(uint32_t o,size_t n){
        require(!(o&3),"Native stage record is unaligned");
        (void)archive->range(o,n);
        const uint32_t end=archive->next_target_offset(o);
        if(n<=end-o)return;
        std::ostringstream message;
        message<<"Native stage record crosses referenced allocation (offset=0x"
               <<std::hex<<o<<", bytes=0x"<<n<<", allocation_end=0x"<<end<<")";
        throw DatError(message.str());
    }
    uint32_t pointer(uint32_t slot,size_t n){auto p=archive->pointer(slot,n);require(bool(p),"Native stage required pointer is null");return *p;}
    float number(uint32_t o){float f=archive->f32(o);require(std::isfinite(f),"Nonfinite native stage scalar");return f;}
    Vec3* vector(uint32_t o){record(o,12);auto* v=make<Vec3>();v->x=number(o);v->y=number(o+4);v->z=number(o+8);return v;}
    HSD_WObjDesc* world(std::optional<uint32_t> o){if(!o)return nullptr;record(*o,20);require(!archive->pointer(*o)&&!archive->pointer(*o+16),"Native stage WObj custom class/constraints unsupported");auto* w=make<HSD_WObjDesc>();w->pos=*vector(*o+4);return w;}
    HSD_AObjDesc* aobj(std::optional<uint32_t> o,uint64_t mask){
        if(!o)return nullptr;if(aobjects.contains(*o)){auto* existing=aobjects.at(*o);for(auto* track=existing->fobjdesc;track;track=track->next)require(mask&(UINT64_C(1)<<track->type),"Shared native scene animation channel has incompatible consumer");return existing;}require(aobjects.size()<4096,"Native scene AObj count exceeds budget");record(*o,16);
        auto* d=make<HSD_AObjDesc>();d->flags=archive->be32(*o);d->end_frame=number(*o+4);
        require(!(d->flags&~0x30000000u)&&d->end_frame>=0&&d->end_frame<=65535,"Native scene AObj flags/range unsupported");
        std::set<uint32_t> seen;auto f=archive->pointer(*o+8,20);HSD_FObjDesc** next=&d->fobjdesc;uint64_t channels=0;
        while(f){record(*f,20);require(seen.size()<32&&seen.insert(*f).second,"Native scene FObj cycle/count");auto* t=make<HSD_FObjDesc>();*next=t;next=&t->next;
            t->length=archive->be32(*f+4);t->startframe=number(*f+8);auto fields=archive->range(*f+12,4);t->type=fields[0];t->frac_value=fields[1];t->frac_slope=fields[2];
            require(t->type<64&&(mask&(UINT64_C(1)<<t->type))&&!(channels&(UINT64_C(1)<<t->type)),"Unsupported native scene animation channel");channels|=UINT64_C(1)<<t->type;
            require(t->startframe>=-32768&&t->startframe<=32767&&std::floor(t->startframe)==t->startframe&&t->length&&t->length<=65535,"Native scene FObj range invalid");
            uint32_t bytes=pointer(*f+16,t->length);require(t->length<=archive->next_target_offset(bytes)-bytes,"Native scene animation crosses referenced allocation");t->ad=make<u8>(t->length);auto span=archive->range(bytes,t->length);std::memcpy(t->ad,span.data(),span.size());
            MeleeWebAnimationTrack view{t->ad,t->length,0,1,t->frac_value,t->frac_slope};char error[256];require(melee_web_animation_validate_native_track(&view,error,sizeof(error)),error);f=archive->pointer(*f,20);
        }
        if(auto object=archive->pointer(*o+12,64)){
            require(mask==0xf0&&(channels&(UINT64_C(1)<<4)),"Native scene object reference requires a spline WObj track");
            auto* target=spline_joint(*object);const auto address=reinterpret_cast<uintptr_t>(target);
            require(address<=UINT32_MAX,"Native scene object ID requires 32-bit source pointers");d->obj_id=uint32_t(address);
        }else require(!(channels&(UINT64_C(1)<<4)),"Spline WObj animation lacks its original joint descriptor");
        aobjects[*o]=d;return d;
    }
    HSD_WObjAnim* world_anim(std::optional<uint32_t> o){if(!o)return nullptr;record(*o,8);require(!archive->pointer(*o+4),"Native scene WObj RObj animation unsupported");auto* w=make<HSD_WObjAnim>();w->aobjdesc=aobj(archive->pointer(*o,16),0xf0);return w;}
    HSD_CameraAnim* camera_anim(uint32_t o){record(o,12);auto* c=make<HSD_CameraAnim>();c->aobjdesc=aobj(archive->pointer(o,16),0x1eee);c->eye_anim=world_anim(archive->pointer(o+4,8));c->interest_anim=world_anim(archive->pointer(o+8,8));return c;}
    HSD_CObjDesc* camera(uint32_t o){
        if(cameras.contains(o))return cameras.at(o);require(cameras.size()<32,"Native stage camera count exceeds budget");record(o,48);require(!archive->pointer(o),"Custom stage camera class unsupported");auto* d=make<HSD_CObjDesc>();auto& c=d->common;
        c.flags=archive->be16(o+4);c.projection_type=archive->be16(o+6);require(c.projection_type>=1&&c.projection_type<=3,"Invalid stage camera projection");
        c.viewport={int16_t(archive->be16(o+8)),int16_t(archive->be16(o+10)),int16_t(archive->be16(o+12)),int16_t(archive->be16(o+14))};
        c.scissor={archive->be16(o+16),archive->be16(o+18),archive->be16(o+20),archive->be16(o+22)};
        c.eyepos=world(archive->pointer(o+24,20));c.interest=world(archive->pointer(o+28,20));c.roll=number(o+32);if(auto p=archive->pointer(o+36,12))c.up_vector=vector(*p);
        c.nnear=number(o+40);c.ffar=number(o+44);require(c.nnear>0&&c.ffar>c.nnear,"Invalid stage camera clip range");
        if(c.projection_type==1){record(o,56);d->perspective.fov=number(o+48);d->perspective.aspect=number(o+52);require(d->perspective.fov>0&&d->perspective.fov<180&&d->perspective.aspect>0,"Invalid stage perspective");}
        else{record(o,64);d->frustum.top=number(o+48);d->frustum.bottom=number(o+52);d->frustum.left=number(o+56);d->frustum.right=number(o+60);require(d->frustum.top!=d->frustum.bottom&&d->frustum.left!=d->frustum.right,"Degenerate stage projection");}
        cameras[o]=d;return d;
    }
    HSD_LightDesc* light(uint32_t o){
        if(lights.contains(o))return lights.at(o);require(lights.size()+active.size()<256,"Native stage light count exceeds budget");record(o,28);require(active.insert(o).second,"Native light descriptor cycle");require(!archive->pointer(o),"Custom native light class unsupported");auto* d=make<HSD_LightDesc>();d->flags=archive->be16(o+8);d->attnflags=archive->be16(o+10);
        require((d->flags&3)<=1,"Stage point/spot descriptor not implemented");std::memcpy(&d->color,archive->range(o+12,4).data(),4);d->position=world(archive->pointer(o+16,20));d->interest=world(archive->pointer(o+20,20));if(auto p=archive->pointer(o+24,4)){d->u.shininess=make<float>();*d->u.shininess=number(*p);}
        if(auto p=archive->pointer(o+4,28))d->next=light(*p);active.erase(o);lights[o]=d;return d;
    }
    HSD_LightAnim* light_anim(uint32_t o){
        if(light_animations.contains(o))return light_animations.at(o);require(light_animations.size()+active.size()<256,"Native stage light animation count exceeds budget");record(o,16);require(active.insert(o).second,"Native light animation cycle");auto* d=make<HSD_LightAnim>();d->aobjdesc=aobj(archive->pointer(o+4,16),0x7ffe);d->position_anim=world_anim(archive->pointer(o+8,8));d->interest_anim=world_anim(archive->pointer(o+12,8));if(auto p=archive->pointer(o,16))d->next=light_anim(*p);active.erase(o);light_animations[o]=d;return d;
    }
    template<class T,class F>T** table(uint32_t o,F hydrate){
        std::vector<T*> values;uint32_t end=archive->next_target_offset(o);
        for(uint32_t i=0;i<=64;i++){require(o+4*i<end,"Native scene pointer table lacks bounded terminator");auto p=archive->pointer(o+4*i,4);if(!p){auto** out=make<T*>(values.size()+1);std::copy(values.begin(),values.end(),out);return out;}require(i<64,"Native scene pointer table exceeds budget");values.push_back(hydrate(*p));}
        throw DatError("Native scene table is unterminated");
    }
    void** light_table(uint32_t o){return table<void>(o,[&](uint32_t p){record(p,8);auto* desc=light(pointer(p,28));HSD_LightAnim** anims=nullptr;if(auto a=archive->pointer(p+4,4))anims=table<HSD_LightAnim>(*a,[&](uint32_t q){return light_anim(q);});return melee_web_stage_map_light_list(arena.reader(),desc,anims);});}
    HSD_FogDesc* fog(uint32_t o){record(o,20);auto* d=make<HSD_FogDesc>();d->type=archive->be32(o);require(d->type<=15,"Invalid source fog type");d->start=number(o+8);d->end=number(o+12);std::memcpy(&d->color,archive->range(o+16,4).data(),4);
        if(auto p=archive->pointer(o+4,68)){record(*p,68);auto* f=make<HSD_FogAdjDesc>();f->center=archive->be16(*p);f->width=archive->be16(*p+2);for(unsigned i=0;i<16;i++)f->mtx[i/4][i%4]=number(*p+4+4*i);d->fogadjdesc=f;}return d;}
    HSD_Joint* spline_joint(uint32_t o){
        if(joints.contains(o))return joints.at(o);record(o,64);
        require(archive->be32(o+4)==0x4000&&!archive->pointer(o)&&!archive->pointer(o+8)&&!archive->pointer(o+12)&&!archive->pointer(o+56)&&!archive->pointer(o+60),"Native scene spline reference requires a simple spline joint");
        auto* d=make<HSD_Joint>();d->flags=0x4000;d->u.spline=spline(pointer(o+16,24));
        d->rotation=*vector(o+20);d->scale=*vector(o+32);d->position=*vector(o+44);
        require(d->scale.x!=0&&d->scale.y!=0&&d->scale.z!=0,"Native spline joint has singular scale");joints[o]=d;return d;
    }
    HSD_Spline* spline(uint32_t o){if(splines.contains(o))return splines.at(o);record(o,24);auto* d=make<HSD_Spline>();d->type=archive->range(o,1)[0];d->numcv=int16_t(archive->be16(o+2));require(d->type<=3&&d->numcv>=2&&d->numcv<=4096,"Invalid native stage spline type/count");d->tension=number(o+4);d->totalLength=number(o+12);require(d->totalLength>0,"Stage spline length must be positive");
        unsigned points=d->type==1?3*(d->numcv-1)+1:d->type>=2?d->numcv+2:d->numcv;uint32_t cv=pointer(o+8,points*12);record(cv,points*12);d->cv=make<Vec3>(points);for(unsigned i=0;i<points;i++)d->cv[i]=*vector(cv+12*i);
        uint32_t lengths=pointer(o+16,d->numcv*4);record(lengths,d->numcv*4);d->segLength=make<float>(d->numcv);for(int i=0;i<d->numcv;i++){d->segLength[i]=number(lengths+4*i);require(d->segLength[i]>=0&&d->segLength[i]<=1&&(!i||d->segLength[i]>d->segLength[i-1]),"Stage spline arc table is not strictly ordered");}require(d->segLength[0]==0&&d->segLength[d->numcv-1]==1,"Stage spline arc table endpoints invalid");
        if(auto p=archive->pointer(o+20,(d->numcv-1)*20)){record(*p,(d->numcv-1)*20);auto* values=make<float>((d->numcv-1)*5);d->segPoly=reinterpret_cast<float(*)[5]>(values);for(int i=0;i<(d->numcv-1)*5;i++)values[i]=number(*p+4*i);}else require(d->type==0,"Nonlinear stage spline requires arc polynomial");splines[o]=d;return d;}
};
DatNativeStage::DatNativeStage(std::shared_ptr<const DatArchive> archive)
    : DatNativeStage(std::move(archive), St_Kind_Last) {}

DatNativeStage::DatNativeStage(std::shared_ptr<const DatArchive> archive, int stage_kind)
    : storage_(std::make_unique<Storage>(archive)){
 auto& s=*storage_;const auto& a=*archive;const auto& meta=s.metadata;
 const auto* profile=melee_web_stage_profile(stage_kind);
 require(profile,"Native stage has no complete source callback profile");
 require(meta.entries.size()==profile->entry_count,"Native stage map entry count differs from source profile");
 require(profile->animation_count_count==profile->entry_count&&profile->animation_counts,
         "Native stage profile lacks animation consumer counts");
 auto* markers=melee_web_stage_markers_decode(s.arena.reader(),meta.root_offset);
 s.map.unkC=meta.entries.size();s.map.unk8=s.make<MeleeWebMapEntryInput>(s.map.unkC);
 for(const auto& e:meta.entries){auto& out=s.map.unk8[e.index];require(bool(e.joint_offset),"Native stage model missing");
  if(e.index==0){out.unk0=static_cast<HSD_Joint*>(melee_web_stage_markers_descriptor(markers));s.joints[*e.joint_offset]=out.unk0;}
  else{
   auto graph=std::make_unique<DatNativeJoint>(archive,*e.joint_offset);char error[256];auto* native=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native,error);s.native.push_back(native);
   out.unk0=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native,error,sizeof(error)));require(out.unk0,error);s.joints[*e.joint_offset]=out.unk0;
   std::vector<void*> native_joint_descriptors(graph->graph().joint_count);
   for(uint32_t i=0;i<graph->graph().joint_count;i++){
    native_joint_descriptors[i]=melee_web_native_joint_descriptor_at(
        native,i,graph->graph().joints[i].source_offset,error,sizeof(error));
    require(native_joint_descriptors[i],error);
   }
   for(uint32_t i=0;i<graph->graph().material_count;i++){auto* material=static_cast<HSD_MObjDesc*>(melee_web_native_joint_material_descriptor(native,i,error,sizeof(error)));require(material,error);s.material_descriptors[graph->graph().materials[i].source_offset].push_back(material);}
   // Source callbacks select animation slots per entry. These are consumer
   // counts, not inferred DAT extents; the complete map descriptor table above
   // is still hydrated for every archive entry.
   const unsigned count=profile->animation_counts[e.index];
   require(count>0&&count<=64,"Native stage profile has an invalid animation consumer count");
   out.unk4=s.make<HSD_AnimJoint*>(count+1);out.unk8=s.make<HSD_MatAnimJoint*>(count+1);
   for(unsigned i=0;i<count;i++){
    if(e.joint_animation_table){s.record(*e.joint_animation_table,count*4);if(auto p=a.pointer(*e.joint_animation_table+4*i,20)){
     auto anim=std::make_unique<DatNativeAnimation>(archive,*p,graph->graph(),DatNativeAnimationPolicy::ParticleDescriptors,native_joint_descriptors);out.unk4[i]=static_cast<HSD_AnimJoint*>(anim->indexed_descriptor());s.events.insert(s.events.end(),anim->particle_events().begin(),anim->particle_events().end());s.animations.push_back(std::move(anim));}}
    if(e.material_animation_table){s.record(*e.material_animation_table,count*4);if(auto p=a.pointer(*e.material_animation_table+4*i,12)){
     auto anim=std::make_unique<DatMaterialAnimation>(archive,*p,graph->graph());out.unk8[i]=static_cast<HSD_MatAnimJoint*>(anim->indexed_descriptor());s.materials.push_back(std::move(anim));}}
   }
   if(e.animation_flags_offset){auto* flags=s.make<u8>(count);auto bytes=a.range(*e.animation_flags_offset,count);std::memcpy(flags,bytes.data(),count);out.x28=flags;}
   s.graphs.push_back(std::move(graph));
  }
  require(!e.shape_animation_table,"Native stage shape animation unsupported");
  if(e.camera_offset)out.x10=&s.camera(*e.camera_offset)->perspective;
  if(e.unknown_14_offset)out.x14=s.table<HSD_CameraAnim>(*e.unknown_14_offset,[&](uint32_t p){return s.camera_anim(p);});
  if(e.light_table_offset)out.x18=s.light_table(*e.light_table_offset);
  if(e.fog_offset)out.x1C=s.fog(*e.fog_offset);
  out.unk24=e.collision_bindings.count;
  if(out.unk24){out.unk20=s.make<int16_t>(out.unk24*3);for(int i=0;i<out.unk24;i++){auto p=*e.collision_bindings.data_offset+6*i;auto* words=out.unk20+3*i;for(unsigned j=0;j<3;j++)words[j]=int16_t(a.be16(p+2*j));}}
  out.x30=e.joint_indices.count;if(out.x30){out.x2C=s.make<s16>(out.x30);for(int i=0;i<out.x30;i++)out.x2C[i]=int16_t(a.be16(*e.joint_indices.data_offset+2*i));}
 }
 struct JointReferences{HSD_Joint* joint;s16* pairs;s32 count;};
 s.map.unk4=meta.joint_reference_table.count;auto* refs=s.make<JointReferences>(s.map.unk4);s.map.unk0=refs;
 for(int i=0;i<s.map.unk4;i++){uint32_t p=*meta.joint_reference_table.data_offset+12*i;uint32_t root=s.pointer(p,64);require(s.joints.contains(root),"Native stage joint reference names unknown model");refs[i].joint=s.joints.at(root);refs[i].count=a.be32(p+8);require(refs[i].count>=0&&refs[i].count<=261,"Native stage marker reference count invalid");uint32_t pairs=s.pointer(p+4,refs[i].count*4);refs[i].pairs=s.make<s16>(refs[i].count*2);for(int j=0;j<refs[i].count*2;j++)refs[i].pairs[j]=int16_t(a.be16(pairs+2*j));}
 s.map.unk14=meta.spline_table.count;s.map.unk10=s.make<HSD_Spline*>(s.map.unk14);for(int i=0;i<s.map.unk14;i++)s.map.unk10[i]=s.spline(s.pointer(*meta.spline_table.data_offset+4*i,24));
 s.map.unk24=meta.shadow_table.count;s.map.unk20=s.make<MeleeWebMapShadowInput>(s.map.unk24);for(int i=0;i<s.map.unk24;i++){uint32_t p=*meta.shadow_table.data_offset+8*i;auto anim=a.pointer(p,16);if(anim)s.map.unk20[i].unk0=s.light_anim(*anim);s.map.unk20[i].flag=(a.range(p+4,1)[0]&0x80)!=0;}
 s.map.unk2C=meta.flagged_object_table.count;s.map.unk28=s.make<void*>(s.map.unk2C);
 for(int i=0;i<s.map.unk2C;i++){
  uint32_t p=s.pointer(*meta.flagged_object_table.data_offset+4*i,8);
  require(s.material_descriptors.contains(p),"Native stage flag mutation names unsupported descriptor kind");
  const auto& descriptors=s.material_descriptors.at(p);
  for(auto* material:descriptors)material->rendermode|=0x04000000;
  s.map.unk28[i]=descriptors.front();
 }

 // Source count32 is not a proven native allocation count. The two original
 // Ground light queries use the explicit bounded identity resolver below.
 s.map.unk1C=meta.light_override_table.count;s.map.unk18=nullptr;
 for(const auto& symbol:a.public_symbols())if(symbol.name=="yakumono_param"){
  if(profile->decode_yakumono){s.yaku=profile->decode_yakumono(s.arena.reader(),symbol.data_offset);require(s.yaku,"Native stage yakumono decoder returned null");continue;}
 // Original grLast uses four pointers to material command programs. Typed
  // command hydration is required before exposing its native pointer table.
  // The word count is part of the source stage ABI: Battlefield has two
  // overlay words and Final Destination has four. Do not infer it from the
  // next DAT allocation, since that boundary can be smaller than a generic
  // four-word read.
  const size_t program_count=profile->yakumono_program_count;
  require(program_count>0&&program_count<=4,"Native stage profile has an invalid yakumono program count");
  s.record(symbol.data_offset,program_count*4);auto** programs=s.make<uint32_t*>(program_count);
  for(size_t program=0;program<program_count;program++){
   const auto start=s.pointer(symbol.data_offset+4*program,4);const auto end=a.next_target_offset(start);
   std::vector<uint32_t> words;bool ended=false;
   for(uint32_t cursor=start;cursor<end;){
    require(words.size()<256,"Native stage material program exceeds budget");
    const uint32_t header=a.be32(cursor);cursor+=4;const auto op=header>>26;
    require(op==10||op==11||op==12||op==18||op==19,"Native stage material program opcode unsupported");
    words.push_back(header);
    if(op==18||op==19){require(cursor+4<=end,"Truncated native material color operand");
     if(op==19)require((header&0x3ffffff)!=0,"Native material blend duration must be positive");
     uint32_t rgba;std::memcpy(&rgba,a.range(cursor,4).data(),4);words.push_back(rgba);cursor+=4;
    }
    if(op==10){ended=true;break;}
   }
   require(ended,"Native stage material program has no bounded end");programs[program]=s.make<uint32_t>(words.size());std::copy(words.begin(),words.end(),programs[program]);
  }
  s.yaku=programs;
 }
 for(const auto& [offset,light]:s.lights){auto flags=read_dat_light_override(a,offset);s.overrides.push_back({light,flags.has_value(),flags.value_or(0)});}
 require(s.yaku,"Native stage yakumono symbol absent");
 s.native_map=melee_web_stage_map_build(s.arena.reader(),&s.map);
}
DatNativeStage::~DatNativeStage()=default;
void* DatNativeStage::map_head()const noexcept{return storage_->native_map;}
void* DatNativeStage::yakumono()const noexcept{return storage_->yaku;}
const std::vector<MeleeWebMapLightOverride>& DatNativeStage::light_overrides()const noexcept{return storage_->overrides;}
const std::vector<DatParticleEvent>& DatNativeStage::particle_events()const noexcept{return storage_->events;}
}
