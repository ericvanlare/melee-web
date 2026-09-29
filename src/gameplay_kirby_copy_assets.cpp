#include "gameplay_compat.h"
#include "gameplay_kirby_copy_assets.hpp"

#include "dat_archive.hpp"
#include "dat_material.hpp"
#include "dat_effect_entries.hpp"
#include "dat_material_animation.hpp"
#include "dat_native_joint.hpp"
#include "dat_item_article.hpp"
#include "native_dat.hpp"
#include "gameplay_article_data.h"
#include "gameplay_archive_sections.h"
#include "gameplay_content.h"
#include "gameplay_menu_host.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
extern "C" {
#include <melee/ft/forward.h>
#include <melee/ft/dobjlist.h>
#include <melee/it/forward.h>
#include <melee/ef/efasync.h>
#include <melee/ef/types.h>
#include <melee/pl/forward.h>
#include <sysdolphin/baselib/archive.h>
#include "hsd_native_joint.h"
void ftKb_Init_800EE528(void);
void ftKb_SpecialN_800EED50(s32, s32);
void ftData_800857E0(FighterKind);
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
#include <cstdio>
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

void write_native16(std::vector<std::uint8_t>& bytes, std::size_t offset,
                    std::uint16_t value)
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

void adapt_native_source_pobj_fields(std::vector<std::uint8_t>& bytes,
                                     const DatArchive& checked,
                                     const MeleeWebNativeGraph& graph,
                                     std::unordered_set<std::uint32_t>& adapted)
{
    // HSD_ArchiveParse relocates PObj pointers but does not convert PPC-native
    // scalar fields on little-endian hosts. The source PObj loader reads both
    // fields directly: flags select SKIN/ENVELOPE/SHAPE, and n_display bounds
    // the display-list records. Only convert descriptors reached by the
    // checked native-joint graph consumed by Kirby's original copy loader.
    for (std::uint32_t index = 0; index < graph.pobj_count; ++index) {
        const auto& pobj = graph.pobjs[index];
        const auto source = pobj.source_offset;
        if (source == UINT32_MAX)
            reject("Kirby source PObj has no authored descriptor identity");
        if (!adapted.insert(source).second) continue;
        const auto flags = checked.be16(source + 12U);
        const auto display_count = checked.be16(source + 14U);
        if (flags != pobj.geometry.flags ||
            std::uint64_t(display_count) * 32U !=
                pobj.geometry.display_byte_size)
            reject("Kirby source PObj scalar fields disagree with its checked graph at DAT+" +
                   std::to_string(source));
        write_native16(bytes, 0x20U + source + 12U, flags);
        write_native16(bytes, 0x20U + source + 14U, display_count);
    }
}

void adapt_native_source_material_modes(std::vector<std::uint8_t>& bytes,
                                        std::shared_ptr<const DatArchive> checked,
                                        const std::string& filename,
                                        const std::vector<std::uint32_t>& joint_roots)
{
    // HSD_ArchiveParse relocates pointers but the source DObj loader also
    // reads MObjDesc::rendermode as a native u32. Use the same checked joint
    // graphs that own the source copy/costume models; a raw aligned scan can
    // mistake overlapping DObj fields for another descriptor.
    std::unordered_set<std::uint32_t> adapted_materials;
    std::unordered_set<std::uint32_t> adapted_pobjs;
    const auto adapt_material = [&](std::uint32_t material) {
        if (!adapted_materials.insert(material).second) return;
        const auto source_mode = checked->be32(material + 4U);
        switch (source_mode & 0x60000000U) {
        case 0:
        case 0x40000000U:
        case 0x60000000U:
            break;
        default:
            reject("Kirby source DObj material in " + filename +
                   " at DAT+" + std::to_string(material) +
                   " has an invalid source blending mode");
        }
        if (filename == "PlKbCpFc.dat")
            std::fprintf(stderr, "Kirby fallback material file=%s DAT+0x%x mode=0x%08x\n",
                         filename.c_str(), material, source_mode);
        write_native32(bytes, 0x20U + material + 4U, source_mode);
    };

    if (joint_roots.empty()) {
        // Some copy roots are FtPartsDesc-only and have no joint pointer.
        // Discover material candidates from checked DAT DObj/PObj pointer
        // fields, rejecting overlapping records rather than treating every
        // aligned pointer-shaped region as a native descriptor.
        struct DObjMaterial {
            std::uint32_t dobj;
            std::uint32_t material;
        };
        std::vector<DObjMaterial> candidates;
        std::unordered_set<std::uint32_t> dobjs;
        const auto data_size = checked->data().size();
        for (std::uint32_t dobj = 0; dobj + 16U <= data_size; dobj += 4U) {
            if (!checked->has_relocation(dobj + 8U)) continue;
            try {
                if (checked->pointer(dobj)) continue;
                (void)checked->pointer(dobj + 4U, 16);
                const auto material = checked->pointer(dobj + 8U, 24);
                (void)checked->pointer(dobj + 12U, 24);
                if (!material) continue;
                candidates.push_back({dobj, *material});
                dobjs.insert(dobj);
            } catch (const DatError&) {
                continue;
            }
        }
        for (const auto& candidate : candidates) {
            if (dobjs.contains(candidate.material) ||
                checked->has_relocation(candidate.material + 4U))
                continue;
            try {
                if (checked->pointer(candidate.material)) continue;
                const auto has_textures = checked->pointer(candidate.material + 8U);
                const auto has_material = checked->pointer(candidate.material + 12U);
                if (!has_textures && !has_material) continue;
                (void)read_dat_material(*checked, candidate.material,
                                        DatMaterialPolicy::NativeDescriptors);
            } catch (const DatError&) {
                continue;
            }
            adapt_material(candidate.material);
        }
        return;
    }

    for (const auto joint : joint_roots) {
        std::unique_ptr<DatNativeJoint> model;
        try {
            model = std::make_unique<DatNativeJoint>(checked, joint);
        } catch (const DatError& error) {
            reject("Kirby source material graph in " + filename + " at joint DAT+" +
                   std::to_string(joint) + ": " + error.what());
        }
        const auto& graph = model->graph();
        if (filename == "PlKbCpFc.dat" && joint == 0xf954U) {
            bool has_skin_target = false;
            for (std::uint32_t index = 0; index < graph.joint_count; ++index)
                has_skin_target |= graph.joints[index].source_offset == 0xc080U;
            std::fprintf(stderr,
                "Kirby copy graph root=0x%x joints=%u dobjs=%u pobjs=%u has_skin_target_0xc080=%u\n",
                joint, graph.joint_count, graph.dobj_count, graph.pobj_count,
                unsigned(has_skin_target));
            for (std::uint32_t index = 0; index < graph.pobj_count; ++index)
                std::fprintf(stderr,
                    "Kirby copy PObj source=0x%x flags=0x%x display_bytes=0x%x\n",
                    graph.pobjs[index].source_offset,
                    graph.pobjs[index].geometry.flags,
                    graph.pobjs[index].geometry.display_byte_size);
            for (std::uint32_t index = 0; index < graph.dobj_count; ++index) {
                const auto& dobj = graph.dobjs[index];
                std::fprintf(stderr,
                    "Kirby copy DObj source=0x%x pobj_index=0x%x pobj_source=0x%x\n",
                    dobj.source_offset, dobj.pobj,
                    dobj.pobj == UINT32_MAX ? UINT32_MAX :
                        graph.pobjs[dobj.pobj].source_offset);
            }
        }
        adapt_native_source_pobj_fields(bytes, *checked, graph, adapted_pobjs);
        for (std::uint32_t index = 0; index < graph.material_count; ++index) {
            const auto material = graph.materials[index].source_offset;
            adapt_material(material);
        }
    }
}

void adapt_kirby_copy_parts_count(std::vector<std::uint8_t>& bytes,
                                  const DatArchive& archive,
                                  const KirbyCopyArchiveRequirement& root,
                                  const std::vector<unsigned>& costume_ids,
                                  unsigned body_model_count)
{
    if (root.costume_root || costume_ids.empty()) return;

    const auto& symbol = public_root(archive, root.symbol);
    // Some copy roots are the FtPartsDesc itself; others begin with the
    // KirbyHatStruct joint pointer followed by its FtPartsDesc. The source DAT
    // relocation table distinguishes those authored layouts without guessing
    // from a neighboring fighter's row.
    const std::uint32_t model_count_offset =
        archive.has_relocation(symbol.data_offset) ? 4U : 0U;
    const std::uint32_t descriptor = symbol.data_offset + model_count_offset;
    try {
        (void)archive.range(descriptor, 8);
    } catch (const DatError&) {
        reject("Kirby copy FtPartsDesc model count escapes its authored source root");
    }

    const auto count = archive.be32(descriptor);
    if (count > 11U)
        reject("Kirby copy source FtPartsDesc model count exceeds ftParts bounds: " +
               root.symbol + " DAT+" + std::to_string(symbol.data_offset) +
               " field+" + std::to_string(model_count_offset) + " value=" +
               std::to_string(count));
    write_native32(bytes, 0x20U + descriptor, count);
    constexpr std::uint32_t kirby_costume_count = 6;
    const auto max_costume = *std::max_element(costume_ids.begin(),
                                               costume_ids.end());
    if (max_costume >= kirby_costume_count)
        reject("Kirby selected costume exceeds the source costume table");

    // The Game & Watch copy root also publishes the source ftData_x8_x8
    // texture-animation pair immediately after its FtPartsDesc. LOAD_HAT
    // passes the address of that pair (root+8) to ftAnim_80070200; convert its
    // authored count and validate only the source-selected costume rows.
    if (model_count_offset == 0 &&
        archive.has_relocation(symbol.data_offset + 12U)) {
        const auto tobj_count=archive.be32(symbol.data_offset+8U);
        constexpr auto tobj_capacity=sizeof(
            ((CostumeTObjList*)nullptr)->costume_tobjs)/sizeof(HSD_TObj*);
        if(tobj_count>tobj_capacity)
            reject("Kirby copy source texture-animation count exceeds the source TObj list bound: "+
                   root.symbol);
        write_native32(bytes,0x20U+symbol.data_offset+8U,tobj_count);
        const auto rows=archive.pointer(symbol.data_offset+12U,
                                        (max_costume+1U)*4U);
        if(!rows)
            reject("Kirby copy source texture-animation costume table is missing: "+
                   root.symbol);
        for(const auto costume_value:costume_ids) {
            const auto costume=static_cast<std::uint32_t>(costume_value);
            auto list=archive.pointer(*rows+costume*4U,
                                       std::size_t{tobj_count}*2U);
            // ftAnim_80070200 falls back to costume zero when the selected
            // costume has no texture-index list of its own.
            if(!list)
                list=archive.pointer(*rows,std::size_t{tobj_count}*2U);
            if(tobj_count&&!list)
                reject("Kirby copy source texture-animation list is missing: "+
                       root.symbol);
            for(std::uint32_t i=0;i<tobj_count;i++)
                write_native16(bytes,0x20U+*list+i*2U,
                               archive.be16(*list+i*2U));
        }
    }

    // ftKb_LoadHat (and the common hat loader) passes costume zero for a
    // joint-backed KirbyHatStruct, independent of Kirby's body costume.
    // Only the FtPartsDesc-only LOAD_HAT path passes x619_costume_id. The
    // joint-backed archives need not author any further visibility rows.
    const std::vector<unsigned> visibility_costumes =
        model_count_offset != 0 ? std::vector<unsigned>{0} : costume_ids;
    const auto max_visibility_costume = *std::max_element(
        visibility_costumes.begin(), visibility_costumes.end());

    // Each FtPartsDesc visibility row has four FtPartsVisLookup pointers; each
    // lookup then owns model_num {variant_count, TempS*} rows. The PPC HSD
    // archive loader relocates those pointers but does not byte-swap the
    // counts, so the original ftParts routines otherwise interpret (for
    // example) BE 1 and 7 as 0x01000000 and 0x07000000 on this host.
    constexpr std::uint32_t visibility_slots_per_costume = 4;
    constexpr std::uint32_t visibility_pointer_bytes = 4;
    constexpr std::uint32_t lookup_row_bytes = 8;
    constexpr std::uint32_t variant_row_bytes = 8;
    constexpr std::uint32_t source_visibility_variant_limit = 128;
    constexpr std::uint32_t source_visibility_index_limit = 124;
    constexpr std::uint32_t costume_row_bytes =
        visibility_slots_per_costume * visibility_pointer_bytes;
    const auto table = archive.pointer(
        descriptor + 4,
        (max_visibility_costume + 1U) * costume_row_bytes);
    if (!table)
        reject("Kirby copy FtPartsDesc visibility table is missing: " +
               root.symbol);
    // Relocation targets can alias rows, so next_target_offset is not an
    // array extent. Validate the source-selected rows and their per-category
    // row-zero fallback, as consumed by ftParts_8007487C.
    for (const auto costume_value : visibility_costumes) {
        const auto costume = static_cast<std::uint32_t>(costume_value);
        for (std::uint32_t slot = 0; slot < visibility_slots_per_costume; ++slot) {
            const auto pointer_slot = *table + costume * costume_row_bytes +
                                      slot * visibility_pointer_bytes;
            auto lookup = archive.pointer(
                pointer_slot, std::size_t{count} * lookup_row_bytes);
            if (!lookup)
                lookup = archive.pointer(
                    *table + slot * visibility_pointer_bytes,
                    std::size_t{count} * lookup_row_bytes);
            if (!lookup || count == 0) continue;

            for (std::uint32_t model = 0; model < count; ++model) {
                const auto row = *lookup + model * lookup_row_bytes;
                const auto variants = archive.be32(row);
                if (variants > source_visibility_variant_limit)
                    reject("Kirby copy part-visibility variant count exceeds the source selector bound: " +
                           root.symbol);
                write_native32(bytes, 0x20U + row, variants);
                const auto entries = archive.pointer(
                    row + 4, std::size_t{variants} * variant_row_bytes);
                if (variants == 0) continue;
                if (!entries)
                    reject("Kirby copy part-visibility variant rows are missing: " +
                           root.symbol);

                for (std::uint32_t variant = 0; variant < variants; ++variant) {
                    const auto entry = *entries + variant * variant_row_bytes;
                    const auto indices_count = archive.be32(entry);
                    if (indices_count > source_visibility_index_limit)
                        reject("Kirby copy part-visibility index count exceeds the source DObj bound: " +
                               root.symbol);
                    write_native32(bytes, 0x20U + entry, indices_count);
                    const auto indices = archive.pointer(entry + 4, indices_count);
                    if (indices_count == 0) continue;
                    if (!indices)
                        reject("Kirby copy part-visibility indices are missing: " +
                               root.symbol);
                }
            }
        }
    }

    // The Game & Watch copy callback also switches the secondary visibility
    // lookup stored in KirbyHatStruct::hat_dynamics[3]. That authored table
    // uses the same FtPartsVisLookup/TempS layout as FtPartsDesc::vis_table,
    // but lives at the source root's +0x18 pointer field and is consumed by
    // ftParts_80074D7C after copy acquisition.
    if (root.fighter_kind == FTKIND_GAMEWATCH) {
        constexpr std::uint32_t special_lookup_pointer_offset = 0x18;
        const auto lookup = archive.pointer(
            symbol.data_offset + special_lookup_pointer_offset,
            std::size_t{count} * lookup_row_bytes);
        if (!lookup)
            reject("Kirby Game & Watch secondary part-visibility lookup is missing");
        // F14B4 lends this same lookup to Kirby's body (whose model count is
        // independent). The original B6C/CA0/D7C inner loops use signed cmpw.
        // Prove every omitted iteration is empty before 750C8 scopes that
        // borrowed view to its owner's count. A relocated count aliases an
        // adjacent pointer: GALE01's original archive lies in MEM1, so that
        // word is negative, unlike its positive Wasm pointer. Never infer an
        // empty row merely from a shorter descriptor or an asset filename.
        if (!count || !body_model_count || body_model_count > 11U)
            reject("Kirby borrowed visibility has no validated body model count");
        for (std::uint32_t model = count; model < body_model_count; ++model) {
            const auto row = *lookup + model * lookup_row_bytes;
            if (archive.has_relocation(row)) {
                if (!archive.pointer(row, 1))
                    reject("Kirby borrowed visibility count alias has no source target");
            } else {
                const auto word = archive.be32(row);
                if (word != 0 && word < 0x80000000U)
                    reject("Kirby borrowed visibility tail has a nonempty original signed loop");
            }
        }
        for (std::uint32_t model = 0; model < count; ++model) {
            const auto row = *lookup + model * lookup_row_bytes;
            const auto groups = archive.be32(row);
            if (groups > source_visibility_variant_limit)
                reject("Kirby Game & Watch secondary visibility group count exceeds the source bound");
            write_native32(bytes, 0x20U + row, groups);
            const auto entries = archive.pointer(
                row + 4, std::size_t{groups} * variant_row_bytes);
            if (groups == 0) continue;
            if (!entries)
                reject("Kirby Game & Watch secondary visibility entries are missing");
            for (std::uint32_t group = 0; group < groups; ++group) {
                const auto entry = *entries + group * variant_row_bytes;
                const auto indices_count = archive.be32(entry);
                if (indices_count > source_visibility_index_limit)
                    reject("Kirby Game & Watch secondary visibility index count exceeds the source DObj bound");
                write_native32(bytes, 0x20U + entry, indices_count);
                if (indices_count != 0 &&
                    !archive.pointer(entry + 4, indices_count))
                    reject("Kirby Game & Watch secondary visibility indices are missing");
            }
        }
    }
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

struct CopyCostumeRequirement {
    unsigned fighter_kind;
    unsigned costume_id;
    const MeleeWebKirbyCostumeStrings* source;
};

std::vector<unsigned> selected_kirby_costumes(
    const MeleeWebMenuMatchSelection& selection)
{
    std::vector<unsigned> result;
    for (const auto& slot : selection.start.players) {
        if (slot.slot_type == Gm_PKind_NA) break;
        if (slot.ckind != CKIND_KIRBY) continue;
        const auto costume = static_cast<unsigned>(slot.color);
        if (costume >= 6U)
            reject("Kirby selected costume is outside ftKb_CostumeList");
        if (std::find(result.begin(), result.end(), costume) == result.end())
            result.push_back(costume);
    }
    return result;
}

std::vector<CopyCostumeRequirement> copy_costume_requirements(
    const std::vector<unsigned>& fighter_kinds,
    const std::vector<unsigned>& selected_costumes)
{
    // Player_80031DC8 preloads color zero. The original scene preload also
    // calls Player_80031DA8 / ftKb_SpecialN_800EED50 for Kirby's selected
    // colors (gm_16A2.c). Acquisition indexes that exact cache row in
    // ftKb_SpecialN_800EF0E4/800EF35C, even when all rows name the same DAT.
    std::vector<unsigned> colors{0};
    for (const auto color : selected_costumes) {
        if (color >= 6U)
            reject("Kirby selected costume is outside ftKb_CostumeList");
        if (std::find(colors.begin(), colors.end(), color) == colors.end())
            colors.push_back(color);
    }
    std::vector<CopyCostumeRequirement> result;
    for (const auto kind : fighter_kinds) {
        if (kind >= FTKIND_MAX) reject("Kirby donor kind exceeds the source table");
        const auto* costumes = ftKb_Init_803CB3E8[kind];
        if (!costumes) continue;
        for (const auto color : colors) {
            const auto& source = costumes[color];
            if (!source.dat_filename || !source.joint_name)
                reject("Kirby donor source costume row is incomplete");
            result.push_back({kind, color, &source});
        }
    }
    return result;
}

std::vector<KirbyCopyArchiveRequirement>
copy_archive_requirements(const std::vector<unsigned>& fighter_kinds,
                          const std::vector<CopyCostumeRequirement>& costumes)
{
    std::vector<KirbyCopyArchiveRequirement> result;
    for (const auto kind : fighter_kinds) {
        if (kind >= FTKIND_MAX) reject("Kirby donor kind exceeds the source table");
        const auto& copy = ftKb_Init_803CA9D0[kind];
        if (copy.filename || copy.name)
            add_requirement(result, copy.filename, copy.name, kind, false);
        for (const auto& costume : costumes) {
            if (costume.fighter_kind != kind) continue;
            const auto& source = *costume.source;
            add_requirement(result, source.dat_filename, source.joint_name,
                            kind, true);
            if (source.matanim_joint_name)
                add_requirement(result, source.dat_filename,
                                source.matanim_joint_name, kind, true);
        }
    }
    return result;
}

std::vector<unsigned> selected_copy_kinds(const MeleeWebMenuMatchSelection& selection)
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
    return selected_kinds;
}

} // namespace

std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const std::vector<unsigned>& fighter_kinds)
{
    return copy_archive_requirements(
        fighter_kinds, copy_costume_requirements(fighter_kinds, {0}));
}

std::vector<KirbyCopyArchiveRequirement>
kirby_copy_archive_requirements(const MeleeWebMenuMatchSelection& selection)
{
    const auto kinds = selected_copy_kinds(selection);
    return copy_archive_requirements(kinds, copy_costume_requirements(
        kinds, selected_kirby_costumes(selection)));
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
            if (!source.ef_DAT_file || !source.effDataTable_name || !fighter) {
                std::string identity = "Kirby copy effect bank " +
                    std::to_string(bank) + " for FighterKind " +
                    std::to_string(kind) + " has incomplete source identity:";
                if (!source.ef_DAT_file) identity += " archive=null;";
                if (!source.effDataTable_name) identity += " table=null;";
                if (!fighter) identity += " fighter-owner=missing;";
                reject(identity);
            }

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
        std::unique_ptr<NativeDatArena> article_arena;
        std::vector<std::unique_ptr<DatItemArticle>> copy_articles;
        std::unique_ptr<DatNativeJoint> copy_hat_model;
        std::unique_ptr<MeleeWebNativeJoint, decltype(&destroy_joint)>
            native_copy_hat_model{nullptr, &destroy_joint};
        void* copy_hat_joint_descriptor = nullptr;
        std::unique_ptr<DatNativeJoint> costume_model;
        std::unique_ptr<MeleeWebNativeJoint, decltype(&destroy_joint)>
            native_costume_model{nullptr, &destroy_joint};
        std::unique_ptr<DatMaterialAnimation> costume_matanim;
        void* costume_joint_descriptor = nullptr;
        const MeleeWebKirbyCostumeStrings* costume_source = nullptr;
        std::map<std::string, std::uint32_t, std::less<>> public_offsets;
    };
    struct OwnedEffect {
        std::shared_ptr<const DatArchive> archive;
        std::unique_ptr<DatEffectEntries> entries;
    };

    std::vector<KirbyCopyArchiveRequirement> requirements;
    std::vector<CopyCostumeRequirement> costume_requirements;
    std::vector<KirbyCopyEffectRequirement> effect_requirements;
    std::vector<NativeDatSourceRegion> particle_source_regions;
    std::vector<unsigned> kirby_costumes;
    std::map<std::string, OwnedArchive, std::less<>> archives;
    std::vector<OwnedEffect> effects;
    std::vector<MeleeWebArchiveSymbol> symbols;
    MeleeWebArchiveSections* scope = nullptr;
    bool activated = false;

    Storage(const RuntimeFiles& files,
            const MeleeWebMenuMatchSelection& selection)
    {
        if (!contains_kirby(selection)) return;

        kirby_costumes = selected_kirby_costumes(selection);
        if (kirby_costumes.empty())
            reject("Kirby copy assets have no source-selected Kirby costume");

        const auto kinds = selected_copy_kinds(selection);
        costume_requirements = copy_costume_requirements(kinds, kirby_costumes);
        requirements = copy_archive_requirements(kinds, costume_requirements);
        effect_requirements = kirby_copy_effect_requirements(selection);
        const auto body_file = files.find("PlKb.dat");
        if (body_file == files.end()) reject("Kirby body source archive was not imported");
        const DatArchive body(body_file->second, DatExternalPolicy::ResolveNull);
        const auto body_root = public_root(body, "ftDataKirby").data_offset;
        const auto body_parts = body.pointer(body_root + 8U, 8);
        if (!body_parts) reject("Kirby body FtPartsDesc is missing");
        const auto body_model_count = body.be32(*body_parts);
        if (!body_model_count || body_model_count > 11U)
            reject("Kirby body model count exceeds source FtParts bounds");
        if (const auto common_items = files.find("ItCo.usd");
            common_items != files.end()) {
            auto item_archive = std::make_shared<const DatArchive>(
                common_items->second, DatExternalPolicy::PreserveUnresolved);
            particle_source_regions.push_back({gale01r2_itco_data_address,
                                                std::move(item_archive)});
        }

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
            std::vector<std::uint32_t> material_joint_roots;
            for (const auto& root : requirements) {
                if (root.filename != requirement.filename) continue;
                const auto root_offset = owned.public_offsets.at(root.symbol);
                if (requirement.filename == "PlKbCpFc.dat")
                    std::fprintf(stderr,
                        "Kirby copy root symbol=%s offset=0x%x costume=%u first=%u dynamic2=%u target=0x%x\n",
                        root.symbol.c_str(), root_offset, unsigned(root.costume_root),
                        unsigned(checked->has_relocation(root_offset)),
                        unsigned(checked->has_relocation(root_offset + 0x14U)),
                        checked->has_relocation(root_offset + 0x14U)
                            ? checked->be32(root_offset + 0x14U) : 0U);
                bool is_costume_joint = false;
                if (root.costume_root) {
                    is_costume_joint = std::any_of(
                        costume_requirements.begin(), costume_requirements.end(),
                        [&](const CopyCostumeRequirement& costume) {
                            return costume.source->dat_filename == root.filename &&
                                   root.symbol == costume.source->joint_name;
                        });
                    if (!is_costume_joint) continue;
                    material_joint_roots.push_back(root_offset);
                } else if (checked->has_relocation(root_offset)) {
                    // KirbyHatStruct starts with its authored HSD_Joint
                    // pointer. FtPartsDesc-only copy roots do not own a joint
                    // graph at this field and are intentionally not guessed.
                    const auto joint = checked->pointer(root_offset, 64);
                    if (!joint)
                        reject("Kirby copied hat source joint is missing: " +
                               root.symbol);
                    material_joint_roots.push_back(*joint);
                }
                if (!root.costume_root &&
                    checked->has_relocation(root_offset + 0x14U)) {
                    // The original copy-model consumer
                    // ftKb_SpecialN_800EF438 reads hat_dynamics[2] at +0x14
                    // as an HSD_Joint root, including for FtPartsDesc-first
                    // public roots.
                    const auto joint = checked->pointer(root_offset + 0x14U, 64);
                    if (!joint)
                        reject("Kirby copied hat dynamic joint is missing: " +
                               root.symbol);
                    material_joint_roots.push_back(*joint);
                }
            }
            std::sort(material_joint_roots.begin(), material_joint_roots.end());
            material_joint_roots.erase(
                std::unique(material_joint_roots.begin(), material_joint_roots.end()),
                material_joint_roots.end());
            if (requirement.filename == "PlKbCpFc.dat") {
                std::fprintf(stderr, "Kirby copy roots file=%s count=%zu",
                             requirement.filename.c_str(), material_joint_roots.size());
                for (const auto joint : material_joint_roots)
                    std::fprintf(stderr, " 0x%x", joint);
                std::fprintf(stderr, "\n");
            }
            adapt_native_source_material_modes(owned.bytes, checked,
                                               requirement.filename,
                                               material_joint_roots);
            for (const auto& root : requirements)
                if (root.filename == requirement.filename)
                    adapt_kirby_copy_parts_count(owned.bytes, *checked, root,
                                                 kirby_costumes, body_model_count);
            owned.native = std::make_unique<HSD_Archive>();
            std::memset(owned.native.get(), 0, sizeof(HSD_Archive));
            // This is the checked native descriptor owner, not a source
            // lbFile/lbHeap copy. Keep its original HSD relocation separate
            // from lbArchive_InitializeDAT's source-file ownership contract;
            // the source loader later borrows the published typed roots.
            if (HSD_ArchiveParse(owned.native.get(), owned.bytes.data(),
                                 owned.bytes.size()) == -1)
                reject("Original HSD parser rejected Kirby archive " +
                       requirement.filename);
            for (int external = 0;; ++external) {
                const char* symbol = HSD_ArchiveGetExtern(owned.native.get(), external);
                if (!symbol) break;
                HSD_ArchiveLocateExtern(owned.native.get(), symbol, nullptr);
            }
            if (!owned.native->data)
                reject("Original HSD parser did not initialize Kirby archive " +
                       requirement.filename);
            if (requirement.filename == "PlKbCpFc.dat") {
                std::uint32_t adapted_envelopes[3]{};
                std::uint32_t parsed_envelopes[3]{};
                for (std::uint32_t index = 0; index < 3; ++index) {
                    std::memcpy(&adapted_envelopes[index],
                                owned.bytes.data() + 0x20U + 0xc5a0U + index * 4U, 4);
                    std::memcpy(&parsed_envelopes[index],
                                owned.native->data + 0xc5a0U + index * 4U, 4);
                }
                std::fprintf(stderr,
                    "Kirby envelope relocation data=0x%08x adapted=%08x,%08x,%08x parsed=%08x,%08x,%08x expected=%08x\n",
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(owned.native->data)),
                    adapted_envelopes[0], adapted_envelopes[1], adapted_envelopes[2],
                    parsed_envelopes[0], parsed_envelopes[1], parsed_envelopes[2],
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(owned.native->data + 0x9d88U)));
                const auto* mode = reinterpret_cast<const std::uint32_t*>(
                    owned.native->data + 0x9b18U + 4U);
                std::fprintf(stderr, "Kirby parsed base archive data=0x%08x mode_at_9b18=0x%08x\n",
                             static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
                                 owned.native->data)), *mode);
                std::uint32_t adapted_pobj_target = 0;
                std::uint32_t parsed_pobj_target = 0;
                std::memcpy(&adapted_pobj_target,
                            owned.bytes.data() + 0x20U + 0xcfc8U, 4);
                std::memcpy(&parsed_pobj_target,
                            owned.native->data + 0xcfbcU + 12U, 4);
                std::fprintf(stderr,
                    "Kirby DObj pointer source=0x%x adapted=0x%x parsed=0x%x expected=0x%x\n",
                    checked->be32(0xcfc8U), adapted_pobj_target,
                    parsed_pobj_target,
                    static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(
                        owned.native->data + 0xc0acU)));
            }
            for (const auto& root : requirements) {
                if (root.filename != requirement.filename || root.costume_root)
                    continue;
                const auto root_offset = owned.public_offsets.at(root.symbol);
                // KirbyHatStruct starts with its authored HSD_Joint pointer;
                // roots without that relocation are FtPartsDesc-only records.
                // Keep the decoded graph alive and publish a checked native
                // descriptor for the original ftKb_LoadHat consumer.
                if (checked->has_relocation(root_offset)) {
                    const auto joint = checked->pointer(root_offset, 64);
                    if (!joint)
                        reject("Kirby copied hat source joint is missing: " +
                               root.symbol);
                    owned.copy_hat_model =
                        std::make_unique<DatNativeJoint>(checked, *joint);
                    char joint_error[256]{};
                    owned.native_copy_hat_model.reset(
                        melee_web_native_joint_hydrate(
                            &owned.copy_hat_model->graph(), joint_error,
                            sizeof(joint_error)));
                    if (!owned.native_copy_hat_model) reject(joint_error);
                    owned.copy_hat_joint_descriptor =
                        melee_web_native_joint_descriptor(
                            owned.native_copy_hat_model.get(), joint_error,
                            sizeof(joint_error));
                    if (!owned.copy_hat_joint_descriptor) reject(joint_error);
                    static_assert(sizeof(void*) == 4,
                                  "Native Kirby joint descriptors require the gameplay ABI");
                    const auto address = reinterpret_cast<std::uintptr_t>(
                        owned.copy_hat_joint_descriptor);
                    if (address > UINT32_MAX)
                        reject("Kirby copied hat joint exceeds source pointer width");
                    const auto encoded = static_cast<std::uint32_t>(address);
                    std::memcpy(static_cast<std::uint8_t*>(owned.native->data) +
                                    root_offset,
                                &encoded, sizeof(encoded));
                }
                struct CopyArticle { std::uint32_t field; std::uint32_t kind; };
                static constexpr CopyArticle mario_articles[]{
                    // ftKb_SpecialN_800F16D0 reads g->x0->xC directly.
                    {0x0C, It_Kind_Kirby_MarioFire},
                };
                static constexpr CopyArticle luigi_articles[]{
                    {0x0C, It_Kind_Kirby_LuigiFire},
                };
                static constexpr CopyArticle peach_articles[]{
                    {0x0C, It_Kind_Kirby_PeachToad},
                    {0x10, It_Kind_Kirby_PeachToadSpore},
                };
                static constexpr CopyArticle samus_articles[]{
                    // The source registers hats[FTKIND_SAMUS]->
                    // hat_dynamics[0] as Kirby's copied Charge Shot.
                    {0x0C, It_Kind_Kirby_SamusCharge},
                };
                static constexpr CopyArticle gamewatch_articles[]{
                    {0x20, It_Kind_Kirby_GameWatchChef},
                    {0x24, It_Kind_Kirby_GameWatchChefPan},
                };
                static constexpr CopyArticle popo_articles[]{
                    // ftKb_SpecialN_800F16D0 registers
                    // hats[FTKIND_POPO]->hat_dynamics[0].
                    {0x0C, It_Kind_Kirby_IceClimberIce},
                };
                static constexpr CopyArticle fox_articles[]{
                    // ftKb_Init_800EE528 registers the first two Fox
                    // hat_dynamics as Kirby's copied Laser and Blaster.
                    {0x0C, It_Kind_Kirby_FoxLaser},
                    {0x10, It_Kind_Kirby_FoxBlaster},
                };
                static constexpr CopyArticle falco_articles[]{
                    // Falco's source registration selects dynamics rows 3/4.
                    {0x18, It_Kind_Kirby_FalcoLaser},
                    {0x1C, It_Kind_Kirby_FalcoBlaster},
                };
                static constexpr CopyArticle link_articles[]{
                    {0x0C, It_Kind_Kirby_LinkArrow},
                    {0x10, It_Kind_Kirby_LinkBow},
                };
                static constexpr CopyArticle clink_articles[]{
                    {0x0C, It_Kind_Kirby_CLinkArrow},
                    {0x10, It_Kind_Kirby_CLinkBow},
                };
                static constexpr CopyArticle mewtwo_articles[]{
                    {0x18, It_Kind_Kirby_MewtwoShadowBall},
                };
                static constexpr CopyArticle ness_articles[]{
                    {0x0C, It_Kind_Kirby_NessPKFlush},
                    {0x10, It_Kind_Kirby_NessPKFlush_Explode},
                };
                static constexpr CopyArticle pikachu_articles[]{
                    {0x0C, It_Kind_Kirby_PikachuTJolt_Ground},
                    {0x10, It_Kind_Kirby_PikachuTJolt_Air},
                };
                static constexpr CopyArticle pichu_articles[]{
                    {0x0C, It_Kind_Kirby_PichuTJolt_Ground},
                    {0x10, It_Kind_Kirby_PichuTJolt_Air},
                };
                static constexpr CopyArticle koopa_articles[]{
                    {0x0C, It_Kind_Kirby_KoopaFlame},
                };
                static constexpr CopyArticle sheik_articles[]{
                    {0x0C, It_Kind_Kirby_SeakNeedleThrow},
                    {0x10, It_Kind_Kirby_SeakNeedleHeld},
                };
                static constexpr CopyArticle drmario_articles[]{
                    {0x0C, It_Kind_Kirby_DrMarioVitamin},
                };
                static constexpr CopyArticle yoshi_articles[]{
                    {0x20, It_Kind_Kirby_YoshiEggLay},
                };
                const CopyArticle* copy_articles = nullptr;
                std::size_t copy_article_count = 0;
                if (root.fighter_kind == FTKIND_MARIO &&
                    requirement.filename == "PlKbCpMr.dat" &&
                    root.symbol == "ftDataKirbyCopyMario") {
                    copy_articles = mario_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_LUIGI &&
                           requirement.filename == "PlKbCpLg.dat" &&
                           root.symbol == "ftDataKirbyCopyLuigi") {
                    copy_articles = luigi_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_PEACH &&
                           requirement.filename == "PlKbCpPe.dat" &&
                           root.symbol == "ftDataKirbyCopyPeach") {
                    copy_articles = peach_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_SAMUS &&
                           requirement.filename == "PlKbCpSs.dat" &&
                           root.symbol == "ftDataKirbyCopySamus") {
                    copy_articles = samus_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_GAMEWATCH &&
                    requirement.filename == "PlKbCpGw.dat" &&
                    root.symbol == "ftDataKirbyCopyGamewatch") {
                    copy_articles = gamewatch_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_POPO &&
                           requirement.filename == "PlKbCpPp.dat" &&
                           root.symbol == "ftDataKirbyCopyPopo") {
                    copy_articles = popo_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_FOX &&
                           requirement.filename == "PlKbCpFx.dat" &&
                           root.symbol == "ftDataKirbyCopyFox") {
                    copy_articles = fox_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_FALCO &&
                           requirement.filename == "PlKbCpFc.dat" &&
                           root.symbol == "ftDataKirbyCopyFalco") {
                    copy_articles = falco_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_LINK &&
                           requirement.filename == "PlKbCpLk.dat" &&
                           root.symbol == "ftDataKirbyCopyLink") {
                    copy_articles = link_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_CLINK &&
                           requirement.filename == "PlKbCpCl.dat" &&
                           root.symbol == "ftDataKirbyCopyClink") {
                    copy_articles = clink_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_MEWTWO &&
                           requirement.filename == "PlKbCpMt.dat" &&
                           root.symbol == "ftDataKirbyCopyMewtwo") {
                    copy_articles = mewtwo_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_NESS &&
                           requirement.filename == "PlKbCpNs.dat" &&
                           root.symbol == "ftDataKirbyCopyNess") {
                    copy_articles = ness_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_PIKACHU &&
                           requirement.filename == "PlKbCpPk.dat" &&
                           root.symbol == "ftDataKirbyCopyPikachu") {
                    copy_articles = pikachu_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_PICHU &&
                           requirement.filename == "PlKbCpPc.dat" &&
                           root.symbol == "ftDataKirbyCopyPichu") {
                    copy_articles = pichu_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_KOOPA &&
                           requirement.filename == "PlKbCpKp.dat" &&
                           root.symbol == "ftDataKirbyCopyKoopa") {
                    copy_articles = koopa_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_SEAK &&
                           requirement.filename == "PlKbCpSk.dat" &&
                           root.symbol == "ftDataKirbyCopySeak") {
                    copy_articles = sheik_articles;
                    copy_article_count = 2;
                } else if (root.fighter_kind == FTKIND_DRMARIO &&
                           requirement.filename == "PlKbCpDr.dat" &&
                           root.symbol == "ftDataKirbyCopyDrmario") {
                    copy_articles = drmario_articles;
                    copy_article_count = 1;
                } else if (root.fighter_kind == FTKIND_YOSHI &&
                           requirement.filename == "PlKbCpYs.dat" &&
                           root.symbol == "ftDataKirbyCopyYoshi") {
                    copy_articles = yoshi_articles;
                    copy_article_count = 1;
                } else {
                    continue;
                }
                owned.article_arena = std::make_unique<NativeDatArena>(checked);
                static_assert(sizeof(void*) == 4,
                              "Native copy Article pointers require the gameplay ABI");
                for (std::size_t article_index = 0;
                     article_index < copy_article_count; ++article_index) {
                    const auto [field_offset, kind] = copy_articles[article_index];
                    const auto article_root = checked->pointer(
                        root_offset + field_offset, 24);
                    if (!article_root)
                        reject("Kirby copied Article root is missing at source field +" +
                               std::to_string(field_offset));
                    std::uint32_t unresolved = 0;
                    void* registered = melee_web_article_decode(
                        owned.article_arena->reader(), *article_root,
                        &unresolved);
                    if (!registered)
                        reject("Kirby copied Article registration root could not be decoded");
                    owned.copy_articles.push_back(std::make_unique<DatItemArticle>(
                        checked, *article_root, kind, registered));
                    if (melee_web_article_unresolved(registered) != 0)
                        reject("Kirby copied Article did not publish a complete source graph");
                    const auto address = reinterpret_cast<std::uintptr_t>(registered);
                    if (address > UINT32_MAX)
                        reject("Kirby copied Article exceeds the source pointer width");
                    const auto encoded = static_cast<std::uint32_t>(address);
                    std::memcpy(static_cast<std::uint8_t*>(owned.native->data) +
                                    root_offset + field_offset,
                                &encoded, sizeof(encoded));
                    (void)unresolved;
                }
            }
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
            try {
                owned.entries = std::make_unique<DatEffectEntries>(
                    owned.archive, requirement.symbol, requirement.effect_bank,
                    entry_count, true, particle_source_regions);
            } catch(const DatError& error) {
                throw DatError("Kirby donor effect archive "+requirement.filename+
                    " table "+requirement.symbol+": "+error.what());
            }
            effects.push_back(std::move(owned));
        }

        for (const auto& costume : costume_requirements) {
            const auto& source = *costume.source;
            auto& archive = archives.at(source.dat_filename);
            if (archive.costume_source) {
                const auto& previous = *archive.costume_source;
                if (std::strcmp(previous.joint_name, source.joint_name) != 0 ||
                    std::string(previous.matanim_joint_name ? previous.matanim_joint_name : "") !=
                    std::string(source.matanim_joint_name ? source.matanim_joint_name : ""))
                    reject("Kirby donor archive owns multiple costume graphs");
                continue; // Several source cache rows may borrow one graph.
            }
            archive.costume_source = &source;
            archive.costume_model = std::make_unique<DatNativeJoint>(
                archive.checked, archive.public_offsets.at(source.joint_name));
            char error[256]{};
            archive.native_costume_model.reset(melee_web_native_joint_hydrate(
                &archive.costume_model->graph(), error, sizeof(error)));
            if (!archive.native_costume_model) reject(error);
            archive.costume_joint_descriptor = melee_web_native_joint_descriptor(
                archive.native_costume_model.get(), error, sizeof(error));
            if (!archive.costume_joint_descriptor) reject(error);
            if (source.matanim_joint_name) {
                archive.costume_matanim =
                    std::make_unique<DatMaterialAnimation>(
                        archive.checked,
                        archive.public_offsets.at(source.matanim_joint_name),
                        archive.costume_model->graph());
            }
        }

        symbols.reserve(requirements.size());
        for (const auto& requirement : requirements) {
            auto& archive = archives.at(requirement.filename);
            void* native_data = archive.native->data +
                                archive.public_offsets.at(requirement.symbol);
            if (requirement.costume_root) {
                const auto* costume = archive.costume_source;
                if (costume->joint_name == requirement.symbol)
                    native_data = archive.costume_joint_descriptor;
                else if (costume->matanim_joint_name == requirement.symbol)
                    native_data = archive.costume_matanim->descriptor();
                else
                    reject("Kirby costume root differs from its checked graph owner");
            } else if (archive.copy_hat_joint_descriptor) {
                // The checked owner replaced KirbyHatStruct::hat_joint in the
                // mutable native HSD image; its public root remains the
                // original source structure and the owner outlives the cache.
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
        // The per-kind callback above only loads row zero. Complete the
        // source scene's selected-color preload through the original loader;
        // do not alias cache rows by filename or change acquisition indexing.
        for (const auto& costume : costume_requirements)
            if (costume.costume_id != 0)
                ftKb_SpecialN_800EED50(costume.fighter_kind, costume.costume_id);

        for (auto& effect : effects) {
            char error[256]{};
            if (!effect.entries->verify_source_load(error, sizeof(error)))
                reject(error);
        }

        for (const auto& requirement : requirements) {
            if (requirement.costume_root) continue;
            auto& archive = archives.at(requirement.filename);
            void* expected = archive.native->data +
                             archive.public_offsets.at(requirement.symbol);
            void* actual = reinterpret_cast<void**>(&ft_80459B88)[
                requirement.fighter_kind];
            if (actual != expected)
                reject("Original Kirby preload did not publish " +
                       requirement.symbol);
        }
        for (const auto& costume : costume_requirements) {
            const auto* cached = ftKb_Init_803C9FC8[costume.fighter_kind];
            if (!cached) reject("Original Kirby costume root owner is absent");
            const auto& archive = archives.at(costume.source->dat_filename);
            if (cached[costume.costume_id].joint != archive.costume_joint_descriptor ||
                cached[costume.costume_id].matanim !=
                    (archive.costume_matanim ? archive.costume_matanim->descriptor() : nullptr))
                reject("Original Kirby preload did not publish selected costume " +
                       std::to_string(costume.costume_id) + " of " +
                       costume.source->dat_filename);
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
        costume_requirements.clear();
        effect_requirements.clear();
        kirby_costumes.clear();
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
