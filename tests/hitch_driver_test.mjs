import assert from 'node:assert/strict';
import {EventEmitter} from 'node:events';
import fs from 'node:fs/promises';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import {BUILD_ARTIFACTS,verifyServedArtifacts,stopTrace,remainingTimeout,TRACE,traceSettings,
  frozenTraceSettings,scheduleTraceEnd,finalizeTrace,pagePaintCondition,verifyPagePaintReport,
  planRole,stopAfterHoldoutFailure} from '../scripts/run_hitch_matrix.mjs';

assert.deepEqual(frozenTraceSettings({trace:TRACE}),traceSettings());
const gpu=traceSettings('gpu-startup');
const profile={trace:gpu.trace,trace_detail:gpu.detail,trace_window_ms:gpu.windowMs};
assert.equal(frozenTraceSettings(profile).windowMs,10000);
assert(gpu.trace.traceConfig.includedCategories.includes('disabled-by-default-gpu.dawn'));
assert(!TRACE.traceConfig.includedCategories.includes('disabled-by-default-gpu.dawn'));
assert.throws(()=>traceSettings('unknown'),/Unknown trace detail/);
assert.throws(()=>frozenTraceSettings(profile,'standard'),/differs from frozen/);
assert.throws(()=>frozenTraceSettings({...profile,trace_window_ms:20000}),/configuration changed/);
assert.throws(()=>frozenTraceSettings({...profile,trace:TRACE}),/configuration changed/);
assert.deepEqual(planRole({identities:{development_recipes:{a:{}}}}),
  {role:'development',recipes:'development_recipes'});
assert.deepEqual(planRole({role:'holdout',identities:{holdout_recipes:{a:{}}}}),
  {role:'holdout',recipes:'holdout_recipes'});
for(const plan of [null,{},
  {role:null,identities:{development_recipes:{a:{}}}},
  {role:'holdout',identities:{development_recipes:{a:{}}}},
  {role:'development',identities:{holdout_recipes:{a:{}}}},
  {role:'holdout',identities:{holdout_recipes:[]}},
  {role:'other',identities:{development_recipes:{a:{}}}}])
  assert.throws(()=>planRole(plan),/Unsupported|Missing/);
assert.equal(stopAfterHoldoutFailure('holdout',{status:'completed',validation:{valid:true}}),false);
for(const finished of [null,{status:'completed'},
  {status:'completed',validation:{valid:false}},
  {status:'aborted',validation:{valid:true}}]) {
  assert.equal(stopAfterHoldoutFailure('holdout',finished),true);
  assert.equal(stopAfterHoldoutFailure('development',finished),false);
}
assert.equal(pagePaintCondition({mode:'profiler'}),'normal');
assert.throws(()=>pagePaintCondition({mode:'unprofiled',page_paint:'hidden'}),/diagnostic only/);
assert.throws(()=>pagePaintCondition({mode:'profiler',page_paint:'unknown'}),/Unknown/);
verifyPagePaintReport({}, {mode:'profiler'}); // Old normal-page report.
const geometry={x:12,y:-200,width:900,height:675,buffer_width:1280,buffer_height:960,dpr:2};
const paintReport={diagnostic_page_paint:{mode:'hidden',diagnostic_only:true,restored:true,
  started_ms:100,ended_ms:200,geometry_before:geometry,geometry_after:{...geometry}}};
const paintSlot={mode:'profiler',page_paint:'hidden'};
verifyPagePaintReport(paintReport,paintSlot);
assert.throws(()=>verifyPagePaintReport({},paintSlot),/disagrees/);
assert.throws(()=>verifyPagePaintReport(paintReport,{mode:'profiler',page_paint:'normal'}),/disagrees/);
for(const patch of [{restored:false},{ended_ms:90},{geometry_after:{...geometry,width:800}},
  {geometry_before:{},geometry_after:{}},
  {geometry_before:{...geometry,buffer_width:640,buffer_height:480},geometry_after:{...geometry,buffer_width:640,buffer_height:480}}]){
  assert.throws(()=>verifyPagePaintReport({diagnostic_page_paint:{...paintReport.diagnostic_page_paint,...patch}},paintSlot));
}

const bytes=Buffer.from('frozen executable');
const digest=crypto.createHash('sha256').update(bytes).digest('hex');
let served=bytes;
let missing=false;
const server=http.createServer((req,res)=>{
  if(missing||!BUILD_ARTIFACTS.some(name=>req.url==='/'+name)){res.writeHead(404);res.end();return;}
  res.writeHead(200);res.end(served);
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
try {
  const machine={url:`http://127.0.0.1:${server.address().port}/runtime.html`,
    artifacts:Object.fromEntries(BUILD_ARTIFACTS.map(name=>[name,digest]))};
  assert.deepEqual(await verifyServedArtifacts(machine),
    Object.fromEntries(BUILD_ARTIFACTS.map(name=>[name,{sha256:digest,bytes:bytes.length}])));
  served=Buffer.from('different executable');
  await assert.rejects(verifyServedArtifacts(machine),/Served artifact changed/);
  missing=true;
  await assert.rejects(verifyServedArtifacts(machine),/HTTP 404/);
  await assert.rejects(verifyServedArtifacts(machine,Date.now()-1),/wall-time bound/);
  assert.throws(()=>remainingTimeout(Date.now()-1),/wall-time bound/);
  assert.equal(remainingTimeout(Date.now()+10000,500),500);
} finally {await new Promise(resolve=>server.close(resolve));}

class TraceSession extends EventEmitter {
  constructor(chunks,loss=false,error=false){super();this.chunks=chunks;this.loss=loss;this.error=error;this.closed=false;this.calls=[];}
  async send(method){
    this.calls.push(method);
    if(method==='Tracing.end') {queueMicrotask(()=>this.emit('Tracing.tracingComplete',{stream:'1',dataLossOccurred:this.loss}));return {};}
    if(method==='IO.close'){this.closed=true;return {};}
    if(method==='IO.read'){
      if(this.error)throw Error('stream failed');
      const data=this.chunks.shift()||'';return {data,eof:this.chunks.length===0};
    }
    throw Error('unexpected '+method);
  }
}
const root=await fs.mkdtemp(path.join(os.tmpdir(),'melee-hitch-driver-'));
try {
  // Deadline and early source stop both end once; neither consumes the stream
  // until the completed replay's teardown explicitly writes the artifact.
  for(const early of [false,true]) {
    const session=new TraceSession(['{}'],!early);
    let fire,cancelled=false;
    const window=scheduleTraceEnd(session,10000,callback=>{fire=callback;return 42;},id=>{
      assert.equal(id,42);cancelled=true;
    });
    if(!early)fire();
    const ended=await window.finish();
    fire(); // A late timer callback must reuse the in-progress/completed end.
    await window.finish();
    assert(cancelled);
    assert.deepEqual(session.calls,['Tracing.end']);
    assert.equal(ended.window.end_reason,early?'replay-stopped':'window-deadline');
    const directory=path.join(root,early?'early-window':'deadline-window');await fs.mkdir(directory);
    const result=await stopTrace(session,directory,1024,{...ended,configuration:gpu.trace});
    assert.equal(session.calls.filter(x=>x==='Tracing.end').length,1);
    assert.equal(result.complete,early); // Loss still fails a short-window trace.
    const metadata=JSON.parse(await fs.readFile(result.paths[1],'utf8'));
    assert.deepEqual(metadata.window,ended.window);
    assert.deepEqual(metadata.configuration,gpu.trace);
  }
  const failure=new TraceSession([]);
  failure.send=async()=>{throw Error('GPU process disconnected');};
  const failedWindow=scheduleTraceEnd(failure,10000,()=>42,()=>{});
  assert.match((await failedWindow.finish()).error,/GPU process disconnected/);
  assert.equal(failure.listenerCount('Tracing.tracingComplete'),0);
  const failedDirectory=path.join(root,'end-failure');await fs.mkdir(failedDirectory);
  const failed=await finalizeTrace(failure,failedDirectory,gpu,failedWindow);
  assert.equal(failed.complete,false);
  assert.equal(failed.reusable,false);
  assert.match(failed.error,/GPU process disconnected/);
  assert.equal(failed.paths.length,2);
  const failureRecord=JSON.parse(await fs.readFile(failed.paths[1],'utf8'));
  assert.equal(failureRecord.complete,false);
  assert.equal(failureRecord.trace_state_unknown,true);
  for(const [name,session,limit,expectedBytes,complete] of [
    ['complete',new TraceSession(['abc','def']),8,6,true],
    ['capped',new TraceSession(['abc','def']),4,3,false],
    ['lost',new TraceSession(['abc'],true),8,3,false],
    ['read-error',new TraceSession([],false,true),8,0,false],
  ]) {
    const directory=path.join(root,name);await fs.mkdir(directory);
    const result=await stopTrace(session,directory,limit);
    assert.equal(result.complete,complete);
    assert.equal(session.closed,true);
    const metadata=JSON.parse(await fs.readFile(result.paths[1],'utf8'));
    assert.equal(metadata.bytes,expectedBytes);
    assert.equal((await fs.stat(result.paths[0])).size,expectedBytes);
    assert.equal(metadata.complete,complete);
    if(name==='capped')assert.equal(metadata.bytes_read,6);
    // Repeated finalization cannot replace the first preserved trace.
    await assert.rejects(stopTrace(new TraceSession(['replacement']),directory),/EEXIST/);
  }
} finally {await fs.rm(root,{recursive:true,force:true});}
console.log('Hitch driver served identity and bounded trace checks passed');
