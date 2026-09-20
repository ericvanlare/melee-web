#pragma once

#include "fighter_attributes.h"
#include "fighter_binding.hpp"
#include "gameplay_pikachu_schema.h"
#include <array>
#include <memory>

namespace melee_web {

struct DatRuntimeAction {
    std::uint32_t motion_id, descriptor_offset, motion_flags;
    std::uint32_t container_offset = 0, archive_bytes = 0;
    std::string symbol;
    std::array<std::uint8_t, 2> blend_dynamics{};
    std::optional<std::uint32_t> command_offset;
};
struct DatWaitChoice { std::uint32_t motion_id, weight; };

struct DatFighterHurtbox {
    std::uint32_t descriptor_offset, bone_index, height, is_grabbable;
    std::array<float, 3> a_offset, b_offset;
    float scale;
};
struct DatFighterDynamicsBone {
    std::uint32_t descriptor_offset, bone_index;
    std::array<float, 3> position;
    // lb_80011710 reads contiguous 0x3c scalar records from the serialized
    // lb_00F9_UnkDesc1Inner array, not native DynamicsData linked-list nodes.
    std::vector<std::array<float, 15>> parameters;
};
struct DatFighterDynamicsSphere {
    std::uint32_t descriptor_offset, bone_index;
    std::array<float, 3> offset;
    float size;
};
struct DatFighterDynamics {
    std::uint32_t descriptor_offset;
    std::vector<DatFighterDynamicsBone> bones;
    std::vector<DatFighterDynamicsSphere> spheres;
    // Mars uses this target as five rows of three pointer-width integer chain
    // cutoffs. Other fighter-specific table schemas remain unadmitted.
    std::optional<std::uint32_t> animation_table_offset;
};

// Owned packed command storage, deliberately NOT a native CmdUnion graph.
// Words remain canonical big-endian numeric values; relocations are body offsets
// into the retained archive. Region bounds are conservative referenced bounds,
// not an instruction count or proof that a complete script is supported.
class DatPackedCommands {
public:
    [[nodiscard]] std::uint32_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const;
    [[nodiscard]] std::uint32_t word(std::size_t index) const;
    [[nodiscard]] std::optional<std::uint32_t> relocated_target(std::size_t index) const;
    static constexpr bool native_execution_ready = false;
private:
    friend class DatFighterRuntime;
    DatPackedCommands(std::shared_ptr<const DatArchive>, std::uint32_t);
    std::shared_ptr<const DatArchive> archive_;
    std::uint32_t offset_, byte_count_;
};

// Initial fighter-data batch. These typed fields do not constitute a complete
// native ftData: article, dynamics, hurtbox, parts and other graphs must still be
// hydrated before Fighter_Create. Source motion rows retain even empty clips.
class DatFighterRuntime {
public:
    DatFighterRuntime(std::shared_ptr<const DatArchive>, const FighterCostume&);
    [[nodiscard]] const FighterCostume& costume() const noexcept { return *costume_; }
    [[nodiscard]] std::uint32_t root_offset() const noexcept { return root_; }
    [[nodiscard]] const MeleeWebFighterBaseAttributes& base_attributes() const noexcept { return base_; }
    [[nodiscard]] const std::optional<MeleeWebMarioAttributes>& mario_attributes() const noexcept { return mario_; }
    [[nodiscard]] const std::optional<MeleeWebLuigiAttributes>& luigi_attributes() const noexcept { return luigi_; }
    // Pikachu and Pichu use the shared original ftPikachuAttributes ABI;
    // their decoded values and Article identities remain family-specific.
    [[nodiscard]] const std::optional<MeleeWebPikachuAttributes>& pikachu_attributes() const noexcept { return pikachu_; }
    // Captain and Ganondorf use the shared original ftCaptain_DatAttrs layout.
    [[nodiscard]] const std::optional<MeleeWebCaptainAttributes>& captain_attributes() const noexcept { return captain_; }
    // Fox and Falco use the shared original ftFox_DatAttrs layout.  The
    // optional is keyed by source kind; it is absent for Mario and for kinds
    // whose extension schema has not been hydrated.
    [[nodiscard]] const std::optional<MeleeWebFoxAttributes>& fox_attributes() const noexcept { return fox_; }
    [[nodiscard]] const std::optional<MeleeWebMarsAttributes>& mars_attributes() const noexcept { return mars_; }
    [[nodiscard]] const std::optional<MeleeWebLinkAttributes>& link_attributes() const noexcept { return link_; }
    [[nodiscard]] std::uint32_t extension_offset() const noexcept { return extension_; }
    [[nodiscard]] const std::vector<DatRuntimeAction>& actions() const noexcept { return actions_; }
    [[nodiscard]] const DatRuntimeAction& action(std::uint32_t motion_id) const;
    [[nodiscard]] const std::vector<DatWaitChoice>& wait_choices() const noexcept { return wait_choices_; }
    [[nodiscard]] const std::vector<DatFighterHurtbox>& hurtboxes() const noexcept { return hurtboxes_; }
    [[nodiscard]] const DatFighterDynamics& dynamics() const noexcept { return dynamics_; }
    // Checks source part indices before a consumer binds any native JObj. Bone
    // dynamics additionally require actual child-chain/pool checks at hydration.
    void validate_part_indices(std::size_t part_count) const;
    [[nodiscard]] std::optional<DatPackedCommands> commands(std::uint32_t motion_id) const;
    [[nodiscard]] const DatFighterActions& archive_actions() const noexcept { return archive_actions_; }
private:
    std::shared_ptr<const DatArchive> archive_;
    const FighterCostume* costume_;
    DatFighterActions archive_actions_;
    std::uint32_t root_, extension_;
    MeleeWebFighterBaseAttributes base_{};
    std::optional<MeleeWebMarioAttributes> mario_;
    std::optional<MeleeWebLuigiAttributes> luigi_;
    std::optional<MeleeWebPikachuAttributes> pikachu_;
    std::optional<MeleeWebCaptainAttributes> captain_;
    std::optional<MeleeWebFoxAttributes> fox_;
    std::optional<MeleeWebMarsAttributes> mars_;
    std::optional<MeleeWebLinkAttributes> link_;
    std::vector<DatRuntimeAction> actions_;
    std::vector<DatWaitChoice> wait_choices_;
    std::vector<DatFighterHurtbox> hurtboxes_;
    DatFighterDynamics dynamics_{};
};

struct DatSelectedAction {
    DatRuntimeAction action; // exact selected row, never the first name alias
    std::shared_ptr<const DatAnimation> animation; // null for an empty source row
    std::optional<DatPackedCommands> commands;
};

// Explicit owned CPU storage replaces console-address classification at this
// boundary. It does not route ftData_80085CD8/80085E50, emulate ARAM completion,
// or publish native FigaTree pointers. Two resident decoded clips bound cache
// ownership; a returned selection independently retains its data across loads.
class DatFighterAnimationStore {
public:
    DatFighterAnimationStore(std::shared_ptr<const DatFighterRuntime>, std::span<const std::uint8_t> container);
    [[nodiscard]] DatSelectedAction select(std::uint32_t motion_id);
    // Source fighter action hydration admits HSD_A_J_NODE visibility tracks;
    // generic inspection selection remains ordinary-SRT-only.
    [[nodiscard]] DatSelectedAction select_native_action(std::uint32_t motion_id);
private:
    struct Entry {
        std::uint32_t offset = 0, size = 0;
        std::string symbol;
        bool native_action = false;
        std::shared_ptr<const DatAnimation> animation;
    };
    [[nodiscard]] DatSelectedAction select_impl(std::uint32_t motion_id, DatAnimationPolicy policy);
    std::shared_ptr<const DatFighterRuntime> fighter_;
    std::vector<std::uint8_t> container_;
    std::array<Entry, 2> cache_;
    std::size_t next_ = 0;
};

} // namespace melee_web
