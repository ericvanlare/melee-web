#include "stadium_c1_item_owner_negative_cases.hpp"

#include "dat_archive.hpp"
#include "dat_color_animation.hpp"
#include "dat_item_registry.hpp"
#include "dat_item_registry_native.hpp"
#include "fighter_runtime_fixture.hpp"
#include "gameplay_item_runtime.h"
#include "gameplay_random_article_layout.h"
#include "native_dat.hpp"
#include "stadium_c1_item_owner_preflight.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace stadium_c1_item_owner {
namespace {
using namespace melee_web;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct PublicDataFixture {
    MeleeWebTestItemPublicDataLayout layout{};
    std::array<std::uint32_t, MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS> targets{};
    std::vector<std::uint8_t> data;
    std::vector<std::uint32_t> slots;

    explicit PublicDataFixture(bool short_character_table = false,
                               std::size_t color_bytes = 0)
    {
        check(melee_web_test_item_public_data_layout(&layout) != 0,
              "Source itPublicData layout probe is unavailable");
        check(layout.character_article_count == MELEE_WEB_ITEM_REGISTRY_COUNT,
              "Source character Article count differs from the native registry");
        targets[0] = layout.root_bytes;
        for (std::size_t field = 1; field < targets.size(); ++field) {
            const auto prior = field - 1;
            targets[field] = targets[prior] + layout.target_bytes[prior];
        }
        if (short_character_table) targets[3] = targets[2] + 4;
        data.resize(targets.back() +
                        (color_bytes ? color_bytes
                                     : layout.target_bytes[MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS - 1]),
                    0);
        for (std::size_t field = 0; field < targets.size(); ++field) {
            const auto slot = layout.field_offsets[field];
            animation_test::put32(data, slot, targets[field]);
            slots.push_back(slot);
        }
    }

    void add_extent_boundary(std::size_t field)
    {
        const auto anchor_slot = static_cast<std::uint32_t>(data.size());
        data.resize(data.size() + 4, 0);
        animation_test::put32(data, anchor_slot, targets.at(field) + 4);
        slots.push_back(anchor_slot);
    }

    void add_random_article_source_root()
    {
        const auto index = static_cast<std::size_t>(layout.random_article_index);
        check(index < layout.character_article_count,
              "Random Pokémon Article index exceeds the source table");
        const auto root = static_cast<std::uint32_t>(data.size());
        const auto slot = targets[2] + static_cast<std::uint32_t>(index * 4);
        data.resize(data.size() + layout.article_root_bytes, 0);
        animation_test::put32(data, slot, root);
        slots.push_back(slot);
    }

    std::vector<std::uint8_t> file(
        std::string_view symbol_name = "itPublicData",
        int missing_field = -1) const
    {
        auto file_data = data;
        auto file_slots = slots;
        if (missing_field >= 0) {
            const auto slot = layout.field_offsets[static_cast<std::size_t>(missing_field)];
            animation_test::put32(file_data, slot, 0);
            std::erase(file_slots, slot);
        }
        return fighter_runtime_test::pack(file_data, file_slots, symbol_name, 0);
    }
};

template <class Operation>
void rejects_dat_error(Operation operation, const char* message)
{
    try {
        operation();
    } catch (const DatError&) {
        return;
    }
    throw std::runtime_error(message);
}

void decode_public_data(const std::shared_ptr<const DatArchive>& archive,
                        std::uint32_t root = 0)
{
    NativeDatArena arena(archive);
    std::array<void*, MELEE_WEB_ITEM_REGISTRY_COUNT> character_articles{};
    check(melee_web_item_public_data_decode(
              arena.reader(), root, character_articles.data(),
              character_articles.size()) != nullptr,
          "Synthetic itPublicData decoder returned no root");
}

} // namespace

void run_synthetic_negative_cases(const std::array<void*, 8>& scripts)
{
    using namespace melee_web;
    using SyntheticFiles = std::map<std::string, std::vector<std::uint8_t>,
                                    std::less<>>;
    SyntheticFiles no_files;
    rejects_dat_error([&] {
        require_retained_inputs(no_files);
    }, "Missing retained ItCo/GrPs inputs were accepted");
    SyntheticFiles missing_itco;
    missing_itco.emplace("GrPs.usd", std::vector<std::uint8_t>{});
    rejects_dat_error([&] {
        require_retained_inputs(missing_itco);
    }, "Missing retained ItCo input was accepted");
    no_files.emplace("ItCo.usd", std::vector<std::uint8_t>{});
    rejects_dat_error([&] {
        require_retained_inputs(no_files);
    }, "Missing retained GrPs input was accepted");
    no_files.emplace("GrPs.usd", std::vector<std::uint8_t>{});
    require_retained_inputs(no_files);

    PublicDataFixture fixture;
    auto archive = std::make_shared<const DatArchive>(fixture.file());
    decode_public_data(archive);
    const DatArchive missing_symbol(fixture.file("notItPublicData"));
    rejects_dat_error([&] {
        DatItemRegistry missing_root(missing_symbol);
        (void) missing_root;
    }, "Missing itPublicData public symbol was accepted");

    const auto random_index = static_cast<std::size_t>(fixture.layout.random_article_index);
    const DatItemRegistry source_registry(*archive);
    check(random_index < source_registry.articles.size() &&
              !source_registry.articles[random_index],
          "Synthetic missing-Random fixture did not leave its source slot null");
    const DatItemRegistryNative native_registry(archive);
    rejects_dat_error([&] {
        require_random_article(source_registry.articles[random_index],
                               native_registry.articles()[random_index]);
    }, "Absent Random Article root and registration were accepted");

    PublicDataFixture unregistered_random;
    unregistered_random.add_random_article_source_root();
    const DatArchive with_random_root(unregistered_random.file());
    const DatItemRegistry present_source_registry(with_random_root);
    check(present_source_registry.articles[random_index].has_value(),
          "Synthetic registration-negative fixture lost its Random source root");
    rejects_dat_error([&] {
        require_random_article(present_source_registry.articles[random_index],
                               nullptr);
    }, "Present Random Article source root without native registration was accepted");

    check(scripts.size() > 1 && scripts[1] != nullptr,
          "Capacity-one guard test requires a non-null row-one script");
    rejects_dat_error([&] {
        require_state_capacity(scripts, 1);
    }, "Capacity-one guard accepted its row-one consumer");
    std::cout << "Capacity-one check used caller script row 1; capacity is "
                 "synthetic guard-boundary evidence, not authored ItCo capacity\n";

    for (std::size_t field = 0;
         field < MELEE_WEB_TEST_ITEM_PUBLIC_FIELDS; ++field) {
        PublicDataFixture missing;
        const auto missing_archive =
            std::make_shared<const DatArchive>(missing.file("itPublicData", field));
        rejects_dat_error([&] { decode_public_data(missing_archive); },
                          "A missing itPublicData root field was accepted");

        PublicDataFixture undersized;
        undersized.add_extent_boundary(field);
        const auto short_archive =
            std::make_shared<const DatArchive>(undersized.file());
        if (field == 5) {
            decode_public_data(short_archive);
            rejects_dat_error([&] {
                (void) checked_color_row_count(*short_archive,
                                               undersized.targets[field]);
            }, "A four-byte color-animation extent was accepted");
            rejects_dat_error([&] {
                DatColorAnimation invalid(short_archive, undersized.targets[field], 1);
            }, "DatColorAnimation accepted a short color row extent");
        } else {
            rejects_dat_error([&] { decode_public_data(short_archive); },
                              "An undersized itPublicData target was accepted");
        }
    }

    PublicDataFixture short_character(true);
    archive = std::make_shared<const DatArchive>(short_character.file());
    rejects_dat_error([&] { decode_public_data(archive); },
                      "A four-byte character Article region was accepted while "
                      "remaining entry words were archive-readable");

    PublicDataFixture bad_color(false, 12);
    archive = std::make_shared<const DatArchive>(bad_color.file());
    rejects_dat_error([&] {
        (void) checked_color_row_count(*archive, bad_color.targets[5]);
    }, "A twelve-byte color-animation extent was accepted");
    rejects_dat_error([&] {
        DatColorAnimation invalid(archive, bad_color.targets[5], 1);
    }, "DatColorAnimation accepted a twelve-byte color-animation extent");

    std::cout << "Synthetic item-owner negatives passed: missing ItCo/GrPs, "
                 "absent and unregistered Random roots, public symbol, "
                 "six missing public-data links, undersized x0-x10 targets, "
                 "x14 color extents and the character-table bound\n";
}

} // namespace stadium_c1_item_owner
