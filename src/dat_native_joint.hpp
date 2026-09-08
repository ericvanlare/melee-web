#pragma once
#include "hsd_native_joint.h"
#include "rigid_model.hpp"
#include <memory>

namespace melee_web {
// Full strict graph validation reuses the existing checked static/envelope
// metadata importer. Original native classes, rather than PreparedScene, own
// runtime joint transforms and materials. No omitted-pass graph is accepted.
class DatNativeJoint {
public:
    DatNativeJoint(std::shared_ptr<const DatArchive> archive, uint32_t root_offset);
    ~DatNativeJoint();
    DatNativeJoint(const DatNativeJoint&) = delete;
    DatNativeJoint& operator=(const DatNativeJoint&) = delete;
    [[nodiscard]] const MeleeWebNativeGraph& graph() const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
