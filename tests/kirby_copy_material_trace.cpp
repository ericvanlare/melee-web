#include "dat_archive.hpp"
#include "dat_fighter_runtime.hpp"
#include "dat_material_animation.hpp"
#include "dat_native_joint.hpp"
#include "fighter_binding.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::uint32_t symbol_offset(const melee_web::DatArchive& archive,
                            const std::string& name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw std::runtime_error("missing source symbol " + name);
}
}

int main(int argc, char** argv)
{
    if (argc != 5) {
        std::cerr << "usage: kirby_copy_material_trace PlKbCpFc.dat PlKbNrCpFc.dat PlKb.dat PlKbAJ.dat\n";
        return 2;
    }
    try {
        auto copy_archive = std::make_shared<const melee_web::DatArchive>(
            read_file(argv[1]), melee_web::DatExternalPolicy::PreserveUnresolved);
        const auto copy_root = symbol_offset(*copy_archive,
                                             "ftDataKirbyCopyFalco");
        for (const auto field : {0U, 4U, 8U, 12U, 16U, 20U}) {
            const auto slot = copy_root + field;
            std::cout << "Falco copy root DAT+0x" << std::hex << copy_root
                      << " field+0x" << field << " word=0x"
                      << copy_archive->be32(slot) << " relocation="
                      << copy_archive->has_relocation(slot);
            for (const auto& external : copy_archive->external_symbols())
                if (std::find(external.slots.begin(), external.slots.end(), slot) !=
                    external.slots.end())
                    std::cout << " external=" << external.name;
            std::cout << std::dec << '\n';
        }
        // KirbyHatStruct::hat_dynamics[2] is at +0x14 in the decomp. The
        // original ftKb_SpecialN_800EF438 passes this exact source root to
        // HSD_JObjLoad/POBJ reference resolution.
        const auto copy_model_root = copy_archive->pointer(copy_root + 0x14U, 64);
        if (!copy_model_root)
            throw std::runtime_error(
                "Falco copy hat_dynamics[2] has no checked joint root");
        melee_web::DatNativeJoint copy_model(copy_archive, *copy_model_root);
        std::uint32_t external_skin_references = 0;
        std::vector<std::string> external_skin_symbols;
        for (std::uint32_t i = 0; i < copy_model.graph().pobj_count; ++i) {
            const auto& pobj = copy_model.graph().pobjs[i];
            const auto slot = pobj.source_offset + 20U;
            std::cout << "Falco copy PObj DAT+0x" << std::hex
                      << pobj.source_offset << " flags=0x"
                      << pobj.geometry.flags << " shared="
                      << static_cast<unsigned>(pobj.has_shared_joint)
                      << " pointer_relocation="
                      << copy_archive->has_relocation(slot) << std::dec;
            for (const auto& external : copy_archive->external_symbols()) {
                if (std::find(external.slots.begin(), external.slots.end(), slot) ==
                    external.slots.end())
                    continue;
                ++external_skin_references;
                external_skin_symbols.push_back(external.name);
                std::cout << " external=" << external.name;
            }
            if (copy_archive->has_relocation(slot) &&
                std::none_of(copy_archive->external_symbols().begin(),
                             copy_archive->external_symbols().end(),
                    [slot](const melee_web::DatExternalSymbol& external) {
                        return std::find(external.slots.begin(),
                                         external.slots.end(), slot) !=
                               external.slots.end();
                    }))
                std::cout << " local_target=0x" << std::hex
                          << *copy_archive->pointer(slot, 64U) << std::dec;
            std::cout << '\n';
        }

        auto archive = std::make_shared<const melee_web::DatArchive>(
            read_file(argv[2]));
        melee_web::DatNativeJoint model(
            archive, symbol_offset(*archive, "PlyKirbyFc_Share_joint"));
        melee_web::DatMaterialAnimation animation(
            archive,
            symbol_offset(*archive, "PlyKirbyFc_Share_matanim_joint"),
            model.graph(),
            melee_web::TextureIndexValidation::StaticSourceFrameZero);
        std::cout << "Kirby Falco-copy source hat graph root=DAT+0x"
                  << std::hex << *copy_model_root << std::dec
                  << " joints=" << copy_model.graph().joint_count
                  << " PObjs=" << copy_model.graph().pobj_count
                  << " unresolved_external_skin_references="
                  << external_skin_references;
        for (const auto& symbol : external_skin_symbols)
            std::cout << " symbol=" << symbol;
        std::cout << ": passed\n";
        std::cout << "Kirby Falco-copy costume material graph joints="
                  << model.graph().joint_count << " textures="
                  << animation.image_count() << ": passed\n";

        const auto& identity = melee_web::resolve_fighter_costume(
            "PlyKirby5K_Share_joint");
        if (identity.fighter_kind != 4 || identity.motion_count <= 42)
            throw std::runtime_error(
                "Kirby source action table identity is incomplete: kind=" +
                std::to_string(identity.fighter_kind) + " motions=" +
                std::to_string(identity.motion_count));
        auto fighter_archive = std::make_shared<const melee_web::DatArchive>(
            read_file(argv[3]),
            melee_web::DatExternalPolicy::PreserveUnresolved);
        auto runtime = std::make_shared<const melee_web::DatFighterRuntime>(
            fighter_archive, identity);
        const auto animation_bytes = read_file(argv[4]);
        melee_web::DatFighterAnimationStore actions(runtime, animation_bytes);
        for (const auto motion : {23U, 42U, 455U}) {
            const auto selected = actions.select_native_action(motion);
            if (!selected.animation)
                throw std::runtime_error("Kirby copy action has no source animation: " +
                                         std::to_string(motion));
            const auto& row = selected.action;
            std::cout << "Kirby copy motion " << motion << " flags=0x"
                      << std::hex << row.motion_flags << " tree_kind=0x"
                      << (row.motion_flags & 0x3fU) << std::dec
                      << " node_count=" << selected.animation->node_counts.size()
                      << " tree_type=" << selected.animation->tree_type << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
