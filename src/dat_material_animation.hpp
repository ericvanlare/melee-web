#pragma once
#include "dat_native_joint.hpp"

namespace melee_web {
enum class TextureIndexValidation {
    AllEncodedValues,
    // Preserve authoring values that the source does not select. The native
    // TObj adapter must check every dispatched index before table access.
    DispatchedValues,
    // The source consumer pins its TObj AObj to its initial frame before the
    // first update (ftAnim_80070200). Validate that selected image/palette
    // pair while retaining every authored animation descriptor and stream.
    StaticSourceFrameZero,
};
// Owned native HSD_MatAnimJoint descriptors, checked against the exact model
// topology. Texture-image/palette indices retain original CON/LIN/SPL0/SPL/KEY
// interpolation and are range-checked over their source curves before the
// original integer table selection. Numeric texture transforms/blend and
// material alpha use the original numeric channels; RGB/TEV uses original
// interpolation with guarded u8 conversion. Other active material/render
// channels are rejected explicitly. Keep this owner alive until all JObjs
// using its descriptors have been destroyed.
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
