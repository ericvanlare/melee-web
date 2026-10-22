/** Actual receipt loader/comparator/queued finite stages with explicit doubles.
 * No native source, browser, driver replacement, or accuracy claim.
 */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {createHash} from 'node:crypto';
const root=path.resolve(process.argv[2]),base=path.resolve(process.argv[3]);
const fixtureKind=process.argv[4];
assert(['synthetic','retained-original'].includes(fixtureKind),'Explicit fixture scope required');
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const source=await fs.readFile(path.join(root,'tests/stadium_c1a_browser_test.mjs'),'utf8');
const development=await fs.readFile(path.join(root,'web/runtime-development.mjs'),'utf8');
const loaderSource=source.slice(source.indexOf('async function loadFirstCssConstructorReturnInputs()'),source.indexOf('let firstCssBrowserStreamInputs ='));
const loadActual=new Function('assert','fs','path','digest','values','preflight','firstCssConstructorReturn',
 'firstCssFreshRow','firstCssFreshFinite','firstCssConstructorManifest','ROOT','pathToFileURL',
 loaderSource+'\nreturn loadFirstCssFreshFiniteInputs();');
const finitePath=path.join(base,'validation-v1/first-css-finite-preparation-v1/extraction-v1/receipt.json');
const basePreflight=JSON.parse(await fs.readFile(path.join(base,'first-row-capture-v1/preflight.json'),'utf8'));
const helperPath=path.join(root,'tests/stadium_first_css_stream_compare.mjs');
const helperBytes=await fs.readFile(helperPath);
const helpers=await import(pathToFileURL(helperPath).href);
const pin=(name,bytes)=>({path:name,bytes:bytes.byteLength,sha256:digest(bytes)});
async function fixture(){
 const receipt=JSON.parse(await fs.readFile(finitePath,'utf8'));
 const descriptor=JSON.parse(await fs.readFile(receipt.input_only_descriptor.path,'utf8'));
 const expected=JSON.parse(await fs.readFile(receipt.expected_host_only.path,'utf8'));
 const pairings=JSON.parse(await fs.readFile(receipt.source_pairings.path,'utf8'));
 const overlay=new Map();
 const bind=(key,value)=>{const bytes=Buffer.from(JSON.stringify(value));const name=receipt[key].path;
  overlay.set(name,bytes);receipt[key]=pin(name,bytes);return receipt[key];};
 const refresh=()=>{
  bind('input_only_descriptor',descriptor);bind('expected_host_only',expected);bind('source_pairings',pairings);
  const bytes=Buffer.from(JSON.stringify(receipt));overlay.set(finitePath,bytes);
  return {...structuredClone(basePreflight),first_css_fresh_finite:{receipt:pin(finitePath,bytes),comparator:pin(helperPath,helperBytes)}};
 };
 return {receipt,descriptor,expected,pairings,overlay,bind,refresh};
}
async function load(f){
 return loadActual(assert,{readFile:async name=>f.overlay.get(name)??await fs.readFile(name)},path,digest,
  {'first-css-finite-receipt':finitePath},f.refresh(),false,false,true,
  basePreflight.first_css_constructor_return.manifest.path,root,pathToFileURL);
}
const f=await fixture();const input=await load(f),desc=f.descriptor,wanted=f.expected;
const oracle=JSON.stringify(wanted);const nativeInput=Buffer.from(input.bytes.consumed_pad_bundle);
assert.equal(nativeInput.length,60+desc.row_count*84);assert.equal(nativeInput.subarray(0,8).toString('ascii'),'STC4PFX1');
assert.equal(nativeInput.readUInt32BE(56),desc.row_count);
const raw=await fs.readFile(f.receipt.literal_rows.path);
for(let index=0;index<desc.row_count;index++)assert.deepEqual(nativeInput.subarray(60+index*84+40,60+(index+1)*84),raw.subarray(index*44,(index+1)*44));
let receiptRefusals=0;
for(const change of ['count','gap','clock','extra-field','raw-payload','source','context','missing-row','terminal-paired','narrow-tick','narrow-draw','row0','pairing']){
 const bad=await fixture();
 if(change==='count')bad.descriptor.row_count++;
 if(change==='gap')bad.descriptor.rows[1].source_tick_sequence++;
 if(change==='clock')bad.descriptor.rows[1].draw_source_tick++;
 if(change==='extra-field')bad.descriptor.expected_seed='ffffffff';
 if(change==='raw-payload')bad.descriptor.rows[1].raw_four_port_hex='00'.repeat(44);
 if(change==='source')bad.descriptor.source_sha256='22'.repeat(32);
 if(change==='context')bad.descriptor.context_return_sequence++;
 if(change==='missing-row')bad.descriptor.rows.pop();
 if(change==='terminal-paired')bad.expected.rows.at(-1).expected_post_tick=structuredClone(bad.expected.rows.at(-1).original_scheduler_end_unpaired);
 if(change==='narrow-tick')bad.expected.tick_scalar_comparison_fields.pop();
 if(change==='narrow-draw')bad.expected.draw_comparison_fields.pop();
 if(change==='row0')bad.expected.rows[0].expected_post_tick.random_seed_hex='00000000';
 if(change==='pairing')bad.pairings.rows[1][1].boundary='pad_poll';
 await assert.rejects(load(bad),undefined,change);receiptRefusals++;
 assert.equal(JSON.stringify(wanted),oracle,'negative fixture aliased comparator oracle');
}
// A changed host-only expectation cannot alter native delivered bytes.
const different=await fixture();different.expected.rows[1].expected_post_tick.random_seed_hex='87654321';
assert.deepEqual((await load(different)).bytes.consumed_pad_bundle,nativeInput);
const stable={source_scene:1,menu_phase:1,scene_kind:8,world_generation:17,
 world_generation_stable:true,scene_owner_stable:true,seed_owner_stable:true};
const streamTick=row=>({...structuredClone(row.expected_post_tick),...stable,
 source_tick:row.metadata.tick_source_tick,draw_ordinal:row.metadata.tick_draw_ordinal});
const streamDraw=row=>({...structuredClone(row.expected_draw_return),...stable});
const terminal={...stable,tick_result:3,executed_host_ticks:desc.row_count,stream_input_ordinal:desc.rows.at(-1).ordinal,
 consumed_pad_sequence:desc.rows.at(-1).consumed_pad_sequence,source_tick:desc.rows.at(-1).tick_source_tick,
 draw_ordinal:desc.rows.at(-1).tick_draw_ordinal,scene_frame:desc.row_count,
 random_seed_hex:'00000000',pad_state_hex:'00'.repeat(822),terminal_transition:true,
 scene_routing_getters:structuredClone(wanted.rows.at(-1).expected_draw_return.scene_routing_getters)};
const compare=helpers.createFreshFiniteCssStreamComparator(desc,wanted);
for(const row of wanted.rows.slice(1,-1)){
 assert.equal(compare.compare('tick',JSON.stringify(streamTick(row))),true);
 assert.equal(compare.compare('draw',JSON.stringify(streamDraw(row))),true);
}
assert.equal(compare.compare('transition',JSON.stringify(terminal)),false);
assert.equal(compare.status().failed,false);assert.equal(compare.status().outcome,'stop_after_last_input_request');
assert.equal(compare.status().next_index,desc.row_count-2);
const final=helpers.createFreshFiniteCssFinalDrawComparator(desc,wanted,terminal);
assert.equal(final.compare('final_draw',JSON.stringify(streamDraw(wanted.rows.at(-1)))),true);
assert.equal(final.compare('final_draw',JSON.stringify(streamDraw(wanted.rows.at(-1)))),false);
let comparatorRefusals=0;
for(const mutation of ['seed','pad','frame','route','tick-ref','extra-draw','early-transition','terminal-result']){
 const c=helpers.createFreshFiniteCssStreamComparator(desc,wanted),row=wanted.rows[1];
 let actual=streamTick(row),phase='tick';
 if(mutation==='seed')actual.random_seed_hex='00000000';
 if(mutation==='pad')actual.pad_state_hex='00'.repeat(822);
 if(mutation==='frame')actual.native_post_host_tick_frame++;
 if(mutation==='route')actual.scene_routing_getters.current_scene_index++;
 if(mutation==='tick-ref')actual.source_tick_sequence++;
 if(mutation==='extra-draw'){actual=streamDraw(row);phase='draw';}
 if(mutation==='early-transition'){actual={...terminal,source_tick:1,draw_ordinal:1,stream_input_ordinal:1,
  consumed_pad_sequence:desc.rows[1].consumed_pad_sequence,executed_host_ticks:2};phase='transition';}
 if(mutation==='terminal-result'){actual={...terminal,tick_result:1};phase='transition';}
 assert.equal(c.compare(phase,JSON.stringify(actual)),false,mutation);assert.equal(c.status().failed,true,mutation);
 comparatorRefusals++;
}
const helperSource=development.slice(development.indexOf('let firstCssConstructorLaunchAttempted='),development.indexOf('window.meleeNetTimingPauseEligibilitySnapshot='));
const entryComparator=source.slice(source.indexOf('function assertFirstCssBrowserEntry('),source.indexOf('function assertFirstCssBrowserTick('));
const driverStart=source.indexOf('async function runFirstCssBrowserDraw(');
const driverPrefix=source.slice(source.indexOf('  const arm = await timeout(page.evaluate(',driverStart),source.indexOf('  await timeout(page.waitForFunction(',source.indexOf('  const kick = await timeout(',driverStart)));
const run=new Function('assert','helperSource','options','input',`return(async()=>{
 const events=[],queue=[];let inside=false,phase=0,running=false;
 let observed={schema:'melee-web-stadium-first-css-finite-observation-v4',mode:'fresh_finite_css_prefix_v4',
  source_stream_sha256:input.finite.descriptor.source_sha256,
  state:'armed',error:null,native_phase:0,running:false,host_entered:false,armed_owner_stable:true,
  armed_world_generation:17,source_steps:0,source_draws:0,host_tick_calls:0,host_draw_calls:0,
  aurora_begin_calls:0,aurora_end_calls:0};
 if(input)observed.finite_input_provenance={source_sha256:input.finite.descriptor.source_sha256,
  row_count:input.finite.descriptor.row_count,phase_end_sequence:input.finite.descriptor.exclusive_phase_end_sequence};
 const window={};let sourceCalls=0;
 let heapCursor=1;
 const initialModule={HEAPU8:new Uint8Array(131072),_malloc(size){const at=heapCursor;heapCursor+=size+8;return at;},_free(){},
  _melee_web_native_menu_stadium_first_css_fresh_finite_v4_arm(context,n,rows,size){
   assert.deepEqual(Buffer.from(initialModule.HEAPU8.subarray(context,context+n)),input.bytes.context_bundle);
   assert.deepEqual(Buffer.from(initialModule.HEAPU8.subarray(rows,rows+size)),input.bytes.consumed_pad_bundle);return 1;},
  _melee_web_native_menu_stadium_first_css_draw_observe(){assert(inside||phase===0);return 1;},
  UTF8ToString(){return JSON.stringify(observed);},_melee_web_native_menu_phase(){return phase;},
  _melee_web_native_menu_running(){return +running;},_melee_web_native_menu_launch(){
   assert(inside);events.push('launch');sourceCalls++;phase=1;running=true;
   observed={...observed,state:'entered',native_phase:1,running:true,host_entered:true};
   if(input){observed.entry=structuredClone(input.contextExpected.expected_return);observed.entry.source_scene=1;
    observed.entry.css.vs.start.rules.pad_x5C=0;observed.entry.abi_padding_excluded=['pad_x5C'];
    for(const key of ['on_unpause_override','on_pause_override','check_for_pauser_override','on_match_start',
      'on_frame_start','on_frame_end','on_match_end','x54_pointer','x58_pointer'])observed.entry.css.vs.start.rules[key]='null';}
   return 1;}};
 let Module=initialModule;
 let busyName='',loadingState=null,preparationLabel='',stateFault=null;const ownerStates=[];
 const ownerSnapshot=()=>{
  const scene=phase===1?'css':phase===3?'sss':'idle',active=['css','sss'].includes(scene);
  const paused=active&&!running&&!preparationLabel&&!busyName;
  const state=busyName||(preparationLabel?'preparing':paused?'paused':active?scene:phase===0?'prepared':'idle');
  const normal={state,scene,phase,running,paused,busy:!!busyName,loading:loadingState,
   requiresReload:false,ready:true,canStart:!active&&!busyName};
  const value=stateFault?{...normal,...stateFault}:normal;ownerStates.push(value);return value;
 };
 let owner={handle:{getState(){assert(inside);return ownerSnapshot();}}};
 const originalOwner=owner;
 let boundary=fn=>new Promise((resolve,reject)=>queue.push({fn,resolve,reject}));
 const check=result=>{assert.equal(result,1);return result;};
 const resetTiming=()=>events.push('reset'),syncAudio=()=>{};let inputDirty=false;const $=()=>({focus(){}});
 const mutate=kind=>{if(kind==='owner')owner={handle:owner.handle};else if(kind==='owner-handle')owner.handle={getState:()=>ownerSnapshot()};
  else if(kind==='module')Module={...Module};
  else if(kind==='generation')observed.armed_world_generation++;
  else if(kind==='unload'){phase=0;running=false;observed.state='idle';}
  else if(kind==='busy')busyName='another-operation';
  else if(kind==='loading')loadingState={phase:'native'};
  else if(kind==='preparing')preparationLabel='Preparing original scene';
  else if(kind==='paused-non-css'){phase=3;running=false;observed.native_phase=3;}
  else if(kind==='paused-css-not-paused')stateFault={state:'paused',scene:'css',phase:1,running:false,paused:false};
  else if(kind==='failed')observed.error='failed';};
 const prepareAudio=async()=>{events.push('audio');if(options.audioMutation)mutate(options.audioMutation);};
 const pauseAudioForPreparation=async()=>events.push('pause'),waitForAudioRender=async()=>events.push('render');
 for(const stage of ['first','stream_arm','stream_kick','final_arm','final_kick']){
  const suffix={first:'first_kick',stream_arm:'stream_arm',stream_kick:'stream_kick',final_arm:'final_arm',final_kick:'final_kick'}[stage];
  initialModule['_melee_web_native_menu_stadium_first_css_fresh_finite_v4_'+suffix]=()=>{
   assert(inside);events.push(stage);sourceCalls++;
   if(stage==='first'){running=false;observed={...observed,state:'complete',running:false,
    source_steps:1,source_draws:1,host_tick_calls:1,host_draw_calls:1,aurora_begin_calls:1,aurora_end_calls:1,frame_end_returned:true};}
   if(stage==='stream_arm')observed.postdraw_stream={status:'armed',failed:false,consumed_inputs:0,matched_ticks:0,matched_draws:0};
   if(stage==='stream_kick')observed.postdraw_stream={terminal:true,failed:false,outcome:'stop_after_last_input_request',tick_result:3};
   if(stage==='final_arm')observed.final_pending_css_draw={status:'armed',failed:false,attempted:false};
   if(stage==='final_kick')observed.final_pending_css_draw={status:'complete',failed:false,attempted:true};
   return 1;
  };
 }
 eval(helperSource);
 const flush=async promise=>{let done=false,error=null;promise.catch(e=>error=e).finally(()=>done=true);
  for(let i=0;i<100&&!done;i++){await Promise.resolve();while(queue.length){const item=queue.shift();
   inside=true;try{item.resolve(item.fn());}catch(e){item.reject(e);}finally{inside=false;}}}
  if(!done)throw Error('control queue did not settle');if(error)throw error;return await promise;};
 const firstCssFreshFinite=true,firstCssFreshRow=false,firstCssBrowserDrawInputs=input;
 const page={evaluate:async(fn,arg)=>fn(arg)},timeout=p=>p,remaining=()=>5000,LIMITS={sourceTransitionMs:20000};
 const report={scenario:{}},persistReport=async()=>{};
 const sourceSnapshot=async()=>({phase,running:+running});
 const readFirstCssBrowserDrawObservation=async()=>({...structuredClone(observed),native_phase:phase,running:+running});
 const driver={launch(){throw Error('ordinary driver forbidden');}};
 ${entryComparator}
 try{
  if(options.useDriverPrefix){
   globalThis.Module=Module;
   globalThis.meleeStadiumFirstCssFreshFiniteLaunch=window.meleeStadiumFirstCssFreshFiniteLaunch;
   globalThis.meleeStadiumFirstCssFreshFiniteStage=window.meleeStadiumFirstCssFreshFiniteStage;
   await flush((async()=>{${driverPrefix}})());
   return {events,sourceCalls,error:null,ownerStates};
  }
  await flush(window.meleeStadiumFirstCssFreshFiniteLaunch(17));
  for(const stage of ['first','stream_arm','stream_kick','final_arm','final_kick']){
   const pending=window.meleeStadiumFirstCssFreshFiniteStage(stage);
   if(stage===options.beforeStage)mutate(options.mutation);
   await flush(pending);
  }
  await assert.rejects(window.meleeStadiumFirstCssFreshFiniteStage('first'),/already attempted/);
  await assert.rejects(flush(window.meleeStadiumFirstCssFreshRowKick()),/Fresh CSS row/);
  return {events,sourceCalls,error:null,ownerStates};
 }catch(error){return {events,sourceCalls,error:error.message,ownerStates};}
 finally{for(const key of ['Module','meleeStadiumFirstCssFreshFiniteLaunch','meleeStadiumFirstCssFreshFiniteStage'])delete globalThis[key];}
})()`);
const baseline=await run(assert,helperSource,{},input);assert.equal(baseline.error,null,JSON.stringify(baseline));
assert.deepEqual(baseline.events,['reset','audio','pause','render','launch','first','stream_arm','stream_kick','final_arm','final_kick']);
assert.deepEqual(baseline.ownerStates[2],{state:'css',scene:'css',phase:1,running:true,paused:false,busy:false,loading:null,
 requiresReload:false,ready:true,canStart:false},'First stage must retain the running CSS owner.');
for(const value of baseline.ownerStates.slice(3,7))assert.deepEqual(value,
 {state:'paused',scene:'css',phase:1,running:false,paused:true,busy:false,loading:null,
  requiresReload:false,ready:true,canStart:false},'Post-first stages must retain the paused CSS owner.');
const actualPrefix=await run(assert,helperSource,{useDriverPrefix:true},input);
assert.equal(actualPrefix.error,null,JSON.stringify(actualPrefix));
assert(actualPrefix.events.includes('launch')&&actualPrefix.events.includes('first'));
let queuedCases=2;
for(const beforeStage of ['first','stream_arm','stream_kick','final_arm','final_kick']){
 for(const mutation of ['owner','module','generation','unload','busy','failed']){
  const result=await run(assert,helperSource,{beforeStage,mutation},input);assert(result.error,JSON.stringify({beforeStage,mutation,result}));
  assert.equal(result.events.includes(beforeStage),false);queuedCases++;
 }
}
for(const audioMutation of ['owner','module','generation','unload','busy','failed']){
 const result=await run(assert,helperSource,{audioMutation},input);assert(result.error);assert.equal(result.events.includes('launch'),false);queuedCases++;
}
let stateRefusals=0;
for(const mutation of ['paused-non-css','paused-css-not-paused','busy','loading','preparing','owner-handle']){
 const result=await run(assert,helperSource,{beforeStage:'stream_arm',mutation},input);
 assert(result.error,JSON.stringify({mutation,result}));assert.equal(result.events.includes('stream_arm'),false,mutation);stateRefusals++;
}
assert.equal(JSON.stringify(wanted),oracle,'comparator mutated reviewed expected oracle');
console.log(JSON.stringify({schema:'stadium-fresh-finite-css-js-controls-v1',result:'PASS',receiptRefusals,
 comparatorRefusals,queuedCases,stateRefusals,fixtureKind,actualLiteralRows:desc.row_count,expectedStateExcludedFromNative:true,
 scope:'Actual extracted loader and queued helper plus real comparator module over explicitly labeled fixture expectations; explicit native/owner/audio/queue doubles, no game/browser/native source.'}));
