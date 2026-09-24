#include "dat_fighter_runtime.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace melee_web {
namespace {
void require(bool value, const char* message) { if (!value) throw DatError(message); }
const DatArchive& checked_archive(const std::shared_ptr<const DatArchive>& archive)
{
    require(bool(archive), "Fighter runtime archive is null");
    return *archive;
}
const FighterCostume* canonical_costume(const FighterCostume& value)
{
    for (const auto& entry : fighter_costumes()) {
        if (entry.fighter_kind == value.fighter_kind && entry.costume_index == value.costume_index &&
            entry.motion_count == value.motion_count && entry.kind_name == value.kind_name &&
            entry.fighter_filename == value.fighter_filename && entry.fighter_symbol == value.fighter_symbol &&
            entry.animation_filename == value.animation_filename && entry.model_filename == value.model_filename &&
            entry.model_symbol == value.model_symbol && entry.material_animation_symbol == value.material_animation_symbol)
            return &entry;
    }
    throw DatError("Fighter runtime identity differs from the source registry");
}
void region(const DatArchive& archive, std::uint32_t offset, std::size_t bytes, bool aligned = true)
{
    require(!aligned || offset % 4 == 0, "Fighter runtime descriptor is unaligned");
    (void) archive.range(offset, bytes);
    require(bytes <= archive.next_target_offset(offset) - offset,
            "Fighter runtime descriptor crosses a referenced region");
}
std::uint32_t pointer(const DatArchive& archive, std::uint32_t slot, std::size_t bytes)
{
    const auto result = archive.pointer(slot, bytes);
    require(result.has_value(), "Required fighter runtime pointer is null");
    return *result;
}
std::uint32_t scalar(const DatArchive& archive, std::uint32_t at)
{
    require(!archive.has_relocation(at), "Fighter scalar unexpectedly contains a relocation");
    return archive.be32(at);
}
std::uint32_t read_U32(const DatArchive& archive, std::uint32_t at) { return scalar(archive, at); }
std::uint32_t read_PTR32(const DatArchive& archive, std::uint32_t at) { return read_U32(archive, at); }
std::int32_t read_I32(const DatArchive& archive, std::uint32_t at)
{
    return std::bit_cast<std::int32_t>(scalar(archive, at));
}
std::int32_t read_ITEM(const DatArchive& archive, std::uint32_t at) { return read_I32(archive, at); }
float read_F32(const DatArchive& archive, std::uint32_t at)
{
    const float value = std::bit_cast<float>(scalar(archive, at));
    require(std::isfinite(value), "Fighter attribute is nonfinite");
    return value;
}
std::uint8_t read_U8(const DatArchive& archive, std::uint32_t at)
{
    require(!archive.has_relocation(at & ~UINT32_C(3)),
            "Fighter byte field unexpectedly occupies a relocation word");
    return archive.range(at, 1)[0];
}
std::uint32_t root(const DatArchive& archive, std::string_view symbol)
{
    for (const auto& entry : archive.public_symbols()) if (entry.name == symbol) return entry.data_offset;
    throw DatError("Fighter runtime public symbol is missing");
}
}

DatPackedCommands::DatPackedCommands(std::shared_ptr<const DatArchive> archive, std::uint32_t offset)
    : archive_(std::move(archive)), offset_(offset), byte_count_(archive_->next_target_offset(offset) - offset)
{
    region(*archive_, offset, 4);
}
std::span<const std::uint8_t> DatPackedCommands::bytes() const { return archive_->range(offset_, byte_count_); }
std::uint32_t DatPackedCommands::word(std::size_t index) const
{
    require(index < byte_count_ / 4, "Packed command word exceeds its referenced region");
    return archive_->be32(offset_ + std::uint32_t(index * 4));
}
std::optional<std::uint32_t> DatPackedCommands::relocated_target(std::size_t index) const
{
    (void) word(index);
    const auto slot = offset_ + std::uint32_t(index * 4);
    return archive_->has_relocation(slot) ? archive_->pointer(slot) : std::nullopt;
}

DatFighterRuntime::DatFighterRuntime(std::shared_ptr<const DatArchive> archive, const FighterCostume& costume)
    : archive_(std::move(archive)), costume_(canonical_costume(costume)),
      archive_actions_(checked_archive(archive_), *costume_)
{
    const auto& data = *archive_;
    root_ = root(data, costume_->fighter_symbol);
    region(data, root_, 0x60);
    auto offset = pointer(data, root_, 0x184);
    region(data, offset, 0x184);
#define READ_CO(at, type, name, original) base_.co.name = read_##type(data, offset + at);
    MELEE_WEB_CO_ATTRIBUTE_FIELDS(READ_CO)
#undef READ_CO
    offset = pointer(data, root_ + 0x40, 0x30);
    region(data, offset, 0x30);
#define READ_PICKUP(at, type, name, original) base_.pickup.name = read_##type(data, offset + at);
    MELEE_WEB_PICKUP_ATTRIBUTE_FIELDS(READ_PICKUP)
#undef READ_PICKUP
    offset = pointer(data, root_ + 0x50, 8);
    region(data, offset, 8);
    base_.x2c4_x = read_F32(data, offset);
    base_.x2c4_y = read_F32(data, offset + 4);
    extension_ = pointer(data, root_ + 4, 1);
    // Kind-specific schema, not a filename exception. Other kinds retain only
    // the checked extension identity; they are not native-extension-ready.
    if (costume_->fighter_kind == 0 || costume_->fighter_kind == 21) {
        region(data, extension_, 0x84);
        mario_.emplace();
#define READ_MARIO(at, type, name, original) mario_->name = read_##type(data, extension_ + at);
        MELEE_WEB_MARIO_ATTRIBUTE_FIELDS(READ_MARIO)
#undef READ_MARIO
    } else if (costume_->fighter_kind == 3) {
        // Donkey keeps a distinct 0x74 source extension. Its signed motion
        // state/counter words must not be read through a float-only schema.
        region(data, extension_, MELEE_WEB_DONKEY_ATTRIBUTE_BYTES);
        donkey_.emplace();
#define READ_DONKEY(at, type, name, original) donkey_->name = read_##type(data, extension_ + at);
        MELEE_WEB_DONKEY_ATTRIBUTE_FIELDS(READ_DONKEY)
#undef READ_DONKEY
    } else if (costume_->fighter_kind == 5) {
        // Koopa's source extension is a distinct 0xa0 ABI. Preserve x4/x20
        // as signed words and x2c/unk50 as unsigned words.
        region(data, extension_, MELEE_WEB_KOOPA_ATTRIBUTE_BYTES);
        koopa_.emplace();
#define READ_KOOPA(at, type, name, original) koopa_->name = read_##type(data, extension_ + at);
        MELEE_WEB_KOOPA_ATTRIBUTE_FIELDS(READ_KOOPA)
#undef READ_KOOPA
    } else if (costume_->fighter_kind == 16) {
        // Mewtwo's source extension is a distinct 0x88 ABI. Preserve the
        // Shadow Ball iteration/release, Teleport duration and angle clamp,
        // and the Confusion reflection bone id as integer words; the
        // reflection behavior byte stays unsigned.
        region(data, extension_, MELEE_WEB_MEWTWO_ATTRIBUTE_BYTES);
        mewtwo_.emplace();
#define READ_MEWTWO(at, type, name, original) mewtwo_->name = read_##type(data, extension_ + at);
        MELEE_WEB_MEWTWO_ATTRIBUTE_FIELDS(READ_MEWTWO)
#undef READ_MEWTWO
    } else if (costume_->fighter_kind == 17) {
        // Luigi has a distinct 0x98 source extension; do not alias Mario's
        // fields just because both fighters are in the Mario family.
        region(data, extension_, MELEE_WEB_LUIGI_ATTRIBUTE_BYTES);
        luigi_.emplace();
#define READ_LUIGI(at, type, name, original) luigi_->name = read_##type(data, extension_ + at);
        MELEE_WEB_LUIGI_ATTRIBUTE_FIELDS(READ_LUIGI)
#undef READ_LUIGI
    } else if (costume_->fighter_kind == 12 || costume_->fighter_kind == 23) {
        // Pichu's source wrapper contains only the item words, but its
        // OnLoad path pushes and every shared special callback consumes the
        // complete ftPikachuAttributes record. Decode that exact 0xf8 ABI for
        // both family identities without aliasing their authored values.
        region(data, extension_, MELEE_WEB_PIKACHU_ATTRIBUTE_BYTES);
        pikachu_.emplace();
#define READ_PIKACHU(at, type, name, original, component, source) \
        pikachu_->name = read_##type(data, extension_ + at);
        MELEE_WEB_PIKACHU_ATTRIBUTE_FIELDS(READ_PIKACHU)
#undef READ_PIKACHU
    } else if (costume_->fighter_kind == 15) {
        // Purin's source extension is a distinct 0x100 ABI.  Keep the two
        // opaque words opaque and preserve the authored Vec2 component order;
        // its x48 hat-part root is handled by the native part owner, not this
        // attribute decoder.
        region(data, extension_, MELEE_WEB_PURIN_ATTRIBUTE_BYTES);
        purin_.emplace();
#define PURIN_READ_F32(at, dst) purin_->dst = read_F32(data, extension_ + at);
#define PURIN_READ_I32(at, dst) purin_->dst = read_I32(data, extension_ + at);
#define PURIN_READ_OPAQUE32(at, dst) purin_->dst = read_U32(data, extension_ + at);
#define PURIN_READ_PAD4(at, dst) do { \
        for (unsigned purin_byte = 0; purin_byte < 4; ++purin_byte) \
            purin_->dst[purin_byte] = read_U8(data, extension_ + at + purin_byte); \
    } while (0)
#define PURIN_READ_PAD8(at, dst) do { \
        for (unsigned purin_byte = 0; purin_byte < 8; ++purin_byte) \
            purin_->dst[purin_byte] = read_U8(data, extension_ + at + purin_byte); \
    } while (0)
#define PURIN_READ_IMPL(type, at, dst) PURIN_READ_##type(at, dst)
#define PURIN_READ(type, at, dst) PURIN_READ_IMPL(type, at, dst)
#define READ_PURIN(at, type, dst, member, component, source, source_expr) \
        PURIN_READ(type, at, dst);
        MELEE_WEB_PURIN_ATTRIBUTE_FIELDS(READ_PURIN)
#undef READ_PURIN
#undef PURIN_READ
#undef PURIN_READ_IMPL
#undef PURIN_READ_PAD8
#undef PURIN_READ_PAD4
#undef PURIN_READ_OPAQUE32
#undef PURIN_READ_I32
#undef PURIN_READ_F32
    } else if (costume_->fighter_kind == 2 || costume_->fighter_kind == 25) {
        // Captain and Ganondorf use the source Captain extension loader.
        // Preserve the complete 0x8c record, including authored
        // unknowns and integer fields, until the native consumer is hydrated.
        region(data, extension_, 0x8C);
        captain_.emplace();
#define READ_CAPTAIN(at, type, name, original) captain_->name = read_##type(data, extension_ + at);
        MELEE_WEB_CAPTAIN_ATTRIBUTE_FIELDS(READ_CAPTAIN)
#undef READ_CAPTAIN
    } else if (costume_->fighter_kind == 1 || costume_->fighter_kind == 22) {
        /* Fox and Falco intentionally share ftFox_DatAttrs and all of the
         * original ftFx special-state consumers.  Their PlFx/PlFc values and
         * Article kinds remain distinct in the decoded fields. */
        /* Keep the portable foreign-kind fixture contract: old synthetic Fox
         * fixtures used a Mario-sized extension solely to exercise the common
         * fields.  A real Fox archive has the full 0xd4-byte schema, while
         * Falco is always strict because its playable path depends on it. */
        const auto extension_bytes = data.next_target_offset(extension_) - extension_;
        if (costume_->fighter_kind != 1 || extension_bytes >= 0xD4) {
            region(data, extension_, 0xD4);
            fox_.emplace();
#define READ_FOX(at, type, name, original) fox_->name = read_##type(data, extension_ + at);
            MELEE_WEB_FOX_ATTRIBUTE_FIELDS(READ_FOX)
#undef READ_FOX
            require(fox_->reflector_bone_id < 140, "Fighter reflector bone exceeds checked part bounds");
        }
    } else if (costume_->fighter_kind == 18 || costume_->fighter_kind == 26) {
        region(data, extension_, 0x98);
        mars_.emplace();
#define READ_MARS(at, type, name, original) mars_->name = read_##type(data, extension_ + at);
        MELEE_WEB_MARS_ATTRIBUTE_FIELDS(READ_MARS)
#undef READ_MARS
        require(mars_->absorb_bone >= 0 && mars_->absorb_bone < 140 && mars_->absorb_size > 0,
                "Marth/Roy counter descriptor is outside checked part bounds");
    } else if (costume_->fighter_kind == 6 || costume_->fighter_kind == 20) {
        region(data, extension_, 0xDC);
        link_.emplace();
#define READ_LINK(at, type, name, original) link_->name = read_##type(data, extension_ + at);
        MELEE_WEB_LINK_ATTRIBUTE_FIELDS(READ_LINK)
#undef READ_LINK
        require(link_->absorb_bone >= 0 && link_->absorb_bone < 140 && link_->absorb_size > 0,
                "Link absorb descriptor is outside checked part bounds");
    } else if (costume_->fighter_kind == 8) {
        // Ness owns a unique 0xDC source extension. PK Flash/PK Thunder and
        // PSI Magnet loop counters and gravity delays are signed or unsigned
        // integer words; the PSI Magnet absorb and baseball bat reflection
        // records keep their original descriptor layouts.
        region(data, extension_, 0xDC);
        ness_.emplace();
#define READ_NESS(at, type, name, original) ness_->name = read_##type(data, extension_ + at);
        MELEE_WEB_NESS_ATTRIBUTE_FIELDS(READ_NESS)
#undef READ_NESS
        require(ness_->psimagnet_absorb_bone >= 0 && ness_->psimagnet_absorb_bone < 140 &&
                    ness_->psimagnet_absorb_size > 0,
                "Ness absorb descriptor is outside checked part bounds");
        require(ness_->bat_reflect_bone_id < 140 && ness_->bat_reflect_max_damage > 0 &&
                    ness_->bat_reflect_size > 0,
                "Ness bat reflection descriptor is outside checked part bounds");
    } else if (costume_->fighter_kind == 9) {
        // Peach owns a unique 0xC0 source extension. The float-fall anim
        // starts are authored zero (ftPe_Init_OnLoad refills them from
        // motions 18/19); the Toad counter's held-item odds/kind pairs and
        // the Toad AbsorbDesc keep their source layouts.
        region(data, extension_, 0xC0);
        peach_.emplace();
#define READ_PEACH(at, type, name, original) peach_->name = read_##type(data, extension_ + at);
        MELEE_WEB_PEACH_ATTRIBUTE_FIELDS(READ_PEACH)
#undef READ_PEACH
        require(peach_->absorb_bone >= 0 && peach_->absorb_bone < 140 && peach_->absorb_size > 0,
                "Peach absorb descriptor is outside checked part bounds");
    }
    // ftColl_8007B320 enforces 15 hurt capsules and 11 dynamics spheres;
    // ftCo_8009CF84 enforces strictly fewer than 10 dynamics sets.
    const auto hurt = pointer(data, root_ + 0x30, 8);
    region(data, hurt, 8);
    const auto hurt_count = read_I32(data, hurt);
    require(hurt_count >= 0 && hurt_count <= 15, "Fighter hurtbox count exceeds source capacity");
    const auto hurt_rows = data.pointer(hurt + 4, hurt_count ? std::size_t(hurt_count) * 40 : 1);
    require(hurt_rows || !hurt_count, "Fighter hurtbox records are missing");
    if (hurt_count) region(data, *hurt_rows, std::size_t(hurt_count) * 40);
    for (std::int32_t i = 0; i < hurt_count; ++i) {
        const auto at = *hurt_rows + std::uint32_t(i) * 40;
        DatFighterHurtbox box{};
        box.descriptor_offset = at; box.bone_index = read_U32(data, at);
        box.height = read_U32(data, at + 4); box.is_grabbable = read_U32(data, at + 8);
        require(box.bone_index < 140 && box.height <= 2, "Fighter hurtbox bone or height is invalid");
        for (std::uint32_t axis = 0; axis < 3; ++axis) {
            box.a_offset[axis] = read_F32(data, at + 12 + axis * 4);
            box.b_offset[axis] = read_F32(data, at + 24 + axis * 4);
        }
        box.scale = read_F32(data, at + 36);
        hurtboxes_.push_back(box);
    }
    const auto dyn = pointer(data, root_ + 0x2c, 20);
    dynamics_.descriptor_offset = dyn;
    region(data, dyn, 20);
    const auto bone_count = read_I32(data, dyn), sphere_count = read_I32(data, dyn + 8);
    require(bone_count >= 0 && bone_count < 10 && sphere_count >= 0 && sphere_count <= 11,
            "Fighter dynamics count exceeds source capacity");
    const auto bones = data.pointer(dyn + 4, bone_count ? std::size_t(bone_count) * 24 : 1);
    const auto spheres = data.pointer(dyn + 12, sphere_count ? std::size_t(sphere_count) * 20 : 1);
    require((bones || !bone_count) && (spheres || !sphere_count), "Fighter dynamics records are missing");
    dynamics_.active_bone_count=bone_count;
    uint32_t stored_bones=bone_count;
    if(costume_->fighter_kind==15) {
        // ftCo_8009DC54 retains body slot 0, installs hat slots 1/2 and sets
        // the live count to three; the serialized body count must stay one.
        require(bone_count==1, "Purin costume dynamics require one initial body chain");
        // The descriptor rows selected for the blue/green hats are 1/2 or 3/4.
        require(bones.has_value(), "Purin authored dynamics table is missing");
        const auto bytes=data.next_target_offset(*bones)-*bones;
        require(bytes%24==0 && bytes/24>=5 && bytes/24<=10,
                "Purin authored dynamics extent cannot cover its costume chains");
        stored_bones=bytes/24;
        require(stored_bones>=std::uint32_t(bone_count), "Active dynamics exceed authored descriptors");
    }
    if (stored_bones) region(data, *bones, std::size_t(stored_bones) * 24);
    if (sphere_count) region(data, *spheres, std::size_t(sphere_count) * 20);
    dynamics_.animation_table_offset = data.pointer(dyn + 16, 4);
    std::uint32_t total_parameters = 0;
    for (std::uint32_t i = 0; i < stored_bones; ++i) {
        const auto at = *bones + std::uint32_t(i) * 24;
        DatFighterDynamicsBone bone{};
        bone.descriptor_offset = at; bone.bone_index = read_U32(data, at);
        const auto count = read_U32(data, at + 8);
        // The original pool has 0x140 entries across the world. This only caps
        // a decoded fighter's demand; allocation must account for other users.
        require(bone.bone_index < 140 && count > 0 && count <= 140 && total_parameters + count <= 320,
                "Fighter bone dynamics parameters exceed checked part/pool bounds");
        total_parameters += count;
        const auto parameters = pointer(data, at + 4, std::size_t(count) * 60);
        region(data, parameters, std::size_t(count) * 60);
        for (std::uint32_t axis = 0; axis < 3; ++axis) bone.position[axis] = read_F32(data, at + 12 + axis * 4);
        for (std::uint32_t n = 0; n < count; ++n) {
            std::array<float, 15> values{};
            for (std::uint32_t field = 0; field < 15; ++field)
                values[field] = read_F32(data, parameters + n * 60 + field * 4);
            bone.parameters.push_back(values);
        }
        dynamics_.bones.push_back(std::move(bone));
    }
    for (std::int32_t i = 0; i < sphere_count; ++i) {
        const auto at = *spheres + std::uint32_t(i) * 20;
        DatFighterDynamicsSphere sphere{};
        sphere.descriptor_offset = at; sphere.bone_index = read_U32(data, at);
        require(sphere.bone_index < 140, "Fighter dynamics sphere bone exceeds checked part bounds");
        for (std::uint32_t axis = 0; axis < 3; ++axis) sphere.offset[axis] = read_F32(data, at + 4 + axis * 4);
        sphere.size = read_F32(data, at + 16);
        dynamics_.spheres.push_back(sphere);
    }
    const auto table = pointer(data, root_ + 0xc, std::size_t(costume_->motion_count) * 24);
    const auto blends = pointer(data, root_ + 0x10, std::size_t(costume_->motion_count) * 2);
    region(data, blends, std::size_t(costume_->motion_count) * 2, false);
    // Blend/dynamics records are two raw bytes, never packed host bitfields.
    for (std::uint32_t slot = blends & ~3U; slot < blends + costume_->motion_count * 2; slot += 4)
        if (std::size_t(slot) + 4 <= data.data().size())
            require(!data.has_relocation(slot), "Fighter blend bytes contain a relocation");
    for (std::uint32_t id = 0; id < costume_->motion_count; ++id) {
        const auto row = table + id * 24;
        DatRuntimeAction action{};
        action.motion_id = id;
        action.descriptor_offset = row;
        action.container_offset = scalar(data, row + 4);
        action.archive_bytes = scalar(data, row + 8);
        action.motion_flags = scalar(data, row + 16);
        action.command_offset = data.pointer(row + 12, 4);
        if (action.command_offset) region(data, *action.command_offset, 4);
        const auto blend = data.range(blends + id * 2, 2);
        std::copy(blend.begin(), blend.end(), action.blend_dynamics.begin());
        actions_.push_back(std::move(action));
    }
    for (const auto& action : archive_actions_.actions) actions_[action.motion_id].symbol = action.symbol;
    auto decode_wait_choices = [&](uint32_t field, std::vector<DatWaitChoice>& output) {
      if (const auto choices = data.pointer(root_ + field, 8)) {
        require(*choices % 4 == 0, "Fighter Wait choices are unaligned");
        const auto capacity = std::min<std::uint32_t>(1025, (data.next_target_offset(*choices) - *choices) / 8);
        std::uint64_t total = 0;
        bool terminated = false;
        for (std::uint32_t i = 0; i < capacity; ++i) {
            const auto at = *choices + i * 8;
            const auto id = read_I32(data, at);
            if (id == -1) { terminated = true; break; }
            const auto weight = read_I32(data, at + 4);
            require(i < 1024 && id >= 0 && std::uint32_t(id) < actions_.size() && weight >= 0,
                    "Fighter Wait choice has invalid motion ID or weight");
            total += std::uint32_t(weight);
            require(total <= std::numeric_limits<std::int32_t>::max(), "Fighter Wait cumulative weight overflows original int");
            output.push_back({std::uint32_t(id), std::uint32_t(weight)});
        }
        require(terminated && total >= 100, "Fighter Wait choices do not terminate or cover source random range 1..100");
      }
    };
    decode_wait_choices(0x24, wait_choices_);
    decode_wait_choices(0x28, squat_wait_choices_);
}
const DatRuntimeAction& DatFighterRuntime::action(std::uint32_t id) const
{
    require(id < actions_.size(), "Fighter motion ID is outside the source action table");
    return actions_[id];
}
void DatFighterRuntime::validate_part_indices(std::size_t count) const
{
    require(count > 0 && count <= 140, "Fighter part count exceeds the checked animation boundary");
    for (const auto& box : hurtboxes_) require(box.bone_index < count, "Fighter hurtbox bone is outside the bound skeleton");
    for (std::uint32_t i=0;i<dynamics_.active_bone_count;++i)
        require(dynamics_.bones[i].bone_index < count, "Fighter dynamics bone is outside the bound skeleton");
    for (const auto& sphere : dynamics_.spheres) require(sphere.bone_index < count, "Fighter sphere bone is outside the bound skeleton");
    if (mario_) require(mario_->cape_reflection_x0_bone_id < count, "Fighter reflector bone is outside the bound skeleton");
    if (fox_) require(fox_->reflector_bone_id < count, "Fighter reflector bone is outside the bound skeleton");
    if (mars_) require(std::size_t(mars_->absorb_bone) < count, "Marth counter bone is outside the bound skeleton");
    if (link_) require(std::size_t(link_->absorb_bone) < count, "Link absorb bone is outside the bound skeleton");
    if (ness_) {
        require(ness_->psimagnet_absorb_bone >= 0 && std::size_t(ness_->psimagnet_absorb_bone) < count,
                "Ness absorb bone is outside the bound skeleton");
        require(std::size_t(ness_->bat_reflect_bone_id) < count,
                "Ness bat reflection bone is outside the bound skeleton");
    }
    if (peach_) require(peach_->absorb_bone >= 0 && std::size_t(peach_->absorb_bone) < count,
                        "Peach absorb bone is outside the bound skeleton");
}
std::optional<DatPackedCommands> DatFighterRuntime::commands(std::uint32_t id) const
{
    const auto offset = action(id).command_offset;
    return offset ? std::optional(DatPackedCommands(archive_, *offset)) : std::nullopt;
}

DatFighterAnimationStore::DatFighterAnimationStore(std::shared_ptr<const DatFighterRuntime> fighter,
                                                 std::span<const std::uint8_t> container)
    : fighter_(std::move(fighter))
{
    require(bool(fighter_), "Fighter animation store has no fighter metadata");
    fighter_->archive_actions().validate_container(container);
    container_.assign(container.begin(), container.end());
}
DatSelectedAction DatFighterAnimationStore::select(std::uint32_t id)
{
    return select_impl(id, DatAnimationPolicy::Inspection);
}
DatSelectedAction DatFighterAnimationStore::select_native_action(std::uint32_t id)
{
    return select_impl(id, DatAnimationPolicy::NativeFighterAction);
}
DatSelectedAction DatFighterAnimationStore::select_impl(std::uint32_t id, DatAnimationPolicy policy)
{
    DatSelectedAction result{fighter_->action(id), {}, fighter_->commands(id)};
    if (!result.action.archive_bytes) return result;
    const bool native_action = policy == DatAnimationPolicy::NativeFighterAction;
    for (const auto& entry : cache_) {
        if (entry.animation && entry.offset == result.action.container_offset && entry.size == result.action.archive_bytes &&
            entry.symbol == result.action.symbol && entry.native_action == native_action) {
            result.animation = entry.animation; return result;
        }
    }
    const DatArchive archive(fighter_->archive_actions().slice(container_, id));
    const auto offset = root(archive, result.action.symbol);
    try {
        result.animation = std::make_shared<const DatAnimation>(archive, offset, policy);
    } catch (const DatError& error) {
        throw DatError("Fighter action "+std::to_string(id)+" ("+
                       result.action.symbol+"): "+error.what());
    }
    cache_[next_] = {result.action.container_offset, result.action.archive_bytes, result.action.symbol,
                     native_action, result.animation};
    next_ = (next_ + 1) % cache_.size();
    return result;
}
} // namespace melee_web
