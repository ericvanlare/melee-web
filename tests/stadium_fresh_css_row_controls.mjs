/** Actual extracted receipt adapter, queued helpers and one-shot driver prefix.
 * Explicit filesystem/owner/native/audio doubles; no browser or source engine.
 */
import assert from 'node:assert/strict';
import fsReal from 'node:fs/promises';
import path from 'node:path';
import {createHash} from 'node:crypto';
const root=path.resolve(process.argv[2]);
const driver=await fsReal.readFile(path.join(root,'tests/stadium_c1a_browser_test.mjs'),'utf8');
const development=await fsReal.readFile(path.join(root,'web/runtime-development.mjs'),'utf8');
const digest=b=>createHash('sha256').update(b).digest('hex');
const loader=driver.slice(driver.indexOf('async function loadFirstCssFreshRowInputs()'),driver.indexOf('let firstCssBrowserStreamInputs ='));
const entryComparator=driver.slice(driver.indexOf('function assertFirstCssBrowserEntry('),driver.indexOf('function assertFirstCssBrowserTick('));
const begin=driver.indexOf('async function runFirstCssBrowserDraw(');
const prefix=driver.slice(driver.indexOf('  const arm = await timeout(page.evaluate(',begin),driver.indexOf('  await timeout(page.waitForFunction(',driver.indexOf('  const kick = await timeout(',begin)));
const helper=development.slice(development.indexOf('let firstCssConstructorLaunchAttempted='),development.indexOf('window.meleeNetTimingPauseEligibilitySnapshot='));
assert(prefix.includes('meleeStadiumFirstCssFreshRowLaunch')&&prefix.includes('meleeStadiumFirstCssFreshRowKick'));
const fields=['source_stream_sha256','consumed_pad_sequence','source_tick_sequence','source_tick_value','source_draw_ordinal','original_source_frame','phase_relation','source_scene_kind','host_source_scene','host_menu_phase','native_post_host_tick_frame','random_seed_hex','pad_state_hex'];
const drawFields=['source_tick','draw_ordinal','pad_state_hex','random_seed_hex','scene_frame','scene_kind','scene_routing_getters'];
const pointerFields=['on_unpause_override','on_pause_override','check_for_pauser_override','on_match_start','on_frame_start','on_frame_end','on_match_end','x54_pointer','x58_pointer'];
function fixture(){
 const files=new Map();
 const pin=(name,bytes)=>{files.set(name,bytes);return {path:name,bytes:bytes.byteLength,sha256:digest(bytes)};};
 const jp=(name,value)=>pin(name,Buffer.from(JSON.stringify(value)));
 const manifest=pin('/fixture/manifest',Buffer.from('exact context manifest'));
 const producers=Object.fromEntries(Array.from({length:13},(_,index)=>['role'+index,pin('/fixture/producer'+index,Buffer.from('producer'+index))]));
 const source='11'.repeat(32),raw=Buffer.from(Array.from({length:44},(_,i)=>(128+i)&255));
 const row={ordinal:0,consumed_pad_sequence:30,source_tick_sequence:35,draw_enter_sequence:40,draw_return_sequence:45,pad_poll_sequence:50,raw_four_port_hex:raw.toString('hex'),consume_source_tick:0,tick_source_tick:0,consume_draw_ordinal:0,tick_draw_ordinal:0,draw_source_tick:1,draw_ordinal:0,poll_source_tick:1,poll_draw_ordinal:1};
 const names=['pad_consume','source_tick','draw_enter','draw_return','pad_poll'],refs=[30,35,40,45,50];
 const context={manifestIdentity:manifest,pinIdentities:producers,contextBytes:Buffer.from('literal bootstrap bytes; codec controls test real v2 structure'),contextBinding:{source_sha256:source,entry_sequence:10,return_sequence:20},expectedForComparison:{provenance:{stream_sha256:source},expected_return:{scene_kind:8,random_seed_hex:'312151c3',pad_state_hex:'0102',ko_counts_hex:'000000000000',css:{match_type:0,vs:{start:{rules:Object.fromEntries(pointerFields.map(p=>[p,null]))}}}}}};
 const topology=jp('/fixture/topology',{reviewed:'same producer first row'});
 const expected={schema:'stadium-fresh-first-css-row-host-expected-v1',source_admission:false,whole_session_equivalent:false,provenance:{constructor_manifest:manifest,topology,stream_sha256:source,row},expected_post_tick:{source_stream_sha256:source,consumed_pad_sequence:30,source_tick_sequence:35,random_seed_hex:'HOST_ONLY_EXPECTED'},comparison_fields:drawFields,tick_scalar_comparison_fields:fields,tick_deep_comparison_fields:['scene_routing_getters']};
 const receipt={schema:'stadium-fresh-first-css-consumed-row-receipt-v1',result:'PASS_ORIGINAL_ONLY_FIRST_ROW_PREPARATION',constructor_manifest:manifest,producer_pins:structuredClone(producers),raw_row:pin('/fixture/raw',raw),expected_host_only:jp('/fixture/expected',expected),selected_source_rows:jp('/fixture/selected',{stream_sha256:source,rows:names.map((boundary,i)=>({seq:refs[i],payload:{boundary}}))}),topology,literal_port_status_hex:[0,1,2,3].map(p=>raw.subarray(p*11,(p+1)*11).toString('hex')),source_pairing:names.map((boundary,i)=>({seq:refs[i],boundary}))};
 const refresh=()=>{const receiptPin=jp('/fixture/receipt',receipt);return {first_css_fresh_row:{receipt:receiptPin}};};
 return {files,context,receipt,expected,raw,row,pin,jp,refresh};
}
const getLoader=new Function('assert','fs','path','digest','values','preflight','loadFirstCssConstructorReturnInputs',loader+'\nreturn loadFirstCssFreshRowInputs();');
async function load(f){return getLoader(assert,{readFile:async name=>{assert(f.files.has(name),name);return f.files.get(name);}},path,digest,{'first-css-row-receipt':'/fixture/receipt'},f.refresh(),async()=>f.context);}
let refused=0;
const good=fixture();const input=await load(good);
assert.equal(input.bytes.consumed_pad_bundle.byteLength,136);assert.equal(input.bytes.consumed_pad_bundle.subarray(92).toString('hex'),good.raw.toString('hex'));
assert(!input.bytes.consumed_pad_bundle.includes(Buffer.from('HOST_ONLY_EXPECTED')));
for(const change of ['raw-length','raw-hash','roles','unordered','clock','narrow-draw','narrow-tick','selected-boundary','different-source']){
 const f=fixture();
 if(change==='raw-length')f.receipt.raw_row=f.pin('/fixture/raw',f.raw.subarray(1));
 if(change==='raw-hash')f.receipt.raw_row.sha256='00'.repeat(32);
 if(change==='roles'){assert.notEqual(f.receipt.producer_pins,f.context.pinIdentities);delete f.receipt.producer_pins.role0;assert.equal(Object.keys(f.context.pinIdentities).length,13);}
 if(change==='unordered')f.expected.provenance.row.source_tick_sequence=30;
 if(change==='clock')f.expected.provenance.row.tick_source_tick=1;
 if(change==='narrow-draw')f.expected.comparison_fields=f.expected.comparison_fields.slice(1);
 if(change==='narrow-tick')f.expected.tick_scalar_comparison_fields=f.expected.tick_scalar_comparison_fields.slice(1);
 if(change==='selected-boundary'){const s=JSON.parse(f.files.get('/fixture/selected'));s.rows[1].payload.boundary='pad_poll';f.receipt.selected_source_rows=f.jp('/fixture/selected',s);}
 if(change==='different-source')f.expected.provenance.stream_sha256='22'.repeat(32);
 f.receipt.expected_host_only=f.jp('/fixture/expected',f.expected);
 try{await load(f);throw Error('malformed fixture accepted: '+change);}catch(e){if(e.message.startsWith('malformed fixture accepted'))throw e;refused++;}
}
// Expectations never affect delivered PAD/schedule bytes even when host-only
// data changes. They are compared on the host after source execution instead.
const changed=fixture();changed.expected.expected_post_tick.random_seed_hex='DIFFERENT_HOST_ONLY_EXPECTED';changed.receipt.expected_host_only=changed.jp('/fixture/expected',changed.expected);
assert.deepEqual((await load(changed)).bytes.consumed_pad_bundle,input.bytes.consumed_pad_bundle);

const run=new Function('assert','input','options',`'use strict';return (async()=>{
 const events=[],queue=[];let armCalls=0,launchCalls=0,kickCalls=0,phase=0,running=0;
 let inside=false,persisted=0;
 const window={};let observed={schema:'melee-web-stadium-first-css-fresh-row-observation-v3',mode:'fresh_consumed_tick_draw_v3',state:'armed',error:null,native_phase:0,running:false,host_entered:false,armed_owner_stable:true,armed_world_generation:17,source_steps:0,source_draws:0,host_tick_calls:0,host_draw_calls:0,aurora_begin_calls:0,aurora_end_calls:0};
 const originalModule={HEAPU8:new Uint8Array(40000),next:100,
 _malloc(n){const p=this.next;this.next+=n;return p;},_free(){},
 _melee_web_native_menu_stadium_first_css_constructor_return_v2_arm(){throw Error('v2 arm forbidden');},
 _melee_web_native_menu_stadium_first_css_draw_arm(){throw Error('legacy arm forbidden');},
 _melee_web_native_menu_stadium_first_css_fresh_row_v3_arm(cp,cs,rp,rs){events.push('arm');armCalls++;assert.equal(rs,136);assert.deepEqual(Array.from(originalModule.HEAPU8.slice(rp,rp+rs)),Array.from(input.bytes.consumed_pad_bundle));return 1;},
 _melee_web_native_menu_stadium_first_css_draw_observe(){return 1;},UTF8ToString(){return JSON.stringify(observed);},
 _melee_web_native_menu_phase(){return phase;},_melee_web_native_menu_running(){return running;},
 _melee_web_native_menu_launch(){assert(inside);events.push('launch');launchCalls++;phase=1;running=1;observed={...observed,state:'entered',native_phase:1,running:true,host_entered:true,entry:structuredClone(input.contextExpected.expected_return)};observed.entry.source_scene=1;observed.entry.abi_padding_excluded=['pad_x5C'];observed.entry.css.vs.start.rules.pad_x5C='00000000';for(const p of ${JSON.stringify(pointerFields)})observed.entry.css.vs.start.rules[p]='null';observed.source_stream_sha256=input.contextExpected.provenance.stream_sha256;return 1;},
 _melee_web_native_menu_stadium_first_css_fresh_row_v3_kick(){assert(inside);events.push('kick');kickCalls++;observed.state='kicked';return 1;},
 _melee_web_native_menu_stadium_first_css_draw_kick(){throw Error('legacy kick forbidden');}};
 let Module=originalModule;
 let owner={handle:{getState(){assert(inside);return {state:phase?'css':'prepared',busy:false,loading:null,requiresReload:false,ready:true,canStart:!phase,phase,running:!!running};}}};
 let boundary=fn=>new Promise((resolve,reject)=>queue.push({fn,resolve,reject}));
 const check=n=>{assert.equal(n,1);return n;};
 const mutate=what=>{if(what==='owner')owner={handle:owner.handle};else if(what==='module'){Module={...Module};globalThis.Module=Module;}else if(what==='generation')observed.armed_world_generation++;else if(what==='unload'){phase=0;running=0;observed.state='idle';}else if(what==='reprepare')owner.handle={getState:()=>({state:'preparing',busy:true})};else if(what==='failed')observed.state='failed';};
 const audio=async label=>{events.push(label);if(options.at===label)mutate(options.change);if(options.error===label)throw Error(label+' failure');};
 const prepareAudio=()=>audio('prepare'),pauseAudioForPreparation=()=>audio('pause'),waitForAudioRender=()=>audio('render');
 const resetTiming=()=>events.push('reset'),syncAudio=()=>events.push('sync');let inputDirty=false;
 const $=()=>({focus:()=>{}});
 ${helper}
 globalThis.Module=Module;globalThis.meleeStadiumFirstCssFreshRowLaunch=window.meleeStadiumFirstCssFreshRowLaunch;globalThis.meleeStadiumFirstCssFreshRowKick=window.meleeStadiumFirstCssFreshRowKick;
 const firstCssFreshRow=true,firstCssBrowserDrawInputs=input;
 const page={evaluate:async(fn,arg)=>fn(arg)};
 const timeout=p=>p,remaining=()=>5000,LIMITS={sourceTransitionMs:20000};
 const report={scenario:{}},persistReport=async()=>{persisted++;if(persisted===1&&options.beforeKick)mutate(options.beforeKick);};
 const sourceSnapshot=async()=>({phase,running});
 const readFirstCssBrowserDrawObservation=async()=>({...structuredClone(observed),native_phase:phase,running});
 const driver={launch(){throw Error('ordinary driver launch forbidden');}};
 ${entryComparator}
 let done=false,error=null;
 const job=(async()=>{${prefix}})().catch(e=>{error=e.message;}).finally(()=>{done=true;});
 for(let i=0;i<100&&!done;i++){await Promise.resolve();while(queue.length){const c=queue.shift();inside=true;try{c.resolve(c.fn());}catch(e){c.reject(e);}finally{inside=false;}}}
 await job;
 if(kickCalls){await assert.rejects(window.meleeStadiumFirstCssFreshRowKick(),/already attempted/);await assert.rejects(window.meleeStadiumFirstCssFreshRowLaunch(17),/already attempted/);}
 for(const key of ['Module','meleeStadiumFirstCssFreshRowLaunch','meleeStadiumFirstCssFreshRowKick'])delete globalThis[key];
 return {events,armCalls,launchCalls,kickCalls,error};
})()`);
const baseline=await run(assert,input,{});assert.equal(baseline.error,null,JSON.stringify(baseline));assert.equal(baseline.kickCalls,1);
assert.deepEqual(baseline.events.filter(x=>['arm','prepare','pause','render','launch','kick'].includes(x)),['arm','prepare','pause','render','launch','kick']);
let controlCount=1;
for(const beforeKick of ['owner','module','generation','unload','reprepare','failed']){const r=await run(assert,input,{beforeKick});assert(r.error,JSON.stringify(r));assert.equal(r.kickCalls,0);controlCount++;}
for(const at of ['prepare','pause','render']){
 for(const change of ['owner','module','generation','unload','reprepare','failed']){const r=await run(assert,input,{at,change});assert(r.error,JSON.stringify(r));assert.equal(r.launchCalls,0);assert.equal(r.kickCalls,0);controlCount++;}
 const r=await run(assert,input,{error:at});assert(r.error);assert.equal(r.launchCalls,0);controlCount++;
}
console.log(JSON.stringify({schema:'stadium-fresh-css-row-js-controls-v1',result:'PASS',receipt_refusals:refused,actual_helper_driver_cases:controlCount,host_expectations_excluded_from_input:true,scope:'Actual extracted receipt loader, queued prepared launch/kick and existing driver arm/entry/kick prefix with explicit doubles. No browser/game/native engine.'}));
