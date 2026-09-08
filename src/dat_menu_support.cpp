#include "dat_menu_support.hpp"

#include "dat_material_animation.hpp"
#include "dat_native_animation.hpp"
#include "dat_native_joint.hpp"
#include "dat_shape_animation.hpp"
#include "gameplay_compat.h"
#include "hsd_native_joint.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include <melee/sc/types.h>
#include <sysdolphin/baselib/cobj.h>
#include <sysdolphin/baselib/jobj.h>
#include <sysdolphin/baselib/wobj.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace melee_web {
namespace {

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

struct Names {
    const char* basename;
    const char* symbol;
};

Names names_for(DatMenuSupportKind kind)
{
    switch (kind) {
    case DatMenuSupportKind::CardIcons:
        return {"LbMcGame.", "MemCardIconData"};
    case DatMenuSupportKind::CardScene:
        return {"NtMemAc", "ScNtcCommon_scene_data"};
    }
    throw DatError("Unknown menu support archive kind");
}

const DatPublicSymbol& find_symbol(const DatArchive& archive, std::string_view name)
{
    for (const auto& symbol : archive.public_symbols())
        if (symbol.name == name) return symbol;
    throw DatError("Required menu support public symbol is missing");
}

} // namespace

// The archive stores SceneCameraDesc as two 32-bit pointers regardless of
// the host ABI. Native published descriptors are pointer-sized, so only the
// Wasm build may assert the source layout directly.
constexpr std::size_t kSceneCameraRecordBytes = 8;
#ifdef __wasm__
static_assert(sizeof(SceneDesc::SceneCameraDesc) == kSceneCameraRecordBytes);
static_assert(offsetof(SceneDesc::SceneCameraDesc, desc) == 0);
static_assert(offsetof(SceneDesc::SceneCameraDesc, anims) == 4);
#endif

struct DatMenuSupport::Storage {
    struct CameraOwner {
        std::vector<std::unique_ptr<HSD_WObjDesc>> worlds;
    };

    struct ModelOwner {
        std::unique_ptr<DatNativeJoint> graph;
    };

    std::shared_ptr<const DatArchive> archive;
    DatMenuSupportKind kind;
    DatMenuSupportLanguage setting_language;
    DatMenuSupportLanguage saved_language;
    std::string resolved;
    std::string source;
    std::string symbol;

    // These allocations are all native pointer arrays or copied scalar
    // descriptors. They are never written into the immutable DAT bytes.
    std::vector<std::shared_ptr<void>> memory;
    std::vector<std::unique_ptr<ModelOwner>> model_owners;
    std::vector<MeleeWebNativeJoint*> native_joints;
    std::vector<CameraOwner> camera_owners;
    std::vector<std::unique_ptr<HSD_CameraAnim>> camera_animations;
    std::vector<std::span<const std::uint8_t>> icon_payloads;

    // Published model descriptors borrow these owners until the SceneDesc is
    // torn down. They are separate from graph owners so native destruction
    // can run before either descriptor graph is released.
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;

    std::vector<std::uint32_t> light_lists;
    bool has_fog = false;

    std::vector<DynamicModelDesc*> models;
    std::vector<SceneDesc::SceneCameraDesc> cameras;
    std::vector<HSD_CameraAnim*> camera_animation_table;
    std::vector<void*> icon_table;
    SceneDesc scene{};

    ~Storage()
    {
        for (auto* native : native_joints) {
            if (!melee_web_native_joint_destroy(native, nullptr, 0))
                std::terminate();
        }
    }

    template <class T>
    T* make(std::size_t count = 1)
    {
        require(count <= 65536, "Menu support allocation exceeds budget");
        auto allocation = std::shared_ptr<T[]>(new T[count]{});
        T* result = allocation.get();
        memory.emplace_back(std::move(allocation), result);
        return result;
    }

    void record(std::uint32_t offset, std::size_t length, const char* message)
    {
        require(!(offset & 3), message);
        (void) archive->range(offset, length);
        require(length <= archive->next_target_offset(offset) - offset, message);
    }

    std::uint32_t pointer(std::uint32_t slot, std::size_t minimum,
                          const char* message)
    {
        const auto target = archive->pointer(slot, minimum);
        require(target.has_value(), message);
        return *target;
    }

    float number(std::uint32_t offset, const char* message)
    {
        const float value = archive->f32(offset);
        require(std::isfinite(value), message);
        return value;
    }

    Vec3* vector(std::uint32_t offset)
    {
        record(offset, 12, "Menu support vector record is invalid");
        auto* result = make<Vec3>();
        result->x = number(offset, "Menu support vector is nonfinite");
        result->y = number(offset + 4, "Menu support vector is nonfinite");
        result->z = number(offset + 8, "Menu support vector is nonfinite");
        return result;
    }

    HSD_WObjDesc* world(std::optional<std::uint32_t> offset,
                        CameraOwner& owner)
    {
        if (!offset) return nullptr;
        record(*offset, 20, "Menu support camera world record is invalid");
        require(!archive->pointer(*offset).has_value() &&
                    !archive->pointer(*offset + 16).has_value(),
                "Menu support camera world class/constraint is unsupported");
        auto result = std::make_unique<HSD_WObjDesc>();
        result->pos = *vector(*offset + 4);
        HSD_WObjDesc* pointer = result.get();
        owner.worlds.push_back(std::move(result));
        return pointer;
    }

    HSD_CObjDesc* camera(std::uint32_t offset, CameraOwner& owner)
    {
        record(offset, 48, "Menu support camera descriptor is truncated");
        require(!archive->pointer(offset).has_value(),
                "Menu support camera class is unsupported");
        auto* result = make<HSD_CObjDesc>();
        auto& common = result->common;
        common.flags = archive->be16(offset + 4);
        common.projection_type = archive->be16(offset + 6);
        require(common.projection_type >= 1 && common.projection_type <= 3,
                "Menu support camera projection is invalid");
        common.viewport = {int16_t(archive->be16(offset + 8)),
                           int16_t(archive->be16(offset + 10)),
                           int16_t(archive->be16(offset + 12)),
                           int16_t(archive->be16(offset + 14))};
        common.scissor = {archive->be16(offset + 16), archive->be16(offset + 18),
                          archive->be16(offset + 20), archive->be16(offset + 22)};
        common.eyepos = world(archive->pointer(offset + 24, 20), owner);
        common.interest = world(archive->pointer(offset + 28, 20), owner);
        common.roll = number(offset + 32, "Menu support camera roll is nonfinite");
        if (const auto up = archive->pointer(offset + 36, 12))
            common.up_vector = vector(*up);
        common.nnear = number(offset + 40, "Menu support camera near clip is nonfinite");
        common.ffar = number(offset + 44, "Menu support camera far clip is nonfinite");
        require(common.nnear > 0 && common.ffar > common.nnear,
                "Menu support camera clip range is invalid");
        if (common.projection_type == 1) {
            record(offset, 56, "Menu support perspective camera is truncated");
            result->perspective.fov = number(offset + 48,
                                             "Menu support camera FOV is nonfinite");
            result->perspective.aspect = number(offset + 52,
                                                "Menu support camera aspect is nonfinite");
            require(result->perspective.fov > 0 && result->perspective.fov < 180 &&
                        result->perspective.aspect > 0,
                    "Menu support perspective camera values are invalid");
        } else {
            record(offset, 64, "Menu support frustum camera is truncated");
            result->frustum.top = number(offset + 48, "Menu support camera top is nonfinite");
            result->frustum.bottom = number(offset + 52, "Menu support camera bottom is nonfinite");
            result->frustum.left = number(offset + 56, "Menu support camera left is nonfinite");
            result->frustum.right = number(offset + 60, "Menu support camera right is nonfinite");
            require(result->frustum.top != result->frustum.bottom &&
                        result->frustum.left != result->frustum.right,
                    "Menu support frustum is degenerate");
        }
        return result;
    }

    template <class Callback>
    std::vector<std::uint32_t> pointer_table(std::uint32_t offset,
                                             const char* message,
                                             Callback&& callback)
    {
        const std::uint32_t end = archive->next_target_offset(offset);
        std::vector<std::uint32_t> result;
        for (std::size_t index = 0; index < 1024; ++index) {
            require(offset + 4 * index + 4 <= end, message);
            const auto target = archive->pointer(offset + 4 * index, 1);
            if (!target.has_value()) {
                require(offset + 4 * (index + 1) == end, message);
                return result;
            }
            result.push_back(*target);
            callback(*target);
        }
        throw DatError(message);
    }

    HSD_CameraAnim* camera_animation(std::uint32_t offset)
    {
        record(offset, 12, "Menu support camera animation is truncated");
        // CSS's card setup never requests camera animation. The local root
        // still contains one authored zero-channel HSD_CameraAnim; preserve it
        // as a typed descriptor and reject any active channels explicitly.
        require(!archive->pointer(offset, 16).has_value() &&
                    !archive->pointer(offset + 4, 8).has_value() &&
                    !archive->pointer(offset + 8, 8).has_value(),
                "Menu support camera animation channels are not consumed by CSS");
        auto animation = std::make_unique<HSD_CameraAnim>();
        auto* result = animation.get();
        camera_animations.push_back(std::move(animation));
        return result;
    }

    void decode_icons(std::uint32_t root)
    {
        // lb_8001C820 selects entries 0..2, while lb_8001C8BC and
        // dont_inline_helper unconditionally pass entry 3 to the card
        // service.  hsd_803B2ADC then copies the first 18 bytes of whichever
        // icon entry is selected into CardState::x3B0.  Keep this decoder at
        // that exact source boundary: four payloads, each large enough for
        // that copy, followed by one null table entry.
        constexpr std::size_t kCardIconCount = 4;
        constexpr std::size_t kCardIconHeaderBytes = 18;
        const std::uint32_t end = archive->next_target_offset(root);
        require(end >= root + 8 && ((end - root) % 4) == 0,
                "MemCardIconData table extent is invalid");
        const auto icon_data = archive->data();
        for (std::size_t index = 0; index < 1024; ++index) {
            const auto slot = root + 4 * index;
            require(slot + 4 <= end, "MemCardIconData table lacks a terminator");
            const auto target = archive->pointer(slot, 1);
            if (!target.has_value()) {
                require(slot + 4 == end, "MemCardIconData has data after its terminator");
                require(index == kCardIconCount,
                        "MemCardIconData has the wrong number of image payloads");
                icon_table.push_back(nullptr);
                return;
            }
            const std::uint32_t payload_end = archive->next_target_offset(*target);
            require(payload_end >= *target + kCardIconHeaderBytes,
                    "MemCardIconData payload is shorter than the card header copy");
            const auto payload = archive->range(*target, payload_end - *target);
            icon_payloads.push_back(payload);
            icon_table.push_back(const_cast<std::uint8_t*>(icon_data.data() + *target));
        }
        throw DatError("MemCardIconData table exceeds its bound");
    }

    std::vector<HSD_AnimJoint*> decode_joint_animations(std::uint32_t table,
                                                        const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_AnimJoint*> result;
        pointer_table(table, "DynamicModelDesc joint animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatNativeAnimation>(
                              archive, root, graph);
                          result.push_back(static_cast<HSD_AnimJoint*>(owner->descriptor()));
                          animations.push_back(std::move(owner));
                      });
        return result;
    }

    std::vector<HSD_MatAnimJoint*> decode_material_animations(
        std::uint32_t table, const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_MatAnimJoint*> result;
        pointer_table(table, "DynamicModelDesc material animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatMaterialAnimation>(
                              archive, root, graph, TextureIndexValidation::AllEncodedValues);
                          result.push_back(static_cast<HSD_MatAnimJoint*>(owner->descriptor()));
                          materials.push_back(std::move(owner));
                      });
        return result;
    }

    std::vector<HSD_ShapeAnimJoint*> decode_shape_animations(
        std::uint32_t table, const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_ShapeAnimJoint*> result;
        pointer_table(table, "DynamicModelDesc shape animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatShapeAnimation>(archive, root, graph);
                          result.push_back(owner->descriptor());
                          shapes.push_back(std::move(owner));
                      });
        return result;
    }

    void decode_model(std::uint32_t offset)
    {
        record(offset, 16, "DynamicModelDesc is truncated");
        auto owner = std::make_unique<ModelOwner>();
        const auto joint = pointer(offset, 64, "DynamicModelDesc joint is missing");
        owner->graph = std::make_unique<DatNativeJoint>(archive, joint);
        char error[256];
        auto* native = melee_web_native_joint_hydrate(&owner->graph->graph(), error,
                                                      sizeof(error));
        require(native, error);
        native_joints.push_back(native);
        auto* descriptor = static_cast<HSD_Joint*>(
            melee_web_native_joint_descriptor(native, error, sizeof(error)));
        require(descriptor, error);

        auto* model = make<DynamicModelDesc>();
        model->joint = descriptor;
        if (const auto animations = archive->pointer(offset + 4, 4)) {
            auto values = decode_joint_animations(*animations, owner->graph->graph());
            auto** table = make<HSD_AnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), table);
            model->anims = table;
        }
        if (const auto materials = archive->pointer(offset + 8, 4)) {
            auto values = decode_material_animations(*materials, owner->graph->graph());
            auto** table = make<HSD_MatAnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), table);
            model->matanims = table;
        }
        if (const auto shapes = archive->pointer(offset + 12, 4)) {
            auto values = decode_shape_animations(*shapes, owner->graph->graph());
            auto** table = make<HSD_ShapeAnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), table);
            model->shapeanims = table;
        }
        models.push_back(model);
        model_owners.push_back(std::move(owner));
    }

    void decode_scene(std::uint32_t root)
    {
        record(root, 16, "SceneDesc is truncated");
        const auto model_table = pointer(root, 4, "SceneDesc model table is missing");
        const auto camera_table = pointer(root + 4, 8, "SceneDesc camera table is missing");

        pointer_table(model_table, "SceneDesc model table is invalid",
                      [&](std::uint32_t model) { decode_model(model); });
        require(!models.empty(), "SceneDesc has no models");
        scene.models = make<DynamicModelDesc*>(models.size() + 1);
        std::copy(models.begin(), models.end(), scene.models);

        const std::uint32_t camera_end = archive->next_target_offset(camera_table);
        require(camera_end > camera_table &&
                    (camera_end - camera_table) % kSceneCameraRecordBytes == 0,
                "SceneDesc camera table extent is invalid");
        const auto camera_count = (camera_end - camera_table) / kSceneCameraRecordBytes;
        require(camera_count <= 64, "SceneDesc camera table exceeds its bound");
        for (std::uint32_t offset = camera_table; offset < camera_end;
             offset += kSceneCameraRecordBytes) {
            SceneDesc::SceneCameraDesc camera_entry{};
            const auto camera_descriptor = pointer(offset, 48,
                                                   "SceneDesc camera descriptor is missing");
            auto owner = CameraOwner{};
            camera_entry.desc = camera(camera_descriptor, owner);
            if (const auto animations = archive->pointer(offset + 4, 4)) {
                std::vector<HSD_CameraAnim*> values;
                pointer_table(*animations, "SceneDesc camera animation table is invalid",
                              [&](std::uint32_t animation) {
                                  values.push_back(camera_animation(animation));
                              });
                auto** table = make<HSD_CameraAnim*>(values.size() + 1);
                std::copy(values.begin(), values.end(), table);
                camera_animation_table.insert(camera_animation_table.end(), values.begin(),
                                              values.end());
                camera_entry.anims = table;
            }
            camera_owners.push_back(std::move(owner));
            cameras.push_back(camera_entry);
        }
        require(!cameras.empty(), "SceneDesc has no cameras");
        scene.cameras = make<SceneDesc::SceneCameraDesc>(cameras.size());
        std::copy(cameras.begin(), cameras.end(), scene.cameras);

        // lbCardGame_LoadArchive only publishes this SceneDesc; the sole CSS
        // card consumer, lb_8001CF18, reads cameras[0].desc and
        // models[0]->joint/animation arrays. It never dereferences lights or
        // fogs. Validate those authored table boundaries and expose counts to
        // the integration layer, while leaving the published source pointers
        // null rather than claiming that general scene lighting is hydrated.
        if (const auto lights = archive->pointer(root + 8, 4)) {
            pointer_table(*lights, "SceneDesc light table is invalid",
                          [&](std::uint32_t list) {
                              record(list, 8, "SceneDesc light list is truncated");
                              light_lists.push_back(list);
                              require(archive->pointer(list, 1).has_value(),
                                      "SceneDesc light list descriptor is missing");
                              if (const auto animations = archive->pointer(list + 4, 4))
                                  (void) pointer_table(*animations,
                                                        "SceneDesc light animation table is invalid",
                                                        [](std::uint32_t) {});
                          });
        }
        has_fog = archive->pointer(root + 12, 4).has_value();
    }
};

std::string DatMenuSupport::resolve_filename(std::string_view basename,
                                              DatMenuSupportLanguage setting_language,
                                              DatMenuSupportLanguage saved_language)
{
    require(!basename.empty() && basename.size() < 0x20,
            "Menu support filename is too long");
    const auto dot = basename.find('.');
    if (dot != std::string_view::npos && dot + 1 < basename.size())
        return std::string(basename);
    std::string result(basename.substr(0, dot == std::string_view::npos ? basename.size() : dot));
    result += '.';
    const auto language = dot == std::string_view::npos ? saved_language : setting_language;
    result += language == DatMenuSupportLanguage::English ? "usd" : "dat";
    return result;
}

DatMenuSupport::DatMenuSupport(std::shared_ptr<const DatArchive> archive,
                               DatMenuSupportKind kind,
                               DatMenuSupportLanguage setting_language,
                               DatMenuSupportLanguage saved_language)
    : storage_(std::make_unique<Storage>())
{
    auto& storage = *storage_;
    require(bool(archive), "Menu support archive is missing");
    const auto names = names_for(kind);
    storage.archive = std::move(archive);
    storage.kind = kind;
    storage.setting_language = setting_language;
    storage.saved_language = saved_language;
    storage.source = names.basename;
    storage.symbol = names.symbol;
    storage.resolved = resolve_filename(storage.source, setting_language, saved_language);
    const auto& root = find_symbol(*storage.archive, storage.symbol);
    if (kind == DatMenuSupportKind::CardIcons)
        storage.decode_icons(root.data_offset);
    else
        storage.decode_scene(root.data_offset);
}

DatMenuSupport::~DatMenuSupport() = default;

void* DatMenuSupport::descriptor() const noexcept
{
    if (storage_->kind == DatMenuSupportKind::CardIcons)
        return const_cast<void*>(static_cast<const void*>(storage_->icon_table.data()));
    return const_cast<SceneDesc*>(static_cast<const SceneDesc*>(&storage_->scene));
}

std::string_view DatMenuSupport::source_basename() const noexcept
{
    return storage_->source;
}

std::string_view DatMenuSupport::resolved_filename() const noexcept
{
    return storage_->resolved;
}

std::string_view DatMenuSupport::symbol_name() const noexcept
{
    return storage_->symbol;
}

std::size_t DatMenuSupport::icon_count() const noexcept
{
    return storage_->icon_payloads.size();
}

std::span<const std::uint8_t> DatMenuSupport::icon_payload(std::size_t index) const
{
    require(storage_->kind == DatMenuSupportKind::CardIcons,
            "Icon payload requested from a scene support archive");
    require(index < storage_->icon_payloads.size(), "Menu support icon index is out of range");
    return storage_->icon_payloads[index];
}

std::size_t DatMenuSupport::model_count() const noexcept
{
    return storage_->models.size();
}

std::size_t DatMenuSupport::camera_count() const noexcept
{
    return storage_->cameras.size();
}

std::size_t DatMenuSupport::camera_animation_count() const noexcept
{
    return storage_->camera_animation_table.size();
}

std::size_t DatMenuSupport::unconsumed_light_list_count() const noexcept
{
    return storage_->light_lists.size();
}

bool DatMenuSupport::has_unconsumed_fog() const noexcept
{
    return storage_->has_fog;
}

} // namespace melee_web
