#pragma once
#if defined(MELEE_WEB_FIRST_DRAW_COMPONENTS_DIAGNOSTIC)
#include <array>
#include <cstdint>
#include <limits>
namespace melee_web::detail {
// Private copied observations only. No clock, native calls, ownership or cancellation.
struct NaturalOneTickEnrollment {
 static constexpr uint32_t MinimumFrame=180,MaximumFrame=240;
 static constexpr uint32_t RowCapacity=MaximumFrame-MinimumFrame+1;
 enum State:uint32_t {Idle,Pending,Selected,Completed,Failed};
 struct Row {
  uint64_t callback_sequence=0;
  uint32_t source_before=0,source_after=0,planned_steps=0,planned_audio_steps=0;
  uint32_t tick_returns=0,draw_returns=0,audio_returns=0,pcm_frames=0;
  uint32_t audio_phase_before=0,audio_phase_after=0;
  bool selected=false;
 };
 struct Facts {
  State state=Idle;uint32_t origin_frame=0,selected_frame=0,owner_generation=0,origin_audio_phase=0,row_count=0;
  uint64_t callback_sequence=0,selection_callback=0,skipped_callbacks=0,skipped_ticks=0,skipped_draws=0,skipped_audio=0,skipped_pcm_frames=0;
  uint64_t source_free_callbacks=0,source_free_audio=0,source_free_pcm_frames=0;
  bool callback_open=false,plan_seen=false;Row current{};std::array<Row,RowCapacity> rows{};
  const char* error="";
 } facts;
 uintptr_t owner=0;
 bool requested()const noexcept{return facts.state!=Idle;}
 bool pending()const noexcept{return facts.state==Pending;}
 void fail(const char* reason)noexcept{if(!*facts.error)facts.error=reason;facts.state=Failed;}
 bool add(uint64_t& value,uint64_t count)noexcept{
  if(count>std::numeric_limits<uint64_t>::max()-value){fail("natural_one_counter_overflow");return false;}value+=count;return true;
 }
 bool request(uintptr_t actual_owner,uint32_t generation,uint32_t frame,uint32_t phase)noexcept{
  if(requested()||!actual_owner||frame<MinimumFrame||frame>MaximumFrame||phase>=60)return false;
  owner=actual_owner;facts.state=Pending;facts.origin_frame=frame;facts.owner_generation=generation;facts.origin_audio_phase=phase;return true;
 }
 bool same(uintptr_t actual_owner,uint32_t generation)const noexcept{return owner==actual_owner&&facts.owner_generation==generation;}
 void begin_callback(uint32_t frame,uint32_t phase)noexcept{
  if(!pending())return;
  if(facts.callback_open||phase>=60||frame!=uint64_t(facts.origin_frame)+facts.skipped_ticks||phase!=(facts.origin_audio_phase+20*(facts.skipped_audio%3))%60){fail("natural_one_callback_order");return;}
  if(!add(facts.callback_sequence,1))return;
  facts.current={};facts.current.callback_sequence=facts.callback_sequence;facts.current.source_before=frame;facts.current.audio_phase_before=phase;
  facts.callback_open=true;facts.plan_seen=false;
 }
 void plan(uint32_t steps,uint32_t audio_steps,bool stalled,bool construction)noexcept{
  if(!facts.callback_open)return;
  if(facts.plan_seen||steps>8||audio_steps>8||stalled||construction){fail("natural_one_plan_invalid");return;}
  facts.plan_seen=true;facts.current.planned_steps=steps;facts.current.planned_audio_steps=audio_steps;
 }
 bool before_tick(uintptr_t actual_owner,uint32_t generation,uint32_t frame,uint32_t prior,uint32_t steps,uint32_t audio_steps,bool guards)noexcept{
  if(!pending())return false;
  if(!facts.callback_open||!facts.plan_seen||!same(actual_owner,generation)||!guards||frame<facts.origin_frame||frame>MaximumFrame||
     prior!=facts.current.tick_returns||frame!=facts.current.source_before+prior||steps!=facts.current.planned_steps||audio_steps!=facts.current.planned_audio_steps){fail("natural_one_tick_guard");return false;}
  if(steps!=1||prior!=0)return false;
  facts.state=Selected;facts.selected_frame=frame;facts.selection_callback=facts.callback_sequence;facts.current.selected=true;return true;
 }
 void tick_returned()noexcept{if(facts.callback_open){if(facts.current.tick_returns>=8)fail("natural_one_tick_overflow");else ++facts.current.tick_returns;}}
 void draw_returned()noexcept{if(facts.callback_open){if(facts.current.draw_returns>=8)fail("natural_one_draw_overflow");else ++facts.current.draw_returns;}}
 void audio_returned(uint32_t frames)noexcept{
  if(!facts.callback_open)return;
  if(facts.current.audio_returns>=8||frames>std::numeric_limits<uint32_t>::max()-facts.current.pcm_frames){fail("natural_one_audio_overflow");return;}
  ++facts.current.audio_returns;facts.current.pcm_frames+=frames;
 }
 void end_callback(uintptr_t actual_owner,uint32_t generation,uint32_t frame,uint32_t phase,uint32_t ticks,uint32_t draws,bool guards)noexcept{
  if(!requested()||facts.state==Completed)return;
  if(!facts.callback_open){fail("natural_one_callback_missing");return;}
  auto& row=facts.current;row.source_after=frame;row.audio_phase_after=phase;
  if(!same(actual_owner,generation)||!guards||!facts.plan_seen||phase>=60||ticks!=row.tick_returns||draws!=row.draw_returns||
     ticks>row.planned_steps||draws>ticks||frame!=row.source_before+ticks||row.audio_returns!=row.planned_audio_steps||
     row.pcm_frames!=(row.audio_phase_before+uint64_t(32000)*row.audio_returns)/60||phase!=(row.audio_phase_before+uint64_t(32000)*row.audio_returns)%60)
   fail("natural_one_callback_facts_mismatch");
  if(row.tick_returns){
   if(facts.row_count>=RowCapacity)fail("natural_one_row_overflow");else facts.rows[facts.row_count++]=row;
  }
  if(!row.selected){
   add(facts.skipped_callbacks,1);add(facts.skipped_ticks,row.tick_returns);add(facts.skipped_draws,row.draw_returns);add(facts.skipped_audio,row.audio_returns);add(facts.skipped_pcm_frames,row.pcm_frames);
   if(!row.tick_returns){add(facts.source_free_callbacks,1);add(facts.source_free_audio,row.audio_returns);add(facts.source_free_pcm_frames,row.pcm_frames);}
   if(frame>MaximumFrame)fail("natural_one_window_exhausted");
  }else if(facts.state!=Failed){
   if(row.planned_steps!=1||ticks!=1||draws!=1||frame!=facts.selected_frame+1)fail("natural_one_selected_count");else facts.state=Completed;
  }
  facts.callback_open=false;
 }
 void aborted()noexcept{if(requested()&&facts.state!=Completed){fail("natural_one_callback_aborted");facts.callback_open=false;}}
};
} // namespace melee_web::detail
#endif
