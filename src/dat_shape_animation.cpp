#include "dat_shape_animation.hpp"

#include "gameplay_compat.h"
#include "hsd_animation_bridge.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/fobj.h>
#include <sysdolphin/baselib/pobj.h>
}
#pragma GCC diagnostic pop

#include <cmath>
#include <functional>
#include <set>

namespace melee_web {
namespace {

void require(bool condition, const char* message) {
    if (!condition) throw DatError(message);
}

}

struct DatShapeAnimation::Storage {
    struct Track {
        HSD_FObjDesc descriptor{};
        std::vector<uint8_t> bytes;
    };
    struct ShapeNode {
        HSD_ShapeAnim descriptor{};
        HSD_AObjDesc animation{};
        std::vector<std::unique_ptr<Track>> tracks;
    };

    std::shared_ptr<const DatArchive> archive;
    std::vector<std::unique_ptr<HSD_ShapeAnimJoint>> joints;
    std::vector<std::unique_ptr<HSD_ShapeAnimDObj>> dobjs;
    std::vector<std::unique_ptr<ShapeNode>> shapes;
    HSD_ShapeAnimJoint* root = nullptr;
};

DatShapeAnimation::DatShapeAnimation(std::shared_ptr<const DatArchive> archive,
                                     uint32_t root,
                                     const MeleeWebNativeGraph& graph)
    : storage_(std::make_unique<Storage>()) {
    auto& s = *storage_;
    s.archive = std::move(archive);
    require(s.archive && graph.joints && graph.root < graph.joint_count,
            "Shape animation requires an archive and checked model");
    const auto& a = *s.archive;
    std::set<uint32_t> joints, dobjs, shapes, tracks;

    const auto record = [&](uint32_t at, size_t size) {
        require(!(at & 3) && size <= a.next_target_offset(at) - at,
                "Shape descriptor alignment or referenced extent is invalid");
        (void) a.range(at, size);
    };

    // HSD_PObjAddAnimAll advances its ShapeAnim and PObj chains together.
    // Carry the checked PObj id through this walk so each shape node is
    // validated against its own shape count and blend mode.
    std::function<HSD_ShapeAnim*(std::optional<uint32_t>, uint32_t)> visit_shape;
    visit_shape = [&](std::optional<uint32_t> at, uint32_t pobj) -> HSD_ShapeAnim* {
        if (!at) return nullptr;
        require(pobj < graph.pobj_count && graph.pobjs,
                "Shape animation has more nodes than the checked PObj chain");
        const auto* shape = graph.pobjs[pobj].shape;
        require(shape && shape->shape_count && shape->shape_count <= 4096,
                "Shape animation PObj has no checked morph geometry");
        require(shapes.size() < 1024 && shapes.insert(*at).second,
                "Shape animation leaf topology, cycle or count is invalid");
        record(*at, 8);

        auto owner = std::make_unique<Storage::ShapeNode>();
        auto* out = &owner->descriptor;
        if (const auto animation = a.pointer(*at + 4, 16)) {
            record(*animation, 16);
            owner->animation.flags = a.be32(*animation);
            owner->animation.end_frame = a.f32(*animation + 4);
            owner->animation.obj_id = a.be32(*animation + 12);
            require(!(owner->animation.flags & ~0x30000000U) &&
                        std::isfinite(owner->animation.end_frame) &&
                        owner->animation.end_frame >= 0 &&
                        owner->animation.end_frame <= 65535,
                    "Shape animation flags or end frame are invalid");

            auto fobj = a.pointer(*animation + 8, 20);
            uint32_t channels = 0;
            while (fobj) {
                require(owner->tracks.size() < 32 && tracks.insert(*fobj).second,
                        "Shape animation track cycle or count limit");
                record(*fobj, 20);
                auto track = std::make_unique<Storage::Track>();
                auto& f = track->descriptor;
                f.length = a.be32(*fobj + 4);
                f.startframe = a.f32(*fobj + 8);
                const auto fields = a.range(*fobj + 12, 4);
                f.type = fields[0];
                f.frac_value = fields[1];
                f.frac_slope = fields[2];

                const bool additive = (shape->flags & SHAPESET_ADDITIVE) != 0;
                const bool valid_channel = additive
                    ? (f.type >= HSD_A_S_W0 &&
                       f.type < HSD_A_S_W0 + shape->shape_count)
                    : (f.type >= 1 && f.type <= 10);
                require(valid_channel && f.type < 32 &&
                            !(channels & (1U << f.type)),
                        "Shape animation channel is outside its shape blend set");
                channels |= 1U << f.type;
                require(std::isfinite(f.startframe) &&
                            f.startframe >= -32768 && f.startframe <= 32767 &&
                            std::floor(f.startframe) == f.startframe &&
                            f.length && f.length <= 65535,
                        "Shape animation track metadata is invalid");

                const auto bytes = a.pointer(*fobj + 16, f.length);
                require(bytes && f.length <= a.next_target_offset(*bytes) - *bytes,
                        "Shape animation track stream is truncated");
                const auto span = a.range(*bytes, f.length);
                track->bytes.assign(span.begin(), span.end());
                f.ad = track->bytes.data();
                const MeleeWebAnimationTrack view{
                    f.ad, f.length, 0, 1, f.frac_value, f.frac_slope};
                char error[256];
                require(melee_web_animation_validate_native_track(
                            &view, error, sizeof(error)),
                        error);
                if (owner->tracks.empty()) {
                    owner->animation.fobjdesc = &f;
                } else {
                    owner->tracks.back()->descriptor.next = &f;
                }
                owner->tracks.push_back(std::move(track));
                fobj = a.pointer(*fobj, 20);
            }
            out->aobjdesc = &owner->animation;
        }

        s.shapes.push_back(std::move(owner));
        out->next = visit_shape(a.pointer(*at, 8), graph.pobjs[pobj].next);
        return out;
    };

    std::function<HSD_ShapeAnimDObj*(std::optional<uint32_t>, uint32_t)> visit_dobj;
    visit_dobj = [&](std::optional<uint32_t> at, uint32_t model) -> HSD_ShapeAnimDObj* {
        if (!at) return nullptr;
        require(model < graph.dobj_count && graph.dobjs &&
                    dobjs.size() < 1024 && dobjs.insert(*at).second,
                "Shape DObj topology, cycle or count is invalid");
        record(*at, 8);
        auto owner = std::make_unique<HSD_ShapeAnimDObj>();
        auto* out = owner.get();
        s.dobjs.push_back(std::move(owner));
        const auto shape = a.pointer(*at + 4, 8);
        if (shape) {
            require(graph.dobjs[model].pobj != UINT32_MAX,
                    "Shape animation DObj has no corresponding PObj chain");
            out->shapeanim = visit_shape(shape, graph.dobjs[model].pobj);
        }
        out->next = visit_dobj(a.pointer(*at, 8), graph.dobjs[model].next);
        return out;
    };

    std::function<HSD_ShapeAnimJoint*(std::optional<uint32_t>, uint32_t)> visit_joint;
    visit_joint = [&](std::optional<uint32_t> at, uint32_t model) -> HSD_ShapeAnimJoint* {
        if (!at) return nullptr;
        require(model < graph.joint_count && joints.size() < 256 &&
                    joints.insert(*at).second,
                "Shape joint topology, cycle or count is invalid");
        record(*at, 12);
        auto owner = std::make_unique<HSD_ShapeAnimJoint>();
        auto* out = owner.get();
        s.joints.push_back(std::move(owner));
        out->shapeanimdobj = visit_dobj(a.pointer(*at + 8, 8), graph.joints[model].dobj);
        out->child = visit_joint(a.pointer(*at, 12), graph.joints[model].child);
        out->next = visit_joint(a.pointer(*at + 4, 12), graph.joints[model].next);
        return out;
    };

    s.root = visit_joint(root, graph.root);
}

DatShapeAnimation::~DatShapeAnimation() = default;
HSD_ShapeAnimJoint* DatShapeAnimation::descriptor() const noexcept { return storage_->root; }
size_t DatShapeAnimation::joint_count() const noexcept { return storage_->joints.size(); }
size_t DatShapeAnimation::dobj_count() const noexcept { return storage_->dobjs.size(); }

}
