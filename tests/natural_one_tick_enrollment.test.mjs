import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {armNativeFrameEffectFirstDraw,readNativeFrameEffectFirstDraw,naturalOneEnrollmentFailures} from './match_frame_effect_first_draw_driver.mjs';
import {naturalEnrollmentFixture,enrolledOuterJoin} from './natural_one_tick_enrollment_fixture.mjs';
test('strict private enrollment joins original zero/two then selected one with distinct origin',()=>{
 assert.deepEqual(naturalOneEnrollmentFailures(naturalEnrollmentFixture(),enrolledOuterJoin(),{originFrame:180}),[]);
});
for(const [name,mutate] of [
 ['missing',v=>delete v.skipped_ticks],['null row',v=>v.rows[0]=null],['native count type',v=>v.skipped_ticks=2],
 ['origin',v=>v.origin_frame=181],['selected',v=>v.selected_frame=181],['owner',v=>v.owner_generation=10],['row bounds',v=>v.row_capacity=62],
 ['sourcefree accounting',v=>v.source_free_callbacks='0'],['PCM accounting',v=>v.source_free_pcm_frames='534'],['duplicate sequence',v=>v.rows[1].callback_sequence='2'],
 ['wrong plan',v=>v.rows[0].planned_steps=1],['nonterminal',v=>v.state=1],['overlap',v=>v.callback_open=true],['failure',v=>v.error='native_owner_changed'],
 ['selected audio',v=>v.rows[1].pcm_frames=533],['extra field',v=>v.guess=true]])test(name+' refuses enrollment',()=>{
 const v=naturalEnrollmentFixture();mutate(v);assert.deepEqual(naturalOneEnrollmentFailures(v,enrolledOuterJoin(),{originFrame:180}),['natural_one_enrollment_receipt']);
});
for(const natural of [false,true])test('actual serialized arm dispatch '+(natural?'natural enrollment':'unchanged legacy'),()=>{
 const calls=[],module={matchFrameEffectNativeIdle:()=>true,UTF8ToString:x=>x,
 _melee_web_native_menu_match_observe:()=>JSON.stringify({ready:true,frame:180}),
 _melee_web_native_menu_frame_effect_arm_first_draw:frame=>{calls.push(['legacy',frame]);return 1;},
 _melee_web_native_menu_frame_effect_request_natural_one:frame=>{calls.push(['enroll',frame]);return 1;},
 _melee_web_native_menu_frame_effect_natural_one_enrollment:()=>'',_melee_web_native_menu_frame_effect_status:()=>'',
 _melee_web_native_menu_frame_effect_components:()=>'',_aurora_browser_depth_no_work_arm_component:()=>1,
 __meleeWebDepthNoWorkV1:{status:()=>({phase:'READY',firstFailure:null,bridgeFailure:false})}};
 const context=vm.createContext({Module:module});const value=vm.runInContext('('+armNativeFrameEffectFirstDraw.toString()+')',context)({componentEligibility:natural,naturalOneEligibility:natural});
 assert.equal(value.armed,true);assert.deepEqual(calls,[[natural?'enroll':'legacy',180]]);
 if(natural){assert.equal(value.selection_pending,true);assert.equal(value.enrollment_mode,'naturally_due_one_tick');}else assert.equal(Object.hasOwn(value,'selection_pending'),false);
});
test('actual serialized read never queries enrollment until returned idle and settled normal fence',()=>{
 const calls=[];let idle=false,fence=0;
 const module={matchFrameEffectNativeIdle:()=>idle,matchFrameEffectCallbackReturned:false,UTF8ToString:x=>x,
 _melee_web_native_menu_frame_effect_status:()=>{calls.push('status');return JSON.stringify({schema_version:6,terminal:true,error:'',selected_depth_owners:{pending:0},renderer:{state:3,fence_count:fence}});},
 _melee_web_native_menu_frame_effect_natural_one_enrollment:()=>{calls.push('enrollment');return JSON.stringify(naturalEnrollmentFixture());}};
 const context=vm.createContext({Module:module});const read=vm.runInContext('('+readNativeFrameEffectFirstDraw.toString()+')',context);
 assert.equal(read({naturalOneEligibility:true}).settled,false);assert.deepEqual(calls,[]);
 idle=true;module.matchFrameEffectCallbackReturned=true;assert.equal(read({naturalOneEligibility:true}).completed,false);assert.deepEqual(calls,['status']);
 fence=1;assert.equal(read({naturalOneEligibility:true}).enrollment.selected_frame,182);assert.deepEqual(calls,['status','status','enrollment']);
});
