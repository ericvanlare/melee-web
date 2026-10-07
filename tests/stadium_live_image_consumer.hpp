#pragma once

#include "gameplay_compat.h"
#include "stadium_screen_roots_synthetic.hpp"
#include "hsd_texture_bounds.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <unordered_set>
#include <vector>

extern "C" {
#include <sysdolphin/baselib/aobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/id.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/mtx.h>
#include <sysdolphin/baselib/objalloc.h>
#include <sysdolphin/baselib/robj.h>
#include <sysdolphin/baselib/tev.h>
#include <sysdolphin/baselib/tobj.h>
}

namespace melee_web::test::stadium_screen {

struct LoadedJointPlan {
    std::vector<HSD_Joint*> joints;
    std::vector<HSD_Joint*> instance_targets;
    bool has_shadow_material{};
};

// Match JObjLoad's own-child / instance / sibling construction boundary.
// Instance children are references to nodes loaded elsewhere in the graph.
inline LoadedJointPlan checked_load_plan(HSD_Joint* root_joint) {
    require(root_joint != nullptr, "Live source map-1 root is null");
    LoadedJointPlan plan;
    std::vector<HSD_Joint*> pending{root_joint};
    std::unordered_set<HSD_Joint*> seen;
    while (!pending.empty()) {
        HSD_Joint* joint = pending.back();
        pending.pop_back();
        require(joint && seen.insert(joint).second,
                "Source JObj construction graph repeats a loaded descriptor");
        plan.joints.push_back(joint);

        if (union_type_dobj(joint)) {
            std::unordered_set<HSD_DObjDesc*> dobjs;
            for (HSD_DObjDesc* dobj = joint->u.dobjdesc; dobj;
                 dobj = dobj->next) {
                require(dobjs.insert(dobj).second,
                        "Source DObj descriptor chain contains a cycle");
                HSD_MObjDesc* material = dobj->mobjdesc;
                if (material && (material->rendermode & RENDER_SHADOW))
                    plan.has_shadow_material = true;
                if (material) {
                    std::unordered_set<HSD_TObjDesc*> tobj_descs;
                    for (HSD_TObjDesc* tobj = material->texdesc; tobj;
                         tobj = tobj->next) {
                        require(tobj_descs.insert(tobj).second,
                                "Source TObj descriptor chain contains a cycle");
                    }
                }
            }
        }

        if (joint->flags & JOBJ_INSTANCE) {
            if (joint->child) plan.instance_targets.push_back(joint->child);
        } else if (joint->child) {
            pending.push_back(joint->child);
        }
        if (joint->next) pending.push_back(joint->next);
    }
    for (HSD_Joint* target : plan.instance_targets)
        require(seen.contains(target),
                "Source instance target is outside the loaded JObj graph");
    return plan;
}

struct BorrowedTObjState {
    HSD_TObj* object{};
    HSD_TObj* next{};
    HSD_ImageDesc* image{};
    GXTexMapID id{};
    u32 mtxid{};
    GXTexCoordID coord{};
    bool operator==(const BorrowedTObjState&) const = default;
};

struct BorrowedTextureState {
    HSD_TObj* toon{};
    HSD_TObj* shadows{};
    std::vector<BorrowedTObjState> toon_chain;
    std::vector<BorrowedTObjState> shadow_chain;
    bool operator==(const BorrowedTextureState&) const = default;
};

inline std::vector<BorrowedTObjState> snapshot_tobj_chain(HSD_TObj* root) {
    std::vector<BorrowedTObjState> result;
    std::unordered_set<HSD_TObj*> seen;
    for (HSD_TObj* tobj = root; tobj; tobj = tobj->next) {
        require(seen.insert(tobj).second,
                "Borrowed texture-object chain contains a cycle");
        result.push_back({tobj, tobj->next, tobj->imagedesc, tobj->id,
                          tobj->mtxid, tobj->coord});
    }
    return result;
}

inline BorrowedTextureState snapshot_borrowed_textures() {
    return {tobj_toon, tobj_shadows, snapshot_tobj_chain(tobj_toon),
            snapshot_tobj_chain(tobj_shadows)};
}

inline void require_borrowed_textures_unchanged(
    const BorrowedTextureState& before) {
    require(snapshot_borrowed_textures() == before,
            "Source JObj construction changed a borrowed toon/shadow TObj");
}

template <class T>
inline void append_bytes(std::vector<std::uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(value));
}

inline void append_object_chain(std::vector<std::uint8_t>& out, HSD_GObj* root,
                                bool gx_chain) {
    std::unordered_set<HSD_GObj*> seen;
    for (HSD_GObj* object = root; object;
         object = gx_chain ? object->next_gx : object->next) {
        require(seen.insert(object).second,
                gx_chain ? "Native GX list contains a cycle"
                         : "Native entity list contains a cycle");
        append_bytes(out, object);
        append_bytes(out, *object);
    }
}

inline void append_process_chain(std::vector<std::uint8_t>& out,
                                 HSD_GObjProc* root, bool child_chain) {
    std::unordered_set<HSD_GObjProc*> seen;
    for (HSD_GObjProc* proc = root; proc;
         proc = child_chain ? proc->child : proc->next) {
        require(seen.insert(proc).second,
                "Native process list contains a cycle");
        append_bytes(out, proc);
        append_bytes(out, *proc);
    }
}

inline std::vector<std::uint8_t> runtime_roots_snapshot() {
    require(HSD_GObj_Entities && plinklow_gobjs && HSD_GObjGXLinkHead &&
                HSD_GObj_804D7820 && HSD_GObj_804D7840 && HSD_GObj_804D7844,
            "Native runtime GObj registries are unavailable");
    std::vector<std::uint8_t> out;
    const u32 entity_count = HSD_GObjLibInitData.p_link_max + 1;
    const u32 gx_count = HSD_GObjLibInitData.gx_link_max + 2;
    const u32 proc_count = HSD_GObjLibInitData.gproc_pri_max + 1;
    append_bytes(out, HSD_GObjLibInitData.p_link_max);
    append_bytes(out, HSD_GObjLibInitData.gx_link_max);
    append_bytes(out, HSD_GObjLibInitData.gproc_pri_max);
    for (u32 i = 0; i < entity_count; ++i) {
        HSD_GObj* head = reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities)[i];
        append_bytes(out, head);
        append_bytes(out, plinklow_gobjs[i]);
        append_object_chain(out, head, false);
    }
    for (u32 i = 0; i < gx_count; ++i) {
        append_bytes(out, HSD_GObjGXLinkHead[i]);
        append_bytes(out, HSD_GObj_804D7820[i]);
        append_object_chain(out, HSD_GObjGXLinkHead[i], true);
        append_object_chain(out, HSD_GObj_804D7820[i], true);
    }
    for (u32 i = 0; i < proc_count; ++i) {
        append_bytes(out, HSD_GObj_804D7840[i]);
        append_process_chain(out, HSD_GObj_804D7840[i], false);
    }
    for (u32 i = 0; i < entity_count * proc_count; ++i)
        append_bytes(out, HSD_GObj_804D7844[i]);
    append_bytes(out, HSD_GObj_804D781C);
    append_bytes(out, HSD_GObj_804D7818);
    append_bytes(out, HSD_GObj_804D7814);
    append_bytes(out, HSD_GObj_804D7830);
    append_bytes(out, HSD_GObj_804D7834);
    append_bytes(out, HSD_GObj_804D7838);
    append_bytes(out, HSD_GObj_804D783C);
    append_bytes(out, HSD_GObj_804D7810);
    append_bytes(out, HSD_GObj_804CE3E4);
    std::vector<std::uint8_t> stage_info_bytes(stadium_screen_stage_info_size());
    require(stadium_screen_stage_info_snapshot(stage_info_bytes.data(),
                                                stage_info_bytes.size()),
            "Native StageInfo snapshot failed");
    out.insert(out.end(), stage_info_bytes.begin(), stage_info_bytes.end());
    return out;
}

inline void require_stage_empty() {
    require(stadium_screen_stage_is_empty(),
            "Synthetic source query started with published stage state");
    require(reinterpret_cast<HSD_GObj**>(HSD_GObj_Entities)[9] == nullptr,
            "Synthetic source query started with an item GObj");
}

inline std::vector<HSD_MObj*> live_materials_for_image(
    HSD_JObj* root_joint, HSD_ImageDesc* image, HSD_TObj* expected_tobj) {
    std::vector<HSD_MObj*> materials;
    std::vector<HSD_JObj*> pending{root_joint};
    std::unordered_set<HSD_JObj*> seen_joints;
    while (!pending.empty()) {
        HSD_JObj* joint = pending.back();
        pending.pop_back();
        require(joint && seen_joints.insert(joint).second,
                "Loaded live JObj query traversal repeats a node");
        if (union_type_dobj(joint)) {
            std::unordered_set<HSD_DObj*> seen_dobjs;
            for (HSD_DObj* dobj = joint->u.dobj; dobj; dobj = dobj->next) {
                require(seen_dobjs.insert(dobj).second,
                        "Loaded live DObj chain contains a cycle");
                HSD_MObj* material = dobj->mobj;
                std::unordered_set<HSD_TObj*> seen_tobjs;
                for (HSD_TObj* tobj = material ? material->tobj : nullptr;
                     tobj; tobj = tobj->next) {
                    require(seen_tobjs.insert(tobj).second,
                            "Loaded live TObj chain contains a cycle");
                    if (tobj->imagedesc == image) {
                        require(tobj == expected_tobj,
                                "Live source IMAGE reference differs from query result");
                        materials.push_back(material);
                    }
                }
            }
        }
        if (!(joint->flags & JOBJ_INSTANCE) && joint->child)
            pending.push_back(joint->child);
        if (joint->next) pending.push_back(joint->next);
    }
    return materials;
}

// Lazy class registration and allocator caches may remain after removal. Only
// live source objects and used pool entries must return to their baseline.
inline std::map<HSD_ClassInfo*, u32> live_class_counts() {
    std::map<HSD_ClassInfo*, u32> counts;
    std::vector<HSD_ClassInfo*> pending{&hsdClass};
    std::unordered_set<HSD_ClassInfo*> seen;
    while (!pending.empty()) {
        HSD_ClassInfo* info = pending.back();
        pending.pop_back();
        require(seen.insert(info).second, "Source class registry contains a cycle");
        if (info->head.nb_exist) counts.emplace(info, info->head.nb_exist);
        if (info->head.child) pending.push_back(info->head.child);
        if (info->head.next) pending.push_back(info->head.next);
    }
    return counts;
}

inline std::array<u32, 8> live_pool_counts() {
    return {HSD_ObjAllocGetUsing(HSD_AObjGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_RObjGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_RvalueObjGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_VecGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_MtxGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_RenderGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_TevRegGetAllocData()),
            HSD_ObjAllocGetUsing(HSD_ChanGetAllocData())};
}

class LoadedRoot {
public:
    explicit LoadedRoot(HSD_Joint* descriptor)
        : root_(HSD_JObjLoadJoint(descriptor)) {}
    ~LoadedRoot() { reset(); }
    LoadedRoot(const LoadedRoot&) = delete;
    LoadedRoot& operator=(const LoadedRoot&) = delete;
    HSD_JObj* get() const { return root_; }
    void reset() {
        if (root_) {
            HSD_JObjRemoveAll(root_);
            root_ = nullptr;
        }
    }
private:
    HSD_JObj* root_{};
};

class LocalQueryView {
public:
    HSD_GObj object{};
    ~LocalQueryView() { object.hsd_obj = nullptr; }
    LocalQueryView(const LocalQueryView&) = delete;
    LocalQueryView& operator=(const LocalQueryView&) = delete;
    LocalQueryView() = default;
};

inline void live_source_consumer_checks() {
    using namespace synthetic;
    const auto raw = fixture();
    const auto archive = std::make_shared<const DatArchive>(raw);
    const std::vector<std::uint8_t> decoded_before(archive->data().begin(),
                                                    archive->data().end());
    const auto stats_before = melee_web_gameplay_stats();
    require_stage_empty();

    for (unsigned lifetime = 0; lifetime < 2; ++lifetime) {
        DatNativeMap map(archive, contract());
        DatSis sis(archive, sis_name);
        HSD_Joint* descriptor = static_cast<HSD_Joint*>(
            stadium_screen_map_entry_joint(map.map_head(), 1));
        LoadedJointPlan plan = checked_load_plan(descriptor);
        const BorrowedTextureState borrowed = snapshot_borrowed_textures();
        require(tobj_toon == nullptr || tobj_toon->imagedesc == nullptr,
                "Synthetic load would compile a borrowed toon IMAGE");
        require(tobj_shadows == nullptr || !plan.has_shadow_material,
                "Synthetic load would compile a borrowed shadow chain");

        const auto runtime_before = runtime_roots_snapshot();
        const auto classes_before = live_class_counts();
        const auto pools_before = live_pool_counts();
        const u32 ids_before = HSD_ObjAllocGetUsing(HSD_IDGetAllocData());
        const u32 texture_bounds_before = melee_web_texture_bounds_live();
        for (HSD_Joint* joint : plan.joints)
            require(HSD_IDGetDataFromTable(
                        nullptr, static_cast<u32>(reinterpret_cast<std::uintptr_t>(joint)),
                        nullptr) == nullptr,
                    "Synthetic candidate descriptor already has an HSD ID binding");

        auto* image = static_cast<HSD_ImageDesc*>(map.image_descriptor(0x3a0));
        require(image != nullptr, "Synthetic map-1 IMAGE accessor returned null");
        const MeleeWebArchiveSymbol symbols[] = {
            {"GrPs.usd", image_name, image},
            {"GrPs.usd", sis_name, sis.descriptor()},
        };
        Catalog catalog("GrPs.usd", symbols, 2);
        require(stadium_screen_source_public(catalog.handle, image_name) == image &&
                    stadium_screen_source_public(catalog.handle, sis_name) ==
                        sis.descriptor(),
                "Synthetic typed catalog lost borrowed IMAGE/SIS identity");
        DatNativeMap foreign(archive, contract());
        auto* foreign_image = static_cast<HSD_ImageDesc*>(
            foreign.image_descriptor(0x3a0));
        require(foreign_image && foreign_image != image,
                "Foreign map did not create a distinct IMAGE identity");

        LoadedRoot loaded(descriptor);
        require(loaded.get() != nullptr, "Original HSD_JObjLoadJoint returned null");
        require(live_class_counts() != classes_before,
                "Original JObj load did not record live source class ownership");
        require_borrowed_textures_unchanged(borrowed);
        require(runtime_roots_snapshot() == runtime_before,
                "Original JObj load changed entity/process/GX lists or StageInfo");
        require(melee_web_texture_bounds_live() == texture_bounds_before,
                "Original JObj load changed texture-bound live count");
        for (HSD_Joint* joint : plan.joints)
            require(HSD_IDGetDataFromTable(
                        nullptr, static_cast<u32>(reinterpret_cast<std::uintptr_t>(joint)),
                        nullptr) != nullptr,
                    "Original source load omitted a descriptor ID binding");

        LocalQueryView view;
        view.object.hsd_obj = loaded.get();
        const auto hit = stadium_screen_live_image_query(&view.object, image, nullptr);
        auto* material = static_cast<HSD_MObj*>(hit.material);
        auto* tobj = static_cast<HSD_TObj*>(hit.texture);
        require(tobj && material && tobj->imagedesc == image,
                "Original Stadium consumer missed canonical map-1 IMAGE");
        const auto live_materials =
            live_materials_for_image(loaded.get(), image, tobj);
        require(live_materials.size() == 1 && live_materials.front() == material,
                "Loaded map-1 IMAGE lacks one live owning material");

        HSD_MObj sentinel_storage{};
        HSD_MObj* sentinel = &sentinel_storage;
        const auto foreign_miss = stadium_screen_live_image_query(
            &view.object, foreign_image, sentinel);
        require(foreign_miss.texture == nullptr && foreign_miss.material == sentinel,
                "Foreign-owner IMAGE miss changed the material sentinel");
        HSD_GObj empty_view{};
        const auto empty_miss = stadium_screen_live_image_query(&empty_view, image, sentinel);
        require(empty_miss.texture == nullptr && empty_miss.material == sentinel,
                "Empty-view miss changed the material sentinel");

        view.object.hsd_obj = nullptr;
        loaded.reset();
        require_borrowed_textures_unchanged(borrowed);
        require(runtime_roots_snapshot() == runtime_before,
                "Original JObj removal changed entity/process/GX lists or StageInfo");
        require(melee_web_texture_bounds_live() == texture_bounds_before,
                "Original JObj removal changed texture-bound live count");
        require(live_class_counts() == classes_before &&
                    live_pool_counts() == pools_before,
                "Original JObj removal retained live source objects or pool entries");
        require(HSD_ObjAllocGetUsing(HSD_IDGetAllocData()) == ids_before,
                "Original JObj removal did not restore ID allocator usage");
        for (HSD_Joint* joint : plan.joints)
            require(HSD_IDGetDataFromTable(
                        nullptr, static_cast<u32>(reinterpret_cast<std::uintptr_t>(joint)),
                        nullptr) == nullptr,
                    "Original JObj removal retained a descriptor ID binding");
        require(std::equal(archive->data().begin(), archive->data().end(),
                           decoded_before.begin()),
                "Original JObj load/removal changed decoded synthetic archive bytes");
        require_stage_empty();
    }
    const auto stats_after = melee_web_gameplay_stats();
    require(stats_after.generation == stats_before.generation &&
                stats_after.ticks == stats_before.ticks,
            "Source consumer query changed runtime generation/ticks");
}

}  // namespace melee_web::test::stadium_screen
