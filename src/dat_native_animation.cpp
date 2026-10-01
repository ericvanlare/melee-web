#include "dat_native_animation.hpp"
#include "gameplay_compat.h"
#include "hsd_animation_bridge.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/fobj.h>
#pragma GCC diagnostic pop
#include <cmath>
#include <functional>
#include <set>
#include <map>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/ft/ftparts.h>
}
#pragma GCC diagnostic pop
namespace melee_web {
namespace {
void require(bool c,const char* why){if(!c)throw DatError(why);}
struct Track { HSD_FObjDesc descriptor{};std::vector<uint8_t> bytes; };
struct Node { HSD_AnimJoint descriptor{};HSD_AObjDesc animation{};std::vector<std::unique_ptr<Track>> tracks; };
}
struct DatNativeAnimation::Storage {
    std::shared_ptr<const DatArchive> archive;
    std::vector<std::unique_ptr<Node>> nodes;
    HSD_AnimJoint* root=nullptr;
    std::vector<HSD_AnimJoint> indexed;
    bool indexable=false;
    std::vector<DatParticleEvent> particle_events;
};
DatNativeAnimation::DatNativeAnimation(std::shared_ptr<const DatArchive> archive,uint32_t root,const MeleeWebNativeGraph& graph,DatNativeAnimationPolicy policy,std::span<void* const> native_joint_descriptors,DatNativeAnimationTopology topology)
    :storage_(std::make_unique<Storage>())
{
    auto& s=*storage_;s.archive=std::move(archive);
    require(bool(s.archive)&&graph.joints&&graph.root<graph.joint_count,"Native joint animation requires archive and checked model");
    const bool part_animation=topology==DatNativeAnimationTopology::FighterParts;
    const auto& a=*s.archive;std::set<uint32_t> visited,tracks;size_t stream_bytes=0;
    auto record=[&](uint32_t o,size_t n){require(!(o&3),"Native animation descriptor is unaligned");(void)a.range(o,n);
        require(n<=a.next_target_offset(o)-o,"Native animation descriptor crosses a referenced region");};
    std::map<HSD_AnimJoint*,uint32_t> indices;
    bool contiguous=graph.root==0;
    std::function<HSD_AnimJoint*(std::optional<uint32_t>,uint32_t,uint32_t)> visit;
    visit=[&](std::optional<uint32_t> at,uint32_t joint,uint32_t depth)->HSD_AnimJoint*{
        // Original AddAnimAll allows a missing subtree, leaving those joints
        // without an animation; a present subtree must map to real joints.
        if(!at)return nullptr;
        if(part_animation) {
            // ftAnim_GetNextAnimJointInTree uses a 30-entry depth stack, and
            // Fighter.parts is allocated at the exact MAX_FT_PARTS source
            // capacity. Part-animation trees are not the model JObj graph.
            require(depth<30&&s.nodes.size()<MAX_FT_PARTS,
                    "Native part animation exceeds source Fighter_Part traversal capacity");
        } else {
            require(joint<graph.joint_count&&s.nodes.size()<256,
                    "Native animation topology/cycle/count is invalid");
        }
        require(!part_animation||visited.insert(*at).second,
                "Native part-animation topology/cycle/count is invalid");
        record(*at,20);require(!a.pointer(*at+12),"Native RObj animation is unsupported");
        const auto flags=a.be32(*at+16);require(!(flags&~1u),"Native animation joint flags are unsupported");
        auto owner=std::make_unique<Node>();auto& n=*owner;n.descriptor.flags=flags;
        if(const auto ao=a.pointer(*at+8,16)){
            record(*ao,16);n.animation.flags=a.be32(*ao);n.animation.end_frame=a.f32(*ao+4);
            require(!(n.animation.flags&~0x30000000U)&&std::isfinite(n.animation.end_frame)&&n.animation.end_frame>=0&&n.animation.end_frame<=65535,
                    "Native joint animation flags or end frame are invalid");
            const auto reference=a.pointer(*ao+12,64);
            if(reference){
                uint32_t target=UINT32_MAX;
                for(uint32_t i=0;i<graph.joint_count;i++)if(graph.joints[i].source_offset==*reference)target=i;
                require(target<graph.joint_count&&(graph.joints[target].flags&0x4000U),
                        "Native path animation reference is not an owned spline joint");
                require(native_joint_descriptors.size()==graph.joint_count&&native_joint_descriptors[target],
                        "Native path animation requires its hydrated descriptor owner");
                void* descriptor=native_joint_descriptors[target];
                const uintptr_t address=reinterpret_cast<uintptr_t>(descriptor);
                require(address<=UINT32_MAX,"Native path descriptor ID exceeds source pointer width");
                n.animation.obj_id=uint32_t(address);
            }
            auto fo=a.pointer(*ao+8,20);uint64_t channels=0;
            while(fo){
                require(n.tracks.size()<12&&tracks.insert(*fo).second,"Native animation track cycle or count limit");record(*fo,20);
                auto track=std::make_unique<Track>();auto& f=track->descriptor;
                f.length=a.be32(*fo+4);f.startframe=a.f32(*fo+8);
                const auto fields=a.range(*fo+12,4);f.type=fields[0];f.frac_value=fields[1];f.frac_slope=fields[2];
                if (!(((f.type>=1&&f.type<=3)||(f.type>=5&&f.type<=12)||
                       (f.type==4&&reference.has_value())||
                       (f.type==40&&policy==DatNativeAnimationPolicy::ParticleDescriptors))&&
                      !(channels&(UINT64_C(1)<<f.type))))
                    throw DatError("Native animation channel " +
                                   std::to_string(f.type) +
                                   " requires an enabled checked policy at track " +
                                   std::to_string(*fo));
                channels|=UINT64_C(1)<<f.type;
                require(std::isfinite(f.startframe)&&f.startframe>=-32768&&f.startframe<=32767&&std::floor(f.startframe)==f.startframe,
                        "Native animation start frame exceeds source signed storage");
                require(f.length&&f.length<=65535&&(stream_bytes+=f.length)<=4U*1024U*1024U,"Native animation stream budget exceeded");
                const auto bytes=a.pointer(*fo+16,f.length);require(bool(bytes),"Native animation stream is absent");
                require(f.length<=a.next_target_offset(*bytes)-*bytes,"Native animation stream crosses referenced region");
                const auto span=a.range(*bytes,f.length);track->bytes.assign(span.begin(),span.end());f.ad=track->bytes.data();
                const MeleeWebAnimationTrack view{f.ad,f.length,0,1,f.frac_value,f.frac_slope};char error[256];
                if(!melee_web_animation_validate_native_track(&view,error,sizeof(error)))
                    throw DatError(std::string(error)+" at track "+std::to_string(*fo)+" start "+std::to_string(f.startframe)+" end "+std::to_string(n.animation.end_frame)+" flags "+std::to_string(n.animation.flags));
                if(f.type==40) {
                    // Original JObjUpdate interprets the float storage as a
                    // packed integer event. KEY copies those bits unchanged;
                    // interpolating them would manufacture different commands.
                    require(f.frac_value==0,"Particle event requires exact packed float storage");
                    size_t cursor=0;auto byte=[&](){require(cursor<track->bytes.size(),"Truncated particle event");return track->bytes[cursor++];};
                    auto continuation=[&](uint32_t value,unsigned shift,uint8_t prev){
                        while(prev&128){require(shift<16,"Particle event count overflow");prev=byte();value+=uint32_t(prev&127)<<shift;shift+=7;}
                        return value;
                    };
                    while(cursor<track->bytes.size()) {
                        const auto header=byte();require((header&15)==6,"Particle event requires KEY packets");
                        const auto count=continuation(((header>>4)&7)+1,3,header);
                        for(uint32_t i=0;i<count;i++){
                            uint32_t packed=0;for(unsigned shift=0;shift<32;shift+=8)packed|=uint32_t(byte())<<shift;
                            s.particle_events.push_back({packed&63,(packed>>6)&0xffffff});
                            if(cursor<track->bytes.size()){auto first=byte();(void)continuation(first&127,7,first);}
                        }
                    }
                }
                if(n.tracks.empty())n.animation.fobjdesc=&f;else n.tracks.back()->descriptor.next=&f;
                n.tracks.push_back(std::move(track));fo=a.pointer(*fo,20);
            }
            require(!reference||(channels&(UINT64_C(1)<<4)),"Spline object reference requires an original PATH channel");
            n.descriptor.aobjdesc=&n.animation;
        }
        auto* result=&n.descriptor;
        if(!part_animation) {
            indices[result]=joint;contiguous=contiguous&&*at==root+joint*20;
        }
        s.nodes.push_back(std::move(owner));
        // ftData's part-animation records are consumed as an authored ordered
        // Fighter_Part list, not as a mirror of the model JObj child/sibling
        // graph. Preserve their own HSD_AnimJoint topology and bound the
        // traversal by that checked source list.
        result->child=visit(a.pointer(*at,20),part_animation?0:graph.joints[joint].child,depth+1);
        result->next=visit(a.pointer(*at+4,20),part_animation?0:graph.joints[joint].next,depth);return result;
    };
    auto* tree=visit(root,part_animation?0:graph.root,0);
    if(part_animation) {
        s.root=tree;
        return;
    }
    s.indexed.resize(graph.joint_count);
    for(const auto& [old,index]:indices){
        auto& out=s.indexed[index];out=*old;
        out.child=old->child?&s.indexed.at(indices.at(old->child)):nullptr;
        out.next=old->next?&s.indexed.at(indices.at(old->next)):nullptr;
    }
    s.root=tree?&s.indexed.at(indices.at(tree)):nullptr;s.indexable=contiguous&&indices.size()==graph.joint_count;
}
DatNativeAnimation::~DatNativeAnimation()=default;
void* DatNativeAnimation::descriptor()const noexcept{return storage_->root;}
void* DatNativeAnimation::indexed_descriptor()const{
    require(storage_->indexable,"Native animation source is not a complete contiguous bone-indexed array");return storage_->root;
}
const std::vector<DatParticleEvent>& DatNativeAnimation::particle_events()const noexcept{return storage_->particle_events;}
}
