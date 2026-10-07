#pragma once
#include "dat_native_stage.hpp"
#include "dat_sis.hpp"
#include "stadium_screen_roots_probe.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>

namespace melee_web::test::stadium_screen {
inline constexpr const char* image_name =
    "GrdPStadiumBG_OVDummy_mat6962_GrdPStadiumDummy_0_image_desc";
inline constexpr const char* sis_name = "SIS_GrPStadiumData";
inline void require(bool value, const char* message) {
    if (!value) throw DatError(message);
}
inline uint32_t root(const DatArchive& archive, const char* name) {
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol.data_offset;
    throw DatError(std::string("Missing required screen public root: ") + name);
}
inline StadiumScreenImageView identity(const DatNativeMap& map, uint32_t entry,
                                      void* candidate) {
    StadiumScreenImageView view{};
    require(stadium_screen_image_view(map.map_head(), entry, candidate, &view) &&
            view.references == 1 && view.image == candidate,
            "Screen IMAGE candidate is not the unique map descriptor reference");
    return view;
}
// The catalog borrows both owners. This scope must end before their destruction.
class Catalog {
public:
    Catalog(const char* filename, const MeleeWebArchiveSymbol* symbols, size_t count) {
        char error[256]{};
        scope = melee_web_archive_sections_register(symbols, count, error, sizeof(error));
        require(scope != nullptr, error);
        handle = melee_web_archive_sections_open(filename);
    }
    ~Catalog() {
        if (handle) melee_web_archive_sections_release(handle);
        if (scope) {
            char error[256]{};
            if (!melee_web_archive_sections_close(scope, error, sizeof(error))) std::abort();
        }
    }
    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;
    MeleeWebArchiveSections* scope{};
    void* handle{};
};
inline void catalog_checks(const DatArchive& archive, const DatNativeMap& map,
                           DatSis& sis, uint32_t entry, uint32_t image_offset) {
    void* image = map.image_descriptor(image_offset);
    const auto before = identity(map, entry, image);
    const auto sis_root = root(archive, sis_name);
    require(sis.entry_count() >= 6, "Screen SIS lacks a checked table slot 5");
    const MeleeWebArchiveSymbol symbols[] = {
        {"GrPs.usd", image_name, image}, {"GrPs.usd", sis_name, sis.descriptor()}};
    Catalog catalog("GrPs.usd", symbols, 2);
    char error[256]{};
    require(!melee_web_archive_sections_register(symbols, 2, error, sizeof(error)),
            "Duplicate screen catalog unexpectedly registered");
    require(!melee_web_archive_sections_close(catalog.scope, error, sizeof(error)),
            "Screen catalog closed with a live consumer");
    int unrelated = 1;
    const MeleeWebArchiveSymbol other_symbol{"ScreenOther.dat", "root", &unrelated};
    Catalog other("ScreenOther.dat", &other_symbol, 1);
    require(!melee_web_archive_sections_close_owned(other.scope, catalog.handle,
                                                   error, sizeof(error)),
            "Wrong scope accepted the screen consumer");
    require(stadium_screen_source_public(other.handle, "root") == &unrelated,
            "Wrong-scope refusal corrupted the other lookup");
    require(stadium_screen_source_public(catalog.handle, image_name) == image &&
            stadium_screen_source_public(catalog.handle, sis_name) == sis.descriptor(),
            "Source HSD lookup lost the checked screen owners");
    require(!stadium_screen_source_public(catalog.handle, "absent-screen-root"),
            "Missing HSD public name did not return NULL");
    auto** table = static_cast<uint8_t**>(sis.descriptor());
    uint8_t scratch[] = {0};
    auto* saved_slot = table[5];
    table[5] = scratch;
    const bool writable_slot = static_cast<uint8_t**>(
        stadium_screen_source_public(catalog.handle, sis_name))[5] == scratch;
    table[5] = saved_slot;
    require(writable_slot, "Published SIS pointer table is not writable");
    bool mutated = false;
    for (uint32_t i = 2; i < sis.entry_count(); ++i) {
        const auto target = archive.pointer(sis_root + 4*i);
        if (!target || !table[i]) continue;
        const auto region = archive.range(*target, archive.next_target_offset(*target)-*target);
        require(!region.empty(), "Screen SIS has no bounded writable region");
        const uint8_t saved = table[i][0];
        table[i][0] ^= 1;
        const bool visible = static_cast<uint8_t**>(
            stadium_screen_source_public(catalog.handle, sis_name))[i][0] == table[i][0];
        const bool raw_unchanged = archive.range(*target, 1)[0] == saved;
        bool aliases_preserved = true;
        for (uint32_t peer=2; peer<sis.entry_count(); ++peer)
            if (archive.pointer(sis_root+4*peer) == target)
                aliases_preserved &= table[peer] == table[i];
        table[i][0] = saved;
        require(visible && raw_unchanged && aliases_preserved, "SIS mutation did not stay inside its owner");
        mutated = true;
        break;
    }
    require(mutated, "Screen SIS lacks a nonempty owned text region");
    const auto after = identity(map, entry, image);
    require(before.image == after.image && before.texture == after.texture &&
            before.material == after.material && before.references == after.references,
            "Screen descriptor graph changed during catalog checks");
}
}
