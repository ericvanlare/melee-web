#include "dat_native_joint.hpp"
#include "dat_common.hpp"
#include "gameplay_bootstrap.h"
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
int main(int argc, char** argv) {
    try {
        if (argc != 2 && argc != 4)
            throw std::runtime_error("Usage: hsd_native_dat_probe PlCo.dat [costume.dat exact_public_joint_symbol]");
        auto archive = read_archive(argv[1]);
        const melee_web::DatCommon common(*archive);
        if (!common.roots[20].data_offset) throw std::runtime_error("Common root20 is absent");
        melee_web::DatNativeJoint graph(archive, *common.roots[20].data_offset);
        std::unique_ptr<melee_web::DatNativeJoint> costume;
        if (argc == 4) {
            auto costume_archive = read_archive(argv[2]);
            for (const auto& symbol : costume_archive->public_symbols())
                if (symbol.name == argv[3])
                    costume = std::make_unique<melee_web::DatNativeJoint>(costume_archive, symbol.data_offset);
            if (!costume) throw std::runtime_error("Exact requested costume joint symbol is absent");
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
                std::cout << "costume native original loader: " << stats.joints << " joints, " << stats.dobjs
                          << " DObjs, " << stats.pobjs << " PObjs, " << stats.materials << " materials, "
                          << stats.textures << " textures, " << stats.resolved_envelopes << " resolved influences\n";
            }
            // Source world owns the real runtime. A surviving host descriptor
            // handle must reject access after shutdown and remain safe to free.
            if (!melee_web_gameplay_shutdown(error, sizeof(error))) throw std::runtime_error(error);
            if (melee_web_native_joint_stats(native, &stats, error, sizeof(error)))
                throw std::runtime_error("Stale native handle was accepted");
            if (!melee_web_native_joint_destroy(native, error, sizeof(error))) throw std::runtime_error(error);
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
