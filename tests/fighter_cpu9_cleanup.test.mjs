import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const source=fs.readFileSync(new URL('./fighter_cpu9_lineup_browser_test.mjs',import.meta.url),'utf8');
const begin=source.indexOf('async function closeOwnedBrowserResources(){');
const end=source.indexOf('\n}\n',begin)+3;
const actualClose=source.slice(begin,end);
for(const failed of [null,'context','browser'])test(`actual owned cleanup independently retires context/browser: ${failed}`,async()=>{
 const calls=[],primary={message:'original route failure'};
 const resource=name=>({close:async()=>{calls.push(name);if(name===failed)throw Error(name+' refused');}});
 const scope={report:{result:'pass',failure:primary},process:{exitCode:0,off(){}},ownedClosePromise:null,driver:null,browserCdp:null,
  browserContext:resource('context'),browser:resource('browser')};
 vm.runInNewContext(actualClose+';globalThis.closeActual=closeOwnedBrowserResources;',scope);
 await scope.closeActual();assert.deepEqual(calls,['context','browser']);assert.equal(scope.report.failure,primary);
 assert.equal(scope.report.cleanup.context.status,failed==='context'?'failed':'closed');
 assert.equal(scope.report.cleanup.browser.status,failed==='browser'?'failed':'closed');
 if(failed){assert.equal(scope.report.result,'fail');assert.equal(scope.process.exitCode,1);}
});
test('actual finally preserves primary evidence error and closes both before report write',async()=>{
 const start=source.indexOf('}finally{\n  try{')+'}finally{'.length;
 const finish=source.indexOf("\nif(!['pass'",start);
 const body=source.slice(start,finish).trim().slice(0,-1);
 for(const withPrimary of [false,true]){
  const calls=[],primary=withPrimary?{message:'original route error'}:null;
  const scope={report:{result:'pass',...(primary?{failure:primary}:{})},process:{exitCode:0,off(){}},ownedClosePromise:null,
   stopControlledContention:async()=>{throw Error('evidence unavailable');},driver:null,browserCdp:null,
   browserContext:{close:async()=>calls.push('context')},browser:{close:async()=>calls.push('browser')},
   onSigint(){},onSigterm(){},output:'/owned',path:{join:()=>'/owned/report.json'},fs:{writeFile:async()=>calls.push('write')}};
  vm.runInNewContext(actualClose+';globalThis.finishActual=async()=>{'+body+'};',scope);
  await scope.finishActual();assert.deepEqual(calls,['context','browser','write']);
  assert.equal(scope.report.result,'fail');assert.equal(scope.report.final_evidence_error,'evidence unavailable');
  if(primary)assert.equal(scope.report.failure,primary);else assert.match(scope.report.failure.message,/Final evidence collection/);
 }
});

test('actual signal cleanup preserves interruption and only closes each owner once',async()=>{
 const calls=[],handlers={};
 const stop=source.indexOf("process.on('SIGINT',onSigint);process.on('SIGTERM',onSigterm);",begin);
 const signalCode=source.slice(begin,stop+"process.on('SIGINT',onSigint);process.on('SIGTERM',onSigterm);".length);
 const scope={report:{result:'pass'},process:{exitCode:0,on(name,fn){handlers[name]=fn;}},ownedClosePromise:null,
 driver:null,browserCdp:null,browserContext:{close:async()=>{calls.push('context');throw Error('context close failed');}},
 browser:{close:async()=>calls.push('browser')}};
 vm.runInNewContext(signalCode+';globalThis.closeActual=closeOwnedBrowserResources;',scope);
 handlers.SIGINT();await scope.closeActual();handlers.SIGTERM();await scope.closeActual();
 assert.deepEqual(calls,['context','browser']);assert.equal(scope.report.interruption.signal,'SIGINT');
 assert.equal(scope.report.failure.code,'owned_interruption');assert.match(scope.report.failure.message,/SIGINT/);
 assert.equal(scope.report.cleanup.context.status,'failed');assert.equal(scope.report.cleanup.browser.status,'closed');
});

test('actual finally independently closes owners after retained-input or capture rejection',async()=>{
 const start=source.indexOf('}finally{\n  try{')+'}finally{'.length;
 const finish=source.indexOf("\nif(!['pass'",start);
 const body=source.slice(start,finish).trim().slice(0,-1);
 for(const rejected of ['retainResultsInputEvents','retainRuntimeDiagnosticsCapture','sourceProvenance']){
  const calls=[],primary={message:'first route error'};
  const scope={report:{result:'fail',failure:primary,controller_inputs:[],provenance:{}},
   process:{exitCode:1,off(){}},ownedClosePromise:null,onSigint(){},onSigterm(){},
   stopControlledContention:async()=>{},campaignWallBoundTimer:null,wallBoundTask:null,campaignWallBoundExceeded:false,
   page:{isClosed:()=>false},activeMatchIndex:null,
   retainResultsInputEvents:async()=>{if(rejected==='retainResultsInputEvents')throw Error(rejected);},
   retainRuntimeDiagnosticsCapture:async()=>{if(rejected==='retainRuntimeDiagnosticsCapture')throw Error(rejected);},
   diagnostic:async()=>({}),artifactReads:[],sourceProvenance:()=>{throw Error('sourceProvenance');},
   driver:{dispose:()=>calls.push('driver')},browserCdp:{detach:async()=>calls.push('cdp')},
   browserContext:{close:async()=>calls.push('context')},browser:{close:async()=>calls.push('browser')},
   output:'/owned',path:{join:()=>'/owned/report.json'},fs:{writeFile:async()=>calls.push('write')}};
  vm.runInNewContext(actualClose+';globalThis.finishActual=async()=>{'+body+'};',scope);
  await scope.finishActual();assert.deepEqual(calls,['driver','cdp','context','browser','write']);
  assert.equal(scope.report.failure,primary);assert.equal(scope.report.final_evidence_error,rejected);
 }
});
