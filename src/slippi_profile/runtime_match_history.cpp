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
#include "gameplay_bootstrap.h"

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
    if(!result->live_owner(result->pre_frame_)){
        result->fail(RuntimeMatchHistoryFailure::Boundary);
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Boundary,
                        "Ordinary match owner changed during retained-history capture");
        result->checkpoint_.reset();
        return {};
    }
    if(arena_mode&&!result->bind_terminal_cursor()){
        result->fail(RuntimeMatchHistoryFailure::Capture,
                     "Arena terminal cursor could not bind the captured bootstrap PRE");
        capture_refusal(diagnostic,RuntimeMatchHistoryFailure::Capture,
                        "Arena terminal cursor could not bind the captured bootstrap PRE");
        result->checkpoint_.reset();
        return {};
    }
    if(!result->gate(RuntimeMatchHistoryGatePoint::CaptureComplete,result->pre_frame_)){
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
        char message[160]{};
        if(result==RuntimeMatchHistoryGateResult::SourceFilesBusy){
            const bool owned_pending_hps=interior_hps_pending(session_,expected);
            std::snprintf(message,sizeof(message),
                "Runtime history boundary refused: point=%u reason=%u expected=%u owned_pending_hps=%u",
                static_cast<unsigned>(point),static_cast<unsigned>(result),expected,
                owned_pending_hps?1u:0u);
        }else{
            std::snprintf(message,sizeof(message),"Runtime history boundary refused: point=%u reason=%u expected=%u",
                static_cast<unsigned>(point),static_cast<unsigned>(result),expected);
        }
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

bool RuntimeMatchHistory::read_terminal_cursor(
    std::uint32_t expected_source_frame,
    MeleeWebRuntimeMatchHistoryTerminalOwner& owner,
    std::uint64_t& completed_tick,std::uint32_t& ending,
    std::uint32_t& complete) const noexcept
{
    owner={};completed_tick=0;ending=complete=0;
    if(!arena_mode_||!episode_mode_||!checkpoint_)return false;
    const auto live=session_.diagnostic_cpu_owners();
    if(!same_owner_tuple(owners_,live)||live.frame!=expected_source_frame||
       session_.source_frames()!=expected_source_frame||
       melee_web_gameplay_generation()!=owners_.world)
        return false;
    const auto bootstrap=melee_web_gameplay_bootstrap_cpu_storage();
    if(!bootstrap.ticks||bootstrap.ticks_bytes!=sizeof(std::uint64_t))return false;
    owner.capsule_identity=this;
    owner.checkpoint_identity=checkpoint_.get();
    owner.storage_identity=live.storage;
    owner.context_identity=live.context;
    owner.flow_identity=live.flow;
    owner.audio_identity=live.audio;
    owner.ticks_identity=bootstrap.ticks;
    owner.world_generation=live.world;
    owner.context_epoch=live.context_epoch;
    owner.ticks_bytes=static_cast<std::uint32_t>(bootstrap.ticks_bytes);
    if(terminal_cursor_.bound&&
       !melee_web_runtime_match_history_terminal_owner_equal(&terminal_cursor_.owner,&owner))
        return false;
    completed_tick=*static_cast<const std::uint64_t*>(bootstrap.ticks);
    ending=session_.ending()?1u:0u;
    complete=session_.complete()?1u:0u;
    return !complete||ending;
}

bool RuntimeMatchHistory::read_terminal_tail_cursor(
    std::uint64_t expected_completed_tick,std::uint32_t expected_source_frame,
    bool allow_complete) const noexcept
{
    if(!arena_mode_||!episode_mode_||!terminal_cursor_.bound||
       !terminal_cursor_.owner.ticks_identity||
       !terminal_cursor_.owner.world_generation||
       !terminal_cursor_.owner.context_epoch||
       session_.source_frames()!=expected_source_frame||
       melee_web_gameplay_generation()!=terminal_cursor_.owner.world_generation)
        return false;
    const auto live=session_.diagnostic_cpu_owners();
    if(!same_owner_tuple(owners_,live)||live.frame!=expected_source_frame||
       live.world!=terminal_cursor_.owner.world_generation||
       live.context_epoch!=terminal_cursor_.owner.context_epoch||
       !session_.diagnostic_source_files_quiescent()||!session_.ending()||
       (session_.complete()&&!allow_complete))
        return false;
    const auto bootstrap=melee_web_gameplay_bootstrap_cpu_storage();
    if(bootstrap.ticks!=terminal_cursor_.owner.ticks_identity||
       bootstrap.ticks_bytes!=sizeof(std::uint64_t))
        return false;
    return *static_cast<const std::uint64_t*>(bootstrap.ticks)==expected_completed_tick;
}

bool RuntimeMatchHistory::bind_terminal_cursor() noexcept
{
    MeleeWebRuntimeMatchHistoryTerminalOwner owner{};
    std::uint64_t completed_tick=0;
    std::uint32_t ending=0,complete=0;
    return arena_mode_&&episode_mode_&&!terminal_cursor_.bound&&
        read_terminal_cursor(pre_frame_,owner,completed_tick,ending,complete)&&
        ending==0&&complete==0&&
        melee_web_runtime_match_history_terminal_bind(
            &terminal_cursor_,&owner,completed_tick,pre_frame_,pre_frame_)&&
        checkpoint_->history_bind_terminal_cursor(session_,
            static_cast<const std::uint64_t*>(owner.ticks_identity),owner.ticks_bytes,
            owner.world_generation,completed_tick);
}

bool RuntimeMatchHistory::rewind_terminal_cursor_pre() noexcept
{
    if(!arena_mode_)return true;
    MeleeWebRuntimeMatchHistoryTerminalOwner owner{};
    std::uint64_t completed_tick=0;
    std::uint32_t ending=0,complete=0;
    return read_terminal_cursor(pre_frame_,owner,completed_tick,ending,complete)&&
        melee_web_runtime_match_history_terminal_rewind_pre(
            &terminal_cursor_,&owner,completed_tick,pre_frame_,ending,complete);
}

bool RuntimeMatchHistory::observe_terminal_cursor(
    std::uint32_t source_frame_before,std::uint64_t tick_before,
    std::uint32_t ending_before,std::uint32_t complete_before,
    std::uint32_t source_frame_after) noexcept
{
    if(!arena_mode_)return true;
    MeleeWebRuntimeMatchHistoryTerminalOwner owner{};
    std::uint64_t tick_after=0;
    std::uint32_t ending_after=0,complete_after=0;
    return read_terminal_cursor(source_frame_after,owner,tick_after,ending_after,complete_after)&&
        melee_web_runtime_match_history_terminal_observe(
            &terminal_cursor_,&owner,tick_before,tick_after,
            source_frame_before,source_frame_after,ending_before,ending_after,
            complete_before,complete_after);
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

bool RuntimeMatchHistory::interior_hps_pending(
    const GameplayMatchSession& session,std::uint32_t expected) const noexcept
{
    return &session==&session_&&episode_mode_&&arena_mode_&&checkpoint_&&
        session.source_frames()==expected&&live_owner(expected)&&
        checkpoint_->interior_hps_pending(session);
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
       !gate(RuntimeMatchHistoryGatePoint::InteriorPoll,current))
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
       !gate(RuntimeMatchHistoryGatePoint::InteriorForwardBefore,current))
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

    MeleeWebRuntimeMatchHistoryTerminalOwner cursor_owner_before{};
    std::uint64_t cursor_tick_before=0;
    std::uint32_t cursor_ending_before=0,cursor_complete_before=0;
    if(arena_mode_&&(!read_terminal_cursor(current,cursor_owner_before,
           cursor_tick_before,cursor_ending_before,cursor_complete_before)||
       !melee_web_runtime_match_history_terminal_before(
           &terminal_cursor_,&cursor_owner_before,cursor_tick_before,current,
           cursor_ending_before,cursor_complete_before)))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Episode forward could not read its bound completed-bootstrap PRE cursor");

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
    if(arena_mode_&&!observe_terminal_cursor(current,cursor_tick_before,
           cursor_ending_before,cursor_complete_before,actual_frame))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Episode forward completed an unowned or out-of-span bootstrap traversal");
    const bool terminal_step=arena_mode_&&terminal_cursor_.selected_terminal_valid&&
        terminal_cursor_.selected_terminal_input_ordinal==pre_frame_+ticket_count_&&
        terminal_cursor_.selected_terminal_source_frame_before==current&&
        terminal_cursor_.selected_terminal_source_frame_after==actual_frame&&
        terminal_cursor_.selected_terminal_ending_after==1u&&
        terminal_cursor_.selected_terminal_complete_after==0u;
    const bool normal_step=actual_frame==current+1u;
    if(terminal_step){
        terminal_selected_source_frame_=actual_frame;
        terminal_selected_ordinal_=terminal_cursor_.selected_terminal_input_ordinal;
        terminal_selected_tick_=terminal_cursor_.selected_terminal_completed_tick;
    }
    if(normal_step||terminal_step)++forward_steps_;
    if((!normal_step&&!terminal_step)||forward_steps_>7u||
       (arena_mode_&&terminal_cursor_.input_ordinal!=pre_frame_+forward_steps_)||
       !live_owner(actual_frame)||
       !gate(terminal_step?RuntimeMatchHistoryGatePoint::TerminalForwardAfter:
                           RuntimeMatchHistoryGatePoint::InteriorForwardAfter,actual_frame))
        return fail(RuntimeMatchHistoryFailure::SourceTick,"Episode forward source frame boundary refused");
    if(prefix_adoption_pending_){
        ++prefix_handoff_->suffix_cursor_;
        prefix_suffix_cursor_=prefix_handoff_->suffix_cursor_;
        prefix_adoption_pending_=false;
    }
    phase_=terminal_step?RuntimeMatchHistoryPhase::TerminalRecording:
                         RuntimeMatchHistoryPhase::EpisodeRecording;
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
    sealed_end_=actual;replay_target_end_=actual;replay_target_ordinal_=actual;endpoint_mask_=0;
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
       current<pre_frame_||current>=replay_target_ordinal_||current-pre_frame_>=episode_rows_.size()||
       replay_steps_>=7u||!live_owner(current)||
       !gate(RuntimeMatchHistoryGatePoint::InteriorReplayBefore,current))
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

    MeleeWebRuntimeMatchHistoryTerminalOwner cursor_owner_before{};
    std::uint64_t cursor_tick_before=0;
    std::uint32_t cursor_ending_before=0,cursor_complete_before=0;
    if(arena_mode_&&(!read_terminal_cursor(current,cursor_owner_before,
           cursor_tick_before,cursor_ending_before,cursor_complete_before)||
       !melee_web_runtime_match_history_terminal_before(
           &terminal_cursor_,&cursor_owner_before,cursor_tick_before,current,
           cursor_ending_before,cursor_complete_before)))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Episode replay could not read its bound completed-bootstrap PRE cursor");

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
    if(arena_mode_&&!observe_terminal_cursor(current,cursor_tick_before,
           cursor_ending_before,cursor_complete_before,actual_frame))
        return fail(RuntimeMatchHistoryFailure::Correction,
                    "Episode replay completed an unowned or out-of-span bootstrap traversal");
    const bool terminal_step=arena_mode_&&terminal_cursor_.selected_terminal_valid&&
        terminal_cursor_.selected_terminal_input_ordinal==pre_frame_+replay_steps_+1u&&
        terminal_cursor_.selected_terminal_source_frame_before==current&&
        terminal_cursor_.selected_terminal_source_frame_after==actual_frame&&
        terminal_cursor_.selected_terminal_ending_after==1u&&
        terminal_cursor_.selected_terminal_complete_after==0u;
    const bool normal_step=actual_frame==current+1u;
    if(terminal_step){
        terminal_selected_source_frame_=actual_frame;
        terminal_selected_ordinal_=terminal_cursor_.selected_terminal_input_ordinal;
        terminal_selected_tick_=terminal_cursor_.selected_terminal_completed_tick;
    }
    if(normal_step||terminal_step)++replay_steps_;
    if((!normal_step&&!terminal_step)||replay_steps_>7u||
       (arena_mode_&&terminal_cursor_.input_ordinal!=pre_frame_+replay_steps_)||
       !live_owner(actual_frame)||
       !gate(terminal_step?RuntimeMatchHistoryGatePoint::TerminalReplayAfter:
                           RuntimeMatchHistoryGatePoint::InteriorReplayAfter,actual_frame))
        return fail(RuntimeMatchHistoryFailure::Correction,"Episode replay source frame boundary refused");
    return true;
}

bool RuntimeMatchHistory::finish_replay() noexcept
{
    if(!episode_mode_||phase_!=RuntimeMatchHistoryPhase::Restored||!checkpoint_||
       current_ticket_pending_||replay_target_ordinal_<=pre_frame_||
       replay_target_ordinal_-pre_frame_>episode_rows_.size()||
       terminal_cursor_.selected_terminal_valid||
       session_.source_frames()!=replay_target_ordinal_||
       replay_steps_!=replay_target_ordinal_-pre_frame_||
       !live_owner(replay_target_ordinal_)||
       !gate(RuntimeMatchHistoryGatePoint::CorrectAfter,replay_target_ordinal_))
        return fail(RuntimeMatchHistoryFailure::Correction,"Episode replay did not reach the exact sealed END");
    if(!checkpoint_->finish_history_replay(session_))
        return checkpoint_failure(RuntimeMatchHistoryFailure::Correction,"Episode replay finish refused");
    sealed_end_=replay_target_ordinal_;replay_target_end_=sealed_end_;endpoint_mask_=0;
    phase_=RuntimeMatchHistoryPhase::CorrectedSealed;
    return true;
}

bool RuntimeMatchHistory::finish_confirmed_prefix(std::uint32_t confirmed_end) noexcept
{
    const auto actual=session_.source_frames();
    if(episode_mode_&&arena_mode_&&phase_==RuntimeMatchHistoryPhase::Restored&&
       checkpoint_&&terminal_cursor_.selected_terminal_valid){
        const auto selected_ordinal=terminal_cursor_.selected_terminal_input_ordinal;
        if(current_ticket_pending_||prefix_adoption_pending_||
           confirmed_end!=actual||terminal_cursor_.selected_terminal_source_frame_after!=actual||
           selected_ordinal!=actual+1u||selected_ordinal<=pre_frame_||
           selected_ordinal>replay_target_ordinal_||replay_target_ordinal_<=pre_frame_||
           replay_target_ordinal_-pre_frame_>episode_rows_.size()||
           forward_steps_!=replay_target_ordinal_-pre_frame_||
           ticket_count_!=forward_steps_||replay_steps_!=selected_ordinal-pre_frame_||
           !live_owner(actual)||
           !gate(RuntimeMatchHistoryGatePoint::TerminalReplayAfter,actual))
            return fail(RuntimeMatchHistoryFailure::Correction,
                        "Confirmed terminal finish requires the selected actual terminal traversal");
        for(std::uint32_t index=0;index<ticket_count_;++index){
            const auto& row=episode_rows_[index];
            if((row.state!=1u&&row.state!=2u)||row.source_frame!=pre_frame_+index||
               !row.ticket_id||!row.poll_serial||row.incarnation==0||row.incarnation>incarnation_||
               row.local_port!=local_port_||row.remote_port!=remote_port_)
                return fail(RuntimeMatchHistoryFailure::Input,
                            "Confirmed terminal input receipt interval is incomplete");
        }
        bool sealed=false;
        if(selected_ordinal==replay_target_ordinal_){
            sealed=checkpoint_->finish_history_terminal_replay(session_,
                terminal_cursor_.selected_terminal_completed_tick,selected_ordinal,actual);
            prefix_selected_end_=selected_ordinal;
        }else{
            sealed=checkpoint_->finish_history_confirmed_terminal(session_,
                terminal_cursor_.selected_terminal_completed_tick,selected_ordinal,actual);
            prefix_selected_end_=replay_target_ordinal_;
        }
        if(!sealed)
            return checkpoint_failure(RuntimeMatchHistoryFailure::Correction,
                                      "Native confirmed terminal endpoint admission refused");
        terminal_selected_source_frame_=actual;
        terminal_selected_ordinal_=selected_ordinal;
        terminal_selected_tick_=terminal_cursor_.selected_terminal_completed_tick;
        terminal_last_presented_tick_=0;
        terminal_checkpoint_sealed_=true;
        terminal_needs_present_=true;
        sealed_end_=actual;replay_target_end_=actual;endpoint_mask_=0;
        phase_=RuntimeMatchHistoryPhase::TerminalSealed;
        return true;
    }
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
    replay_target_ordinal_=0;
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
    const bool terminal_endpoint=phase_==RuntimeMatchHistoryPhase::TerminalCpuCopied;
    constexpr std::uint32_t predicted_required=PredictedCpu|PredictedDraw;
    const std::uint32_t required=terminal_endpoint?TerminalEnd:
        presented_frontier?PresentedFrontier:predicted_required;
    if((phase_!=RuntimeMatchHistoryPhase::PredictedComplete&&!presented_frontier&&!terminal_endpoint)||!checkpoint_||
       (episode_mode_?restore_count_>=2u:restore_count_!=0u)||
       (endpoint_mask_&required)!=required||!live_owner(sealed_end_)||
       (presented_frontier&&endpoint_mask_!=PresentedFrontier)||
       (terminal_endpoint&&endpoint_mask_!=TerminalEnd)||
       (terminal_endpoint&&!gate(RuntimeMatchHistoryGatePoint::TerminalPresentComplete,sealed_end_))||
       (presented_frontier&&!gate(RuntimeMatchHistoryGatePoint::PresentComplete,sealed_end_))||
       !gate(terminal_endpoint?RuntimeMatchHistoryGatePoint::TerminalRestoreBefore:
                               RuntimeMatchHistoryGatePoint::RestoreBefore,sealed_end_))
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
    if(arena_mode_){
        if(!rewind_terminal_cursor_pre())
            return fail(RuntimeMatchHistoryFailure::Restore,
                        "Native checkpoint restore did not return the bound bootstrap cursor to PRE");
        terminal_selected_source_frame_=terminal_selected_ordinal_=0;
        terminal_selected_tick_=0;
        terminal_last_presented_tick_=0;
        terminal_checkpoint_sealed_=false;
        terminal_needs_present_=false;
    }
    audio_phase_=pre_audio_phase_;
    ++restore_count_;
    last_present_source_frame_valid_=false;
    replay_target_end_=sealed_end_;
    replay_target_ordinal_=pre_frame_+forward_steps_;
    replay_steps_=0;
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
    const bool terminal=phase_==RuntimeMatchHistoryPhase::TerminalRecording||
        (phase_==RuntimeMatchHistoryPhase::TerminalSealed&&terminal_needs_present_);
    if(!episode_mode_||!arena_mode_||
       (phase_!=RuntimeMatchHistoryPhase::EpisodeRecording&&!terminal)||
       !checkpoint_||current_ticket_pending_||!ticket_count_||ticket_count_!=forward_steps_||
       ticket_count_>episode_rows_.size()||
       (terminal?(!terminal_cursor_.selected_terminal_valid||
                  terminal_cursor_.selected_terminal_input_ordinal<=pre_frame_||
                  terminal_cursor_.selected_terminal_input_ordinal-pre_frame_>forward_steps_||
                  terminal_cursor_.selected_terminal_input_ordinal-pre_frame_>ticket_count_||
                  terminal_cursor_.selected_terminal_source_frame_after!=current||
                  terminal_cursor_.selected_terminal_source_frame_before!=current||
                  terminal_cursor_.selected_terminal_ending_after!=1u||
                  terminal_cursor_.selected_terminal_complete_after!=0u||
                  terminal_cursor_.input_ordinal!=terminal_cursor_.selected_terminal_input_ordinal||
                  terminal_cursor_.source_frame!=current||
                  terminal_selected_source_frame_!=current||
                  terminal_selected_ordinal_!=terminal_cursor_.selected_terminal_input_ordinal||
                  terminal_selected_tick_!=terminal_cursor_.selected_terminal_completed_tick):
                 (current<=pre_frame_||current-pre_frame_!=forward_steps_))||!live_owner(current)||
       (terminal?terminal_last_presented_tick_==
                     terminal_cursor_.selected_terminal_completed_tick:
                 (last_present_source_frame_valid_&&last_present_source_frame_==current))||
       recording_present_count_==UINT32_MAX||
       !gate(terminal?RuntimeMatchHistoryGatePoint::TerminalPresentSubmit:
                      RuntimeMatchHistoryGatePoint::InteriorPresentSubmit,current))
        return fail(RuntimeMatchHistoryFailure::Phase,"Recording present requires one unpresented committed arena forward");

    if(terminal){
        const auto selected_span=terminal_cursor_.selected_terminal_input_ordinal-pre_frame_;
        const auto selected_index=selected_span-1u;
        const auto& selected=episode_rows_[selected_index];
        const auto& origin=episode_origin_rows_[selected_index];
        if((selected.state!=1u&&selected.state!=2u)||
           (origin.state!=1u&&origin.state!=2u)||
           selected.source_frame!=current||origin.source_frame!=current||
           selected.ticket_id!=origin.ticket_id||selected.poll_serial!=origin.poll_serial||
           selected.incarnation==0||selected.incarnation>incarnation_||
           selected.local_port!=origin.local_port||selected.remote_port!=origin.remote_port||
           selected.raw_pad!=origin.raw_pad||!origin.ticket_id||!origin.poll_serial||
           origin.incarnation==0||origin.incarnation>3u||
           origin.local_port!=local_port_||origin.remote_port!=remote_port_||
           origin.local_port>=PAD_MAX_CONTROLLERS||origin.remote_port>=PAD_MAX_CONTROLLERS||
           origin.local_port==origin.remote_port)
            return fail(RuntimeMatchHistoryFailure::Present,
                        "Terminal recording present does not bind the selected immutable ticket");
    }else{
        const auto& latest=episode_rows_[ticket_count_-1u];
        if(latest.state!=1u||latest.source_frame+1u!=current||
           latest.ticket_id!=last_ticket_id_||latest.poll_serial!=poll_serial_||
           latest.incarnation!=incarnation_||!latest_ticket_valid_||
           latest_ticket_.ticket_id!=latest.ticket_id||latest_ticket_.source_frame!=latest.source_frame)
            return fail(RuntimeMatchHistoryFailure::Present,"Recording present does not bind the latest forward ticket");
    }

    if(!present_||present_(session_,current,callback_context_)!=
       RuntimeMatchHistoryPresentResult::Submitted||!live_owner(current))
        return fail(RuntimeMatchHistoryFailure::Present,"Recording source present refused");
    last_present_source_frame_=current;
    last_present_source_frame_valid_=true;
    if(terminal)
        terminal_last_presented_tick_=terminal_cursor_.selected_terminal_completed_tick;
    ++recording_present_count_;
    phase_=terminal?RuntimeMatchHistoryPhase::TerminalPresentPending:
                    RuntimeMatchHistoryPhase::EpisodePresentPending;
    if(terminal)terminal_needs_present_=false;
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
    }else if(phase_==RuntimeMatchHistoryPhase::TerminalPresentPending&&last_present_source_frame_valid_){
        expected=last_present_source_frame_;
        idle=terminal_checkpoint_sealed_?RuntimeMatchHistoryPhase::TerminalPresentIdle:
                                        RuntimeMatchHistoryPhase::TerminalRecording;
    }else if(phase_==RuntimeMatchHistoryPhase::TerminalTailPresentPending&&
             last_present_source_frame_valid_){
        expected=last_present_source_frame_;
        idle=terminal_tail_complete_after_?
            RuntimeMatchHistoryPhase::TerminalTailPresented:
            RuntimeMatchHistoryPhase::TerminalTailReady;
    }else return fail(RuntimeMatchHistoryFailure::Phase);
    const auto completion_gate=phase_==RuntimeMatchHistoryPhase::EpisodePresentPending?
        RuntimeMatchHistoryGatePoint::InteriorPresentComplete:
        phase_==RuntimeMatchHistoryPhase::TerminalPresentPending?
            RuntimeMatchHistoryGatePoint::TerminalPresentComplete:
        phase_==RuntimeMatchHistoryPhase::TerminalTailPresentPending?
            RuntimeMatchHistoryGatePoint::TerminalTailPresentComplete:
            RuntimeMatchHistoryGatePoint::PresentComplete;
    const bool tail=phase_==RuntimeMatchHistoryPhase::TerminalTailPresentPending;
    if(!(tail?read_terminal_tail_cursor(terminal_tail_completed_tick_,expected,true):
              live_owner(expected))||!gate(completion_gate,expected))
        return fail(RuntimeMatchHistoryFailure::Boundary);
    if(phase_==RuntimeMatchHistoryPhase::TerminalPresentPending&&
       (!terminal_cursor_.selected_terminal_valid||
        terminal_last_presented_tick_!=terminal_cursor_.selected_terminal_completed_tick))
        return fail(RuntimeMatchHistoryFailure::Present,
                    "Terminal present completion is not bound to the selected completed traversal");
    if(tail)terminal_tail_last_presented_ordinal_=terminal_tail_input_ordinal_;
    phase_=idle;
    return true;
}

bool RuntimeMatchHistory::seal_presented_frontier() noexcept
{
    const auto current=session_.source_frames();
    const bool terminal=phase_==RuntimeMatchHistoryPhase::TerminalRecording;
    if(!episode_mode_||!arena_mode_||
       (phase_!=RuntimeMatchHistoryPhase::EpisodeRecording&&!terminal)||
       !checkpoint_||reader_.active()||endpoint_role_!=EndpointRole::None||
       current_ticket_pending_||!ticket_count_||ticket_count_!=forward_steps_||
       ticket_count_>episode_rows_.size()||
       (terminal?(!terminal_cursor_.selected_terminal_valid||
                  terminal_cursor_.selected_terminal_source_frame_after!=current||
                  terminal_cursor_.selected_terminal_input_ordinal-pre_frame_!=forward_steps_):
                 (current<=pre_frame_||current-pre_frame_!=forward_steps_))||!live_owner(current)||
       !last_present_source_frame_valid_||last_present_source_frame_!=current||
       (terminal&&terminal_last_presented_tick_!=
                   terminal_cursor_.selected_terminal_completed_tick))
        return fail(RuntimeMatchHistoryFailure::Phase,
                    "Presented frontier requires an idle recording present at the current frame");

    const auto& latest=episode_rows_[ticket_count_-1u];
    if(latest.state!=1u||(terminal?latest.source_frame!=current:latest.source_frame+1u!=current)||
       latest.ticket_id!=last_ticket_id_||latest.poll_serial!=poll_serial_||
       latest.incarnation!=incarnation_||!latest_ticket_valid_||
       latest_ticket_.ticket_id!=latest.ticket_id||
       latest_ticket_.source_frame!=latest.source_frame||
       latest_ticket_.incarnation!=latest.incarnation||
       latest_ticket_.poll_serial!=latest.poll_serial)
        return fail(RuntimeMatchHistoryFailure::Present,
                    "Presented frontier does not bind the latest committed recording ticket");

    if(!gate(terminal?RuntimeMatchHistoryGatePoint::TerminalPresentComplete:
                      RuntimeMatchHistoryGatePoint::PresentComplete,current))
        return fail(RuntimeMatchHistoryFailure::Boundary,
                    "Presented frontier lost the admitted current renderer submission");
    const char* refusal=nullptr;
    const bool sealed=terminal?
        checkpoint_->seal_history_terminal_incarnation(session_,
            terminal_cursor_.selected_terminal_completed_tick,
            terminal_cursor_.selected_terminal_input_ordinal,current,&refusal):
        checkpoint_->seal_history_incarnation(session_,&refusal);
    if(!sealed)
        return checkpoint_failure(RuntimeMatchHistoryFailure::Seal,refusal);
    sealed_end_=current;replay_target_end_=current;
    replay_target_ordinal_=pre_frame_+forward_steps_;endpoint_mask_=0;
    if(terminal){
        terminal_selected_source_frame_=current;
        terminal_selected_ordinal_=terminal_cursor_.selected_terminal_input_ordinal;
        terminal_selected_tick_=terminal_cursor_.selected_terminal_completed_tick;
        terminal_last_presented_tick_=0;
        terminal_checkpoint_sealed_=true;
        terminal_needs_present_=false;
        phase_=RuntimeMatchHistoryPhase::TerminalSealed;
    }else phase_=RuntimeMatchHistoryPhase::PresentedSealed;
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
    if(view==View::TerminalEnd&&provenance==Provenance::Terminal&&
       (phase_==RuntimeMatchHistoryPhase::TerminalSealed||
        phase_==RuntimeMatchHistoryPhase::TerminalPresentIdle)){
        role=EndpointRole::TerminalEnd;return true;
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
    const auto begin_point=role==EndpointRole::TerminalEnd?
        RuntimeMatchHistoryGatePoint::TerminalEndpointBegin:
        RuntimeMatchHistoryGatePoint::EndpointBegin;
    if(!live_owner(expected)||!gate(begin_point,expected))
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
    return end_endpoint_batch_impl(EndpointCompletionMode::CopiedRows);
}

bool RuntimeMatchHistory::admit_endpoint_batch(View view,
                                               Provenance provenance) noexcept
{
    if(!begin_endpoint_batch(view,provenance))return false;
    return end_endpoint_batch_impl(EndpointCompletionMode::ImmediateAdmission);
}

bool RuntimeMatchHistory::end_endpoint_batch_impl(
    EndpointCompletionMode mode) noexcept
{
    if(!reader_.active()||endpoint_role_==EndpointRole::None)
        return fail(RuntimeMatchHistoryFailure::Phase);
    const EndpointRole role=endpoint_role_;
    const std::uint32_t expected=(role==EndpointRole::CapturedPre||
        role==EndpointRole::ArmedPre||role==EndpointRole::RestoredPre)?
        pre_frame_:sealed_end_;
    const auto completion_point=role==EndpointRole::TerminalEnd?
        RuntimeMatchHistoryGatePoint::TerminalEndpointComplete:
        RuntimeMatchHistoryGatePoint::EndpointComplete;
    if(endpoint_read_failure_||
       !(mode==EndpointCompletionMode::ImmediateAdmission?
             reader_.complete_immediate():reader_.revalidate())||!live_owner(expected)||
       !gate(completion_point,expected))
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
    case EndpointRole::TerminalEnd:
        endpoint_mask_|=TerminalEnd;phase_=RuntimeMatchHistoryPhase::TerminalCpuCopied;break;
    case EndpointRole::None:return fail(RuntimeMatchHistoryFailure::Endpoint);
    }
    return true;
}

bool RuntimeMatchHistory::close_selected_endpoint() noexcept
{
    const bool corrected=phase_==RuntimeMatchHistoryPhase::CorrectedComplete;
    const bool reference=phase_==RuntimeMatchHistoryPhase::PredictedComplete;
    const bool presented_frontier=phase_==RuntimeMatchHistoryPhase::PresentedComplete;
    const bool terminal_endpoint=phase_==RuntimeMatchHistoryPhase::TerminalCpuCopied;
    const std::uint32_t required=terminal_endpoint?TerminalEnd:
        presented_frontier?PresentedFrontier:
        (corrected?(CorrectedCpu|CorrectedDraw):(PredictedCpu|PredictedDraw));
    if(!corrected&&!reference&&!presented_frontier&&!terminal_endpoint)
        return fail(RuntimeMatchHistoryFailure::Close,"Close endpoint phase refused");
    if(!checkpoint_)
        return fail(RuntimeMatchHistoryFailure::Close,"Close endpoint owner refused");
    if((endpoint_mask_&required)!=required||
       (presented_frontier&&endpoint_mask_!=PresentedFrontier)||
       (terminal_endpoint&&endpoint_mask_!=TerminalEnd)||reader_.active())
        return fail(RuntimeMatchHistoryFailure::Close,"Close endpoint receipt or reader refused");
    if(!live_owner(sealed_end_))
        return fail(RuntimeMatchHistoryFailure::Close,"Close endpoint owner refused");
    if(((presented_frontier||terminal_endpoint)&&
        !gate(terminal_endpoint?RuntimeMatchHistoryGatePoint::TerminalPresentComplete:
                               RuntimeMatchHistoryGatePoint::PresentComplete,sealed_end_))||
       !gate(terminal_endpoint?RuntimeMatchHistoryGatePoint::TerminalClose:
                               RuntimeMatchHistoryGatePoint::Close,sealed_end_))
        return fail(RuntimeMatchHistoryFailure::Close);
    char refusal[192]{};
    if(!checkpoint_->close_history_endpoint(session_,refusal,sizeof(refusal)))
        return fail(RuntimeMatchHistoryFailure::Close,refusal[0]?refusal:"Close checkpoint endpoint refused");
    phase_=terminal_endpoint?RuntimeMatchHistoryPhase::TerminalClosed:
                             RuntimeMatchHistoryPhase::Closed;
    return true;
}

bool RuntimeMatchHistory::retire_confirmed() noexcept
{
    const bool terminal=phase_==RuntimeMatchHistoryPhase::TerminalClosed;
    if((phase_!=RuntimeMatchHistoryPhase::Closed&&!terminal)||!checkpoint_||
       !live_owner(sealed_end_)||
       !gate(terminal?RuntimeMatchHistoryGatePoint::TerminalRetire:
                      RuntimeMatchHistoryGatePoint::Retire,sealed_end_)||
       !checkpoint_->retire_history_confirmed(session_))
        return fail(RuntimeMatchHistoryFailure::Retire);
    reader_=GameplayCpuEndpointReader{};
    checkpoint_.reset();
    source_step_possible_=false;
    phase_=terminal?RuntimeMatchHistoryPhase::TerminalRetired:
                    RuntimeMatchHistoryPhase::Retired;
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

RuntimeMatchHistoryTerminalReceipt RuntimeMatchHistory::terminal_receipt() const noexcept
{
    RuntimeMatchHistoryTerminalReceipt out{};
    if(!arena_mode_||!episode_mode_||!terminal_cursor_.first_terminal_valid)return {};
    out.flags=1u;
    out.incarnation=incarnation_;
    out.pre_source_frame=terminal_cursor_.pre_source_frame;
    out.pre_ordinal=terminal_cursor_.pre_input_ordinal;
    out.first_ordinal=terminal_cursor_.first_terminal_input_ordinal;
    out.first_source_frame=terminal_cursor_.first_terminal_source_frame_after;
    out.pre_completed_tick=terminal_cursor_.pre_completed_tick;
    out.first_completed_tick=terminal_cursor_.first_terminal_completed_tick;
    out.world_generation=terminal_cursor_.owner.world_generation;
    out.context_epoch=terminal_cursor_.owner.context_epoch;
    if(!terminal_cursor_.selected_terminal_valid)return out;
    const auto index=terminal_cursor_.selected_terminal_input_ordinal-
        terminal_cursor_.pre_input_ordinal-1u;
    if(index>=ticket_count_||index>=episode_rows_.size())return {};
    const auto& row=episode_rows_[index];
    if((row.state!=1u&&row.state!=2u)||!row.ticket_id||!row.poll_serial||
       row.incarnation==0||row.incarnation>incarnation_||
       row.source_frame!=terminal_cursor_.selected_terminal_source_frame_before||
       row.local_port>=PAD_MAX_CONTROLLERS||row.remote_port>=PAD_MAX_CONTROLLERS||
       row.local_port==row.remote_port)return {};
    out.flags|=2u;
    out.selected_source_frame=terminal_cursor_.selected_terminal_source_frame_after;
    out.selected_ordinal=terminal_cursor_.selected_terminal_input_ordinal;
    out.selected_completed_tick=terminal_cursor_.selected_terminal_completed_tick;
    out.ticket_id=row.ticket_id;
    out.poll_serial=row.poll_serial;
    out.local_port=row.local_port;
    out.remote_port=row.remote_port;
    return out;
}

bool RuntimeMatchHistory::terminal_selected_at(std::uint32_t source_frame) const noexcept
{
    return checkpoint_&&terminal_cursor_.selected_terminal_valid&&
        terminal_cursor_.selected_terminal_source_frame_after==source_frame&&
        terminal_selected_source_frame_==source_frame&&terminal_selected_ordinal_>
            terminal_cursor_.pre_input_ordinal&&
        session_.source_frames()==source_frame&&session_.ending()&&!session_.complete()&&
        checkpoint_->history_terminal_cursor_admit(session_,terminal_selected_tick_,
            terminal_selected_ordinal_,source_frame,true,false);
}

bool RuntimeMatchHistory::begin_terminal_tail(std::uint32_t selected_ordinal) noexcept
{
    if(!episode_mode_||!arena_mode_||phase_!=RuntimeMatchHistoryPhase::TerminalRetired||
       checkpoint_||reader_.active()||failure_!=RuntimeMatchHistoryFailure::None||
       terminal_tail_active_||!terminal_cursor_.selected_terminal_valid||
       !terminal_cursor_.first_terminal_valid||
       selected_ordinal!=terminal_cursor_.selected_terminal_input_ordinal||
       selected_ordinal!=terminal_selected_ordinal_||
       terminal_cursor_.selected_terminal_source_frame_after!=terminal_selected_source_frame_||
       terminal_cursor_.selected_terminal_completed_tick!=terminal_selected_tick_||
       terminal_cursor_.selected_terminal_ending_after!=1u||
       terminal_cursor_.selected_terminal_complete_after!=0u||
       terminal_cursor_.input_ordinal!=selected_ordinal||
       terminal_cursor_.source_frame!=terminal_selected_source_frame_||
       terminal_cursor_.completed_tick!=terminal_selected_tick_||
       selected_ordinal<=terminal_cursor_.pre_input_ordinal||
       selected_ordinal-terminal_cursor_.pre_input_ordinal>ticket_count_||
       ticket_count_>episode_rows_.size()||!ticket_count_||
       !session_.ending()||session_.complete()||
       !live_owner(terminal_selected_source_frame_)||
       !read_terminal_tail_cursor(terminal_selected_tick_,
                                  terminal_selected_source_frame_,false)||
       !gate(RuntimeMatchHistoryGatePoint::TerminalTailBegin,
             terminal_selected_source_frame_))
        return fail(RuntimeMatchHistoryFailure::Phase,
                    "Confirmed terminal tail does not match the selected retired endpoint");

    const std::uint32_t consumed=selected_ordinal-terminal_cursor_.pre_input_ordinal;
    const std::uint32_t selected_index=consumed-1u;
    const auto& selected=episode_rows_[selected_index];
    const auto& selected_origin=episode_origin_rows_[selected_index];
    if((selected.state!=1u&&selected.state!=2u)||
       (selected_origin.state!=1u&&selected_origin.state!=2u)||
       selected.source_frame!=terminal_selected_source_frame_||
       selected_origin.source_frame!=selected.source_frame||
       selected.ticket_id!=selected_origin.ticket_id||
       selected.poll_serial!=selected_origin.poll_serial||
       selected.local_port!=local_port_||selected.remote_port!=remote_port_||
       selected_origin.local_port!=local_port_||selected_origin.remote_port!=remote_port_||
       selected.raw_pad!=selected_origin.raw_pad||
       selected_origin.incarnation==0||selected_origin.incarnation>incarnation_)
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Selected terminal input receipt changed before confirmed tail");

    const std::uint32_t suffix_count=ticket_count_-consumed;
    if(suffix_count>terminal_tail_suffix_rows_.size())
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Confirmed terminal suffix exceeds fixed native receipt storage");
    std::array<RuntimeMatchHistoryInputReceipt,7> suffix{};
    std::uint32_t previous_ticket=selected_origin.ticket_id;
    std::uint64_t previous_poll=selected_origin.poll_serial;
    for(std::uint32_t index=0;index<suffix_count;++index){
        const std::uint32_t row_index=consumed+index;
        const auto& live=episode_rows_[row_index];
        const auto& origin=episode_origin_rows_[row_index];
        const std::uint32_t expected_input=selected_ordinal+index;
        if((live.state!=1u&&live.state!=2u)||
           (origin.state!=1u&&origin.state!=2u)||
           live.source_frame!=expected_input||origin.source_frame!=expected_input||
           live.ticket_id!=origin.ticket_id||live.poll_serial!=origin.poll_serial||
           live.incarnation==0||live.incarnation>incarnation_||
           origin.incarnation==0||origin.incarnation>incarnation_||
           live.local_port!=local_port_||live.remote_port!=remote_port_||
           origin.local_port!=local_port_||origin.remote_port!=remote_port_||
           live.raw_pad!=origin.raw_pad||!origin.ticket_id||!origin.poll_serial||
           origin.ticket_id<=previous_ticket||origin.poll_serial<=previous_poll)
            return fail(RuntimeMatchHistoryFailure::Input,
                        "Confirmed terminal suffix ticket provenance changed");
        suffix[index]=origin;
        previous_ticket=origin.ticket_id;
        previous_poll=origin.poll_serial;
    }

    terminal_tail_suffix_rows_=suffix;
    terminal_tail_suffix_count_=suffix_count;
    terminal_tail_suffix_cursor_=0;
    terminal_tail_source_frame_=terminal_selected_source_frame_;
    terminal_tail_input_ordinal_=selected_ordinal;
    terminal_tail_last_input_ordinal_=selected_ordinal-1u;
    terminal_tail_incarnation_=incarnation_;
    terminal_tail_completed_tick_=terminal_selected_tick_;
    terminal_tail_last_ticket_id_=selected.ticket_id;
    terminal_tail_last_poll_serial_=selected.poll_serial;
    terminal_tail_last_presented_ordinal_=selected_ordinal;
    terminal_tail_ticket_={};
    terminal_tail_applied_={};
    terminal_tail_ticket_pending_=false;
    terminal_tail_remote_pending_=false;
    terminal_tail_adopted_pending_=false;
    terminal_tail_complete_after_=false;
    terminal_tail_active_=true;
    phase_=RuntimeMatchHistoryPhase::TerminalTailReady;
    return true;
}

bool RuntimeMatchHistory::can_admit_terminal_tail_poll_ticket(
    std::uint32_t request_sequence,unsigned local_port,unsigned remote_port) const noexcept
{
    return terminal_tail_active_&&
        phase_==RuntimeMatchHistoryPhase::TerminalTailReady&&
        terminal_tail_suffix_cursor_==terminal_tail_suffix_count_&&
        !terminal_tail_ticket_pending_&&!terminal_tail_remote_pending_&&
        request_sequence&&request_sequence>terminal_tail_last_ticket_id_&&
        local_port==local_port_&&remote_port==remote_port_&&
        local_port<PAD_MAX_CONTROLLERS&&remote_port<PAD_MAX_CONTROLLERS&&
        local_port!=remote_port&&!session_.complete()&&
        read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,false)&&
        live_owner(terminal_tail_source_frame_);
}

bool RuntimeMatchHistory::adopt_terminal_tail_ticket(
    std::uint32_t request_sequence) noexcept
{
    if(!terminal_tail_active_||phase_!=RuntimeMatchHistoryPhase::TerminalTailReady||
       terminal_tail_ticket_pending_||terminal_tail_remote_pending_||!request_sequence||
       request_sequence<=terminal_tail_last_ticket_id_||
       terminal_tail_suffix_cursor_>=terminal_tail_suffix_count_||session_.complete()||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,false)||
       !gate(RuntimeMatchHistoryGatePoint::TerminalTailPoll,
             terminal_tail_source_frame_))
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Confirmed terminal suffix ticket adoption refused");
    const auto& origin=terminal_tail_suffix_rows_[terminal_tail_suffix_cursor_];
    if(origin.schema!=1||origin.bytes!=sizeof(origin)||
       (origin.state!=1u&&origin.state!=2u)||
       origin.source_frame!=terminal_tail_input_ordinal_||
       !origin.ticket_id||!origin.poll_serial||
       origin.local_port!=local_port_||origin.remote_port!=remote_port_||
       origin.incarnation==0||origin.incarnation>terminal_tail_incarnation_||
       origin.ticket_id<=terminal_tail_last_ticket_id_||
       origin.poll_serial<=terminal_tail_last_poll_serial_)
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Confirmed terminal suffix origin no longer matches its input ordinal");
    RuntimeMatchHistoryTerminalTailTicket ticket{};
    ticket.flags=1u;
    ticket.ticket_id=origin.ticket_id;
    ticket.request_sequence=request_sequence;
    ticket.source_frame=terminal_tail_source_frame_;
    ticket.input_ordinal=terminal_tail_input_ordinal_;
    ticket.origin_ticket_id=origin.ticket_id;
    ticket.origin_source_frame=origin.source_frame;
    ticket.incarnation=terminal_tail_incarnation_;
    ticket.origin_incarnation=origin.incarnation;
    ticket.local_port=origin.local_port;
    ticket.remote_port=origin.remote_port;
    ticket.completed_tick=terminal_tail_completed_tick_;
    ticket.poll_serial=origin.poll_serial;
    ticket.raw_pad=origin.raw_pad;
    terminal_tail_ticket_=ticket;
    terminal_tail_ticket_pending_=true;
    terminal_tail_adopted_pending_=true;
    phase_=RuntimeMatchHistoryPhase::TerminalTailTicketed;
    return true;
}

bool RuntimeMatchHistory::admit_terminal_tail_poll_ticket(
    std::uint32_t request_sequence,const PADStatus raw[PAD_MAX_CONTROLLERS],
    std::uint64_t poll_serial,unsigned local_port,unsigned remote_port) noexcept
{
    if(!raw||!poll_serial||!can_admit_terminal_tail_poll_ticket(
           request_sequence,local_port,remote_port)||
       poll_serial<=terminal_tail_last_poll_serial_||
       !gate(RuntimeMatchHistoryGatePoint::TerminalTailPoll,
             terminal_tail_source_frame_))
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Actual terminal-tail poll ticket admission refused");
    RuntimeMatchHistoryTerminalTailTicket ticket{};
    ticket.ticket_id=request_sequence;
    ticket.request_sequence=request_sequence;
    ticket.source_frame=terminal_tail_source_frame_;
    ticket.input_ordinal=terminal_tail_input_ordinal_;
    ticket.incarnation=terminal_tail_incarnation_;
    ticket.local_port=local_port;
    ticket.remote_port=remote_port;
    ticket.completed_tick=terminal_tail_completed_tick_;
    ticket.poll_serial=poll_serial;
    encode_pad_frame(raw,ticket.raw_pad);
    terminal_tail_ticket_=ticket;
    terminal_tail_ticket_pending_=true;
    terminal_tail_adopted_pending_=false;
    phase_=RuntimeMatchHistoryPhase::TerminalTailTicketed;
    return true;
}

bool RuntimeMatchHistory::forward_terminal_tail_ticket(
    std::uint32_t request_sequence,const std::uint8_t* remote_pad,
    unsigned remote_pad_bytes) noexcept
{
    if(!terminal_tail_active_||phase_!=RuntimeMatchHistoryPhase::TerminalTailTicketed||
       !terminal_tail_ticket_pending_||terminal_tail_remote_pending_||!remote_pad||
       remote_pad_bytes!=MELEE_WEB_NET_PAD_BYTES||
       request_sequence!=terminal_tail_ticket_.request_sequence||
       terminal_tail_ticket_.input_ordinal!=terminal_tail_input_ordinal_||
       terminal_tail_ticket_.source_frame!=terminal_tail_source_frame_||
       terminal_tail_ticket_.completed_tick!=terminal_tail_completed_tick_||
       terminal_tail_ticket_.local_port!=local_port_||
       terminal_tail_ticket_.remote_port!=remote_port_||session_.complete()||
       terminal_tail_completed_tick_==UINT64_MAX||
       terminal_tail_input_ordinal_==UINT32_MAX||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,false)||
       !gate(RuntimeMatchHistoryGatePoint::TerminalTailForwardBefore,
             terminal_tail_source_frame_))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Terminal-tail forward ticket lost its selected owner or ordinal");

    std::array<PADStatus,PAD_MAX_CONTROLLERS> raw{},applied{};
    for(unsigned port=0;port<PAD_MAX_CONTROLLERS;++port)
        if(!melee_web_net_decode_pad(
               terminal_tail_ticket_.raw_pad.data()+port*MELEE_WEB_NET_PAD_BYTES,
               MELEE_WEB_NET_PAD_BYTES,&raw[port]))
            return fail(RuntimeMatchHistoryFailure::Input,
                        "Terminal-tail retained raw PAD ticket refused");
    PADStatus confirmed_remote{};
    if(!melee_web_net_decode_pad(remote_pad,remote_pad_bytes,&confirmed_remote))
        return fail(RuntimeMatchHistoryFailure::Input,
                    "Terminal-tail confirmed remote PAD refused");
    applied=raw;
    applied[remote_port_]=confirmed_remote;
    encode_pad_frame(applied.data(),terminal_tail_ticket_.applied_pad);
    terminal_tail_applied_=applied;
    terminal_tail_remote_pending_=true;

    try{
        session_.tick(terminal_tail_applied_.data());
        if(!audio_step_(session_,callback_context_))
            return fail(RuntimeMatchHistoryFailure::SourceTick,
                        "Ordinary audio render refused after terminal-tail source tick");
    }catch(const std::exception& error){
        return fail(RuntimeMatchHistoryFailure::SourceTick,error.what());
    }catch(...){return fail(RuntimeMatchHistoryFailure::SourceTick);}

    if(!session_.ending()||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_+1u,
                                  terminal_tail_source_frame_,true))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Terminal-tail traversal changed source frame, typed owner or tick delta");
    ++terminal_tail_completed_tick_;
    ++terminal_tail_input_ordinal_;
    terminal_tail_last_input_ordinal_=terminal_tail_ticket_.input_ordinal;
    terminal_tail_last_ticket_id_=terminal_tail_ticket_.ticket_id;
    terminal_tail_last_poll_serial_=terminal_tail_ticket_.poll_serial;
    if(terminal_tail_adopted_pending_){
        ++terminal_tail_suffix_cursor_;
        terminal_tail_adopted_pending_=false;
    }
    terminal_tail_ticket_pending_=false;
    terminal_tail_complete_after_=session_.complete();
    terminal_tail_remote_pending_=false;
    phase_=RuntimeMatchHistoryPhase::TerminalTailForwarded;
    if(!gate(RuntimeMatchHistoryGatePoint::TerminalTailForwardAfter,
             terminal_tail_source_frame_))
        return fail(RuntimeMatchHistoryFailure::SourceTick,
                    "Terminal-tail completed traversal lost its owner boundary");
    return true;
}

bool RuntimeMatchHistory::submit_terminal_tail_present() noexcept
{
    if(!terminal_tail_active_||phase_!=RuntimeMatchHistoryPhase::TerminalTailForwarded||
       terminal_tail_ticket_pending_||terminal_tail_remote_pending_||
       terminal_tail_last_presented_ordinal_>=terminal_tail_input_ordinal_||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,true)||
       !gate(RuntimeMatchHistoryGatePoint::TerminalTailPresentSubmit,
             terminal_tail_source_frame_))
        return fail(RuntimeMatchHistoryFailure::Present,
                    "Terminal-tail present requires one newly completed owned source tick");
    if(!present_||present_(session_,terminal_tail_source_frame_,callback_context_)!=
       RuntimeMatchHistoryPresentResult::Submitted||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,true))
        return fail(RuntimeMatchHistoryFailure::Present,
                    "Terminal-tail source presentation refused");
    last_present_source_frame_=terminal_tail_source_frame_;
    last_present_source_frame_valid_=true;
    ++recording_present_count_;
    phase_=RuntimeMatchHistoryPhase::TerminalTailPresentPending;
    return true;
}

bool RuntimeMatchHistory::terminal_tail_finish_ready(
    std::uint32_t& consumed_ordinal,std::uint32_t& last_input_ordinal) const noexcept
{
    consumed_ordinal=last_input_ordinal=0;
    if(!terminal_tail_finish_state_ready(consumed_ordinal,last_input_ordinal))
        return false;
    const auto admitted=boundary_?boundary_(session_,
        RuntimeMatchHistoryGatePoint::TerminalTailFinish,
        terminal_tail_source_frame_,callback_context_):
        RuntimeMatchHistoryGateResult::OtherOwnerRefused;
    if(admitted!=RuntimeMatchHistoryGateResult::Admitted){
        consumed_ordinal=last_input_ordinal=0;
        return false;
    }
    return true;
}

bool RuntimeMatchHistory::terminal_tail_finish_state_ready(
    std::uint32_t& consumed_ordinal,std::uint32_t& last_input_ordinal) const noexcept
{
    consumed_ordinal=last_input_ordinal=0;
    if(!terminal_tail_active_||
       phase_!=RuntimeMatchHistoryPhase::TerminalTailPresented||
       terminal_tail_ticket_pending_||terminal_tail_remote_pending_||
       terminal_tail_suffix_cursor_!=terminal_tail_suffix_count_||
       !terminal_tail_input_ordinal_||
       terminal_tail_last_input_ordinal_==UINT32_MAX||
       terminal_tail_last_input_ordinal_+1u!=terminal_tail_input_ordinal_||
       terminal_tail_last_presented_ordinal_!=terminal_tail_input_ordinal_||
       !terminal_tail_complete_after_||!session_.ending()||!session_.complete()||
       !read_terminal_tail_cursor(terminal_tail_completed_tick_,
                                  terminal_tail_source_frame_,true))
        return false;
    consumed_ordinal=terminal_tail_input_ordinal_;
    last_input_ordinal=terminal_tail_last_input_ordinal_;
    return true;
}

bool RuntimeMatchHistory::finish_terminal_tail(
    std::uint32_t consumed_ordinal,std::uint32_t last_input_ordinal) noexcept
{
    std::uint32_t expected_consumed=0,expected_last=0;
    if(!terminal_tail_finish_state_ready(expected_consumed,expected_last)||
       consumed_ordinal!=expected_consumed||last_input_ordinal!=expected_last||
       !terminal_tail_ticket_.ticket_id||
       terminal_tail_ticket_.input_ordinal!=last_input_ordinal||
       terminal_tail_ticket_.poll_serial!=terminal_tail_last_poll_serial_)
        return fail(RuntimeMatchHistoryFailure::Retire,
                    "Terminal-tail completion confirmation did not match its final input receipt");
    if(!gate(RuntimeMatchHistoryGatePoint::TerminalTailFinish,
             terminal_tail_source_frame_))return false;
    int winner=-1;
    try{
        if(session_.outcome(winner)==0)
            return fail(RuntimeMatchHistoryFailure::Retire,
                        "Completed terminal tail has no original source outcome");
    }catch(...){return fail(RuntimeMatchHistoryFailure::Retire,
                            "Completed terminal tail outcome read refused");}
    phase_=RuntimeMatchHistoryPhase::TerminalFinished;
    return true;
}

bool RuntimeMatchHistory::terminal_tail_authorized(
    const GameplayMatchSession& session,RuntimeMatchHistoryGatePoint point,
    std::uint32_t source_frame) const noexcept
{
    if(&session!=&session_)return false;
    if(point==RuntimeMatchHistoryGatePoint::TerminalTailBegin){
        return !terminal_tail_active_&&
            phase_==RuntimeMatchHistoryPhase::TerminalRetired&&!checkpoint_&&
            terminal_cursor_.selected_terminal_valid&&
            terminal_cursor_.selected_terminal_input_ordinal==terminal_selected_ordinal_&&
            terminal_cursor_.selected_terminal_source_frame_after==source_frame&&
            terminal_selected_source_frame_==source_frame&&session.source_frames()==source_frame&&
            read_terminal_tail_cursor(terminal_selected_tick_,source_frame,false);
    }
    if(source_frame!=terminal_tail_source_frame_||!terminal_tail_active_)
        return false;
    bool phase_ok=false;
    switch(point){
    case RuntimeMatchHistoryGatePoint::TerminalTailPoll:
        phase_ok=phase_==RuntimeMatchHistoryPhase::TerminalTailReady;break;
    case RuntimeMatchHistoryGatePoint::TerminalTailForwardBefore:
        phase_ok=phase_==RuntimeMatchHistoryPhase::TerminalTailTicketed&&
                 terminal_tail_ticket_pending_;break;
    case RuntimeMatchHistoryGatePoint::TerminalTailForwardAfter:
    case RuntimeMatchHistoryGatePoint::TerminalTailPresentSubmit:
        phase_ok=phase_==RuntimeMatchHistoryPhase::TerminalTailForwarded;break;
    case RuntimeMatchHistoryGatePoint::TerminalTailPresentComplete:
        phase_ok=phase_==RuntimeMatchHistoryPhase::TerminalTailPresentPending;break;
    case RuntimeMatchHistoryGatePoint::TerminalTailFinish:
        phase_ok=phase_==RuntimeMatchHistoryPhase::TerminalTailPresented;break;
    default:return false;
    }
    return phase_ok&&read_terminal_tail_cursor(terminal_tail_completed_tick_,
        terminal_tail_source_frame_,terminal_tail_complete_after_);
}

const RuntimeMatchHistoryTerminalTailTicket*
RuntimeMatchHistory::terminal_tail_ticket() const noexcept
{
    return terminal_tail_active_&&terminal_tail_ticket_.ticket_id?
        &terminal_tail_ticket_:nullptr;
}

const PADStatus* RuntimeMatchHistory::terminal_tail_applied_input() const noexcept
{
    std::uint32_t consumed=0,last=0;
    return terminal_tail_finish_ready(consumed,last)?terminal_tail_applied_.data():nullptr;
}

} // namespace melee_web

#endif // MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
