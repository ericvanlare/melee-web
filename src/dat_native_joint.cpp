#include "dat_native_joint.hpp"
#include <algorithm>
#include <map>

namespace melee_web {
struct DatNativeJoint::Storage {
    RigidModel model;
    MeleeWebNativeGraph graph{};
    std::vector<MeleeWebNativeJointDesc> joints;
    std::vector<MeleeWebNativeDObjDesc> dobjs;
    std::vector<MeleeWebNativePObjDesc> pobjs;
    std::vector<MeleeWebNativeMaterialDesc> materials;
    std::vector<std::vector<MeleeWebNativeTextureDesc>> textures;
    std::vector<std::vector<MeleeWebSkinEnvelope>> envelopes;
    Storage(std::shared_ptr<const DatArchive> a, uint32_t root)
        : model(std::move(a), root, "native HSD joint", ModelRenderPass::All) {}
};

DatNativeJoint::DatNativeJoint(std::shared_ptr<const DatArchive> archive, uint32_t root)
    : storage_(std::make_unique<Storage>(std::move(archive), root)) {
    auto& s = *storage_;
    const auto& a = *s.model.archive;
    std::map<uint32_t, uint32_t> joint_ids, dobj_ids, pobj_ids, material_ids;
    for (const auto& j : s.model.joints) joint_ids.emplace(j.descriptor_offset, uint32_t(joint_ids.size()));
    // The source loader creates one runtime object per descriptor occurrence.
    // Shared DObj/PObj descriptors remain shared descriptor identities; original
    // loading still allocates their separate runtime occurrences as usual.
    for (const auto& j : s.model.joints) {
        for (auto d = a.pointer(j.descriptor_offset + 16, 16); d; d = a.pointer(*d + 4, 16))
            if (!dobj_ids.contains(*d)) dobj_ids.emplace(*d, uint32_t(dobj_ids.size()));
    }
    for (const auto& m : s.model.meshes) {
        if (!pobj_ids.contains(m.descriptor_offset)) pobj_ids.emplace(m.descriptor_offset, uint32_t(pobj_ids.size()));
        if (!material_ids.contains(m.material->descriptor_offset))
            material_ids.emplace(m.material->descriptor_offset, uint32_t(material_ids.size()));
    }
    // DObjs with empty PObj chains still have a real native material.
    std::map<uint32_t, DatMaterial> empty_materials;
    for (const auto& [offset, unused] : dobj_ids) {
        (void) unused;
        const auto material = *a.pointer(offset + 8, 24);
        if (!material_ids.contains(material)) {
            material_ids.emplace(material, uint32_t(material_ids.size()));
            empty_materials.emplace(material, read_dat_material(a, material));
        }
    }
    const auto index = [&](const auto& ids, uint32_t slot) {
        const auto p = a.pointer(slot);
        return p ? ids.at(*p) : UINT32_MAX;
    };
    s.joints.resize(joint_ids.size()); s.dobjs.resize(dobj_ids.size());
    s.pobjs.resize(pobj_ids.size()); s.envelopes.resize(pobj_ids.size());
    s.materials.resize(material_ids.size()); s.textures.resize(material_ids.size());
    for (const auto& j : s.model.joints) {
        auto& out = s.joints.at(joint_ids.at(j.descriptor_offset));
        out.source_offset = j.descriptor_offset; out.flags = j.flags;
        out.child = index(joint_ids, j.descriptor_offset + 8);
        out.next = index(joint_ids, j.descriptor_offset + 12);
        out.dobj = index(dobj_ids, j.descriptor_offset + 16);
        std::copy(j.rotation.begin(), j.rotation.end(), out.rotation);
        std::copy(j.scale.begin(), j.scale.end(), out.scale);
        std::copy(j.translation.begin(), j.translation.end(), out.translation);
        if (j.inverse_bind) {
            out.has_inverse_bind = 1;
            std::copy(j.inverse_bind->begin(), j.inverse_bind->end(), &out.inverse_bind[0][0]);
        }
    }
    for (const auto& [offset, id] : dobj_ids)
        s.dobjs.at(id) = {offset, index(dobj_ids, offset + 4), index(material_ids, offset + 8), index(pobj_ids, offset + 12)};
    for (const auto& mesh : s.model.meshes) {
        const auto id = pobj_ids.at(mesh.descriptor_offset);
        auto& out = s.pobjs.at(id);
        if (out.geometry.attributes) continue;
        out.source_offset = mesh.descriptor_offset;
        out.next = index(pobj_ids, mesh.descriptor_offset + 4);
        out.geometry = {mesh.attributes.data(), uint32_t(mesh.attributes.size()), mesh.display, mesh.display_bytes, mesh.flags};
        for (const auto& e : mesh.envelopes)
            s.envelopes.at(id).push_back({e.influences.data(), uint32_t(e.influences.size())});
        out.envelopes = s.envelopes.at(id).data();
        out.envelope_count = uint32_t(s.envelopes.at(id).size());
    }
    std::vector<bool> copied_materials(s.materials.size(), false);
    const auto copy_material = [&](const DatMaterial& m) {
        const auto id = material_ids.at(m.descriptor_offset);
        auto& out = s.materials.at(id);
        if (copied_materials.at(id)) return;
        copied_materials.at(id) = true;
        out.source_offset = m.descriptor_offset;
        auto& mat = out.material;
        mat.rendermode = m.render_mode; mat.alpha = m.alpha; mat.shininess = m.shininess;
        std::copy(m.ambient.begin(), m.ambient.end(), mat.ambient);
        std::copy(m.diffuse.begin(), m.diffuse.end(), mat.diffuse);
        std::copy(m.specular.begin(), m.specular.end(), mat.specular);
        for (const auto& t : m.textures) {
            MeleeWebNativeTextureDesc n{};
            n.source_offset = t.descriptor_offset; n.source_id = t.id; n.has_lod = t.lod_descriptor_offset.has_value();
            auto& o = n.texture;
            o.flags = t.source_flags; o.source = t.source;
            std::copy(t.rotation.begin(), t.rotation.end(), o.rotation);
            std::copy(t.scale.begin(), t.scale.end(), o.scale);
            std::copy(t.translation.begin(), t.translation.end(), o.translation);
            o.wrap_s = t.sampler.wrap_s; o.wrap_t = t.sampler.wrap_t;
            o.repeat_s = t.repeat_s; o.repeat_t = t.repeat_t; o.blending = t.blending;
            o.min_filter = t.sampler.min_filter; o.mag_filter = t.sampler.mag_filter;
            o.anisotropy = t.sampler.anisotropy; o.lod_bias = t.sampler.lod_bias;
            o.bias_clamp = t.sampler.bias_clamp; o.edge_lod = t.sampler.edge_lod;
            o.image_data = t.image.bytes.data(); o.image_bytes = uint32_t(t.image.bytes.size());
            o.width = t.image.width; o.height = t.image.height; o.format = t.image.format;
            o.mipmap = t.image.mipmap; o.min_lod = t.image.min_lod; o.max_lod = t.image.max_lod;
            if (t.palette) {
                n.palette_name = t.palette->source_name;
                o.palette_data = t.palette->bytes.data(); o.palette_bytes = uint32_t(t.palette->bytes.size());
                o.palette_format = t.palette->format; o.palette_entries = t.palette->entries;
            }
            s.textures.at(id).push_back(n);
        }
        mat.texture_count = uint32_t(s.textures.at(id).size());
        out.textures = s.textures.at(id).data();
    };
    for (const auto& m : s.model.meshes) copy_material(*m.material);
    for (const auto& [offset, m] : empty_materials) { (void) offset; copy_material(m); }
    s.graph = {s.joints.data(), s.dobjs.data(), s.pobjs.data(), s.materials.data(),
               uint32_t(s.joints.size()), uint32_t(s.dobjs.size()), uint32_t(s.pobjs.size()),
               uint32_t(s.materials.size()), joint_ids.at(root)};
}
DatNativeJoint::~DatNativeJoint() = default;
const MeleeWebNativeGraph& DatNativeJoint::graph() const noexcept { return storage_->graph; }
}
