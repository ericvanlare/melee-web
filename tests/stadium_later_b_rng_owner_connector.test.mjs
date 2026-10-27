import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
import {deriveLaterBRngOwnerWindow,validateRngDrawProbeRows} from '../scripts/rng_draw_probe.mjs';

const root=fileURLToPath(new URL('../',import.meta.url));
const native=fs.readFileSync(root+'src/gameplay_menu_browser.cpp','utf8');
const capture=fs.readFileSync(root+'scripts/capture_whole_session_browser.mjs','utf8');
// Modeled reference shape exercises coordinate derivation; full reference
// admission remains the unchanged strict serialized-projection decoder.
function reference(go=52){
 const actions=[];
 for(let tick=0;tick<180;++tick){const n=1000+tick*4;
  actions.push({kind:'tick',action_index:tick*2,source_tick:tick,consume:{sequence:n},tick:{sequence:n+1}});
  actions.push({kind:'draw',action_index:tick*2+1,source_tick:tick+1,draw_enter:{sequence:n+2},draw_return:{sequence:n+3}});
 }
 return {goTick:go,reference:{setup_global_cursor:943,go:{coordinate:{source_tick:go}},actions}};
}
function nativeSelector(window,expectedGo){
 const start=native.indexOf('  const int packed=EM_ASM_INT({',native.indexOf('void later_b_rng_owner_begin('));
 const end=native.indexOf('  },state.later_b_reference_go_tick);',start);
 assert.ok(start>=0&&end>start);
 const body=native.slice(start+'  const int packed=EM_ASM_INT({'.length,end).replaceAll('$0','expectedGo');
 return vm.runInNewContext(`(function(){${body}})()`,{window,expectedGo});
}
function windowFor(config){return {__meleeLaterBRngOwnerWindow:config.configuration,
 __meleeRngDrawProbeOwners:true,__meleeRngDrawProbeCursors:config.selection.selected};}

test('coordinates derive from supplied actual GO, keep1–9 pairs, and never fall back to84',()=>{
 const row=deriveLaterBRngOwnerWindow('1:3',reference());
 assert.deepEqual(row.selection.selected,[996,997,998]);
 assert.deepEqual(row.coordinates.map(r=>[r.source_tick,r.tick_action_index,r.draw_action_index]),[[53,106,107],[54,108,109],[55,110,111]]);
 assert.equal(deriveLaterBRngOwnerWindow('1:9',reference()).coordinates.length,9);
 assert.equal(deriveLaterBRngOwnerWindow(undefined,reference()),null);
 for(const spec of ['0:1','1:10','3:2','-1:1','1','01:2','128:129'])assert.throws(()=>deriveLaterBRngOwnerWindow(spec,reference()));
 const wrong=reference();wrong.reference.go.coordinate.source_tick++;
 assert.throws(()=>deriveLaterBRngOwnerWindow('1:2',wrong),/actual GO/);
 const reordered=reference();reordered.reference.actions[107].draw_return.sequence=0;
 assert.throws(()=>deriveLaterBRngOwnerWindow('1:1',reordered),/ordered tick\/draw/);
});

test('actual native configuration parser is inactive by default and refuses stale or broadened coordinates',()=>{
 const row=deriveLaterBRngOwnerWindow('1:3',reference());
 assert.equal(nativeSelector({},52),-1);
 assert.equal(nativeSelector(windowFor(row),52),53*256+55);
 for(const mutate of [w=>w.__meleeLaterBRngOwnerWindow.go_tick++,w=>w.__meleeLaterBRngOwnerWindow.prefix_cursor--,
  w=>w.__meleeRngDrawProbeOwners=false,w=>w.__meleeRngDrawProbeCursors.pop(),w=>w.__meleeRngDrawProbeCursors.reverse(),
  w=>w.__meleeLaterBRngOwnerWindow.first_tick=52,w=>w.__meleeLaterBRngOwnerWindow.last_tick=180]){
  const w=structuredClone(windowFor(row));mutate(w);assert.equal(nativeSelector(w,52),-2);
 }
});

test('actual capture initialization enables owners before runtime startup and installs only derived cursors',async()=>{
 const marker='  await page.addInitScript(({pairedFull,cpuObservationRowLimit, captureCpuObservations,';
 const start=capture.indexOf(marker),end=capture.indexOf('  page.setDefaultTimeout',start);
 assert.ok(start>=0&&end>start);
 let fn,args;
 const context={window:{},pairedFull:false,page:{addInitScript:(f,a)=>{fn=f;args=a;}},
  cpuObservationRowLimit:2013,captureCpuObservations:false,rngDrawProbe:deriveLaterBRngOwnerWindow('1:3',reference()).selection,
  laterBRngOwnerWindow:deriveLaterBRngOwnerWindow('1:3',reference()),hitTransitionProbe:null,firstReplayCallbackProbe:false,
  FIRST_REPLAY_BOUNDARY_MARKER_PREFIX:'unused',FIRST_REPLAY_BOUNDARY_MARKER_NAMES:[]};
 await vm.runInNewContext(`(async()=>{${capture.slice(start,end)}})()`,context);
 fn(args);
 assert.equal(context.window.__meleeRngDrawProbeOwners,true);
 assert.deepEqual(Array.from(context.window.__meleeRngDrawProbeCursors),[996,997,998]);
 const row=JSON.stringify({schema:'melee-web-rng-draw-probe',version:2,source_cursor:996,overflowed:false,draws:[]});
 context.window.meleeRngDrawObservation(row);
 assert.equal(context.window.__rngDrawProbeRows.length,1);
 assert.throws(()=>context.window.meleeRngDrawObservation(row),/duplicate/);
 assert.throws(()=>context.window.meleeRngDrawObservation(JSON.stringify({source_cursor:999})),/unselected/);
 fn({...args,rngDrawProbeSelection:null,laterBRngOwnerConfiguration:null});
 assert.equal(context.window.__meleeRngDrawProbeOwners,false);
 assert.equal(context.window.__meleeLaterBRngOwnerWindow,null);
});


test('selected empty windows remain explicit version2 rows and service placement brackets tick plus draw',()=>{
 const row=deriveLaterBRngOwnerWindow('1:3',reference());
 const rows=row.selection.selected.map(source_cursor=>JSON.stringify({schema:'melee-web-rng-draw-probe',version:2,source_cursor,overflowed:false,draws:[]}));
 assert.equal(validateRngDrawProbeRows(rows,row.selection,{observedCursor:998}).complete,true);
 assert.throws(()=>validateRngDrawProbeRows(rows.slice(1),row.selection,{observedCursor:998}),/omitted reached/);
 const service=native.slice(native.indexOf('bool matched_setup_cf_service(){'),native.indexOf('bool matched_setup_cf_service(){')+22000);
 const begin=service.indexOf('later_b_rng_owner_begin(state,ordinal);'),tick=service.indexOf('match->tick(raw);');
 const draw=service.indexOf('match->draw();'),postOwner=service.indexOf('matched_setup_cf_check_owner(state.last_ticks_after,state.last_draws_after);');
 const flush=service.indexOf('if(!tick_action&&!state.later_b_gate2_long_mode)later_b_rng_owner_draw_return(state,ordinal);');
 assert.ok(begin>=0&&begin<tick&&tick<draw&&draw<postOwner&&postOwner<flush);
 assert.doesNotMatch(service.slice(tick,draw),/melee_web_rng_draw_probe_scheduler_return/);
});
