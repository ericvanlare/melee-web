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
  class GPUQueue {writeTexture(...a){calls.push(['write',this,a]);return 37;} copyExternalImageToTexture(...a){calls.push(['external',this,a]);} onSubmittedWorkDone(){calls.push(['fence',this,[]]);return fence;}}
  class GPUCommandEncoder {copyBufferToTexture(...a){calls.push(['buffer',this,a]);} copyTextureToTexture(...a){calls.push(['texture',this,a]);}}
  class GPUTexture {constructor(){this.usage=6;}destroy(...a){calls.push(['destroy',this,a]);}}
  class GPUTextureView {}
  class GPUDevice {constructor(q){this.queue=q;this.lost=new Promise(r=>{resolveLost=r;});} destroy(...a){calls.push(['device-destroy',this,a]);}}
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
async function active(options) {const f=fixture(options);assert.equal(f.issue(),1);await f.api.armFixture(f.api.capability());return f;}
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
