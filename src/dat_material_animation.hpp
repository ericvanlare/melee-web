#pragma once
#include "dat_native_joint.hpp"

namespace melee_web {
enum class TextureIndexValidation {
    AllEncodedValues,
    // Preserve authoring values that the source does not select. The native
    // TObj adapter must check every dispatched index before table access.
    DispatchedValues,
};
// Owned native HSD_MatAnimJoint descriptors, checked against the exact model
// topology. This gate supports constant/key texture-image and palette indices plus numeric texture transforms/blend;
// material alpha uses the original numeric channel; RGB/TEV uses original interpolation with guarded u8 conversion. Other active material/render
// channels are rejected explicitly. Keep this
// owner alive until all JObjs using its descriptors have been destroyed.
class DatMaterialAnimation {
public:
    DatMaterialAnimation(std::shared_ptr<const DatArchive>, uint32_t root,
                         const MeleeWebNativeGraph& model,
                         TextureIndexValidation = TextureIndexValidation::AllEncodedValues);
    ~DatMaterialAnimation();
    DatMaterialAnimation(const DatMaterialAnimation&) = delete;
    DatMaterialAnimation& operator=(const DatMaterialAnimation&) = delete;
    void* descriptor() const noexcept;
    // Checked source array export for original grAnime bone-index consumers.
    void* indexed_descriptor() const;
    uint32_t texture_animation_count() const noexcept;
    uint32_t image_count() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
