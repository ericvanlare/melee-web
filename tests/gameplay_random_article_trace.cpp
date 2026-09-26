#include "dat_archive.hpp"
#include "dat_item_article.hpp"
#include "dat_item_registry.hpp"
#include "dat_item_registry_native.hpp"
#include "gameplay_article_data.h"
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
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace melee_web;

extern "C" int melee_web_random_article_rows_writable(void* article,
                                                       uint32_t rows);
extern "C" int melee_web_random_article_ready(void* article);
extern "C" int melee_web_random_article_late_attributes_ready(void* article);

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::shared_ptr<const DatArchive> read_archive(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), "Random Article ItCo archive cannot be opened");
    const auto size = input.tellg();
    check(size > 0 && size <= std::streamoff(DatArchive::max_archive_bytes),
          "Random Article ItCo archive exceeds the checked bound");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(bytes.data()), size)),
          "Random Article ItCo archive is truncated");
    return std::make_shared<const DatArchive>(
        std::move(bytes), DatExternalPolicy::PreserveUnresolved);
}
}

int main(int argc, char** argv)
{
    try {
        check(argc == 2, "Expected ItCo.usd path");
        const auto archive = read_archive(argv[1]);
        const std::vector<std::uint8_t> source_before(archive->data().begin(),
                                                      archive->data().end());
        const std::size_t random_index =
            static_cast<std::size_t>(It_PKind_Random - It_Kind_Kuriboh);
        check(random_index == MELEE_WEB_ITEM_REGISTRY_COUNT - 1,
              "Random Pokémon Article moved from its authored registry slot");
        const DatItemRegistry table(*archive);
        check(table.articles[random_index].has_value(),
              "Random Pokémon Article root is absent");
        const auto model_slot = archive->pointer(
            *table.articles[random_index] + 16, 16);
        check(model_slot.has_value(), "Random Pokémon Article model slot is absent");
        const auto model_root = archive->pointer(*model_slot, 64);
        check(!model_root && archive->be32(*model_slot + 4) == 1 &&
                  archive->be32(*model_slot + 8) == 0,
              "Random Pokémon Article changed its source identity-root model descriptor");

        DatItemRegistryNative registered(archive);
        auto* article = static_cast<Article*>(registered.articles()[random_index]);
        check(article != nullptr, "Random Pokémon Article registration is missing");
        {
            DatItemArticle owner(archive, *table.articles[random_index],
                                 It_PKind_Random, article);
            check(owner.state_count() == 20,
                  "Random Pokémon Article did not retain its 20 authored state rows");
            check(melee_web_random_article_ready(article) == 0 &&
                      melee_web_article_unresolved(article) == 1,
                  "Random Pokémon Article did not retain its late common-attribute boundary");

            // Ground_801C0800 writes the original ALDYakuAll command pointers
            // into Random's state rows 1..N before loading collision.
            check(melee_web_random_article_rows_writable(article, 20) == 0,
                  "Random Pokémon Article source state rows are not writable");
            check(melee_web_random_article_late_attributes_ready(article) == 0,
                  "Random Pokémon Article did not become ready after its source late attribute");
        }
        check(std::equal(source_before.begin(), source_before.end(),
                         archive->data().begin(), archive->data().end()),
              "Random Article hydration modified immutable ItCo DAT input");
        std::cout << "Random Pokémon Article retained 20 DAT-bounded state rows; "
                     "Ground ALDYakuAll rows writable, late attribute resolved, "
                     "and immutable archive preserved\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
