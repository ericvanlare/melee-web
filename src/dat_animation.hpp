#pragma once

#include "dat_archive.hpp"
#include "hsd_animation_bridge.h"

#include <memory>

namespace melee_web {

struct DatAnimationTrack {
    std::uint32_t descriptor_offset;
    std::uint16_t start_frame;
    std::uint8_t type, value_format, slope_format;
    std::vector<std::uint8_t> bytes;
};

// Fighter FigaTree, not an HSD_AnimJoint tree. Counts correspond to consecutive
// fighter animation parts, whose mapping to the model must be established by
// the caller. Mario Wait1 and the default Mario model both use 61 preorder nodes.
struct DatAnimation {
    static constexpr std::size_t max_nodes = 140;
    static constexpr std::size_t max_tracks = max_nodes * 9;
    static constexpr std::size_t max_stream_bytes = 4U * 1024U * 1024U;
    static constexpr float max_frames = 65535.0f;

    std::uint32_t descriptor_offset, tree_type, flags;
    float end_frame;
    std::vector<std::uint8_t> node_counts;
    std::vector<DatAnimationTrack> tracks;

    DatAnimation(const DatArchive& archive, std::uint32_t root);
};

using AnimationPose = MeleeWebAnimationPose;

// Owns validated streams and host HSD runtime state. Original aobj.c/fobj.c and
// spline.c perform frame control and interpolation. The callback updates only
// ordinary SRT channels; gameplay blending, visibility and events are excluded.
// This preserves the source path, not a claim of bit-exact PowerPC output.
class HsdAnimation {
public:
    HsdAnimation(const DatAnimation& data, std::span<const AnimationPose> bind_pose);
    ~HsdAnimation();
    HsdAnimation(const HsdAnimation&) = delete;
    HsdAnimation& operator=(const HsdAnimation&) = delete;
    HsdAnimation(HsdAnimation&&) noexcept;
    HsdAnimation& operator=(HsdAnimation&&) noexcept;

    void request(float frame);
    void advance();
    [[nodiscard]] std::span<const AnimationPose> pose() const noexcept;
    [[nodiscard]] float frame() const noexcept;
    [[nodiscard]] bool finished() const noexcept;

private:
    std::unique_ptr<MeleeWebAnimation, decltype(&melee_web_animation_destroy)> animation_;
    std::size_t node_count_;
};

// Viewer policy for an already-requested clip: consume one animation tick and
// evaluate frame zero in that same tick when a nonlooping clip reaches its end.
// This avoids adding an extra held frame to each inspection playback cycle.
void advance_inspection_loop(HsdAnimation& animation);

} // namespace melee_web
