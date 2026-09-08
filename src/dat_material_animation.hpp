#pragma once
#include "dat_native_joint.hpp"

namespace melee_web {
// Owned native HSD_MatAnimJoint descriptors, checked against the exact model
// topology. This gate supports constant/key texture-image and palette indices plus numeric texture blend;
// other active material/render channels are rejected explicitly. Keep this
// owner alive until all JObjs using its descriptors have been destroyed.
class DatMaterialAnimation {
public:
    DatMaterialAnimation(std::shared_ptr<const DatArchive>, uint32_t root,
                         const MeleeWebNativeGraph& model);
    ~DatMaterialAnimation();
    DatMaterialAnimation(const DatMaterialAnimation&) = delete;
    DatMaterialAnimation& operator=(const DatMaterialAnimation&) = delete;
    void* descriptor() const noexcept;
    uint32_t texture_animation_count() const noexcept;
    uint32_t image_count() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
