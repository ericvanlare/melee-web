#pragma once
#include "gameplay_action_store.hpp"
#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_interval.h"
#include "gameplay_match_session.hpp"
#include <array>
#include <memory>
#include <vector>
namespace melee_web {
class GameplayCpuEndpointReader;
/* Private fixed one/two/seven-tick CPU capsule. It owns pre/post evidence and typed
 * snapshots, never a renderer or arbitrary Wasm-memory snapshot. The fixture
 * owns input/PCM/phase comparison and enforces runtime/Asyncify/GPU boundaries.
 * After begin_record, retain this capsule through ordinary teardown unless a
 * clean confirmed endpoint proves its action leases have permanent owners. */
/* Private capture-only refusal evidence. status1 is a refusal, not a byte
 * mismatch. detail is CpuCaptureRefusal except Payload domain, where it is
 * the actual MeleeWebSourceMemoryPayloadSnapshotStatus. Opaque null results
 * are named as such; text does not invent an internal owner failure. */
enum class CpuCaptureRefusal : std::uint32_t {
    CapsuleAllocation=1, MissingStatics=2, OwnerTuple=3, SourceFrameWrap=4,
    NullSnapshot=5, StaticCapture=6, AudioGeneration=7, AllocationException=8,
    UnexpectedException=9, InvalidSpan=10, LObjOwner=11, TObjOwner=12,
    ParticleOwner=13
};
struct CpuCaptureDiagnostic {
    CpuComparison comparison{};
    std::array<char,160> message{};
};
enum class CpuCheckpointSpan : std::uint32_t { OneTick=1, TwoTicks=2, SevenTicks=7 };
class GameplayCpuCheckpoint final {
public:
    static std::unique_ptr<GameplayCpuCheckpoint> capture(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&,CpuCheckpointSpan=CpuCheckpointSpan::OneTick) noexcept;
    /* Private reusable-loop entry point. The legacy enum API above retains its
     * historical 1/2/7 whitelist; this checked path admits actual spans 1..7. */
    static std::unique_ptr<GameplayCpuCheckpoint> capture_span(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&,std::uint32_t span) noexcept;
    /* Private match-scoped retained history: one immutable PRE, at most three
     * forward incarnations and two corrections within seven actual ticks. */
    static std::unique_ptr<GameplayCpuCheckpoint> capture_history(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&) noexcept;
    ~GameplayCpuCheckpoint();
    bool begin_record(const GameplayMatchSession&) noexcept;
    bool seal_post(const GameplayMatchSession&) noexcept;
    /* Intermediate boundary only: no final slot/cache or typed-byte claim. */
    /* Pure owned pending HPS proof, only between the PRE and END frames. */
    bool interior_hps_pending(const GameplayMatchSession&) const noexcept;
    bool interval_boundary(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore(const GameplayMatchSession&) noexcept;
    bool finish_replay(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore_corrected(const GameplayMatchSession&) noexcept;
    bool finish_corrected(const GameplayMatchSession&) noexcept;
    bool begin_history_incarnation(const GameplayMatchSession&) noexcept;
    bool seal_history_incarnation(const GameplayMatchSession&) noexcept;
    bool history_endpoint_admit(const GameplayMatchSession&) const noexcept;
    /* Archive a predicted endpoint before PRE restore. This closes only the
     * reusable CPU static bank; ActionStore remains open for correction. */
    bool archive_history_endpoint(const GameplayMatchSession&) noexcept;
    /* Close ActionStore only after the endpoint is selected (corrected, or
     * an uncorrected final endpoint) and its complete receipt was copied. */
    bool close_history_endpoint(const GameplayMatchSession&) noexcept;
    GameplayCpuCommitResult restore_history_pre(const GameplayMatchSession&) noexcept;
    bool finish_history_replay(const GameplayMatchSession&) noexcept;
    bool retire_history_confirmed(const GameplayMatchSession&) noexcept;
    GameplayActionStoreHistory* history_actions() const noexcept {
        return history_actions_.get();
    }
    CpuStaticEndpointHeader history_static_header() const noexcept;
    CpuStaticEndpointRow history_static_row(std::uint64_t index) const noexcept;
    bool seal_drawn_reference(const GameplayMatchSession&) noexcept;
    bool matches_drawn_reference(const GameplayMatchSession&) noexcept;
    // Release-safety proof only; host independently admits CPU/DRAW/raw
    // comparisons and GPU/Asyncify idle before calling this method.
    bool retire_confirmed(const GameplayMatchSession&) noexcept;
    void disarm() noexcept;
    /* Read-only current-endpoint evidence, bound to this live capsule's exact
     * interval identity. A detached capsule never exposes process-global state. */
    const MeleeWebCpuMotorEvidence* motor_evidence(
        const GameplayMatchSession&) const noexcept;
    const MeleeWebTObjWriterEvidence* tobj_writer_witness() const noexcept {return melee_web_tobj_writer_evidence(tobj_writer_session_);}
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
    friend class GameplayCpuEndpointReader;
#if defined(MELEE_WEB_CPU_ENDPOINT_TEST)
    friend struct GameplayCpuEndpointTestFixture;
#endif
    enum class Phase { Captured, Recording, Sealed, Replaying, Correcting, CorrectedSealed,
                       HistoryReady, HistoryRecording, HistorySealed,
                       HistoryCorrecting, HistoryCorrectedSealed,
                       Finished, Failed, ConfirmedDetached, Retired };
    struct PostBytes {const void* address{};std::vector<std::byte> bytes;CpuComparisonDomain domain{};std::uint32_t kind{};};
    explicit GameplayCpuCheckpoint(std::uint32_t span,bool reusable_loop)
        :span_(span),reusable_loop_(reusable_loop){}
    explicit GameplayCpuCheckpoint(CpuCheckpointSpan span)
        :GameplayCpuCheckpoint(static_cast<std::uint32_t>(span),false){}
    static std::unique_ptr<GameplayCpuCheckpoint> capture_checked(
        const GameplayMatchSession&,std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&,std::uint32_t span,bool allow_variable,
        bool history_mode=false) noexcept;
    const std::uint32_t span_;
    const bool reusable_loop_;
    std::uint64_t motor_token_{};
    bool same_owner(const GameplayMatchSession&) const noexcept;
    using AudioRanges=std::array<MeleeWebAudioOwnedRange,MELEE_WEB_AUDIO_OWNED_RANGE_MAX>;
    bool capture_post_bytes(const GameplayMatchSession&,std::vector<PostBytes>&,AudioRanges&,std::size_t&);
    bool matches_post_bytes(const GameplayMatchSession&,const std::vector<PostBytes>&,const AudioRanges&,std::size_t) noexcept;
    void release() noexcept;
    GameplayMatchCpuOwners owner_{};
    GameplayCpuCommitOwners commit_{};
    std::unique_ptr<GameplayCpuStatics> statics_;
    std::unique_ptr<GameplayActionStoreHistory> history_actions_;
    bool history_mode_=false;
    std::uint64_t history_incarnation_{};
    std::uint32_t history_start_frame_{},history_end_frame_{},history_restore_end_{};
    std::uint32_t history_restore_count_{};
    bool history_endpoint_archived_{},history_endpoint_closed_{};
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
    bool confirmed_disarmed_=false;
    CpuComparison comparison_{};
    std::uint64_t lobj_writer_session_=0,tobj_writer_session_=0;
    MeleeWebClassPieceAnchor class_piece_anchor_{};
    MeleeWebClassPieceWitness class_piece_witness_{};
    MeleeWebClassPieceCaptureWitness class_piece_capture_witness_{};
    MeleeWebSourcePayloadFailureWitness payload_failure_witness_{};
    MeleeWebSourceAllocationOriginWitness allocation_origin_witness_{};
    std::uint32_t checked_domains_=0,matched_domains_=0;
};
}
