#pragma once

#include "dat_animation.hpp"
#include <string_view>

namespace melee_web {

// Generated from the pinned original source registries and costume initializers.
// Strings have static lifetime. Equal model symbols can be ambiguous (e.g. color
// variants sharing geometry); automatic resolution rejects that ambiguity.
struct FighterCostume {
    std::uint32_t fighter_kind, costume_index, motion_count;
    std::string_view kind_name, fighter_filename, fighter_symbol;
    std::string_view animation_filename, model_filename, model_symbol;
};
[[nodiscard]] std::span<const FighterCostume> fighter_costumes() noexcept;
[[nodiscard]] std::uint32_t fighter_kind_count() noexcept;
[[nodiscard]] const FighterCostume& resolve_fighter_costume(std::string_view model_symbol);

struct DatAlternatePart {
    std::uint8_t part_slot, parent_part, insertion_mode, source_joint;
};
struct DatCommonFighterLayout {
    std::uint32_t fighter_kind, descriptor_offset, part_count;
    bool has_alternate_descriptor = false;
    std::vector<DatAlternatePart> alternate_parts;
    DatCommonFighterLayout(const DatArchive& common, const FighterCostume& costume);
};

struct DatFighterAction {
    std::uint32_t motion_id, container_offset, archive_bytes, motion_flags;
    std::string symbol;
};
class DatFighterActions {
public:
    std::uint32_t fighter_kind;
    std::vector<DatFighterAction> actions;
    DatFighterActions(const DatArchive& fighter_data, const FighterCostume& costume);
    [[nodiscard]] bool contains(std::string_view symbol) const noexcept;
    // Aliased motion records may share the same archive/name. Conflicting
    // offset/length aliases are rejected; the first source motion id is returned.
    [[nodiscard]] const DatFighterAction& find(std::string_view symbol, std::size_t archive_bytes) const;
    // The actions vector contains only nonempty archives, retaining the source
    // motion IDs. Container checks use every active range, including aliases.
    void validate_container(std::span<const std::uint8_t> container) const;
    [[nodiscard]] std::span<const std::uint8_t>
    slice(std::span<const std::uint8_t> container, std::uint32_t motion_id) const;
};

struct FighterAnimationBinding {
    std::uint32_t fighter_kind, costume_index, motion_id;
    std::vector<std::uint32_t> animation_node_to_model_joint;
};
// Generic ordinary-part gate. Requires source costume identity, common layout,
// exact action membership and archive size, plus matching part/model/node count.
// Alternate insertion and gameplay suppression flags remain unsupported.
[[nodiscard]] FighterAnimationBinding bind_fighter_animation(
    const FighterCostume&, const DatCommonFighterLayout&, const DatFighterActions&,
    std::string_view model_symbol, std::size_t model_joint_count,
    std::string_view animation_symbol, const DatAnimation&, std::size_t archive_bytes);

} // namespace melee_web
