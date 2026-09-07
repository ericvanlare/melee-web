#include "dat_animation.hpp"

namespace melee_web {
HsdAnimation::HsdAnimation(const DatAnimation& data, std::span<const AnimationPose> bind_pose)
    : animation_(nullptr, melee_web_animation_destroy), node_count_(data.node_counts.size())
{
    std::vector<MeleeWebAnimationTrack> tracks;
    tracks.reserve(data.tracks.size());
    for (const auto& track : data.tracks)
        tracks.push_back({track.bytes.data(), track.bytes.size(), track.start_frame,
                          track.type, track.value_format, track.slope_format});
    char error[256];
    animation_.reset(melee_web_animation_create(data.tree_type, data.flags, data.end_frame,
        data.node_counts.data(), data.node_counts.size(), tracks.data(), tracks.size(),
        bind_pose.data(), bind_pose.size(), error, sizeof(error)));
    if (!animation_) throw DatError(error);
}

HsdAnimation::~HsdAnimation() = default;
HsdAnimation::HsdAnimation(HsdAnimation&&) noexcept = default;
HsdAnimation& HsdAnimation::operator=(HsdAnimation&&) noexcept = default;

void HsdAnimation::request(float frame)
{
    char error[256];
    if (!melee_web_animation_request(animation_.get(), frame, error, sizeof(error))) throw DatError(error);
}

void HsdAnimation::advance()
{
    char error[256];
    if (!melee_web_animation_advance(animation_.get(), error, sizeof(error))) throw DatError(error);
}

std::span<const AnimationPose> HsdAnimation::pose() const noexcept
{
    return {melee_web_animation_pose(animation_.get()), animation_ ? node_count_ : 0};
}
float HsdAnimation::frame() const noexcept { return melee_web_animation_frame(animation_.get()); }
bool HsdAnimation::finished() const noexcept { return melee_web_animation_finished(animation_.get()) != 0; }

void advance_inspection_loop(HsdAnimation& animation)
{
    animation.advance();
    if (animation.finished()) {
        animation.request(0);
        animation.advance();
    }
}
} // namespace melee_web
