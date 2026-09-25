#include "gameplay_compat.h"
#include "gameplay_kirby_copy_assets.hpp"

#include "dat_archive.hpp"
#include "dat_effect_entries.hpp"
#include "dat_material_animation.hpp"
#include "dat_native_joint.hpp"
#include "gameplay_archive_sections.h"
#include "gameplay_content.h"
#include "gameplay_menu_host.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/ft/forward.h>
#include <melee/ef/efasync.h>
#include <melee/ef/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/archive.h>
#include "hsd_native_joint.h"
void ftKb_Init_800EE528(void);
void ftData_800857E0(FighterKind);
void lbArchive_InitializeDAT(HSD_Archive*, void*, size_t);
struct MeleeWebKirbyCopyName { char* filename; char* name; };
struct MeleeWebKirbyCostumeStrings {
    char* dat_filename;
    char* joint_name;
    char* matanim_joint_name;
};
struct MeleeWebKirbyCostumeArchive { void* joint; void* matanim; };
extern MeleeWebKirbyCopyName ftKb_Init_803CA9D0[];
extern MeleeWebKirbyCostumeStrings* ftKb_Init_803CB3E8[];
extern MeleeWebKirbyCostumeArchive* ftKb_Init_803C9FC8[];
extern u8 ftKb_Init_803CB46C[];
extern void* ft_80459B88;
extern EF_DAT_Entry efAsync_DatEntries[51];
}
#pragma GCC diagnostic pop

#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace melee_web {
namespace {

[[noreturn]] void reject(const std::string& message)
{
    throw DatError(message);
}

void destroy_joint(MeleeWebNativeJoint* joint)
{
    char error[128]{};
    if (!melee_web_native_joint_destroy(joint, error, sizeof(error)))
        std::terminate();
}

bool contains_kirby(const MeleeWebMenuMatchSelection& selection)
{
    for (unsigned player = 0; player < GM_MAX_PLAYERS; ++player) {
        if (selection.start.players[player].slot_type == Gm_PKind_NA) break;
        if (selection.start.players[player].ckind == CKIND_KIRBY) return true;
    }
    return false;
}

const DatPublicSymbol& public_root(const DatArchive& archive,
                                  const std::string& symbol);

unsigned source_effect_entry_count(const DatArchive& archive,
                                   const std::string& symbol)
{
    const auto& root = public_root(archive, symbol);
    const auto region = archive.next_target_offset(root.data_offset) -
                        root.data_offset;
    if (region < 8U)
        reject("Kirby source effect table is shorter than its authored header");
    const auto rows = (region - 8U) / 20U;
    if (!rows || rows > 64U)
        reject("Kirby source effect table row count is outside the checked descriptor bound");
    return rows;
}

const DatPublicSymbol& public_root(const DatArchive& archive,
                                  const std::string& symbol)
{
    const auto found = std::find_if(archive.public_symbols().begin(),
                                    archive.public_symbols().end(),
        [&](const DatPublicSymbol& candidate) {
            return candidate.name == symbol;
        });
    if (found == archive.public_symbols().end())
        reject("Kirby source archive is missing public root " + symbol);
    (void)archive.range(found->data_offset, 1);
    return *found;
}

std::uint32_t read_be32(const std::vector<std::uint8_t>& bytes,
                        std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 4)
        reject("Validated Kirby archive field became truncated");
    return (std::uint32_t{bytes[offset]} << 24U) |
           (std::uint32_t{bytes[offset + 1]} << 16U) |
           (std::uint32_t{bytes[offset + 2]} << 8U) |
           std::uint32_t{bytes[offset + 3]};
}

void write_native32(std::vector<std::uint8_t>& bytes, std::size_t offset,
                    std::uint32_t value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void adapt_for_native_source_parser(std::vector<std::uint8_t>& bytes,
                                    const DatArchive& checked)
{
    // The source HSD parser consumes native PPC words. The checked runtime
    // runs on little-endian hosts, so convert only fields identified by the
    // validated GALE01 DAT tables before invoking that original parser.
    const auto data_size = read_be32(bytes, 4);
    const auto reloc_count = read_be32(bytes, 8);
    const auto public_count = read_be32(bytes, 12);
    const auto external_count = read_be32(bytes, 16);
    const std::size_t reloc_start = 0x20U + data_size;
    const std::size_t public_start = reloc_start +
                                     std::size_t{reloc_count} * 4U;
    const std::size_t external_start = public_start +
                                       std::size_t{public_count} * 8U;
    std::unordered_set<std::uint32_t> slots;

    for (const auto& external : checked.external_symbols())
        for (const auto slot : external.slots)
            slots.insert(slot);
    for (std::uint32_t index = 0; index < reloc_count; ++index)
        slots.insert(read_be32(bytes, reloc_start + std::size_t{index} * 4U));
    for (const auto slot : slots)
        write_native32(bytes, 0x20U + slot,
                       read_be32(bytes, 0x20U + slot));

    for (const std::size_t offset : {0U, 4U, 8U, 12U, 16U, 24U, 28U})
        write_native32(bytes, offset, read_be32(bytes, offset));
    for (std::uint32_t index = 0; index < reloc_count; ++index)
        write_native32(bytes, reloc_start + std::size_t{index} * 4U,
                       read_be32(bytes, reloc_start + std::size_t{index} * 4U));
    for (std::uint32_t index = 0; index < public_count * 2U; ++index)
        write_native32(bytes, public_start + std::size_t{index} * 4U,
                       read_be32(bytes, public_start + std::size_t{index} * 4U));
    for (std::uint32_t index = 0; index < external_count * 2U; ++index)
        write_native32(bytes, external_start + std::size_t{index} * 4U,
                       read_be32(bytes, external_start + std::size_t{index} * 4U));
}

void adapt_kirby_copy_parts_count(std::vector<std::uint8_t>& bytes,
                                  const DatArchive& archive,
                                  const KirbyCopyArchiveRequirement& root)
{
    if (root.costume_root) return;

    const auto& symbol = public_root(archive, root.symbol);
    // Some copy roots are the FtPartsDesc itself; others begin with the
    // KirbyHatStruct joint pointer followed by its FtPartsDesc. The source DAT
    // relocation table distinguishes those authored layouts without guessing
    // from a neighboring fighter's row.
    const std::uint32_t model_count_offset =
        archive.has_relocation(symbol.data_offset) ? 4U : 0U;
    const auto region_end = archive.next_target_offset(symbol.data_offset);
    if (region_end < symbol.data_offset ||
        region_end - symbol.data_offset < model_count_offset + 4U)
        reject("Kirby copy FtPartsDesc model count escapes its authored source root");

    const auto count = read_be32(bytes, 0x20U + symbol.data_offset +
                                      model_count_offset);
    if (count > 11U)
        reject("Kirby copy source FtPartsDesc model count exceeds ftParts bounds: " +
               root.symbol + " DAT+" + std::to_string(symbol.data_offset) +
               " field+" + std::to_string(model_count_offset) + " value=" +
               std::to_string(count));
    write_native32(bytes, 0x20U + symbol.data_offset + model_count_offset,
                   count);
}

void add_requirement(std::vector<KirbyCopyArchiveRequirement>& result,
                     const char* filename, const char* symbol,
                     unsigned kind, bool costume_root)
{
    if (!filename || !symbol) reject("Kirby source table contains an incomplete archive root");
    const auto found = std::find_if(result.begin(), result.end(),
        [&](const KirbyCopyArchiveRequirement& value) {
            return value.filename == filename && value.symbol == symbol;
        });
    if (found == result.end()) {
        result.push_back({filename, symbol, kind, costume_root});
    } else if (found->fighter_kind != kind || found->costume_root != costume_root) {
        reject("Kirby source archive root is ambiguously owned");
    }
}

} // namespace

std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const std::vector<unsigned>& fighter_kinds)
{
    std::vector<KirbyCopyArchiveRequirement> result;
    for (const auto kind : fighter_kinds) {
        if (kind >= FTKIND_MAX) reject("Kirby donor kind exceeds the source table");
        const auto& copy = ftKb_Init_803CA9D0[kind];
        if (copy.filename || copy.name)
            add_requirement(result, copy.filename, copy.name, kind, false);

        const auto* costumes = ftKb_Init_803CB3E8[kind];
        if (!costumes) continue;
        const auto& costume = costumes[0]; // Player_80031DC8 passes color zero.
        if (!costume.dat_filename || !costume.joint_name)
            reject("Kirby donor costume-zero source row is incomplete");
        add_requirement(result, costume.dat_filename, costume.joint_name,
                        kind, true);
        if (costume.matanim_joint_name)
            add_requirement(result, costume.dat_filename,
                            costume.matanim_joint_name, kind, true);
    }
    return result;
}

std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const MeleeWebMenuMatchSelection& selection)
{
    if (!contains_kirby(selection)) return {};
    std::vector<unsigned> selected_kinds;
    for (unsigned player = 0; player < GM_MAX_PLAYERS; ++player) {
        const auto& slot = selection.start.players[player];
        if (slot.slot_type == Gm_PKind_NA) break;
        for (unsigned identity = 0;
             identity < melee_web_fighter_kind_count(slot.ckind); ++identity) {
            const auto kind = static_cast<unsigned>(
                melee_web_fighter_kind_at(slot.ckind, identity));
            if (kind >= FTKIND_MAX)
                reject("Selected Kirby donor kind is absent from source identity");
            if (std::find(selected_kinds.begin(), selected_kinds.end(), kind) ==
                selected_kinds.end())
                selected_kinds.push_back(kind);
        }
    }
    return kirby_copy_archive_requirements(selected_kinds);
}

bool selection_uses_kirby(const MeleeWebMenuMatchSelection& selection)
{
    return contains_kirby(selection);
}

std::vector<KirbyCopyEffectRequirement>
kirby_copy_effect_requirements(const MeleeWebMenuMatchSelection& selection)
{
    if (!contains_kirby(selection)) return {};

    std::vector<KirbyCopyEffectRequirement> result;
    for (unsigned player = 0; player < GM_MAX_PLAYERS; ++player) {
        const auto& slot = selection.start.players[player];
        if (slot.slot_type == Gm_PKind_NA) break;
        for (unsigned identity = 0;
             identity < melee_web_fighter_kind_count(slot.ckind); ++identity) {
            const auto kind = static_cast<unsigned>(
                melee_web_fighter_kind_at(slot.ckind, identity));
            const unsigned bank = ftKb_Init_803CB46C[kind];
            if (bank == 0xFFU) continue;
            if (bank >= 51U)
                reject("Kirby copy source effect index exceeds efAsync_DatEntries");
            const auto& source = efAsync_DatEntries[bank];
            const auto* fighter = melee_web_fighter_content_by_kind(kind);
            if (!source.ef_DAT_file || !source.effDataTable_name || !fighter)
                reject("Kirby copy effect bank has no complete source identity");

            KirbyCopyEffectRequirement requirement{
                source.ef_DAT_file, source.effDataTable_name, bank};
            const auto found = std::find_if(result.begin(), result.end(),
                [&](const KirbyCopyEffectRequirement& value) {
                    return value.effect_bank == requirement.effect_bank;
                });
            if (found == result.end()) {
                result.push_back(std::move(requirement));
            } else if (found->filename != requirement.filename ||
                       found->symbol != requirement.symbol) {
                reject("Kirby copy effect source bank has conflicting owners");
            }
        }
    }
    return result;
}

struct GameplayKirbyCopyAssets::Storage {
    struct OwnedArchive {
        std::vector<std::uint8_t> bytes;
        std::unique_ptr<HSD_Archive> native;
        std::shared_ptr<const DatArchive> checked;
        std::unique_ptr<DatNativeJoint> costume_model;
        std::unique_ptr<MeleeWebNativeJoint, decltype(&destroy_joint)>
            native_costume_model{nullptr, &destroy_joint};
        std::unique_ptr<DatMaterialAnimation> costume_matanim;
        void* costume_joint_descriptor = nullptr;
        std::map<std::string, std::uint32_t, std::less<>> public_offsets;
    };
    struct OwnedEffect {
        std::shared_ptr<const DatArchive> archive;
        std::unique_ptr<DatEffectEntries> entries;
    };

    std::vector<KirbyCopyArchiveRequirement> requirements;
    std::vector<KirbyCopyEffectRequirement> effect_requirements;
    std::map<std::string, OwnedArchive, std::less<>> archives;
    std::vector<OwnedEffect> effects;
    std::vector<MeleeWebArchiveSymbol> symbols;
    MeleeWebArchiveSections* scope = nullptr;
    bool activated = false;

    Storage(const RuntimeFiles& files,
            const MeleeWebMenuMatchSelection& selection)
    {
        if (!contains_kirby(selection)) return;

        requirements = kirby_copy_archive_requirements(selection);
        effect_requirements = kirby_copy_effect_requirements(selection);

        for (const auto& requirement : requirements) {
            if (archives.contains(requirement.filename)) continue;
            const auto input = files.find(requirement.filename);
            if (input == files.end())
                reject("Kirby donor archive was not imported: " + requirement.filename);

            // Validate the full source relocation/public/external table before
            // giving mutable bytes to the original HSD archive parser.
            auto checked = std::make_shared<const DatArchive>(
                input->second, DatExternalPolicy::ResolveNull);
            OwnedArchive owned;
            owned.checked = checked;
            for (const auto& root : requirements) {
                if (root.filename == requirement.filename)
                    owned.public_offsets.emplace(
                        root.symbol, public_root(*checked, root.symbol).data_offset);
            }

            owned.bytes = input->second;
            adapt_for_native_source_parser(owned.bytes, *checked);
            for (const auto& root : requirements)
                if (root.filename == requirement.filename)
                    adapt_kirby_copy_parts_count(owned.bytes, *checked, root);
            owned.native = std::make_unique<HSD_Archive>();
            std::memset(owned.native.get(), 0, sizeof(HSD_Archive));
            lbArchive_InitializeDAT(owned.native.get(), owned.bytes.data(),
                                    owned.bytes.size());
            if (!owned.native->data)
                reject("Original HSD parser did not initialize Kirby archive " +
                       requirement.filename);
            archives.emplace(requirement.filename, std::move(owned));
        }

        effects.reserve(effect_requirements.size());
        for (const auto& requirement : effect_requirements) {
            const auto input = files.find(requirement.filename);
            if (input == files.end())
                reject("Kirby donor effect archive was not imported: " +
                       requirement.filename);
            OwnedEffect owned;
            owned.archive = std::make_shared<const DatArchive>(input->second);
            const auto entry_count = source_effect_entry_count(
                *owned.archive, requirement.symbol);
            owned.entries = std::make_unique<DatEffectEntries>(
                owned.archive, requirement.symbol, requirement.effect_bank,
                entry_count, true);
            effects.push_back(std::move(owned));
        }

        for (const auto& requirement : requirements) {
            if (!requirement.costume_root) continue;
            auto& archive = archives.at(requirement.filename);
            const auto* costume = ftKb_Init_803CB3E8[
                requirement.fighter_kind];
            if (!costume) reject("Kirby costume source owner disappeared");
            if (costume[0].joint_name == requirement.symbol) {
                if (archive.costume_model)
                    reject("Kirby donor archive owns multiple costume-zero joint roots");
                archive.costume_model = std::make_unique<DatNativeJoint>(
                    archive.checked,
                    archive.public_offsets.at(requirement.symbol));
                char error[256]{};
                archive.native_costume_model.reset(
                    melee_web_native_joint_hydrate(
                        &archive.costume_model->graph(), error, sizeof(error)));
                if (!archive.native_costume_model) reject(error);
                archive.costume_joint_descriptor =
                    melee_web_native_joint_descriptor(
                        archive.native_costume_model.get(), error,
                        sizeof(error));
                if (!archive.costume_joint_descriptor) reject(error);
            } else if (costume[0].matanim_joint_name == requirement.symbol) {
                if (!archive.costume_model || archive.costume_matanim)
                    reject("Kirby costume material root has no unique source joint owner");
                archive.costume_matanim =
                    std::make_unique<DatMaterialAnimation>(
                        archive.checked,
                        archive.public_offsets.at(requirement.symbol),
                        archive.costume_model->graph());
            } else {
                reject("Kirby costume root differs from the source costume-zero table");
            }
        }

        symbols.reserve(requirements.size());
        for (const auto& requirement : requirements) {
            auto& archive = archives.at(requirement.filename);
            void* native_data = archive.native->data +
                                archive.public_offsets.at(requirement.symbol);
            if (requirement.costume_root) {
                const auto* costume = ftKb_Init_803CB3E8[
                    requirement.fighter_kind];
                if (costume[0].joint_name == requirement.symbol)
                    native_data = archive.costume_joint_descriptor;
                else if (costume[0].matanim_joint_name == requirement.symbol)
                    native_data = archive.costume_matanim->descriptor();
            }
            symbols.push_back({requirement.filename.c_str(),
                               requirement.symbol.c_str(),
                               native_data});
        }
        char error[256]{};
        scope = melee_web_archive_sections_register(
            symbols.data(), symbols.size(), error, sizeof(error));
        if (!scope) reject(error);
    }

    void activate()
    {
        if (requirements.empty()) return;
        if (activated) reject("Kirby donor source preload was activated twice");
        // Publish each donor effect root before the original per-kind preload
        // asks efAsync to load it. The source callback owns the actual load.
        activated = true;
        for (auto& effect : effects) {
            char error[256]{};
            if (!effect.entries->publish_for_source(error, sizeof(error)))
                reject(error);
        }
        // The original per-kind callback enumerates current source player
        // slots. Mark first so a partial callback is reset by teardown.
        ftKb_Init_800EE528();
        ftData_800857E0(FTKIND_KIRBY);

        for (auto& effect : effects) {
            char error[256]{};
            if (!effect.entries->verify_source_load(error, sizeof(error)))
                reject(error);
        }

        for (const auto& requirement : requirements) {
            auto& archive = archives.at(requirement.filename);
            void* expected = archive.native->data +
                             archive.public_offsets.at(requirement.symbol);
            void* actual = nullptr;
            if (requirement.costume_root) {
                auto* costumes = ftKb_Init_803C9FC8[requirement.fighter_kind];
                if (!costumes) reject("Original Kirby costume root owner is absent");
                const auto* source_costume = ftKb_Init_803CB3E8[
                    requirement.fighter_kind];
                if (source_costume[0].joint_name == requirement.symbol) {
                    expected = archive.costume_joint_descriptor;
                    actual = costumes[0].joint;
                } else if (source_costume[0].matanim_joint_name == requirement.symbol) {
                    expected = archive.costume_matanim->descriptor();
                    actual = costumes[0].matanim;
                } else {
                    reject("Kirby costume root no longer matches its source table");
                }
            } else {
                actual = reinterpret_cast<void**>(&ft_80459B88)[
                    requirement.fighter_kind];
            }
            if (actual != expected)
                reject("Original Kirby preload did not publish " +
                       requirement.symbol);
        }
    }

    void close()
    {
        if (activated) {
            ftKb_Init_800EE528();
            activated = false;
        }
        if (scope) {
            char error[256]{};
            if (!melee_web_archive_sections_close(scope, error, sizeof(error)))
                reject(error);
            scope = nullptr;
        }
        for (auto& effect : effects) {
            char error[256]{};
            if (!effect.entries->detach(error, sizeof(error))) reject(error);
        }
        effects.clear();
        // Source globals are cleared above before releasing any typed native
        // roots borrowed by the Kirby costume cache.
        archives.clear();
        requirements.clear();
        effect_requirements.clear();
        symbols.clear();
    }

    ~Storage()
    {
        try {
            close();
        } catch (...) {
            std::terminate();
        }
    }
};

GameplayKirbyCopyAssets::GameplayKirbyCopyAssets(
    const RuntimeFiles& files, const MeleeWebMenuMatchSelection& selection)
    : storage_(std::make_unique<Storage>(files, selection))
{
}

GameplayKirbyCopyAssets::~GameplayKirbyCopyAssets() = default;

void GameplayKirbyCopyAssets::activate()
{
    if (storage_) storage_->activate();
}

void GameplayKirbyCopyAssets::close()
{
    if (storage_) storage_->close();
}

} // namespace melee_web
