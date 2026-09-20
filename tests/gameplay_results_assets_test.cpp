#include "dat_animation.hpp"
#include "dat_archive.hpp"
#include "fighter_binding.hpp"

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
    std::uint32_t motion_count;
    std::size_t result_clip_count;
};

constexpr Fixture fixtures[] = {
    {"Mario", 0,  "PlMr.dat", "GmRstMMr.dat", "ftDemoResultMotionFileMario",   16, 9},
    {"DrMario", 21, "PlDr.dat", "GmRstMDr.dat", "ftDemoResultMotionFileDrmario", 14, 9},
    {"Fox", 1,    "PlFx.dat", "GmRstMFx.dat", "ftDemoResultMotionFileFox",      14, 10},
    {"Falco", 22, "PlFc.dat", "GmRstMFc.dat", "ftDemoResultMotionFileFalco",    14, 9},
    {"Marth", 18, "PlMs.dat", "GmRstMMs.dat", "ftDemoResultMotionFileMars",     14, 9},
    {"Roy", 26,   "PlFe.dat", "GmRstMFe.dat", "ftDemoResultMotionFileEmblem",   14, 9},
    {"Link", 6,   "PlLk.dat", "GmRstMLk.dat", "ftDemoResultMotionFileLink",      14, 9},
    {"YoungLink", 20, "PlCl.dat", "GmRstMCl.dat", "ftDemoResultMotionFileClink", 14, 9},
};

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: gameplay_results_assets_test <results asset directory>\n";
        return 2;
    }
    try {
        const std::string root = argv[1];
        std::size_t total_clips = 0;
        for (const auto& fixture : fixtures) {
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
                root_symbol->data_offset != 0)
                throw std::runtime_error(std::string(fixture.name) +
                                         " result motion root is not the source public base");

            std::set<std::string> found;
            std::set<std::uint32_t> motion_ids;
            for (const auto& action : actions.actions) {
                if (!is_result_action(action.symbol)) continue;
                const auto bytes = result->data();
                if (action.container_offset > bytes.size() ||
                    action.archive_bytes > bytes.size() - action.container_offset)
                    throw std::runtime_error(std::string(fixture.name) +
                                             " result action range is outside GmRstM data");
                const auto nested = std::make_shared<const melee_web::DatArchive>(
                    bytes.subspan(action.container_offset, action.archive_bytes));
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
            if (found.size() != fixture.result_clip_count ||
                motion_ids.size() != fixture.result_clip_count)
                throw std::runtime_error(std::string(fixture.name) +
                                         " result archive did not construct every authored clip");
            total_clips += found.size();
            std::cout << fixture.name << " Results authored scene action archives validated: "
                      << found.size() << " clips\n";
        }
        std::cout << "Validated " << total_clips << " authored Results clips across "
                  << std::size(fixtures) << " source fighters\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
