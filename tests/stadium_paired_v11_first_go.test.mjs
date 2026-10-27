// Actual harness functions and projection; native/page status leaves modeled.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import {parseArgs} from 'node:util';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {typedOriginalValues,compareLaterBGate2Go} from './stadium_matched_setup_cf_consumer.mjs';
const source=readFileSync(new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url),'utf8');
const projectionPath=process.env.MELEE_PAIRED_FIRST_GO_PROJECTION;
function body(name){const start=source.indexOf(`function ${name}(`);assert.notEqual(start,-1);const next=source.indexOf('\n}',start);assert.notEqual(next,-1);return source.slice(start,next+2);}
function functions(extra={}){const ctx={typedOriginalValues,compareLaterBGate2Go,...extra};vm.createContext(ctx);vm.runInContext(body('derivePairedFirstGo')+'\n'+body('comparePairedFirstGo'),ctx);return ctx;}
function fixture(){assert.ok(projectionPath,'explicit actual projection required');const p=JSON.parse(readFileSync(projectionPath));const ctx=functions();const e=ctx.derivePairedFirstGo(p,p.recipe_sha256,p.actual_frame_count),c=e.match.constructor.state_projection;
 const s={schema:'melee-web-stadium-paired-v11-first-go-v1',version:1,scope:'first_match_setup_actual_go_first_post_go_tick_and_closed_draw_batch',enabled:true,admitted:true,setup_captured:true,go_armed:true,go_observed:true,first_post_go_tick_captured:true,stop_requested:true,stopped:true,failed:false,error:null,native_retired:false,whole_session_complete:false,match_index:0,
 setup_input_cursor:e.setupCursor,go_input_cursor:e.goCursor,post_go_input_cursor:e.postCursor,closure_action_input_cursor:e.closureActionCursor,closure_input_cursor:e.closureCursor,post_go_source_tick_before:e.post.tick.source_tick,post_go_source_tick_after:e.post.tick.source_tick+1,natural_batch_tail_ticks:e.tail,draws_before:e.drawsBefore,draws_after:e.drawsAfter,
 setup:{schema:'melee-web-stadium-paired-v11-setup-v1',match_index:0,input_cursor:e.setupCursor,seed_hex:e.match.setup.rng.value_hex,typed22:{world_generation:7,world_ticks:0,source_frame:0},constructor:{scope:'shared-constructor-aggregate-only',entry_count:c.constructor_entry_count,parameter_roles:{min_parameter:'parameter_x0',max_parameter:'parameter_x4'},min_parameter:c.parameter_x0,max_parameter:c.parameter_x4,random_bound:c.random_bound,random_result:c.random_result,seed_before:c.seed_before_hex,seed_after:c.seed_after_hex,randi_call_count:1}},
 go_world_tick:e.go.tick.source_tick,go_event:{generation:7,world_ticks:e.go.tick.source_tick,source_seed_valid:1,sequence:3,source_branch:0,map2_gate:Number(e.match.go.state.map2.gate_mask_80),display_mode:e.match.go.state.display.mode_s16,source_seed_hex:e.match.go.rng.value_hex},
 first_post_go_typed22:{...e.typed,schema:'melee-web-stadium-per-tick-common-fields-native-v3',world_generation:7,source_tick:e.post.tick.source_tick,world_ticks:e.post.tick.source_tick+1,seed_hex:e.post.rng.value_hex},
 first_post_go_pad_queue:{schema:'melee-web-stadium-hsd-pad-queue-observation',version:2,source_generation:7,source_tick_before:e.post.tick.source_tick,source_tick_after:e.post.tick.source_tick+1,qnum_before:1,qread_before:0,qcount_before:1,qcount_after_hsd:0,tick_complete:true,queue_slot_status_hex:e.post.pads}};return {ctx,e,s};}
test('actual projection derives first strict postGO and unchanged pure typed comparison',{skip:!projectionPath},()=>{const {ctx,e,s}=fixture();assert.equal(ctx.comparePairedFirstGo(s,e).pass,true);for(const mutate of [s=>s.setup.seed_hex='00000000',s=>s.first_post_go_typed22.seed_hex='00000000',s=>s.first_post_go_pad_queue.queue_slot_status_hex=['00'.repeat(11),...s.first_post_go_pad_queue.queue_slot_status_hex.slice(1)].map((x,i)=>i?x:'ff'.repeat(11)),s=>s.closure_input_cursor++,s=>s.first_post_go_typed22.world_generation++,s=>s.go_event.source_seed_hex='00000000']){const wrong=structuredClone(s);mutate(wrong);assert.throws(()=>ctx.comparePairedFirstGo(wrong,e));}});
test('actual getter path suppresses all native reads while owner/Asyncify busy',async()=>{let reads=0,idle=false;const status={enabled:false};const Module={stadiumMatchedSetupNativeIdle:()=>idle,_melee_web_native_menu_stadium_paired_v11_status:()=>{reads++;return 1;},UTF8ToString:()=>JSON.stringify(status),_melee_web_native_menu_phase:()=>3,_melee_web_native_menu_running:()=>1};const ctx={page:{evaluate:fn=>vm.runInNewContext('('+fn.toString()+')()',{Module})},observePageOperation:(_,fn)=>fn()};vm.createContext(ctx);vm.runInContext('async '+body('readPairedFirstGoStatus'),ctx);assert.equal((await ctx.readPairedFirstGoStatus()).busy,true);assert.equal(reads,0);idle=true;assert.equal((await ctx.readPairedFirstGoStatus()).busy,false);assert.equal(reads,1);});
test('actual firstGO phase compares stopped status and uses exactly one existing unload',{skip:!projectionPath},async()=>{const {e,s}=fixture();const clicks=[],writes=[];let retired=false;const ctx=functions({Date,Promise,setTimeout,report:{paired_v11_first_go:{}},pairedFirstGoExpected:e,replayTimeoutMs:1000,replayPollMs:1,pageErrors:[],write:async(name)=>writes.push(name),page:{locator:label=>({click:async()=>{clicks.push(label);if(label==='#unload')retired=true;}})},readPairedFirstGoStatus:async()=>({busy:false,status:retired?{...s,native_retired:true}:s,phase:retired?0:3,running:0})});vm.runInContext('async '+body('pairedFirstGoPhase'),ctx);await ctx.pairedFirstGoPhase();assert.deepEqual(clicks,['#retail-replay-start','#unload']);assert.equal(ctx.report.paired_v11_first_go.component_pass,true);assert.equal(writes.length,2);});

// Actual parseArgs table and complete pre-resource option prelude; mode/probe URL
// helpers are modeled leaves. No filesystem write, browser import or launch occurs.
test('actual CLI prelude admits bounded paired artifacts and refuses incomplete or conflicting routes',async()=>{
 const start=source.indexOf('const {values, tokens} = parseArgs(');
 const end=source.indexOf('await fs.mkdir(output, {recursive: false});',start);
 assert.ok(start>=0 && end>start);
 const prelude=source.slice(start,end).replaceAll('import.meta.url','sourceUrl');
 const base=['--url','http://127.0.0.1:8787/runtime.html','--disc','/inputs/disc.iso','--recipe','/inputs/recipe.mwrc',
  '--out','/output/new','--mode','state','--runtime-incidents','--artifact-root','/artifacts','--runtime-data','/artifacts/gameplay_menu_browser.data',
  '--phase-timeout','150000','--replay-timeout','150000','--stadium-paired-v11-first-go-projection','/inputs/projection.json',
  '--stadium-paired-v11-first-go-projection-sha256','a'.repeat(64)];
 async function run(args){const context={URL,path,fileURLToPath,sourceUrl:new URL('../scripts/capture_whole_session_browser.mjs',import.meta.url).href,
  parseArgs:options=>parseArgs({...options,args}),resolveCaptureMode:mode=>({mode,diagnostic:false}),
  incidentRecordingUrl:value=>new URL(value),parseRngDrawProbe:()=>null,parseHitTransitionProbe:()=>null,
  NATURAL_PAUSE_PROTOCOL:{},STOPPED_SCENE_PAIR_PROTOCOL:{},OBSERVED_B_REFERENCE:{id:'observed'},LONG_B_REFERENCE:{id:'long'}};
  vm.createContext(context);return vm.runInContext('(async()=>{'+prelude+';return {pairedFirstGo,inputPaths,artifactRoot,phaseTimeoutMs,replayTimeoutMs};})()',context);}
 const admitted=await run(base);assert.equal(admitted.pairedFirstGo,true);assert.equal(admitted.artifactRoot,'/artifacts');
 assert.ok(admitted.inputPaths.includes('/inputs/projection.json'));assert.equal(admitted.phaseTimeoutMs,150000);assert.equal(admitted.replayTimeoutMs,150000);
 function without(flag){const args=[...base],i=args.indexOf(flag);args.splice(i,2);return args;}
 for(const flag of ['--artifact-root','--runtime-data'])await assert.rejects(run(without(flag)),/Paired v11 first-GO requires/);
 for(const flag of ['--phase-timeout','--replay-timeout']){const args=[...base];args[args.indexOf(flag)+1]='150001';await assert.rejects(run(args),/Paired v11 first-GO requires/);}
 await assert.rejects(run([...base,'--expected-frame0','/inputs/frame0.json']),/Paired v11 first-GO requires/);
 await assert.rejects(run([...base,'--stadium-later-sss-exit']),/Paired v11 first-GO requires/);
 const ordinary=without('--stadium-paired-v11-first-go-projection');ordinary.splice(ordinary.indexOf('--stadium-paired-v11-first-go-projection-sha256'),2);
 await assert.rejects(run(ordinary),/--artifact-root require/);
});
