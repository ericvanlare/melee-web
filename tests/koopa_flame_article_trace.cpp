#include "dat_archive.hpp"
#include "dat_item_article.hpp"
#include "gameplay_article_data.h"
#include "native_dat.hpp"
#include "gameplay_compat.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/it/forward.h>
}
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

extern "C" int melee_web_koopa_flame_article_fields_ok(const void*);
extern "C" int melee_web_koopa_flame_null_form_rejections(
    const MeleeWebNativeDat*, void*);

namespace {
void check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}

std::shared_ptr<const DatArchive> read_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "Koopa Flame archive cannot be opened");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes),
          "Koopa Flame archive exceeds the checked bound");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)),
          "Koopa Flame archive is truncated");
    return std::make_shared<const DatArchive>(std::move(bytes));
}

std::uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& entry : archive.public_symbols())
        if (entry.name == name)
            return entry.data_offset;
    throw std::runtime_error("Koopa fighter root is missing");
}
}

int main(int argc, char** argv)
{
    try {
        check(argc == 2, "Expected PlKp.dat path");
        const auto archive = read_archive(argv[1]);
        const auto fighter_root = symbol(*archive, "ftDataKoopa");
        const auto table = archive->pointer(fighter_root + 0x48, 4);
        check(table.has_value(), "Koopa Article table is missing");
        const auto root = archive->pointer(*table, 24);
        check(root.has_value(), "Koopa Flame Article root is missing");

        const std::vector<std::uint8_t> source_before(archive->data().begin(),
                                                      archive->data().end());
        NativeDatArena registration_arena(archive);
        std::uint32_t unresolved = 0;
        void* article = melee_web_article_decode(registration_arena.reader(),
                                                 *root, &unresolved);
        check(article && unresolved == ((1U << 1) | (1U << 3) | (1U << 4)),
              "Koopa Flame registration graph is not source-shaped");

        {
            DatItemArticle hydrated(archive, *root, It_Kind_Koopa_Flame,
                                    article);
            check(hydrated.state_count() == 1,
                  "Koopa Flame serialized state count changed");
            check(melee_web_koopa_flame_article_fields_ok(article) == 0,
                  "Koopa Flame special/model/state graph changed");
            check(melee_web_koopa_flame_null_form_rejections(
                      registration_arena.reader(), article) == 0,
                  "Malformed Koopa null-joint forms were accepted or mutated");
        }
        check(std::equal(source_before.begin(), source_before.end(),
                         archive->data().begin(), archive->data().end()),
              "Koopa Flame hydration modified immutable DAT input");
        std::cout << "Koopa Flame 24-byte special, one state, source null-joint model, "
                     "and malformed null-form rejection passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
