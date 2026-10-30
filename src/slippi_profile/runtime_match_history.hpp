// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC)

#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_cpu_endpoint.hpp"
#include "gameplay_cpu_statics.hpp"
#include "gameplay_match_session.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace melee_web {

/* This is an ordinary-runtime owner for one bounded late-input correction.
 * The menu/runtime supplies the exact validated statics table and callbacks
 * that read its real runtime/Aurora owners. It does not configure a source
 * catalog, poll input, or claim that an Aurora submission is GPU-idle. */
enum class RuntimeMatchHistoryCommand : std::uint32_t {
    Capture=1, Begin=2, Forward=3, Present=4, AdmitIdle=5, Restore=6,
    Correct=7, CloseEndpoint=8, Retire=9, Cleanup=10,
};

enum class RuntimeMatchHistoryGatePoint : std::uint32_t {
    CapturePre=1, CaptureComplete=2, Arm=3, EndpointBegin=4,
    EndpointComplete=5, ForwardBefore=6, ForwardAfter=7,
    RestoreBefore=8, RestoreAfter=9, CorrectBefore=10, CorrectAfter=11,
    PresentSubmit=12, PresentComplete=13, Close=14, Retire=15,
};

enum class RuntimeMatchHistoryGateResult : std::uint32_t {
    Admitted=0, WrongFrame=1, MatchOwnerChanged=2, SourceFilesBusy=3,
    RuntimeBusy=4, RendererBusy=5, IdentityRefused=6, OtherOwnerRefused=7,
};

using RuntimeMatchHistoryAudioStep = bool (*)(GameplayMatchSession&, void*) noexcept;

using RuntimeMatchHistoryBoundaryCheck = RuntimeMatchHistoryGateResult (*) (
    const GameplayMatchSession&, RuntimeMatchHistoryGatePoint,
    std::uint32_t expected_source_frame, void* context) noexcept;

/* Submitted means the ordinary menu owner completed its real
 * aurora_begin_frame / source draw / aurora_end_frame path. It does not mean
 * queued GPU work has completed; that is a separate boundary admission. */
enum class RuntimeMatchHistoryPresentResult : std::uint32_t {
    Submitted=0, Refused=1,
};
using RuntimeMatchHistoryPresentSubmit = RuntimeMatchHistoryPresentResult (*) (
    GameplayMatchSession&, std::uint32_t expected_source_frame,
    void* context) noexcept;

enum class RuntimeMatchHistoryPhase : std::uint32_t {
    Captured=1, CapturedPreCopied=2, ArmedPrePending=3, Armed=4,
    PredictedSealed=5, PredictedCpuCopied=6, PredictedDrawPending=7,
    PredictedDrawIdle=8, PredictedComplete=9, RestoredPrePending=10,
    Restored=11, CorrectedSealed=12, CorrectedCpuCopied=13,
    CorrectedDrawPending=14, CorrectedDrawIdle=15, CorrectedComplete=16,
    Closed=17, Retired=18, Failed=19, MatchClosed=20,
};

enum class RuntimeMatchHistoryFailure : std::uint32_t {
    None=0, Boundary=1, Capture=2, Owner=3, Phase=4, Input=5,
    SourceTick=6, Seal=7, Endpoint=8, Restore=9, Correction=10,
    Present=11, Close=12, Retire=13, Teardown=14,
};

struct RuntimeMatchHistoryMetadata {
    std::uint32_t schema=1;
    std::uint32_t bytes=64u;
    std::uint32_t phase{};
    std::uint32_t first_failure{};
    std::uint32_t pre_frame{};
    std::uint32_t current_frame{};
    std::uint32_t sealed_end{};
    std::uint32_t incarnation{};
    std::uint32_t restore_count{};
    std::uint32_t endpoint_mask{};
    std::uint32_t forward_steps{};
    std::uint32_t replay_steps{};
    std::uint64_t poll_serial{};
    std::uint32_t pre_audio_phase{}, current_audio_phase{};
};
static_assert(sizeof(RuntimeMatchHistoryMetadata)==64);

/* One PRE, one actual forward source tick, at most one restored-PRE correction,
 * and one source tick of actual elapsed time. This first runtime adapter is
 * intentionally narrower than the underlying three-incarnation/two-restore
 * capsule. A failed or incomplete history remains owned until match teardown. */
class RuntimeMatchHistory final {
public:
    using View=GameplayCpuEndpointView;
    using Provenance=GameplayCpuEndpointProvenance;
    using Domain=GameplayCpuEndpointDomain;
    using RowKind=GameplayCpuEndpointRowKind;

    static std::unique_ptr<RuntimeMatchHistory> capture(
        GameplayMatchSession&, std::unique_ptr<GameplayCpuStatics>,
        CpuCaptureDiagnostic&, RuntimeMatchHistoryBoundaryCheck,
        RuntimeMatchHistoryPresentSubmit, RuntimeMatchHistoryAudioStep,
        unsigned& audio_phase, void* callback_context) noexcept;
    ~RuntimeMatchHistory();
    RuntimeMatchHistory(const RuntimeMatchHistory&)=delete;
    RuntimeMatchHistory& operator=(const RuntimeMatchHistory&)=delete;

    /* PRE evidence must be copied/revalidated before this arms incarnation 1.
     * Forward consumes the ordinary loop's already-polled raw PAD row and
     * serial. The remote prediction is decoded from the canonical 11-byte PAD
     * format here; correction later uses the retained row and never polls. */
    bool begin() noexcept;
    bool forward_from_poll(const PADStatus raw[PAD_MAX_CONTROLLERS],
                           std::uint64_t poll_serial,
                           unsigned local_port, unsigned remote_port,
                           const std::uint8_t* predicted_remote,
                           unsigned predicted_remote_bytes) noexcept;
    bool restore_pre() noexcept;
    bool correct_from_retained(const std::uint8_t* late_remote,
                               unsigned late_remote_bytes) noexcept;

    /* The ordinary owner's callback performs exactly one real source draw
     * inside Aurora begin/end. Completion is admitted separately only after
     * the host's actual GPU/Asyncify fence boundary has become idle. */
    bool submit_present() noexcept;
    bool admit_present_completion() noexcept;

    /* Copy the entire strict 9-domain endpoint and particle sidecar batch
     * through these accessors, then call end_endpoint_batch(). That method
     * performs the reader's post-copy full owner revalidation before recording
     * any endpoint bit. */
    bool begin_endpoint_batch(View, Provenance) noexcept;
    GameplayCpuEndpointRecord endpoint_record(Domain, RowKind,
                                               std::uint32_t index=0) const noexcept;
    bool endpoint_particle_header(SlippiParticleCellEndpointHeader&) const noexcept;
    bool endpoint_particle_row(std::uint32_t kind, std::uint32_t index,
                               SlippiParticleCellEndpointRow&) const noexcept;
    bool end_endpoint_batch() noexcept;

    bool close_selected_endpoint() noexcept;
    bool retire_confirmed() noexcept;
    /* Failure cleanup retains all capsule leases while the source match is
     * live. Call only after GameplayMatchSession::close(); its independent
     * owner/world retirement proof must succeed before releasing the capsule. */
    bool release_after_match_close() noexcept;

    void disarm_for_teardown() noexcept;
    bool endpoint_active() const noexcept { return reader_.active(); }
    const PADStatus* retained_input(unsigned kind) const noexcept;
    unsigned local_port() const noexcept { return local_port_; }
    unsigned remote_port() const noexcept { return remote_port_; }
    const char* error() const noexcept { return error_.data(); }
    RuntimeMatchHistoryMetadata metadata() const noexcept;
    RuntimeMatchHistoryPhase phase() const noexcept { return phase_; }
    RuntimeMatchHistoryFailure first_failure() const noexcept { return failure_; }
    std::uint32_t pre_frame() const noexcept { return pre_frame_; }
    std::uint32_t current_frame() const noexcept;
    std::uint32_t sealed_end() const noexcept { return sealed_end_; }
    std::uint32_t incarnation() const noexcept { return incarnation_; }
    std::uint32_t restore_count() const noexcept { return restore_count_; }
    std::uint32_t endpoint_mask() const noexcept { return endpoint_mask_; }
    std::uint64_t poll_serial() const noexcept { return poll_serial_; }
    /* Copy this value before restore/retire reuses the selected motor tape. */
    const MeleeWebCpuMotorEvidence* motor_evidence() const noexcept;

private:
    enum EndpointBit : std::uint32_t {
        CapturedPre=1u<<0, ArmedPre=1u<<1, PredictedCpu=1u<<2,
        PredictedDraw=1u<<3, RestoredPre=1u<<4, CorrectedCpu=1u<<5,
        CorrectedDraw=1u<<6,
    };
    enum class EndpointRole : std::uint32_t {
        None, CapturedPre, ArmedPre, PredictedCpu, PredictedDraw,
        RestoredPre, CorrectedCpu, CorrectedDraw,
    };

    RuntimeMatchHistory(GameplayMatchSession&, RuntimeMatchHistoryBoundaryCheck,
                        RuntimeMatchHistoryPresentSubmit, RuntimeMatchHistoryAudioStep, unsigned&, void*) noexcept;
    bool gate(RuntimeMatchHistoryGatePoint,std::uint32_t) noexcept;
    bool live_owner(std::uint32_t expected_frame) const noexcept;
    bool fail(RuntimeMatchHistoryFailure, const char* message=nullptr) noexcept;
    bool checkpoint_failure(RuntimeMatchHistoryFailure,const char*) noexcept;
    bool endpoint_role_allowed(View,Provenance,EndpointRole&) const noexcept;

    GameplayMatchSession& session_;
    RuntimeMatchHistoryBoundaryCheck boundary_{};
    RuntimeMatchHistoryPresentSubmit present_{};
    RuntimeMatchHistoryAudioStep audio_step_{};
    unsigned& audio_phase_;
    unsigned pre_audio_phase_{};
    std::array<char,256> error_{};
    void* callback_context_{};
    std::unique_ptr<GameplayCpuCheckpoint> checkpoint_;
    GameplayCpuEndpointReader reader_{};
    GameplayMatchCpuOwners owners_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_raw_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_forward_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_corrected_{};
    RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::Captured;
    RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
    EndpointRole endpoint_role_=EndpointRole::None;
    std::uint32_t pre_frame_{},sealed_end_{},incarnation_{},restore_count_{};
    std::uint32_t endpoint_mask_{};
    std::uint32_t forward_steps_{},replay_steps_{};
    std::uint32_t local_port_{},remote_port_{};
    std::uint64_t poll_serial_{};
    mutable bool endpoint_read_failure_{};
    bool has_input_{},source_step_possible_{};
};

} // namespace melee_web

#endif // MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
