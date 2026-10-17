import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {fileURLToPath} from 'node:url';
const lib=fileURLToPath(new URL('../.deps/aurora/lib/gfx/aurora_browser_texture_readonly_guard_library.js',import.meta.url));
let passed=0;
function fixture({optin=true, deferred=false}={}) {
  // Synthetic dispatch tests only; these objects are deliberately not GPUs.
  const calls=[];let resolveFence,resolveLost;
  const fence=deferred ? new Promise(r=>{resolveFence=r;}) : Promise.resolve();
  class GPUQueue {submit(...a){calls.push(['submit',this,a]);return 41;} writeTexture(...a){calls.push(['write',this,a]);return 37;} copyExternalImageToTexture(...a){calls.push(['external',this,a]);} onSubmittedWorkDone(){calls.push(['fence',this,[]]);return fence;}}
  class GPUCommandBuffer {}
  class GPUCommandEncoder {finish(...a){calls.push(['finish',this,a]);return new GPUCommandBuffer();} copyBufferToTexture(...a){calls.push(['buffer',this,a]);} copyTextureToTexture(...a){calls.push(['texture',this,a]);}}
  class GPUTexture {constructor(){this.usage=6;}destroy(...a){calls.push(['destroy',this,a]);}}
  class GPUTextureView {}
  class GPUDevice {constructor(q){this.queue=q;this.lost=new Promise(r=>{resolveLost=r;});} createCommandEncoder(...a){calls.push(['create-encoder',this,a]);return new GPUCommandEncoder();} destroy(...a){calls.push(['device-destroy',this,a]);}}
  const types={GPUQueue,GPUCommandEncoder,GPUTexture,GPUTextureView,GPUDevice};
  const queue=new GPUQueue(),texture=new GPUTexture(),view=new GPUTextureView(),device=new GPUDevice(queue);
  const Module={__meleeWebTextureReadonlyFixtureV1:optin};
  const raw=[];[texture,view,device,queue].forEach((v,i)=>raw[(i+1)*4]=v);
  const context={...types,WebGPU:{Internals:{jsObjects:raw,futures:[]}},Module,addToLibrary(x){context.library=x;}};
  vm.createContext(context);vm.runInContext(fs.readFileSync(lib,'utf8'),context);
  vm.runInContext(context.library.$AuroraTextureReadonlyGuardV1__postset,context);
  const native=context.AuroraTextureReadonlyGuardV1, api=Module.__meleeWebTextureReadonlyGuardV1;
  const issue=()=>native.issue(91,4,8,12,16,635,0,636,0,1,0);
  return {calls,context,native,api,Module,queue,texture,view,device,raw,types,issue,resolveFence,resolveLost};
}
async function active(options) {const f=fixture(options);assert.equal(f.issue(),1);assert.equal(f.api.status().scope,'native-issued-unarmed-component-only');await f.api.armFixture(f.api.capability());assert.equal(f.api.status().scope,'fixture-component-only');return f;}
function refused(f,fn,code) {
 const count=f.calls.length,dispatch=f.api.status().dispatches;
 assert.throws(fn,/texture-readonly-guard/);
 assert.equal(f.api.status().phase,'TERMINAL');assert.equal(f.api.status().firstFailure.code,code);
 assert.equal(f.calls.length,count);assert.equal(f.api.status().dispatches,dispatch);
 assert.throws(()=>f.api.close(f.api.capability()));
 assert.equal(f.api.status().firstFailure.code,code);++passed;
}
{
 const f=await active();const data={},layout={},size={},other=new f.types.GPUTexture(),dst={texture:other};
 const before=f.calls.length;
 assert.equal(f.queue.writeTexture(dst,data,layout,size),37);
 assert.equal(f.calls.length,before+1);assert.equal(f.calls.at(-1)[1],f.queue);
 assert.deepEqual(f.calls.at(-1)[2],[dst,data,layout,size]);
 assert.equal(f.api.close(f.api.capability()).phase,'CLOSED');assert.equal(f.api.status().gameplayAdmission,false);++passed;
}
for(const [name,action,code] of [
 ['write',f=>f.queue.writeTexture({texture:f.texture}), 'TARGET_writeTexture'],
 ['buffer',f=>new f.types.GPUCommandEncoder().copyBufferToTexture({}, {texture:f.texture}), 'TARGET_copyBufferToTexture'],
 ['texture',f=>new f.types.GPUCommandEncoder().copyTextureToTexture({}, {texture:f.texture}), 'TARGET_copyTextureToTexture'],
 ['external',f=>f.queue.copyExternalImageToTexture({}, {texture:f.texture}), 'TARGET_copyExternalImageToTexture'],
 ['destroy',f=>f.texture.destroy(), 'TARGET_destroy'],
 ['device',f=>f.device.destroy(), 'TARGET_destroy'],
 ['saved-installed-alias',f=>Reflect.apply(f.queue.writeTexture,f.queue,[{texture:f.texture}]),'TARGET_writeTexture'],
 ['prototype-key',f=>{f.context.WebGPU.Internals.jsObjects.__proto__={};},'TABLE_KEY'],
 ['set',f=>{f.context.WebGPU.Internals.jsObjects[4]=f.texture;},'MAPPING_MUTATION'],
 ['define',f=>Object.defineProperty(f.context.WebGPU.Internals.jsObjects,'4',{value:f.texture}),'MAPPING_MUTATION'],
 ['delete',f=>{delete f.context.WebGPU.Internals.jsObjects[4];},'NATIVE_FINAL_RELEASE_OR_TABLE_DELETE'],
 ['truncate',f=>{f.context.WebGPU.Internals.jsObjects.length=4;},'MAPPING_TRUNCATION'],
 ['alias',f=>{f.context.WebGPU.Internals.jsObjects[20]=f.texture;},'MAPPING_MUTATION'],
 ['table-replace',f=>{f.context.WebGPU.Internals.jsObjects=[];},'TABLE_REPLACEMENT'],
 ['internals-replace',f=>{f.context.WebGPU.Internals={};},'TABLE_REPLACEMENT'],
 ['define-table',f=>Object.defineProperty(f.context.WebGPU.Internals,'jsObjects',{value:[]}),'TABLE_REPLACEMENT'],
 ['delete-table',f=>{delete f.context.WebGPU.Internals.jsObjects;},'TABLE_REPLACEMENT'],
 ['detach',f=>f.native.detach(91),'OWNER_DETACH'],
 ['device-epoch',f=>f.native.deviceEpochChange(),'DEVICE_EPOCH_CHANGE'],
 ['duplicate',f=>f.issue(),'DUPLICATE_CAPABILITY'],
 ['unknown-destination',f=>f.queue.writeTexture({get texture(){throw Error('must not evaluate');}}),'UNKNOWN_DESTINATION'],
 ['method-set',f=>{f.types.GPUQueue.prototype.writeTexture=()=>{};},'METHOD_REPLACEMENT'],
 ]) {const f=await active();refused(f,()=>action(f),code);}
{
 const f=fixture();f.issue();refused(f,()=>f.api.armGameplay(),'GAMEPLAY_ADMISSION_UNCLASSIFIED');
}
{
 const f=fixture({optin:false});f.issue();await assert.rejects(f.api.armFixture(f.api.capability()),/PENDING_WORK_UNCLASSIFIED/);assert.equal(f.api.status().phase,'TERMINAL');++passed;
}
{
 const f=fixture({deferred:true});f.issue();const p=f.api.armFixture(f.api.capability());
 assert.equal(f.api.status().phase,'ARMING');refused(f,()=>f.queue.writeTexture({texture:f.texture}),'TARGET_writeTexture');
 f.resolveFence();await assert.rejects(p,/TARGET_writeTexture/);++passed;
}
{
 const f=await active();f.resolveLost();await Promise.resolve();assert.equal(f.api.status().firstFailure.code,'DEVICE_LOST');assert.throws(()=>f.queue.writeTexture({texture:new f.types.GPUTexture()}));++passed;
}
{
 const f=fixture();const table=Object.getOwnPropertyDescriptor(f.context.WebGPU.Internals,'jsObjects').value;
 assert.equal(table,f.context.WebGPU.Internals.jsObjects);assert.equal(Array.isArray(table),true);
 table[24]={};assert.equal(f.raw[24],table[24]);f.issue();f.native.detach(92);assert.equal(f.api.status().phase,'ISSUED');++passed;
}
{
 const f=fixture();const memory=new ArrayBuffer(1024);f.context.HEAPU8=new Uint8Array(memory);f.context.HEAPU32=new Uint32Array(memory);
 const accessor=fileURLToPath(new URL('../.deps/aurora/lib/gfx/aurora_browser_future_owner_accessor_library.js',import.meta.url));
 vm.runInContext(fs.readFileSync(accessor,'utf8'),f.context);
 vm.runInContext(f.context.library.$AuroraBrowserFutureOwnerV5__postset,f.context);
 assert.equal(f.context.AuroraBrowserFutureOwnerV5.summary(8,96),1);
 f.issue();await f.api.armFixture(f.api.capability());
 assert.equal(f.context.AuroraBrowserFutureOwnerV5.summary(8,96),1);
 assert.equal(f.context.AuroraBrowserFutureOwnerV5.rowCount(),4);
 f.api.close(f.api.capability());++passed;
}
console.log(JSON.stringify({scope:'synthetic-dispatch-and-identity-controls',passed,realGpu:false}));

// Depth no-work is a separate component. Synthetic GPU classes above are not real devices.
function depthFacts(enabled){const words=Array(35).fill(0);words[0]=1;words[1]=enabled;words[4]=3;for(let i=0;i<3;++i)words[8+i*9]=i;return words;}
function depthReady(){const f=fixture();assert.equal(f.native.observeDepthFacts(0,0,depthFacts(0)),true);f.native.deviceEpochChange();f.native.observeDepthEvent(1);assert.equal(f.native.observeDepthFacts(2,12,depthFacts(1)),true);return f;}
let depthPassed=0;
{
 const f=depthReady(),status=f.Module.__meleeWebDepthNoWorkV1.status;
 assert.equal(status().phase,'READY');assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),true);
 assert.equal(status().phase,'ACTIVE');assert.equal(status().gameplayAdmission,false);assert.equal(f.calls.length,0);
 assert.equal(status().snapshots.preMain.length,35);assert.equal(status().snapshots.arm.length,35);++depthPassed;
}
for(const [kind,name] of [[3,'READ'],[4,'REQUEST'],[5,'CONSUME'],[6,'COPY'],[7,'MAP'],[8,'CALLBACK'],[9,'RESET'],[10,'SHUTDOWN']]){
 for(const active of [false,true]){
  const f=depthReady();if(active)assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),true);
  const before=f.calls.length;assert.doesNotThrow(()=>f.native.observeDepthEvent(kind));
  const first=f.Module.__meleeWebDepthNoWorkV1.status().firstFailure;assert.equal(first.code,'DEPTH_FORBIDDEN_'+name);
  assert.equal(f.calls.length,before); // observation neither dispatches nor suppresses a native API
  assert.equal(f.native.observeDepthFacts(0,0,depthFacts(0)),false); // a native reset/rewind cannot clear host history
  assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure,first);++depthPassed;
 }
}
{
 const f=fixture();f.native.observeDepthEvent(1);assert.equal(f.native.observeDepthFacts(0,0,depthFacts(0)),false);
 assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure.code,'DEPTH_INITIALIZE_ORDER');++depthPassed;
}
for(const mutate of [w=>w.pop(),w=>w[0]=2,w=>w[2]=1,w=>w[4]=2,w=>w[7]=1,w=>w[9]=1,w=>w[14]=4,w=>w[17]=0,w=>w[0]=NaN]){
 const f=fixture(),words=depthFacts(0);mutate(words);assert.equal(f.native.observeDepthFacts(0,0,words),false);
 assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure.code,'DEPTH_PREMAIN_COVERAGE');++depthPassed;
}
{
 const f=depthReady();f.native.deviceEpochChange();assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),false);
 assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure.code,'DEPTH_DEVICE_EPOCH_CHANGE');++depthPassed;
}
{
 const f=depthReady();f.raw[12]=new f.types.GPUDevice(f.queue);assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),false);
 assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure.code,'DEPTH_ARM_STATE');++depthPassed;
}
{
 const f=depthReady();f.Module.__meleeWebDepthNoWorkBridgeFailureV1=true;assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),false);
 assert.equal(f.Module.__meleeWebDepthNoWorkV1.status().firstFailure.code,'DEPTH_BRIDGE_COVERAGE');++depthPassed;
}
console.log(`depth no-work actual factory: ${depthPassed} focused synthetic controls passed; no gameplay admission`);

{
 const f=depthReady();Object.freeze(f.Module);f.Module.__meleeWebDepthNoWorkBridgeFailureV1=true;f.Module.__meleeWebDepthNoWorkBridgeFailureV1=false;
 assert.equal(f.Module.__meleeWebDepthNoWorkBridgeFailureV1,true);assert.equal(f.native.observeDepthFacts(11,12,depthFacts(1)),false);++depthPassed;
}
console.log(`depth monotonic bridge failure control passed; total ${depthPassed}`);

// Prospective selected component, synthetic objects only. No gameplay/restore.
let intervalPassed=0;
async function selectedActive(options){const f=fixture(options);f.issue();assert.equal(f.api.status().scope,'native-issued-unarmed-component-only');await f.api.armSelectedComponent(f.api.capability());return f;}
{
 const f=await selectedActive(),createArgs={label:'actual args'},finishArgs={label:'finish args'};
 const before=f.calls.length,e=f.device.createCommandEncoder(createArgs),b=e.finish(finishArgs),list=[b];
 assert.equal(f.queue.submit(list),41);
 assert.deepEqual(f.calls.slice(before).map(x=>x[0]),['create-encoder','finish','submit']);
 assert.equal(f.calls[before][1],f.device);assert.deepEqual(f.calls[before][2],[createArgs]);
 assert.equal(f.calls[before+1][1],e);assert.deepEqual(f.calls[before+1][2],[finishArgs]);
 assert.equal(f.calls.at(-1)[2][0],list);assert.equal(f.api.status().schema,2);
 assert.equal(f.api.status().scope,'selected-texture-readonly-component-only');
 assert.equal(f.api.status().endFenceComplete,false);
 await f.api.endSelectedComponent(f.api.capability());assert.equal(f.api.status().endFenceComplete,true);
 assert.equal(f.api.close(f.api.capability()).phase,'CLOSED');assert.equal(f.api.status().gameplayAdmission,false);++intervalPassed;
}
{
 const f=fixture(),e=f.device.createCommandEncoder(),b=e.finish();f.issue();await f.api.armSelectedComponent(f.api.capability());
 refused(f,()=>f.queue.submit([b]),'PREISSUE_COMMAND_BUFFER');++intervalPassed;
}
{
 const f=fixture(),e=f.device.createCommandEncoder();f.issue();await f.api.armSelectedComponent(f.api.capability());
 refused(f,()=>e.finish(),'PREISSUE_OR_FOREIGN_ENCODER');++intervalPassed;
}
{
 const f=await selectedActive();refused(f,()=>f.queue.submit([{}]),'UNKNOWN_COMMAND_BUFFER');++intervalPassed;
}
{
 const f=await selectedActive(),e=new f.types.GPUCommandEncoder();refused(f,()=>e.finish(),'UNKNOWN_ENCODER');++intervalPassed;
}
{
 const f=await selectedActive(),other=new f.types.GPUDevice(new f.types.GPUQueue()),e=other.createCommandEncoder();
 refused(f,()=>e.finish(),'PREISSUE_OR_FOREIGN_ENCODER');++intervalPassed;
}
{
 const f=await selectedActive(),e=f.device.createCommandEncoder(),b=e.finish();
 refused(f,()=>f.queue.submit({[Symbol.iterator]:function*(){yield b;}}),'SUBMIT_ARGUMENT');++intervalPassed;
}
{
 const f=await selectedActive();refused(f,()=>f.api.close(f.api.capability()),'CLOSE_STATE');++intervalPassed;
}
{
 const f=fixture({deferred:true});f.issue();const start=f.api.armSelectedComponent(f.api.capability());
 assert.equal(f.api.status().phase,'COMPONENT_ARMING');
 refused(f,()=>f.queue.submit([]),'COMPONENT_QUEUE_PHASE');f.resolveFence();await assert.rejects(start,/COMPONENT_QUEUE_PHASE/);++intervalPassed;
}
{
 const f=await selectedActive();const ending=f.api.endSelectedComponent(f.api.capability());
 assert.equal(f.api.status().phase,'COMPONENT_ENDING');
 refused(f,()=>f.queue.submit([]),'COMPONENT_QUEUE_PHASE');await assert.rejects(ending,/COMPONENT_QUEUE_PHASE/);++intervalPassed;
}
{
 const f=await selectedActive();await f.api.endSelectedComponent(f.api.capability());
 refused(f,()=>f.queue.submit([]),'COMPONENT_QUEUE_PHASE');++intervalPassed;
}
{
 const f=fixture({optin:false});f.issue();await f.api.armSelectedComponent(f.api.capability());
 assert.equal(f.api.status().gameplayAdmission,false);await f.api.endSelectedComponent(f.api.capability());f.api.close(f.api.capability());++intervalPassed;
}
console.log(JSON.stringify({scope:'actual-library-selected-component-synthetic-controls',passed:intervalPassed,realGpu:false}));
