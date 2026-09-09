#pragma once

#include "dat_archive.hpp"

struct SceneDesc;
struct DynamicModelDesc;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace melee_web {

enum class DatSceneRootKind {
    SceneDesc,
    DynamicModelTable,
};

// Owns a source SceneDesc or DynamicModelDesc** table and every typed
// descriptor reachable from its public symbol. Model graphs and their
// joint/material/shape animations reuse the
// checked native owners. Camera/light/fog animation records are hydrated when
// they are empty; active non-model channels fail explicitly until their source
// AObj consumers are implemented.
class DatScene {
public:
    DatScene(std::shared_ptr<const DatArchive>, std::string_view public_symbol,
             DatSceneRootKind = DatSceneRootKind::SceneDesc);
    ~DatScene();
    DatScene(const DatScene&) = delete;
    DatScene& operator=(const DatScene&) = delete;

    [[nodiscard]] SceneDesc* descriptor() const noexcept;
    // DynamicModelDesc** roots such as ScInfCnt_scene_models, Stc_scemdls,
    // DmgNum_scene_models, ScInfTim_scene_models, tdsce, and lupe are
    // published separately from SceneDesc.
    // Returns nullptr when this object was constructed for a SceneDesc root.
    [[nodiscard]] DynamicModelDesc** model_table() const noexcept;
    [[nodiscard]] std::string_view symbol_name() const noexcept;
    [[nodiscard]] std::size_t model_count() const noexcept;
    [[nodiscard]] std::size_t camera_count() const noexcept;
    [[nodiscard]] std::size_t light_list_count() const noexcept;
    [[nodiscard]] std::size_t fog_count() const noexcept;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

} // namespace melee_web
