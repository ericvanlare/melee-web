// Component controls only. Native, shell callback leaves and driver result leaves
// below are explicitly modeled; no compiled game, source action or close is run.
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';
import {evaluateMatchedSetupCfNativeCommand, createLaterBGate2NativeAdapter,
  runLaterBGate2ActionContinuation} from './stadium_matched_setup_cf_runner_gate.mjs';
const root = new URL('../', import.meta.url);
const captureSource = await fs.readFile(new URL('scripts/capture_whole_session_browser.mjs',root),'utf8');
const scopeSource = await fs.readFile(new URL('scripts/stadium_current_sss_exit_observation.mjs',root),'utf8');
const tokenId='123e4567-e89b-42d3-a456-426614174000';
function cut(text,start,end){assert.equal(text.split(start).length,2,start);assert.equal(text.split(end).length,2,end);return text.slice(text.indexOf(start),text.indexOf(end));}
function nativeFixture(){
  const calls=[];let idle=true,statusPointer=1,retireResult=1;
  const receipt={schema:'melee-web-stadium-later-b-gate2-native-v1',armed:false,failed:true,complete:false,
    state:'failed',error:'first native service error',source_ticks:0,source_draws:0,cursor:0,
    queued:false,awaiting_approval:false,last_action:null};
  let retirementStatus={schema:'melee-web-stadium-matched-setup-cf-retirement-v1',native_retired:false,
    terminal:true,error:'controlled native close refusal'};
  const nativeMessage='controlled native retirement message';
  const owner={gate2CanSettle:()=>true,gate2Pending:kind=>calls.push(['pending',kind]),gate2Settled:()=>{assert.equal(idle,true);calls.push(['settled']);},
    gate2Uncertain:reason=>calls.push(['uncertain',reason]),gate2ApproveSetup:()=>{calls.push(['setup-vote']);return true;}};
  const module={HEAPU8:new Uint8Array(30000),stadiumMatchedSetupNativeIdle:()=>idle,
    _malloc:size=>{calls.push(['malloc',size]);return 8;},_free:p=>calls.push(['free',p]),
    _melee_web_native_menu_stadium_first_sss_vs_setup_cf_full_explicit_arm:()=>{
      assert.equal(globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved(),true);calls.push(['arm']);return 1;},
    _melee_web_native_menu_stadium_first_sss_vs_setup_cf_v2_queue:(...args)=>{calls.push(['queue',...args]);return 1;},
    _melee_web_native_menu_stadium_first_sss_vs_setup_cf_approve:()=>{calls.push(['approve']);return 1;},
    _melee_web_native_menu_stadium_first_sss_vs_setup_cf_retirement_status:()=>{calls.push(['live']);return statusPointer;},
    _melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire_ready:()=>{calls.push(['ready']);return 1;},
    _melee_web_native_menu_message:()=>{calls.push(['message']);return 3;},
    UTF8ToString:pointer=>pointer===2?JSON.stringify(retirementStatus):pointer===3?nativeMessage:JSON.stringify(receipt),
    ccall:(name,_type,_types,_args,options)=>{assert.equal(options.async,true);calls.push(['ccall',name]);
      return Promise.resolve(name==='melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire'?retireResult:1)
        .then(result=>{if(name==='melee_web_native_menu_stadium_first_sss_vs_setup_cf_retire'&&result===0)
          statusPointer=retirementStatus.native_retired===true?2:1;return result;});}};
  for(const key of ['_melee_web_native_menu_running','_melee_web_native_menu_phase',
    '_melee_web_native_menu_stadium_current_sss_exit_observe','saveRuntimeCache'])
    module[key]=()=>{throw Error(`Forbidden ordinary native access ${key}`);};
  return {calls,owner,module,receipt,retirementStatus,setIdle:value=>{idle=value;},
    setRetirementStatus:value=>{retirementStatus=value;
      Object.assign(receipt,{state:'complete',armed:true,failed:false,complete:true,error:null,cursor:360,
        source_ticks:180,source_draws:180,queued:false,awaiting_approval:false,last_action:{complete:true}});},
    setRetireResult:value=>{retireResult=value;}};
}
async function withModule(module,body){const before=globalThis.Module;globalThis.Module=module;try{return await body();}finally{globalThis.Module=before;}}

test('shared evaluator retains legacy dispatch without a Gate2 token',async()=>{
  await withModule({_melee_web_native_menu_stadium_first_sss_vs_setup_cf_arm:()=>1},()=>{
    assert.deepEqual(evaluateMatchedSetupCfNativeCommand({kind:'arm'}),{result:1,message:null});
  });
  const c1a=await fs.readFile(new URL('tests/stadium_c1a_browser_test.mjs',root),'utf8');
  const body=cut(c1a,'async function executeMatchedSetupCfNativeCommand(command) {','\nfunction matchedSetupCfNativeAdapter(');
  assert.ok(body.indexOf('dispatchMatchedSetupCfNativeEvaluation')<body.indexOf('page.evaluate(evaluateMatchedSetupCfNativeCommand, command)'));
  assert.doesNotMatch(body,/module\._melee_web_native_menu_/);
});

test('actual currentSSS wrapper reserves once and closes all ordinary getters through JS-only finalization',async()=>{
  const calls=[];let moved=false;
  const actual={reference_descriptor:'original-b-2013-943-v1',notification_coordinates:{order:1893,replay_cursor:943},
    prepared_to_setup:{setup:{complete:true,captured:true,stadium_setup_state:{world_generation:7},actual:{random_seed_hex:'4b25c79b'}}}};
  const prepared={captured:true,control:'modeled preparedStart receipt'};
  const ctx={Uint8Array,console};vm.createContext(ctx);ctx.window=ctx;
  const m={HEAPU8:new Uint8Array(500),stadiumMatchedSetupNativeIdle:()=>true,
    _melee_web_native_menu_replay:()=>1,_melee_web_native_menu_stadium_current_sss_exit_arm_profile:()=>1,
    _melee_web_native_menu_stadium_current_sss_exit_observe:()=>{assert.equal(moved,false,'ordinary getter after reservation');return 1;},
    _melee_web_native_menu_stadium_current_sss_prepared_start_observe:()=>{assert.equal(moved,false);return 2;},
    _melee_web_native_menu_stadium_current_sss_prepared_setup_begin:()=>ctx.__meleeWebStadiumCurrentSssPreparedStartApproved()?1:0,
    UTF8ToString:p=>JSON.stringify(p===1?actual:prepared)};ctx.Module=m;
  const token=Object.freeze({id:tokenId});
  ctx.menuReplayReserveLaterBTransfer=()=>{calls.push('reserve');moved=true;return token;};
  for(const name of ['Pending','Settled','Uncertain'])ctx[`menuReplayLaterBCommand${name}`]=(t)=>{assert.equal(t,token);calls.push(name);};
  ctx.menuReplayReadLaterBTransferEvidence=t=>{assert.equal(t,token);return {scope:'synthetic shell leaf',core:{phase:'pending'},uncertain:false};};
  ctx.menuReplayRecordLaterBAction=t=>assert.equal(t,token);
  ctx.menuReplayFinalizeLaterBTransfer=async t=>{assert.equal(t,token);calls.push('finalize');ctx.lastRetailReplayReport={complete:false};};
  const source=cut(scopeSource,'export function installCurrentSssExitReplayArm(', '\n\nexport function validateCurrentSssExitExpected');
  const install=vm.runInContext('('+source.replace('export ','')+')',ctx);
  const scope=install({recipeBytes:Array(328).fill(0),observationProfile:3,preparedStartProbe:true,laterPreparedSetup:true});
  assert.equal(m._melee_web_native_menu_replay(8,328,1),1);
  scope.continuePreparedStart(prepared);
  const request={reference_id:'original-b-2013-943-v1',recipe_sha256:'5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755',
    cursor:943,notification_order:1893,world_generation:7,setup_seed_hex:'4b25c79b'};
  assert.equal(scope.reserveLaterBTransfer(actual,request).id,tokenId);
  for(const call of [()=>scope.observe(),()=>scope.observePreparedStart(),()=>scope.continuePreparedStart(prepared),
    ()=>scope.reserveLaterBTransfer(actual,request),()=>scope.dispose()])assert.throws(call,/Gate2|one-use/);
  scope.gate2Pending('arm');assert.equal(scope.gate2ApproveSetup(),true);assert.equal(scope.gate2ApproveSetup(),false);scope.gate2Settled();
  const original=ctx.Module;ctx.Module={};assert.throws(()=>scope.gate2Pending('action'),/foreign/);ctx.Module=original;
  assert.equal((await scope.gate2Finalize()).finalized,true);
  assert.throws(()=>scope.gate2Pending('action'),/finalized/);
  assert.equal(scope.dispose(),true);assert.equal(calls.filter(x=>x==='reserve').length,1);
});

test('actual Gate2 page commands keep full transactions pending and never read native after close',async()=>{
  const f=nativeFixture();await withModule(f.module,async()=>{
    const bytes=Array(20580).fill(0);bytes.splice(0,4,77,87,66,50);
    assert.equal((await evaluateMatchedSetupCfNativeCommand({kind:'gate2-arm',manifestBytes:bytes,gate2Owner:f.owner})).result,1);
    assert.ok(f.calls.findIndex(x=>x[0]==='free')<f.calls.findIndex(x=>x[0]==='settled'));
    assert.equal(globalThis.__meleeWebStadiumFirstSssVsSetupCfApproved,undefined);
    await evaluateMatchedSetupCfNativeCommand({kind:'gate2-queue',scheduleIndex:3,action:'draw',ordinal:1,bundle:null,gate2Owner:f.owner});
    assert.deepEqual(f.calls.find(x=>x[0]==='queue'),['queue',3,2,1,-1]);
    const liveBefore=f.calls.filter(x=>x[0]==='live').length;
    assert.equal((await evaluateMatchedSetupCfNativeCommand({kind:'gate2-retire',gate2Owner:f.owner})).result,1);
    assert.equal(f.calls.filter(x=>x[0]==='live').length,liveBefore,'successful retirement has no post-close getter');
    assert.equal(f.calls.filter(x=>x[0]==='message').length,0,'successful retirement needs no failure message read');
    f.receipt.source_ticks=1;
    const ccallBefore=f.calls.filter(x=>x[0]==='ccall').length;
    assert.equal((await evaluateMatchedSetupCfNativeCommand({kind:'gate2-abort',gate2Owner:f.owner})).asyncify_idle,false);
    assert.equal(f.calls.filter(x=>x[0]==='ccall').length,ccallBefore,'nonzero-action receipt forbids unload');
    f.receipt.source_ticks=0;
    assert.equal((await evaluateMatchedSetupCfNativeCommand({kind:'gate2-abort',gate2Owner:f.owner})).result,1);
    assert.equal(f.calls.at(-2)[0],'ccall');assert.equal(f.calls.at(-1)[0],'settled');
    for (const rejection of [false,true]) {
      let resolve,reject,canSettle=true;
      f.owner.gate2CanSettle=()=>canSettle;
      f.module.ccall=()=>new Promise((yes,no)=>{resolve=yes;reject=no;});
      const late=evaluateMatchedSetupCfNativeCommand({kind:'gate2-step',gate2Owner:f.owner});
      canSettle=false; // Labeled model of the shell's sticky uncertainty latch.
      const getters=f.calls.filter(x=>x[0]==='live').length;
      if(rejection)reject(Error('controlled late rejection'));else resolve(1);
      assert.equal((await late).asyncify_idle,false);
      assert.equal(f.calls.filter(x=>x[0]==='live').length,getters);
    }
    f.owner.gate2CanSettle=()=>true;
    f.setIdle(false);
    const nativeCalls=f.calls.filter(x=>['malloc','live','ccall','queue','approve','ready'].includes(x[0])).length;
    assert.equal((await evaluateMatchedSetupCfNativeCommand({kind:'gate2-live',gate2Owner:f.owner})).asyncify_idle,false);
    assert.equal(f.calls.filter(x=>['malloc','live','ccall','queue','approve','ready'].includes(x[0])).length,nativeCalls);
  });
});

test('settled zero Gate2 step retains its native failure while pending or uncertain outcomes read nothing',async()=>{
  const f=nativeFixture();
  await withModule(f.module,async()=>{
    f.module.ccall=()=>Promise.resolve(0);
    const response=await evaluateMatchedSetupCfNativeCommand({kind:'gate2-step',gate2Owner:f.owner});
    assert.equal(response.result,0);assert.equal(response.asyncify_idle,true);
    assert.equal(response.failure.classification,'settled-native-step-failure');
    assert.equal(response.failure.message,'first native service error');
    assert.equal(response.failure.observation.error,'first native service error');
    assert.ok(f.calls.findIndex(x=>x[0]==='settled')<f.calls.findIndex(x=>x[0]==='live'));

    for (const mode of ['owner-uncertain','asyncify-pending']) {
      let resolve;let canSettle=true;f.owner.gate2CanSettle=()=>canSettle;
      f.module.ccall=()=>new Promise(r=>{resolve=r;});
      const before=f.calls.filter(x=>x[0]==='live').length;
      const pending=evaluateMatchedSetupCfNativeCommand({kind:'gate2-step',gate2Owner:f.owner});
      if(mode==='owner-uncertain')canSettle=false;else f.setIdle(false);
      resolve(0);
      const refused=await pending;
      assert.equal(refused.asyncify_idle,false);
      assert.equal(f.calls.filter(x=>x[0]==='live').length,before,
        'pending or uncertain settlement must not read retained native state');
      if(mode==='asyncify-pending')f.setIdle(true);
    }
  });
});

test('settled zero Gate2 retirement retains native message and status; uncertain outcomes perform no getters',async()=>{
  for (const nativeRetired of [false,true]) {
    const f=nativeFixture();
    f.setRetireResult(0);
    f.setRetirementStatus({schema:'melee-web-stadium-matched-setup-cf-retirement-v1',
      native_retired:nativeRetired,terminal:true,error:nativeRetired
        ?'Matched Setup C/F runtime retirement completed but runtime notification was refused'
        :'Stadium B terminal retirement lost its admitted owner or RNG boundary'});
    await withModule(f.module,async()=>{
      const response=await evaluateMatchedSetupCfNativeCommand({kind:'gate2-retire',gate2Owner:f.owner});
      assert.equal(response.result,0);assert.equal(response.asyncify_idle,true);
      assert.equal(response.failure.classification,'settled-native-retire-failure');
      assert.equal(response.failure.message,'controlled native retirement message');
      assert.equal(response.failure.native_retired,nativeRetired);
      if(nativeRetired) {
        assert.equal(response.failure.native_status.schema,'melee-web-stadium-matched-setup-cf-retirement-v1');
        assert.equal(response.failure.native_status.error,'Matched Setup C/F runtime retirement completed but runtime notification was refused');
      } else {
        assert.equal(response.failure.native_status.schema,'melee-web-stadium-later-b-gate2-native-v1');
        assert.equal(response.failure.native_status.complete,true);
        assert.equal(response.failure.native_status.error,null);
      }
      const calls=f.calls.map(x=>x[0]);
      assert.ok(calls.indexOf('ccall')<calls.indexOf('settled'));
      assert.ok(calls.indexOf('settled')<calls.indexOf('live'));
      assert.ok(calls.indexOf('live')<calls.indexOf('message'));
    });
  }

  const f=nativeFixture();f.setRetireResult(0);
  const adapter=createLaterBGate2NativeAdapter({owner:f.owner,deadline:100,now:()=>0,
    evaluate:(_fn,command)=>evaluateMatchedSetupCfNativeCommand(command)});
  await withModule(f.module,async()=>{
    await adapter.arm(new Uint8Array(20580));
    await assert.rejects(adapter.retire(),/Gate2 settled native retirement refusal: controlled native retirement message/);
    const failure=adapter.state().settled_retirement_failure;
    assert.equal(failure.classification,'settled-native-retire-failure');
    assert.equal(failure.native_retired,false);
    assert.equal(adapter.state().unsafe,false);
    assert.equal(adapter.state().closed,false,'a zero result is not native-close acknowledgement');
    const before=f.calls.length;
    await assert.rejects(adapter.observe(),/failed/);
    await assert.rejects(adapter.abortUnarmed(),/definite/);
    assert.equal(f.calls.length,before,'settled retirement failure cannot trigger another native command');
  });

  for (const mode of ['owner-uncertain','asyncify-pending']) {
    const f=nativeFixture();f.setRetireResult(0);
    if (mode==='owner-uncertain') f.owner.gate2CanSettle=()=>false;
    else f.module.ccall=(name,_type,_types,_args,options)=>{
      assert.equal(options.async,true);f.calls.push(['ccall',name]);
      return Promise.resolve(0).then(value=>{f.setIdle(false);return value;});
    };
    await withModule(f.module,async()=>{
      const before=f.calls.filter(x=>['live','message'].includes(x[0])).length;
      const response=await evaluateMatchedSetupCfNativeCommand({kind:'gate2-retire',gate2Owner:f.owner});
      assert.equal(response.asyncify_idle,false);
      assert.equal(f.calls.filter(x=>['live','message'].includes(x[0])).length,before,
        `${mode} must not read retained native state`);
    });
  }
});

test('host transport is one-use, serial, sticky on lost responses and refuses negative ACK before native dispatch',async()=>{
  let resolve;const commands=[];
  const adapter=createLaterBGate2NativeAdapter({owner:{},deadline:100,now:()=>0,
    evaluate:(_fn,c)=>{commands.push(c);return c.kind==='gate2-arm'?new Promise(r=>{resolve=r;}):Promise.resolve({result:1,asyncify_idle:true});}});
  const arm=adapter.arm(new Uint8Array(20580));
  await assert.rejects(adapter.arm(new Uint8Array()),/one-use/);
  await assert.rejects(adapter.queue(0,{action:'tick',action_index:0,bundle:0}),/confirmed/);
  resolve({result:1,asyncify_idle:true});await arm;
  await adapter.queue(2,{action:'tick',action_index:2,bundle:1});
  await adapter.queue(3,{action:'draw',action_index:3,draw:1});
  assert.equal(commands[1].ordinal,1);assert.equal(commands[1].bundle,1);assert.equal(commands[1].scheduleIndex,2);
  assert.equal(commands[2].ordinal,1);assert.equal(commands[2].bundle,null);assert.equal(commands[2].scheduleIndex,3);
  await assert.rejects(adapter.approve(0,false),/unACKed/);
  await assert.rejects(adapter.observe(),/closed|uncertain/);assert.deepEqual(commands.map(c=>c.kind),['gate2-arm','gate2-queue','gate2-queue']);
  let count=0;const lost=createLaterBGate2NativeAdapter({owner:{},deadline:100,now:()=>0,
    evaluate:()=>{++count;return Promise.reject(Error('controlled lost response'));}});
  await assert.rejects(lost.arm(new Uint8Array()),/lost response/);
  await assert.rejects(lost.abortUnarmed(),/definite/);assert.equal(count,1);assert.equal(lost.state().unsafe,true);
});

test('actual orchestration uses supplied existing driver,360 ordered actions, and never cleans up an unACKed partial failure',async()=>{
  function fixture(failAt=null){
    const calls=[];let cursor=0,complete=false;const schedule=Array.from({length:360},(_,i)=>i%2?{action:'draw',action_index:i,draw:Math.floor(i/2)}:{action:'tick',action_index:i,bundle:Math.floor(i/2)});
    // Deliberate driver/model leaf: strict consumer acceptance is independently
    // exercised with the actual reference in the consumer component controls.
    const driver={laterB:true,payload:{schedule},arm:async()=>{calls.push('arm');return true;},
      submit:async action=>{assert.equal(action,schedule[cursor]);calls.push(`queue${cursor}`);return true;},
      accept:async()=>{calls.push(`compare${cursor}`);if(cursor===failAt)return false;++cursor;complete=cursor===360;return true;},
      status:()=>({complete,pending:!complete,cursor,first_error:'modeled exact comparison refusal'})};
    const adapter={state:()=>({}),step:async()=>calls.push(`step${cursor}`),observe:async()=>({actual_action:cursor}),
      retire:async()=>calls.push('retire'),abortUnarmed:async()=>calls.push('abort')};
    return{driver,adapter,calls,options:{driver,adapter,actualSetup:{},deadline:100,now:()=>0,
      persist:async()=>calls.push(`persist${cursor}`),record:async()=>calls.push(`record${cursor}`),finalize:async()=>{calls.push('finalize');return{finalized:true};}}};
  }
  const f=fixture();assert.equal((await runLaterBGate2ActionContinuation(f.options)).component_pass,true);
  assert.deepEqual(f.calls.slice(-2),['retire','finalize']);assert.equal(f.calls.filter(x=>x.startsWith('step')).length,360);
  const bad=fixture(1);await assert.rejects(runLaterBGate2ActionContinuation(bad.options),/comparison refusal/);
  assert.equal(bad.calls.includes('retire'),false);assert.equal(bad.calls.includes('abort'),false);assert.equal(bad.calls.includes('finalize'),false);
});

test('settled failed step is persisted with its native snapshot and never promoted to cleanup',async()=>{
  const failure={classification:'settled-native-step-failure',message:'first native service error',
    observation:{schema:'melee-web-stadium-later-b-gate2-native-v1',state:'failed',failed:true,
      error:'first native service error',cursor:0,source_ticks:0,source_draws:0}};
  const calls=[];let submitted=0;
  const adapter=createLaterBGate2NativeAdapter({owner:{},deadline:100,now:()=>0,
    evaluate:(_fn,command)=>Promise.resolve(command.kind==='gate2-step'
      ?{result:0,asyncify_idle:true,failure}:{result:1,asyncify_idle:true})});
  await adapter.arm(new Uint8Array(20580));
  const schedule=Array.from({length:360},(_,i)=>i%2
    ?{action:'draw',action_index:i,draw:Math.floor(i/2)}
    :{action:'tick',action_index:i,bundle:Math.floor(i/2)});
  const driver={laterB:true,payload:{schedule},arm:async()=>true,
    submit:async()=>{submitted++;return true;},accept:async()=>true,
    status:()=>({complete:false,pending:true,cursor:0,first_error:null})};
  await assert.rejects(runLaterBGate2ActionContinuation({driver,adapter,actualSetup:{},deadline:100,now:()=>0,
    persist:async row=>calls.push(row),record:async()=>calls.push({phase:'record'}),
    finalize:async()=>{calls.push({phase:'finalize'});return{finalized:true};}}),
    /Gate2 settled native step refusal: first native service error/);
  assert.equal(submitted,1);
  assert.equal(calls.length,1);
  assert.equal(calls[0].phase,'settled-native-step-failure');
  assert.equal(calls[0].action_index,0);
  assert.deepEqual(calls[0].failure,failure);
  assert.equal(adapter.state().settled_failure,failure);
  assert.equal(adapter.state().armed,true);assert.equal(adapter.state().unsafe,false);
  await assert.rejects(adapter.retire(),/failed/);
  await assert.rejects(adapter.abortUnarmed(),/definite/);
  assert.deepEqual(calls,[calls[0]],'failed step cannot be promoted to retire/abort/finalize');
});

test('actual harness catch/finally/snapshot branches bypass every ordinary cleanup after reservation or uncertainty',async()=>{
  const calls=[];const ctx={laterBGate2Reserved:true,page:{isClosed:()=>false},console};vm.createContext(ctx);
  ctx.observePageOperation=()=>{throw Error('ordinary/native snapshot must not run');};
  const snapshot=cut(captureSource,"async function snapshot(reason = 'poll', {captureCss = !diagnostic} = {}) {",'\nasync function phase(');
  const snap=vm.runInContext('('+snapshot+')',ctx);
  assert.equal((await snap('failure',{captureCss:false})).native_access_suppressed.includes('Gate2'),true);
  const prefix='  if (laterBGate2Reserved) await finalizeLaterBGate2Capture();\n  else if (firstReplayCallbackProbe) await finalizeFirstReplayCallbackProbe();';
  assert.equal(captureSource.split(prefix).length,2);
  ctx.finalizeLaterBGate2Capture=async()=>calls.push('gate2');ctx.finalizeFirstReplayCallbackProbe=()=>{throw Error('legacy cleanup');};ctx.firstReplayCallbackProbe=false;
  await vm.runInContext('(async()=>{'+prefix+'\nelse { throw Error("ordinary snapshot/unload/cache path"); }})()',ctx);
  assert.deepEqual(calls,['gate2']);
  const catchGuard='report.current_sss_exit.prepared_setup_failure = String(error.message || error);\n              if (laterBGate2Reserved) throw error;';
  assert.equal(captureSource.split(catchGuard).length,2);ctx.report={current_sss_exit:{}};ctx.error=Error('controlled partial failure');
  assert.throws(()=>vm.runInContext(catchGuard+'\nthrow Error("ordinary UI unload reached");',ctx),/controlled partial failure/);
  const finalizer=cut(captureSource,'async function finalizeLaterBGate2Capture() {','\nasync function browserArtifactInventory() {');
  ctx.laterBGate2Finalized=false;ctx.pageObservationTimedOut=true;
  ctx.closeOwnedCaptureBrowser=async()=>{calls.push('OS-close');ctx.report.cleanup={browser_close_returned:true};};
  ctx.write=async()=>calls.push('host-write');ctx.pageErrors=[];ctx.retainReplayArtifacts=()=>{throw Error('unsafe page evidence');};
  await vm.runInContext('('+finalizer+')()',ctx);
  assert.equal(ctx.report.result,'fail');assert.equal(ctx.report.later_b_gate2.native_cleanup,'UNRUN_OWNED_OS_CLEANUP_ONLY');
  assert.deepEqual(calls.slice(1),['OS-close','host-write']);
  // Synthetic acknowledged-terminal leaf, not observed native proof. Exercise
  // the actual success finalizer with ordinary/native access still trapped.
  ctx.laterBGate2Finalized=true;ctx.pageObservationTimedOut=false;
  ctx.observePageOperation=async(label,operation)=>{
    assert.ok(['Gate2 pure callback ownership','Gate2 restore JS replay callback'].includes(label),
      `Forbidden ordinary/native operation in acknowledged finalizer: ${label}`);
    return typeof operation==='function'?operation():operation;
  };
  ctx.laterBGate2Reference={goTick:84};ctx.currentSssExpected={};ctx.currentSssTrace='retained prefix';
  ctx.currentSssPolicy={trace:()=>({pass:true})};
  const receipt={native_retired:true,terminal:true,phase_after_close:0,running_after_close:0,error:null,
    later_b_transfer:{token:tokenId,parent_close_checked:true},matched_setup_cf:{schema:'melee-web-stadium-later-b-gate2-native-v1',
      action_count:360,cursor:360,complete:true,source_ticks:180,source_draws:180,actual_go:{observed:true,tick:84},post_go_typed22_state_count:96}};
  ctx.report={current_sss_exit:{comparison:{pass:true},prepared_start_comparison:{pass:true},prepared_setup_comparison:{pass:true},observation:{}},
    later_b_gate2:{token_id:tokenId,accepted_actions:360,component_pass:true},
    browser_report:{complete:false,later_b_transfer:{native_terminal:{kind:'retired',receipt},action_evidence:Array(360).fill({})}},
    artifact_inventory_pre:{equal:true},unexpected_requests:[]};
  ctx.currentSssArm={evaluate:async fn=>fn({state:()=>({owned:true}),dispose:()=>true}),dispose:async()=>calls.push('JS-handle-dispose')};
  ctx.retainReplayArtifacts=async()=>calls.push('pure-blob-evidence');
  ctx.browserArtifactInventory=async()=>({equal:true});ctx.finalizeSessionCapture=report=>{report.result='incomplete';report.finalization_failures=['whole replay remains incomplete'];};
  const actualFinalizationErrors=[];
  ctx.firstError=(kind,message)=>actualFinalizationErrors.push({kind,message});
  await vm.runInContext('('+finalizer+')()',ctx);
  assert.deepEqual(actualFinalizationErrors,[],`Actual finalizer refused: ${JSON.stringify(actualFinalizationErrors)}`);
  assert.equal(ctx.report.result,'later_b_gate2_component_pass_replay_incomplete');
  assert.deepEqual(ctx.report.finalization_failures,['whole replay remains incomplete']);
});

test('Gate2 capture opt-in requires the exact existing profile3 route, with no deadline or route fallback',()=>{
  const declaration=cut(captureSource,"const laterBGate2ProjectionPath = values['stadium-later-b-gate2-projection']",'\nif (laterSssReference !== null');
  const run=values=>vm.runInNewContext(declaration,{values,path:{resolve:x=>x},laterSssReference:values.reference,
    laterSssPreparedSetup:values.setup,OBSERVED_B_REFERENCE:{id:'original-b-2013-943-v1'}});
  assert.doesNotThrow(()=>run({}));
  assert.throws(()=>run({'stadium-later-b-gate2-projection':'projection.json'}),/exact new-reference/);
  assert.doesNotThrow(()=>run({'stadium-later-b-gate2-projection':'projection.json',reference:'original-b-2013-943-v1',setup:true}));
  assert.ok(captureSource.indexOf('deriveLaterBGate2Expectations(laterBGate2ProjectionBytes)')<captureSource.indexOf('const {chromium, browser: launchOptions'));
});
