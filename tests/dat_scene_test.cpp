#include "gameplay_compat.h"
#include "dat_scene.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <melee/sc/types.h>
#pragma GCC diagnostic pop

#include <fstream>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void be32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = std::uint8_t(value >> (24 - 8 * i));
}

Bytes empty_archive()
{
    Bytes result(0x20, 0);
    be32(result, 0, static_cast<std::uint32_t>(result.size()));
    return result;
}

Bytes malformed_scene_archive()
{
    constexpr std::size_t data_size = 0x20;
    constexpr std::string_view symbol = "MalformedScene";
    const auto public_offset = 0x20 + data_size;
    const auto names_offset = public_offset + 8;
    Bytes result(names_offset + symbol.size() + 1, 0);
    be32(result, 0, static_cast<std::uint32_t>(result.size()));
    be32(result, 4, static_cast<std::uint32_t>(data_size));
    be32(result, 8, 0); // The model pointer below deliberately lacks a relocation.
    be32(result, 12, 1);
    be32(result, public_offset, 0);
    be32(result, public_offset + 4, 0);
    std::copy(symbol.begin(), symbol.end(), result.begin() + names_offset);
    be32(result, 0x20, 16);
    return result;
}

Bytes read_file(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "scene asset is unavailable");
    return {std::istreambuf_iterator<char>(stream), {}};
}

template <class F>
void rejects(F&& function, const char* message)
{
    bool rejected = false;
    try {
        function();
    } catch (const melee_web::DatError&) {
        rejected = true;
    }
    check(rejected, message);
}

void test_missing_symbol()
{
    auto archive = std::make_shared<melee_web::DatArchive>(empty_archive());
    rejects([&] {
        melee_web::DatScene scene(archive, "ScInfDmg_scene_data");
    }, "missing SceneDesc public symbol is rejected");
}

void test_invalid_root_kind()
{
    auto archive = std::make_shared<melee_web::DatArchive>(empty_archive());
    rejects([&] {
        melee_web::DatScene scene(
            archive, "MissingScene",
            static_cast<melee_web::DatSceneRootKind>(0xff));
    }, "invalid Scene root kind is rejected");
}

void test_malformed_root_pointer()
{
    auto archive = std::make_shared<melee_web::DatArchive>(malformed_scene_archive());
    rejects([&] {
        melee_web::DatScene scene(archive, "MalformedScene");
    }, "nonrelocated SceneDesc model pointer is rejected");
}

template <typename T>
bool check_animation_list(T** list, const char* message)
{
    if (!list) return false;
    for (std::size_t index = 0; index <= 1024; ++index) {
        if (!list[index]) return index != 0;
    }
    check(false, message);
    return false;
}

void check_models(DynamicModelDesc** table, std::size_t count,
                  const char* message, bool& saw_joint_animation,
                  bool& saw_material_animation)
{
    check(table && count > 0, message);
    for (std::size_t index = 0; index < count; ++index) {
        auto* model = table[index];
        check(model && model->joint, message);
        saw_joint_animation |= check_animation_list(
            model->anims, "DynamicModelDesc joint animation list is unterminated");
        saw_material_animation |= check_animation_list(
            model->matanims, "DynamicModelDesc material animation list is unterminated");
        check_animation_list(model->shapeanims,
                             "DynamicModelDesc shape animation list is unterminated");
    }
}

void test_local_once(const char* ifall_path, const char* ifcoget_path)
{
    auto ifall = std::make_shared<melee_web::DatArchive>(read_file(ifall_path));
    melee_web::DatScene hud(ifall, "ScInfDmg_scene_data");
    check(hud.symbol_name() == "ScInfDmg_scene_data", "IfAll symbol is exact");
    check(hud.model_count() == 1 && hud.camera_count() == 1 &&
              hud.light_list_count() == 2 && hud.fog_count() == 0,
          "IfAll SceneDesc tables have exact source counts");
    auto* hud_desc = hud.descriptor();
    bool saw_joint_animation = false;
    bool saw_material_animation = false;
    check(hud.model_table() == nullptr && hud_desc && hud_desc->models,
          "IfAll SceneDesc publishes its model table");
    check_models(hud_desc->models, hud.model_count(),
                 "IfAll SceneDesc publishes typed model roots",
                 saw_joint_animation, saw_material_animation);
    check(hud_desc->cameras && hud_desc->cameras[0].desc &&
              hud_desc->lights &&
              hud_desc->lights[0] && hud_desc->lights[0]->desc,
          "IfAll SceneDesc publishes typed model, camera, and light roots");
    check(hud_desc->cameras[0].anims && hud_desc->cameras[0].anims[0],
          "IfAll zero-channel camera animation remains published");
    check(hud_desc->lights[0]->anims && hud_desc->lights[0]->anims[0],
          "IfAll zero-channel light animation remains published");

    constexpr struct {
        std::string_view name;
        std::size_t count;
    } model_roots[] = {
        {"ScInfCnt_scene_models", 8}, {"ScInfStc_scene_models", 1},
        {"ScInfTim_scene_models", 1}, {"DmgNum_scene_models", 1},
        {"DmgMrk_scene_models", 1},   {"Stc_scemdls", 2},
        {"Stc_rarwmdls", 1},          {"ScInfPnm_scene_models", 1},
        {"lupe", 1},                   {"tdsce", 1},
    };
    for (const auto& root : model_roots) {
        melee_web::DatScene models(
            ifall, root.name, melee_web::DatSceneRootKind::DynamicModelTable);
        check(models.descriptor() == nullptr && models.model_table(),
              "IfAll DynamicModelDesc table root is typed and published");
        check(models.model_count() == root.count,
              "IfAll DynamicModelDesc table preserves source slot count");
        check_models(models.model_table(), models.model_count(),
                     "IfAll DynamicModelDesc table contains every source model",
                     saw_joint_animation, saw_material_animation);
    }
    check(saw_joint_animation && saw_material_animation,
          "IfAll model tables hydrate original joint and material animations");

    auto ifcoget = std::make_shared<melee_web::DatArchive>(read_file(ifcoget_path));
    melee_web::DatScene coin_get(ifcoget, "ScInfCgt_scene_data");
    check(coin_get.model_count() == 1 && coin_get.camera_count() == 1 &&
              coin_get.light_list_count() == 2 && coin_get.fog_count() == 0,
          "IfCoGet SceneDesc tables have exact source counts");
    auto* coin_desc = coin_get.descriptor();
    check(coin_desc && coin_desc->models && coin_desc->cameras &&
              coin_desc->cameras[0].desc && coin_desc->lights &&
              coin_desc->lights[0] && coin_desc->lights[0]->desc,
          "IfCoGet SceneDesc publishes typed model, camera, and light roots");
    check_models(coin_desc->models, coin_get.model_count(),
                 "IfCoGet SceneDesc model root is hydrated",
                 saw_joint_animation, saw_material_animation);
}

void test_local(const char* ifall_path, const char* ifcoget_path)
{
    // Each pass destroys every DatScene owner before opening fresh archives.
    // This exercises native HSD descriptor teardown and rehydration across two
    // sequential SDK-world lifetimes without retaining borrowed asset storage.
    test_local_once(ifall_path, ifcoget_path);
    test_local_once(ifall_path, ifcoget_path);
}

void test_stage_once(const char* stage_path)
{
    auto archive = std::make_shared<melee_web::DatArchive>(
        read_file(stage_path), melee_web::DatExternalPolicy::ResolveNull);
    melee_web::DatScene quake(
        archive, "quake_model_set", melee_web::DatSceneRootKind::DynamicModel);
    check(quake.descriptor() == nullptr && quake.model_table() == nullptr &&
              quake.single_model() && quake.model_count() == 1,
          "stage quake_model_set publishes one typed DynamicModelDesc root");
    auto* model = quake.single_model();
    check(model->joint && model->anims,
          "stage quake_model_set publishes a joint and animation table");
    for (unsigned index = 0; index < 4; ++index)
        check(model->anims[index],
              "stage quake_model_set has four grLib_801C9CEC animations");
    check(model->anims[4] == nullptr,
          "stage quake_model_set animation table is terminated after four entries");
}

void test_stage(const char* stage_path)
{
    // Rehydrate each stage root twice so native animation/joint ownership is
    // released before the next source archive lifetime begins.
    test_stage_once(stage_path);
    test_stage_once(stage_path);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const char* ifall_path = nullptr;
        const char* ifcoget_path = nullptr;
        std::vector<const char*> stage_paths;
        for (int index = 1; index < argc;) {
            const std::string_view option = argv[index++];
            if (option == "--assets") {
                check(!ifall_path && index + 1 < argc,
                      "usage: [--assets IfAll.usd IfCoGet.dat] [--stage GrNLa.dat ...]");
                ifall_path = argv[index++];
                ifcoget_path = argv[index++];
            } else if (option == "--stage") {
                check(index < argc,
                      "usage: [--assets IfAll.usd IfCoGet.dat] [--stage GrNLa.dat ...]");
                stage_paths.push_back(argv[index++]);
            } else {
                check(false,
                      "usage: [--assets IfAll.usd IfCoGet.dat] [--stage GrNLa.dat ...]");
            }
        }
        check((ifall_path == nullptr) == (ifcoget_path == nullptr),
              "--assets requires both IfAll.usd and IfCoGet.dat");
        test_missing_symbol();
        test_invalid_root_kind();
        test_malformed_root_pointer();
        if (ifall_path) test_local(ifall_path, ifcoget_path);
        for (const auto* stage_path : stage_paths) test_stage(stage_path);
        std::cout << (ifall_path || !stage_paths.empty() ?
                          "Typed SceneDesc and DynamicModel hydration passed\n" :
                          "SceneDesc missing-symbol rejection passed\n");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
