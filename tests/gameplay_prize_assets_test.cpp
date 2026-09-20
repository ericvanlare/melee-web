#include "dat_archive.hpp"
#include "gameplay_bootstrap.h"
#include "gameplay_prize_assets.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <memory>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes read_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("missing owned Prize asset: " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}

void require_symbol(const melee_web::DatArchive& archive,
                    std::string_view name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return;
    throw std::runtime_error("missing authored Prize public symbol: " +
                             std::string(name));
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: gameplay_prize_assets_test <Prize asset directory>\n";
        return 2;
    }
    try {
        const std::string root = argv[1];
        const auto ifprize = std::make_shared<const melee_web::DatArchive>(
            read_file(root + "/IfPrize.usd"));
        const auto sdprize = std::make_shared<const melee_web::DatArchive>(
            read_file(root + "/SdPrize.usd"));
        require_symbol(*ifprize, "ScInfPrize_scene_data");
        require_symbol(*sdprize, "SIS_PrizeData");

        // DatSis is independent of the SDK world and gives this focused test
        // a real authored bytecode/font bound before the owner is integrated
        // into a Prize session.
        melee_web::DatSis text(sdprize, "SIS_PrizeData");
        if (!text.descriptor() || text.entry_count() < 3)
            throw std::runtime_error("authored Prize SIS root is empty");

        // The SceneDesc parser also checks every authored model/camera/light/
        // fog extent. Native world publication is intentionally left to the
        // Prize session target, which must call GameplayPrizeAssets after its
        // SDK heap starts and close it after the source scene exits.
        melee_web::DatScene scene(ifprize, "ScInfPrize_scene_data");
        if (!scene.descriptor() || scene.model_count() != 1 ||
            scene.camera_count() != 1 || scene.light_list_count() != 2 ||
            scene.fog_count() != 1 || text.entry_count() != 393)
            throw std::runtime_error("authored Prize SceneDesc is incomplete");

        // Exercise the owner and its heap archive publication separately from
        // the direct parser checks above.  The owner is deliberately closed
        // after the SDK world, matching the source lbArchive scene-heap
        // lifetime required by IfPrize and lbCardGame.
        char error[256]{};
        if (!melee_web_gameplay_startup(32U * 1024U * 1024U,
                                        error, sizeof(error)))
            throw std::runtime_error(error);
        auto owner = std::make_unique<melee_web::GameplayPrizeAssets>(
            melee_web::RuntimeFiles{
                {"IfPrize.usd", read_file(root + "/IfPrize.usd")},
                {"SdPrize.usd", read_file(root + "/SdPrize.usd")},
                {"TyDatai.usd", read_file(root + "/TyDatai.usd")},
                {"LbMcGame.usd", read_file(root + "/LbMcGame.usd")},
                {"NtMemAc.usd", read_file(root + "/NtMemAc.usd")},
            });
        if (owner->scene().model_count() != scene.model_count() ||
            owner->scene().camera_count() != scene.camera_count() ||
            owner->scene().light_list_count() != scene.light_list_count() ||
            owner->scene().fog_count() != scene.fog_count() ||
            owner->sis().entry_count() != text.entry_count() ||
            owner->trophy_data().init_model_table().size() != 294 ||
            owner->trophy_data().init_model_d_table().size() != 6 ||
            owner->trophy_data().model_sort_table().size() != 294 ||
            owner->trophy_data().display_model_table().size() != 295 ||
            owner->trophy_data().display_model_us_table().size() != 5 ||
            owner->card_icons().icon_count() != 4 ||
            owner->card_scene().model_count() != 1 ||
            owner->card_scene().camera_count() != 1)
            throw std::runtime_error("Prize owner changed authored root bounds");
        owner->verify();
        if (!melee_web_gameplay_shutdown(error, sizeof(error)))
            throw std::runtime_error(error);
        owner->close();
        owner.reset();

        std::cout << "Validated authored Prize IfPrize SceneDesc ("
                  << scene.model_count() << " models, "
                  << scene.camera_count() << " cameras, "
                  << scene.light_list_count() << " light lists, "
                  << scene.fog_count() << " fogs) and SIS_PrizeData ("
                  << text.entry_count() << " entries; typed Toy/card roots validated)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
