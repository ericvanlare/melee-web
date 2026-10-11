import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import fs from 'node:fs';
import {readRuntimeOwnerSnapshot,observeRuntimeOwner} from './runtime_owner_observation.mjs';
import {checkNeutralTimeout} from './vs_sudden_death_browser_route.mjs';

// Only native getters and browser scheduling are fixtures. The serialized
// production reader itself runs in the isolated page context.
function pageFixture(states) {
  let index=0,evaluations=0;
  const current=()=>states[Math.min(index,states.length-1)];
  const context=vm.createContext({Module:{
    UTF8ToString:value=>value,
    _melee_web_native_menu_message:()=>current().state.message,
    _melee_web_native_menu_phase:()=>current().state.phase,
    _melee_web_native_menu_running:()=>current().state.running,
    _melee_web_native_menu_diagnostics:()=>current().state.diagnostics,
    _melee_web_native_menu_match_observe:()=>JSON.stringify(current().match),
    _melee_web_native_menu_results_pad_trace:()=>JSON.stringify(current().trace),
  },document:{querySelector:selector=>selector==='#status'?
    {textContent:current().state.status,dataset:{runtimeError:current().state.error}}:
    selector==='#pause'?{disabled:current().state.pause_disabled}:null}});
  return {get evaluations(){return evaluations;},evaluate:async(reader,options)=>{
    const result=vm.runInContext(`(${reader.toString()})(${JSON.stringify(options??{})})`,context);
    evaluations++;index++; // queued owner transition occurs after evaluation.
    return JSON.parse(JSON.stringify(result));
  }};
}

const deferred=JSON.parse(fs.readFileSync(new URL('./fixtures/sd_deferred_continuation_observation.json',import.meta.url),'utf8'));
// The prior phase5 payload is captured; added Pause fields and the scheduling
// sequence are synthetic. V9's exact rejected pair was not retained.
const sourceDeferred={state:{...deferred.state,pause_present:true,pause_disabled:true},match:deferred.match};
const constructing={state:{...sourceDeferred.state,phase:14,message:'Preparing original Sudden Death...'},match:{}};

test('actual serialized reader keeps each owner pair coherent across scheduled SD construction',async()=>{
  const split=pageFixture([sourceDeferred,constructing]);
  const oldState=(await split.evaluate(readRuntimeOwnerSnapshot,{includeMatch:false})).state;
  const nextMatch=(await split.evaluate(readRuntimeOwnerSnapshot)).match;
  assert.throws(()=>checkNeutralTimeout(oldState,nextMatch),'split evaluations reproduce the rejected phase5/empty match hypothesis');
  const page=pageFixture([sourceDeferred,constructing]),retention={};
  const first=await observeRuntimeOwner(page,retention,'completed VS continuation');
  checkNeutralTimeout(first.state,first.match);
  assert.deepEqual(retention.latestOwnerObservation,{label:'completed VS continuation',...first});
  const next=await observeRuntimeOwner(page,retention,'SD construction');
  checkNeutralTimeout(next.state,next.match);
  assert.equal(next.state.phase,14);assert.deepEqual(next.match,{});assert.equal(page.evaluations,2);
});

test('exact rejected pair is retained before strict validation, not replaced by later owner reads',async()=>{
  const invalid={state:sourceDeferred.state,match:{}}; // synthetic malformed source observation.
  const retention={},page=pageFixture([invalid,constructing]);
  const snapshot=await observeRuntimeOwner(page,retention,'strict phase5 rejection');
  assert.throws(()=>checkNeutralTimeout(snapshot.state,snapshot.match));
  assert.deepEqual(retention.latestOwnerObservation,{label:'strict phase5 rejection',...snapshot});
  assert.equal(retention.latestOwnerObservation.state.phase,5);assert.equal(page.evaluations,1);
});

test('host and Results trace use the same serialized evaluation at P2 exit',async()=>{
  const before={state:{...sourceDeferred.state,phase:8,running:1,pause_disabled:false},trace:{completion:'before P2'}};
  const after={state:{...sourceDeferred.state,message:'Preparing original next scene...'},trace:{completion:'after P2'}};
  const retention={},page=pageFixture([before,after]);
  const first=await observeRuntimeOwner(page,retention,'before P2',{includeMatch:false,includeResultsTrace:true});
  assert.equal(first.state.phase,8);assert.equal(first.trace.completion,'before P2');assert.equal(first.match,undefined);
  const next=await observeRuntimeOwner(page,retention,'after P2',{includeMatch:false,includeResultsTrace:true});
  assert.equal(next.state.phase,5);assert.equal(next.trace.completion,'after P2');
  assert.deepEqual(retention.latestOwnerObservation,{label:'after P2',...next});
});
