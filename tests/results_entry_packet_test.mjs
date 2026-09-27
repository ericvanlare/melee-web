import assert from 'node:assert/strict';
import {bindResultsEntryPacket,readResultsEntryPacket} from './results_entry_packet.mjs';

const packet={schema:'melee-web-results-entry-v1',abi:{target:'wasm32',byte_order:'little-endian',pointer_bytes:4},
  match_index:1,entry_seed:0xfedcba98,
  sizeof:{MatchExitInfo:2,ResultsMatchInfo:3},terminal_hex:'aa00',results_info_hex:'bb0000',pad:{bytes:822,hex:'00'.repeat(822)}};
const artifacts=['js','wasm'].map(extension=>({url:`http://127.0.0.1:8871/gameplay_menu_browser.${extension}?v=1`,
  bytes:100,status:200,sha256:'a'.repeat(64)}));
const bound=bindResultsEntryPacket(packet,artifacts);
assert.equal(bound.build_binding.status,'bound');
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
console.log('Results entry observer retains typed packets with served JS/Wasm identity; no mutation endpoints');
