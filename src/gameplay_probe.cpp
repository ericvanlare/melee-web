#include "dat_common.hpp"
#include "dat_collision.hpp"
#include "dat_stage.hpp"
#include "gameplay_abi.h"
#include "gameplay_bootstrap.h"
#include "gameplay_collision.h"
#include "gameplay_fighter_probe.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
melee_web::DatArchive read_archive(const std::string& filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Unable to open local DAT file");
    const auto length = file.tellg();
    if (length <= 0 || length > melee_web::DatArchive::max_archive_bytes)
        throw std::runtime_error("Local DAT file exceeds the supported size range");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size())))
        throw std::runtime_error("Unable to read complete local DAT file");
    return melee_web::DatArchive(bytes);
}
void require(int result, const char* error)
{
    if (!result) throw std::runtime_error(error);
}
struct World {
    World() { char error[256]; require(melee_web_gameplay_startup(1024 * 1024, error, sizeof(error)), error); }
    ~World() { char error[256]; if (!melee_web_gameplay_shutdown(error, sizeof(error))) std::terminate(); }
};
struct ProbeDeleter {
    void operator()(MeleeWebFighterInputProbe* probe) const
    {
        char error[256];
        if (!melee_web_fighter_input_probe_destroy(probe, error, sizeof(error))) std::terminate();
    }
};
struct CollisionDeleter {
    void operator()(MeleeWebCollision* collision) const
    {
        char error[256];
        if (!melee_web_collision_destroy(collision, error, sizeof(error))) std::terminate();
    }
};
using CollisionOwner = std::unique_ptr<MeleeWebCollision, CollisionDeleter>;
CollisionOwner load_collision(const melee_web::DatCollision& data, int stage_kind, float scale)
{
    // The C boundary copies these typed arrays into its owned SDK allocation.
    // No native pointers or bitfields are overlaid onto archive bytes.
    std::vector<MeleeWebCollisionVertex> vertices;
    std::vector<MeleeWebCollisionLine> lines;
    std::vector<MeleeWebCollisionJoint> joints;
    for (const auto& v : data.vertices) vertices.push_back({v.x, v.y});
    for (const auto& l : data.lines)
        lines.push_back({l.v0, l.v1, l.prev0, l.next0, l.prev1, l.next1, l.hi_flags, l.lo_flags});
    for (const auto& j : data.joints) {
        MeleeWebCollisionJoint value{};
        for (std::size_t k = 0; k < 5; ++k)
            value.ranges[k] = {j.line_ranges[k].start, j.line_ranges[k].count};
        value.left = j.left; value.right = j.right; value.bottom = j.bottom; value.top = j.top;
        value.vertices = {j.vertices.start, j.vertices.count};
        joints.push_back(value);
    }
    MeleeWebCollisionInput input{};
    input.vertices = vertices.data(); input.vertex_count = vertices.size();
    input.lines = lines.data(); input.line_count = lines.size();
    input.joints = joints.data(); input.joint_count = joints.size();
    input.stage_kind = stage_kind; input.stage_scale = scale;
    input.source_reserved_2c = data.source_reserved_2c;
    for (std::size_t k = 0; k < 5; ++k)
        input.ranges[k] = {data.line_ranges[k].start, data.line_ranges[k].count};
    char error[256];
    CollisionOwner owner(melee_web_collision_create(&input, error, sizeof(error)));
    if (!owner) throw std::runtime_error(error);
    return owner;
}
}

int main(int argc, char** argv)
{
    try {
        std::string common_path, stage_path;
        std::optional<int> stage_kind;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if ((option != "--common" && option != "--stage" && option != "--stage-kind") || i + 1 >= argc)
                throw std::runtime_error("Usage: gameplay_probe [--common PlCo.dat] [--stage stage.dat [--stage-kind GrKind]]");
            if (option == "--stage-kind") {
                if (stage_kind) throw std::runtime_error("Duplicate input option");
                const std::string value = argv[++i];
                int parsed;
                const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
                if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
                    throw std::runtime_error("Stage kind must be an original GrKind integer");
                stage_kind = parsed;
                continue;
            }
            auto& value = option == "--common" ? common_path : stage_path;
            if (!value.empty()) throw std::runtime_error("Duplicate input option");
            value = argv[++i];
        }
        if (stage_kind && stage_path.empty()) throw std::runtime_error("--stage-kind requires --stage");
        char error[256];
        require(melee_web_gameplay_check_fighter_flags(error, sizeof(error)), error);
        std::unique_ptr<melee_web::DatCommon> common;
        std::unique_ptr<melee_web::DatCollision> collision;
        std::size_t archive_bindings = 0;
        float stage_scale = 0;
        if (!common_path.empty()) common = std::make_unique<melee_web::DatCommon>(read_archive(common_path));
        if (!stage_path.empty()) {
            const auto archive = read_archive(stage_path);
            collision = std::make_unique<melee_web::DatCollision>(archive);
            const melee_web::DatStage stage(archive);
            archive_bindings = melee_web::read_dat_collision_bindings(archive, stage, *collision).size();
            if (stage_kind) stage_scale = melee_web::read_dat_stage_scale(archive);
        }
        World world;
        CollisionOwner collision_runtime;
        MeleeWebCollisionReadiness collision_readiness{};
        std::vector<MeleeWebCollisionLineResult> collision_lines;
        std::optional<MeleeWebCollisionFloorResult> floor_query;
        if (stage_kind) {
            collision_runtime = load_collision(*collision, *stage_kind, stage_scale);
            require(melee_web_collision_readiness(collision_runtime.get(), &collision_readiness,
                    error, sizeof(error)), error);
            for (std::size_t i = 0; i < collision->lines.size(); ++i) {
                MeleeWebCollisionLineResult result{};
                require(melee_web_collision_line(collision_runtime.get(), int(i), &result,
                        error, sizeof(error)), error);
                collision_lines.push_back(result);
            }
            if (collision->line_ranges[0].count > 0) {
                MeleeWebCollisionFloorResult result{};
                // A fixed diagnostic point; this is not an ECB/fighter update.
                require(melee_web_collision_floor(collision_runtime.get(), collision->line_ranges[0].start,
                        0.f, 20.f, &result, error, sizeof(error)), error);
                floor_query = result;
            }
        }
        std::vector<MeleeWebFighterInputResult> samples;
        std::unique_ptr<MeleeWebFighterInputProbe, ProbeDeleter> probe;
        if (common) {
            probe.reset(melee_web_fighter_input_probe_create(&common->scalars, error, sizeof(error)));
            if (!probe) throw std::runtime_error(error);
            // Representative normalized stick inputs exercise an original
            // predicate only. They do not advance movement or simulate frames.
            for (float facing : std::array{-1.f, 1.f}) {
                for (float stick : std::array{-1.f, 0.f, 1.f}) {
                    MeleeWebFighterInputResult result{};
                    require(melee_web_fighter_input_probe_sample(probe.get(), stick, facing,
                            &result, error, sizeof(error)), error);
                    samples.push_back(result);
                }
            }
        }
        require(melee_web_gameplay_step(error, sizeof(error)), error);
        const auto stats = melee_web_gameplay_stats();
        std::cout << "{\"kind\":\"gameplay-bootstrap-probe\",\"fighter_initialized\":false,"
                     "\"match_running\":false,\"full_common_roots_ready\":false,"
                     "\"native_command_overlay_compatible\":"
                  << (melee_web_gameplay_native_command_overlay_compatible() ? "true" : "false")
                  << ",\"scheduler_ticks\":" << stats.ticks << ",\"objects\":" << stats.objects
                  << ",\"processes\":" << stats.processes << ",\"heap_free_bytes\":" << stats.heap_free_bytes;
        std::cout << ",\"common_roots\":[";
        if (common) for (std::size_t i = 0; i < common->roots.size(); ++i) {
            const auto& root = common->roots[i];
            if (i) std::cout << ',';
            std::cout << "{\"index\":" << root.index << ",\"source\":\"" << root.source_global
                      << "\",\"present\":" << (root.data_offset ? "true" : "false")
                      << ",\"scalars_decoded\":"
                      << (root.readiness == melee_web::DatCommonReadiness::ScalarsDecoded ? "true" : "false") << '}';
        }
        std::cout << "],\"original_walk_predicate_samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i) {
            if (i) std::cout << ',';
            const auto& sample = samples[i];
            std::cout << "{\"stick\":" << sample.stick_x << ",\"facing\":" << sample.facing_direction
                      << ",\"threshold\":" << sample.walk_threshold
                      << ",\"can_walk\":" << (sample.can_walk ? "true" : "false") << '}';
        }
        std::cout << "],\"collision\":";
        if (collision) {
            std::cout << "{\"vertices\":" << collision->vertices.size() << ",\"lines\":" << collision->lines.size()
                      << ",\"joints\":" << collision->joints.size() << ",\"archive_bindings\":" << archive_bindings
                      << ",\"source_stage_bindings_applied\":false,\"physics_running\":false"
                      << ",\"original_indices_initialized\":"
                      << (collision_readiness.original_indices_initialized ? "true" : "false");
            if (collision_runtime) {
                std::cout << ",\"stage_kind\":" << *stage_kind << ",\"stage_scale\":" << stage_scale
                          << ",\"floor_islands\":" << collision_readiness.floor_islands
                          << ",\"ceiling_islands\":" << collision_readiness.ceiling_islands
                          << ",\"original_line_queries\":[";
                for (std::size_t i = 0; i < collision_lines.size(); ++i) {
                    const auto& line = collision_lines[i];
                    if (i) std::cout << ',';
                    std::cout << "{\"line\":" << i << ",\"kind\":" << line.kind
                              << ",\"v0\":[" << line.v0[0] << ',' << line.v0[1]
                              << "],\"v1\":[" << line.v1[0] << ',' << line.v1[1]
                              << "],\"normal\":[" << line.normal[0] << ',' << line.normal[1]
                              << "],\"material_flags\":" << line.material_flags << '}';
                }
                std::cout << ']';
                if (floor_query) {
                    const auto& floor = *floor_query;
                    std::cout << ",\"original_floor_query\":{\"x\":0,\"y\":20,\"line\":" << floor.line
                              << ",\"displacement_y\":" << floor.displacement_y
                              << ",\"normal\":[" << floor.normal[0] << ',' << floor.normal[1]
                              << "],\"material_flags\":" << floor.material_flags << '}';
                }
            }
            std::cout << '}';
        } else std::cout << "null";
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Gameplay bootstrap rejected: " << error.what() << '\n';
        return 1;
    }
}
