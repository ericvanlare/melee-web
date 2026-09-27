import assert from 'node:assert/strict';
import {bindResultsEntryPacket,readResultsEntryPacket} from './results_entry_packet.mjs';
import {queueResultsP1StartAtCurrentSource} from './results_source_pad_input.mjs';

const packet={schema:'melee-web-results-entry-v1',abi:{target:'wasm32',byte_order:'little-endian',pointer_bytes:4},
  match_index:1,entry_seed:0xfedcba98,
  sizeof:{MatchExitInfo:2,ResultsMatchInfo:3},terminal_hex:'aa00',results_info_hex:'bb0000',pad:{bytes:822,hex:'00'.repeat(822)}};
const artifacts=['js','wasm'].map(extension=>({url:`http://127.0.0.1:8871/gameplay_menu_browser.${extension}?v=1`,
  bytes:100,status:200,sha256:'a'.repeat(64)}));
artifacts[1].hash_basis={method:'same-origin-static-file-sha256-plus-http-metadata',
  path:'build/browser/gameplay_menu_browser.wasm',content_length:100,content_encoding:null};
const bound=bindResultsEntryPacket(packet,artifacts);
assert.equal(bound.build_binding.status,'bound');
assert.equal(bound.build_binding.kind,'http-response-metadata-plus-static-file-sha256');
assert.deepEqual(bound.build_binding.artifacts[1].hash_basis,artifacts[1].hash_basis);
assert.deepEqual(bound.packet,packet);
assert.throws(()=>bindResultsEntryPacket(packet,[]),/Missing served/);
assert.throws(()=>bindResultsEntryPacket(packet,[...artifacts,{...artifacts[1],sha256:'b'.repeat(64)}]),/Conflicting/);
assert.throws(()=>bindResultsEntryPacket({...packet,results_info_hex:'bb'},artifacts));
assert.throws(()=>bindResultsEntryPacket({...packet,abi:{...packet.abi,byte_order:'big-endian'}},artifacts));
let calls=0;
const page={evaluate:fn=>fn()};
assert.deepEqual(await readResultsEntryPacket(page),{status:'getter-unavailable'});
globalThis.Module={UTF8ToString:text=>text,_melee_web_native_menu_results_entry_packet:()=>{calls++;return 'null';}};
assert.equal((await readResultsEntryPacket(page)).status,'no-entry');
Module._melee_web_native_menu_results_entry_packet=()=>{calls++;return JSON.stringify(packet);};
assert.deepEqual((await readResultsEntryPacket(page)).packet,packet);
assert.equal(calls,2);
delete globalThis.Module;

const pauseEvents=[];
const diagnostics=[
  'Original Results · Results source frame: 600',
  'Original Results · Results source frame: 600',
];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Paused after a timing disruption. Resume to continue.'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{pauseEvents.push('running');return 0;},
  _melee_web_native_menu_diagnostics:()=>{
    pauseEvents.push('diagnostics');
    return diagnostics.shift()||'Original Results · Results source frame: 600';
  },
  _melee_web_native_menu_pause:value=>pauseEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:(port,button,_x,_y,duration)=>{
    pauseEvents.push(`pad:${port}:${button}:${duration}`);
    return 1;
  },
};
const afterTimingPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.deepEqual(afterTimingPause,{result:1,source_frame:600,resumed_after_timing_pause:true,
  running_before_queue:false,
  status_before_queue:'Paused after a timing disruption. Resume to continue.',
  diagnostics:'Original Results · Results source frame: 600'});
assert.deepEqual(pauseEvents,['running','diagnostics','pause:0','diagnostics','pad:0:4096:10','diagnostics']);

const normalEvents=[];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Original Results'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{normalEvents.push('running');return 1;},
  _melee_web_native_menu_diagnostics:()=>{
    normalEvents.push('diagnostics');
    return 'Original Results · Results source frame: 601';
  },
  _melee_web_native_menu_pause:value=>normalEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:(port,button,_x,_y,duration)=>{
    normalEvents.push(`pad:${port}:${button}:${duration}`);
    return 1;
  },
};
const withoutPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.equal(withoutPause.resumed_after_timing_pause,false);
assert.deepEqual(normalEvents,['running','diagnostics','pad:0:4096:10','diagnostics']);

const unsupportedPauseEvents=[];
globalThis.document={querySelector:selector=>selector==='#status'?
  {textContent:'Paused.'}:null};
globalThis.Module={
  UTF8ToString:text=>text,
  _melee_web_native_menu_running:()=>{unsupportedPauseEvents.push('running');return 0;},
  _melee_web_native_menu_diagnostics:()=>{
    unsupportedPauseEvents.push('diagnostics');
    return 'Original Results · Results source frame: 602';
  },
  _melee_web_native_menu_pause:value=>unsupportedPauseEvents.push(`pause:${value}`),
  _melee_web_native_menu_pad_sample:()=>{
    unsupportedPauseEvents.push('pad');
    return 0;
  },
};
const unsupportedPause=queueResultsP1StartAtCurrentSource({button:4096,duration:10});
assert.equal(unsupportedPause.result,0);
assert.equal(unsupportedPause.resumed_after_timing_pause,false);
assert.deepEqual(unsupportedPauseEvents,['running','diagnostics','pad','diagnostics']);
delete globalThis.Module;
delete globalThis.document;
console.log('Results entry binding and atomic source-tick PAD resume/queue boundary pass');
