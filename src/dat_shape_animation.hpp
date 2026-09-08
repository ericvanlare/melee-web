#pragma once
#include "dat_native_joint.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <sysdolphin/baselib/pobj.h>
#include <sysdolphin/baselib/dobj.h>
}
#pragma GCC diagnostic pop
#include <functional>
#include <set>
namespace melee_web {
// Original sparse ShapeAnimJoint/DObj trees. Nonnull ShapeAnim leaves require
// active morph geometry, which the current native model importer rejects.
// Missing source leaves remain missing; no nonnull track is discarded.
class DatShapeAnimation {
    std::shared_ptr<const DatArchive> archive_;
    std::vector<std::unique_ptr<HSD_ShapeAnimJoint>> joints_;
    std::vector<std::unique_ptr<HSD_ShapeAnimDObj>> dobjs_;
    HSD_ShapeAnimJoint* root_=nullptr;
public:
    DatShapeAnimation(std::shared_ptr<const DatArchive> archive,uint32_t root,const MeleeWebNativeGraph& graph)
        :archive_(std::move(archive))
    {
        if(!archive_||!graph.joints||graph.root>=graph.joint_count)throw DatError("Shape animation requires a checked model");
        const auto& a=*archive_;std::set<uint32_t> joints,dobjs;
        auto record=[&](uint32_t at,size_t size){
            if((at&3)||size>a.next_target_offset(at)-at)throw DatError("Shape descriptor alignment or referenced extent is invalid");
            (void)a.range(at,size);
        };
        std::function<HSD_ShapeAnimDObj*(std::optional<uint32_t>,uint32_t)> visit_dobj;
        visit_dobj=[&](std::optional<uint32_t> at,uint32_t model)->HSD_ShapeAnimDObj*{
            if(!at)return nullptr;
            if(model>=graph.dobj_count||!graph.dobjs||dobjs.size()>=1024||!dobjs.insert(*at).second)
                throw DatError("Shape DObj topology, cycle or count is invalid");
            record(*at,8);
            if(a.pointer(*at+4,8))throw DatError("Active shape animation requires hydrated morph geometry");
            auto owner=std::make_unique<HSD_ShapeAnimDObj>();auto* out=owner.get();dobjs_.push_back(std::move(owner));
            out->next=visit_dobj(a.pointer(*at,8),graph.dobjs[model].next);return out;
        };
        std::function<HSD_ShapeAnimJoint*(std::optional<uint32_t>,uint32_t)> visit_joint;
        visit_joint=[&](std::optional<uint32_t> at,uint32_t model)->HSD_ShapeAnimJoint*{
            if(!at)return nullptr;
            if(model>=graph.joint_count||joints.size()>=256||!joints.insert(*at).second)
                throw DatError("Shape joint topology, cycle or count is invalid");
            record(*at,12);
            auto owner=std::make_unique<HSD_ShapeAnimJoint>();auto* out=owner.get();joints_.push_back(std::move(owner));
            out->shapeanimdobj=visit_dobj(a.pointer(*at+8,8),graph.joints[model].dobj);
            out->child=visit_joint(a.pointer(*at,12),graph.joints[model].child);
            out->next=visit_joint(a.pointer(*at+4,12),graph.joints[model].next);return out;
        };
        root_=visit_joint(root,graph.root);
    }
    HSD_ShapeAnimJoint* descriptor()const noexcept{return root_;}
    size_t joint_count()const noexcept{return joints_.size();}
    size_t dobj_count()const noexcept{return dobjs_.size();}
};
}
