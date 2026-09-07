#include "rigid_model.hpp"

#include <fstream>
#include <iostream>
#include <memory>
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

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "Usage: asset_check <local DAT>\n";
        return 2;
    }
    try {
        const auto archive = load(argv[1]);
        if (archive->public_symbols().empty())
            throw melee_web::DatError("DAT has no public roots to check");
        bool rejected = false;
        for (const auto& root : archive->public_symbols()) {
            std::cout << "{\"scope\":\"rigid_model\",\"root\":";
            json_string(root.name);
            std::cout << ",\"offset\":" << root.data_offset;
            try {
                const melee_web::RigidModel model(archive, root.name);
                std::set<std::uint32_t> materials, textures;
                for (const auto& mesh : model.meshes) {
                    materials.insert(mesh.material->descriptor_offset);
                    for (const auto& texture : mesh.material->textures)
                        textures.insert(texture.descriptor_offset);
                }
                std::cout << ",\"status\":\"accepted\",\"joints\":" << model.joints.size()
                          << ",\"meshes\":" << model.meshes.size()
                          << ",\"materials\":" << materials.size()
                          << ",\"textures\":" << textures.size()
                          << ",\"packets\":" << model.draw_packets
                          << ",\"submitted_vertices\":" << model.submitted_vertices;
            } catch (const std::exception& error) {
                rejected = true;
                std::cout << ",\"status\":\"rejected\",\"reason\":";
                json_string(error.what());
            }
            std::cout << "}\n";
        }
        return rejected ? 1 : 0;
    } catch (const std::exception& error) {
        std::cout << "{\"scope\":\"archive\",\"status\":\"rejected\",\"reason\":";
        json_string(error.what());
        std::cout << "}\n";
        return 1;
    }
}
