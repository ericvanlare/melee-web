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
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace melee_web;

extern "C" int melee_web_pikachu_article_fields_ok(const void*, unsigned,
                                                     int, unsigned);
extern "C" int melee_web_pikachu_article_null_form_rejected(
    const MeleeWebNativeDat*, void*);
extern "C" int melee_web_pikachu_article_missing_model_rejected(
    const MeleeWebNativeDat*, std::uint32_t);

namespace {
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

std::shared_ptr<const DatArchive> read_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "Pikachu Article archive cannot be opened");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes),
          "Pikachu Article archive exceeds the checked bound");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)),
          "Pikachu Article archive is truncated");
    return std::make_shared<const DatArchive>(std::move(bytes));
}

std::uint32_t symbol(const DatArchive& archive, const char* name)
{
    for (const auto& entry : archive.public_symbols())
        if (entry.name == name) return entry.data_offset;
    throw std::runtime_error("Pikachu Article fighter root is missing");
}

void hydrate_family(const std::shared_ptr<const DatArchive>& archive,
                    const char* fighter_symbol, std::uint32_t thunder_kind,
                    std::uint32_t ground_kind, std::uint32_t air_kind,
                    bool pichu)
{
    const std::vector<std::uint8_t> source_before(archive->data().begin(),
                                                  archive->data().end());
    const auto fighter_root = symbol(*archive, fighter_symbol);
    const auto item_table = archive->pointer(fighter_root + 0x48, 12);
    check(item_table.has_value(), "Pikachu Article table is missing");
    NativeDatArena registration_arena(archive);
    const std::uint32_t kinds[] = {thunder_kind, ground_kind, air_kind};
    const std::uint32_t rows[] = {1, 2, 1};
    std::vector<std::unique_ptr<DatItemArticle>> articles;
    articles.reserve(3);
    std::vector<void*> registered;
    registered.reserve(3);
    for (std::size_t slot = 0; slot < 3; ++slot) {
        const auto root = archive->pointer(*item_table + static_cast<std::uint32_t>(slot * 4), 24);
        check(root.has_value(), "Pikachu Article root is missing");
        std::uint32_t unresolved = 0;
        void* article = melee_web_article_decode(registration_arena.reader(), *root, &unresolved);
        check(article && unresolved == ((1U << 1) | (1U << 3) | (1U << 4)),
              "Pikachu Article registration graph is not source-shaped");
        registered.push_back(article);
        try {
            articles.push_back(std::make_unique<DatItemArticle>(archive, *root, kinds[slot], article));
        } catch (const DatError& error) {
            throw DatError(std::string(fighter_symbol) + " Article slot " +
                           std::to_string(slot) + " root " + std::to_string(*root) +
                           " failed: " + error.what());
        }
        check(articles.back()->state_count() == rows[slot],
              "Pikachu Article serialized state count changed");
        check(melee_web_pikachu_article_fields_ok(article, static_cast<unsigned>(slot),
                                                   pichu ? 1 : 0, rows[slot]) == 0,
              "Pikachu Article model, special or state graph changed");
        if (slot == 1) {
            check(melee_web_pikachu_article_missing_model_rejected(
                      registration_arena.reader(), *root) == 0,
                  "Article publication accepted a missing model descriptor");
            check(melee_web_pikachu_article_null_form_rejected(
                      registration_arena.reader(), article) == 0,
                  "Article publication accepted malformed null-model fields");
        }
    }
    /* The air root's 4-byte special block must reject a Thunder schema's
     * 12-byte request; this guards against dispatching by family alone. */
    const auto air_root = archive->pointer(*item_table + 8, 24);
    check(air_root.has_value(), "Pikachu air Article root is missing");
    bool rejected = false;
    try {
        DatItemArticle wrong(archive, *air_root, thunder_kind, registered[2]);
        (void)wrong;
    } catch (const DatError&) {
        rejected = true;
    }
    check(rejected, "Pikachu Article schema accepted an undersized special record");
    articles.clear();
    check(std::equal(source_before.begin(), source_before.end(),
                     archive->data().begin(), archive->data().end()),
          "Pikachu Article hydration modified immutable DAT input");
}
}

int main(int argc, char** argv)
{
    try {
        check(argc == 3, "Expected PlPk.dat and PlPc.dat paths");
        hydrate_family(read_archive(argv[1]), "ftDataPikachu",
                       It_Kind_Pikachu_Thunder, It_Kind_Pikachu_TJolt_Ground,
                       It_Kind_Pikachu_TJolt_Air, false);
        hydrate_family(read_archive(argv[2]), "ftDataPichu",
                       It_Kind_Pichu_Thunder, It_Kind_Pichu_TJolt_Ground,
                       It_Kind_Pichu_TJolt_Air, true);
        std::cout << "Pikachu/Pichu six Article schemas hydrated and torn down; "
                     "undersized Thunder special rejected\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
