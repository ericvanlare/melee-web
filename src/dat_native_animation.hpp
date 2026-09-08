#pragma once
#include "dat_native_joint.hpp"
namespace melee_web {
// Owned original HSD_AnimJoint/AObj/FObj descriptors for ordinary joint SRT
// and node/branch visibility, plus PATH references to checked spline joints.
// Optional descriptor pointers are indexed by the exact checked model graph and
// borrowed from its native owner. Both owners must outlive all source users.
struct DatParticleEvent { uint32_t bank,command; };
enum class DatNativeAnimationPolicy { Transforms, ParticleDescriptors };
class DatNativeAnimation {
public:
    DatNativeAnimation(std::shared_ptr<const DatArchive>,uint32_t root,const MeleeWebNativeGraph&,
                       DatNativeAnimationPolicy = DatNativeAnimationPolicy::Transforms,
                       std::span<void* const> native_joint_descriptors = {});
    ~DatNativeAnimation();
    DatNativeAnimation(const DatNativeAnimation&)=delete;
    DatNativeAnimation& operator=(const DatNativeAnimation&)=delete;
    void* descriptor()const noexcept;
    // Original grAnime indexes descriptors directly by model bone index.
    // Requires proven complete source array layout in addition to linked topology.
    void* indexed_descriptor()const;
    // Nonempty means actual source particle callback/bank execution is required
    // before these descriptors can be requested by any runtime JObj.
    const std::vector<DatParticleEvent>& particle_events()const noexcept;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
