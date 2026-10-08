#include "dat_native_stage.hpp"
#include "dat_collision.hpp"
#include "dat_material_animation.hpp"
#include "dat_shape_animation.hpp"
#include "dat_stage.hpp"
#include "dat_lights.hpp"
#include "dat_sis.hpp"
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
#include <sysdolphin/baselib/tobj.h>
#include <sysdolphin/baselib/wobj.h>
#include <melee/mp/types.h>
#pragma GCC diagnostic pop
#include <cmath>
#include <cstring>
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <sstream>
#include <utility>
namespace melee_web {
namespace {void require(bool c,const char* m){if(!c)throw DatError(m);}}
struct NativeMapStorage {
    std::shared_ptr<const DatArchive> archive;
    NativeDatArena arena;
    DatStage metadata;
    std::vector<std::shared_ptr<void>> memory;
    std::vector<std::unique_ptr<DatNativeJoint>> graphs;
    std::vector<MeleeWebNativeJoint*> native;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;
    std::vector<DatParticleEvent> events;
    std::vector<MeleeWebMapLightOverride> overrides;
    std::vector<uint32_t> source_light_counts;
    std::map<uint32_t,HSD_Joint*> joints;
    std::map<uint32_t,HSD_ImageDesc*> images;
    std::map<uint32_t,HSD_CObjDesc*> cameras;
    std::map<uint32_t,std::vector<HSD_MObjDesc*>> material_descriptors;
    std::map<uint32_t,HSD_LightDesc*> lights;
    std::map<uint32_t,HSD_AObjDesc*> aobjects;
    std::map<uint32_t,HSD_Spline*> splines;
    std::map<uint32_t,HSD_LightAnim*> light_animations;
    std::set<uint32_t> active;
    MeleeWebMapInput map{};void* native_map=nullptr;void* native_collision=nullptr;
    explicit NativeMapStorage(std::shared_ptr<const DatArchive> a):archive(a),arena(a),metadata(*a){}
    ~NativeMapStorage(){for(auto* h:native)if(!melee_web_native_joint_destroy(h,nullptr,0))std::terminate();}
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
        const unsigned type=d->flags&3;
        std::memcpy(&d->color,archive->range(o+12,4).data(),4);d->position=world(archive->pointer(o+16,20));d->interest=world(archive->pointer(o+20,20));
        auto payload=archive->pointer(o+24,4);
        if(type==1){require(d->position&&payload,"Infinite native stage light lacks position or shininess");d->u.shininess=make<float>();*d->u.shininess=number(*payload);}
        else if(type==2){require(d->position&&payload,"Point native stage light lacks position or attenuation");
            if(d->attnflags&1){record(*payload,24);auto* v=make<HSD_LightAttn>();v->a0=number(*payload);v->a1=number(*payload+4);v->a2=number(*payload+8);v->k0=number(*payload+12);v->k1=number(*payload+16);v->k2=number(*payload+20);d->u.attn=v;}
            else{record(*payload,12);auto* v=make<HSD_LightPointDesc>();v->ref_br=number(*payload);v->ref_dist=number(*payload+4);v->dist_func=archive->be32(*payload+8);require(v->ref_br>=0&&v->ref_dist>=0&&v->dist_func<=3,"Point native stage light attenuation invalid");d->u.point=v;}}
        else if(type==3){require(d->position&&d->interest&&payload,"Spot native stage light lacks position, interest or attenuation");
            if(d->attnflags){record(*payload,24);auto* v=make<HSD_LightAttn>();v->a0=number(*payload);v->a1=number(*payload+4);v->a2=number(*payload+8);v->k0=number(*payload+12);v->k1=number(*payload+16);v->k2=number(*payload+20);d->u.attn=v;}
            else{record(*payload,20);auto* v=make<HSD_LightSpotDesc>();v->cutoff=number(*payload);v->spot_func=archive->be32(*payload+4);v->ref_br=number(*payload+8);v->ref_dist=number(*payload+12);v->dist_func=archive->be32(*payload+16);require(v->cutoff>=0&&v->cutoff<=180&&v->spot_func<=3&&v->ref_br>=0&&v->ref_dist>=0&&v->dist_func<=3,"Spot native stage light attenuation invalid");d->u.spot=v;}}
        else require(!payload,"Ambient native stage light has an unexpected payload");
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
    void** light_table(uint32_t o,uint32_t* count){
        std::vector<void*> values;uint32_t end=archive->next_target_offset(o);
        for(uint32_t i=0;i<=64;i++){
            require(o+4*i<end,"Native stage light table lacks bounded terminator");
            auto p=archive->pointer(o+4*i,4);
            if(!p){
                require(count!=nullptr,"Native stage light count output is missing");
                *count=i;auto** out=make<void*>(values.size()+1);
                std::copy(values.begin(),values.end(),out);return out;
            }
            require(i<64,"Native stage light table exceeds its descriptor budget");
            record(*p,8);auto* desc=light(pointer(*p,28));HSD_LightAnim** anims=nullptr;
            if(auto a=archive->pointer(*p+4,4))
                anims=table<HSD_LightAnim>(*a,[&](uint32_t q){return light_anim(q);});
            values.push_back(melee_web_stage_map_light_list(arena.reader(),desc,anims));
        }
        throw DatError("Native stage light table is unterminated");
    }
    void* collision(){
        const DatCollision source(*archive);
        const auto* r=arena.reader();
        static_assert(sizeof(MapLine)==16&&sizeof(MapJoint)==40&&sizeof(MapCollData)==48,
                      "Native MapCollData ABI differs from the original runtime");
        auto* out=static_cast<MapCollData*>(r->allocate(r->context,1,sizeof(MapCollData)));
        auto* vertices=static_cast<Vec2*>(r->allocate(r->context,source.vertices.size(),sizeof(Vec2)));
        auto* lines=static_cast<MapLine*>(r->allocate(r->context,source.lines.size(),sizeof(MapLine)));
        auto* joints=static_cast<MapJoint*>(r->allocate(r->context,source.joints.size(),sizeof(MapJoint)));
        for(size_t i=0;i<source.vertices.size();i++){
            vertices[i].x=source.vertices[i].x;vertices[i].y=source.vertices[i].y;
        }
        for(size_t i=0;i<source.lines.size();i++){
            const auto& from=source.lines[i];auto& to=lines[i];
            to.v0_idx=from.v0;to.v1_idx=from.v1;to.prev_id0=from.prev0;
            to.next_id0=from.next0;to.prev_id1=from.prev1;to.next_id1=from.next1;
            to.hi_flags=from.hi_flags;to.lo_flags=from.lo_flags;
        }
        for(size_t i=0;i<source.joints.size();i++){
            const auto& from=source.joints[i];auto& to=joints[i];
            to.floor_start=from.line_ranges[0].start;to.floor_count=from.line_ranges[0].count;
            to.ceiling_start=from.line_ranges[1].start;to.ceiling_count=from.line_ranges[1].count;
            to.right_wall_start=from.line_ranges[2].start;to.right_wall_count=from.line_ranges[2].count;
            to.left_wall_start=from.line_ranges[3].start;to.left_wall_count=from.line_ranges[3].count;
            to.dynamic_start=from.line_ranges[4].start;to.dynamic_count=from.line_ranges[4].count;
            to.left_bound=from.left;to.bottom_bound=from.bottom;
            to.right_bound=from.right;to.top_bound=from.top;
            to.vtx_start=from.vertices.start;to.vtx_count=from.vertices.count;
        }
        out->verts=vertices;out->vert_count=static_cast<int>(source.vertices.size());
        out->lines=lines;out->line_count=static_cast<int>(source.lines.size());
        out->floor_start=source.line_ranges[0].start;out->floor_count=source.line_ranges[0].count;
        out->ceiling_start=source.line_ranges[1].start;out->ceiling_count=source.line_ranges[1].count;
        out->right_wall_start=source.line_ranges[2].start;out->right_wall_count=source.line_ranges[2].count;
        out->left_wall_start=source.line_ranges[3].start;out->left_wall_count=source.line_ranges[3].count;
        out->dynamic_start=source.line_ranges[4].start;out->dynamic_count=source.line_ranges[4].count;
        out->joints=joints;out->joint_count=static_cast<int>(source.joints.size());
        out->x2C=source.source_reserved_2c;
        return out;
    }
    HSD_FogDesc* fog(uint32_t o){record(o,20);auto* d=make<HSD_FogDesc>();d->type=archive->be32(o);require(d->type<=15,"Invalid source fog type");d->start=number(o+8);d->end=number(o+12);std::memcpy(&d->color,archive->range(o+16,4).data(),4);
        if(auto p=archive->pointer(o+4,68)){record(*p,68);auto* f=make<HSD_FogAdjDesc>();f->center=archive->be16(*p);f->width=archive->be16(*p+2);for(unsigned i=0;i<16;i++)f->mtx[i/4][i%4]=number(*p+4+4*i);d->fogadjdesc=f;}return d;}
    HSD_Joint* spline_joint(uint32_t o){
        if(joints.contains(o))return joints.at(o);record(o,64);
        const uint32_t flags=archive->be32(o+4);
        // Classical scale and hidden are ordinary source joint properties,
        // including on a WObj animation's spline carrier. Keep both intact.
        require((flags&JOBJ_SPLINE)&&!(flags&~uint32_t(JOBJ_SPLINE|JOBJ_CLASSICAL_SCALE|JOBJ_HIDDEN))&&!archive->pointer(o)&&!archive->pointer(o+8)&&!archive->pointer(o+12)&&!archive->pointer(o+56)&&!archive->pointer(o+60),"Native scene spline reference requires a simple spline joint");
        auto* d=make<HSD_Joint>();d->flags=flags;d->u.spline=spline(pointer(o+16,24));
        d->rotation=*vector(o+20);d->scale=*vector(o+32);d->position=*vector(o+44);
        require(d->scale.x!=0&&d->scale.y!=0&&d->scale.z!=0,"Native spline joint has singular scale");joints[o]=d;return d;
    }
    HSD_Spline* spline(uint32_t o){if(splines.contains(o))return splines.at(o);record(o,24);auto* d=make<HSD_Spline>();d->type=archive->range(o,1)[0];d->numcv=int16_t(archive->be16(o+2));require(d->type<=3&&d->numcv>=2&&d->numcv<=4096,"Invalid native stage spline type/count");d->tension=number(o+4);d->totalLength=number(o+12);require(d->totalLength>0,"Stage spline length must be positive");
        unsigned points=d->type==1?3*(d->numcv-1)+1:d->type>=2?d->numcv+2:d->numcv;uint32_t cv=pointer(o+8,points*12);record(cv,points*12);d->cv=make<Vec3>(points);for(unsigned i=0;i<points;i++)d->cv[i]=*vector(cv+12*i);
        uint32_t lengths=pointer(o+16,d->numcv*4);record(lengths,d->numcv*4);d->segLength=make<float>(d->numcv);for(int i=0;i<d->numcv;i++){d->segLength[i]=number(lengths+4*i);require(d->segLength[i]>=0&&d->segLength[i]<=1&&(!i||d->segLength[i]>d->segLength[i-1]),"Stage spline arc table is not strictly ordered");}require(d->segLength[0]==0&&d->segLength[d->numcv-1]==1,"Stage spline arc table endpoints invalid");
        if(auto p=archive->pointer(o+20,(d->numcv-1)*20)){record(*p,(d->numcv-1)*20);auto* values=make<float>((d->numcv-1)*5);d->segPoly=reinterpret_cast<float(*)[5]>(values);for(int i=0;i<(d->numcv-1)*5;i++)values[i]=number(*p+4*i);}else require(d->type==0,"Nonlinear stage spline requires arc polynomial");splines[o]=d;return d;}
    void hydrate_map(const DatNativeMapContract& contract,MeleeWebStageMarkers* markers){
        require(markers,"Native map marker owner is absent");
        constexpr std::array<uint32_t,11> pointer_fields={0,4,8,12,16,20,24,28,32,40,44};
        require(metadata.entries.size()==contract.entry_count,"Native map entry count differs from its authored contract");
        require(contract.animation_consumer_counts.size()==contract.entry_count,
                "Native map contract lacks explicit animation consumer counts");
        std::vector<bool> resident(metadata.entries.size(),false);
        for(uint32_t id:contract.resident_entry_ids){
            require(id<resident.size()&&!resident[id],"Native map contract has an invalid or duplicate resident ID");
            resident[id]=true;
        }
        for(uint8_t count:contract.animation_consumer_counts)
            require(count>0&&count<=64,"Native map contract has an invalid animation consumer count");
        std::vector<bool> animation_flag_consumers(metadata.entries.size(),false);
        for(uint32_t id:contract.animation_flag_entry_ids){
            require(id<animation_flag_consumers.size()&&!animation_flag_consumers[id],
                    "Native map contract has an invalid or duplicate animation-flag consumer ID");
            animation_flag_consumers[id]=true;
        }
        require(contract.flagged_objects.size()==metadata.flagged_object_table.count,
                "Native map contract lacks authored flagged-object expectations");
        for(size_t i=0;i<contract.flagged_objects.size();i++){
            const auto& expected=contract.flagged_objects[i];
            require(expected.index==i,"Native map flagged-object expectations changed authored order");
            if(expected.kind==DatNativeMapFlagKind::LocalMaterial)
                require(expected.symbol.empty(),"Local native map flag has an external symbol identity");
            else if(expected.kind==DatNativeMapFlagKind::ExternalNull)
                require(!expected.symbol.empty(),"External native map flag lacks its exact symbol identity");
            else
                require(expected.kind==DatNativeMapFlagKind::Null&&expected.symbol.empty()&&!expected.target_offset,
                        "Native map null-flag expectation is malformed");
        }

        std::map<uint32_t,std::string_view> expected_external;
        for(const auto& reference:contract.external_references){
            require(reference.entry_index<metadata.entries.size(),"Native map external reference names an absent entry");
            require(std::find(pointer_fields.begin(),pointer_fields.end(),reference.field_offset)!=pointer_fields.end(),
                    "Native map external reference names a non-pointer entry field");
            require(!reference.symbol.empty(),"Native map external reference has an empty symbol identity");
            const uint32_t slot=metadata.entries[reference.entry_index].descriptor_offset+reference.field_offset;
            require(expected_external.emplace(slot,reference.symbol).second,
                    "Native map contract repeats an external entry field");
        }
        std::map<uint32_t,std::string_view> archive_external;
        for(const auto& symbol:archive->external_symbols())
            for(uint32_t slot:symbol.slots)
                archive_external.emplace(slot,symbol.name);
        for(const auto& expected:contract.flagged_objects){
            const uint32_t slot=*metadata.flagged_object_table.data_offset+4*expected.index;
            const auto actual=archive_external.find(slot);
            if(expected.kind==DatNativeMapFlagKind::ExternalNull){
                require(actual!=archive_external.end()&&actual->second==expected.symbol,
                        "Native map flagged external slot or name differs from its exact contract");
                require(!archive->pointer(slot),"Native map flagged external did not resolve to null");
            }else{
                require(actual==archive_external.end(),
                        "Native map local/null flag unexpectedly names an external slot");
            }
        }
        for(const auto& entry:metadata.entries){
            // Also reject extern links hidden in the two scalar count words;
            // they are not legal pointer references and are not contractable.
            for(uint32_t field=0;field<0x34;field+=4){
                const uint32_t slot=entry.descriptor_offset+field;
                const auto expected=expected_external.find(slot);
                const auto actual=archive_external.find(slot);
                require((expected==expected_external.end())==(actual==archive_external.end()),
                        "Native map external entry fields differ from their exact contract");
                if(expected!=expected_external.end()){
                    require(expected->second==actual->second,
                            "Native map external entry symbol differs from its exact contract");
                    require(!archive->pointer(slot),
                            "Native map external entry field did not resolve to null");
                }
            }
            require(resident[entry.index]==bool(entry.joint_offset),
                    resident[entry.index]?"Expected native map resident joint is absent":
                                          "Unexpected local joint in an imported native map row");
            if(!resident[entry.index]){
                const uint32_t slot=entry.descriptor_offset;
                require(expected_external.contains(slot),
                        "Imported native map row lacks its exact external joint identity");
                require(!entry.joint_animation_table&&!entry.material_animation_table&&
                        !entry.shape_animation_table,
                        "Imported native map row has a local animation table without a resident joint");
            }
        }

        source_light_counts.assign(metadata.entries.size(),0);
        map.unkC=static_cast<int32_t>(metadata.entries.size());map.unk8=make<MeleeWebMapEntryInput>(map.unkC);
        for(const auto& e:metadata.entries){
            auto& out=map.unk8[e.index];
            if(resident[e.index]){
                if(e.index==0){
                    out.unk0=static_cast<HSD_Joint*>(melee_web_stage_markers_descriptor(markers));
                    joints[*e.joint_offset]=out.unk0;
                }else{
                    std::unique_ptr<DatNativeJoint> graph;
                    try { graph=std::make_unique<DatNativeJoint>(archive,*e.joint_offset); }
                    catch(const DatError& error) { throw DatError("Stage map entry "+std::to_string(e.index)+": "+error.what()); }
                    char error[256];auto* native_joint=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native_joint,error);native.push_back(native_joint);
                    out.unk0=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native_joint,error,sizeof(error)));require(out.unk0,error);joints[*e.joint_offset]=out.unk0;
                    std::vector<void*> native_joint_descriptors(graph->graph().joint_count);
                    for(uint32_t i=0;i<graph->graph().joint_count;i++){
                        native_joint_descriptors[i]=melee_web_native_joint_descriptor_at(
                            native_joint,i,graph->graph().joints[i].source_offset,error,sizeof(error));
                        require(native_joint_descriptors[i],error);
                    }
                    for(uint32_t i=0;i<graph->graph().material_count;i++){
                        const auto& checked=graph->graph().materials[i];
                        auto* material=static_cast<HSD_MObjDesc*>(melee_web_native_joint_material_descriptor(native_joint,i,error,sizeof(error)));require(material,error);
                        material_descriptors[checked.source_offset].push_back(material);
                        // Keep source image aliases pointer-identical before the
                        // original scene loaders copy descriptor references.
                        auto* texture=material->texdesc;
                        for(uint32_t j=0;j<checked.material.texture_count;j++){
                            require(texture&&texture->imagedesc,"Native stage texture descriptor missing");
                            const uint32_t image_offset=pointer(checked.textures[j].source_offset+76,24);
                            auto [entry,inserted]=images.emplace(image_offset,texture->imagedesc);
                            if(!inserted)texture->imagedesc=entry->second;
                            texture=texture->next;
                        }
                        require(!texture,"Native stage texture count differs from checked graph");
                    }
                    const unsigned count=contract.animation_consumer_counts[e.index];
                    out.unk4=make<HSD_AnimJoint*>(count+1);out.unk8=make<HSD_MatAnimJoint*>(count+1);
                    if(e.shape_animation_table)out.unkC=make<HSD_ShapeAnimJoint*>(count+1);
                    for(unsigned i=0;i<count;i++){
                        if(e.joint_animation_table){record(*e.joint_animation_table,count*4);if(auto p=archive->pointer(*e.joint_animation_table+4*i,20)){
                            auto anim=std::make_unique<DatNativeAnimation>(archive,*p,graph->graph(),DatNativeAnimationPolicy::ParticleDescriptors,native_joint_descriptors);out.unk4[i]=static_cast<HSD_AnimJoint*>(anim->indexed_descriptor());events.insert(events.end(),anim->particle_events().begin(),anim->particle_events().end());animations.push_back(std::move(anim));}}
                        if(e.material_animation_table){record(*e.material_animation_table,count*4);if(auto p=archive->pointer(*e.material_animation_table+4*i,12)){
                            auto anim=std::make_unique<DatMaterialAnimation>(archive,*p,graph->graph());out.unk8[i]=static_cast<HSD_MatAnimJoint*>(anim->indexed_descriptor());materials.push_back(std::move(anim));}}
                        if(e.shape_animation_table){record(*e.shape_animation_table,count*4);if(auto p=archive->pointer(*e.shape_animation_table+4*i,12)){
                            auto anim=std::make_unique<DatShapeAnimation>(archive,*p,graph->graph());out.unkC[i]=anim->descriptor();shapes.push_back(std::move(anim));}}
                    }
                    graphs.push_back(std::move(graph));
                }
            }
            const unsigned count=contract.animation_consumer_counts[e.index];
            if(animation_flag_consumers[e.index]&&e.animation_flags_offset){auto* flags=make<u8>(count);auto bytes=archive->range(*e.animation_flags_offset,count);std::memcpy(flags,bytes.data(),count);out.x28=flags;}
            require(e.index!=0||!e.shape_animation_table,"Native stage marker shape animation unsupported");
            if(e.camera_offset)out.x10=&camera(*e.camera_offset)->perspective;
            if(e.unknown_14_offset)out.x14=table<HSD_CameraAnim>(*e.unknown_14_offset,[&](uint32_t p){return camera_anim(p);});
            if(e.light_table_offset)out.x18=light_table(*e.light_table_offset,&source_light_counts[e.index]);
            if(e.fog_offset)out.x1C=fog(*e.fog_offset);
            out.unk24=e.collision_bindings.count;
            if(out.unk24){out.unk20=make<int16_t>(out.unk24*3);for(int i=0;i<out.unk24;i++){auto p=*e.collision_bindings.data_offset+6*i;auto* words=out.unk20+3*i;for(unsigned j=0;j<3;j++)words[j]=int16_t(archive->be16(p+2*j));}}
            out.x30=e.joint_indices.count;
            if(out.x30){out.x2C=make<s16>(out.x30);for(int i=0;i<out.x30;i++)out.x2C[i]=int16_t(archive->be16(*e.joint_indices.data_offset+2*i));}
        }
        struct JointReferences{HSD_Joint* joint;s16* pairs;s32 count;};
        map.unk4=metadata.joint_reference_table.count;auto* refs=make<JointReferences>(map.unk4);map.unk0=refs;
        for(int i=0;i<map.unk4;i++){uint32_t p=*metadata.joint_reference_table.data_offset+12*i;uint32_t root=pointer(p,64);require(joints.contains(root),"Native stage joint reference names unknown model");refs[i].joint=joints.at(root);refs[i].count=archive->be32(p+8);require(refs[i].count>=0&&refs[i].count<=261,"Native stage marker reference count invalid");uint32_t pairs=pointer(p+4,refs[i].count*4);refs[i].pairs=make<s16>(refs[i].count*2);for(int j=0;j<refs[i].count*2;j++)refs[i].pairs[j]=int16_t(archive->be16(pairs+2*j));}
        map.unk14=metadata.spline_table.count;map.unk10=make<HSD_Spline*>(map.unk14);for(int i=0;i<map.unk14;i++)map.unk10[i]=spline(pointer(*metadata.spline_table.data_offset+4*i,24));
        map.unk24=metadata.shadow_table.count;map.unk20=make<MeleeWebMapShadowInput>(map.unk24);for(int i=0;i<map.unk24;i++){uint32_t p=*metadata.shadow_table.data_offset+8*i;auto anim=archive->pointer(p,16);if(anim)map.unk20[i].unk0=light_anim(*anim);map.unk20[i].flag=(archive->range(p+4,1)[0]&0x80)!=0;}
        map.unk2C=metadata.flagged_object_table.count;map.unk28=make<void*>(map.unk2C);
        for(int i=0;i<map.unk2C;i++){
            const uint32_t slot=*metadata.flagged_object_table.data_offset+4*i;
            auto target=archive->pointer(slot,8);
            const auto& expected=contract.flagged_objects[i];
            if(expected.kind==DatNativeMapFlagKind::ExternalNull){
                require(!target,"Native map imported flagged object unexpectedly resolved locally");
                map.unk28[i]=nullptr;continue;
            }
            if(expected.kind==DatNativeMapFlagKind::Null){
                require(!target&&!archive->has_relocation(slot),
                        "Native map authored null flag changed to a source pointer");
                map.unk28[i]=nullptr;continue;
            }
            require(target&&*target==expected.target_offset,
                    "Native map local flagged-object target differs from its exact contract");
            require(material_descriptors.contains(*target),"Native stage flag mutation names unsupported descriptor kind");
            const auto& descriptors=material_descriptors.at(*target);
            for(auto* material:descriptors)material->rendermode|=0x04000000;
            map.unk28[i]=descriptors.front();
        }
        // Source count32 is not a proven native allocation count. The two
        // original Ground light queries use a bounded identity resolver.
        map.unk1C=metadata.light_override_table.count;map.unk18=nullptr;
    }
    void build_map(){
        native_map=melee_web_stage_map_build(arena.reader(),&map);
        require(native_map,"Native source map builder returned null");
    }
};
struct DatNativeMap::Storage : NativeMapStorage {
    using NativeMapStorage::NativeMapStorage;
};
struct DatNativeStage::Storage : NativeMapStorage {
    std::unique_ptr<DatStageYaku> random_item_scripts;
    std::vector<std::unique_ptr<DatSis>> sis_owners;
    std::vector<MeleeWebArchiveSymbol> public_symbols;
    void* yaku=nullptr;
    using NativeMapStorage::NativeMapStorage;
};

DatNativeMap::DatNativeMap(std::shared_ptr<const DatArchive> archive,
                           const DatNativeMapContract& contract)
    : storage_(std::make_unique<Storage>(archive)) {
    auto* markers=melee_web_stage_markers_decode_structural(
        storage_->arena.reader(),storage_->metadata.root_offset);
    require(markers,"Native map structural marker decoder returned null");
    storage_->hydrate_map(contract,markers);
    storage_->build_map();
}
DatNativeMap::~DatNativeMap()=default;
void* DatNativeMap::map_head()const noexcept{return storage_->native_map;}
void* DatNativeMap::image_descriptor(uint32_t source_offset)const{
 require(storage_->images.contains(source_offset),"IMAGE is absent from native map texture graph");
 return storage_->images.at(source_offset);
}
void* DatNativeMap::collision(){
 if(!storage_->native_collision)storage_->native_collision=storage_->collision();
 return storage_->native_collision;
}
std::span<const uint32_t> DatNativeMap::source_light_counts()const noexcept{return storage_->source_light_counts;}

DatNativeMapContract DatNativeStageMapContractData::view() const noexcept
{
 return {
  entry_count,
  animation_consumer_counts,
  resident_entry_ids,
  external_references,
  animation_flag_entry_ids,
  flagged_objects,
 };
}

DatNativeStageMapContractData dat_native_stage_map_contract_from_profile(
    const MeleeWebStageProfile& profile, const DatArchive& archive,
    const DatStage& metadata)
{
 require(metadata.entries.size()==profile.entry_count,
         "Native stage map entry count differs from source profile");
 require(profile.animation_count_count==profile.entry_count&&profile.animation_counts,
         "Native stage profile lacks animation consumer counts");

 DatNativeStageMapContractData result;
 result.entry_count=profile.entry_count;
 result.animation_consumer_counts.assign(
     profile.animation_counts,profile.animation_counts+profile.animation_count_count);

 constexpr std::array<uint32_t,11> pointer_fields={0,4,8,12,16,20,24,28,32,40,44};
 switch(profile.map_ownership_policy){
 case MELEE_WEB_STAGE_MAP_OWNERSHIP_UNSPECIFIED:
  require(false,"Native stage profile has no explicit map ownership policy");
  break;
 case MELEE_WEB_STAGE_MAP_OWNERSHIP_CURRENT_ALL_RESIDENT:{
  require(!profile.map_ownership,
          "Current all-resident map policy contradicts an authored declaration");
  result.resident_entry_ids.resize(metadata.entries.size());
  for(uint32_t i=0;i<result.resident_entry_ids.size();i++)result.resident_entry_ids[i]=i;
  for(const auto& entry:metadata.entries)for(uint32_t field:pointer_fields){
   const uint32_t slot=entry.descriptor_offset+field;
   for(const auto& symbol:archive.external_symbols())
    if(std::find(symbol.slots.begin(),symbol.slots.end(),slot)!=symbol.slots.end())
     result.external_references.push_back({entry.index,field,symbol.name});
  }
  for(const auto& entry:metadata.entries)
   if(entry.index!=0)result.animation_flag_entry_ids.push_back(entry.index);
  result.flagged_objects.reserve(metadata.flagged_object_table.count);
  for(uint32_t i=0;i<metadata.flagged_object_table.count;i++){
   const uint32_t slot=*metadata.flagged_object_table.data_offset+4*i;
   const auto target=archive.pointer(slot,8);
   require(target.has_value(),"Native stage flag mutation pointer is null");
   result.flagged_objects.push_back({i,DatNativeMapFlagKind::LocalMaterial,*target,{}});
  }
  break;
 }
 case MELEE_WEB_STAGE_MAP_OWNERSHIP_AUTHORED:{
  const auto* ownership=profile.map_ownership;
  require(ownership,"Authored native stage map policy lacks its declaration");
  const auto require_array=[](const void* data,size_t count,const char* message){
   require(!count||data,message);
  };
  require(ownership->resident_entry_count<=profile.entry_count,
          "Authored native stage resident list exceeds the entry table");
  require_array(ownership->resident_entry_ids,ownership->resident_entry_count,
                "Authored native stage resident list is missing");
  require(ownership->animation_flag_entry_count<=profile.entry_count,
          "Authored native stage animation-flag list exceeds the entry table");
  require_array(ownership->animation_flag_entry_ids,ownership->animation_flag_entry_count,
                "Authored native stage animation-flag list is missing");
  require(ownership->external_reference_count<=profile.entry_count*pointer_fields.size(),
          "Authored native stage external-reference list exceeds pointer fields");
  require_array(ownership->external_references,ownership->external_reference_count,
                "Authored native stage external-reference list is missing");
  require(ownership->flagged_object_count==metadata.flagged_object_table.count,
          "Authored native stage flagged-object list differs from its source table");
  require_array(ownership->flagged_objects,ownership->flagged_object_count,
                "Authored native stage flagged-object list is missing");

  if(ownership->resident_entry_count)
   result.resident_entry_ids.assign(ownership->resident_entry_ids,
                                    ownership->resident_entry_ids+ownership->resident_entry_count);
  if(ownership->animation_flag_entry_count)
   result.animation_flag_entry_ids.assign(ownership->animation_flag_entry_ids,
                                          ownership->animation_flag_entry_ids+ownership->animation_flag_entry_count);
  result.external_references.reserve(ownership->external_reference_count);
  for(size_t i=0;i<ownership->external_reference_count;i++){
   const auto& reference=ownership->external_references[i];
   require(reference.symbol&&reference.symbol[0],
           "Authored native stage external reference lacks its exact symbol");
   result.external_references.push_back({reference.entry_index,reference.field_offset,reference.symbol});
  }
  result.flagged_objects.reserve(ownership->flagged_object_count);
  for(size_t i=0;i<ownership->flagged_object_count;i++){
   const auto& expected=ownership->flagged_objects[i];
   const std::string_view symbol=expected.symbol?expected.symbol:"";
   DatNativeMapFlagKind kind;
   switch(expected.kind){
   case MELEE_WEB_STAGE_MAP_FLAG_LOCAL_MATERIAL:
    require(symbol.empty(),"Authored local native stage flag has an external symbol");
    kind=DatNativeMapFlagKind::LocalMaterial;
    break;
   case MELEE_WEB_STAGE_MAP_FLAG_EXTERNAL_NULL:
    require(!symbol.empty()&&!expected.target_offset,
            "Authored external native stage flag lacks its exact symbol or has a local target");
    kind=DatNativeMapFlagKind::ExternalNull;
    break;
   case MELEE_WEB_STAGE_MAP_FLAG_NULL:
    require(symbol.empty()&&!expected.target_offset,
            "Authored null native stage flag has a target or symbol");
    kind=DatNativeMapFlagKind::Null;
    break;
   default:
    require(false,"Authored native stage flag has an unknown kind");
    kind=DatNativeMapFlagKind::Null;
    break;
   }
   result.flagged_objects.push_back({expected.index,kind,expected.target_offset,symbol});
  }
  break;
 }
 default:
  require(false,"Native stage profile has an unknown map ownership policy");
 }
 return result;
}

DatNativeStage::DatNativeStage(std::shared_ptr<const DatArchive> archive)
    : DatNativeStage(std::move(archive), St_Kind_Last, ProfileMode::Complete) {}

DatNativeStage::DatNativeStage(std::shared_ptr<const DatArchive> archive, int stage_kind)
    : DatNativeStage(std::move(archive), stage_kind, ProfileMode::Complete) {}

DatNativeStage::DatNativeStage(std::shared_ptr<const DatArchive> archive,
                               int stage_kind, ProfileMode profile_mode)
    : storage_(std::make_unique<Storage>(archive)){
  auto& s=*storage_;const auto& a=*archive;const auto& meta=s.metadata;
  const auto* profile=melee_web_stage_profile(stage_kind);
  require(profile,"Native stage has no complete source callback profile");
  require(profile->diagnostic_only ==
              (profile_mode == ProfileMode::DiagnosticOnly),
          "Native stage profile mode does not match its admission scope");
 for(const auto& symbol:a.public_symbols())
  if(symbol.name=="ALDYakuAll")
   s.random_item_scripts=std::make_unique<DatStageYaku>(archive,symbol.data_offset);
 auto contract_data=dat_native_stage_map_contract_from_profile(*profile,a,meta);
 const DatNativeMapContract contract=contract_data.view();
 auto* markers=melee_web_stage_markers_decode(s.arena.reader(),meta.root_offset);
 require(markers,"Native stage strict marker decoder returned null");
 s.hydrate_map(contract,markers);

 for(const auto& symbol:a.public_symbols())if(symbol.name=="yakumono_param"){
  if(profile->decode_yakumono){s.yaku=profile->decode_yakumono(s.arena.reader(),symbol.data_offset);require(s.yaku,"Native stage yakumono decoder returned null");continue;}
  if(profile->opaque_yakumono){
   s.record(symbol.data_offset,4);
   auto* opaque=s.make<uint8_t>(4);
   auto bytes=a.range(symbol.data_offset,4);
   std::memcpy(opaque,bytes.data(),bytes.size());
   s.yaku=opaque;
   continue;
  }
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
 s.build_map();
 s.native_collision=s.collision();
  const auto* content=melee_web_stage_content_for_profile(stage_kind);
 require(content,"Native stage public catalog has no archive identity");
 for(const auto& symbol:a.public_symbols()){
  void* value=nullptr;
  if(symbol.name=="map_head")value=s.native_map;
  else if(symbol.name=="coll_data")value=s.native_collision;
  else if(symbol.name=="yakumono_param")value=s.yaku;
  else if(symbol.name=="ALDYakuAll"&&s.random_item_scripts)
   value=s.random_item_scripts->native_data();
  for(size_t i=0;i<profile->public_symbol_count;i++){
   const auto& request=profile->public_symbols[i];
   if(symbol.name!=request.name)continue;
   if(request.kind==MELEE_WEB_STAGE_PUBLIC_IMAGE){
    require(s.images.contains(symbol.data_offset),"Stage public image is not owned by its map texture graph");
    value=s.images.at(symbol.data_offset);
   }else if(request.kind==MELEE_WEB_STAGE_PUBLIC_JOINT){
    if(!s.joints.contains(symbol.data_offset)){
     auto graph=std::make_unique<DatNativeJoint>(archive,symbol.data_offset);
     char error[256];auto* native=melee_web_native_joint_hydrate(&graph->graph(),error,sizeof(error));require(native,error);
     s.native.push_back(native);
     auto* descriptor=static_cast<HSD_Joint*>(melee_web_native_joint_descriptor(native,error,sizeof(error)));require(descriptor,error);
     s.joints.emplace(symbol.data_offset,descriptor);s.graphs.push_back(std::move(graph));
    }
    value=s.joints.at(symbol.data_offset);
   }else if(request.kind==MELEE_WEB_STAGE_PUBLIC_SIS){
    auto owner=std::make_unique<DatSis>(archive,symbol.name);
    value=owner->descriptor();
    require(value,"Stage public SIS owner returned a null descriptor");
    s.sis_owners.push_back(std::move(owner));
   }else require(false,"Unknown native stage public descriptor kind");
  }
  // Retain unhydrated public names as explicit unsupported capabilities.
  s.public_symbols.push_back({content->archive,symbol.name.c_str(),value});
 }
 for(size_t i=0;i<profile->public_symbol_count;i++){
  bool found=false;
  for(const auto& symbol:s.public_symbols)if(std::strcmp(symbol.symbol,profile->public_symbols[i].name)==0&&symbol.native_data)found=true;
  require(found,"Required original stage public symbol missing");
 }
}
DatNativeStage::~DatNativeStage()=default;
void* DatNativeStage::map_head()const noexcept{return storage_->native_map;}
void* DatNativeStage::yakumono()const noexcept{return storage_->yaku;}
void* DatNativeStage::random_item_scripts()const noexcept{return storage_->random_item_scripts?storage_->random_item_scripts->native_data():nullptr;}
void DatNativeStage::set_particle_roots(void* commands,void* textures){
 auto& symbols=storage_->public_symbols;bool has_commands=false,has_textures=false;
 for(auto& symbol:symbols){
  if(std::strcmp(symbol.symbol,"map_ptcl")==0){symbol.native_data=commands;has_commands=true;}
  else if(std::strcmp(symbol.symbol,"map_texg")==0){symbol.native_data=textures;has_textures=true;}
 }
 require(has_commands==has_textures,"Source stage particle public roots are incomplete");
 require(has_commands==(commands!=nullptr&&textures!=nullptr),
         "Source stage particle public roots differ from their decoded owner");
}
void DatNativeStage::set_quake_model(void* descriptor){
 require(descriptor,"Source stage quake descriptor is absent");
 for(auto& symbol:storage_->public_symbols)
  if(std::strcmp(symbol.symbol,"quake_model_set")==0){
   symbol.native_data=descriptor;return;
  }
 throw DatError("Source stage quake public symbol is absent");
}
const std::vector<MeleeWebMapLightOverride>& DatNativeStage::light_overrides()const noexcept{return storage_->overrides;}
std::span<const uint32_t> DatNativeStage::source_light_counts()const noexcept{return storage_->source_light_counts;}
const std::vector<DatParticleEvent>& DatNativeStage::particle_events()const noexcept{return storage_->events;}
const std::vector<MeleeWebArchiveSymbol>& DatNativeStage::public_symbols()const noexcept{return storage_->public_symbols;}
void* DatNativeStage::light_animation_table(uint32_t offset){
 auto& s=*storage_;
 return s.table<HSD_LightAnim>(offset,[&](uint32_t root){return s.light_anim(root);});
}
}
