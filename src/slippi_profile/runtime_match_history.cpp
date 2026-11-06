// SPDX-License-Identifier: GPL-3.0-or-later
#include "slippi_profile/runtime_match_history.hpp"

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <exception>
#include "gameplay_net_input.h"

namespace melee_web {
namespace {

GameplayCpuEndpointRecord incomplete_record() noexcept
{
    GameplayCpuEndpointRecord result{};
    result.status=static_cast<std::uint32_t>(GameplayCpuEndpointStatus::Incomplete);
    result.schema=1;
    return result;
}

void capture_refusal(CpuCaptureDiagnostic& out,
                     RuntimeMatchHistoryFailure failure,
                     const char* message) noexcept
{
    out={};
    out.comparison={static_cast<std::uint32_t>(CpuComparisonDomain::Owner),1,
        std::numeric_limits<std::uint32_t>::max(),
        std::numeric_limits<std::uint32_t>::max(),0,0,0,0,
        static_cast<std::uint32_t>(failure)};
    std::snprintf(out.message.data(),out.message.size(),"%s",message);
}

bool same_owner_tuple(const GameplayMatchCpuOwners& a,
                      const GameplayMatchCpuOwners& b) noexcept
{
    return a.storage&&a.context&&a.flow&&a.audio&&a.world&&a.context_epoch&&
        b.storage==a.storage&&b.context==a.context&&b.flow==a.flow&&
        b.audio==a.audio&&b.world==a.world&&b.context_epoch==a.context_epoch;
}

} // namespace

RuntimeMatchHistory::RuntimeMatchHistory(
    GameplayMatchSession& session, RuntimeMatchHistoryBoundaryCheck boundary,
    RuntimeMatchHistoryPresentSubmit present, RuntimeMatchHistoryAudioStep audio_step,
    unsigned& audio_phase, void* callback_context) noexcept
    :session_(session),boundary_(boundary),present_(present),audio_step_(audio_step),
     audio_phase_(audio_phase),pre_audio_phase_(audio_phase),
     callback_context_(callback_context)
{}

std::unique_ptr<RuntimeMatchHistory> RuntimeMatchHistory::capture(
    GameplayMatchSession& session,std::unique_ptr<GameplayCpuStatics> statics,
    CpuCaptureDiagnostic& diagnostic,RuntimeMatchHistoryBoundaryCheck boundary,
    RuntimeMatchHistoryPresentSubmit present,RuntimeMatchHistoryAudioStep audio_step,
    unsigned& audio_phase,void* callback_context) noexcept
{
    diagnostic={};
    if(!statics||!boundary||!present||!audio_step||audio_phase>=60){
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Capture,
                        "Private runtime history requires injected statics and ordinary owner callbacks");
        return {};
    }
    const auto owners=session.diagnostic_cpu_owners();
    if(!owners.storage||!owners.context||!owners.flow||!owners.audio||
       !owners.world||!owners.context_epoch||owners.frame==UINT32_MAX||owners.frame!=session.source_frames()||
       !session.construction_complete()||session.paused()||session.ending()||
       session.complete()||!session.diagnostic_source_files_quiescent()||
       boundary(session,RuntimeMatchHistoryGatePoint::CapturePre,
                session.source_frames(),callback_context)!=
           RuntimeMatchHistoryGateResult::Admitted){
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Boundary,
                        "Ordinary match owner refused retained-history PRE admission");
        return {};
    }

    std::unique_ptr<RuntimeMatchHistory> result(
        new(std::nothrow) RuntimeMatchHistory(session,boundary,present,audio_step,audio_phase,callback_context));
    if(!result){
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Capture,
                        "Runtime history owner allocation returned null");
        return {};
    }
    result->owners_=owners;
    result->pre_frame_=owners.frame;
    result->sealed_end_=0;
    result->checkpoint_=GameplayCpuCheckpoint::capture_history(
        session,std::move(statics),diagnostic);
    if(!result->checkpoint_){
        result->phase_=RuntimeMatchHistoryPhase::Failed;
        result->failure_=RuntimeMatchHistoryFailure::Capture;
        return {};
    }
    if(!result->live_owner(result->pre_frame_)||
       result->gate(RuntimeMatchHistoryGatePoint::CaptureComplete,result->pre_frame_)==false){
        result->fail(RuntimeMatchHistoryFailure::Boundary);
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Boundary,
                        "Ordinary match owner changed during retained-history capture");
        result->checkpoint_.reset();
        return {};
    }
    result->phase_=RuntimeMatchHistoryPhase::Captured;
    return result;
}

RuntimeMatchHistory::~RuntimeMatchHistory()
{
    /* A started history can hold live source borrowers. The checkpoint has an
     * independent abort guard; this makes the owner rule explicit at the
     * runtime adapter boundary as well. Failure teardown must call
     * release_after_match_close(), while success uses retire_confirmed(). */
    if(checkpoint_&&source_step_possible_)std::abort();
}

bool RuntimeMatchHistory::gate(RuntimeMatchHistoryGatePoint point,
                               std::uint32_t expected) noexcept
{
    const auto result=boundary_?boundary_(session_,point,expected,callback_context_):
        RuntimeMatchHistoryGateResult::OtherOwnerRefused;
    if(result!=RuntimeMatchHistoryGateResult::Admitted){
        char message[128]{};
        std::snprintf(message,sizeof(message),"Runtime history boundary refused: point=%u reason=%u expected=%u",
            static_cast<unsigned>(point),static_cast<unsigned>(result),expected);
        return fail(RuntimeMatchHistoryFailure::Boundary,message);
    }
    return true;
}

bool RuntimeMatchHistory::live_owner(std::uint32_t expected) const noexcept
{
    const auto live=session_.diagnostic_cpu_owners();
    return same_owner_tuple(owners_,live)&&session_.source_frames()==expected&&
        session_.diagnostic_source_files_quiescent();
}

bool RuntimeMatchHistory::fail(RuntimeMatchHistoryFailure failure,const char* message) noexcept
{
    if(failure_==RuntimeMatchHistoryFailure::None){
        failure_=failure;
        std::snprintf(error_.data(),error_.size(),"%s",message?message:"Runtime match history admission failed");
    }
    phase_=RuntimeMatchHistoryPhase::Failed;
    if(checkpoint_)checkpoint_->disarm();
    return false;
}

bool RuntimeMatchHistory::checkpoint_failure(RuntimeMatchHistoryFailure failure,const char* operation) noexcept
{
    const auto proof=checkpoint_?checkpoint_->comparison():CpuComparison{};
    char message[200]{};
    std::snprintf(message,sizeof(message),"%s: domain=%u status=%u row=%u detail=%u",
        operation,proof.domain,proof.status,proof.row,proof.detail);
    return fail(failure,message);
}

bool RuntimeMatchHistory::begin() noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::CapturedPreCopied||!checkpoint_||
       !live_owner(pre_frame_)||!gate(RuntimeMatchHistoryGatePoint::Arm,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->begin_history_incarnation(session_))
        return fail(RuntimeMatchHistoryFailure::Owner);
    incarnation_=1;
    source_step_possible_=true;
    phase_=RuntimeMatchHistoryPhase::ArmedPrePending;
    return true;
}

bool RuntimeMatchHistory::forward_from_poll(
    const PADStatus raw[PAD_MAX_CONTROLLERS],std::uint64_t poll_serial,
    unsigned local_port,unsigned remote_port,const std::uint8_t* predicted_remote,
    unsigned predicted_remote_bytes) noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::Armed||!checkpoint_||!raw||
       poll_serial==0||local_port>=PAD_MAX_CONTROLLERS||
       remote_port>=PAD_MAX_CONTROLLERS||local_port==remote_port||
       !predicted_remote||predicted_remote_bytes!=MELEE_WEB_NET_PAD_BYTES||
       !live_owner(pre_frame_)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardBefore,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Input);

    if(!checkpoint_->interval_boundary(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Owner,"Forward native interval admission refused");
    PADStatus predicted{};
    if(!melee_web_net_decode_pad(predicted_remote,predicted_remote_bytes,&predicted))
        return fail(RuntimeMatchHistoryFailure::Input);
    std::array<PADStatus,PAD_MAX_CONTROLLERS> next{};
    std::memcpy(retained_raw_.data(),raw,sizeof(PADStatus)*PAD_MAX_CONTROLLERS);
    std::memcpy(next.data(),retained_raw_.data(),sizeof(PADStatus)*PAD_MAX_CONTROLLERS);
    next[remote_port]=predicted;
    std::memcpy(retained_forward_.data(),next.data(),sizeof(PADStatus)*PAD_MAX_CONTROLLERS);
    poll_serial_=poll_serial;
    local_port_=local_port;
    remote_port_=remote_port;
    has_input_=true;

    try {
        session_.tick(retained_forward_.data());
        if(!audio_step_(session_,callback_context_))
            return fail(RuntimeMatchHistoryFailure::SourceTick,"Ordinary audio render refused after forward tick");
    } catch(const std::exception& error) {
        return fail(RuntimeMatchHistoryFailure::SourceTick,error.what());
    } catch(...) {
        return fail(RuntimeMatchHistoryFailure::SourceTick);
    }
    const std::uint32_t actual=session_.source_frames();
    if(actual==pre_frame_+1u)++forward_steps_;
    if(actual!=pre_frame_+1u||!live_owner(actual)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardAfter,actual))
        return fail(RuntimeMatchHistoryFailure::SourceTick);
    if(!checkpoint_->seal_history_incarnation(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Seal,"Native history seal refused");
    sealed_end_=actual;
    phase_=RuntimeMatchHistoryPhase::PredictedSealed;
    return true;
}

bool RuntimeMatchHistory::restore_pre() noexcept
{
    constexpr std::uint32_t required=PredictedCpu|PredictedDraw;
    if(phase_!=RuntimeMatchHistoryPhase::PredictedComplete||!checkpoint_||
       (endpoint_mask_&required)!=required||!live_owner(sealed_end_)||
       !gate(RuntimeMatchHistoryGatePoint::RestoreBefore,sealed_end_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->archive_history_endpoint(session_)||
       checkpoint_->restore_history_pre(session_)!=GameplayCpuCommitResult::Ok)
        return checkpoint_failure(RuntimeMatchHistoryFailure::Restore,"Native typed history restore refused");
    audio_phase_=pre_audio_phase_;
    ++restore_count_;
    if(restore_count_!=1u||session_.source_frames()!=pre_frame_||
       !live_owner(pre_frame_)||
       !gate(RuntimeMatchHistoryGatePoint::RestoreAfter,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Restore);
    phase_=RuntimeMatchHistoryPhase::RestoredPrePending;
    return true;
}

bool RuntimeMatchHistory::correct_from_retained(
    const std::uint8_t* late_remote,unsigned late_remote_bytes) noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::Restored||!checkpoint_||!has_input_||
       !late_remote||late_remote_bytes!=MELEE_WEB_NET_PAD_BYTES||
       session_.source_frames()!=pre_frame_||!live_owner(pre_frame_)||
       !gate(RuntimeMatchHistoryGatePoint::CorrectBefore,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->interval_boundary(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Owner,"Corrected native interval admission refused");
    PADStatus actual_remote{};
    if(!melee_web_net_decode_pad(late_remote,late_remote_bytes,&actual_remote))
        return fail(RuntimeMatchHistoryFailure::Input);
    std::array<PADStatus,PAD_MAX_CONTROLLERS> corrected{};
    std::memcpy(corrected.data(),retained_raw_.data(),
                sizeof(PADStatus)*PAD_MAX_CONTROLLERS);
    corrected[remote_port_]=actual_remote;
    /* The replay consumes the same polled local PAD bytes. It does not poll or
     * advance the host sample serial. */
    if(std::memcmp(&corrected[local_port_],&retained_raw_[local_port_],
                   sizeof(PADStatus))!=0)
        return fail(RuntimeMatchHistoryFailure::Input);
    retained_corrected_=corrected;
    try {
        session_.tick(corrected.data());
        if(!audio_step_(session_,callback_context_))
            return fail(RuntimeMatchHistoryFailure::Correction,"Ordinary audio render refused after corrected tick");
    } catch(const std::exception& error) {
        return fail(RuntimeMatchHistoryFailure::Correction,error.what());
    } catch(...) {
        return fail(RuntimeMatchHistoryFailure::Correction);
    }
    const std::uint32_t actual=session_.source_frames();
    if(actual==sealed_end_)++replay_steps_;
    if(actual!=sealed_end_||!live_owner(actual)||
       !gate(RuntimeMatchHistoryGatePoint::CorrectAfter,actual))
        return fail(RuntimeMatchHistoryFailure::Correction);
    if(!checkpoint_->finish_history_replay(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Correction,"Native history replay finish refused");
    phase_=RuntimeMatchHistoryPhase::CorrectedSealed;
    return true;
}

bool RuntimeMatchHistory::submit_present() noexcept
{
    std::uint32_t expected=0;
    RuntimeMatchHistoryPhase pending{};
    if(phase_==RuntimeMatchHistoryPhase::PredictedCpuCopied){
        expected=sealed_end_;pending=RuntimeMatchHistoryPhase::PredictedDrawPending;
    }else if(phase_==RuntimeMatchHistoryPhase::CorrectedCpuCopied){
        expected=sealed_end_;pending=RuntimeMatchHistoryPhase::CorrectedDrawPending;
    }else return fail(RuntimeMatchHistoryFailure::Phase);
    if(!present_||!live_owner(expected)||
       !gate(RuntimeMatchHistoryGatePoint::PresentSubmit,expected))
        return fail(RuntimeMatchHistoryFailure::Present);
    if(present_(session_,expected,callback_context_)!=
       RuntimeMatchHistoryPresentResult::Submitted||
       !live_owner(expected))
        return fail(RuntimeMatchHistoryFailure::Present);
    phase_=pending;
    return true;
}

bool RuntimeMatchHistory::admit_present_completion() noexcept
{
    std::uint32_t expected=0;
    RuntimeMatchHistoryPhase idle{};
    if(phase_==RuntimeMatchHistoryPhase::PredictedDrawPending){
        expected=sealed_end_;idle=RuntimeMatchHistoryPhase::PredictedDrawIdle;
    }else if(phase_==RuntimeMatchHistoryPhase::CorrectedDrawPending){
        expected=sealed_end_;idle=RuntimeMatchHistoryPhase::CorrectedDrawIdle;
    }else return fail(RuntimeMatchHistoryFailure::Phase);
    if(!live_owner(expected)||
       !gate(RuntimeMatchHistoryGatePoint::PresentComplete,expected))
        return fail(RuntimeMatchHistoryFailure::Boundary);
    phase_=idle;
    return true;
}

bool RuntimeMatchHistory::endpoint_role_allowed(
    View view,Provenance provenance,EndpointRole& role) const noexcept
{
    role=EndpointRole::None;
    if(view==View::Pre&&provenance==Provenance::SharedPre){
        switch(phase_){
        case RuntimeMatchHistoryPhase::Captured: role=EndpointRole::CapturedPre;return true;
        case RuntimeMatchHistoryPhase::ArmedPrePending: role=EndpointRole::ArmedPre;return true;
        case RuntimeMatchHistoryPhase::RestoredPrePending: role=EndpointRole::RestoredPre;return true;
        default:return false;
        }
    }
    if(view==View::CpuEnd&&provenance==Provenance::Reference&&
       phase_==RuntimeMatchHistoryPhase::PredictedSealed){
        role=EndpointRole::PredictedCpu;return true;
    }
    if(view==View::DrawEnd&&provenance==Provenance::Reference&&
       phase_==RuntimeMatchHistoryPhase::PredictedDrawIdle){
        role=EndpointRole::PredictedDraw;return true;
    }
    if(view==View::CpuEnd&&provenance==Provenance::Corrected&&
       phase_==RuntimeMatchHistoryPhase::CorrectedSealed){
        role=EndpointRole::CorrectedCpu;return true;
    }
    if(view==View::DrawEnd&&provenance==Provenance::Corrected&&
       phase_==RuntimeMatchHistoryPhase::CorrectedDrawIdle){
        role=EndpointRole::CorrectedDraw;return true;
    }
    return false;
}

bool RuntimeMatchHistory::begin_endpoint_batch(View view,
                                               Provenance provenance) noexcept
{
    EndpointRole role{};
    if(!checkpoint_||reader_.active()||endpoint_role_!=EndpointRole::None||
       !endpoint_role_allowed(view,provenance,role))
        return fail(RuntimeMatchHistoryFailure::Phase);
    const std::uint32_t expected=(role==EndpointRole::CapturedPre||
        role==EndpointRole::ArmedPre||role==EndpointRole::RestoredPre)?
        pre_frame_:sealed_end_;
    if(!live_owner(expected)||
       !gate(RuntimeMatchHistoryGatePoint::EndpointBegin,expected))
        return fail(RuntimeMatchHistoryFailure::Boundary);
    if(!reader_.begin_history(session_,*checkpoint_,view,provenance))
        return fail(RuntimeMatchHistoryFailure::Endpoint);
    endpoint_role_=role;
    endpoint_read_failure_=false;
    return true;
}

GameplayCpuEndpointRecord RuntimeMatchHistory::endpoint_record(
    Domain domain,RowKind kind,std::uint32_t index) const noexcept
{
    if(!reader_.active()||endpoint_role_==EndpointRole::None){
        endpoint_read_failure_=true;
        return incomplete_record();
    }
    auto result=reader_.record(domain,kind,index);
    if(result.status)endpoint_read_failure_=true;
    return result;
}

bool RuntimeMatchHistory::endpoint_particle_header(
    SlippiParticleCellEndpointHeader& out) const noexcept
{
    out={};
    const bool okay=reader_.active()&&endpoint_role_!=EndpointRole::None&&
        reader_.particle_header(out);
    if(!okay)endpoint_read_failure_=true;
    return okay;
}

bool RuntimeMatchHistory::endpoint_particle_row(
    std::uint32_t kind,std::uint32_t index,
    SlippiParticleCellEndpointRow& out) const noexcept
{
    out={};
    const bool okay=reader_.active()&&endpoint_role_!=EndpointRole::None&&
        reader_.particle_row(kind,index,out);
    if(!okay)endpoint_read_failure_=true;
    return okay;
}

bool RuntimeMatchHistory::end_endpoint_batch() noexcept
{
    if(!reader_.active()||endpoint_role_==EndpointRole::None)
        return fail(RuntimeMatchHistoryFailure::Phase);
    const EndpointRole role=endpoint_role_;
    const std::uint32_t expected=(role==EndpointRole::CapturedPre||
        role==EndpointRole::ArmedPre||role==EndpointRole::RestoredPre)?
        pre_frame_:sealed_end_;
    if(endpoint_read_failure_||!reader_.revalidate()||!live_owner(expected)||
       !gate(RuntimeMatchHistoryGatePoint::EndpointComplete,expected))
        return fail(RuntimeMatchHistoryFailure::Endpoint);
    reader_=GameplayCpuEndpointReader{};
    endpoint_role_=EndpointRole::None;
    endpoint_read_failure_=false;
    switch(role){
    case EndpointRole::CapturedPre:
        endpoint_mask_|=CapturedPre;phase_=RuntimeMatchHistoryPhase::CapturedPreCopied;break;
    case EndpointRole::ArmedPre:
        endpoint_mask_|=ArmedPre;phase_=RuntimeMatchHistoryPhase::Armed;break;
    case EndpointRole::PredictedCpu:
        endpoint_mask_|=PredictedCpu;phase_=RuntimeMatchHistoryPhase::PredictedCpuCopied;break;
    case EndpointRole::PredictedDraw:
        endpoint_mask_|=PredictedDraw;phase_=RuntimeMatchHistoryPhase::PredictedComplete;break;
    case EndpointRole::RestoredPre:
        endpoint_mask_|=RestoredPre;phase_=RuntimeMatchHistoryPhase::Restored;break;
    case EndpointRole::CorrectedCpu:
        endpoint_mask_|=CorrectedCpu;phase_=RuntimeMatchHistoryPhase::CorrectedCpuCopied;break;
    case EndpointRole::CorrectedDraw:
        endpoint_mask_|=CorrectedDraw;phase_=RuntimeMatchHistoryPhase::CorrectedComplete;break;
    case EndpointRole::None:return fail(RuntimeMatchHistoryFailure::Endpoint);
    }
    return true;
}

bool RuntimeMatchHistory::close_selected_endpoint() noexcept
{
    const bool corrected=phase_==RuntimeMatchHistoryPhase::CorrectedComplete;
    const bool reference=phase_==RuntimeMatchHistoryPhase::PredictedComplete;
    const std::uint32_t required=corrected?(CorrectedCpu|CorrectedDraw):(PredictedCpu|PredictedDraw);
    if((!corrected&&!reference)||!checkpoint_||
       (endpoint_mask_&required)!=required||reader_.active()||
       !live_owner(sealed_end_)||
       !gate(RuntimeMatchHistoryGatePoint::Close,sealed_end_)||
       !checkpoint_->close_history_endpoint(session_))
        return fail(RuntimeMatchHistoryFailure::Close);
    phase_=RuntimeMatchHistoryPhase::Closed;
    return true;
}

bool RuntimeMatchHistory::retire_confirmed() noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::Closed||!checkpoint_||
       !live_owner(sealed_end_)||
       !gate(RuntimeMatchHistoryGatePoint::Retire,sealed_end_)||
       !checkpoint_->retire_history_confirmed(session_))
        return fail(RuntimeMatchHistoryFailure::Retire);
    reader_=GameplayCpuEndpointReader{};
    checkpoint_.reset();
    source_step_possible_=false;
    phase_=RuntimeMatchHistoryPhase::Retired;
    return true;
}

bool RuntimeMatchHistory::can_rebase_confirmed(std::uint32_t end_frame) const noexcept
{
    return phase_==RuntimeMatchHistoryPhase::Retired&&!checkpoint_&&!reader_.active()&&
        failure_==RuntimeMatchHistoryFailure::None&&forward_steps_==1&&
        end_frame==sealed_end_&&live_owner(end_frame);
}

bool RuntimeMatchHistory::release_after_match_close() noexcept
{
    if(!checkpoint_){
        if(phase_==RuntimeMatchHistoryPhase::Retired){
            phase_=RuntimeMatchHistoryPhase::MatchClosed;
            return true;
        }
        phase_=RuntimeMatchHistoryPhase::MatchClosed;
        return true;
    }
    const auto live=session_.diagnostic_cpu_owners();
    if(live.storage||live.context||live.flow||live.audio||live.world||
       live.context_epoch||!checkpoint_->source_owners_retired(&session_)){
        if(failure_==RuntimeMatchHistoryFailure::None)
            failure_=RuntimeMatchHistoryFailure::Teardown;
        return false;
    }
    reader_=GameplayCpuEndpointReader{};
    checkpoint_.reset();
    source_step_possible_=false;
    phase_=RuntimeMatchHistoryPhase::MatchClosed;
    return true;
}

void RuntimeMatchHistory::disarm_for_teardown() noexcept
{
    if(checkpoint_)checkpoint_->disarm();
    reader_=GameplayCpuEndpointReader{};
    endpoint_role_=EndpointRole::None;
}

const PADStatus* RuntimeMatchHistory::retained_input(unsigned kind) const noexcept
{
    if(!has_input_)return nullptr;
    if(kind==0)return retained_raw_.data();
    if(kind==1)return retained_forward_.data();
    if(kind==2&&replay_steps_==1)return retained_corrected_.data();
    return nullptr;
}

std::uint32_t RuntimeMatchHistory::current_frame() const noexcept
{
    return session_.source_frames();
}

const MeleeWebCpuMotorEvidence* RuntimeMatchHistory::motor_evidence() const noexcept
{
    return checkpoint_?checkpoint_->motor_evidence(session_):nullptr;
}

RuntimeMatchHistoryMetadata RuntimeMatchHistory::metadata() const noexcept
{
    RuntimeMatchHistoryMetadata out{};
    out.phase=static_cast<std::uint32_t>(phase_);
    out.first_failure=static_cast<std::uint32_t>(failure_);
    out.pre_frame=pre_frame_;
    out.current_frame=session_.source_frames();
    out.sealed_end=sealed_end_;
    out.incarnation=incarnation_;
    out.restore_count=restore_count_;
    out.endpoint_mask=endpoint_mask_;
    out.forward_steps=forward_steps_;
    out.replay_steps=replay_steps_;
    out.poll_serial=poll_serial_;
    out.pre_audio_phase=pre_audio_phase_;
    out.current_audio_phase=audio_phase_;
    return out;
}

} // namespace melee_web

#endif // MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
