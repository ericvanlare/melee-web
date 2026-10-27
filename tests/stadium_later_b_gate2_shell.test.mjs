import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import test from 'node:test';
import {webcrypto} from 'node:crypto';

// Actual source functions; synthetic DOM/audio/native-callback leaves only.
// These controls do NOT show a compiled native transfer, close or lifetime.
const root = process.env.MELEE_GATE2_SHELL_SOURCE_ROOT || path.resolve(import.meta.dirname, '..');
const core = fs.readFileSync(path.join(root, 'web/melee-runtime.mjs'), 'utf8');
const dev = fs.readFileSync(path.join(root, 'web/runtime-development.mjs'), 'utf8');
function cut(text, first, last) {
  assert.equal(text.split(first).length, 2, `unique start ${first}`);
  const a = text.indexOf(first), b = text.indexOf(last, a + first.length);
  assert.ok(b > a, `missing ordered endpoint ${last}`);
  return text.slice(a, b);
}
const factory = cut(core, '  function createLaterBReplayTransferOwner() {', '  // END LATER_B_REPLAY_TRANSFER_OWNER');
const suspended = cut(dev, 'const laterBReplayNativeSuspended=', '\nconst $=');
const transfer = cut(dev, '// BEGIN LATER_B_REPLAY_SHELL_TRANSFER', '// END LATER_B_REPLAY_SHELL_TRANSFER');
const observers=cut(dev,'window.menuObserveFighter=kind=>{','const nextAnimationFrame=');
const writers = cut(dev, 'const replayMemorySnapshot=()=>{', 'window.menuReplayStarted=');
const polling = cut(dev, 'window.menuReplayStarted=', '// Development-only networked-session entry');
const timing = cut(dev, 'window.menuRuntimeTiming=data=>', '\ndevelopmentHooks.preparation=');
const frame = cut(dev, 'developmentHooks.frame=wasRunning=>{', "for(const type of ['focus','blur'");
const reload = cut(dev, 'async function reloadApplication(clearRenderCache){', "$('reload-app').onclick=");
const clicks = cut(dev, "$('pause').onclick=()=>{", 'controllerSettings = mountControllerSettings');
const events = cut(dev, '    onEvent(name,data){', '\n    },\n  });').trim().replace(/^onEvent\(/, 'function actualOnEvent(') + '\n}';
const retirement = cut(core, '    menuMatchedSetupCfRetired(receipt) {', '    // Private nonterminal canary boundary.');
const preparationCanceled = cut(core, '    menuPreparationCanceled() {', '    menuPreparationFailed(error) {');
const actualSnapshot = cut(core, '  function snapshot() {', '  function publish() {');
const actualPublish = cut(core, '  function publish() {', '  function recordNetworkCleanupError(');
const otherRetirements=cut(core,'    menuMatchDrawDiscardRetired() {','    // The explicit-step Stadium path')+
  cut(core,'    menuStadiumRecreationClosed(receipt) {','    menuFrame(wasRunning) {');
const ordinaryBoundary = cut(core, '  const boundary = run =>', '  async function operation(');
const coreFrame = cut(core, '    menuFrame(wasRunning) {', '\n  };');
const snapshot = cut(core, '  function snapshot() {', '  function publish() {');
const sync = cut(core, '  function syncAudio() {', '  function diagnosticIncident(');
const stop = cut(core, '  function stop(error, startupFailureKind', '  failStartup = stop;');
const unload = cut(core, '  async function unloadAndSave() {', '  function putNow(');
const destroy = cut(core, '    destroy() {', '\n  });\n  function setAutomaticDiagnostics');
const service = cut(core, '    menuServiceCommands() {', '    // An in-flight native transaction');
const checkpoint = cut(core, '  function requestDiagnosticCheckpoint() {', '  function beginDiagnosticCheckpoint() {');
const cacheExport = cut(dev, "$('export-render-cache').onclick=async()=>", '// Replay setup/decoding');
const save=cut(core,'  async function configureSaveProfile(mode, cardData = null) {','  async function unloadAndSave() {');
const handleUnload=cut(core,'    unload() {','    destroy() {');
const request = {
  reference_id: 'original-b-2013-943-v1',
  recipe_sha256: '5fb53bdc71b35b800bc3451b1d9117709edef2413a38a8f7311ee352647a6755',
  cursor: 943, notification_order: 1897, world_generation: 7, setup_seed_hex: '4b25c79b',
}; // Order is arbitrary synthetic owner bookkeeping, never an original-order claim.
const longRequest = {
  reference_id: 'original-b-5422-951-long-v1',
  recipe_sha256: 'e669f831538b90e39241a569b6c38884300bfe246b252cacda994cbc45c10bbf',
  cursor: 951, notification_order: 1903, world_generation: 9, setup_seed_hex: '25d557a7',
}; // Synthetic request only; the source-bound descriptor is independently reviewed.
const inputProfile = value => value.reference_id === request.reference_id ? {frames:2013,scope:'frozen-menu-prefix943-not-action-metrics'} :
  value.reference_id === longRequest.reference_id ? {frames:5422,scope:'literal-global-prefix951-not-action-metrics'} : null;
function fixture(inputRequest = request) {
  const profile=inputProfile(inputRequest);
  assert.ok(profile,'test fixture only supports the two explicit checked profiles');
  const nodes = new Map(), downloads = [], calls = [], deferred = [];
  const $ = name => {
    if (!nodes.has(name)) nodes.set(name, {disabled: false, files: [{}], dataset: {}, textContent: '',querySelectorAll:()=>[],replaceChildren(){},append(){}});
    return nodes.get(name);
  };
  const context = vm.createContext({assert, console, TextEncoder, crypto: webcrypto, JSON,
    navigator: {userAgent: 'synthetic-control'}, devicePixelRatio: 1,
    window: {meleeHitchCapture: {stop() {}, report() {return null;}}},
    document: {hidden: false}, performance: {now: () => 3000001},
    saveProfileSnapshotsActive:0,ready: true, fatal: false, destroyed: false, busy: '', assetTransfer: null, networkSessionClose: null, commands: [], loading: null,
    terminalMatchedSetupCfRetirementObserved: false, terminalMatchedSetupCfRetirementConsumed: false,
    preparationLabel: '', diagnosticActive: false, startupProfilingPending: false,
    failProfile() {},diagnosticActivity() {},clearStartupTimeout() {},
    diagnosticIncident:()=>{throw Error('Unexpected native recorder incident');},
    closeNetworkSession:()=>Promise.resolve(),retireDiscAfterNetwork:()=>{throw Error('Unexpected disc retirement');},
    audio:{fail(){}},rejectStartup(){},onError(){},
    diagnosticLifecycle:()=>calls.push('diagnosticLifecycle'), publish:()=>calls.push('publish'), callbacks: {menuPreparationCanceled() {}},
    emit: (name, value) => calls.push(['emit', name, value]),
    snapshot: () => ({state: 'preparing', running: false, phase: 7}),
    Module: {HEAPU8: new Uint8Array(32), stadiumMatchedSetupNativeIdle: () => true,
      _melee_web_native_menu_replay_cursor: () => {calls.push('cursor'); return inputRequest.cursor;},
      _melee_web_native_menu_memory: () => {calls.push('memory'); return 1;},
      UTF8ToString: () => '{"scope":"synthetic-pre-move-memory"}'},
    $, calls, downloads, deferred, laterRequest: inputRequest,
    bundle: true, importing: false, replayLoading: false, preparationSince: 0,
    nativeSourceSteps: inputRequest.cursor, nativeSourceDraws: inputRequest.cursor, worstBrowserCallback: null,
    activeFrames: 4, worstFrame: 0, longFrames: 0, browserLongTasks: 0, browserLongTaskWorst: 0,
    nativeTimingFrames: 4, nativeTimingWorst: 0, nativeLongFrames: 0, nativeOverBudgetFrames: 0,
    nativeWorstTiming: null, livePipelineQueued: 0, livePipelineCreated: 0, liveTextureUploads: 0,
    liveStagingUsedBytes: 0, peakStagingUsedBytes: 0, preparationCount: 2,
    latestAudio: {underruns: 0, overflows: 0}, diagnosticCaptureInvalid: false,
    retailReplayWallTimeMs: () => 2700000, markFirstReplayBoundary() {},
    replayHash: async () => 'synthetic-trace-hash',
    replayDownload: (name, text) => downloads.push([name, text]),
    syncAudio: () => calls.push('syncAudio'), unloadAndSave: async () => {calls.push('unload'); return true;},
    resetTiming: () => calls.push('resetTiming'),
    setTimeout: fn => {deferred.push(fn); return 1;}, log() {},
    lockstepRoomConfigured: false, location: {href: 'https://synthetic.test/', replace() {calls.push('reload');}}, URL,
    developmentHooks: {}, status: () => {calls.push('status'); return 'Replay stopped';},
  });
  vm.runInContext(`${factory}\nconst laterBReplayTransfer=createLaterBReplayTransferOwner();
    const owner={laterBReplayTransfer};
    let cacheEvidence=[];
    let retailRun={observe:true,wholeSession:true,baseline:{preparations:2,heap:32,underruns:0,overflows:0},
      hash:laterRequest.recipe_sha256,frames:${profile.frames},expectedDraws:${profile.frames},rows:Array.from({length:laterRequest.cursor},(_,index)=>JSON.stringify({record:'session_frame',index,supplied_inputs:Array(4).fill('00'.repeat(11))})),timerRows:[],
      memory:{},preparation:{},cache:{},started:0,lastProgress:17,lastCursor:laterRequest.cursor,scheduling:2};
    ${suspended}\n${observers}\n${transfer}\n${writers}\n${polling}\n${timing}\n${frame}\n${reload}\n${clicks}\n${cacheExport}\n${events}
    const actualCallbacks={${retirement}};
    const actualOtherRetirements={${otherRetirements}};
    const actualCoreFrame={${coreFrame}};
    const actualDestroy={${destroy}};const actualService={${service}};
    const actualCoreUnload=(()=>{${unload};return unloadAndSave;})();
    const actualSave=(()=>{${save};return {configureSaveProfile,snapshotSaveProfile};})();
    const actualHandleUnload={${handleUnload}};
${checkpoint}
    ${ordinaryBoundary}
    globalThis.api={reserve:window.menuReplayReserveLaterBTransfer,core:laterBReplayTransfer,
      callbacks:actualCallbacks,otherRetirements:actualOtherRetirements,coreFrame:actualCoreFrame,boundary,
      coreDestroy:()=>actualDestroy.destroy(),coreService:()=>actualService.menuServiceCommands(),
      coreUnload:actualCoreUnload,coreHandleUnload:()=>actualHandleUnload.unload(),
      coreSave:actualSave,checkpoint:requestDiagnosticCheckpoint,
      coreStop:()=>{${stop};return stop(Error('synthetic fatal'));},
      run:()=>retailRun,setRun:r=>{retailRun=r;},owner,
      finish:finishRetailReplay,reload:reloadApplication,events:actualOnEvent,
      coreSnapshot:()=>{${snapshot};return snapshot();},coreSync:()=>{${sync};return syncAudio();}};`, context);
  return {context, api: context.api, calls, downloads, $, request:inputRequest, run: () => context.api.run()};
}
function reserve(f) {const token = f.api.reserve(f.request); f.calls.length = 0; return token;}
function proof(token, extra = {}, inputRequest=request) {
  return {schema: 'melee-web-stadium-later-b-parent-transfer-v1', ...inputRequest, token: token.id,
    parent_identity_checked: true, stage: 'after-parent-move', parent_archived: true,
    source_ticks: 0, source_draws: 0, ...extra};
}
function noNative(f) {
  for (const name of ['_melee_web_native_menu_replay_cursor','_melee_web_native_menu_memory',
    '_melee_web_native_menu_phase','_melee_web_native_menu_running','_melee_web_native_menu_message',
    '_melee_web_native_menu_diagnostics','_melee_web_input_message','_melee_web_native_menu_stock_check_ready',
    '_melee_web_native_menu_match_draw_discard_status','_melee_web_native_menu_stadium_recreation_status',
    '_melee_web_input_set_activity','_melee_web_input_set_keyboard','_melee_web_input_set_keyboard_port'])
    f.context.Module[name] = () => {throw Error(`Forbidden native ${name}`);};
}
function pendingTransfer(f, token) {
  f.context.window.menuReplayLaterBCommandPending(token,'arm');
  assert.equal(f.api.callbacks.menuLaterBReplayParentTransferred(proof(token, {}, f.request)), true);
  f.context.window.menuReplayLaterBCommandSettled(token);
}
function retirementReceipt(token, inputRequest=request) {
  return {schema:'melee-web-stadium-matched-setup-cf-retirement-v1',native_retired:true,terminal:true,error:null,
    phase_after_close:0,running_after_close:0,later_b_transfer:proof(token,{parent_close_checked:true},inputRequest),
    matched_setup_cf:{state:'complete',complete:true,error:null,queued:false}};
}
test('explicit long B profile4 reserves only its exact 951-row literal prefix and retires once', async () => {
  const wrongRecipe=fixture(longRequest);
  assert.throws(()=>wrongRecipe.api.reserve({...longRequest,recipe_sha256:request.recipe_sha256}),/exact stopped state replay951 owner/);
  const wrongSeed=fixture(longRequest);
  assert.throws(()=>wrongSeed.api.reserve({...longRequest,setup_seed_hex:'4b25c79b'}),/exact stopped state replay951 owner/);
  const unknownProfile=fixture(longRequest);
  assert.throws(()=>unknownProfile.api.reserve({...longRequest,reference_id:'__proto__'}),/exact stopped state/);
  const wrongPrefix=fixture(longRequest);
  wrongPrefix.run().rows[950]=JSON.stringify({record:'session_frame',index:950,supplied_inputs:['00'.repeat(11)]});
  assert.throws(()=>wrongPrefix.api.reserve(longRequest),/literal951/);

  const f=fixture(longRequest),token=reserve(f),prefix=f.run().laterBTransfer.prefix;
  assert.equal(prefix.recipe_sha256,longRequest.recipe_sha256);
  assert.equal(prefix.rows.length,951);
  assert.equal(prefix.metrics.sourceFrames,951);
  assert.equal(prefix.scope,'literal-global-prefix951-not-action-metrics');
  assert.equal(f.api.core.state().transferred,false);
  pendingTransfer(f,token);
  f.api.core.pending(token,'retire');
  assert.equal(f.api.callbacks.menuMatchedSetupCfRetired(retirementReceipt(token,longRequest)),true);
  f.api.core.settled(token);
  await f.context.window.menuReplayFinalizeLaterBTransfer(token);
  const report=JSON.parse(f.downloads.find(([name])=>name==='retail-browser-report.json')[1]);
  assert.equal(report.complete,false);
  assert.equal(report.later_b_transfer.reference_id,'original-b-5422-951-long-v1');
  assert.equal(report.later_b_transfer.prefix.rows.length,951);
  assert.equal(report.later_b_transfer.native_terminal.kind,'retired');
  assert.equal(f.downloads.filter(([name])=>name==='retail-browser-report.json').length,1);
});
test('actual reservation binds exact run/token/context; no native transfer inferred', () => {
  const f = fixture();
  assert.throws(() => f.api.reserve({...request, cursor:940}), /exact stopped/);
  f.context.saveProfileSnapshotsActive=1;assert.throws(()=>f.api.reserve(request),/idle exact/);
  assert.deepEqual(f.calls,[]);f.context.saveProfileSnapshotsActive=0;
  const intact=f.run().rows[43];f.run().rows[43]=JSON.stringify({record:'session_frame',index:44,supplied_inputs:Array(4).fill('00'.repeat(11))});
  assert.throws(()=>f.api.reserve(request),/literal943/);f.run().rows[43]=intact;
  const token = reserve(f), prefix = JSON.stringify(f.run().laterBTransfer.prefix);
  assert.equal(f.api.core.state().transferred, false);
  assert.throws(() => f.api.reserve(request), /exact stopped/);
  assert.throws(() => f.api.core.pending({id:token.id},'arm'), /foreign/);
  const other=fixture(),otherToken=reserve(other);other.api.core.pending(otherToken,'arm');
  assert.notEqual(otherToken.id,token.id);assert.equal(other.api.callbacks.menuLaterBReplayParentTransferred(proof(token)),false);
  const original = f.run(); f.api.setRun({...original});
  assert.throws(() => f.context.window.menuReplayLaterBCommandPending(token), /foreign/);
  f.api.setRun(original); assert.equal(JSON.stringify(original.laterBTransfer.prefix), prefix);
  f.api.core.pending(token,'arm');
  assert.equal(f.api.callbacks.menuLaterBReplayParentTransferred(proof(token,{world_generation:8})), false);
  assert.equal(f.api.callbacks.menuLaterBReplayParentTransferred(proof(token)), true);
  assert.equal(f.api.callbacks.menuLaterBReplayParentTransferred(proof(token)), false);
});
test('actual poll/frame/timing/core frame/boundary and every ordinary finish path avoid native access', async () => {
  const f = fixture(), token = reserve(f); noNative(f);
  assert.equal(f.api.coreService(),0);
  assert.equal(f.api.otherRetirements.menuMatchDrawDiscardRetired(),false);
  assert.equal(f.api.otherRetirements.menuStadiumRecreationClosed({}),false);
  const originalRun = f.run(), before = JSON.stringify(originalRun.laterBTransfer.prefix);
  f.context.window.menuReplayPoll(); // both stopped700ms and expired walltime branches are bypassed.
  f.context.developmentHooks.frame(true);
  f.api.coreFrame.menuFrame(true);
  f.context.window.menuRuntimeTiming({valid:true,source_steps:1,source_draws:0});
  f.context.window.menuReplayStarted(2013,true);
  f.context.window.menuReplayCompleted(2013,true,0,0,1);
  await f.api.finish('deferred completion'); await f.api.reload(false);
  await f.$('unload').onclick(); f.$('pause').onclick();await f.$('export-render-cache').onclick();
  for(const name of ['menuObserveFighter','menuObserveCssSetup','menuObserveFighterPort','menuObserveStage','menuObservePlayer'])
    assert.throws(()=>f.context.window[name](),/ordinary native diagnostics/);
  f.api.events('fatal','synthetic fatal');
  for (const fn of f.context.deferred.splice(0)) await fn();
  await assert.rejects(f.api.boundary(() => {throw Error('Must not run');}), /owns native/);
  assert.equal(f.api.coreSnapshot().state, 'diagnostic-transfer'); f.api.coreSync();
  assert.equal(f.run(), originalRun); assert.equal(f.run().completed, undefined);
  assert.equal(f.run().lastProgress,17); assert.equal(JSON.stringify(f.run().laterBTransfer.prefix),before);
  assert.equal(f.run().laterBTransfer.timing.source_steps,1);
  assert.ok(f.run().laterBTransfer.finishRequests.some(x => x.includes('wall time')));
  assert.ok(f.run().laterBTransfer.finishRequests.some(x => x.includes('synthetic fatal')));
  assert.deepEqual(f.calls.filter(x => ['cursor','memory','unload','syncAudio','status','reload','resetTiming'].includes(x)),[]);
  await assert.rejects(f.context.window.menuReplayFinalizeLaterBTransfer(token),/no acknowledged/);
});
test('actual core callback + settled retirement finalizes writer once without a second native teardown', async () => {
  const f=fixture(),token=reserve(f);pendingTransfer(f,token);noNative(f);
  const prefix=JSON.stringify(f.run().laterBTransfer.prefix);assert.equal(f.api.coreService(),0);
  f.context.window.menuReplayRecordLaterBAction(token,{action_index:0,kind:'tick',source_tick:0});
  assert.throws(()=>f.api.core.pending(token,'arm'),/repeated/);
  f.api.core.pending(token,'retire');
  const bad=retirementReceipt(token);bad.later_b_transfer.token='foreign-token';
  const hostEffects=f.calls.length;assert.equal(f.api.callbacks.menuMatchedSetupCfRetired(bad),false);
  assert.equal(f.calls.length,hostEffects);assert.equal(f.context.terminalMatchedSetupCfRetirementObserved,false);
  assert.equal(f.api.callbacks.menuMatchedSetupCfRetired(retirementReceipt(token)),true);
  await assert.rejects(f.context.window.menuReplayFinalizeLaterBTransfer(token),/settled/);
  f.api.core.settled(token);
  await f.context.window.menuReplayFinalizeLaterBTransfer(token);
  const report=JSON.parse(f.downloads.find(([name])=>name==='retail-browser-report.json')[1]);
  assert.equal(report.complete,false);assert.equal(report.pass,false);
  assert.equal(report.metrics.sourceFrames,943);
  assert.equal(report.later_b_transfer.action_evidence.length,1);
  assert.equal(JSON.stringify(report.later_b_transfer.prefix),prefix);
  assert.equal(report.later_b_transfer.idbfs_persistence,'not_run');
  assert.equal(f.downloads.filter(([name])=>name==='retail-browser-report.json').length,1);
  assert.equal(f.api.coreService(),0);
  assert.equal(f.api.otherRetirements.menuMatchDrawDiscardRetired(),false);
  assert.equal(f.api.otherRetirements.menuStadiumRecreationClosed({}),false);
  f.context.developmentHooks.frame(false);f.api.coreFrame.menuFrame(false);
  f.context.window.menuRuntimeTiming({valid:true});await f.$('unload').onclick();await f.api.reload(false);
  assert.ok(!f.calls.some(x=>['unload','memory','cursor','syncAudio','reload'].includes(x)));
  await assert.rejects(f.context.window.menuReplayFinalizeLaterBTransfer(token),/stale/);
});
test('known unarmed abort uses its own checked callback; uncertain command has OS-only evidence', async () => {
  const f=fixture(),token=reserve(f);noNative(f);f.api.core.pending(token,'arm');f.api.core.settled(token);
  assert.throws(()=>f.api.core.pending(token,'arm'),/repeated/);f.api.core.pending(token,'abort');
  const abort={schema:'melee-web-stadium-later-b-unarmed-abort-v1',native_closed:true,attempt_consumed:true,
    armed:false,source_ticks:0,source_draws:0,action_cursor:0,error:null,phase_after_close:0,running_after_close:0,
    later_b_transfer:proof(token,{parent_close_checked:true})};
  assert.equal(f.api.callbacks.menuLaterBReplayUnarmedAborted({...abort,armed:true}),false);
  assert.equal(f.api.callbacks.menuLaterBReplayUnarmedAborted(abort),true);
  f.api.core.settled(token);await f.context.window.menuReplayFinalizeLaterBTransfer(token);
  assert.equal(JSON.parse(f.downloads.at(-1)[1]).later_b_transfer.native_terminal.kind,'known-unarmed-abort');
  const g=fixture(),other=reserve(g);g.api.core.pending(other,'arm');noNative(g);
  g.context.window.menuReplayLaterBCommandUncertain(other,'lost response');
  await g.api.finish('timeout');g.context.window.menuReplayPoll();g.context.developmentHooks.frame(true);
  await assert.rejects(g.context.window.menuReplayFinalizeLaterBTransfer(other),/OS cleanup only/);
  assert.throws(()=>g.api.core.settled(other),/settled/);
  assert.equal(g.api.callbacks.menuLaterBReplayUnarmedAborted(abort),false);
  const retained=g.context.window.menuReplayReadLaterBTransferEvidence(other);
  assert.equal(retained.native_cleanup,'unrun-OS-only');assert.equal(retained.whole_replay_complete,false);
  assert.equal(g.downloads.length,0);assert.ok(!g.calls.includes('unload'));
  assert.equal(g.api.coreService(),-1);
  await assert.rejects(g.api.coreDestroy(),/OS cleanup/);
  await assert.rejects(g.api.coreUnload(),/ordinary unload/);
  assert.throws(()=>g.api.coreHandleUnload(),/save\/unload access/);
  g.context.Module._malloc=()=>{throw Error('Forbidden native allocation');};
  g.context.Module._free=()=>{throw Error('Forbidden native free');};
  await assert.rejects(g.api.coreSave.snapshotSaveProfile(),/save\/unload access/);
  await assert.rejects(g.api.coreSave.configureSaveProfile('everything'),/save\/unload access/);
  assert.equal((await g.api.checkpoint()).reason,'private-replay-transfer');
  g.api.coreStop();
  for(const fn of g.context.deferred.splice(0))await fn();
  assert.ok(!g.calls.some(x=>['unload','memory','cursor'].includes(x)));
});
test('inactive transfer preserves actual ordinary report writer/unload behavior and performance lease rejection', async () => {
  const f=fixture();await f.api.finish('ordinary stopped prefix');
  assert.equal(f.calls.filter(x=>x==='unload').length,1);
  assert.equal(f.calls.filter(x=>x==='memory').length,2);
  const report=JSON.parse(f.downloads.at(-1)[1]);assert.equal(report.later_b_transfer,undefined);
  assert.equal(report.complete,false);assert.ok(f.calls.includes('syncAudio'));
  const lease=cut(dev,'window.menuReplayAcquireStoppedEvidence=', '// Keep read-only diagnostics');
  const g=fixture();vm.runInContext(lease,g.context);
  assert.throws(()=>g.context.window.menuReplayAcquireStoppedEvidence(943,()=>({acknowledged:true})),/performance replay/);
});

test('actual cache export owns the entire awaited persistence and FS transaction before reservation', async () => {
  const f=fixture();let release,saveStarted=false,fsReads=0;
  const saved=new Promise(resolve=>{release=resolve;});
  f.context.Module._melee_web_native_menu_cache_idle=()=>1;
  f.context.Module.saveRuntimeCache=()=>{saveStarted=true;return saved;};
  f.context.Module.FS={readdir(){fsReads++;return[];}};
  const exporting=f.$('export-render-cache').onclick();
  assert.equal(f.context.commands.length,1);
  // Model the existing command queue leaf only; the actual export handler,
  // transaction owner and reservation checks are the production source.
  const command=f.context.commands.shift();command.resolve(command.run());
  await Promise.resolve();await Promise.resolve();
  assert.equal(saveStarted,true);assert.equal(f.context.commands.length,0);
  const nativeBefore=f.calls.length;
  assert.throws(()=>f.api.reserve(request),/idle exact/);
  assert.equal(f.calls.length,nativeBefore);assert.equal(fsReads,0);
  release(true);await exporting;assert.equal(fsReads,1);
  const token=reserve(f);assert.equal(f.api.core.state().phase,'reserved');
  await f.$('export-render-cache').onclick();assert.equal(fsReads,1);
  assert.throws(()=>f.api.core.endCacheExport({}),/foreign or repeated/);
  assert.equal(typeof token.id,'string');
});



function terminalPublicationFixture({busyOwner=false, unrelatedLoading=false}={}) {
  const calls=[];
  const context=vm.createContext({
    assert, crypto:webcrypto, JSON, Object, Number, Error,
    ready:true, fatal:false, destroyed:false, busy:'', assetTransfer:null,
    networkSessionClose:null, commands:[], saveProfileSnapshotsActive:0,
    loading:{phase:'native',message:'Preparing game data…',complete:0,total:0},
    preparationLabel:'Preparing original scene', preparationKeepsAudio:true, message:'',
    terminalMatchedSetupCfRetirementObserved:false, terminalMatchedSetupCfRetirementConsumed:false,
    diagnosticActive:true, lastState:'', document:{hidden:false},
    diagnosticActivity(value){context.diagnosticActive=value;calls.push(['diagnosticActivity',value]);},
    onState(state){calls.push(['state',state]);},
    diagnosticLifecycle(name){calls.push(['lifecycle',name]);},
    emit(name){calls.push(['emit',name]);},
    Module:{stadiumMatchedSetupNativeIdle:()=>true}, calls,
  });
  const init=[
    factory,
    "let snapshot=()=>({state:'preparing',running:false,busy:false,loading});",
    "const laterBReplayTransfer=createLaterBReplayTransferOwner();",
    "const request="+JSON.stringify(request)+";",
    "const token=laterBReplayTransfer.reserve(request);",
    "const before=laterBReplayTransfer.snapshot();",
    "const actual=(()=>{",
    actualSnapshot,
    actualPublish,
    "return {snapshot,publish};})();",
    "snapshot=actual.snapshot; const publish=actual.publish;",
    "const callbacks={",
    preparationCanceled,
    retirement,
    "};",
    "globalThis.api={laterBReplayTransfer,request,token,before,snapshot,publish,callbacks};",
  ].join('\n');
  vm.runInContext(init,context);
  if(busyOwner) context.busy='asset transfer';
  if(unrelatedLoading) context.loading={phase:'disc',message:'Controlled unrelated read',complete:1,total:2};
  const api=context.api, token=api.token;
  api.laterBReplayTransfer.pending(token,'arm');
  const parentProof={schema:'melee-web-stadium-later-b-parent-transfer-v1',...api.request,
    token:token.id,parent_identity_checked:true,stage:'after-parent-move',parent_archived:true,
    source_ticks:0,source_draws:0};
  assert.equal(api.laterBReplayTransfer.transferred(parentProof),true);
  api.laterBReplayTransfer.settled(token);
  api.laterBReplayTransfer.pending(token,'retire');
  // After transfer, even the idle predicate is forbidden to the host callback.
  for(const name of ['stadiumMatchedSetupNativeIdle','_melee_web_native_menu_phase',
    '_melee_web_native_menu_running','_melee_web_native_menu_message'])
    context.Module[name]=()=>{throw Error('Forbidden post-transfer native getter '+name);};
  const receipt={schema:'melee-web-stadium-matched-setup-cf-retirement-v1',native_retired:true,
    terminal:true,error:null,phase_after_close:0,running_after_close:0,
    later_b_transfer:{...parentProof,parent_close_checked:true},
    matched_setup_cf:{state:'complete',complete:true,error:null,queued:false}};
  return {context,api,receipt,calls};
}

test('actual settled retirement cancels the reserved native-loading snapshot without weakening refusal guards',()=>{
  const valid=terminalPublicationFixture();
  assert.equal(valid.api.before.loading.phase,'native');
  assert.equal(valid.api.callbacks.menuMatchedSetupCfRetired(valid.receipt),true,
    'actual snapshot and publish consume current JS-owned loading after native preparation cancellation');
  assert.equal(valid.context.loading,null);
  assert.equal(valid.context.preparationLabel,'');
  assert.equal(valid.context.diagnosticActive,false);
  assert.equal(valid.api.laterBReplayTransfer.state().phase,'pending',
    'the callback records terminal proof; shell settlement remains a separate command');
  assert.ok(valid.calls.some(row=>row[0]==='diagnosticActivity'&&row[1]===false));

  const foreign=terminalPublicationFixture();
  const bad={...foreign.receipt,later_b_transfer:{...foreign.receipt.later_b_transfer,token:'foreign-token'}};
  assert.equal(foreign.api.callbacks.menuMatchedSetupCfRetired(bad),false);
  assert.equal(foreign.context.terminalMatchedSetupCfRetirementObserved,false);
  assert.equal(foreign.context.loading.phase,'native');
  assert.equal(foreign.calls.length,0,'foreign token refusal performs no publication or native access');

  const busy=terminalPublicationFixture({busyOwner:true});
  assert.equal(busy.api.callbacks.menuMatchedSetupCfRetired(busy.receipt),false);
  assert.equal(busy.context.busy,'asset transfer');
  assert.equal(busy.context.loading.phase,'native');
  assert.equal(busy.context.diagnosticActive,true,'active host owner remains visible');

  const unrelated=terminalPublicationFixture({unrelatedLoading:true});
  assert.equal(unrelated.api.callbacks.menuMatchedSetupCfRetired(unrelated.receipt),false);
  assert.equal(unrelated.context.loading.phase,'disc');
  assert.equal(unrelated.context.diagnosticActive,true,'unrelated loading remains visible and unconsumed');
});
