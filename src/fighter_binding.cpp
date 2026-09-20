#include "fighter_binding.hpp"
#include "fighter_binding.h"

#include <algorithm>
#include <numeric>
#include <set>

namespace melee_web {
namespace {
#include "fighter_registry.inc"

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}
void region(const DatArchive& archive, std::uint32_t offset, std::size_t size)
{
    require(offset % 4 == 0, "Fighter descriptor is unaligned");
    (void) archive.range(offset, size);
    require(size <= archive.next_target_offset(offset) - offset,
            "Fighter descriptor crosses a referenced region");
}
std::uint32_t pointer(const DatArchive& archive, std::uint32_t slot, std::size_t size)
{
    const auto value = archive.pointer(slot, size);
    require(value.has_value(), "Required fighter metadata pointer is null");
    return *value;
}
std::uint32_t symbol_root(const DatArchive& archive, std::string_view name)
{
    const auto& symbols = archive.public_symbols();
    const auto found = std::find_if(symbols.begin(), symbols.end(),
                                  [&](const auto& symbol) { return symbol.name == name; });
    require(found != symbols.end(), "Required fighter metadata public symbol is missing");
    return found->data_offset;
}
std::string string_at(const DatArchive& archive, std::uint32_t offset)
{
    const auto bound = std::min<std::size_t>(4096, archive.next_target_offset(offset) - offset);
    const auto bytes = archive.range(offset, bound);
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    require(end != bytes.end() && end != bytes.begin(), "Fighter action symbol is empty or unterminated");
    for (auto cursor = bytes.begin(); cursor != end; ++cursor)
        require(*cursor >= 32 && *cursor <= 126, "Fighter action symbol contains unsupported characters");
    return {bytes.begin(), end};
}
void validate_costume(const FighterCostume& costume)
{
    const auto match = std::find_if(std::begin(source_costumes), std::end(source_costumes), [&](const auto& entry) {
        return entry.fighter_kind == costume.fighter_kind && entry.costume_index == costume.costume_index &&
               entry.motion_count == costume.motion_count && entry.model_symbol == costume.model_symbol &&
               entry.fighter_symbol == costume.fighter_symbol && entry.animation_filename == costume.animation_filename;
    });
    require(match != std::end(source_costumes), "Fighter costume identity is not in the source registry");
}
void check_container_range(const DatFighterAction& action, std::size_t size)
{
    require(action.container_offset <= size && action.archive_bytes <= size - action.container_offset,
            "Fighter animation container is truncated or does not contain every declared action");
}
} // namespace

std::span<const FighterCostume> fighter_costumes() noexcept { return source_costumes; }
std::uint32_t fighter_kind_count() noexcept { return source_fighter_kind_count; }

const FighterCostume& resolve_fighter_costume(std::string_view model_symbol)
{
    const FighterCostume* result = nullptr;
    for (const auto& costume : source_costumes) {
        if (costume.model_symbol != model_symbol) continue;
        require(result == nullptr, "Model symbol has ambiguous source costume identity; explicit costume selection is required");
        result = &costume;
    }
    require(result != nullptr, "Model symbol is not a fighter costume in the source registry");
    return *result;
}

DatCommonFighterLayout::DatCommonFighterLayout(const DatArchive& archive, const FighterCostume& costume)
    : fighter_kind(costume.fighter_kind)
{
    validate_costume(costume);
    const auto root = symbol_root(archive, "ftLoadCommonData");
    region(archive, root, 24);
    const auto parts = pointer(archive, root + 16, source_fighter_kind_count * 4);
    const auto alternatives = pointer(archive, root + 20, source_fighter_kind_count * 4);
    region(archive, parts, source_fighter_kind_count * 4);
    region(archive, alternatives, source_fighter_kind_count * 4);
    descriptor_offset = pointer(archive, parts + fighter_kind * 4, 12);
    region(archive, descriptor_offset, 12);
    part_count = archive.be32(descriptor_offset + 8);
    require(part_count > 0 && part_count <= 140, "Fighter part count exceeds the source capacity");
    (void) pointer(archive, descriptor_offset, part_count); // named lookup, not attachment order
    (void) pointer(archive, descriptor_offset + 4, 1);
    const auto alternate = archive.pointer(alternatives + fighter_kind * 4, 8);
    has_alternate_descriptor = alternate.has_value();
    if (!alternate) return;
    region(archive, *alternate, 8);
    const auto count = archive.be32(*alternate + 4);
    require(count <= 32, "Alternate fighter part count exceeds the source bit mask");
    const auto records = archive.pointer(*alternate, count ? count * 4 : 1);
    require(!count || records.has_value(), "Alternate fighter part records are missing");
    if (count) region(archive, *records, count * 4);
    std::set<std::uint8_t> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        const auto bytes = archive.range(*records + i * 4, 4);
        require(bytes[0] < part_count && bytes[1] < part_count && bytes[2] <= 3 &&
                    (bytes[3] == 255 || bytes[3] < part_count) && seen.insert(bytes[0]).second,
                "Alternate fighter part record is invalid");
        alternate_parts.push_back({bytes[0], bytes[1], bytes[2], bytes[3]});
    }
}

DatFighterActions::DatFighterActions(const DatArchive& archive, const FighterCostume& costume)
    : fighter_kind(costume.fighter_kind)
{
    validate_costume(costume);
    const auto root = symbol_root(archive, costume.fighter_symbol);
    region(archive, root, 16);
    const auto table = pointer(archive, root + 12, std::size_t(costume.motion_count) * 24);
    region(archive, table, std::size_t(costume.motion_count) * 24);
    for (std::uint32_t id = 0; id < costume.motion_count; ++id) {
        const auto offset = table + id * 24;
        const auto name = archive.pointer(offset);
        const auto start = archive.be32(offset + 4), size = archive.be32(offset + 8);
        (void) archive.pointer(offset + 12); // Command bytes are not interpreted here.
        require(archive.be32(offset + 20) == 0 && !archive.pointer(offset + 20),
                "Fighter action table contains a runtime-resolved address");
        if (!size) {
            require(!name && start == 0, "Empty fighter motion has inconsistent archive metadata");
            continue;
        }
        require(name.has_value(), "Nonempty fighter motion has no public symbol");
        require(size >= 32 && size <= 0x8000 && start % 32 == 0 &&
                    start <= DatArchive::max_archive_bytes && size <= DatArchive::max_archive_bytes - start,
                "Fighter motion archive range exceeds source or inspection limits");
        actions.push_back({id, start, size, archive.be32(offset + 16), string_at(archive, *name)});
    }
    require(!actions.empty(), "Fighter data has no nonempty motion archives");
}

bool DatFighterActions::contains(std::string_view name) const noexcept
{
    return std::any_of(actions.begin(), actions.end(), [&](const auto& action) { return action.symbol == name; });
}
const DatFighterAction& DatFighterActions::find(std::string_view name, std::size_t size) const
{
    const DatFighterAction* result = nullptr;
    for (const auto& action : actions) {
        if (action.symbol != name) continue;
        require(action.archive_bytes == size, "Animation archive length differs from the fighter action table");
        if (result) require(result->container_offset == action.container_offset && result->archive_bytes == action.archive_bytes,
                            "Fighter action symbol resolves to conflicting archives");
        else result = &action;
    }
    require(result != nullptr, "Animation public symbol does not belong to the selected fighter action table");
    return *result;
}
void DatFighterActions::validate_container(std::span<const std::uint8_t> container) const
{
    require(!container.empty() && container.size() <= DatArchive::max_archive_bytes,
            "Animation container exceeds the inspection size limit");
    for (const auto& action : actions) check_container_range(action, container.size());
}
std::span<const std::uint8_t> DatFighterActions::slice(std::span<const std::uint8_t> container, std::uint32_t motion_id) const
{
    require(!container.empty() && container.size() <= DatArchive::max_archive_bytes,
            "Animation container exceeds the inspection size limit");
    const auto action = std::find_if(actions.begin(), actions.end(), [&](const auto& item) { return item.motion_id == motion_id; });
    require(action != actions.end(), "Selected motion ID has no animation archive");
    check_container_range(*action, container.size());
    return container.subspan(action->container_offset, action->archive_bytes);
}

FighterAnimationBinding bind_fighter_animation(
    const FighterCostume& costume, const DatCommonFighterLayout& common, const DatFighterActions& actions,
    std::string_view model_symbol, std::size_t joint_count, std::string_view animation_symbol,
    const DatAnimation& animation, std::size_t archive_bytes)
{
    validate_costume(costume);
    require(costume.model_symbol == model_symbol && costume.fighter_kind == common.fighter_kind &&
                costume.fighter_kind == actions.fighter_kind, "Fighter binding mixes unrelated model or metadata identities");
    require(!common.has_alternate_descriptor, "Alternate fighter parts require original insertion and motion-mask binding");
    require(common.part_count == joint_count && joint_count == animation.node_counts.size(),
            "Fighter common part, model joint and animation node counts differ");
    const auto& action = actions.find(animation_symbol, archive_bytes);
    // ftParts_SetupParts builds child-before-sibling parts. Its only static
    // holes are the alternate slots found by ftParts_8007506C; those are rejected
    // above. ftParts_80074194 marks each real joint present, then
    // ftAnim_8006F4C8 consumes one FigaTree node per present part. Thus ordinary
    // initial parts use preorder directly; the named-bone lookup bytes are not
    // an animation remap. Gameplay part suppression is outside this viewer.
    FighterAnimationBinding binding{costume.fighter_kind, costume.costume_index, action.motion_id, {}};
    binding.animation_node_to_model_joint.resize(joint_count);
    std::iota(binding.animation_node_to_model_joint.begin(), binding.animation_node_to_model_joint.end(), 0U);
    return binding;
}
} // namespace melee_web

extern "C" int melee_web_fighter_costume_material_required(uint32_t kind,uint32_t costume)
{
    for(const auto& row:melee_web::fighter_costumes())
        if(row.fighter_kind==kind && row.costume_index==costume)
            return !row.material_animation_symbol.empty();
    return -1;
}
