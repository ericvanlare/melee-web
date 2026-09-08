#pragma once

#include "dat_native_joint.hpp"

struct HSD_ShapeAnimJoint;

namespace melee_web {

// Owns checked source HSD_ShapeAnimJoint/DObj/AObj/FObj descriptors. Shape
// geometry remains owned by DatNativeJoint and is paired with each source PObj
// in the same order used by HSD_DObjAddAnimAll/HSD_PObjAddAnimAll.
class DatShapeAnimation {
public:
    DatShapeAnimation(std::shared_ptr<const DatArchive>, uint32_t,
                      const MeleeWebNativeGraph&);
    ~DatShapeAnimation();
    DatShapeAnimation(const DatShapeAnimation&) = delete;
    DatShapeAnimation& operator=(const DatShapeAnimation&) = delete;

    HSD_ShapeAnimJoint* descriptor() const noexcept;
    size_t joint_count() const noexcept;
    size_t dobj_count() const noexcept;

private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};

}
