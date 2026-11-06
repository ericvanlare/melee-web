// SPDX-License-Identifier: GPL-3.0-or-later
#include "slippi_profile/runtime_match_history.hpp"

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC)

#include <algorithm>
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

void encode_pad_row(const PADStatus& pad,std::uint8_t* out) noexcept
{
    out[0]=static_cast<std::uint8_t>(pad.button>>8);
    out[1]=static_cast<std::uint8_t>(pad.button);
    out[2]=static_cast<std::uint8_t>(pad.stickX);
    out[3]=static_cast<std::uint8_t>(pad.stickY);
    out[4]=static_cast<std::uint8_t>(pad.substickX);
    out[5]=static_cast<std::uint8_t>(pad.substickY);
    out[6]=pad.triggerLeft;out[7]=pad.triggerRight;
    out[8]=pad.analogA;out[9]=pad.analogB;out[10]=static_cast<std::uint8_t>(pad.err);
}

void encode_pad_frame(const PADStatus pads[PAD_MAX_CONTROLLERS],
                      std::array<std::uint8_t,44>& out) noexcept
{
    for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port)
        encode_pad_row(pads[port],out.data()+port*MELEE_WEB_NET_PAD_BYTES);
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
    unsigned& audio_phase,void* callback_context,bool episode_mode,bool arena_mode) noexcept
{
    diagnostic={};
    if(!statics||!boundary||!present||!audio_step||audio_phase>=60||(arena_mode&&!episode_mode)){
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
    result->segment_start_frame_=owners.frame;
    result->episode_mode_=episode_mode;
    result->arena_mode_=arena_mode;
    result->checkpoint_=GameplayCpuCheckpoint::capture_history(
        session,std::move(statics),diagnostic,
        arena_mode?CpuHistoryStorage::OwnedArena:CpuHistoryStorage::StablePayload);
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
    char message[512]{};
    std::snprintf(message,sizeof(message),"%s: domain=%u status=%u row=%u detail=%u",
        operation,proof.domain,proof.status,proof.row,proof.detail);
    return fail(failure,message);
}

bool RuntimeMatchHistory::begin() noexcept
{
    if(episode_mode_||phase_!=RuntimeMatchHistoryPhase::CapturedPreCopied||!checkpoint_||
       !live_owner(pre_frame_)||!gate(RuntimeMatchHistoryGatePoint::Arm,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->begin_history_incarnation(session_))
        return fail(RuntimeMatchHistoryFailure::Owner);
    incarnation_=1;
    source_step_possible_=true;
    phase_=RuntimeMatchHistoryPhase::ArmedPrePending;
    return true;
}

bool RuntimeMatchHistory::begin_episode() noexcept
{
    if(!episode_mode_||phase_!=RuntimeMatchHistoryPhase::CapturedPreCopied||!checkpoint_||
       !live_owner(pre_frame_)||!gate(RuntimeMatchHistoryGatePoint::Arm,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->begin_history_incarnation(session_))
        return fail(RuntimeMatchHistoryFailure::Owner);
    incarnation_=1;
    segment_start_frame_=pre_frame_;
    source_step_possible_=true;
    phase_=RuntimeMatchHistoryPhase::ArmedPrePending;
    return true;
}

bool RuntimeMatchHistory::can_admit_poll_ticket(
    std::uint32_t ticket_id,unsigned local_port,unsigned remote_port) const noexcept
{
    const auto current=session_.source_frames();
    const bool phase_ok=phase_==RuntimeMatchHistoryPhase::Armed||
        phase_==RuntimeMatchHistoryPhase::EpisodeRecording;
    const bool prefix_suffix_complete=!prefix_handoff_imported_||
        (prefix_handoff_&&prefix_handoff_->active_&&
         prefix_handoff_->suffix_cursor_==prefix_handoff_->suffix_count_&&
         current>=prefix_handoff_->selected_end_frame_);
    return episode_mode_&&phase_ok&&checkpoint_&&ticket_id&&ticket_id>last_ticket_id_&&
        local_port<PAD_MAX_CONTROLLERS&&remote_port<PAD_MAX_CONTROLLERS&&
        local_port!=remote_port&&current>=pre_frame_&&
        current-pre_frame_==ticket_count_&&ticket_count_<episode_rows_.size()&&
        !current_ticket_pending_&&prefix_suffix_complete&&live_owner(current)&&
        (!ticket_count_||(episode_rows_[0].local_port==local_port&&
          episode_rows_[0].remote_port==remote_port));
}

bool RuntimeMatchHistory::admit_poll_ticket(
    std::uint32_t ticket_id,const PADStatus raw[PAD_MAX_CONTROLLERS],
    std::uint64_t poll_serial,unsigned local_port,unsigned remote_port) noexcept
{
    const auto current=session_.source_frames();
    if(!raw||!poll_serial||poll_serial<=poll_serial_||
       !can_admit_poll_ticket(ticket_id,local_port,remote_port)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardBefore,current))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode poll ticket admission refused");

    auto& row=episode_rows_[ticket_count_];
    row={};
    row.ticket_id=ticket_id;row.source_frame=current;row.incarnation=incarnation_;
    row.local_port=local_port;row.remote_port=remote_port;row.poll_serial=poll_serial;
    encode_pad_frame(raw,row.raw_pad);
    episode_origin_rows_[ticket_count_]=row;
    latest_ticket_={};
    latest_ticket_.ticket_id=ticket_id;latest_ticket_.source_frame=current;
    latest_ticket_.incarnation=incarnation_;latest_ticket_.local_port=local_port;
    latest_ticket_.remote_port=remote_port;latest_ticket_.poll_serial=poll_serial;
    latest_ticket_.raw_pad=row.raw_pad;
    ++ticket_count_;last_ticket_id_=ticket_id;poll_serial_=poll_serial;
    latest_ticket_valid_=true;current_ticket_pending_=true;
    return true;
}

bool RuntimeMatchHistory::forward_ticket(
    std::uint32_t ticket_id,const std::uint8_t* planned_frame,
    unsigned planned_frame_bytes) noexcept
{
    const auto current=session_.source_frames();
    if(!episode_mode_||!checkpoint_||!planned_frame||planned_frame_bytes!=44||
       !current_ticket_pending_||!latest_ticket_valid_||ticket_id!=latest_ticket_.ticket_id||
       ticket_id!=last_ticket_id_||latest_ticket_.source_frame!=current||
       latest_ticket_.incarnation!=incarnation_||
       (phase_!=RuntimeMatchHistoryPhase::Armed&&phase_!=RuntimeMatchHistoryPhase::EpisodeRecording)||
       current<pre_frame_||current-pre_frame_>=7u||
       forward_steps_>=7u||!live_owner(current)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardBefore,current))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode forward ticket is stale or out of order");
    if(prefix_adoption_pending_&&
       (!prefix_handoff_imported_||!prefix_handoff_||!prefix_handoff_->active_||
        prefix_handoff_->suffix_cursor_!=prefix_adopted_cursor_||
        current!=prefix_handoff_->confirmed_frame_+prefix_adopted_cursor_))
        return fail(RuntimeMatchHistoryFailure::Input,"Adopted prefix ticket handoff changed before forward");
    if(!checkpoint_->interval_boundary(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Owner,"Episode forward interval admission refused");

    const auto ordinal=current-pre_frame_;
    if(ordinal>=episode_rows_.size())
        return fail(RuntimeMatchHistoryFailure::Input,"Episode forward ordinal exceeds retained ticket storage");
    auto& row=episode_rows_[ordinal];
    if(row.state!=0||row.ticket_id!=ticket_id||row.source_frame!=current||
       row.incarnation!=incarnation_||row.poll_serial!=latest_ticket_.poll_serial||
       row.local_port!=latest_ticket_.local_port||row.remote_port!=latest_ticket_.remote_port)
        return fail(RuntimeMatchHistoryFailure::Input,"Episode forward ticket identity changed");

    std::array<PADStatus,PAD_MAX_CONTROLLERS> actual{},applied{};
    for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port){
        if(!melee_web_net_decode_pad(row.raw_pad.data()+port*MELEE_WEB_NET_PAD_BYTES,
              MELEE_WEB_NET_PAD_BYTES,&actual[port]))
            return fail(RuntimeMatchHistoryFailure::Input,"Episode retained actual PAD row refused");
    }
    applied=actual;
    if(!melee_web_net_decode_pad(planned_frame+row.remote_port*MELEE_WEB_NET_PAD_BYTES,
          MELEE_WEB_NET_PAD_BYTES,&applied[row.remote_port]))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode planned remote PAD refused");
    std::array<std::uint8_t,44> expected{};
    encode_pad_frame(applied.data(),expected);
    if(!std::equal(expected.begin(),expected.end(),planned_frame))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode plan changed actual local or unused PAD bytes");

    std::memcpy(retained_raw_.data(),actual.data(),sizeof(actual));
    std::memcpy(retained_forward_.data(),applied.data(),sizeof(applied));
    std::memcpy(row.forward_pad.data(),planned_frame,44);
    row.selected_pad=row.forward_pad;row.state=1;
    if(!prefix_adoption_pending_)
        episode_origin_rows_[ordinal]=row;
    local_port_=row.local_port;remote_port_=row.remote_port;has_input_=true;
    current_ticket_pending_=false;
    try{
        session_.tick(retained_forward_.data());
        if(!audio_step_(session_,callback_context_))
            return fail(RuntimeMatchHistoryFailure::SourceTick,"Ordinary audio render refused after episode forward tick");
    }catch(const std::exception& error){
        return fail(RuntimeMatchHistoryFailure::SourceTick,error.what());
    }catch(...){return fail(RuntimeMatchHistoryFailure::SourceTick);}
    const std::uint32_t actual_frame=session_.source_frames();
    if(actual_frame==current+1u)++forward_steps_;
    if(actual_frame!=current+1u||actual_frame-pre_frame_>7u||!live_owner(actual_frame)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardAfter,actual_frame))
        return fail(RuntimeMatchHistoryFailure::SourceTick,"Episode forward source frame boundary refused");
    if(prefix_adoption_pending_){
        ++prefix_handoff_->suffix_cursor_;
        prefix_suffix_cursor_=prefix_handoff_->suffix_cursor_;
        prefix_adoption_pending_=false;
    }
    phase_=RuntimeMatchHistoryPhase::EpisodeRecording;
    return true;
}

bool RuntimeMatchHistory::seal_incarnation() noexcept
{
    const auto actual=session_.source_frames();
    if(!episode_mode_||phase_!=RuntimeMatchHistoryPhase::EpisodeRecording||!checkpoint_||
       current_ticket_pending_||actual<=segment_start_frame_||actual<=pre_frame_||
       actual-pre_frame_>7u||forward_steps_!=actual-pre_frame_||
       !live_owner(actual)||!gate(RuntimeMatchHistoryGatePoint::ForwardAfter,actual))
        return fail(RuntimeMatchHistoryFailure::Seal,"Episode incarnation seal boundary refused");
    const char* refusal=nullptr;
    if(!checkpoint_->seal_history_incarnation(session_,&refusal))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Seal,refusal);
    sealed_end_=actual;replay_target_end_=actual;endpoint_mask_=0;
    phase_=RuntimeMatchHistoryPhase::PredictedSealed;
    return true;
}

bool RuntimeMatchHistory::replay_ticket(
    std::uint32_t ticket_id,const std::uint8_t* planned_frame,
    unsigned planned_frame_bytes) noexcept
{
    const auto current=session_.source_frames();
    if(!episode_mode_||!checkpoint_||!planned_frame||planned_frame_bytes!=44||
       phase_!=RuntimeMatchHistoryPhase::Restored||
       current<pre_frame_||current>=replay_target_end_||current-pre_frame_>=episode_rows_.size()||
       replay_steps_>=7u||!live_owner(current)||
       !gate(RuntimeMatchHistoryGatePoint::CorrectBefore,current))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode replay step admission refused");
    if(!checkpoint_->interval_boundary(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Owner,"Episode replay interval admission refused");
    auto& row=episode_rows_[current-pre_frame_];
    if((row.state!=1&&row.state!=2)||row.ticket_id!=ticket_id||row.source_frame!=current||
       row.poll_serial==0||row.local_port!=local_port_||row.remote_port!=remote_port_||
       row.incarnation==0||row.incarnation>incarnation_)
        return fail(RuntimeMatchHistoryFailure::Input,"Episode replay did not bind the retained source ticket");

    std::array<PADStatus,PAD_MAX_CONTROLLERS> actual{},applied{};
    for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port){
        if(!melee_web_net_decode_pad(row.raw_pad.data()+port*MELEE_WEB_NET_PAD_BYTES,
              MELEE_WEB_NET_PAD_BYTES,&actual[port]))
            return fail(RuntimeMatchHistoryFailure::Input,"Episode replay retained actual PAD row refused");
    }
    applied=actual;
    if(!melee_web_net_decode_pad(planned_frame+row.remote_port*MELEE_WEB_NET_PAD_BYTES,
          MELEE_WEB_NET_PAD_BYTES,&applied[row.remote_port]))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode replay planned remote PAD refused");
    std::array<std::uint8_t,44> expected{};
    encode_pad_frame(applied.data(),expected);
    if(!std::equal(expected.begin(),expected.end(),planned_frame))
        return fail(RuntimeMatchHistoryFailure::Input,"Episode replay plan changed retained local or unused PAD bytes");

    std::memcpy(retained_raw_.data(),actual.data(),sizeof(actual));
    std::memcpy(retained_corrected_.data(),applied.data(),sizeof(applied));
    std::memcpy(row.selected_pad.data(),planned_frame,44);row.state=2;
    has_input_=true;
    try{
        session_.tick(retained_corrected_.data());
        if(!audio_step_(session_,callback_context_))
            return fail(RuntimeMatchHistoryFailure::Correction,"Ordinary audio render refused after episode replay tick");
    }catch(const std::exception& error){
        return fail(RuntimeMatchHistoryFailure::Correction,error.what());
    }catch(...){return fail(RuntimeMatchHistoryFailure::Correction);}
    const std::uint32_t actual_frame=session_.source_frames();
    if(actual_frame==current+1u)++replay_steps_;
    if(actual_frame!=current+1u||!live_owner(actual_frame)||
       !gate(RuntimeMatchHistoryGatePoint::CorrectAfter,actual_frame))
        return fail(RuntimeMatchHistoryFailure::Correction,"Episode replay source frame boundary refused");
    return true;
}

bool RuntimeMatchHistory::finish_replay() noexcept
{
    if(!episode_mode_||phase_!=RuntimeMatchHistoryPhase::Restored||!checkpoint_||
       current_ticket_pending_||replay_target_end_<=pre_frame_||
       session_.source_frames()!=replay_target_end_||
       replay_steps_!=replay_target_end_-pre_frame_||
       !live_owner(replay_target_end_)||!gate(RuntimeMatchHistoryGatePoint::CorrectAfter,replay_target_end_))
        return fail(RuntimeMatchHistoryFailure::Correction,"Episode replay did not reach the exact sealed END");
    if(!checkpoint_->finish_history_replay(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Correction,"Episode replay finish refused");
    sealed_end_=replay_target_end_;endpoint_mask_=0;
    phase_=RuntimeMatchHistoryPhase::CorrectedSealed;
    return true;
}

bool RuntimeMatchHistory::finish_confirmed_prefix(std::uint32_t confirmed_end) noexcept
{
    const auto actual=session_.source_frames();
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::Restored||!checkpoint_||
       current_ticket_pending_||prefix_adoption_pending_||confirmed_end<=pre_frame_||
       confirmed_end>=replay_target_end_||replay_target_end_!=sealed_end_||
       replay_target_end_<=pre_frame_||replay_target_end_-pre_frame_>episode_rows_.size()||
       forward_steps_!=replay_target_end_-pre_frame_||ticket_count_!=forward_steps_||
       replay_steps_!=confirmed_end-pre_frame_||actual!=confirmed_end||
       !live_owner(confirmed_end)||!gate(RuntimeMatchHistoryGatePoint::CorrectAfter,confirmed_end))
        return fail(RuntimeMatchHistoryFailure::Correction,
                    "Confirmed-prefix finish requires the actual strictly interior replay CPU frame");
    for(std::uint32_t index=0;index<ticket_count_;++index){
        const auto& row=episode_rows_[index];
        if((row.state!=1u&&row.state!=2u)||row.source_frame!=pre_frame_+index||
           !row.ticket_id||!row.poll_serial||row.incarnation==0||row.incarnation>incarnation_||
           row.local_port!=local_port_||row.remote_port!=remote_port_)
            return fail(RuntimeMatchHistoryFailure::Input,
                        "Confirmed-prefix input receipt interval is incomplete");
    }
    if(!checkpoint_->finish_history_confirmed_prefix(session_,confirmed_end))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Correction,
                                  "Native confirmed-prefix leaf admission refused");
    prefix_selected_end_=replay_target_end_;
    sealed_end_=confirmed_end;
    endpoint_mask_=0;
    phase_=RuntimeMatchHistoryPhase::PrefixSealed;
    return true;
}

bool RuntimeMatchHistory::capture_confirmed_prefix_handoff(
    std::uint32_t finish_request_sequence,
    RuntimeMatchHistoryPrefixHandoff& handoff) const noexcept
{
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::PrefixSealed||
       !checkpoint_||!finish_request_sequence||!prefix_selected_end_||
       sealed_end_<=pre_frame_||sealed_end_>=prefix_selected_end_||
       prefix_selected_end_-pre_frame_!=ticket_count_||
       ticket_count_>episode_rows_.size()||!live_owner(sealed_end_))
        return false;
    if(handoff.active_&&handoff.suffix_cursor_!=handoff.suffix_count_)
        return false;
    const std::uint32_t first=sealed_end_-pre_frame_;
    const std::uint32_t suffix_count=prefix_selected_end_-sealed_end_;
    if(!suffix_count||first+suffix_count!=ticket_count_||suffix_count>handoff.suffix_.size())
        return false;
    RuntimeMatchHistoryPrefixHandoff value;
    value.active_=true;
    value.owners_=owners_;
    value.old_pre_frame_=pre_frame_;
    value.confirmed_frame_=sealed_end_;
    value.selected_end_frame_=prefix_selected_end_;
    value.old_incarnation_=incarnation_;
    value.finish_request_sequence_=finish_request_sequence;
    value.local_port_=local_port_;
    value.remote_port_=remote_port_;
    value.suffix_count_=suffix_count;
    for(std::uint32_t index=0;index<suffix_count;++index){
        const auto& live_row=episode_rows_[first+index];
        const auto& row=episode_origin_rows_[first+index];
        if((live_row.state!=1u&&live_row.state!=2u)||
           (row.state!=1u&&row.state!=2u)||row.schema!=1||row.bytes!=sizeof(row)||
           live_row.source_frame!=sealed_end_+index||row.source_frame!=live_row.source_frame||
           row.ticket_id!=live_row.ticket_id||row.poll_serial!=live_row.poll_serial||
           row.incarnation==0||row.incarnation>3u||row.local_port!=local_port_||
           row.remote_port!=remote_port_||row.raw_pad!=live_row.raw_pad||
           live_row.local_port!=local_port_||live_row.remote_port!=remote_port_||
           live_row.incarnation==0||live_row.incarnation>incarnation_||
           (index&&(row.ticket_id<=value.suffix_[index-1u].ticket_id||
                    row.poll_serial<=value.suffix_[index-1u].poll_serial)))
            return false;
        value.suffix_[index]=row;
    }
    handoff.clear();
    handoff.active_=value.active_;
    handoff.owners_=value.owners_;
    handoff.old_pre_frame_=value.old_pre_frame_;
    handoff.confirmed_frame_=value.confirmed_frame_;
    handoff.selected_end_frame_=value.selected_end_frame_;
    handoff.old_incarnation_=value.old_incarnation_;
    handoff.finish_request_sequence_=value.finish_request_sequence_;
    handoff.local_port_=value.local_port_;
    handoff.remote_port_=value.remote_port_;
    handoff.suffix_count_=value.suffix_count_;
    handoff.suffix_=value.suffix_;
    return true;
}

bool RuntimeMatchHistory::close_confirmed_prefix() noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::PrefixCpuCopied||!checkpoint_||
       !(endpoint_mask_&PrefixCpu)||reader_.active()||!live_owner(sealed_end_)||
       !gate(RuntimeMatchHistoryGatePoint::Close,sealed_end_)||
       !checkpoint_->close_history_endpoint(session_))
        return fail(RuntimeMatchHistoryFailure::Close,"Confirmed-prefix CPU endpoint close refused");
    phase_=RuntimeMatchHistoryPhase::PrefixClosed;
    return true;
}

bool RuntimeMatchHistory::retire_confirmed_prefix() noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::PrefixClosed||!checkpoint_||
       !live_owner(sealed_end_)||!gate(RuntimeMatchHistoryGatePoint::Retire,sealed_end_)||
       !checkpoint_->retire_history_confirmed(session_))
        return fail(RuntimeMatchHistoryFailure::Retire,"Confirmed-prefix retirement refused");
    reader_=GameplayCpuEndpointReader{};
    checkpoint_.reset();
    source_step_possible_=false;
    phase_=RuntimeMatchHistoryPhase::PrefixRetired;
    return true;
}

bool RuntimeMatchHistory::can_rebase_confirmed_prefix(
    std::uint32_t confirmed_end,RuntimeMatchHistoryPrefixHandoff& handoff,
    std::uint32_t rebase_request_sequence) const noexcept
{
    if(phase_!=RuntimeMatchHistoryPhase::PrefixRetired||checkpoint_||reader_.active()||
       failure_!=RuntimeMatchHistoryFailure::None||confirmed_end!=sealed_end_||
       confirmed_end<=pre_frame_||confirmed_end>=prefix_selected_end_||
       session_.source_frames()!=confirmed_end||!live_owner(confirmed_end)||
       !handoff.active_||handoff.old_pre_frame_!=pre_frame_||
       handoff.confirmed_frame_!=confirmed_end||
       handoff.selected_end_frame_!=prefix_selected_end_||
       handoff.old_incarnation_!=incarnation_||handoff.finish_request_sequence_==0||
       handoff.suffix_count_!=prefix_selected_end_-confirmed_end||
       handoff.suffix_cursor_!=0||!rebase_request_sequence||
       rebase_request_sequence<=handoff.finish_request_sequence_||
       !same_owner_tuple(owners_,handoff.owners_))
        return false;
    handoff.rebase_request_sequence_=rebase_request_sequence;
    return true;
}

bool RuntimeMatchHistory::begin_episode(RuntimeMatchHistoryPrefixHandoff& handoff) noexcept
{
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::CapturedPreCopied||
       !checkpoint_||!handoff.active_||handoff.rebase_request_sequence_==0||
       handoff.confirmed_frame_!=pre_frame_||handoff.suffix_cursor_!=0||
       handoff.suffix_count_!=handoff.selected_end_frame_-handoff.confirmed_frame_||
       handoff.suffix_count_==0||handoff.suffix_count_>prefix_suffix_rows_.size()||
       !same_owner_tuple(owners_,handoff.owners_)||!live_owner(pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase,
                    "Fresh PRE does not match the retained confirmed-prefix handoff");
    if(!begin_episode())return false;
    prefix_handoff_=&handoff;
    prefix_handoff_imported_=true;
    prefix_suffix_cursor_=handoff.suffix_cursor_;
    prefix_selected_end_=handoff.selected_end_frame_;
    local_port_=handoff.local_port_;
    remote_port_=handoff.remote_port_;
    prefix_suffix_rows_=handoff.suffix_;
    return true;
}

bool RuntimeMatchHistory::adopt_next_prefix_ticket() noexcept
{
    const auto current=session_.source_frames();
    const bool phase_ok=phase_==RuntimeMatchHistoryPhase::Armed||
        phase_==RuntimeMatchHistoryPhase::EpisodeRecording;
    if(!episode_mode_||!arena_mode_||!prefix_handoff_imported_||!prefix_handoff_||
       !prefix_handoff_->active_||!checkpoint_||!phase_ok||current_ticket_pending_||
       prefix_adoption_pending_||prefix_suffix_cursor_!=prefix_handoff_->suffix_cursor_||
       prefix_suffix_cursor_>=prefix_handoff_->suffix_count_||
       current!=prefix_handoff_->confirmed_frame_+prefix_suffix_cursor_||
       current<pre_frame_||current-pre_frame_!=ticket_count_||
       ticket_count_>=episode_rows_.size()||!live_owner(current)||
       !gate(RuntimeMatchHistoryGatePoint::ForwardBefore,current)||
       !checkpoint_->interval_boundary(session_))
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Native retained prefix ticket adoption refused before source work");
    const auto& original=prefix_suffix_rows_[prefix_suffix_cursor_];
    if(original.schema!=1||original.bytes!=sizeof(original)||
       (original.state!=1u&&original.state!=2u)||
       original.source_frame!=current||!original.ticket_id||!original.poll_serial||
       original.incarnation==0||original.incarnation>3u||
       original.local_port!=prefix_handoff_->local_port_||
       original.remote_port!=prefix_handoff_->remote_port_||
       original.local_port>=PAD_MAX_CONTROLLERS||original.remote_port>=PAD_MAX_CONTROLLERS||
       original.local_port==original.remote_port||
       (prefix_suffix_cursor_&&
        (original.ticket_id<=prefix_suffix_rows_[prefix_suffix_cursor_-1u].ticket_id||
         original.poll_serial<=prefix_suffix_rows_[prefix_suffix_cursor_-1u].poll_serial))||
       original.ticket_id<=last_ticket_id_||original.poll_serial<=poll_serial_)
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Stored prefix ticket identity or monotonicity refused");
    std::array<PADStatus,PAD_MAX_CONTROLLERS> decoded{};
    for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port)
        if(!melee_web_net_decode_pad(original.raw_pad.data()+port*MELEE_WEB_NET_PAD_BYTES,
              MELEE_WEB_NET_PAD_BYTES,&decoded[port]))
            return fail(RuntimeMatchHistoryFailure::Input,
                        "Stored prefix ticket raw PAD refused");
    episode_origin_rows_[ticket_count_]=original;
    auto& row=episode_rows_[ticket_count_];
    row=original;
    row.incarnation=incarnation_;
    row.state=0;
    row.forward_pad={};
    row.selected_pad={};
    row.reserved=0;
    latest_ticket_={};
    latest_ticket_.ticket_id=original.ticket_id;
    latest_ticket_.source_frame=original.source_frame;
    latest_ticket_.incarnation=incarnation_;
    latest_ticket_.local_port=original.local_port;
    latest_ticket_.remote_port=original.remote_port;
    latest_ticket_.poll_serial=original.poll_serial;
    latest_ticket_.raw_pad=original.raw_pad;
    ++ticket_count_;
    last_ticket_id_=original.ticket_id;
    poll_serial_=original.poll_serial;
    local_port_=original.local_port;
    remote_port_=original.remote_port;
    latest_ticket_valid_=true;
    current_ticket_pending_=true;
    prefix_adopted_cursor_=prefix_suffix_cursor_;
    prefix_adoption_pending_=true;
    return true;
}

std::uint32_t RuntimeMatchHistory::prefix_suffix_cursor() const noexcept
{
    return prefix_handoff_imported_&&prefix_handoff_?prefix_handoff_->suffix_cursor_:0;
}

std::uint32_t RuntimeMatchHistory::prefix_suffix_remaining() const noexcept
{
    if(!prefix_handoff_imported_||!prefix_handoff_||
       prefix_handoff_->suffix_cursor_>=prefix_handoff_->suffix_count_)
        return 0;
    return prefix_handoff_->suffix_count_-prefix_handoff_->suffix_cursor_;
}

bool RuntimeMatchHistory::begin_continuation() noexcept
{
    if(!episode_mode_||phase_!=RuntimeMatchHistoryPhase::Closed||!checkpoint_||
       (endpoint_mask_&PresentedFrontier)||current_ticket_pending_||
       incarnation_>=3u||sealed_end_<=pre_frame_||
       !live_owner(sealed_end_)||!gate(RuntimeMatchHistoryGatePoint::Arm,sealed_end_))
        return fail(RuntimeMatchHistoryFailure::Phase,"Episode continuation admission refused");
    if(!checkpoint_->begin_history_incarnation(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Owner,"Episode continuation incarnation refused");
    ++incarnation_;segment_start_frame_=sealed_end_;sealed_end_=0;replay_target_end_=0;
    replay_steps_=0;endpoint_mask_=0;endpoint_role_=EndpointRole::None;
    phase_=RuntimeMatchHistoryPhase::EpisodeRecording;source_step_possible_=true;
    return true;
}

bool RuntimeMatchHistory::forward_from_poll(
    const PADStatus raw[PAD_MAX_CONTROLLERS],std::uint64_t poll_serial,
    unsigned local_port,unsigned remote_port,const std::uint8_t* predicted_remote,
    unsigned predicted_remote_bytes) noexcept
{
    if(episode_mode_||phase_!=RuntimeMatchHistoryPhase::Armed||!checkpoint_||!raw||
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
    const bool presented_frontier=phase_==RuntimeMatchHistoryPhase::PresentedComplete;
    constexpr std::uint32_t predicted_required=PredictedCpu|PredictedDraw;
    const std::uint32_t required=presented_frontier?PresentedFrontier:predicted_required;
    if((phase_!=RuntimeMatchHistoryPhase::PredictedComplete&&!presented_frontier)||!checkpoint_||
       (episode_mode_?restore_count_>=2u:restore_count_!=0u)||
       (endpoint_mask_&required)!=required||!live_owner(sealed_end_)||
       (presented_frontier&&endpoint_mask_!=PresentedFrontier)||
       (presented_frontier&&!gate(RuntimeMatchHistoryGatePoint::PresentComplete,sealed_end_))||
       !gate(RuntimeMatchHistoryGatePoint::RestoreBefore,sealed_end_))
        return fail(RuntimeMatchHistoryFailure::Phase);
    if(!checkpoint_->archive_history_endpoint(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Restore,
                                  "Native typed history endpoint archive refused");
    char refusal[320]{};
    const auto restore_result=checkpoint_->restore_history_pre(session_,refusal,sizeof(refusal));
    if(restore_result!=GameplayCpuCommitResult::Ok){
        char operation[384]{};
        std::snprintf(operation,sizeof(operation),
            "Native typed history restore refused result=%u%s%s",
            static_cast<unsigned>(restore_result),refusal[0]?" ":"",refusal);
        return checkpoint_failure(RuntimeMatchHistoryFailure::Restore,operation);
    }
    audio_phase_=pre_audio_phase_;
    ++restore_count_;
    replay_target_end_=sealed_end_;replay_steps_=0;
    if(episode_mode_)endpoint_mask_=0;
    if((episode_mode_?restore_count_>2u:restore_count_!=1u)||session_.source_frames()!=pre_frame_||
       !live_owner(pre_frame_)||
       !gate(RuntimeMatchHistoryGatePoint::RestoreAfter,pre_frame_))
        return fail(RuntimeMatchHistoryFailure::Restore);
    phase_=RuntimeMatchHistoryPhase::RestoredPrePending;
    return true;
}

bool RuntimeMatchHistory::correct_from_retained(
    const std::uint8_t* late_remote,unsigned late_remote_bytes) noexcept
{
    if(episode_mode_||phase_!=RuntimeMatchHistoryPhase::Restored||!checkpoint_||!has_input_||
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
    last_present_source_frame_=expected;
    last_present_source_frame_valid_=true;
    phase_=pending;
    return true;
}

bool RuntimeMatchHistory::submit_recording_present() noexcept
{
    const auto current=session_.source_frames();
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::EpisodeRecording||
       !checkpoint_||current_ticket_pending_||!ticket_count_||ticket_count_!=forward_steps_||
       ticket_count_>episode_rows_.size()||current<=pre_frame_||
       current-pre_frame_!=forward_steps_||!live_owner(current)||
       (last_present_source_frame_valid_&&last_present_source_frame_==current)||
       recording_present_count_==UINT32_MAX||!gate(RuntimeMatchHistoryGatePoint::PresentSubmit,current))
        return fail(RuntimeMatchHistoryFailure::Phase,"Recording present requires one unpresented committed arena forward");

    const auto& latest=episode_rows_[ticket_count_-1u];
    if(latest.state!=1u||latest.source_frame+1u!=current||
       latest.ticket_id!=last_ticket_id_||latest.poll_serial!=poll_serial_||
       latest.incarnation!=incarnation_||!latest_ticket_valid_||
       latest_ticket_.ticket_id!=latest.ticket_id||latest_ticket_.source_frame!=latest.source_frame)
        return fail(RuntimeMatchHistoryFailure::Present,"Recording present does not bind the latest forward ticket");

    if(!present_||present_(session_,current,callback_context_)!=
       RuntimeMatchHistoryPresentResult::Submitted||!live_owner(current))
        return fail(RuntimeMatchHistoryFailure::Present,"Recording source present refused");
    last_present_source_frame_=current;
    last_present_source_frame_valid_=true;
    ++recording_present_count_;
    phase_=RuntimeMatchHistoryPhase::EpisodePresentPending;
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
    }else if(phase_==RuntimeMatchHistoryPhase::EpisodePresentPending&&last_present_source_frame_valid_){
        expected=last_present_source_frame_;idle=RuntimeMatchHistoryPhase::EpisodeRecording;
    }else return fail(RuntimeMatchHistoryFailure::Phase);
    if(!live_owner(expected)||
       !gate(RuntimeMatchHistoryGatePoint::PresentComplete,expected))
        return fail(RuntimeMatchHistoryFailure::Boundary);
    phase_=idle;
    return true;
}

bool RuntimeMatchHistory::seal_presented_frontier() noexcept
{
    const auto current=session_.source_frames();
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::EpisodeRecording||
       !checkpoint_||reader_.active()||endpoint_role_!=EndpointRole::None||
       current_ticket_pending_||!ticket_count_||ticket_count_!=forward_steps_||
       ticket_count_>episode_rows_.size()||current<=pre_frame_||
       current-pre_frame_!=forward_steps_||!live_owner(current)||
       !last_present_source_frame_valid_||last_present_source_frame_!=current)
        return fail(RuntimeMatchHistoryFailure::Phase,
                    "Presented frontier requires an idle recording present at the current frame");

    const auto& latest=episode_rows_[ticket_count_-1u];
    if(latest.state!=1u||latest.source_frame+1u!=current||
       latest.ticket_id!=last_ticket_id_||latest.poll_serial!=poll_serial_||
       latest.incarnation!=incarnation_||!latest_ticket_valid_||
       latest_ticket_.ticket_id!=latest.ticket_id||
       latest_ticket_.source_frame!=latest.source_frame||
       latest_ticket_.incarnation!=latest.incarnation||
       latest_ticket_.poll_serial!=latest.poll_serial)
        return fail(RuntimeMatchHistoryFailure::Present,
                    "Presented frontier does not bind the latest committed recording ticket");

    if(!gate(RuntimeMatchHistoryGatePoint::PresentComplete,current))
        return fail(RuntimeMatchHistoryFailure::Boundary,
                    "Presented frontier lost the admitted current renderer submission");
    const char* refusal=nullptr;
    if(!checkpoint_->seal_history_incarnation(session_,&refusal))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Seal,refusal);
    sealed_end_=current;replay_target_end_=current;endpoint_mask_=0;
    phase_=RuntimeMatchHistoryPhase::PresentedSealed;
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
    if(view==View::CpuEnd&&provenance==Provenance::Corrected&&
       phase_==RuntimeMatchHistoryPhase::PrefixSealed){
        role=EndpointRole::PrefixCpu;return true;
    }
    if(view==View::DrawEnd&&provenance==Provenance::Reference&&
       phase_==RuntimeMatchHistoryPhase::PresentedSealed){
        role=EndpointRole::PresentedFrontier;return true;
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

bool RuntimeMatchHistory::endpoint_arena(GameplayCpuArenaEndpoint& out) const noexcept
{
    out={};
    return reader_.active()&&endpoint_role_!=EndpointRole::None&&reader_.arena(out);
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
    case EndpointRole::PrefixCpu:
        endpoint_mask_|=PrefixCpu;phase_=RuntimeMatchHistoryPhase::PrefixCpuCopied;break;
    case EndpointRole::PresentedFrontier:
        endpoint_mask_|=PresentedFrontier;phase_=RuntimeMatchHistoryPhase::PresentedComplete;break;
    case EndpointRole::None:return fail(RuntimeMatchHistoryFailure::Endpoint);
    }
    return true;
}

bool RuntimeMatchHistory::close_selected_endpoint() noexcept
{
    const bool corrected=phase_==RuntimeMatchHistoryPhase::CorrectedComplete;
    const bool reference=phase_==RuntimeMatchHistoryPhase::PredictedComplete;
    const bool presented_frontier=phase_==RuntimeMatchHistoryPhase::PresentedComplete;
    const std::uint32_t required=presented_frontier?PresentedFrontier:
        (corrected?(CorrectedCpu|CorrectedDraw):(PredictedCpu|PredictedDraw));
    if((!corrected&&!reference&&!presented_frontier)||!checkpoint_||
       (endpoint_mask_&required)!=required||
       (presented_frontier&&endpoint_mask_!=PresentedFrontier)||reader_.active()||
       !live_owner(sealed_end_)||
       (presented_frontier&&!gate(RuntimeMatchHistoryGatePoint::PresentComplete,sealed_end_))||
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
    if(phase_!=RuntimeMatchHistoryPhase::Retired||checkpoint_||reader_.active()||
       failure_!=RuntimeMatchHistoryFailure::None||end_frame!=sealed_end_||
       session_.source_frames()!=end_frame||!live_owner(end_frame))
        return false;

    /* Preserve the historical single-tick route verbatim. Only explicitly
     * arena-backed episodes get the wider confirmed END admission below. */
    if(!episode_mode_)
        return !arena_mode_&&forward_steps_==1;

    if(!arena_mode_||current_ticket_pending_||end_frame<=pre_frame_)
        return false;
    const auto span=end_frame-pre_frame_;
    if(span>episode_rows_.size()||
       forward_steps_!=span||ticket_count_!=span||ticket_count_>episode_rows_.size()||
       incarnation_==0||incarnation_>3u||restore_count_>2u||
       !latest_ticket_valid_||!last_ticket_id_||!poll_serial_||
       latest_ticket_.ticket_id!=last_ticket_id_||latest_ticket_.poll_serial!=poll_serial_||
       latest_ticket_.source_frame!=end_frame-1u)
        return false;

    for(std::uint32_t index=0;index<ticket_count_;++index){
        const auto& row=episode_rows_[index];
        if((row.state!=1u&&row.state!=2u)||row.source_frame!=pre_frame_+index||
           !row.ticket_id||!row.poll_serial||row.incarnation==0||row.incarnation>incarnation_||
           row.local_port>=PAD_MAX_CONTROLLERS||row.remote_port>=PAD_MAX_CONTROLLERS||
           row.local_port==row.remote_port||row.local_port!=local_port_||row.remote_port!=remote_port_||
           (index&&(row.ticket_id<=episode_rows_[index-1u].ticket_id||
                    row.poll_serial<=episode_rows_[index-1u].poll_serial)))
            return false;
    }
    const auto& last=episode_rows_[ticket_count_-1u];
    if(latest_ticket_.ticket_id!=last.ticket_id||
       latest_ticket_.source_frame!=last.source_frame||
       latest_ticket_.incarnation!=last.incarnation||
       latest_ticket_.local_port!=last.local_port||
       latest_ticket_.remote_port!=last.remote_port||
       latest_ticket_.poll_serial!=last.poll_serial)
        return false;
    return true;
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

const RuntimeMatchHistoryPollTicket* RuntimeMatchHistory::latest_ticket() const noexcept
{
    return episode_mode_&&latest_ticket_valid_?&latest_ticket_:nullptr;
}

const RuntimeMatchHistoryInputReceipt* RuntimeMatchHistory::episode_input_row(
    std::uint32_t ordinal) const noexcept
{
    if(!episode_mode_||ordinal>=ticket_count_||ordinal>=episode_rows_.size()||
       !episode_rows_[ordinal].ticket_id)return nullptr;
    return &episode_rows_[ordinal];
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
