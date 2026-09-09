#include "gameplay_action_store.hpp"
#include "gameplay_fighter_data.h"
#include "native_dat.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

using namespace melee_web;

extern "C" int fox_test_native_fighter_data(void*);

namespace {
std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error(std::string("cannot open ") + path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

const FighterCostume& fox_costume()
{
    for (const auto& costume : fighter_costumes())
        if (costume.fighter_kind == 1 && costume.costume_index == 0) return costume;
    throw std::runtime_error("FTKIND_FOX costume 0 is absent from the generated registry");
}
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: fox_gameplay_action_trace PlFx.dat PlFxAJ.dat\n";
        return 2;
    }
    try {
        auto archive = std::make_shared<const DatArchive>(read_file(argv[1]));
        const auto animation = read_file(argv[2]);
        GameplayActionStore store(archive, fox_costume(), animation);
        if (store.runtime().actions().size() != 327 || !store.runtime().fox_attributes())
            throw std::runtime_error("Fox runtime identity or extension is incomplete");
        for (std::uint32_t motion = 44; motion <= 77; ++motion) {
            if (motion == 54 || motion == 56 || motion == 61 || motion == 63 || motion == 65)
                continue;
            if (!store.command_ready(motion))
                throw std::runtime_error("Fox common attack command graph is not admitted: " + std::to_string(motion));
        }
        for (std::uint32_t motion = 295; motion <= 326; ++motion)
            if (!store.command_ready(motion))
                throw std::runtime_error("Fox special command graph is not admitted: " + std::to_string(motion));
        const auto fighter_root = [&] {
            for (const auto& symbol : archive->public_symbols())
                if (symbol.name == "ftDataFox") return symbol.data_offset;
            throw std::runtime_error("Fox fighter root is missing");
        }();
        NativeDatArena arena(archive);
        std::uint32_t unresolved = 0;
        void* data = melee_web_fighter_data_decode(arena.reader(), fighter_root, 1, 4,
            store.action_rows(), store.blend_rows(), store.wait_choices(), &unresolved);
        if (!data || !melee_web_fighter_data_article(data, 0) ||
            !melee_web_fighter_data_article(data, 1) || !melee_web_fighter_data_article(data, 2) ||
            melee_web_fighter_data_article(data, 3))
            throw std::runtime_error("Fox native ftData did not preserve exact Article slots 0, 1 and 2");
        if (!(unresolved & (1U << 7)) || !(unresolved & (1U << 23)))
            throw std::runtime_error("Fox native ftData did not retain part-animation and metal ownership gates");
        if (!fox_test_native_fighter_data(data))
            throw std::runtime_error("Fox native dynamics differ from the source");
        std::cout << "Fox checked ftData, Article slots, common attacks and all 32 source special command rows: passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
