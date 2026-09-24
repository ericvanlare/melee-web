#include "dat_trophy_data.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatError;
using melee_web::DatTrophyData;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

template <typename F> void rejects(F&& operation, const char* message)
{
    try {
        operation();
    } catch (const DatError&) {
        return;
    }
    throw std::runtime_error(message);
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes.at(offset) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    bytes.at(offset) = static_cast<std::uint8_t>(value >> 24U);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 16U);
    bytes.at(offset + 2) = static_cast<std::uint8_t>(value >> 8U);
    bytes.at(offset + 3) = static_cast<std::uint8_t>(value);
}

Bytes synthetic(bool bad_sort_terminator = false)
{
    constexpr std::size_t trophy_entry = 0x24;
    constexpr std::size_t name_entry = 0x0c;
    constexpr std::size_t display_entry = 0x10;
    const auto init = std::size_t{0};
    const auto init_d = init + trophy_entry;
    const auto no_get = init_d + trophy_entry;
    const auto exp = no_get + 4;
    const auto sort = exp + 12;
    const auto display = sort + 294 * name_entry;
    const auto display_us = display + display_entry;
    const auto data_size = display_us + display_entry;
    Bytes data(data_size, 0);

    put32(data, init, 0xffffffffU);
    put32(data, init_d, 0xffffffffU);
    for (std::size_t i = 0; i < 293; ++i)
        put16(data, sort + i * name_entry, static_cast<std::uint16_t>(i));
    put16(data, sort + 293 * name_entry,
          bad_sort_terminator ? 0 : 0xffffU);
    put16(data, exp + 10, 0xffffU);
    put16(data, no_get, 0);
    put16(data, no_get + 2, 0xffffU);
    put32(data, display, 0xffffffffU);
    put32(data, display_us, 0xffffffffU);

    const std::vector<std::string> names = {
        "tyInitModelTbl", "tyInitModelDTbl", "tyModelSortTbl",
        "tyExpDifferentTbl", "tyNoGetUsTbl", "tyDisplayModelTbl",
        "tyDisplayModelUsTbl",
    };
    const std::vector<std::uint32_t> roots = {
        static_cast<std::uint32_t>(init), static_cast<std::uint32_t>(init_d),
        static_cast<std::uint32_t>(sort), static_cast<std::uint32_t>(exp),
        static_cast<std::uint32_t>(no_get), static_cast<std::uint32_t>(display),
        static_cast<std::uint32_t>(display_us),
    };
    std::size_t names_bytes = 0;
    for (const auto& name : names) names_bytes += name.size() + 1;
    const auto names_start = std::size_t{0x20} + data.size() + names.size() * 8;
    Bytes archive(names_start + names_bytes, 0);
    put32(archive, 0, static_cast<std::uint32_t>(archive.size()));
    put32(archive, 4, static_cast<std::uint32_t>(data.size()));
    put32(archive, 8, 0);
    put32(archive, 12, static_cast<std::uint32_t>(names.size()));
    std::copy(data.begin(), data.end(), archive.begin() + 0x20);
    std::size_t name_offset = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto entry = std::size_t{0x20} + data.size() + i * 8;
        put32(archive, entry, roots[i]);
        put32(archive, entry + 4, static_cast<std::uint32_t>(name_offset));
        std::copy(names[i].begin(), names[i].end(),
                  archive.begin() + names_start + name_offset);
        name_offset += names[i].size() + 1;
    }
    return archive;
}

Bytes read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return Bytes(std::istreambuf_iterator<char>(input), {});
}

void synthetic_checks()
{
    auto archive = std::make_shared<const DatArchive>(synthetic());
    DatTrophyData data(archive);
    check(data.init_model_table().size() == 1, "synthetic US table terminator");
    check(data.init_model_d_table().size() == 1, "synthetic JP table terminator");
    check(data.model_sort_table().size() == 294, "source sort table bound");
    check(data.exp_different_table().size() == 6, "synthetic expansion terminator");
    check(data.no_get_us_table().size() == 2, "synthetic no-get terminator");
    check(data.display_model_table().size() == 1 &&
              data.display_model_us_table().size() == 1,
          "synthetic display terminators");

    rejects([] {
        auto malformed = std::make_shared<const DatArchive>(synthetic(true));
        DatTrophyData ignored(malformed);
    }, "model-sort table without source terminator must reject");
}

void asset_checks(const char* path)
{
    auto archive = std::make_shared<const DatArchive>(read_file(path));
    DatTrophyData data(archive);
    check(data.init_model_table().size() == 294, "TyDatai US table count");
    check(data.init_model_d_table().size() == 6, "TyDatai D table count");
    check(data.model_sort_table().size() == 294, "TyDatai sort table count");
    check(data.exp_different_table().size() == 6, "TyDatai expansion table count");
    check(data.no_get_us_table().size() == 2, "TyDatai no-get table count");
    check(data.display_model_table().size() == 295, "TyDatai JP display count");
    check(data.display_model_us_table().size() == 5, "TyDatai US display count");
    check(data.init_model_table().back().id == -1 &&
              data.init_model_d_table().back().id == -1 &&
              data.model_sort_table().back().x0 == -1 &&
              data.exp_different_table().back() == -1 &&
              data.no_get_us_table().back() == -1 &&
              data.display_model_table().back().x00 == -1 &&
              data.display_model_us_table().back().x00 == -1,
          "TyDatai source sentinels");
    check(data.init_model_table().front().id == 0 &&
              data.init_model_table()[1].id == 1,
          "TyDatai source IDs retain big-endian values");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        check(argc == 1 || (argc == 3 && std::string_view(argv[1]) == "--assets"),
              "usage: [--assets TyDatai.usd]");
        synthetic_checks();
        if (argc == 3) asset_checks(argv[2]);
        std::cout << (argc == 1 ? "synthetic" : "typed")
                  << " TyDatai roots, source bounds, and sentinels passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
