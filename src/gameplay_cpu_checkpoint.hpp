#pragma once
#include "gameplay_cpu_commit.hpp"
#include "gameplay_match_session.hpp"
#include <array>
#include <memory>
#include <vector>
namespace melee_web {
/* Private fixed one/two-tick CPU capsule. It owns pre/post evidence and typed
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
    UnexpectedException=9, InvalidSpan=10, LObjOwner=11
};
struct CpuCaptureDiagnostic {
    CpuComparison comparison{};
    std::array<char,160> message{};
};
enum class CpuCheckpointSpan : std::uint32_t { OneTick=1, TwoTicks=2 };
class GameplayCpuCheckpoint final {
public:
    static std::unique_ptr<GameplayCpuCheckpoint> capture(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&,CpuCheckpointSpan=CpuCheckpointSpan::OneTick) noexcept;
    ~GameplayCpuCheckpoint();
    bool begin_record(const GameplayMatchSession&) noexcept;
    bool seal_post(const GameplayMatchSession&) noexcept;
    /* Intermediate boundary only: no final slot/cache or typed-byte claim. */
    bool interval_boundary(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore(const GameplayMatchSession&) noexcept;
    bool finish_replay(const GameplayMatchSession&) noexcept;
    bool seal_drawn_reference(const GameplayMatchSession&) noexcept;
    bool matches_drawn_reference(const GameplayMatchSession&) noexcept;
    void disarm() noexcept;
    const MeleeWebLObjWriterEvidence* lobj_writer_witness() const noexcept {return melee_web_lobj_writer_evidence(lobj_writer_session_);}
    const CpuComparison& comparison() const noexcept {return comparison_;}
    const MeleeWebClassPieceCaptureWitness& class_piece_capture_witness() const noexcept {return class_piece_capture_witness_;}
    const MeleeWebClassPieceWitness& class_piece_witness() const noexcept {return class_piece_witness_;}
    const MeleeWebSourceAllocationOriginWitness& allocation_origin_witness() const noexcept {return allocation_origin_witness_;}
    const MeleeWebSourcePayloadFailureWitness& payload_failure_witness() const noexcept {return payload_failure_witness_;}
    std::uint32_t checked_domains() const noexcept {return checked_domains_;}
    std::uint32_t matched_domains() const noexcept {return matched_domains_;}
    bool source_owners_retired(const GameplayMatchSession*) noexcept;
private:
    enum class Phase { Captured, Recording, Sealed, Replaying, Finished, Failed, Retired };
    struct PostBytes {const void* address{};std::vector<std::byte> bytes;CpuComparisonDomain domain{};std::uint32_t kind{};};
    explicit GameplayCpuCheckpoint(CpuCheckpointSpan span):span_(span){}
    const CpuCheckpointSpan span_;
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
    std::uint64_t lobj_writer_session_=0;
    MeleeWebClassPieceAnchor class_piece_anchor_{};
    MeleeWebClassPieceWitness class_piece_witness_{};
    MeleeWebClassPieceCaptureWitness class_piece_capture_witness_{};
    MeleeWebSourcePayloadFailureWitness payload_failure_witness_{};
    MeleeWebSourceAllocationOriginWitness allocation_origin_witness_{};
    std::uint32_t checked_domains_=0,matched_domains_=0;
};
}
