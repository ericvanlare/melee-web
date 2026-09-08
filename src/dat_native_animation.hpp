#pragma once
#include "dat_native_joint.hpp"
namespace melee_web {
// Owned original HSD_AnimJoint/AObj/FObj descriptors for ordinary joint SRT
// and node/branch visibility. Keep alive until every source user is removed.
class DatNativeAnimation {
public:
    DatNativeAnimation(std::shared_ptr<const DatArchive>,uint32_t root,const MeleeWebNativeGraph&);
    ~DatNativeAnimation();
    DatNativeAnimation(const DatNativeAnimation&)=delete;
    DatNativeAnimation& operator=(const DatNativeAnimation&)=delete;
    void* descriptor()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
