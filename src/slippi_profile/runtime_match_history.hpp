// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC)

#include "gameplay_cpu_checkpoint.hpp"
#include "gameplay_cpu_endpoint.hpp"
#include "gameplay_cpu_statics.hpp"
#include "gameplay_match_session.hpp"
#include "slippi_profile/runtime_match_history_terminal_cursor.h"
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
    Correct=7, CloseEndpoint=8, Retire=9, Cleanup=10, RebaseConfirmed=11,
    RecordingPresent=19,
    FinishConfirmedPrefix=20, CloseConfirmedPrefix=21,
    RetireConfirmedPrefix=22, RebaseConfirmedPrefix=23,
    AdoptPrefixTicket=24,
    BeginTerminalTail=26, TerminalTailTicket=27,
    TerminalTailForward=28, TerminalTailPresent=29,
    FinishTerminalTail=30,
};

enum class RuntimeMatchHistoryGatePoint : std::uint32_t {
    CapturePre=1, CaptureComplete=2, Arm=3, EndpointBegin=4,
    EndpointComplete=5, ForwardBefore=6, ForwardAfter=7,
    RestoreBefore=8, RestoreAfter=9, CorrectBefore=10, CorrectAfter=11,
    PresentSubmit=12, PresentComplete=13, Close=14, Retire=15,
    InteriorPoll=16, InteriorForwardBefore=17, InteriorForwardAfter=18,
    InteriorReplayBefore=19, InteriorReplayAfter=20,
    InteriorPresentSubmit=21, InteriorPresentComplete=22,
    InteriorRendererPrepare=23,
    TerminalForwardAfter=24, TerminalPresentSubmit=25,
    TerminalPresentComplete=26, TerminalEndpointBegin=27,
    TerminalEndpointComplete=28, TerminalClose=29, TerminalRetire=30,
    TerminalReplayAfter=31,
    TerminalTailBegin=32, TerminalTailPoll=33,
    TerminalTailForwardBefore=34, TerminalTailForwardAfter=35,
    TerminalTailPresentSubmit=36, TerminalTailPresentComplete=37,
    TerminalTailFinish=38, TerminalRestoreBefore=39,
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
    EpisodeRecording=21, EpisodePresentPending=22,
    PrefixSealed=23, PrefixCpuCopied=24, PrefixClosed=25, PrefixRetired=26,
    PresentedSealed=27, PresentedComplete=28,
    TerminalRecording=29, TerminalPresentPending=30, TerminalSealed=31,
    TerminalCpuCopied=32, TerminalPresentIdle=33, TerminalClosed=34,
    TerminalRetired=35, TerminalTailReady=36, TerminalTailTicketed=37,
    TerminalTailForwarded=38, TerminalTailPresentPending=39,
    TerminalTailPresented=40, TerminalFinished=41,
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

/* Separate private terminal evidence. Original source frames and selected
 * traversal ordinals remain distinct; the legacy input/ticket records keep
 * their existing 88/176-byte schema-1 layouts. */
struct RuntimeMatchHistoryTerminalReceipt {
    std::uint32_t schema=1,bytes=104,flags{},incarnation{};
    std::uint32_t pre_source_frame{},selected_source_frame{},pre_ordinal{},selected_ordinal{};
    std::uint32_t first_ordinal{},first_source_frame{},ticket_id{},local_port{};
    std::uint32_t remote_port{},reserved{};
    std::uint64_t pre_completed_tick{},selected_completed_tick{},first_completed_tick{};
    std::uint64_t world_generation{},context_epoch{},poll_serial{};
};
static_assert(sizeof(RuntimeMatchHistoryTerminalReceipt)==104);

/* The terminal tail outlives the seven-row rollback capsule. Its current
 * source frame remains the authored ending frame while `input_ordinal` and
 * `completed_tick` advance independently. Adopted rows retain their original
 * poll identity and raw PAD in this separate ABI. */
struct RuntimeMatchHistoryTerminalTailTicket {
    std::uint32_t schema=1,bytes=160,flags{},ticket_id{},request_sequence{};
    std::uint32_t source_frame{},input_ordinal{},origin_ticket_id{};
    std::uint32_t origin_source_frame{},incarnation{},origin_incarnation{};
    std::uint32_t local_port{},remote_port{},reserved{};
    std::uint64_t completed_tick{},poll_serial{};
    std::array<std::uint8_t,44> raw_pad{};
    std::array<std::uint8_t,44> applied_pad{};
};
static_assert(sizeof(RuntimeMatchHistoryTerminalTailTicket)==160);

/* Private optional episode receipts. The legacy 168-byte input ABI is kept
 * separate; these fixed records bind actual polls and applied rows to one
 * captured owner without allocating after PRE. */
struct RuntimeMatchHistoryPollTicket {
    std::uint32_t schema=1, bytes=88;
    std::uint32_t ticket_id{}, source_frame{}, incarnation{}, local_port{}, remote_port{}, reserved0{};
    std::uint64_t poll_serial{};
    std::array<std::uint8_t,44> raw_pad{};
    std::uint32_t reserved1{};
};
static_assert(sizeof(RuntimeMatchHistoryPollTicket)==88);

struct RuntimeMatchHistoryInputReceipt {
    std::uint32_t schema=1, bytes=176;
    std::uint32_t ticket_id{}, source_frame{}, incarnation{}, local_port{}, remote_port{}, state{};
    std::uint64_t poll_serial{};
    std::array<std::uint8_t,44> raw_pad{};
    std::array<std::uint8_t,44> forward_pad{};
    std::array<std::uint8_t,44> selected_pad{};
    std::uint32_t reserved{};
};
static_assert(sizeof(RuntimeMatchHistoryInputReceipt)==176);

/* Native-owned, fixed suffix handoff created only from actual retired input
 * receipts. The menu keeps one instance across the sole prefix rebase; browser
 * arguments can read its evidence but cannot construct or edit its rows. */
class RuntimeMatchHistoryPrefixHandoff final {
public:
    RuntimeMatchHistoryPrefixHandoff() noexcept = default;
    RuntimeMatchHistoryPrefixHandoff(const RuntimeMatchHistoryPrefixHandoff&) = delete;
    RuntimeMatchHistoryPrefixHandoff& operator=(const RuntimeMatchHistoryPrefixHandoff&) = delete;
    bool active() const noexcept { return active_; }
    std::uint32_t old_pre_frame() const noexcept { return old_pre_frame_; }
    std::uint32_t confirmed_frame() const noexcept { return confirmed_frame_; }
    std::uint32_t selected_end_frame() const noexcept { return selected_end_frame_; }
    std::uint32_t old_incarnation() const noexcept { return old_incarnation_; }
    std::uint32_t finish_request_sequence() const noexcept { return finish_request_sequence_; }
    std::uint32_t rebase_request_sequence() const noexcept { return rebase_request_sequence_; }
    std::uint32_t suffix_count() const noexcept { return suffix_count_; }
    std::uint32_t suffix_cursor() const noexcept { return suffix_cursor_; }
    unsigned local_port() const noexcept { return local_port_; }
    unsigned remote_port() const noexcept { return remote_port_; }
    const GameplayMatchCpuOwners& owners() const noexcept { return owners_; }
    const RuntimeMatchHistoryInputReceipt* origin_row(std::uint32_t ordinal) const noexcept {
        return active_&&ordinal<suffix_count_?&suffix_[ordinal]:nullptr;
    }
    void clear() noexcept {
        active_=false;owners_={};old_pre_frame_=confirmed_frame_=selected_end_frame_=0;
        old_incarnation_=finish_request_sequence_=rebase_request_sequence_=0;
        suffix_count_=suffix_cursor_=local_port_=remote_port_=0;suffix_={};
    }
private:
    friend class RuntimeMatchHistory;
    bool active_{};
    GameplayMatchCpuOwners owners_{};
    std::uint32_t old_pre_frame_{},confirmed_frame_{},selected_end_frame_{};
    std::uint32_t old_incarnation_{},finish_request_sequence_{},rebase_request_sequence_{};
    std::uint32_t suffix_count_{},suffix_cursor_{},local_port_{},remote_port_{};
    std::array<RuntimeMatchHistoryInputReceipt,7> suffix_{};
};

/* Legacy mode remains one actual forward source tick and one restored-PRE
 * correction. The explicitly enabled private episode mode uses the fixed
 * seven-row receipts above across at most three source incarnations and two
 * restores. Failed or incomplete history remains owned until match teardown. */
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
        unsigned& audio_phase, void* callback_context,
        bool episode_mode=false,bool arena_mode=false) noexcept;
    ~RuntimeMatchHistory();
    RuntimeMatchHistory(const RuntimeMatchHistory&)=delete;
    RuntimeMatchHistory& operator=(const RuntimeMatchHistory&)=delete;

    /* PRE evidence must be copied/revalidated before this arms incarnation 1.
     * Forward consumes the ordinary loop's already-polled raw PAD row and
     * serial. The remote prediction is decoded from the canonical 11-byte PAD
     * format here; correction later uses the retained row and never polls. */
    bool begin() noexcept;
    bool begin_episode() noexcept;
    bool can_admit_poll_ticket(std::uint32_t ticket_id,
                               unsigned local_port, unsigned remote_port) const noexcept;
    /* Narrow evidence for servicing already-owned HPS work at a strict
     * interior source frame. This does not admit an endpoint or pump audio. */
    bool interior_hps_pending(const GameplayMatchSession&,
                              std::uint32_t expected_source_frame) const noexcept;
    bool admit_poll_ticket(std::uint32_t ticket_id,
                           const PADStatus raw[PAD_MAX_CONTROLLERS],
                           std::uint64_t poll_serial,
                           unsigned local_port, unsigned remote_port) noexcept;
    bool forward_ticket(std::uint32_t ticket_id,
                       const std::uint8_t* planned_frame,
                       unsigned planned_frame_bytes) noexcept;
    bool seal_incarnation() noexcept;
    bool replay_ticket(std::uint32_t ticket_id,
                       const std::uint8_t* planned_frame,
                       unsigned planned_frame_bytes) noexcept;
    bool finish_replay() noexcept;
    bool finish_confirmed_prefix(std::uint32_t confirmed_end) noexcept;
    bool capture_confirmed_prefix_handoff(
        std::uint32_t finish_request_sequence,
        RuntimeMatchHistoryPrefixHandoff&) const noexcept;
    bool close_confirmed_prefix() noexcept;
    bool retire_confirmed_prefix() noexcept;
    bool can_rebase_confirmed_prefix(
        std::uint32_t confirmed_end,RuntimeMatchHistoryPrefixHandoff&,
        std::uint32_t rebase_request_sequence) const noexcept;
    bool begin_continuation() noexcept;
    bool begin_episode(RuntimeMatchHistoryPrefixHandoff&) noexcept;
    bool adopt_next_prefix_ticket() noexcept;
    bool begin_terminal_tail(std::uint32_t selected_ordinal) noexcept;
    bool can_admit_terminal_tail_poll_ticket(std::uint32_t request_sequence,
                                             unsigned local_port,
                                             unsigned remote_port) const noexcept;
    bool adopt_terminal_tail_ticket(std::uint32_t request_sequence) noexcept;
    bool admit_terminal_tail_poll_ticket(
        std::uint32_t request_sequence,const PADStatus raw[PAD_MAX_CONTROLLERS],
        std::uint64_t poll_serial,unsigned local_port,unsigned remote_port) noexcept;
    bool forward_terminal_tail_ticket(std::uint32_t request_sequence,
                                      const std::uint8_t* remote_pad,
                                      unsigned remote_pad_bytes) noexcept;
    bool submit_terminal_tail_present() noexcept;
    bool terminal_tail_finish_ready(std::uint32_t& consumed_ordinal,
                                   std::uint32_t& last_input_ordinal) const noexcept;
    bool finish_terminal_tail(std::uint32_t consumed_ordinal,
                              std::uint32_t last_input_ordinal) noexcept;
    bool terminal_tail_authorized(const GameplayMatchSession&,
                                  RuntimeMatchHistoryGatePoint,
                                  std::uint32_t source_frame) const noexcept;
    const RuntimeMatchHistoryTerminalTailTicket* terminal_tail_ticket() const noexcept;
    const PADStatus* terminal_tail_applied_input() const noexcept;
    std::uint32_t terminal_tail_consumed_ordinal() const noexcept {
        return terminal_tail_input_ordinal_;
    }
    std::uint32_t terminal_tail_last_input_ordinal() const noexcept {
        return terminal_tail_last_input_ordinal_;
    }
    std::uint32_t terminal_tail_source_frame() const noexcept {
        return terminal_tail_source_frame_;
    }
    std::uint64_t terminal_tail_completed_tick() const noexcept {
        return terminal_tail_completed_tick_;
    }
    std::uint32_t terminal_tail_suffix_count() const noexcept {
        return terminal_tail_suffix_count_;
    }
    std::uint32_t terminal_tail_suffix_cursor() const noexcept {
        return terminal_tail_suffix_cursor_;
    }
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
    bool submit_recording_present() noexcept;
    bool admit_present_completion() noexcept;
    /* Opt in to a post-draw frontier after the current recording present idles. */
    bool seal_presented_frontier() noexcept;
    std::uint32_t recording_present_count() const noexcept { return recording_present_count_; }

    /* Copy the entire strict 9-domain endpoint and particle sidecar batch
     * through these accessors, then call end_endpoint_batch(). That method
     * performs the reader's post-copy full owner revalidation before recording
     * any endpoint bit. */
    bool begin_endpoint_batch(View, Provenance) noexcept;
    GameplayCpuEndpointRecord endpoint_record(Domain, RowKind,
                                               std::uint32_t index=0) const noexcept;
    bool endpoint_arena(GameplayCpuArenaEndpoint&) const noexcept;
    bool endpoint_particle_header(SlippiParticleCellEndpointHeader&) const noexcept;
    bool endpoint_particle_row(std::uint32_t kind, std::uint32_t index,
                               SlippiParticleCellEndpointRow&) const noexcept;
    bool end_endpoint_batch() noexcept;
    /* Same-stack admission for callers that publish no endpoint rows. */
    bool admit_endpoint_batch(View, Provenance) noexcept;

    bool close_selected_endpoint() noexcept;
    bool retire_confirmed() noexcept;
    bool can_rebase_confirmed(std::uint32_t end_frame) const noexcept;
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
    std::uint32_t prefix_selected_end() const noexcept { return prefix_selected_end_; }
    std::uint32_t prefix_suffix_cursor() const noexcept;
    std::uint32_t prefix_suffix_remaining() const noexcept;
    std::uint32_t incarnation() const noexcept { return incarnation_; }
    std::uint32_t restore_count() const noexcept { return restore_count_; }
    std::uint32_t endpoint_mask() const noexcept { return endpoint_mask_; }
    std::uint32_t forward_steps() const noexcept { return forward_steps_; }
    std::uint32_t replay_steps() const noexcept { return replay_steps_; }
    std::uint64_t poll_serial() const noexcept { return poll_serial_; }
    bool episode_mode() const noexcept { return episode_mode_; }
    const MeleeWebRuntimeMatchHistoryTerminalCursor& terminal_cursor() const noexcept {
        return terminal_cursor_;
    }
    RuntimeMatchHistoryTerminalReceipt terminal_receipt() const noexcept;
    bool terminal_selected_at(std::uint32_t source_frame) const noexcept;
    const RuntimeMatchHistoryPollTicket* latest_ticket() const noexcept;
    const RuntimeMatchHistoryInputReceipt* episode_input_row(std::uint32_t ordinal) const noexcept;
    /* Copy this value before restore/retire reuses the selected motor tape. */
    const MeleeWebCpuMotorEvidence* motor_evidence() const noexcept;

private:
    enum EndpointBit : std::uint32_t {
        CapturedPre=1u<<0, ArmedPre=1u<<1, PredictedCpu=1u<<2,
        PredictedDraw=1u<<3, RestoredPre=1u<<4, CorrectedCpu=1u<<5,
        CorrectedDraw=1u<<6, PrefixCpu=1u<<7, PresentedFrontier=1u<<8,
        TerminalEnd=1u<<9,
    };
    enum class EndpointRole : std::uint32_t {
        None, CapturedPre, ArmedPre, PredictedCpu, PredictedDraw,
        RestoredPre, CorrectedCpu, CorrectedDraw, PrefixCpu, PresentedFrontier,
        TerminalEnd,
    };
    enum class EndpointCompletionMode : std::uint32_t { CopiedRows, ImmediateAdmission };

    RuntimeMatchHistory(GameplayMatchSession&, RuntimeMatchHistoryBoundaryCheck,
                        RuntimeMatchHistoryPresentSubmit, RuntimeMatchHistoryAudioStep, unsigned&, void*) noexcept;
    bool gate(RuntimeMatchHistoryGatePoint,std::uint32_t) noexcept;
    bool live_owner(std::uint32_t expected_frame) const noexcept;
    bool fail(RuntimeMatchHistoryFailure, const char* message=nullptr) noexcept;
    bool checkpoint_failure(RuntimeMatchHistoryFailure,const char*) noexcept;
    bool endpoint_role_allowed(View,Provenance,EndpointRole&) const noexcept;
    bool end_endpoint_batch_impl(EndpointCompletionMode) noexcept;
    bool read_terminal_cursor(std::uint32_t expected_source_frame,
                              MeleeWebRuntimeMatchHistoryTerminalOwner&,
                              std::uint64_t& completed_tick,
                              std::uint32_t& ending,
                              std::uint32_t& complete) const noexcept;
    bool read_terminal_tail_cursor(std::uint64_t expected_completed_tick,
                                   std::uint32_t expected_source_frame,
                                   bool allow_complete) const noexcept;
    bool terminal_tail_finish_state_ready(std::uint32_t& consumed_ordinal,
                                          std::uint32_t& last_input_ordinal) const noexcept;
    bool bind_terminal_cursor() noexcept;
    bool rewind_terminal_cursor_pre() noexcept;
    bool observe_terminal_cursor(std::uint32_t source_frame_before,
                                 std::uint64_t tick_before,
                                 std::uint32_t ending_before,
                                 std::uint32_t complete_before,
                                 std::uint32_t source_frame_after) noexcept;

    GameplayMatchSession& session_;
    RuntimeMatchHistoryBoundaryCheck boundary_{};
    RuntimeMatchHistoryPresentSubmit present_{};
    RuntimeMatchHistoryAudioStep audio_step_{};
    unsigned& audio_phase_;
    unsigned pre_audio_phase_{};
    std::array<char,512> error_{};
    void* callback_context_{};
    std::unique_ptr<GameplayCpuCheckpoint> checkpoint_;
    GameplayCpuEndpointReader reader_{};
    GameplayMatchCpuOwners owners_{};
    MeleeWebRuntimeMatchHistoryTerminalCursor terminal_cursor_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_raw_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_forward_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> retained_corrected_{};
    RuntimeMatchHistoryPhase phase_=RuntimeMatchHistoryPhase::Captured;
    RuntimeMatchHistoryFailure failure_=RuntimeMatchHistoryFailure::None;
    EndpointRole endpoint_role_=EndpointRole::None;
    std::uint32_t pre_frame_{},sealed_end_{},incarnation_{},restore_count_{};
    std::uint32_t endpoint_mask_{};
    std::uint32_t forward_steps_{},replay_steps_{};
    std::uint32_t last_present_source_frame_{},recording_present_count_{};
    std::uint32_t local_port_{},remote_port_{};
    std::uint64_t poll_serial_{};
    mutable bool endpoint_read_failure_{};
    bool has_input_{},source_step_possible_{};
    bool episode_mode_{},arena_mode_{},latest_ticket_valid_{},current_ticket_pending_{};
    bool last_present_source_frame_valid_{};
    std::uint32_t segment_start_frame_{},replay_target_end_{},replay_target_ordinal_{},
                  ticket_count_{},last_ticket_id_{};
    std::uint32_t prefix_selected_end_{},prefix_suffix_cursor_{},prefix_adopted_cursor_{};
    std::uint32_t terminal_selected_source_frame_{},terminal_selected_ordinal_{};
    std::uint64_t terminal_selected_tick_{},terminal_last_presented_tick_{};
    bool prefix_handoff_imported_{},prefix_adoption_pending_{};
    bool terminal_checkpoint_sealed_{},terminal_needs_present_{};
    bool terminal_tail_active_{},terminal_tail_ticket_pending_{},
         terminal_tail_remote_pending_{},terminal_tail_adopted_pending_{},
         terminal_tail_complete_after_{};
    std::uint32_t terminal_tail_source_frame_{},terminal_tail_input_ordinal_{},
                  terminal_tail_last_input_ordinal_{},terminal_tail_incarnation_{},
                  terminal_tail_suffix_count_{},terminal_tail_suffix_cursor_{},
                  terminal_tail_last_ticket_id_{},terminal_tail_last_presented_ordinal_{};
    std::uint64_t terminal_tail_completed_tick_{},terminal_tail_last_poll_serial_{};
    RuntimeMatchHistoryTerminalTailTicket terminal_tail_ticket_{};
    std::array<RuntimeMatchHistoryInputReceipt,7> terminal_tail_suffix_rows_{};
    std::array<PADStatus,PAD_MAX_CONTROLLERS> terminal_tail_applied_{};
    RuntimeMatchHistoryPrefixHandoff* prefix_handoff_{};
    RuntimeMatchHistoryPollTicket latest_ticket_{};
    std::array<RuntimeMatchHistoryInputReceipt,7> episode_rows_{};
    std::array<RuntimeMatchHistoryInputReceipt,7> episode_origin_rows_{};
    std::array<RuntimeMatchHistoryInputReceipt,7> prefix_suffix_rows_{};
};

} // namespace melee_web

#endif // MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
