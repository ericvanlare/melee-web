import assert from 'node:assert/strict';
import test from 'node:test';
import {createStadiumRecreationCanaryContext} from './stadium_recreation_canary_context.mjs';

function fixture(fullPair = false) {
  const native = {complete: true, failed: false, cursor: 174,
    match_seed: 0x1234, host_seed: 0x5678, parent_seed: 0x9abc};
  const reportA = {scenario: {
    matched_setup_cf: {actions_completed: 174,
      actions: Array.from({length: 174}, (_, index) => ({index})),
      decision: 'PASS_RUN2_PROPER_PREFIX_C_F_STATE_ONLY', last_native_state: native},
    first_sss_vs_setup: {observation: {first_sss_vs_setup: {match_world_generation: 3}}},
  }};
  if (fullPair) {
    const cf = reportA.scenario.matched_setup_cf;
    cf.actions_completed = 7167;cf.actions = Array.from({length:7167},(_,index)=>({index}));
    cf.decision = 'PASS_ORIGINAL_FULL7167_3501_TICK_15_COMMON_FIELDS_AND_SEED_C_F_GO';
    cf.common_field_comparisons_passed = 3501;cf.seed_value_comparisons_passed = 3501;
    cf.exact_value_comparisons_passed = 56016;
    Object.assign(native,{cursor:7167,action_count:7167,source_ticks:3585,source_draws:3582,
      schema:'melee-web-stadium-source-action-native-v5'});
  }
  const checkpointA = Object.freeze({owner: 'A'});
  const context = createStadiumRecreationCanaryContext({reportA, checkpointA, fullPair});
  const close = {schema: 'melee-web-stadium-recreation-boundary-v1', attempted: true,
    closed: true, restarted: false, error: null, match_to_host_restored: true,
    host_to_parent_restored: true, prior_world_generation: 3,
    matched_setup_cf: structuredClone(native), match_seed: 0x1234,
    host_seed: 0x5678, parent_seed: 0x9abc};
  return {native, reportA, checkpointA, context, close};
}

test('A archive, exact close receipt, restart, and distinct B ownership form one gated sequence', async () => {
  const f = fixture();
  let persistedA = 0, persistedClose = 0;
  await f.context.archiveA(async (report, checkpoint) => {
    assert.equal(report, f.reportA);
    assert.equal(checkpoint, f.checkpointA);
    ++persistedA;
  });
  assert.equal(persistedA, 1);
  assert.equal(f.context.beforeClose(), true);
  await f.context.archiveClose(f.close, async receipt => {
    assert.deepEqual(receipt, f.close);
    ++persistedClose;
  });
  assert.equal(persistedClose, 1);
  assert.equal(f.context.afterClose(), true);
  f.context.acceptRestart({...f.close, restarted: true});
  const reportB = {scenario: {matched_setup_cf: null}};
  const checkpointB = Object.freeze({owner: 'B'});
  assert.deepEqual(f.context.beginB(reportB, checkpointB), {
    report: reportB, checkpoint: checkpointB, priorWorldGeneration: 3,
  });
  assert.equal(f.context.status().phase, 'B');
  assert.equal(f.context.status().archivedA, JSON.stringify(f.reportA));
});

test('incomplete A cannot be archived and changed A refuses close and B admission', async () => {
  const incomplete = fixture();
  incomplete.reportA.scenario.matched_setup_cf.actions.pop();
  await assert.rejects(incomplete.context.archiveA(async () => {}), /complete lifetime A/);
  assert.equal(incomplete.context.status().phase, 'A');

  const changed = fixture();
  await changed.context.archiveA(async () => {});
  changed.reportA.scenario.late_mutation = true;
  assert.equal(changed.context.beforeClose(), false);
  await assert.rejects(changed.context.archiveClose(changed.close, async () => {}), /exact archived A native closure/);
  assert.throws(() => changed.context.acceptRestart({...changed.close, restarted: true}), /exact returned acknowledged/);
});

test('close and restart receipts must preserve archived generation and owner values', async () => {
  const f = fixture();
  await f.context.archiveA(async () => {});
  await assert.rejects(f.context.archiveClose({...f.close, prior_world_generation: 4}, async () => {}),
    /exact archived A native closure/);
  await f.context.archiveClose(f.close, async () => {});
  assert.throws(() => f.context.acceptRestart({...f.close, restarted: true, host_seed: 0x5679}),
    /exact returned acknowledged/);
  assert.equal(f.context.status().phase, 'A-closed-persisted');
  f.context.acceptRestart({...f.close, restarted: true});
});

test('B must use fresh report, scenario, and checkpoint owners', async () => {
  const f = fixture();
  await f.context.archiveA(async () => {});
  await f.context.archiveClose(f.close, async () => {});
  f.context.acceptRestart({...f.close, restarted: true});
  await assert.rejects(async () => f.context.beginB(f.reportA,
    Object.freeze({owner: 'B'})), /distinct fresh/);
  await assert.rejects(async () => f.context.beginB({scenario: f.reportA.scenario},
    Object.freeze({owner: 'B'})), /distinct fresh/);
  await assert.rejects(async () => f.context.beginB({scenario: {matched_setup_cf: null}},
    f.checkpointA), /distinct fresh/);
});

// Execute the actual serialized browser callbacks in a separate VM realm. Node
// lexical helper names are intentionally absent from that realm.
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const runnerSource = readFileSync(new URL('./stadium_c1a_browser_test.mjs', import.meta.url), 'utf8');
const between = (start, end) => {
  const a = runnerSource.indexOf(start);assert.ok(a >= 0, start);
  const b = runnerSource.indexOf(end, a + start.length);assert.ok(b > a, end);
  return runnerSource.slice(a + start.length, b);
};
function pageCallback(name) {
  return between(`const ${name} = await timeout(page.evaluate(`,
    name === 'close' ? "}, recreationFullPair), Math.min(LIMITS.unloadMs, remaining()), 'Checked native close of lifetime A'" :
      "}), Math.min(5000, remaining()), 'Restart ordinary ownerless loop on the retained instance'") + '}';
}
function browserFixture({idleAfter = true, pending = null} = {}) {
  let idle = true, calls = 0, getters = 0;
  const receipt = {closed: true, restarted: false, error: null};
  const Module = {
    stadiumMatchedSetupNativeIdle: () => idle,
    _melee_web_native_menu_stadium_recreation_close() {},
    _melee_web_native_menu_stadium_recreation_close_full_pair() {},
    _melee_web_native_menu_stadium_recreation_restart() {},
    _melee_web_native_menu_stadium_recreation_status() { getters++; return 1; },
    UTF8ToString() { return JSON.stringify(receipt); },
    __meleeWebStadiumRecreationIdentityV1: {compare: () => ({same_instance: true})},
    ccall(name, result, types, args, options) {
      assert.equal(options.async, true);calls++;idle = false;
      const operation = pending ?? Promise.resolve(1);
      return operation.then(value => {idle = idleAfter;receipt.restarted = name.endsWith('restart');return value;});
    },
  };
  const browser = vm.createContext({Module, menuOwnerStopped: () => false,
    __meleeWebStadiumRecreationCanary: {beforeClose: () => true, afterClose: () => true}});
  return {browser, Module, counts: () => ({calls, getters}), setIdle(value) {idle = value;}};
}

test('actual close/restart evaluate callbacks have no Node lexical dependency and await idle', async () => {
  for (const kind of ['close', 'restart']) for (const fullPair of [false,true]) {
    const f = browserFixture();const callback = vm.runInContext(`(${pageCallback(kind)})`, f.browser);
    const result = await callback(fullPair);assert.equal(result.result, 1);assert.equal(result.identity.same_instance, true);
    assert.deepEqual(f.counts(), {calls: 1, getters: 1});
  }
});

test('actual callbacks refuse pre-entry idle false and never read status while yielded/failed', async () => {
  for (const kind of ['close', 'restart']) {
    const f = browserFixture();f.setIdle(false);
    await assert.rejects(vm.runInContext(`(${pageCallback(kind)})`, f.browser)());
    assert.deepEqual(f.counts(), {calls: 0, getters: 0});
    const after = browserFixture({idleAfter: false});
    await assert.rejects(vm.runInContext(`(${pageCallback(kind)})`, after.browser)(), /Asyncify idle/);
    assert.deepEqual(after.counts(), {calls: 1, getters: 0});
    const rejection = browserFixture({pending: Promise.reject(Error('lost transport'))});
    await assert.rejects(vm.runInContext(`(${pageCallback(kind)})`, rejection.browser)(), /lost transport/);
    assert.deepEqual(rejection.counts(), {calls: 1, getters: 0});
  }
});

test('fresh B projection admits null prerequisites but completed A still requires checked Setup', async () => {
  const body = between('function recreationApprovalProjection(value) {', '\nasync function installRecreationCanaryContext');
  const project = vm.runInNewContext(`(function(value){${body})`);
  const fresh = project({scenario: {matched_setup_cf: null, first_sss_vs_setup: null}});
  assert.equal(fresh.scenario.first_sss_vs_setup.observation, null);
  const f = fixture();await f.context.archiveA(async () => {});await f.context.archiveClose(f.close, async () => {});
  f.context.acceptRestart({...f.close, restarted: true});assert.equal(f.context.beginB(fresh, {}).report, fresh);
  const invalid = fixture();invalid.reportA.scenario.first_sss_vs_setup.observation = null;
  await assert.rejects(invalid.context.archiveA(async () => {}));
});

test('actual B generation guard rejects reused/older/missing generations', () => {
  const body = between('    const generation = native.first_sss_vs_setup.match_world_generation;', '\n    const admitted = await timeout(page.evaluate(');
  const guard = vm.runInNewContext(`(function(native,recreationCloseReceipt){const generation=native.first_sss_vs_setup.match_world_generation;${body}})`, {assert});
  guard({first_sss_vs_setup:{match_world_generation:4}}, {prior_world_generation:3});
  for (const generation of [3,2,0,undefined,4.5]) assert.throws(() =>
    guard({first_sss_vs_setup:{match_world_generation:generation}}, {prior_world_generation:3}));
});

test('actual callback scope only disposes exact claimed descriptors and retains foreign replacements', async () => {
  const browser = vm.createContext({});
  const host = vm.createContext({
    page: {evaluate(fn,args) {return vm.runInContext(`(${fn.toString()})`, browser)(args);}},
    timeout: value => value, remaining: () => 1000,
    recreationCallbackNames: ['owned', 'baseline'], recreationHelperKey: 'helper', recreationCallbackScopeKey: 'scope',
  });
  const beginBody = between('async function beginRecreationCallbackScope(label) {', '\nasync function disposeRecreationCallbackScope');
  const begin = vm.runInContext(`(async function(label){${beginBody})`, host);
  await begin('A');
  vm.runInContext(`globalThis.owned=()=>1;scope.claimInstalled('owned',owned);`,browser);
  const owned = browser.owned;
  vm.runInContext(`globalThis.owned=()=>2;`,browser);const foreign=browser.owned;
  assert.throws(() => browser.scope.dispose('A'), /Foreign callback/);assert.equal(browser.owned,foreign);
  browser.owned=owned;const disposed=browser.scope.dispose('A');assert.equal(disposed.changed[0],'owned');assert.equal(browser.owned,undefined);
  await begin('B');vm.runInContext(`globalThis.owned=()=>3;scope.claimInstalled('owned',owned);scope.disposeOne('owned',owned);`,browser);
  assert.equal(browser.owned,undefined);assert.equal(browser.scope.dispose('B').changed.length,0);
});

import fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';

test('actual supervisor publication retains full report attribution shape in every canary phase', async () => {
  const root = await fs.mkdtemp(path.join(process.env.MELEE_STADIUM_RECREATION_TEST_ROOT ?? os.tmpdir(), 'canary-publication-'));
  try {
    const body = between('async function publishRecreationSupervisorReport() {', '\nasync function persistRecreationSummary');
    for (const phase of ['early-A', 'A-close-control', 'B-failed', 'B-success']) {
      const inventoryOutput = path.join(root, phase + '.json');
      // Explicit shape fixture only: no process attribution/cleanup is claimed.
      const report = {schema:'melee-web-stadium-c1a-browser-v1', result:phase==='B-success'?'pass':'fail',
        browser_process_info:[{id:123,type:'browser'}],browser_process_groups:[{pgid:123}],
        browser_process_ps:[{pid:123}],browser_process_start:[{pid:123,start_identity:'fixture'}],
        browser_process_cleanup:[{pid:123,absent:true}],browser_process_group_cleanup:[{pgid:123,absent:true}],
        cleanup:{server_process_pid:456,browser_process_pid:123},scenario:{recreation_canary:{phase}}};
      const bytes=JSON.stringify(report)+'\n';await fs.writeFile(inventoryOutput,bytes);
      const publish=vm.runInNewContext(`(async function(){${body})`,{fs,path,assert,output:root,inventoryOutput});
      await publish();assert.equal(await fs.readFile(path.join(root,'report.json'),'utf8'),bytes);
      assert.deepEqual(JSON.parse(await fs.readFile(path.join(root,'report.json'),'utf8')),report);
      assert.equal(await fs.readFile(inventoryOutput,'utf8'),bytes);
    }
    // Summary sidecar can never replace the supervisor's report path.
    const summaryBody=between('async function persistRecreationSummary() {','\nasync function persistRecreationProgress');
    assert.ok(summaryBody.includes("path.join(output, 'recreation-summary.json')"));
    assert.equal(summaryBody.includes("path.join(output, 'report.json')"),false);
  } finally {await fs.rm(root,{recursive:true,force:true});}
});

test('actual private identity postset binds Module/exports/Memory and wrappers independently of memory growth', () => {
  const librarySource=readFileSync(new URL('../src/stadium_recreation_identity_library.js',import.meta.url),'utf8');
  let library;const memory=new WebAssembly.Memory({initial:1,maximum:3});
  const Module={HEAPU8:new Uint8Array(memory.buffer)};
  for(const name of ['_main','_melee_web_native_menu_stadium_recreation_status','_melee_web_native_menu_stadium_recreation_close',
    '_melee_web_native_menu_stadium_recreation_restart','_melee_web_native_menu_stadium_recreation_admit_b'])Module[name]=()=>{};
  const context=vm.createContext({Module,wasmExports:{native:()=>{}},wasmMemory:memory,WebAssembly,Uint8Array,
    addToLibrary(value){library=value;}});
  vm.runInContext(librarySource,context);vm.runInContext(library.$MeleeWebStadiumRecreationIdentityV1__postset,context);
  const api=Module.__meleeWebStadiumRecreationIdentityV1;assert.equal(api.capture().captured,true);assert.equal(api.compare().same_instance,true);
  memory.grow(1);Module.HEAPU8=new Uint8Array(memory.buffer);
  const grown=api.compare();assert.equal(grown.same_instance,true);assert.equal(grown.memory_buffer_changed,true);
  const wrapper=Module._melee_web_native_menu_stadium_recreation_admit_b;
  Module._melee_web_native_menu_stadium_recreation_admit_b=()=>{};assert.equal(api.compare().same_instance,false);
  Module._melee_web_native_menu_stadium_recreation_admit_b=wrapper;
  const exports=context.wasmExports;context.wasmExports={native:()=>{}};assert.equal(api.compare().same_instance,false);context.wasmExports=exports;
  context.Module={...Module};assert.equal(api.compare().same_instance,false);
});


test('actual progress publication persists control before hash and preserves final-only decision semantics', async () => {
  const root=await fs.mkdtemp(path.join(process.env.MELEE_STADIUM_RECREATION_TEST_ROOT??os.tmpdir(),'canary-progress-'));
  let passed=false;
  try {
    const checkpointModule=await import('./stadium_c3_report_checkpoint.mjs');
    const crypto=await import('node:crypto');
    const hash=bytes=>crypto.createHash('sha256').update(bytes).digest('hex');
    const persistBody=between('const persistReport = async () => {','\n};');
    const publishBody=between('async function publishRecreationSupervisorReport() {','\nasync function persistRecreationSummary');
    const progressBody=between('async function persistRecreationProgress() {','\nasync function beginRecreationCallbackScope');
    const finalBody=between('async function persistRecreationSummary() {','\nasync function persistRecreationProgress');
    const decisionExpression=between('const recreationPassDecision =',';\nlet recreationLifetime');
    for (const recreationFullB of [false,true]) for (const failAt of [null,'control-write','control-rename','summary-write','summary-rename']) {
      const output=path.join(root,(recreationFullB?'fullB-':'prefix-')+(failAt??'positive'));await fs.mkdir(output);
      const inventoryOutput=path.join(output,'recreation-control.json');
      const events=[];let reachedRestart=false;
      const controlledFs={...fs,
        async writeFile(file,...args){const name=path.basename(file);events.push('write:'+name);if((failAt==='control-write'&&name==='recreation-control.json.tmp')||(failAt==='summary-write'&&name==='recreation-summary.json.tmp'))throw Error(failAt);return fs.writeFile(file,...args);},
        async rename(from,to){const name=path.basename(to);events.push('rename:'+name);if((failAt==='control-rename'&&name==='recreation-control.json')||(failAt==='summary-rename'&&name==='recreation-summary.json'))throw Error(failAt);return fs.rename(from,to);}};
      const report={result:'fail',execution_state:'running',cleanup:{failures:[],browser_process_terminated:false,server_process_terminated:false},scenario:{recreation_canary:{lifetime:'A-close-control'}}};
      const summary={schema:'melee-web-stadium-recreation-canary-summary-v1',result:'running',decision:null,close_receipt:{host_settlement:{accepted:true}}};
      const scope={fs:controlledFs,path,assert,output,inventoryOutput,report,recreationSummary:summary,recreationCanary:true,failure:null,matchedSetupCfTiming:null,
        recreationPassDecision:vm.runInNewContext(`(${decisionExpression})`,{recreationFullB,recreationFullPair:false}),
        stadiumC3ReportCheckpoint:checkpointModule.createStadiumC3ReportCheckpoint({reportPath:inventoryOutput,fileSystem:controlledFs}),
        hashFile:async file=>{events.push('hash:'+path.basename(file));return hash(await fs.readFile(file));}};
      assert.equal(scope.recreationPassDecision,recreationFullB
        ? 'PASS_SAME_INSTANCE_A174_B7167_COMMON_FIELDS_SEED_COMPONENT_ONLY'
        : 'PASS_SAME_INSTANCE_174_ACTION_RECREATION_COMPONENT_ONLY');
      vm.createContext(scope);
      vm.runInContext(`async function publishRecreationSupervisorReport(){${publishBody}\nconst persistReport=async()=>{${persistBody}\n};async function persistRecreationProgress(){${progressBody}\nasync function persistRecreationSummary(){${finalBody}`,scope);
      const operation=async()=>{await vm.runInContext('persistRecreationProgress()',scope);reachedRestart=true;};
      if(failAt){await assert.rejects(operation,new RegExp(failAt));assert.equal(reachedRestart,false);}
      else {
        await operation();assert.equal(reachedRestart,true);
        const bytes=await fs.readFile(inventoryOutput);assert.deepEqual(await fs.readFile(path.join(output,'report.json')),bytes);
        const published=JSON.parse(await fs.readFile(path.join(output,'recreation-summary.json'),'utf8'));
        assert.equal(published.result,'running');assert.equal(published.decision,null);assert.equal(published.cleanup,undefined);assert.equal(published.final_report,undefined);assert.equal(published.progress_report.sha256,hash(bytes));
        assert.ok(events.indexOf('rename:recreation-control.json')<events.indexOf('hash:recreation-control.json'));
        assert.equal(summary.result,'running');assert.equal(summary.decision,null);
        scope.report.result='pass';scope.report.execution_state='completed';scope.report.cleanup.browser_process_terminated=true;scope.report.cleanup.server_process_terminated=true;
        summary.final_archive_integrity={result:'pass'};summary.decision=scope.recreationPassDecision;
        await vm.runInContext('persistReport();',scope);await vm.runInContext('persistRecreationSummary()',scope);
        const terminal=JSON.parse(await fs.readFile(path.join(output,'recreation-summary.json'),'utf8'));assert.equal(terminal.result,'pass');assert.equal(terminal.cleanup.browser_process_terminated,true);assert.equal(terminal.final_report.sha256,hash(await fs.readFile(inventoryOutput)));
      }
    }
    passed=true;
  } finally {if(passed)await fs.rm(root,{recursive:true,force:true});}
});


test('full-pair A archive requires exact full counts/typed version/comparisons and preserves prefix default', async () => {
  const f=fixture(true);await f.context.archiveA(async()=>{});
  await f.context.archiveClose(f.close,async()=>{});f.context.acceptRestart({...f.close,restarted:true});
  assert.equal(f.context.beginB({scenario:{matched_setup_cf:null}},Object.freeze({owner:'B'})).priorWorldGeneration,3);
  for(const mutate of [cf=>cf.actions.pop(),cf=>cf.actions_completed=174,
    cf=>cf.decision='PASS_RUN2_PROPER_PREFIX_C_F_STATE_ONLY',cf=>cf.common_field_comparisons_passed=3500,
    cf=>cf.seed_value_comparisons_passed=3500,cf=>cf.exact_value_comparisons_passed=56015,
    cf=>cf.last_native_state.schema='melee-web-stadium-source-action-native-v3',
    cf=>cf.last_native_state.source_ticks=3584,cf=>cf.last_native_state.source_draws=3581]){
    const bad=fixture(true);mutate(bad.reportA.scenario.matched_setup_cf);
    await assert.rejects(bad.context.archiveA(async()=>{throw Error('must not persist');}),/complete lifetime A/);
    assert.equal(bad.context.beforeClose(),false);
  }
  const defaultFull=fixture(true);
  const defaultContext=createStadiumRecreationCanaryContext({reportA:defaultFull.reportA,checkpointA:defaultFull.checkpointA});
  await assert.rejects(defaultContext.archiveA(async()=>{}),/complete lifetime A/);
  const prefix=fixture();
  const fullContext=createStadiumRecreationCanaryContext({reportA:prefix.reportA,checkpointA:prefix.checkpointA,fullPair:true});
  await assert.rejects(fullContext.archiveA(async()=>{}),/complete lifetime A/);
});
