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
};
DatNativeAnimation::DatNativeAnimation(std::shared_ptr<const DatArchive> archive,uint32_t root,const MeleeWebNativeGraph& graph)
    :storage_(std::make_unique<Storage>())
{
    auto& s=*storage_;s.archive=std::move(archive);
    require(bool(s.archive)&&graph.joints&&graph.root<graph.joint_count,"Native joint animation requires archive and checked model");
    const auto& a=*s.archive;std::set<uint32_t> visited,tracks;size_t stream_bytes=0;
    auto record=[&](uint32_t o,size_t n){require(!(o&3),"Native animation descriptor is unaligned");(void)a.range(o,n);
        require(n<=a.next_target_offset(o)-o,"Native animation descriptor crosses a referenced region");};
    std::function<HSD_AnimJoint*(std::optional<uint32_t>,uint32_t)> visit;
    visit=[&](std::optional<uint32_t> at,uint32_t joint)->HSD_AnimJoint*{
        // Original AddAnimAll allows a missing subtree, leaving those joints
        // without an animation; a present subtree must map to real joints.
        if(!at)return nullptr;
        require(joint<graph.joint_count&&s.nodes.size()<256&&visited.insert(*at).second,"Native animation topology/cycle/count is invalid");
        record(*at,20);require(!a.pointer(*at+12),"Native RObj animation is unsupported");
        const auto flags=a.be32(*at+16);require(!(flags&~1u),"Native animation joint flags are unsupported");
        auto owner=std::make_unique<Node>();auto& n=*owner;n.descriptor.flags=flags;
        if(const auto ao=a.pointer(*at+8,16)){
            record(*ao,16);n.animation.flags=a.be32(*ao);n.animation.end_frame=a.f32(*ao+4);
            require(!(n.animation.flags&~0x30000000U)&&std::isfinite(n.animation.end_frame)&&n.animation.end_frame>=0&&n.animation.end_frame<=65535,
                    "Native joint animation flags or end frame are invalid");
            require(!a.pointer(*ao+12),"Native joint animation object reference is unsupported");
            auto fo=a.pointer(*ao+8,20);unsigned channels=0;
            while(fo){
                require(n.tracks.size()<11&&tracks.insert(*fo).second,"Native animation track cycle or count limit");record(*fo,20);
                auto track=std::make_unique<Track>();auto& f=track->descriptor;
                f.length=a.be32(*fo+4);f.startframe=a.f32(*fo+8);
                const auto fields=a.range(*fo+12,4);f.type=fields[0];f.frac_value=fields[1];f.frac_slope=fields[2];
                require(((f.type>=1&&f.type<=3)||(f.type>=5&&f.type<=12))&&!(channels&(1u<<f.type)),
                        "Native animation channel requires ordinary SRT or visibility");channels|=1u<<f.type;
                require(std::isfinite(f.startframe)&&f.startframe>=0&&f.startframe<=32767&&std::floor(f.startframe)==f.startframe,
                        "Native animation start frame exceeds source signed storage");
                require(f.length&&f.length<=65535&&(stream_bytes+=f.length)<=4U*1024U*1024U,"Native animation stream budget exceeded");
                const auto bytes=a.pointer(*fo+16,f.length);require(bool(bytes),"Native animation stream is absent");
                require(f.length<=a.next_target_offset(*bytes)-*bytes,"Native animation stream crosses referenced region");
                const auto span=a.range(*bytes,f.length);track->bytes.assign(span.begin(),span.end());f.ad=track->bytes.data();
                const MeleeWebAnimationTrack view{f.ad,f.length,0,1,f.frac_value,f.frac_slope};char error[256];
                require(melee_web_animation_validate_track(&view,error,sizeof(error)),error);
                if(n.tracks.empty())n.animation.fobjdesc=&f;else n.tracks.back()->descriptor.next=&f;
                n.tracks.push_back(std::move(track));fo=a.pointer(*fo,20);
            }
            n.descriptor.aobjdesc=&n.animation;
        }
        auto* result=&n.descriptor;s.nodes.push_back(std::move(owner));
        result->child=visit(a.pointer(*at,20),graph.joints[joint].child);
        result->next=visit(a.pointer(*at+4,20),graph.joints[joint].next);return result;
    };
    s.root=visit(root,graph.root);
}
DatNativeAnimation::~DatNativeAnimation()=default;
void* DatNativeAnimation::descriptor()const noexcept{return storage_->root;}
}
