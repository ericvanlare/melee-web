#include "asset_scene.h"
#include "rigid_model.hpp"
#include "gx_material.hpp"
#include "hsd_transform_bridge.h"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <algorithm>
#include <memory>
#include <map>
#include <cmath>
#include <limits>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
std::shared_ptr<const melee_web::DatArchive> loaded_archive;
struct PreparedScene {
    struct ViewMatrix { Mtx value; };
    melee_web::RigidModel model;
    std::vector<MeleeWebJointTransform> joints;
    std::vector<ViewMatrix> view_matrices;
    std::vector<melee_web::GxTexture> textures;
    std::vector<int> texture_indices;
    Mtx camera{};

    PreparedScene(std::shared_ptr<const melee_web::DatArchive> archive, const char* name)
        : model(std::move(archive), name), joints(model.joints.size()) {
        char error[256];
        for (size_t i = 0; i < model.joints.size(); ++i) {
            const auto& node = model.joints[i];
            const auto* parent = node.parent == melee_web::RigidJoint::no_parent ? nullptr : &joints.at(node.parent);
            if (!melee_web_joint_transform(node.flags, node.scale.data(), node.rotation.data(),
                                           node.translation.data(), parent, &joints[i], error, sizeof(error)))
                throw melee_web::DatError(error);
        }
        std::array<float, 3> low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
        std::map<uint32_t, int> texture_cache;
        for (const auto& mesh : model.meshes) {
            int texture_index = -1;
            if (mesh.texture) {
                auto [entry, inserted] = texture_cache.emplace(mesh.texture->descriptor_offset, int(textures.size()));
                if (inserted) textures.emplace_back(*mesh.texture);
                texture_index = entry->second;
            }
            texture_indices.push_back(texture_index);
            if (model.joints[mesh.joint_index].flags & 0x10) continue; // HSD hides this joint's own objects.
            const auto& matrix = joints[mesh.joint_index].matrix;
            // Transform local AABB corners for an inspection-camera bound only.
            // Original geometry is never rebaked or rewritten for host endianness.
            for (unsigned corner = 0; corner < 8; ++corner) {
                const float point[3] = {corner & 1 ? mesh.maximum[0] : mesh.minimum[0],
                                        corner & 2 ? mesh.maximum[1] : mesh.minimum[1],
                                        corner & 4 ? mesh.maximum[2] : mesh.minimum[2]};
                for (size_t axis = 0; axis < 3; ++axis) {
                    float value = matrix[axis][3];
                    for (size_t k = 0; k < 3; ++k) value += matrix[axis][k] * point[k];
                    if (!std::isfinite(value)) throw melee_web::DatError("Transformed model bounds are nonfinite");
                    low[axis] = std::min(low[axis], value);
                    high[axis] = std::max(high[axis], value);
                }
            }
        }
        const float width = high[0] - low[0], height = high[1] - low[1], depth = high[2] - low[2];
        const float extent = std::max(height, width * 0.75f);
        if (!std::isfinite(extent) || extent <= 0 || !std::isfinite(depth))
            throw melee_web::DatError("Model has no finite visible surface extent");
        const float scale = 1.6f / extent, z_scale = 0.8f / std::max({width, height, depth});
        const float cx = low[0] + width * 0.5f, cy = low[1] + height * 0.5f, cz = low[2] + depth * 0.5f;
        camera[0][0] = scale * 0.75f; camera[0][3] = -cx * camera[0][0];
        camera[1][1] = scale; camera[1][3] = -cy * scale;
        camera[2][2] = z_scale; camera[2][3] = -0.5f - cz * z_scale;
        for (const auto& row : camera) for (float value : row)
            if (!std::isfinite(value)) throw melee_web::DatError("Inspection camera is nonfinite");
        view_matrices.resize(joints.size());
        for (size_t i = 0; i < joints.size(); ++i) {
            if (!melee_web_joint_view_matrix(camera, &joints[i], view_matrices[i].value, error, sizeof(error)))
                throw melee_web::DatError(error);
        }
    }
};
std::unique_ptr<PreparedScene> scene;
std::string message;
}

extern "C" {
void melee_web_asset_clear(void) {
    scene.reset();
    loaded_archive.reset();
    message.clear();
}

int melee_web_asset_open(const void* bytes, uint32_t size) {
    scene.reset();
    loaded_archive.reset();
    try {
        if (!bytes || !size) throw melee_web::DatError("Asset is empty");
        loaded_archive = std::make_shared<melee_web::DatArchive>(
            std::span(static_cast<const uint8_t*>(bytes), size));
        message = "Archive validated. Select a supported rigid model symbol.";
        return static_cast<int>(loaded_archive->public_symbols().size());
    } catch (const std::exception& error) {
        message = error.what();
        return -1;
    }
}

const char* melee_web_asset_symbol(uint32_t index) {
    if (!loaded_archive || index >= loaded_archive->public_symbols().size()) return nullptr;
    return loaded_archive->public_symbols()[index].name.c_str();
}

int melee_web_asset_select(uint32_t index) {
    scene.reset();
    try {
        const char* name = melee_web_asset_symbol(index);
        if (!name) throw melee_web::DatError("Select a public model symbol");
        scene = std::make_unique<PreparedScene>(loaded_archive, name);
        message = std::to_string(scene->model.joints.size()) + " joints · " +
                  std::to_string(scene->model.meshes.size()) + " meshes · " +
                  std::to_string(scene->textures.size()) + " textures · " +
                  std::to_string(scene->model.draw_packets) + " primitive packets · " +
                  std::to_string(scene->model.submitted_vertices) + " submitted vertices";
        return 1;
    } catch (const std::exception& error) {
        message = error.what();
        return 0;
    }
}

const char* melee_web_asset_message(void) { return message.c_str(); }

int melee_web_asset_draw(void) {
    if (!scene) return 0;
    for (size_t i = 0; i < scene->model.meshes.size(); ++i) {
        const auto& mesh = scene->model.meshes[i];
        if (scene->model.joints[mesh.joint_index].flags & 0x10) continue;
        GXLoadPosMtxImm(scene->view_matrices[mesh.joint_index].value, GX_PNMTX0);
        const int texture_index = scene->texture_indices[i];
        melee_web::apply_diffuse_material({mesh.diffuse[0], mesh.diffuse[1], mesh.diffuse[2], mesh.diffuse[3]},
                                          texture_index < 0 ? nullptr : &scene->textures[texture_index]);
        const MeleeWebPObjView view{mesh.attributes.data(), uint32_t(mesh.attributes.size()),
                                   mesh.display, mesh.display_bytes, mesh.flags};
        char error[256];
        if (!melee_web_pobj_draw(&view, error, sizeof(error))) {
            message = error;
            scene.reset();
#ifdef __EMSCRIPTEN__
            EM_ASM({ window.assetFailed(UTF8ToString($0)); }, message.c_str());
#endif
            return 0;
        }
    }
    return 1;
}
}
