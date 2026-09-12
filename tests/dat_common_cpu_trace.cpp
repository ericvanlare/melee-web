#include "dat_common.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatCommon;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

/* This is the source Fighter_804D64FC layout from the pinned fighter.h. */
struct CpuRoot {
    std::uint8_t** cmdscripts;
    void** x4;
    void** x8;
    void** xC;
    void** x10;
    void** x14;
    void** x18;
    void** x1C;
    float* x20;
    void* x24;
};
static_assert(sizeof(CpuRoot) == 10 * sizeof(void*));

struct AttackEntry {
    std::int32_t cmd, x04;
    float x08, x0C, x10, x14, weight;
    std::int32_t x1C, x20;
};
static_assert(sizeof(AttackEntry) == 36);

Bytes read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open PlCo.dat");
    return Bytes(std::istreambuf_iterator<char>(input), {});
}

std::uint32_t be32(const Bytes& bytes, std::size_t offset)
{
    return (std::uint32_t(bytes.at(offset)) << 24) |
           (std::uint32_t(bytes.at(offset + 1)) << 16) |
           (std::uint32_t(bytes.at(offset + 2)) << 8) |
           std::uint32_t(bytes.at(offset + 3));
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte)
        bytes.at(offset + byte) = static_cast<std::uint8_t>(value >> (24 - 8 * byte));
}

Bytes add_relocation(Bytes bytes, std::uint32_t slot)
{
    const auto data_size = be32(bytes, 4);
    const auto relocation_count = be32(bytes, 8);
    const auto relocation_end = std::size_t{32} + data_size + relocation_count * 4;
    bytes.insert(bytes.begin() + relocation_end, 4, std::uint8_t{0});
    put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 8, relocation_count + 1);
    put32(bytes, relocation_end, slot);
    return bytes;
}

template<class F>
void rejects(F&& operation)
{
    try {
        operation();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("malformed root22 input was accepted");
}

void check_graph(DatCommon& common)
{
    check(common.roots[22].readiness == melee_web::DatCommonReadiness::CpuDataDecoded,
          "root22 readiness was not promoted after graph hydration");
    auto* handle = common.tables.cpu_data;
    check(handle && handle->root && handle->storage && handle->refs == 1,
          "root22 handle is not a complete owned graph");
    auto* root = static_cast<CpuRoot*>(handle->root);
    check(root->cmdscripts && root->cmdscripts[0] == nullptr && root->cmdscripts[1],
          "command script table does not preserve the null row and scripts");
    check(root->cmdscripts[1][0] == 0x92 && root->cmdscripts[1][8] == 0x7f,
          "first PlCo command script bytes drifted");
    check(root->cmdscripts[61][0] == 0x80 && root->cmdscripts[61][12] == 0x7f,
          "last PlCo command script bytes drifted");
    check(root->x4 && root->x4[0] && root->x4[1],
          "ground attack table did not preserve source rows");
    const auto* ground_mario = static_cast<const AttackEntry*>(root->x4[1]);
    check(ground_mario[0].cmd == 2 && ground_mario[0].x1C == 1 &&
              ground_mario[1].cmd == 3,
          "ground attack records were not copied in source order");
    check(root->x20 && root->x20[0] == 15.5f && root->x20[31] == 15.0f,
          "distance threshold table boundary drifted");
    const auto* reach = static_cast<const float*>(root->x24);
    check(reach && reach[0] == -1.6f && reach[5] == -0.7f,
          "weapon reach table boundary drifted");
}

void check_lifetime(const Bytes& bytes)
{
    DatArchive archive(bytes);
    MeleeWebCommonCpuData* retained = nullptr;
    {
        DatCommon common(archive);
        check_graph(common);
        retained = common.tables.cpu_data;
        check(melee_web_common_cpu_retain(retained) && retained->refs == 2,
              "explicit CPU graph retain failed");
        DatCommon copy(common);
        check(retained->refs == 3, "DatCommon copy did not retain CPU graph");
        DatCommon moved(std::move(copy));
        check(moved.tables.cpu_data == retained && retained->refs == 3,
              "DatCommon move lost CPU graph ownership");
        DatCommon assigned(archive);
        assigned = moved;
        check(assigned.tables.cpu_data == retained && retained->refs == 4,
              "DatCommon copy assignment did not retain CPU graph");
        DatCommon move_assigned(archive);
        move_assigned = std::move(assigned);
        check(move_assigned.tables.cpu_data == retained && retained->refs == 4,
              "DatCommon move assignment lost CPU graph ownership");
    }
    check(retained->refs == 1, "retained CPU graph was released with DatCommon");
    auto* root = static_cast<CpuRoot*>(retained->root);
    check(root->cmdscripts[1][0] == 0x92 && root->x20[0] == 15.5f,
          "retained CPU graph became invalid after DatCommon destruction");
    melee_web_common_cpu_release(retained);
}

void check_malformed(const Bytes& source, const char* kind)
{
    constexpr std::size_t data_base = 32;
    constexpr std::uint32_t first_script = 0xe10c;
    constexpr std::uint32_t command_table = 0xe5f8;
    if (std::string_view(kind) == "opcode") {
        auto bytes = source;
        bytes[data_base + first_script] = 0x96; // Gap after CpuCmd_LstickXTowardFighter.
        rejects([&] { DatCommon common{DatArchive(bytes)}; });
    } else if (std::string_view(kind) == "row0") {
        auto bytes = source;
        put32(bytes, data_base + command_table, first_script);
        bytes = add_relocation(std::move(bytes), command_table);
        rejects([&] { DatCommon common{DatArchive(bytes)}; });
    } else if (std::string_view(kind) == "sentinel") {
        auto bytes = source;
        bytes[data_base + first_script + 8] = 0x80;
        bytes[data_base + first_script + 9] = 0;
        bytes[data_base + first_script + 10] = 0x80;
        bytes[data_base + first_script + 11] = 0;
        rejects([&] { DatCommon common{DatArchive(bytes)}; });
    } else if (std::string_view(kind) == "divisor") {
        auto bytes = source;
        put32(bytes, data_base + 0x1c, 0);
        rejects([&] { DatCommon common{DatArchive(bytes)}; });
    } else if (std::string_view(kind) == "relocation") {
        auto bytes = source;
        put32(bytes, data_base + first_script, first_script);
        bytes = add_relocation(std::move(bytes), first_script);
        rejects([&] { DatCommon common{DatArchive(bytes)}; });
    } else {
        throw std::runtime_error("unknown malformed root22 case");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) return 2;
    try {
        const auto bytes = read_file(argv[1]);
        if (argc == 2) {
            check_lifetime(bytes);
            std::cout << "Local PlCo root22 command graph and ownership: passed\n";
        } else {
            check_malformed(bytes, argv[2]);
            std::cout << "Malformed PlCo root22 input rejected: " << argv[2] << "\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
