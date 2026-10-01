#include "dat_native_animation.hpp"
#include "dat_native_joint.hpp"
#include "gameplay_compat.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <sysdolphin/baselib/aobj.h>
#pragma GCC diagnostic pop
#include <array>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace melee_web;

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static std::shared_ptr<DatArchive> open_archive(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    check(bool(file), "Kirby source archive is unavailable");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return std::make_shared<DatArchive>(bytes, DatExternalPolicy::ResolveNull);
}

static uint32_t symbol(const DatArchive& archive, const char* name) {
    for (const auto& entry : archive.public_symbols())
        if (entry.name == name) return entry.data_offset;
    throw DatError(std::string("Kirby source symbol missing: ") + name);
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "assets-local directory is required");
        const std::string directory = argv[1];
        auto fighter = open_archive(directory + "/PlKb.dat");
        const auto fighter_root = symbol(*fighter, "ftDataKirby");
        const auto part_table = fighter->pointer(fighter_root + 0x1c, 4);
        check(bool(part_table), "Kirby source part-animation table missing");
        const auto group_table_end = fighter->next_target_offset(*part_table);
        check(group_table_end > *part_table && (group_table_end - *part_table) % 4 == 0,
              "Kirby source part-animation table extent invalid");
        check((group_table_end - *part_table) / 4 == 3,
              "Kirby source part-animation group count changed");

        constexpr std::array<const char*, 6> files = {
            "PlKbNr.dat", "PlKbYe.dat", "PlKbBu.dat",
            "PlKbRe.dat", "PlKbGr.dat", "PlKbWh.dat"};
        constexpr std::array<const char*, 6> symbols = {
            "PlyKirby5K_Share_joint", "PlyKirby5KYe_Share_joint",
            "PlyKirby5KBu_Share_joint", "PlyKirby5KRe_Share_joint",
            "PlyKirby5KGr_Share_joint", "PlyKirby5KWh_Share_joint"};
        constexpr std::array<uint16_t, 3> part_counts = {1, 1, 14};
        unsigned animation_count = 0;
        for (size_t costume = 0; costume < files.size(); ++costume) {
            auto model_archive = open_archive(directory + "/" + files[costume]);
            DatNativeJoint model(model_archive, symbol(*model_archive, symbols[costume]));
            for (uint32_t group_index = 0; group_index < part_counts.size(); ++group_index) {
                const auto descriptor = fighter->pointer(*part_table + group_index * 4, 12);
                check(bool(descriptor), "Kirby source part-animation group missing");
                const auto part_count = fighter->be16(*descriptor + 2);
                check(part_count == part_counts[group_index],
                      "Kirby source part-animation list extent changed");
                const auto parts = fighter->pointer(*descriptor + 4, part_count);
                const auto roots = fighter->pointer(*descriptor + 8, 4);
                check(bool(parts) && bool(roots), "Kirby source part-animation payload missing");
                const auto root_table_end = fighter->next_target_offset(*roots);
                check(root_table_end > *roots && (root_table_end - *roots) % 4 == 0,
                      "Kirby source part-animation root extent invalid");
                const auto variant_count = (root_table_end - *roots) / 4;
                check(variant_count == 3, "Kirby source part-animation variant count changed");
                for (uint32_t variant = 0; variant < variant_count; ++variant) {
                    const auto root = fighter->pointer(*roots + variant * 4, 20);
                    if (!root) continue;
                    DatNativeAnimation animation(fighter, *root, model.graph(),
                        DatNativeAnimationPolicy::Transforms, {},
                        DatNativeAnimationTopology::FighterParts);
                    auto* descriptor_root = static_cast<HSD_AnimJoint*>(animation.descriptor());
                    check(descriptor_root != nullptr,
                          "Kirby source part-animation descriptor was not retained");
                    std::set<HSD_AnimJoint*> seen;
                    std::function<void(HSD_AnimJoint*)> walk = [&](HSD_AnimJoint* node) {
                        if (!node) return;
                        check(seen.insert(node).second,
                              "Kirby source part-animation descriptor topology is cyclic");
                        walk(node->child);
                        walk(node->next);
                    };
                    walk(descriptor_root);
                    check(seen.size() > 0 && seen.size() <= 140,
                          "Kirby source part-animation exceeds Fighter.parts capacity");
                    bool indexed_rejected = false;
                    try {
                        (void) animation.indexed_descriptor();
                    } catch (const DatError&) {
                        indexed_rejected = true;
                    }
                    check(indexed_rejected,
                          "Kirby part animation must retain tree, not model-array, topology");
                    ++animation_count;
                }
            }
        }
        check(animation_count == 42,
              "Kirby six-costume part-animation inventory changed");
        std::cout << "Kirby source part-animation trees preserved across six costume graphs: "
                  << animation_count << " descriptors passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
