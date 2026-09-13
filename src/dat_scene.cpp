#include "dat_scene.hpp"

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
#include <sysdolphin/baselib/fog.h>
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace melee_web {
namespace {

constexpr std::size_t kSceneCameraRecordBytes = 8;
constexpr std::size_t kSceneLightListRecordBytes = 8;
constexpr std::size_t kSceneFogRecordBytes = 8;
constexpr std::size_t kMaxSceneModels = 1024;
constexpr std::size_t kMaxSceneCameras = 64;
constexpr std::size_t kMaxSceneLights = 256;
constexpr std::size_t kMaxSceneAnimations = 256;

void require(bool value, const char* message)
{
    if (!value) throw DatError(message);
}

const DatPublicSymbol& find_symbol(const DatArchive& archive, std::string_view name)
{
    for (const auto& symbol : archive.public_symbols()) {
        if (symbol.name == name) return symbol;
    }
    throw DatError("Required SceneDesc public symbol is missing");
}

} // namespace

struct DatScene::Storage {
    struct AnimationOwner {
        std::vector<std::unique_ptr<HSD_CameraAnim>> cameras;
        std::vector<std::unique_ptr<HSD_LightAnim>> lights;
    } animation_owner;

    std::shared_ptr<const DatArchive> archive;
    std::string symbol;
    DatSceneRootKind root_kind = DatSceneRootKind::SceneDesc;
    std::vector<std::shared_ptr<void>> memory;

    // These source graph owners must remain alive while their hydrated native
    // descriptors are reachable through any published DynamicModelDesc.
    std::vector<std::unique_ptr<DatNativeJoint>> graphs;
    std::vector<MeleeWebNativeJoint*> natives;
    std::vector<std::unique_ptr<DatNativeAnimation>> animations;
    std::vector<std::unique_ptr<DatMaterialAnimation>> materials;
    std::vector<std::unique_ptr<DatShapeAnimation>> shapes;

    std::map<std::uint32_t, DynamicModelDesc*> models_by_offset;
    std::set<std::uint32_t> active_models;
    std::map<std::uint32_t, HSD_CObjDesc*> cameras_by_offset;
    std::map<std::uint32_t, HSD_LightDesc*> lights_by_offset;
    std::set<std::uint32_t> active_lights;
    std::map<std::uint32_t, HSD_LightAnim*> light_animations_by_offset;
    std::set<std::uint32_t> active_light_animations;

    std::vector<DynamicModelDesc*> models;
    std::vector<SceneDesc::SceneCameraDesc> cameras;
    std::vector<SceneDesc::LightList*> light_lists;
    std::vector<SceneDesc::SceneFogDesc> fogs;
    SceneDesc scene{};
    DynamicModelDesc** published_model_table = nullptr;
    DynamicModelDesc* published_single_model = nullptr;

    ~Storage()
    {
        // Native handles are the runtime owner of the hydrated HSD joint
        // descriptors. Destroy them before DatNativeJoint releases its graph.
        for (auto* native : natives) {
            if (!melee_web_native_joint_destroy(native, nullptr, 0))
                std::terminate();
        }
    }

    template <class T>
    T* make(std::size_t count = 1)
    {
        require(count > 0 && count <= 65536,
                "Scene descriptor allocation exceeds its bound");
        auto allocation = std::shared_ptr<T[]>(new T[count]{});
        T* result = allocation.get();
        memory.emplace_back(std::move(allocation), result);
        return result;
    }

    void record(std::uint32_t offset, std::size_t length, const char* message)
    {
        require((offset & 3U) == 0, message);
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
        const float result = archive->f32(offset);
        require(std::isfinite(result), message);
        return result;
    }

    Vec3* vector(std::uint32_t offset, const char* message)
    {
        record(offset, 12, message);
        auto* result = make<Vec3>();
        result->x = number(offset, "Scene vector x is nonfinite");
        result->y = number(offset + 4, "Scene vector y is nonfinite");
        result->z = number(offset + 8, "Scene vector z is nonfinite");
        return result;
    }

    HSD_WObjDesc* world(std::optional<std::uint32_t> offset)
    {
        if (!offset) return nullptr;
        record(*offset, 20, "Scene camera world descriptor is truncated");
        require(!archive->pointer(*offset) &&
                    !archive->pointer(*offset + 16),
                "Scene camera world class or constraint is unsupported");
        auto* result = make<HSD_WObjDesc>();
        result->pos = *vector(*offset + 4,
                              "Scene camera world position is truncated");
        return result;
    }

    HSD_CObjDesc* camera(std::uint32_t offset)
    {
        if (const auto existing = cameras_by_offset.find(offset);
            existing != cameras_by_offset.end()) {
            return existing->second;
        }
        record(offset, 48, "Scene camera descriptor is truncated");
        require(!archive->pointer(offset), "Scene camera class is unsupported");

        auto* result = make<HSD_CObjDesc>();
        auto& common = result->common;
        common.flags = archive->be16(offset + 4);
        common.projection_type = archive->be16(offset + 6);
        require(common.projection_type >= 1 && common.projection_type <= 3,
                "Scene camera projection is invalid");
        common.viewport = {int16_t(archive->be16(offset + 8)),
                           int16_t(archive->be16(offset + 10)),
                           int16_t(archive->be16(offset + 12)),
                           int16_t(archive->be16(offset + 14))};
        common.scissor = {archive->be16(offset + 16), archive->be16(offset + 18),
                          archive->be16(offset + 20), archive->be16(offset + 22)};
        common.eyepos = world(archive->pointer(offset + 24, 20));
        common.interest = world(archive->pointer(offset + 28, 20));
        common.roll = number(offset + 32, "Scene camera roll is nonfinite");
        if (const auto up = archive->pointer(offset + 36, 12))
            common.up_vector = vector(*up, "Scene camera up vector is truncated");
        common.nnear = number(offset + 40, "Scene camera near clip is nonfinite");
        common.ffar = number(offset + 44, "Scene camera far clip is nonfinite");
        require(common.nnear > 0 && common.ffar > common.nnear,
                "Scene camera clip range is invalid");
        if (common.projection_type == 1) {
            record(offset, 56, "Scene perspective camera is truncated");
            result->perspective.fov =
                number(offset + 48, "Scene camera FOV is nonfinite");
            result->perspective.aspect =
                number(offset + 52, "Scene camera aspect is nonfinite");
            require(result->perspective.fov > 0 && result->perspective.fov < 180 &&
                        result->perspective.aspect > 0,
                    "Scene perspective camera values are invalid");
        } else {
            record(offset, 64, "Scene frustum camera is truncated");
            result->frustum.top = number(offset + 48,
                                         "Scene camera top is nonfinite");
            result->frustum.bottom = number(offset + 52,
                                            "Scene camera bottom is nonfinite");
            result->frustum.left = number(offset + 56,
                                          "Scene camera left is nonfinite");
            result->frustum.right = number(offset + 60,
                                           "Scene camera right is nonfinite");
            require(result->frustum.top != result->frustum.bottom &&
                        result->frustum.left != result->frustum.right,
                    "Scene frustum is degenerate");
        }
        cameras_by_offset.emplace(offset, result);
        return result;
    }

    // Scene camera/light/fog animation objects may contain AObj/FObj channels.
    // The model animation classes already own those channels. These scene
    // records are published only when the source record is empty; accepting an
    // active channel without its original scheduler would silently omit motion.
    HSD_CameraAnim* camera_animation(std::uint32_t offset)
    {
        require(animation_owner.cameras.size() < kMaxSceneAnimations,
                "Scene camera animation count exceeds its bound");
        record(offset, 12, "Scene camera animation is truncated");
        require(!archive->pointer(offset) && !archive->pointer(offset + 4) &&
                    !archive->pointer(offset + 8),
                "Active Scene camera animation channels are unsupported");
        auto owner = std::make_unique<HSD_CameraAnim>();
        auto* result = owner.get();
        animation_owner.cameras.push_back(std::move(owner));
        return result;
    }

    HSD_WObjAnim* reject_world_animation(std::optional<std::uint32_t> offset,
                                         const char* message)
    {
        if (!offset) return nullptr;
        record(*offset, 8, message);
        require(!archive->pointer(*offset) && !archive->pointer(*offset + 4),
                "Active Scene world animation channels are unsupported");
        return nullptr;
    }

    HSD_LightAnim* light_animation(std::uint32_t offset)
    {
        if (const auto existing = light_animations_by_offset.find(offset);
            existing != light_animations_by_offset.end()) {
            return existing->second;
        }
        require(active_light_animations.insert(offset).second,
                "Scene light animation cycle detected");
        require(animation_owner.lights.size() < kMaxSceneAnimations,
                "Scene light animation count exceeds its bound");
        record(offset, 16, "Scene light animation is truncated");
        auto owner = std::make_unique<HSD_LightAnim>();
        auto* result = owner.get();
        if (const auto next = archive->pointer(offset, 16))
            result->next = light_animation(*next);
        require(!archive->pointer(offset + 4),
                "Active Scene light animation channels are unsupported");
        (void) reject_world_animation(archive->pointer(offset + 8, 8),
                                       "Scene light position animation is truncated");
        (void) reject_world_animation(archive->pointer(offset + 12, 8),
                                       "Scene light interest animation is truncated");
        animation_owner.lights.push_back(std::move(owner));
        light_animations_by_offset.emplace(offset, result);
        active_light_animations.erase(offset);
        return result;
    }

    HSD_LightDesc* light(std::uint32_t offset)
    {
        if (const auto existing = lights_by_offset.find(offset);
            existing != lights_by_offset.end()) {
            return existing->second;
        }
        require(active_lights.insert(offset).second,
                "Scene light descriptor cycle detected");
        require(lights_by_offset.size() + active_lights.size() <= kMaxSceneLights,
                "Scene light descriptor count exceeds its bound");
        record(offset, 28, "Scene light descriptor is truncated");
        require(!archive->pointer(offset), "Scene light class is unsupported");

        auto* result = make<HSD_LightDesc>();
        result->flags = archive->be16(offset + 8);
        result->attnflags = archive->be16(offset + 10);
        const unsigned type = result->flags & LOBJ_TYPE_MASK;
        std::memcpy(&result->color, archive->range(offset + 12, 4).data(), 4);
        result->position = world(archive->pointer(offset + 16, 20));
        result->interest = world(archive->pointer(offset + 20, 20));
        const auto parameters = archive->pointer(offset + 24, 4);
        if (type == LOBJ_POINT || type == LOBJ_SPOT) {
            require(parameters.has_value(), "Scene positional light parameters are missing");
            const auto base = *parameters;
            const bool raw = type == LOBJ_POINT ?
                (result->attnflags & LOBJ_LIGHT_ATTN) != 0 : result->attnflags != 0;
            if (raw) {
                record(base, 24, "Scene raw light attenuation is truncated");
                auto* attn = make<HSD_LightAttn>();
                attn->a0 = number(base, "Nonfinite light a0");
                attn->a1 = number(base + 4, "Nonfinite light a1");
                attn->a2 = number(base + 8, "Nonfinite light a2");
                attn->k0 = number(base + 12, "Nonfinite light k0");
                attn->k1 = number(base + 16, "Nonfinite light k1");
                attn->k2 = number(base + 20, "Nonfinite light k2");
                result->u.attn = attn;
            } else if (type == LOBJ_POINT) {
                record(base, 12, "Scene point light parameters are truncated");
                auto* point = make<HSD_LightPointDesc>();
                point->ref_br = number(base, "Nonfinite light reference brightness");
                point->ref_dist = number(base + 4, "Nonfinite light reference distance");
                point->dist_func = archive->be32(base + 8);
                require(point->dist_func <= GX_DA_STEEP, "Invalid light distance function");
                result->u.point = point;
            } else {
                record(base, 20, "Scene spot light parameters are truncated");
                auto* spot = make<HSD_LightSpotDesc>();
                spot->cutoff = number(base, "Nonfinite spotlight cutoff");
                spot->spot_func = archive->be32(base + 4);
                spot->ref_br = number(base + 8, "Nonfinite light reference brightness");
                spot->ref_dist = number(base + 12, "Nonfinite light reference distance");
                spot->dist_func = archive->be32(base + 16);
                require(spot->spot_func <= GX_SP_RING2 && spot->dist_func <= GX_DA_STEEP,
                        "Invalid spotlight attenuation function");
                result->u.spot = spot;
            }
        } else if (parameters) {
            result->u.shininess = make<float>();
            *result->u.shininess = number(*parameters, "Scene light shininess is nonfinite");
        }
        if (const auto next = archive->pointer(offset + 4, 28))
            result->next = light(*next);
        lights_by_offset.emplace(offset, result);
        active_lights.erase(offset);
        return result;
    }

    HSD_FogDesc* fog(std::uint32_t offset)
    {
        record(offset, 20, "Scene fog descriptor is truncated");
        auto* result = make<HSD_FogDesc>();
        result->type = archive->be32(offset);
        require(result->type <= 15, "Scene fog type is invalid");
        result->start = number(offset + 8, "Scene fog start is nonfinite");
        result->end = number(offset + 12, "Scene fog end is nonfinite");
        require(result->end >= result->start,
                "Scene fog range is inverted");
        std::memcpy(&result->color, archive->range(offset + 16, 4).data(), 4);
        if (const auto adjustment = archive->pointer(offset + 4, 68)) {
            record(*adjustment, 68, "Scene fog adjustment is truncated");
            auto* value = make<HSD_FogAdjDesc>();
            value->center = archive->be16(*adjustment);
            value->width = archive->be16(*adjustment + 2);
            for (unsigned row = 0; row < 4; ++row)
                for (unsigned column = 0; column < 4; ++column)
                    value->mtx[row][column] = number(
                        *adjustment + 4 + 4 * (row * 4 + column),
                        "Scene fog adjustment matrix is nonfinite");
            result->fogadjdesc = value;
        }
        return result;
    }

    template <class Callback>
    std::vector<std::uint32_t> pointer_table(std::uint32_t offset,
                                             const char* message,
                                             Callback&& callback,
                                             std::size_t minimum = 1,
                                             bool allow_null_padding = false)
    {
        const std::uint32_t end = archive->next_target_offset(offset);
        require(end > offset && end - offset >= 4 && (end - offset) % 4 == 0,
                message);
        std::vector<std::uint32_t> result;
        for (std::size_t index = 0; index < 1024; ++index) {
            const std::uint32_t slot = offset + 4 * static_cast<std::uint32_t>(index);
            require(slot + 4 <= end, message);
            const auto target = archive->pointer(slot, minimum);
            if (!target.has_value()) {
                if (allow_null_padding) {
                    for (std::uint32_t padding = slot + 4; padding < end;
                         padding += 4) {
                        require(!archive->pointer(padding, 1), message);
                    }
                } else {
                    require(slot + 4 == end, message);
                }
                return result;
            }
            result.push_back(*target);
            callback(*target);
        }
        throw DatError(message);
    }

    std::vector<HSD_AnimJoint*> joint_animations(std::uint32_t table,
                                                  const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_AnimJoint*> result;
        pointer_table(table, "Scene joint animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatNativeAnimation>(
                              archive, root, graph);
                          result.push_back(static_cast<HSD_AnimJoint*>(
                              owner->descriptor()));
                          animations.push_back(std::move(owner));
                      });
        return result;
    }

    std::vector<HSD_MatAnimJoint*> material_animations(
        std::uint32_t table, const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_MatAnimJoint*> result;
        pointer_table(table, "Scene material animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatMaterialAnimation>(
                              archive, root, graph,
                              TextureIndexValidation::AllEncodedValues);
                          result.push_back(static_cast<HSD_MatAnimJoint*>(
                              owner->descriptor()));
                          materials.push_back(std::move(owner));
                      });
        return result;
    }

    std::vector<HSD_ShapeAnimJoint*> shape_animations(
        std::uint32_t table, const MeleeWebNativeGraph& graph)
    {
        std::vector<HSD_ShapeAnimJoint*> result;
        pointer_table(table, "Scene shape animation table is invalid",
                      [&](std::uint32_t root) {
                          auto owner = std::make_unique<DatShapeAnimation>(
                              archive, root, graph);
                          result.push_back(owner->descriptor());
                          shapes.push_back(std::move(owner));
                      });
        return result;
    }

    DynamicModelDesc* model(std::uint32_t offset)
    {
        if (const auto existing = models_by_offset.find(offset);
            existing != models_by_offset.end()) {
            return existing->second;
        }
        require(active_models.insert(offset).second,
                "Scene model descriptor cycle detected");
        require(models.size() + active_models.size() <= kMaxSceneModels,
                "Scene model count exceeds its bound");
        record(offset, 16, "Scene DynamicModelDesc is truncated");
        const auto joint = pointer(offset, 64,
                                   "Scene DynamicModelDesc joint is missing");
        std::unique_ptr<DatNativeJoint> graph;
        try {
            graph = std::make_unique<DatNativeJoint>(archive, joint);
        } catch (const DatError& error) {
            throw DatError("Scene DynamicModelDesc at joint " +
                           std::to_string(joint) + ": " + error.what());
        }
        char error[256]{};
        auto* native = melee_web_native_joint_hydrate(&graph->graph(), error,
                                                      sizeof(error));
        require(native, error[0] ? error : "Scene native joint hydration failed");
        natives.push_back(native);
        auto* descriptor = static_cast<HSD_Joint*>(
            melee_web_native_joint_descriptor(native, error, sizeof(error)));
        require(descriptor,
                error[0] ? error : "Scene native joint descriptor is missing");

        auto* result = make<DynamicModelDesc>();
        models_by_offset.emplace(offset, result);
        result->joint = descriptor;
        if (const auto table = archive->pointer(offset + 4, 4)) {
            const auto values = joint_animations(*table, graph->graph());
            auto** published = make<HSD_AnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), published);
            result->anims = published;
        }
        if (const auto table = archive->pointer(offset + 8, 4)) {
            const auto values = material_animations(*table, graph->graph());
            auto** published = make<HSD_MatAnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), published);
            result->matanims = published;
        }
        if (const auto table = archive->pointer(offset + 12, 4)) {
            const auto values = shape_animations(*table, graph->graph());
            auto** published = make<HSD_ShapeAnimJoint*>(values.size() + 1);
            std::copy(values.begin(), values.end(), published);
            result->shapeanims = published;
        }
        graphs.push_back(std::move(graph));
        active_models.erase(offset);
        return result;
    }

    void decode_models(std::uint32_t table)
    {
        pointer_table(table, "Scene model table is invalid",
                      [&](std::uint32_t root) { models.push_back(model(root)); }, 16,
                      true);
        require(!models.empty(), "Scene has no DynamicModelDesc models");
        published_model_table = make<DynamicModelDesc*>(models.size() + 1);
        std::copy(models.begin(), models.end(), published_model_table);
        scene.models = published_model_table;
    }

    void decode_model_table_root(std::uint32_t table)
    {
        decode_models(table);
    }

    void decode_single_model_root(std::uint32_t root)
    {
        models.push_back(model(root));
        published_single_model = models.back();
    }

    void decode_cameras(std::uint32_t table)
    {
        const std::uint32_t end = archive->next_target_offset(table);
        require(end > table && (end - table) % kSceneCameraRecordBytes == 0,
                "Scene camera table extent is invalid");
        const std::size_t count = (end - table) / kSceneCameraRecordBytes;
        require(count > 0 && count <= kMaxSceneCameras,
                "Scene camera table exceeds its bound");
        cameras.reserve(count);
        for (std::uint32_t offset = table; offset < end;
             offset += static_cast<std::uint32_t>(kSceneCameraRecordBytes)) {
            SceneDesc::SceneCameraDesc entry{};
            entry.desc = camera(pointer(offset, 48,
                                        "Scene camera table entry is missing"));
            if (const auto animations = archive->pointer(offset + 4, 4)) {
                std::vector<HSD_CameraAnim*> values;
                pointer_table(*animations, "Scene camera animation table is invalid",
                              [&](std::uint32_t root) {
                                  values.push_back(camera_animation(root));
                              },
                              12);
                auto** published = make<HSD_CameraAnim*>(values.size() + 1);
                std::copy(values.begin(), values.end(), published);
                entry.anims = published;
            }
            cameras.push_back(entry);
        }
        scene.cameras = make<SceneDesc::SceneCameraDesc>(cameras.size());
        std::copy(cameras.begin(), cameras.end(), scene.cameras);
    }

    void decode_lights(std::uint32_t table)
    {
        pointer_table(table, "Scene light table is invalid",
                      [&](std::uint32_t root) {
                          record(root, kSceneLightListRecordBytes,
                                 "Scene light list is truncated");
                          auto* entry = make<SceneDesc::LightList>();
                          entry->desc = light(pointer(root, 28,
                                                       "Scene light list descriptor is missing"));
                          if (const auto animations = archive->pointer(root + 4, 4)) {
                              std::vector<HSD_LightAnim*> values;
                              pointer_table(*animations,
                                            "Scene light animation table is invalid",
                                            [&](std::uint32_t animation) {
                                                values.push_back(light_animation(animation));
                                            },
                                            16);
                              auto** published = make<HSD_LightAnim*>(values.size() + 1);
                              std::copy(values.begin(), values.end(), published);
                              entry->anims = published;
                          }
                          light_lists.push_back(entry);
                      },
                      kSceneLightListRecordBytes);
        require(!light_lists.empty(), "Scene light table is empty");
        scene.lights = make<SceneDesc::LightList*>(light_lists.size() + 1);
        std::copy(light_lists.begin(), light_lists.end(), scene.lights);
    }

    void decode_fogs(std::uint32_t table)
    {
        const std::uint32_t end = archive->next_target_offset(table);
        require(end > table && (end - table) % kSceneFogRecordBytes == 0,
                "Scene fog table extent is invalid");
        const std::size_t count = (end - table) / kSceneFogRecordBytes;
        require(count > 0 && count <= kMaxSceneCameras,
                "Scene fog table exceeds its bound");
        fogs.reserve(count);
        for (std::uint32_t offset = table; offset < end;
             offset += static_cast<std::uint32_t>(kSceneFogRecordBytes)) {
            SceneDesc::SceneFogDesc entry{};
            entry.desc = fog(pointer(offset, 20,
                                     "Scene fog table descriptor is missing"));
            if (const auto animations = archive->pointer(offset + 4, 4)) {
                std::vector<HSD_CameraAnim*> values;
                pointer_table(*animations, "Scene fog animation table is invalid",
                              [&](std::uint32_t root) {
                                  values.push_back(camera_animation(root));
                              },
                              12);
                auto** published = make<HSD_CameraAnim*>(values.size() + 1);
                std::copy(values.begin(), values.end(), published);
                entry.anims = published;
            }
            fogs.push_back(entry);
        }
        scene.fogs = make<SceneDesc::SceneFogDesc>(fogs.size());
        std::copy(fogs.begin(), fogs.end(), scene.fogs);
    }

    void decode(std::uint32_t root)
    {
        record(root, 16, "SceneDesc root is truncated");
        decode_models(pointer(root, 4, "SceneDesc model table is missing"));
        decode_cameras(pointer(root + 4, 8,
                              "SceneDesc camera table is missing"));
        if (const auto lights = archive->pointer(root + 8, 4))
            decode_lights(*lights);
        if (const auto fogs = archive->pointer(root + 12, 8))
            decode_fogs(*fogs);
    }
};

DatScene::DatScene(std::shared_ptr<const DatArchive> archive,
                   std::string_view public_symbol, DatSceneRootKind root_kind)
    : storage_(std::make_unique<Storage>())
{
    require(bool(archive), "Scene archive is missing");
    require(!public_symbol.empty(), "Scene public symbol is empty");
    storage_->archive = std::move(archive);
    storage_->symbol = public_symbol;
    storage_->root_kind = root_kind;
    require(root_kind == DatSceneRootKind::SceneDesc ||
                root_kind == DatSceneRootKind::DynamicModelTable ||
                root_kind == DatSceneRootKind::DynamicModel,
            "Scene root kind is invalid");
    const auto& symbol = find_symbol(*storage_->archive, public_symbol);
    switch (root_kind) {
    case DatSceneRootKind::SceneDesc:
        storage_->decode(symbol.data_offset);
        break;
    case DatSceneRootKind::DynamicModelTable:
        storage_->decode_model_table_root(symbol.data_offset);
        break;
    case DatSceneRootKind::DynamicModel:
        storage_->decode_single_model_root(symbol.data_offset);
        break;
    }
}

DatScene::~DatScene() = default;

SceneDesc* DatScene::descriptor() const noexcept
{
    if (storage_->root_kind != DatSceneRootKind::SceneDesc) return nullptr;
    return const_cast<SceneDesc*>(&storage_->scene);
}

DynamicModelDesc** DatScene::model_table() const noexcept
{
    if (storage_->root_kind != DatSceneRootKind::DynamicModelTable)
        return nullptr;
    return storage_->published_model_table;
}

DynamicModelDesc* DatScene::single_model() const noexcept
{
    if (storage_->root_kind != DatSceneRootKind::DynamicModel)
        return nullptr;
    return storage_->published_single_model;
}

std::string_view DatScene::symbol_name() const noexcept
{
    return storage_->symbol;
}

std::size_t DatScene::model_count() const noexcept
{
    return storage_->models.size();
}

std::size_t DatScene::camera_count() const noexcept
{
    return storage_->cameras.size();
}

std::size_t DatScene::light_list_count() const noexcept
{
    return storage_->light_lists.size();
}

std::size_t DatScene::fog_count() const noexcept
{
    return storage_->fogs.size();
}

} // namespace melee_web
