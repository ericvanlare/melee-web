#include "asset_scene.h"
#include "rigid_model.hpp"
#include "hsd_material.hpp"
#include "hsd_inspection.h"
#include "hsd_transform_bridge.h"
#include "dat_animation.hpp"
#include "dat_fighter.hpp"
#include "animation_clock.hpp"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
using melee_web::DatError;
using melee_web::AnimationPose;
constexpr float identity[3][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
std::shared_ptr<const melee_web::DatArchive> loaded_archive;
bool animation_visible = true;
struct PreparedScene {
    struct ViewMatrix { Mtx value; };
    struct PreparedMesh {
        std::vector<MeleeWebSkinEnvelope> envelopes;
        MeleeWebPObjPalette palette{};
        uint64_t revision = 0;
    };
    // Archive-backed spans outlive every hydrated material and envelope view.
    melee_web::RigidModel model;
    std::vector<AnimationPose> bind_pose;
    std::vector<MeleeWebJointTransform> joints;
    std::vector<ViewMatrix> view_matrices, normal_matrices;
    std::vector<melee_web::HsdMaterialHandle> materials;
    std::vector<size_t> material_indices;
    std::vector<PreparedMesh> meshes;
    std::vector<bool> visible_dobjs;
    std::vector<MeleeWebSkinJoint> skin_joints;
    std::unique_ptr<MeleeWebSkinSkeleton, decltype(&melee_web_skin_destroy)> skin{nullptr, &melee_web_skin_destroy};
    std::unique_ptr<melee_web::HsdAnimation> animation;
    melee_web::AnimationClock clock;
    std::string animation_name, playback_notice;
    float end_frame = 0;
    bool playing = false;
    uint64_t pose_revision = 0;
    size_t texture_count = 0, skinned_meshes = 0;
    Mtx camera{};
    Mtx44 projection{};

    PreparedScene(std::shared_ptr<const melee_web::DatArchive> archive, const char* name)
        : model(std::move(archive), name), bind_pose(model.joints.size()), joints(model.joints.size()),
          view_matrices(joints.size()), normal_matrices(joints.size()), meshes(model.meshes.size()),
          visible_dobjs(model.dobj_count, true) {
        for (size_t i = 0; i < model.joints.size(); ++i) {
            const auto& node = model.joints[i];
            auto& pose = bind_pose[i];
            std::copy(node.rotation.begin(), node.rotation.end(), pose.rotation);
            std::copy(node.translation.begin(), node.translation.end(), pose.translation);
            std::copy(node.scale.begin(), node.scale.end(), pose.scale);
            pose.flags = node.flags;
        }
        for (size_t i = 0; i < meshes.size(); ++i) {
            for (const auto& envelope : model.meshes[i].envelopes)
                meshes[i].envelopes.push_back({envelope.influences.data(), uint32_t(envelope.influences.size())});
            if (!meshes[i].envelopes.empty()) ++skinned_meshes;
        }
        if (skinned_meshes) skin_joints.resize(joints.size());
        update_world();
        std::map<uint32_t, size_t> material_cache;
        std::set<uint32_t> texture_offsets;
        for (const auto& mesh : model.meshes) {
            auto [entry, inserted] = material_cache.emplace(mesh.material->descriptor_offset, materials.size());
            if (inserted) materials.push_back(melee_web::make_hsd_material(*mesh.material));
            material_indices.push_back(entry->second);
            for (const auto& texture : mesh.material->textures) texture_offsets.insert(texture.descriptor_offset);
        }
        texture_count = texture_offsets.size();
        fit_camera();
        update_view();
    }

    void update_world() {
        char error[256];
        const auto pose = animation ? animation->pose() : std::span<const AnimationPose>(bind_pose);
        for (size_t i = 0; i < joints.size(); ++i) {
            const auto& node = model.joints[i];
            const auto* parent = node.parent == melee_web::RigidJoint::no_parent ? nullptr : &joints.at(node.parent);
            if (!melee_web_joint_transform(pose[i].flags, pose[i].scale, pose[i].rotation,
                                          pose[i].translation, parent, &joints[i], error, sizeof(error)))
                throw DatError(error);
            if (!skin_joints.empty()) {
                auto& out = skin_joints[i];
                out.flags = pose[i].flags; out.parent = node.parent;
                std::memcpy(out.world, joints[i].matrix, sizeof(out.world));
                out.has_inverse_bind = node.inverse_bind.has_value();
                if (node.inverse_bind) std::memcpy(out.inverse_bind, node.inverse_bind->data(), sizeof(out.inverse_bind));
            }
        }
        if (!skin_joints.empty()) {
            if (!skin) {
                skin.reset(melee_web_skin_create(skin_joints.data(), uint32_t(skin_joints.size()), error, sizeof(error)));
                if (!skin) throw DatError(error);
            } else if (!melee_web_skin_update(skin.get(), skin_joints.data(), uint32_t(skin_joints.size()), error, sizeof(error))) {
                throw DatError(error);
            }
        }
        ++pose_revision;
    }

    void update_view() {
        char error[256];
        for (size_t i = 0; i < joints.size(); ++i) {
            if (!melee_web_joint_view_matrix(camera, &joints[i], view_matrices[i].value, error, sizeof(error)) ||
                !melee_web_joint_normal_matrix(view_matrices[i].value, normal_matrices[i].value, error, sizeof(error)))
                throw DatError(error);
        }
    }

    void prepare_palette(size_t index, const float view[3][4], MeleeWebPObjPalette& output) const {
        const auto& mesh = model.meshes[index];
        const auto& envelopes = meshes[index].envelopes;
        char error[256];
        if (!melee_web_pobj_prepare_palette(skin.get(), mesh.joint_index, envelopes.data(),
            uint32_t(envelopes.size()), view, mesh.material->render_mode, &output, error, sizeof(error)))
            throw DatError(error);
    }

    void fit_camera() {
        std::array<float, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        auto include_box = [&](const float matrix[3][4], const auto& minimum, const auto& maximum) {
            for (unsigned corner = 0; corner < 8; ++corner) {
                const float point[3] = {corner & 1 ? maximum[0] : minimum[0],
                                       corner & 2 ? maximum[1] : minimum[1],
                                       corner & 4 ? maximum[2] : minimum[2]};
                for (size_t axis = 0; axis < 3; ++axis) {
                    float value = matrix[axis][3];
                    for (size_t k = 0; k < 3; ++k) value += matrix[axis][k] * point[k];
                    if (!std::isfinite(value)) throw DatError("Transformed model bounds are nonfinite");
                    low[axis] = std::min(low[axis], value); high[axis] = std::max(high[axis], value);
                }
            }
        };
        for (size_t i = 0; i < meshes.size(); ++i) {
            const auto& mesh = model.meshes[i];
            if (!visible_dobjs[mesh.dobj_index] || (model.joints[mesh.joint_index].flags & 0x10)) continue;
            if (meshes[i].envelopes.empty()) {
                include_box(joints[mesh.joint_index].matrix, mesh.minimum, mesh.maximum);
            } else {
                // Original envelope matrices with an identity view give world
                // bounds. Do not apply the mesh joint twice to bound vertices.
                MeleeWebPObjPalette world{};
                prepare_palette(i, identity, world);
                for (uint32_t slot = 0; slot < world.count; ++slot)
                    if (mesh.palette_used_mask & (1U << slot))
                        include_box(world.position[slot], mesh.palette_minimum[slot], mesh.palette_maximum[slot]);
            }
        }
        const float width = high[0] - low[0], height = high[1] - low[1], depth = high[2] - low[2];
        const float extent = std::max(height, width * .75f);
        if (!std::isfinite(extent) || extent <= 0 || !std::isfinite(depth))
            throw DatError("Model has no finite visible surface extent");
        const float max_dimension = std::max({width, height, depth});
        const float distance = 2.f * max_dimension + 1.f;
        const float cx = low[0] + width * .5f, cy = low[1] + height * .5f, cz = low[2] + depth * .5f;
        camera[0][0] = camera[1][1] = camera[2][2] = 1;
        camera[0][3] = -cx; camera[1][3] = -cy; camera[2][3] = -cz - distance;
        char projection_error[256];
        if (!melee_web_inspection_projection(extent, max_dimension, distance,
                projection, projection_error, sizeof(projection_error)))
            throw DatError(projection_error);
        for (const auto& row : camera) for (float value : row)
            if (!std::isfinite(value)) throw DatError("Inspection camera is nonfinite");
        for (const auto& row : projection) for (float value : row)
            if (!std::isfinite(value)) throw DatError("Inspection projection is nonfinite");
    }
};
std::unique_ptr<PreparedScene> scene;
std::string message, animation_message = "Load a model, then its animation archive.";
std::string fighter_message = "Load a fighter model, then its fighter metadata.";

void discard_animation() {
    if (!scene) return;
    scene->animation.reset(); scene->playing = false; scene->clock.reset();
    scene->animation_name.clear(); scene->playback_notice.clear();
    scene->update_world(); scene->update_view();
}
}

extern "C" {
void melee_web_asset_clear(void) {
    scene.reset(); loaded_archive.reset(); message.clear();
    animation_message = "Load a model, then its animation archive.";
    fighter_message = "Load a fighter model, then its fighter metadata.";
}
int melee_web_asset_open(const void* bytes, uint32_t size) {
    melee_web_asset_clear();
    try {
        if (!bytes || !size) throw DatError("Asset is empty");
        loaded_archive = std::make_shared<melee_web::DatArchive>(std::span(static_cast<const uint8_t*>(bytes), size));
        message = "Archive validated. Select a supported model symbol.";
        return static_cast<int>(loaded_archive->public_symbols().size());
    } catch (const std::exception& error) { message = error.what(); return -1; }
}
const char* melee_web_asset_symbol(uint32_t index) {
    if (!loaded_archive || index >= loaded_archive->public_symbols().size()) return nullptr;
    return loaded_archive->public_symbols()[index].name.c_str();
}
int melee_web_asset_select(uint32_t index) {
    scene.reset(); animation_message = "Load a model, then its animation archive.";
    fighter_message = "Load a fighter model, then its fighter metadata.";
    try {
        const char* name = melee_web_asset_symbol(index);
        if (!name) throw DatError("Select a public model symbol");
        scene = std::make_unique<PreparedScene>(loaded_archive, name);
        fighter_message = scene->skinned_meshes ? "Raw model: alternate representations may overlap. Load fighter metadata to select normal geometry." : "Ordinary model loaded; fighter metadata is optional.";
        message = std::to_string(scene->model.joints.size()) + " joints · " +
                  std::to_string(scene->model.meshes.size()) + " meshes (" + std::to_string(scene->skinned_meshes) + " skinned) · " +
                  std::to_string(scene->texture_count) + " textures · " +
                  std::to_string(scene->model.draw_packets) + " primitive packets · " +
                  std::to_string(scene->model.submitted_vertices) + " submitted vertices";
        return 1;
    } catch (const std::exception& error) { message = error.what(); return 0; }
}
const char* melee_web_asset_message(void) { return message.c_str(); }

int melee_web_asset_fighter_open(const void* bytes, uint32_t size) {
    try {
        if (!scene) throw DatError("Load a fighter model before its metadata");
        if (!bytes || !size) throw DatError("Fighter metadata archive is empty");
        // Source binding: ftMr_Init_DataName and ftMr_Init_CostumeStrings[0]
        // in ftmariostrings.c. Costume identity is not stored in the DAT.
        if (scene->model.symbol != "PlyMario5K_Share_joint")
            throw DatError("Fighter metadata binding is currently verified for default Mario only");
        const melee_web::DatArchive archive(std::span(static_cast<const uint8_t*>(bytes), size));
        const melee_web::DatFighterParts parts(archive, "ftDataMario", 0);
        const auto indices = parts.normal_dobj_indices(scene->model.dobj_count);
        std::vector<bool> selected(scene->model.dobj_count, false);
        for (auto index : indices) selected[index] = true;
        // Commit a complete checked mask. Keep the inspection camera fixed so
        // loading metadata during animation cannot change its framing.
        scene->visible_dobjs = std::move(selected);
        size_t count = 0;
        for (const auto& mesh : scene->model.meshes) count += scene->visible_dobjs[mesh.dobj_index];
        fighter_message = "Normal representation · " + std::to_string(indices.size()) + " / " +
            std::to_string(scene->model.dobj_count) + " display objects · " + std::to_string(count) +
            " / " + std::to_string(scene->model.meshes.size()) + " meshes · category 0, variant 0";
        return 1;
    } catch (const std::exception& error) {
        fighter_message = "Metadata rejected; previous visibility retained: " + std::string(error.what());
        return 0;
    }
}
const char* melee_web_asset_fighter_message(void) { return fighter_message.c_str(); }

int melee_web_asset_animation_open(const void* bytes, uint32_t size) {
    try {
        if (!scene) throw DatError("Load a model before its animation");
        discard_animation();
        if (!bytes || !size) throw DatError("Animation archive is empty");
        // This initial binding is proven from PlCo's Mario parts table and the
        // original ftParts preorder walk. A matching count alone cannot prove
        // other fighters' alternate mappings; their typed mapping loader remains
        // required. The generic decoder/evaluator performs every validation.
        if (scene->model.symbol != "PlyMario5K_Share_joint" || scene->bind_pose.size() != 61)
            throw DatError("Animation binding is currently verified for the default Mario hierarchy only");
        const melee_web::DatArchive archive(std::span(static_cast<const uint8_t*>(bytes), size));
        const melee_web::DatPublicSymbol* selected = nullptr;
        for (const auto& symbol : archive.public_symbols()) {
            if (symbol.name.starts_with("PlyMario5K_Share_ACTION_") && symbol.name.ends_with("_figatree")) {
                if (selected) throw DatError("Animation archive contains multiple matching action roots");
                selected = &symbol;
            }
        }
        if (!selected) throw DatError("Animation does not match the loaded default Mario model");
        const melee_web::DatAnimation data(archive, selected->data_offset);
        if (data.node_counts.size() != scene->bind_pose.size()) throw DatError("Animation node count differs from the model");
        auto animation = std::make_unique<melee_web::HsdAnimation>(data, scene->bind_pose);
        animation->request(0); animation->advance();
        scene->animation = std::move(animation); scene->animation_name = selected->name;
        scene->end_frame = data.end_frame;
        scene->update_world(); scene->update_view();
        animation_message = "Animation loaded at frame 0. Press Play.";
        return 1;
    } catch (const std::exception& error) {
        const std::string reason = error.what();
        try { discard_animation(); } catch (...) { scene.reset(); }
        animation_message = "Animation rejected: " + reason;
        return 0;
    }
}
int melee_web_asset_animation_play(int enabled) {
    if (!scene || !scene->animation) return 0;
    scene->playing = enabled != 0; scene->clock.reset(); scene->playback_notice.clear();
    return scene->playing;
}
int melee_web_asset_animation_playing(void) { return scene && scene->animation && scene->playing; }
int melee_web_asset_animation_loaded(void) { return scene && scene->animation; }
void melee_web_asset_animation_visible(int visible) {
    animation_visible = visible != 0;
    if (scene) scene->clock.reset();
}
const char* melee_web_asset_animation_message(void) {
    if (scene && scene->animation) {
        animation_message = scene->animation_name + " · frame " + std::to_string(scene->animation->frame()) +
            " / " + std::to_string(scene->end_frame) + " · " + (scene->playing ? "playing at 60 Hz" : "paused");
        if (!scene->playback_notice.empty()) animation_message += " · " + scene->playback_notice;
    }
    return animation_message.c_str();
}
void melee_web_asset_tick(double now_ms) {
    if (!scene || !scene->animation) return;
    const auto tick = scene->clock.tick(now_ms, scene->playing && animation_visible);
    if (tick.stalled) {
        scene->playing = false; scene->playback_notice = "Long frame stall; press Play to resume."; return;
    }
    if (!tick.steps) return;
    try {
        for (unsigned i = 0; i < tick.steps; ++i) {
            melee_web::advance_inspection_loop(*scene->animation);
        }
        scene->update_world(); scene->update_view();
    } catch (const std::exception& error) {
        const std::string reason = error.what();
        try { discard_animation(); } catch (...) { scene.reset(); }
        animation_message = "Animation stopped: " + reason;
    }
}

int melee_web_asset_draw(void) {
    if (!scene) return 0;
    char error[256];
    try {
        GXSetProjection(scene->projection, GX_ORTHOGRAPHIC);
        if (!melee_web_inspection_begin(scene->camera, error, sizeof(error))) throw DatError(error);
        for (size_t i = 0; i < scene->model.meshes.size(); ++i) {
            const auto& mesh = scene->model.meshes[i];
            if (!scene->visible_dobjs[mesh.dobj_index] || (scene->model.joints[mesh.joint_index].flags & 0x10)) continue;
            if ((mesh.material->render_mode & 8) &&
                !melee_web_inspection_specular(scene->view_matrices[mesh.joint_index].value, error, sizeof(error)))
                throw DatError(error);
            auto* material = scene->materials[scene->material_indices[i]].get();
            melee_web_hsd_material_setup(material);
            const MeleeWebPObjView view{mesh.attributes.data(), uint32_t(mesh.attributes.size()),
                                       mesh.display, mesh.display_bytes, mesh.flags};
            bool drawn;
            try {
                auto& prepared = scene->meshes[i];
                if (prepared.envelopes.empty()) {
                    GXLoadPosMtxImm(scene->view_matrices[mesh.joint_index].value, GX_PNMTX0);
                    GXLoadNrmMtxImm(scene->normal_matrices[mesh.joint_index].value, GX_PNMTX0);
                    GXLoadTexMtxImm(scene->normal_matrices[mesh.joint_index].value, GX_TEXMTX0, GX_MTX3x4);
                    drawn = melee_web_pobj_draw(&view, error, sizeof(error));
                } else {
                    if (prepared.revision != scene->pose_revision) {
                        scene->prepare_palette(i, scene->camera, prepared.palette);
                        prepared.revision = scene->pose_revision;
                    }
                    drawn = melee_web_pobj_draw_palette(&view, &prepared.palette, error, sizeof(error));
                }
            } catch (...) { melee_web_hsd_material_unset(material); throw; }
            melee_web_hsd_material_unset(material);
            if (!drawn) throw DatError(error);
        }
        return 1;
    } catch (const std::exception& failure) {
        message = failure.what(); scene.reset();
        animation_message = "Animation unavailable: the model was discarded after a draw failure.";
        fighter_message = "Fighter metadata unavailable: the model was discarded after a draw failure.";
#ifdef __EMSCRIPTEN__
        EM_ASM({ window.assetFailed(UTF8ToString($0)); }, message.c_str());
#endif
        // The next tick resets the complete synthetic probe state.
        return 1;
    }
}
}
