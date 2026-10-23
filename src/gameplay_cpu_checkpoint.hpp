#pragma once
#include "gameplay_cpu_commit.hpp"
#include "gameplay_match_session.hpp"
#include <array>
#include <memory>
#include <vector>
namespace melee_web {
/* Private fixed-one-tick CPU capsule. It owns pre/post evidence and typed
 * snapshots, never a renderer or arbitrary Wasm-memory snapshot. The fixture
 * owns input/PCM/phase comparison and enforces runtime/Asyncify/GPU boundaries.
 * After begin_record, disarm BEFORE ordinary source teardown; retain this
 * capsule until source_owners_retired succeeds, then destroy it. */
/* Private capture-only refusal evidence. status1 is a refusal, not a byte
 * mismatch. detail is CpuCaptureRefusal except Payload domain, where it is
 * the actual MeleeWebSourceMemoryPayloadSnapshotStatus. Opaque null results
 * are named as such; text does not invent an internal owner failure. */
enum class CpuCaptureRefusal : std::uint32_t {
    CapsuleAllocation=1, MissingStatics=2, OwnerTuple=3, SourceFrameWrap=4,
    NullSnapshot=5, StaticCapture=6, AudioGeneration=7, AllocationException=8,
    UnexpectedException=9
};
struct CpuCaptureDiagnostic {
    CpuComparison comparison{};
    std::array<char,160> message{};
};
class GameplayCpuCheckpoint final {
public:
    static std::unique_ptr<GameplayCpuCheckpoint> capture(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&) noexcept;
    ~GameplayCpuCheckpoint();
    bool begin_record(const GameplayMatchSession&) noexcept;
    bool seal_post(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore(const GameplayMatchSession&) noexcept;
    bool finish_replay(const GameplayMatchSession&) noexcept;
    bool seal_drawn_reference(const GameplayMatchSession&) noexcept;
    bool matches_drawn_reference(const GameplayMatchSession&) noexcept;
    void disarm() noexcept;
    const CpuComparison& comparison() const noexcept {return comparison_;}
    std::uint32_t checked_domains() const noexcept {return checked_domains_;}
    std::uint32_t matched_domains() const noexcept {return matched_domains_;}
    bool source_owners_retired(const GameplayMatchSession*) noexcept;
private:
    enum class Phase { Captured, Recording, Sealed, Replaying, Finished, Failed, Retired };
    struct PostBytes {const void* address{};std::vector<std::byte> bytes;CpuComparisonDomain domain{};std::uint32_t kind{};};
    GameplayCpuCheckpoint()=default;
    bool same_owner(const GameplayMatchSession&) const noexcept;
    using AudioRanges=std::array<MeleeWebAudioOwnedRange,MELEE_WEB_AUDIO_OWNED_RANGE_MAX>;
    bool capture_post_bytes(const GameplayMatchSession&,std::vector<PostBytes>&,AudioRanges&,std::size_t&);
    bool matches_post_bytes(const GameplayMatchSession&,const std::vector<PostBytes>&,const AudioRanges&,std::size_t) noexcept;
    void release() noexcept;
    GameplayMatchCpuOwners owner_{};
    GameplayCpuCommitOwners commit_{};
    std::unique_ptr<GameplayCpuStatics> statics_;
    MeleeWebSourceMemoryPayloadSnapshot* post_payload_{};
    MeleeWebMatchFlowCpuSnapshot* post_flow_{};
    std::vector<PostBytes> post_bytes_;
    std::array<MeleeWebAudioOwnedRange,MELEE_WEB_AUDIO_OWNED_RANGE_MAX> audio_ranges_{};
    std::size_t audio_range_count_{};
    // Read-only expected endpoint; only commit_ owns restore authority.
    MeleeWebSourceMemoryPayloadSnapshot* drawn_payload_{};
    MeleeWebMatchFlowCpuSnapshot* drawn_flow_{};
    std::vector<PostBytes> drawn_bytes_;
    AudioRanges drawn_audio_ranges_{};
    std::size_t drawn_audio_range_count_{};
    std::vector<std::byte> drawn_statics_;
    bool drawn_sealed_=false;
    std::uint64_t audio_generation_{};
    std::uint32_t post_frame_{};
    Phase phase_=Phase::Captured;
    bool source_step_possible_=false;
    CpuComparison comparison_{};
    std::uint32_t checked_domains_=0,matched_domains_=0;
};
}
