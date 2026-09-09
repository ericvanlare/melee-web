#include "dat_native_joint.hpp"
#include "dat_material_animation.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/dobj.h>
#include <sysdolphin/baselib/mobj.h>
#pragma GCC diagnostic pop
#include "dat_common.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_common_context.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static std::shared_ptr<const melee_web::DatArchive> read_archive(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Unable to open supplied archive");
    file.seekg(0, std::ios::end);
    const auto length = file.tellg();
    if (length < 0 || static_cast<uint64_t>(length) > melee_web::DatArchive::max_archive_bytes)
        throw std::runtime_error("Supplied archive exceeds input bound");
    file.seekg(0);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return std::make_shared<melee_web::DatArchive>(bytes);
}
static unsigned check_texture_indices(HSD_JObj* joint, unsigned expected) {
    unsigned found = 0;
    for (auto* j = joint; j; j = j->next) {
        for (auto* d = j->u.dobj; d; d = d->next)
            for (auto* t = d->mobj->tobj; t; t = t->next)
                if (t->aobj) {
                    if (!t->imagetbl || t->imagedesc != t->imagetbl[expected] || t->tlut_no != expected)
                        throw std::runtime_error("Original texture animation selected the wrong image or palette");
                    ++found;
                }
        found += check_texture_indices(j->child, expected);
    }
    return found;
}
int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 4)
            throw std::runtime_error("Usage: hsd_native_dat_probe PlCo.dat [costume.dat exact_public_joint_symbol]");
        auto archive = read_archive(argv[1]);
        const melee_web::DatCommon common(*archive);
        if (!common.roots[16].data_offset) throw std::runtime_error("Common root16 accessory is absent");
        if (!common.roots[20].data_offset) throw std::runtime_error("Common root20 is absent");
        melee_web::DatNativeJoint graph(archive, *common.roots[20].data_offset);
        melee_web::DatNativeJoint root16_graph(archive, *common.roots[16].data_offset);
        std::unique_ptr<melee_web::DatNativeJoint> costume;
        std::unique_ptr<melee_web::DatMaterialAnimation> material_animation;
        if (argc == 4) {
            auto costume_archive = read_archive(argv[2]);
            for (const auto& symbol : costume_archive->public_symbols())
                if (symbol.name == argv[3])
                    costume = std::make_unique<melee_web::DatNativeJoint>(costume_archive, symbol.data_offset);
            if (!costume) throw std::runtime_error("Exact requested costume joint symbol is absent");
            const std::string model_symbol = argv[3];
            if (model_symbol == "PlyMario5K_Share_joint") {
                for (const auto& symbol : costume_archive->public_symbols())
                    if (symbol.name == "PlyMario5K_Share_matanim_joint")
                        material_animation = std::make_unique<melee_web::DatMaterialAnimation>(
                            costume_archive, symbol.data_offset, costume->graph());
                if (!material_animation) throw std::runtime_error("Exact Mario material animation symbol is absent");
            }
        }
        const auto& color = common.scalars.x7D8;
        const uint8_t diffuse[4] = {color.r, color.g, color.b, color.a};
        char error[256];
        for (unsigned iteration = 0; iteration < 2; ++iteration) {
            if (!melee_web_gameplay_startup(8U * 1024U * 1024U, error, sizeof(error))) throw std::runtime_error(error);
            auto* native = melee_web_native_common_joint_create(&graph.graph(), diffuse, error, sizeof(error));
            if (!native) throw std::runtime_error(error);
            MeleeWebNativeJointStats stats{};
            if (!melee_web_native_joint_stats(native, &stats, error, sizeof(error))) throw std::runtime_error(error);
            for (unsigned i = 0; i < 4; ++i)
                if (stats.first_diffuse[i] != diffuse[i]) throw std::runtime_error("Original common material color did not match root0.x7D8");
            std::cout << "root20 native original consumer: " << stats.joints << " joints, " << stats.dobjs
                      << " DObjs, " << stats.pobjs << " PObjs, " << stats.materials << " materials, "
                      << stats.textures << " textures; diffuse=" << unsigned(diffuse[0]) << ','
                      << unsigned(diffuse[1]) << ',' << unsigned(diffuse[2]) << ',' << unsigned(diffuse[3]) << '\n';
            MeleeWebNativeJoint* native_costume = nullptr;
            if (costume) {
                native_costume = melee_web_native_joint_create(&costume->graph(), error, sizeof(error));
                if (!native_costume || !melee_web_native_joint_stats(native_costume, &stats, error, sizeof(error)))
                    throw std::runtime_error(error);
                if (stats.joints != costume->graph().joint_count || !stats.dobjs || !stats.pobjs)
                    throw std::runtime_error("Native costume did not retain its complete joint/geometry graph");
                uint32_t expected_influences = 0;
                const auto& input = costume->graph();
                for (uint32_t j = 0; j < input.joint_count; ++j)
                    for (auto d = input.joints[j].dobj; d != UINT32_MAX; d = input.dobjs[d].next)
                        for (auto p = input.dobjs[d].pobj; p != UINT32_MAX; p = input.pobjs[p].next)
                            for (uint32_t e = 0; e < input.pobjs[p].envelope_count; ++e)
                                expected_influences += input.pobjs[p].envelopes[e].influence_count;
                if (stats.resolved_envelopes != expected_influences)
                    throw std::runtime_error("Native costume envelope resolution count differs from checked descriptors");
                if (material_animation) {
                    if (!melee_web_native_joint_add_material_animation(native_costume, material_animation->descriptor(), error, sizeof(error)))
                        throw std::runtime_error(error);
                    // Original source index channels select six blink images
                    // and palettes, then hold the last entry to frame10.
                    for (unsigned frame = 0; frame <= 10; ++frame) {
                        if (!melee_web_native_joint_request_animation(native_costume, float(frame), error, sizeof(error)) ||
                            !melee_web_native_joint_animate(native_costume, error, sizeof(error))) throw std::runtime_error(error);
                        auto* loaded = static_cast<HSD_JObj*>(melee_web_native_joint_object(native_costume,error,sizeof(error)));
                        if (!loaded || check_texture_indices(loaded, std::min(frame,5U)) != 2)
                            throw std::runtime_error("Mario material animation did not evaluate both original eye textures");
                    }
                    std::cout << "Mario original material animation: two eye textures, six images/palettes, frames0-10 passed\n";
                }
                std::cout << "costume native original loader: " << stats.joints << " joints, " << stats.dobjs
                          << " DObjs, " << stats.pobjs << " PObjs, " << stats.materials << " materials, "
                          << stats.textures << " textures, " << stats.resolved_envelopes << " resolved influences\n";
            }
            auto* common_context = melee_web_common_context_create(&common.scalars, &common.tables,
                &graph.graph(), error, sizeof(error));
            auto* root16 = melee_web_native_joint_hydrate(&root16_graph.graph(), error, sizeof(error));
            if (!root16) throw std::runtime_error(error);
            if (!common_context || !melee_web_common_context_set_root16(common_context,
                    melee_web_native_joint_descriptor(root16,error,sizeof(error)),error,sizeof(error)) ||
                !melee_web_common_context_attach(common_context,error,sizeof(error)) ||
                !melee_web_common_context_require(common_context,MELEE_WEB_COMMON_RUNTIME_MASK,error,sizeof(error)) ||
                !melee_web_common_context_initialize_materials(common_context,error,sizeof(error)))
                throw std::runtime_error(error);
            if (melee_web_common_context_require(common_context,(1u<<23)-1,error,sizeof(error)))
                throw std::runtime_error("Unsupported common roots were marked ready");
            std::cout << "Local common context: 18 copied roots, original8064/8F6C material owners passed\n";
            // Source world owns the real runtime. A surviving host descriptor
            // handle must reject access after shutdown and remain safe to free.
            if (!melee_web_gameplay_shutdown(error, sizeof(error))) throw std::runtime_error(error);
            if (!melee_web_common_context_destroy(common_context,error,sizeof(error))) throw std::runtime_error(error);
            if (melee_web_native_joint_stats(native, &stats, error, sizeof(error)))
                throw std::runtime_error("Stale native handle was accepted");
            if (!melee_web_native_joint_destroy(native, error, sizeof(error))) throw std::runtime_error(error);
            if (melee_web_native_joint_stats(root16, &stats, error, sizeof(error)))
                throw std::runtime_error("Stale common root16 handle was accepted");
            if (!melee_web_native_joint_destroy(root16, error, sizeof(error))) throw std::runtime_error(error);
            if (native_costume) {
                if (melee_web_native_joint_stats(native_costume, &stats, error, sizeof(error)))
                    throw std::runtime_error("Stale costume handle was accepted");
                if (!melee_web_native_joint_destroy(native_costume, error, sizeof(error))) throw std::runtime_error(error);
            }
        }
        std::cout << "Original common root20 initialization/destruction/restart passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
