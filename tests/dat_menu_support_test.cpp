#include "gameplay_compat.h"
#include "dat_menu_support.hpp"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <melee/sc/types.h>
#pragma GCC diagnostic pop

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = std::uint8_t(value >> (24 - 8 * i));
}

Bytes icon_fixture(std::vector<std::uint32_t> payload_sizes = {24, 24, 24, 24})
{
    // The source indexes four payloads (0..3), then consumes a null
    // terminator. Each payload must cover hsd_803B2ADC's 18-byte copy.
    check(!payload_sizes.empty() && payload_sizes.size() <= 16,
          "synthetic icon fixture has an invalid payload count");
    constexpr std::string_view name = "MemCardIconData";
    std::uint32_t root = 0;
    for (const auto size : payload_sizes) {
        check(size > 0, "synthetic icon fixture has an empty payload");
        root += size;
    }
    const std::uint32_t data_size = root +
                                    static_cast<std::uint32_t>((payload_sizes.size() + 1) * 4);
    const std::uint32_t relocation_count = static_cast<std::uint32_t>(payload_sizes.size());
    const std::size_t relocation_table = 0x20 + data_size;
    const std::size_t public_table = relocation_table + relocation_count * 4;
    const std::size_t names = public_table + 8;
    Bytes bytes(names + name.size() + 1, 0);
    put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
    put32(bytes, 4, data_size);
    put32(bytes, 8, relocation_count);
    put32(bytes, 12, 1); // one public symbol
    std::uint32_t target = 0;
    for (std::size_t index = 0; index < payload_sizes.size(); ++index) {
        put32(bytes, 0x20 + root + index * 4, target);
        put32(bytes, relocation_table + index * 4,
              root + static_cast<std::uint32_t>(index * 4));
        target += payload_sizes[index];
    }
    put32(bytes, public_table, root);
    put32(bytes, public_table + 4, 0);
    std::copy(name.begin(), name.end(), bytes.begin() + names);
    return bytes;
}

std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "menu support asset is unavailable");
    return {std::istreambuf_iterator<char>(stream), {}};
}

template <class F>
void rejects(F&& function, const char* message)
{
    bool rejected = false;
    try {
        function();
    } catch (const melee_web::DatError&) {
        rejected = true;
    }
    check(rejected, message);
}

void test_synthetic()
{
    using melee_web::DatArchive;
    using melee_web::DatMenuSupport;
    using melee_web::DatMenuSupportKind;
    using melee_web::DatMenuSupportLanguage;

    check(DatMenuSupport::resolve_filename("LbMcGame.", DatMenuSupportLanguage::Other,
                                          DatMenuSupportLanguage::English) == "LbMcGame.dat",
          "trailing-dot filename uses the setting locale");
    check(DatMenuSupport::resolve_filename("NtMemAc", DatMenuSupportLanguage::English,
                                          DatMenuSupportLanguage::Other) == "NtMemAc.dat",
          "extensionless filename uses the saved locale");
    check(DatMenuSupport::resolve_filename("already.bin", DatMenuSupportLanguage::Other,
                                          DatMenuSupportLanguage::English) == "already.bin",
          "existing source extension is preserved");

    auto bytes = icon_fixture();
    auto archive = std::make_shared<DatArchive>(bytes);
    DatMenuSupport icons(archive, DatMenuSupportKind::CardIcons);
    check(icons.source_basename() == "LbMcGame.", "synthetic source basename is exact");
    check(icons.resolved_filename() == "LbMcGame.usd", "synthetic filename is normalized");
    check(icons.symbol_name() == "MemCardIconData", "synthetic symbol is exact");
    check(icons.icon_count() == 4 && icons.icon_payload(0).size() == 24 &&
              icons.icon_payload(1).size() == 24 && icons.icon_payload(2).size() == 24 &&
              icons.icon_payload(3).size() == 24,
          "synthetic pointer table preserves bounded payload spans");
    check(icons.descriptor() != nullptr, "synthetic icon descriptor is published");

    rejects([&] {
        auto bad = std::make_shared<DatArchive>(icon_fixture({24, 24, 24}));
        DatMenuSupport ignored(bad, DatMenuSupportKind::CardIcons);
    }, "short icon table is rejected before source index 3");

    rejects([&] {
        auto bad = std::make_shared<DatArchive>(icon_fixture({17, 24, 24, 24}));
        DatMenuSupport ignored(bad, DatMenuSupportKind::CardIcons);
    }, "short icon payload is rejected before the 18-byte card copy");

    auto bad_extra_entry = icon_fixture({24, 24, 24, 24, 24});
    rejects([&] {
        auto bad = std::make_shared<DatArchive>(bad_extra_entry);
        DatMenuSupport ignored(bad, DatMenuSupportKind::CardIcons);
    }, "extra icon entry is rejected beyond source-owned table");

    auto missing_root = bytes;
    std::copy(std::string_view("WrongRoot").begin(), std::string_view("WrongRoot").end(),
              missing_root.begin() + 0x20 + 116 + 16 + 8);
    rejects([&] {
        auto bad = std::make_shared<DatArchive>(missing_root);
        DatMenuSupport ignored(bad, DatMenuSupportKind::CardIcons);
    }, "missing required public root is rejected");
}

void test_local_assets(const char* icon_path, const char* scene_path)
{
    using melee_web::DatArchive;
    using melee_web::DatMenuSupport;
    using melee_web::DatMenuSupportKind;

    auto icon_bytes = read_file(icon_path);
    auto icon_archive = std::make_shared<DatArchive>(icon_bytes);
    DatMenuSupport icons(icon_archive, DatMenuSupportKind::CardIcons);
    check(icons.source_basename() == "LbMcGame.", "card source basename is exact");
    check(icons.resolved_filename() == "LbMcGame.usd", "card filename is normalized");
    check(icons.symbol_name() == "MemCardIconData", "card symbol is exact");
    check(icons.icon_count() == 4, "card icon table has four payload entries");
    const std::size_t expected_sizes[] = {0x1800, 0x1800, 0x1800, 0x600};
    for (std::size_t i = 0; i < icons.icon_count(); ++i)
        check(icons.icon_payload(i).size() == expected_sizes[i],
              "card payload retains its exact archive extent");
    check(icons.icon_payload(0).data() == icon_archive->data().data(),
          "card payload retains relocated zero target identity");

    auto scene_bytes = read_file(scene_path);
    auto scene_archive = std::make_shared<DatArchive>(scene_bytes);
    DatMenuSupport scene(scene_archive, DatMenuSupportKind::CardScene);
    check(scene.source_basename() == "NtMemAc", "scene source basename is exact");
    check(scene.resolved_filename() == "NtMemAc.usd", "scene filename is normalized");
    check(scene.symbol_name() == "ScNtcCommon_scene_data", "scene symbol is exact");
    check(scene.model_count() == 1 && scene.camera_count() == 1,
          "CSS scene publishes its single model and camera records");
    check(scene.camera_animation_count() == 1,
          "authored zero-channel camera animation table remains owned");
    check(scene.unconsumed_light_list_count() == 2 && !scene.has_unconsumed_fog(),
          "CSS scene reports source light/fog services outside lb_8001CF18");
    auto* descriptor = static_cast<SceneDesc*>(scene.descriptor());
    check(descriptor && descriptor->models && descriptor->models[0] &&
              descriptor->models[0]->joint && descriptor->models[0]->matanims,
          "CSS scene model 0 joint/material animation descriptors are typed");
    check(descriptor->cameras && descriptor->cameras[0].desc &&
              descriptor->cameras[0].anims && descriptor->cameras[0].anims[0],
          "CSS scene camera 0 and authored camera animation are typed");
    check(descriptor->lights == nullptr && descriptor->fogs == nullptr,
          "unconsumed scene services are not published as guessed descriptors");

    auto malformed = icon_bytes;
    check(malformed.size() > 32 + 0x4e00 + 16 + 4,
          "card archive fixture has an unexpected size");
    malformed[32 + 0x4e00 + 16 + 3] = 1;
    rejects([&] {
        auto bad_archive = std::make_shared<DatArchive>(malformed);
        DatMenuSupport bad(bad_archive, DatMenuSupportKind::CardIcons);
    }, "malformed card terminator is rejected");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        check(argc == 1 || (argc == 4 && std::string_view(argv[1]) == "--assets"),
              "usage: [--assets LbMcGame.usd NtMemAc.usd]");
        test_synthetic();
        if (argc == 4)
            test_local_assets(argv[2], argv[3]);
        std::cout << (argc == 1 ? "synthetic " : "typed ")
                     << "CSS card support roots, normalization, ownership, and bounds passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
