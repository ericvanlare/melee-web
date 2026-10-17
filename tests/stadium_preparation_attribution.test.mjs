import assert from 'node:assert/strict';
import test from 'node:test';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const source = readFileSync(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
function between(start,end) {
  const a=source.indexOf(start),b=source.indexOf(end,a+start.length);
  assert.ok(a>=0&&b>a,`Actual source delimiters missing: ${start}`);
  return source.slice(a,b);
}
const helper = between('const recreationPreparationAttributionKey =', '\nasync function snapshotRecreationCheckpointFiles');
const tail = between('try {\n  await main();\n}', '\nif (failure || report.cleanup.failures.length');
function fixture() {
  const calls=[];let now=10;let nativeReads=0;
  const original=function(...args){calls.push({receiver:this,args});if(args[0]?.throwOriginal)throw Error('original-failure');return 'original-return';};
  const browser=vm.createContext({performance:{now:()=>++now,getEntriesByType:()=>[]},
    Module:new Proxy({graphicsStartup:{renderer_init_ms:12,queued_pipelines:0,created_pipelines:890,cache_idle:1,terminal:true}},
      {get(target,key){if(String(key).startsWith('_')){++nativeReads;throw Error('Native read forbidden');}return target[key];}})});
  Object.defineProperty(browser,'menuConstructionTiming',{value:original,writable:true,enumerable:true,configurable:true});
  let handleDisposed=0;
  // Explicit transport/realm doubles only. The installed callback, capsule and operation functions are extracted actual candidate source.
  const page={async evaluateHandle(fn,arg){const api=vm.runInContext(`(${fn.toString()})`,browser)(arg);return {
    async evaluate(operation,args){return vm.runInContext(`(${operation.toString()})`,browser)(api,args);},
    async dispose(){++handleDisposed;}};}};
  const report={result:'running',execution_state:'running',scenario:{},cleanup:{failures:[]}};
  const node=vm.createContext({page,assert,report,remaining:()=>1000,timeout:async promise=>promise});
  vm.runInContext(helper,node);
  return {browser,node,calls,report,original,install:()=>node.installRecreationPreparationAttribution(),
    op:(operation,role)=>node.observeRecreationPreparation(operation,role),
    record:role=>node.recordRecreationPreparationAttribution(role,{import_total_ms:40,select_disc_ms:1}),
    emit:(data)=>browser.menuConstructionTiming.call(browser,data,'extra'),
    nativeReads:()=>nativeReads,handleDisposed:()=>handleDisposed};
}
const row=()=>({kind:'scene-prepare',total_ms:30,construct_ms:30,entry_ms:0,queued_delta:0,created_delta:0,wasm_heap_bytes:134217728});
test('actual observer preserves callback semantics, separates A/B and restores exact ownership',async()=>{
 const f=fixture(),before=Object.getOwnPropertyDescriptor(f.browser,'menuConstructionTiming');await f.install();
 await f.op('begin','A');assert.equal(f.emit(row()),'original-return');assert.equal(f.calls[0].receiver,f.browser);assert.equal(f.calls[0].args[1],'extra');
 const a=await f.record('A');assert.equal(a.role,'A');assert.equal(a.construction.length,1);assert.equal(f.report.scenario.preparation_attribution.role,'A');
 await f.op('begin','B');f.emit(row());const b=await f.record('B');assert.equal(b.construction.length,1);assert.equal(b.construction[0].role,'B');
 assert.equal(await f.op('dispose',null),true);assert.deepEqual(Object.getOwnPropertyDescriptor(f.browser,'menuConstructionTiming'),before);assert.equal(f.handleDisposed(),1);assert.equal(f.nativeReads(),0);
 const g=fixture();await g.install();await g.op('begin','A');assert.throws(()=>g.emit({...row(),throwOriginal:true}),/original-failure/);assert.equal(g.calls.length,1);await g.record('A');await g.op('dispose',null);
});
test('actual observer refuses foreign capsule/callback descriptors and reordered or active roles',async()=>{
 const f=fixture();await f.install();await assert.rejects(()=>f.op('begin','B'),/invalid or repeated/);await f.op('begin','A');
 await assert.rejects(()=>f.op('begin','A'),/invalid or repeated/);await assert.rejects(()=>f.op('begin','B'),/invalid or repeated/);await assert.rejects(()=>f.op('finish','B'),/out of order/);
 f.emit(row());await f.record('A');await assert.rejects(()=>f.op('begin','A'),/invalid or repeated/);await f.op('dispose',null);
 for(const mode of ['capsule-value','capsule-descriptor','callback']){
  const g=fixture();await g.install();const key='__meleeStadiumPreparationAttributionV1',old=g.browser[key];
  if(mode==='capsule-value')Object.defineProperty(g.browser,key,{value:{begin(){return true;}},configurable:true});
  if(mode==='capsule-descriptor')Object.defineProperty(g.browser,key,{value:old,writable:true,configurable:true});
  if(mode==='callback')g.browser.menuConstructionTiming=()=>{};
  await assert.rejects(()=>g.op('begin','A'),/identity\/operation|owner changed/);
  assert.throws(()=>old.begin('A'),/owner changed/);await assert.rejects(()=>g.op('dispose',null),/identity\/operation|owner changed/);
 }
});
test('actual failure publication retains missing/nonfinite/overflow attribution before assertions',async()=>{
 for(const mode of ['missing','nonfinite','overflow']){
  const f=fixture();await f.install();await f.op('begin','A');
  if(mode==='nonfinite')f.emit({...row(),construct_ms:NaN});if(mode==='overflow')for(let i=0;i<5;i++)f.emit(row());
  let persisted=null;
  Object.assign(f.node,{main:()=>f.record('A'),failure:null,errorText:error=>String(error),
    cleanupOwnedProcesses:async()=>{},matchedSetupCfTiming:null,pageErrors:[],consoleErrors:[],consoleWarnings:[],consoleMessages:[],
    recreationCanary:true,recreationEventSnapshot:()=>({}),verifyRecreationArchiveAfterFinalCleanup:async()=>{},
    persistReport:async()=>{persisted=JSON.parse(JSON.stringify(f.report));},persistRecreationSummary:async()=>{}});
  // Actual outer catch/finally; endpoint cleanup/persistence doubles only test publication ordering, not real native/process cleanup.
  await vm.runInContext(`(async()=>{${tail}})()`,f.node);
  assert.equal(persisted.result,'fail');assert.ok(persisted.error);assert.equal(persisted.execution_state,'completed');
  assert.equal(persisted.scenario.preparation_attribution.role,'A');
  if(mode==='missing')assert.equal(persisted.scenario.preparation_attribution.construction.length,0);
  if(mode==='nonfinite')assert.equal(persisted.scenario.preparation_attribution.construction[0].construct_ms,null);
  if(mode==='overflow')assert.equal(persisted.scenario.preparation_attribution.overflow,true);
 }
});
