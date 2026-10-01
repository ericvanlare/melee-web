#include "dat_animation.hpp"
#include "dat_archive.hpp"
#include "fighter_binding.hpp"
#include "gameplay_action_store.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
Bytes read_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("missing real Results asset: " + path);
    return {std::istreambuf_iterator<char>(stream), {}};
}

const melee_web::FighterCostume& source_identity(std::uint32_t fighter_kind)
{
    const auto costumes = melee_web::fighter_costumes();
    const auto found = std::find_if(costumes.begin(), costumes.end(),
                                    [&](const auto& value) {
                                        return value.fighter_kind == fighter_kind &&
                                               value.costume_index == 0;
                                    });
    if (found == costumes.end())
        throw std::runtime_error("source Results fighter identity is absent");
    return *found;
}

bool is_result_action(std::string_view symbol)
{
    return symbol.find("_ACTION_Win") != std::string_view::npos ||
           symbol.find("_ACTION_Selected") != std::string_view::npos ||
           symbol.find("_ACTION_Lose") != std::string_view::npos;
}

struct Fixture {
    const char* name;
    std::uint32_t fighter_kind;
    const char* fighter_file;
    const char* result_file;
    const char* result_root;
    std::uint32_t result_root_offset;
    std::uint32_t motion_count;
    std::size_t result_clip_count;
};

constexpr Fixture fixtures[] = {
    {"Mario", 0,  "PlMr.dat", "GmRstMMr.dat", "ftDemoResultMotionFileMario",    0, 16, 9},
    {"DrMario", 21, "PlDr.dat", "GmRstMDr.dat", "ftDemoResultMotionFileDrmario", 0, 14, 9},
    {"Fox", 1,    "PlFx.dat", "GmRstMFx.dat", "ftDemoResultMotionFileFox",       0, 14, 10},
    {"Falco", 22, "PlFc.dat", "GmRstMFc.dat", "ftDemoResultMotionFileFalco",     0, 14, 9},
    {"Marth", 18, "PlMs.dat", "GmRstMMs.dat", "ftDemoResultMotionFileMars",      0, 14, 9},
    {"Roy", 26,   "PlFe.dat", "GmRstMFe.dat", "ftDemoResultMotionFileEmblem",    0, 14, 9},
    {"Link", 6,   "PlLk.dat", "GmRstMLk.dat", "ftDemoResultMotionFileLink",       0, 14, 9},
    {"YoungLink", 20, "PlCl.dat", "GmRstMCl.dat", "ftDemoResultMotionFileClink", 0, 14, 9},
    {"Ness", 8, "PlNs.dat", "GmRstMNs.dat", "ftDemoResultMotionFileNess", 0, 14, 9},
    {"Peach", 9, "PlPe.dat", "GmRstMPe.dat", "ftDemoResultMotionFilePeach", 0, 14, 9},
    {"GameWatch", 24, "PlGw.dat", "GmRstMGw.dat", "ftDemoResultMotionFileGamewatch", 0, 14, 9},
    {"Kirby", 4, "PlKb.dat", "GmRstMKb.dat", "ftDemoResultMotionFileKirby", 0, 18, 9},
    {"Popo", 10, "PlPp.dat", "GmRstMPn.dat", "ftDemoResultMotionFilePopo", 0, 14, 9},
    {"Nana", 11, "PlNn.dat", "GmRstMPn.dat", "ftDemoResultMotionFileNana", 88800, 14, 9},
    {"Samus", 13, "PlSs.dat", "GmRstMSs.dat", "ftDemoResultMotionFileSamus", 0, 14, 9},
    {"Yoshi", 14, "PlYs.dat", "GmRstMYs.dat", "ftDemoResultMotionFileYoshi", 0, 14, 9},
    {"Zelda", 19, "PlZd.dat", "GmRstMZd.dat", "ftDemoResultMotionFileZelda", 0, 14, 9},
    {"Sheik", 7, "PlSk.dat", "GmRstMSk.dat", "ftDemoResultMotionFileSeak", 0, 14, 9},
};

} // namespace

int main(int argc, char** argv)
{
    if (argc == 5 && std::string_view(argv[1]) == "--root") {
        try {
            const auto archive = std::make_shared<const melee_web::DatArchive>(
                read_file(std::string(argv[2]) + "/" + argv[3]));
            const auto root = std::find_if(
                archive->public_symbols().begin(), archive->public_symbols().end(),
                [&](const auto& symbol) { return symbol.name == argv[4]; });
            if (root == archive->public_symbols().end())
                throw std::runtime_error(std::string(argv[3]) +
                                         " is missing public root " + argv[4]);
            std::cout << argv[3] << ' ' << argv[4] << " data_offset="
                      << root->data_offset << " data_bytes="
                      << archive->data().size() << '\n';
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: gameplay_results_assets_test <results asset directory> [fighter]\n";
        return 2;
    }
    try {
        const std::string root = argv[1];
        const std::string_view selected = argc == 3 ? argv[2] : "";
        std::size_t total_clips = 0;
        std::size_t checked_fighters = 0;
        for (const auto& fixture : fixtures) {
            if (!selected.empty() && selected != fixture.name) continue;
            const auto fighter = std::make_shared<const melee_web::DatArchive>(
                read_file(root + "/" + fixture.fighter_file),
                melee_web::DatExternalPolicy::ResolveNull);
            const auto result = std::make_shared<const melee_web::DatArchive>(
                read_file(root + "/" + fixture.result_file));
            const auto& identity = source_identity(fixture.fighter_kind);
            const melee_web::DatFighterActions actions(
                *fighter, identity, 0x14, fixture.motion_count);
            if (melee_web::fighter_demo_motion_count(fixture.fighter_kind) !=
                fixture.motion_count)
                throw std::runtime_error(std::string(fixture.name) +
                                         " demo table count diverges from ftData_UnkIntPairs");
            const auto root_symbol = std::find_if(
                result->public_symbols().begin(), result->public_symbols().end(),
                [&](const auto& symbol) { return symbol.name == fixture.result_root; });
            if (root_symbol == result->public_symbols().end() ||
                root_symbol->data_offset != fixture.result_root_offset)
                throw std::runtime_error(std::string(fixture.name) +
                    " result motion root offset differs from its source-owned identity");

            std::set<std::string> found;
            std::set<std::uint32_t> motion_ids;
            for (const auto& action : actions.actions) {
                if (!is_result_action(action.symbol)) continue;
                const auto bytes = result->data();
                if (fixture.result_root_offset > bytes.size() ||
                    action.container_offset > bytes.size() - fixture.result_root_offset)
                    throw std::runtime_error(std::string(fixture.name) +
                        " result action offset exceeds its authored public root");
                const auto nested_offset = std::size_t{fixture.result_root_offset} +
                                           action.container_offset;
                if (action.archive_bytes > bytes.size() - nested_offset)
                    throw std::runtime_error(std::string(fixture.name) +
                                             " result action range is outside GmRstM data");
                const auto nested = std::make_shared<const melee_web::DatArchive>(
                    bytes.subspan(nested_offset, action.archive_bytes));
                const auto clip = std::find_if(
                    nested->public_symbols().begin(), nested->public_symbols().end(),
                    [&](const auto& symbol) { return symbol.name == action.symbol; });
                if (clip == nested->public_symbols().end())
                    throw std::runtime_error(std::string(fixture.name) +
                                             " result archive omitted an authored clip symbol");
                const melee_web::DatAnimation animation(
                    *nested, clip->data_offset,
                    melee_web::DatAnimationPolicy::NativeFighterAction);
                if (animation.node_counts.empty() || animation.tracks.empty() ||
                    animation.end_frame <= 0)
                    throw std::runtime_error(std::string(fixture.name) +
                                             " result FigaTree failed native hydration bounds");
                if (!found.insert(action.symbol).second ||
                    !motion_ids.insert(action.motion_id).second)
                    throw std::runtime_error(std::string(fixture.name) +
                                             " result action aliases a duplicate authored row");
            }
            if (found.empty() || motion_ids.size() != found.size() ||
                (fixture.result_clip_count != 0 &&
                 found.size() != fixture.result_clip_count))
                throw std::runtime_error(std::string(fixture.name) +
                                         " result archive did not construct every authored clip");
            if (std::string_view(fixture.name) == "Nana") {
                const auto animation = read_file(root + "/PlNnAJ.dat");
                const auto popo = std::make_shared<const melee_web::DatArchive>(
                    read_file(root + "/PlPp.dat"),
                    melee_web::DatExternalPolicy::ResolveNull);
                const auto popo_animation = read_file(root + "/PlPpAJ.dat");
                const auto& popo_identity = source_identity(10);
                melee_web::GameplayActionStore action_store(
                    fighter, identity, animation, result, popo,
                    &popo_identity, popo_animation);
                if (!action_store.demo_action_rows() ||
                    !action_store.demo_blend_rows())
                    throw std::runtime_error(
                        "Nana's nonzero-root result demo action rows were not retained");
                std::cout << "Nana demo action store used the authored nonzero Results root\n";
            }
            total_clips += found.size();
            ++checked_fighters;
            std::cout << fixture.name << " Results authored scene action archives validated: "
                      << found.size() << " clips; root offset "
                      << root_symbol->data_offset << '\n';
        }
        if (checked_fighters == 0)
            throw std::runtime_error("selected source fighter is not in the Results fixture");
        std::cout << "Validated " << total_clips << " authored Results clips across "
                  << checked_fighters << " source fighters\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
