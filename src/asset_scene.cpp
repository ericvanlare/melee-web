#include "asset_scene.h"
#include "rigid_model.hpp"
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <algorithm>
#include <memory>
#include <string>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace {
std::shared_ptr<const melee_web::DatArchive> loaded_archive;
std::unique_ptr<melee_web::RigidModel> model;
std::string message;
}

extern "C" {
void melee_web_asset_clear(void) {
    model.reset();
    loaded_archive.reset();
    message.clear();
}

int melee_web_asset_open(const void* bytes, uint32_t size) {
    model.reset();
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
    model.reset();
    try {
        const char* name = melee_web_asset_symbol(index);
        if (!name) throw melee_web::DatError("Select a public model symbol");
        model = std::make_unique<melee_web::RigidModel>(loaded_archive, name);
        message = std::to_string(model->meshes.size()) + " meshes · " +
                  std::to_string(model->draw_packets) + " primitive packets · " +
                  std::to_string(model->submitted_vertices) + " submitted vertices";
        return 1;
    } catch (const std::exception& error) {
        message = error.what();
        return 0;
    }
}

const char* melee_web_asset_message(void) { return message.c_str(); }

int melee_web_asset_draw(void) {
    if (!model) return 0;
    // Normalize only the inspection camera. Archive coordinates remain unchanged.
    const float width = model->maximum[0] - model->minimum[0];
    const float height = model->maximum[1] - model->minimum[1];
    const float depth = model->maximum[2] - model->minimum[2];
    const float scale = 1.6f / std::max(height, width * 0.75f);
    const float z_scale = 0.8f / std::max({width, height, depth});
    const float cx = (model->maximum[0] + model->minimum[0]) * 0.5f;
    const float cy = (model->maximum[1] + model->minimum[1]) * 0.5f;
    const float cz = (model->maximum[2] + model->minimum[2]) * 0.5f;
    Mtx transform = {{scale * 0.75f, 0, 0, -cx * scale * 0.75f},
                     {0, scale, 0, -cy * scale},
                     {0, 0, z_scale, -0.5f - cz * z_scale}};
    GXLoadPosMtxImm(transform, GX_PNMTX0);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG,
                  GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    for (const auto& mesh : model->meshes) {
        GXSetChanMatColor(GX_COLOR0A0, {mesh.diffuse[0], mesh.diffuse[1], mesh.diffuse[2], mesh.diffuse[3]});
        const MeleeWebPObjView view{mesh.attributes.data(), uint32_t(mesh.attributes.size()),
                                   mesh.display, mesh.display_bytes, mesh.flags};
        char error[256];
        if (!melee_web_pobj_draw(&view, error, sizeof(error))) {
            message = error;
            model.reset();
#ifdef __EMSCRIPTEN__
            EM_ASM({ window.assetFailed(UTF8ToString($0)); }, message.c_str());
#endif
            return 0;
        }
    }
    return 1;
}
}
