#include "rigid_model.hpp"
#include "dat_stage.hpp"

#include <charconv>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace {

void json_string(std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    std::cout << '"';
    for (const unsigned char byte : value) {
        if (byte == '"' || byte == '\\') std::cout << '\\' << char(byte);
        else if (byte < 32) std::cout << "\\u00" << hex[byte >> 4] << hex[byte & 15];
        else std::cout << char(byte);
    }
    std::cout << '"';
}

std::shared_ptr<const melee_web::DatArchive> load(const char* path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw melee_web::DatError("Cannot open local DAT file");
    const auto size = file.tellg();
    if (size < 0 || std::uintmax_t(size) > melee_web::DatArchive::max_archive_bytes)
        throw melee_web::DatError("DAT input exceeds the 64 MiB limit or its size is unavailable");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
        throw melee_web::DatError("Could not read the complete local DAT file");
    return std::make_shared<melee_web::DatArchive>(bytes);
}

void model_counts(const melee_web::RigidModel& model)
{
    std::set<std::uint32_t> materials, textures;
    size_t envelope_count = 0, influences = 0, inverse_binds = 0;
    for (const auto& joint : model.joints) inverse_binds += joint.inverse_bind.has_value();
    for (const auto& mesh : model.meshes) {
        envelope_count += mesh.envelopes.size();
        for (const auto& envelope : mesh.envelopes) influences += envelope.influences.size();
        materials.insert(mesh.material->descriptor_offset);
        for (const auto& texture : mesh.material->textures)
            textures.insert(texture.descriptor_offset);
    }
    std::cout << ",\"status\":\"accepted\",\"joints\":" << model.joints.size()
              << ",\"meshes\":" << model.meshes.size()
              << ",\"materials\":" << materials.size()
              << ",\"textures\":" << textures.size()
              << ",\"inverse_binds\":" << inverse_binds
              << ",\"envelopes\":" << envelope_count
              << ",\"influences\":" << influences
              << ",\"packets\":" << model.draw_packets
              << ",\"submitted_vertices\":" << model.submitted_vertices
              << ",\"omitted_dobjs\":" << model.omitted_dobjs
              << ",\"omitted_joints\":" << model.omitted_joints
              << ",\"omitted_meshes\":" << model.omitted_meshes()
              << ",\"omitted_translucent_meshes\":" << model.omitted_translucent_meshes
              << ",\"omitted_texture_edge_meshes\":" << model.omitted_texture_edge_meshes;
}

void rejection(const std::exception& error)
{
    std::cout << ",\"status\":\"rejected\",\"reason\":";
    json_string(error.what());
}

struct Options {
    const char* path = nullptr;
    std::optional<std::string> symbol;
    std::optional<std::uint32_t> stage_entry;
    bool opaque = false;
};

Options options(int argc, char** argv)
{
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--opaque") {
            if (result.opaque) throw std::runtime_error("Duplicate --opaque");
            result.opaque = true;
        } else if (arg == "--symbol") {
            if (result.symbol || ++i == argc) throw std::runtime_error("--symbol requires one unique value");
            result.symbol = argv[i];
        } else if (arg == "--stage-entry") {
            if (result.stage_entry || ++i == argc) throw std::runtime_error("--stage-entry requires one unique index");
            const std::string_view value(argv[i]);
            std::uint32_t index;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), index);
            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
                throw std::runtime_error("--stage-entry must be an unsigned 32-bit index");
            result.stage_entry = index;
        } else if (arg.starts_with("--")) {
            throw std::runtime_error("Unknown argument: " + std::string(arg));
        } else if (!result.path) result.path = argv[i];
        else if (!result.symbol) result.symbol = argv[i]; // Preserve the original positional symbol CLI.
        else throw std::runtime_error("Unexpected positional argument");
    }
    if (!result.path) throw std::runtime_error("A local DAT path is required");
    if (result.opaque && !result.stage_entry) throw std::runtime_error("--opaque requires --stage-entry");
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    Options args;
    try { args = options(argc, argv); }
    catch (const std::exception& error) {
        std::cerr << error.what() << "\nUsage: asset_check <local DAT> [--symbol exact-root] "
                     "[--stage-entry N [--opaque]]\n";
        return 2;
    }
    try {
        const auto archive = load(args.path);
        if (args.stage_entry) {
            const auto symbol = args.symbol.value_or("map_head");
            std::cout << "{\"scope\":\"stage_model\",\"target_type\":\"stage_entry\",\"root\":";
            json_string(symbol);
            std::cout << ",\"source_entry\":" << *args.stage_entry << ",\"render_pass\":";
            json_string(args.opaque ? "opaque" : "all");
            bool rejected = false;
            try {
                const melee_web::DatStage stage(*archive, symbol);
                std::cout << ",\"offset\":" << stage.root_offset << ",\"stage_entries\":" << stage.entries.size();
                if (*args.stage_entry >= stage.entries.size()) throw melee_web::DatError("Stage entry index is out of range");
                const auto& entry = stage.entries[*args.stage_entry];
                std::cout << ",\"entry_offset\":" << entry.descriptor_offset << ",\"services_unapplied\":[";
                bool first = true;
                for (const auto& service : stage.unapplied_services(*args.stage_entry)) {
                    if (!first) std::cout << ',';
                    json_string(service); first = false;
                }
                std::cout << ']';
                if (!entry.joint_offset) throw melee_web::DatError("Stage entry has no joint graph");
                std::cout << ",\"joint_offset\":" << *entry.joint_offset;
                const melee_web::RigidModel model(archive, *entry.joint_offset,
                    symbol + " entry " + std::to_string(entry.index),
                    args.opaque ? melee_web::ModelRenderPass::Opaque : melee_web::ModelRenderPass::All);
                model_counts(model);
            } catch (const std::exception& error) { rejection(error); rejected = true; }
            std::cout << "}\n";
            return rejected ? 1 : 0;
        }
        if (archive->public_symbols().empty())
            throw melee_web::DatError("DAT has no public roots to check");
        bool rejected = false;
        bool found = false;
        for (const auto& root : archive->public_symbols()) {
            if (args.symbol && root.name != *args.symbol) continue;
            found = true;
            std::cout << "{\"scope\":\"rigid_model\",\"target_type\":\"joint_root\",\"render_pass\":\"all\",\"root\":";
            json_string(root.name);
            std::cout << ",\"offset\":" << root.data_offset;
            try { model_counts(melee_web::RigidModel(archive, root.name)); }
            catch (const std::exception& error) { rejected = true; rejection(error); }
            std::cout << "}\n";
        }
        if (!found) throw melee_web::DatError("Requested public model symbol is missing");
        return rejected ? 1 : 0;
    } catch (const std::exception& error) {
        std::cout << "{\"scope\":\"archive\"";
        rejection(error);
        std::cout << "}\n";
        return 1;
    }
}
