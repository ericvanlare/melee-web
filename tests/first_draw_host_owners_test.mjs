import assert from 'node:assert/strict';
import {readFirstDrawHostOwners} from './match_frame_effect_first_draw_driver.mjs';
let reads,nativeCalls,armCalls;
const snapshot=()=>({generation:'4',unresolvedReads:0,resultCount:0,discSession:true,assetTransfer:false,preparing:false});
function setup(){reads=0;nativeCalls=0;armCalls=0;const m={_melee_web_native_menu_running:()=>0,matchFrameEffectCallbackReturned:true,matchFrameEffectNativeIdle:()=>true,_melee_web_native_menu_first_pair_file_owners_quiescent:()=>{nativeCalls++;return 1;}};globalThis.Module=m;globalThis.__meleeWebFirstDrawComponentModuleV1=m;globalThis.window={__meleeWebFirstPairFileGuardArmed:false,menuFirstPairHostReadSnapshot:()=>{reads++;return snapshot();},menuFirstPairArm:()=>{armCalls++;throw Error('must not arm');}};return m;}
setup();assert.equal(readFirstDrawHostOwners().status,'observed');assert.equal(reads,2);assert.equal(nativeCalls,1);assert.equal(armCalls,0);
for(const patch of [{unresolvedReads:1},{resultCount:1},{discSession:false},{assetTransfer:true},{preparing:true}]){setup();window.menuFirstPairHostReadSnapshot=()=>({...snapshot(),...patch});assert.equal(readFirstDrawHostOwners().reason,'host_file_work_pending');}
for(const value of [null,{}, {...snapshot(),generation:4},{...snapshot(),extra:0},{...snapshot(),resultCount:-1}]){setup();window.menuFirstPairHostReadSnapshot=()=>value;assert.equal(readFirstDrawHostOwners().reason,'host_owner_snapshot_malformed');assert.equal(nativeCalls,0);}
setup();window.menuFirstPairHostReadSnapshot=()=>({...snapshot(),generation:String(++reads)});assert.equal(readFirstDrawHostOwners().reason,'host_owner_snapshot_drift');
setup()._melee_web_native_menu_first_pair_file_owners_quiescent=()=>0;assert.equal(readFirstDrawHostOwners().reason,'native_file_owners_not_quiescent');
setup().matchFrameEffectNativeIdle=()=>false;assert.equal(readFirstDrawHostOwners().reason,'same_module_or_returned_idle_missing');assert.equal(nativeCalls,0);
setup()._melee_web_native_menu_first_pair_file_owners_quiescent=()=>{globalThis.Module={};return 1;};assert.equal(readFirstDrawHostOwners().reason,'module_reader_or_arm_changed');assert.equal(reads,1);
setup();window.menuFirstPairHostReadSnapshot=()=>{globalThis.Module={};return snapshot();};assert.equal(readFirstDrawHostOwners().reason,'module_reader_or_arm_changed');assert.equal(nativeCalls,0);
setup();delete window.menuFirstPairHostReadSnapshot;assert.equal(readFirstDrawHostOwners().reason,'host_owner_readers_missing');
setup();window.__meleeWebFirstPairFileGuardArmed=true;assert.equal(readFirstDrawHostOwners().reason,'module_reader_or_arm_changed');
setup();window.menuFirstPairHostReadSnapshot=()=>{throw Error('read failure');};assert.equal(readFirstDrawHostOwners().reason,'host_owner_reader_failed');
console.log('Actual first-draw host owner point-read controls PASS');
setup()._melee_web_native_menu_running=()=>1;assert.equal(readFirstDrawHostOwners().reason,'same_module_or_returned_idle_missing');assert.equal(nativeCalls,0);

for(const value of [1,{},'idle']){setup().matchFrameEffectNativeIdle=value;assert.equal(readFirstDrawHostOwners().reason,'host_owner_reader_failed');assert.equal(nativeCalls,0);}
setup().matchFrameEffectNativeIdle=()=>{throw Error('idle');};assert.equal(readFirstDrawHostOwners().reason,'host_owner_reader_failed');assert.equal(nativeCalls,0);
setup()._melee_web_native_menu_running=()=>{throw Error('running');};assert.equal(readFirstDrawHostOwners().reason,'host_owner_reader_failed');assert.equal(nativeCalls,0);
setup();globalThis.__meleeWebFirstDrawComponentModuleV1={};assert.equal(readFirstDrawHostOwners().reason,'same_module_or_returned_idle_missing');assert.equal(nativeCalls,0);
setup()._melee_web_native_menu_first_pair_file_owners_quiescent=()=>{globalThis.__meleeWebFirstDrawComponentModuleV1={};return 1;};assert.equal(readFirstDrawHostOwners().reason,'module_reader_or_arm_changed');assert.equal(reads,1);
setup().matchFrameEffectNativeIdle=null;assert.equal(readFirstDrawHostOwners().reason,'same_module_or_returned_idle_missing');assert.equal(nativeCalls,0);
