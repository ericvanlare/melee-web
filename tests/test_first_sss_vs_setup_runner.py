"""Asset-free actual Setup CLI/loader/reader/history contracts; no browser."""
from pathlib import Path
import os,subprocess,unittest
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
from check_gameplay import node_runtime
ROOT=Path(__file__).resolve().parents[1]
class FirstSssVsSetupRunnerTests(OwnedWorkspaceTests):
 def test_actual_setup_runner_contract(self):
  source=(ROOT/'tests/stadium_c1a_browser_test.mjs').read_text()
  cli=source[source.index('const {values} = parseArgs'):source.index('const LIMITS =')]
  self.assertEqual(cli.count('parseArgs({options:'),1)
  cli=cli.replace('parseArgs({options:','parseArgs({args, options:')
  route=source[source.index('  if (firstSssVsSetup) {',source.index('async function main()')):source.index('  assert.equal(preflight.source_revision',source.index('async function main()'))]
  js="import assert from 'node:assert/strict';import {parseArgs} from 'node:util';import {pathToFileURL} from 'node:url';\n"
  js+="import {validateMatchedSetupCfRequest} from "+repr((ROOT/'tests/stadium_matched_setup_cf_runner_gate.mjs').as_uri())+";\n"
  js+='const cli='+repr(cli)+',route='+repr(route)+';\n'
  js+=_function(source,'function validateFirstSssTextTailLog(snapshot)')
  runtime=(ROOT/'web/runtime-development.mjs').read_text()
  sink=runtime[runtime.index('const STADIUM_TEXT_TAIL_MAX_RECORDS='):runtime.index("if(typeof PerformanceObserver",runtime.index('const STADIUM_TEXT_TAIL_MAX_RECORDS='))]
  js+='const actualSinkSource='+repr(sink)+';\n'
  # This extracted reader is exercised on the default-off, non-explicit route.
  js+='const firstSssVsSetupCfExplicitStep=false; let matchedSetupCfExplicitAsyncifyUnsafe=false;\n'

  js+=_function(source,'function assertFirstSssVsSetupHistory(native, before)')
  js+=_function(source,'function assertFirstSssVsSetupOwners(setup)')
  js+=_function(source,'async function readFirstSssVsEntryObservation(matchedActionOnly = false)')
  js+='const expectedPath='+repr(os.environ.get('MELEE_WEB_SSS_VS_SETUP_EXPECTED',''))+';\n'
  js+='const helperPath='+repr(os.environ.get('MELEE_WEB_SSS_VS_SETUP_COMPARATOR',''))+';\n'
  js+='const loaderSource='+repr(_function(source,'async function loadFirstSssVsSetupInputs()'))+';\n'
  native=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
  begin=_function(native,'int melee_web_native_menu_stadium_first_sss_vs_setup_begin(int text_tail_probe)')
  a=begin.index('check(EM_ASM_INT({')+len('check(EM_ASM_INT({')
  b=begin.index('})==1,',a)
  js+='const approvalSource='+repr(begin[a:b])+';\n'

  js+=r'''
const approve=new Function('globalThis',approvalSource);
assert.equal(approve({}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>false}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>{throw Error('refused');}}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>Promise.resolve(true)}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>1}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>true}),1);
const parse=args=>new Function('parseArgs','args','validateMatchedSetupCfRequest',cli+';return {values,firstSssVsSetup,firstSssVsSetupTextTail,firstSssVsSetupCf,firstSssVsEntry,firstSssPrefix,firstSssConsumedPadTick,firstSssConstructorPair,firstCssBrowserDraw,firstCssBrowserStream,firstCssBrowserFinalDraw};')(parseArgs,args,validateMatchedSetupCfRequest);
const args=['--first-sss-vs-setup'];for(const name of ['first-sss-vs-setup-expected','first-sss-vs-entry-expected','first-sss-prefix-expected','first-sss-prefix-input','first-sss-draw-expected','first-sss-pair-expected','first-sss-tick-expected','first-sss-tick-input'])args.push('--'+name,'fixture');
const flags=parse(args);assert.equal(flags.firstSssVsSetup,true);assert.equal(flags.firstSssVsSetupCf,false);assert.equal(flags.firstSssPrefix,true);
for(const missing of ['first-sss-vs-setup-expected','first-sss-vs-entry-expected','first-sss-prefix-input']){const bad=[...args],i=bad.indexOf('--'+missing);bad.splice(i,2);assert.throws(()=>parse(bad));}
assert.throws(()=>parse([...args,'--first-sss-vs-entry']));assert.throws(()=>parse(['--first-sss-vs-setup-expected','orphan']));
const names=['assert','preflight',...Object.keys(flags)];const routeCheck=new Function(...names,route);
routeCheck(assert,{route_mode:'first-sss-vs-setup',first_sss_vs_setup:{text_tail_probe:false}},...Object.values(flags));
routeCheck(assert,{route_mode:'first-sss-vs-setup',first_sss_vs_setup:{}},...Object.values(flags));
assert.throws(()=>routeCheck(assert,{route_mode:'first-sss-vs-setup',first_sss_vs_setup:{text_tail_probe:null}},...Object.values(flags)));
assert.throws(()=>routeCheck(assert,{route_mode:'first-sss-prefix'},...Object.values(flags)));
const tailFlags=parse([...args,'--first-sss-vs-setup-text-tail']);assert.equal(tailFlags.firstSssVsSetupTextTail,true);
routeCheck(assert,{route_mode:'first-sss-vs-setup',first_sss_vs_setup:{text_tail_probe:true}},...Object.values(tailFlags));
assert.throws(()=>routeCheck(assert,{route_mode:'first-sss-vs-setup',first_sss_vs_setup:{text_tail_probe:false}},...Object.values(tailFlags)));
assert.throws(()=>parse(['--first-sss-vs-setup-text-tail']));
assert.throws(()=>parse([...args,'--first-sss-vs-setup-text-tail','--first-sss-vs-entry']));

const sinkWindow={};new Function('window','TextEncoder',actualSinkSource)(sinkWindow,TextEncoder);
const sink=sinkWindow.__meleeWebStadiumTextTailProbe;
const opaque='C1_SIS_TEXT_EVENT event=append text=0xdeadbeef tail=0x0 font=2 context=-1 é';
sink.observe(opaque);assert.equal(sink.snapshot().retained_records,0);assert.equal(sink.snapshot().enabled,false);
assert.equal(sink.enable(),true);sink.observe('unrelated event');sink.observe(opaque);
let sinkSnapshot=sink.snapshot();assert.deepEqual(sinkSnapshot.records,[opaque]);assert.equal(sinkSnapshot.retained_bytes,Buffer.byteLength(opaque,'utf8'));assert.equal(sinkSnapshot.total_records,1);assert.equal(validateFirstSssTextTailLog(sinkSnapshot).complete,true);
sinkSnapshot.records.push('external mutation');assert.deepEqual(sink.snapshot().records,[opaque]);
sink.disable();sink.observe(opaque);assert.equal(sink.snapshot().retained_records,1);assert.equal(sink.snapshot().enabled,false);
sink.enable();const small='C1_SIS_TEXT_EVENT x';for(let i=0;i<8192;i++)sink.observe(small);sink.observe(small);
sinkSnapshot=sink.snapshot();assert.equal(sinkSnapshot.retained_records,8192);assert.equal(sinkSnapshot.total_records,8193);assert.equal(sinkSnapshot.dropped_records,1);assert.equal(sinkSnapshot.dropped_bytes,Buffer.byteLength(small));assert.equal(sinkSnapshot.overflow,true);assert.equal(validateFirstSssTextTailLog(sinkSnapshot).state,'OPEN');
sink.enable();const prefix='C1_SIS_TEXT_EVENT ';const exactByteRecord=prefix+'x'.repeat(1024*1024-Buffer.byteLength(prefix));sink.observe(exactByteRecord);sink.observe(opaque);
sinkSnapshot=sink.snapshot();assert.equal(sinkSnapshot.retained_records,1);assert.equal(sinkSnapshot.retained_bytes,1024*1024);assert.equal(sinkSnapshot.dropped_records,1);assert.equal(sinkSnapshot.dropped_bytes,Buffer.byteLength(opaque,'utf8'));assert.equal(sinkSnapshot.total_bytes,1024*1024+Buffer.byteLength(opaque,'utf8'));assert.equal(sinkSnapshot.overflow,true);
sink.enable();sink.observe(prefix+'é'.repeat(1024*1024));sinkSnapshot=sink.snapshot();assert.equal(sinkSnapshot.retained_records,0);assert.equal(sinkSnapshot.dropped_records,1);assert.equal(sinkSnapshot.overflow,true);
sink.reset();sinkSnapshot=sink.snapshot();assert.equal(sinkSnapshot.enabled,false);for(const key of ['retained_records','retained_bytes','total_records','total_bytes','dropped_records','dropped_bytes'])assert.equal(sinkSnapshot[key],0);assert.deepEqual(sinkSnapshot.records,[]);assert.equal(sinkSnapshot.overflow,false);
// Empty is transport-valid after enable, never proof of a text creator or owner.
sink.enable();assert.equal(validateFirstSssTextTailLog(sink.snapshot()).state,'retained');sink.reset();
const textRecord='C1_SIS_TEXT_EVENT {\"event\":\"append\"}';
const log={schema:'melee-web-stadium-first-sss-text-tail-log-v1',enabled:true,records:[textRecord],retained_records:1,retained_bytes:Buffer.byteLength(textRecord),total_records:1,total_bytes:Buffer.byteLength(textRecord),dropped_records:0,dropped_bytes:0,overflow:false,limits:{records:8192,bytes:1024*1024}};
assert.deepEqual(validateFirstSssTextTailLog(log),{state:'retained',complete:true,errors:[],retained_records:1,retained_bytes:Buffer.byteLength(textRecord),dropped_records:0,dropped_bytes:0,overflow:false});
assert.equal(validateFirstSssTextTailLog(null).state,'OPEN');
assert.equal(validateFirstSssTextTailLog({...log,total_bytes:0}).state,'OPEN');
assert.equal(validateFirstSssTextTailLog({...log,dropped_records:1,dropped_bytes:4,total_records:2,total_bytes:log.retained_bytes+4,overflow:true}).state,'OPEN');
assert.equal(validateFirstSssTextTailLog({...log,records:['unrelated text'],retained_bytes:14,total_bytes:14}).state,'OPEN');
assert.equal(validateFirstSssTextTailLog({...log,enabled:false}).state,'OPEN');
const before={running:0,native_phase:20,first_sss_prefix:{complete:true},first_sss_vs_entry:{complete:true}};
const native={...structuredClone(before),native_phase:21,first_sss_vs_setup:{attempted:true}};
let readCalls=0;const calls={observe:0,utf8:0,phase:0,running:0,host:0};
const page={async evaluate(callback){++readCalls;return callback();}};
globalThis.Module={
 _melee_web_native_menu_stadium_first_css_draw_observe(){++calls.observe;return 1;},
 UTF8ToString(pointer){++calls.utf8;assert.equal(pointer,1);return JSON.stringify(native);},
 _melee_web_native_menu_phase(){++calls.phase;return 21;},
 _melee_web_native_menu_running(){++calls.running;return 0;},
};
globalThis.__meleeWebStadiumFirstSssPrefixRead=()=>{++calls.host;return {rows:[{phase:'complete',actual_json:'host history'}]};};
assert.deepEqual(await readFirstSssVsEntryObservation(),native);assert.equal(readCalls,1);
assert.deepEqual(calls,{observe:1,utf8:1,phase:1,running:1,host:1});
assertFirstSssVsSetupHistory(native,before);assert.throws(()=>assertFirstSssVsSetupHistory({...native,running:1},before));assert.throws(()=>assertFirstSssVsSetupHistory({...native,first_sss_prefix:{complete:false}},before));
const setup={menu_retired:true,parent_seed_owner_value_restored:true,parent_seed_before_retirement:17,parent_seed_after_retirement:17,requested_generation:8,committed_generation:8,match_world_generation:9,prior_world_generation:7,audio_render_calls_before:274,audio_render_calls_after:274,audio_render_frames_before:146133,audio_render_frames_after:146133,current_owners:{host:true,match:true,cache:true,source_world_exists:true,menu_world:false,host_entered:false,running:false,pending:false,first_use_draw_pending:false,host_phase:5,source_world_generation:9,pending_scope_generation:0,asset_generation:8,active_files:2}};
assertFirstSssVsSetupOwners(setup);for(const key of ['parent_seed_owner_value_restored','menu_retired'])assert.throws(()=>assertFirstSssVsSetupOwners({...setup,[key]:false}));assert.throws(()=>assertFirstSssVsSetupOwners({...setup,audio_render_calls_after:275}));assert.throws(()=>assertFirstSssVsSetupOwners({...setup,current_owners:{...setup.current_owners,pending:true}}));
if(expectedPath){
 const fs=await import('node:fs/promises');const expected=JSON.parse(await fs.readFile(expectedPath,'utf8'));
 const entry={expected:{provenance:structuredClone(expected.provenance)}};
 const preflight={first_sss_vs_setup:{diagnostic_export:'_melee_web_native_menu_stadium_first_sss_vs_setup_begin',expected_state_host_only:true}};
 const bound=async()=>({expected,helperPath});
 const load=new Function('loadBoundFirstSssExpected','firstSssVsSetup','firstSssVsSetupTextTail','preflight','firstSssVsEntryInputs','pathToFileURL','assert',loaderSource+';return loadFirstSssVsSetupInputs();');
 await load(bound,true,false,preflight,entry,pathToFileURL,assert);
 const absentBinding={first_sss_vs_setup:{...preflight.first_sss_vs_setup}};delete absentBinding.first_sss_vs_setup.text_tail_probe;
 await load(bound,true,false,absentBinding,entry,pathToFileURL,assert);
 await assert.rejects(()=>load(bound,true,false,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,text_tail_probe:null}},entry,pathToFileURL,assert));
 await load(bound,true,true,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,text_tail_probe:true}},entry,pathToFileURL,assert);
 await assert.rejects(()=>load(bound,true,true,preflight,entry,pathToFileURL,assert));
 await assert.rejects(()=>load(bound,true,false,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,diagnostic_export:'wrong'}},entry,pathToFileURL,assert));
 await assert.rejects(()=>load(bound,true,false,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,expected_state_host_only:false}},entry,pathToFileURL,assert));
 await assert.rejects(()=>load(bound,true,false,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,text_tail_probe:true}},entry,pathToFileURL,assert));
 entry.expected.provenance.observer_sha256='wrong';await assert.rejects(()=>load(bound,true,false,preflight,entry,pathToFileURL,assert));
 console.log('PASS actual retained Setup expected through actual loader; wrong export/host-only/provenance refusals');
}
console.log('PASS actual Setup CLI/route/reader/history/owner controls; no browser');
'''
  w=self.new_workspace(ROOT,'setup-runner-');p=w/'control.mjs';p.write_text(js)
  r=subprocess.run([str(node_runtime()),str(p)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
class FirstSssVsSetupTextTailChronologyTests(unittest.TestCase):
 def test_actual_cleanup_text_tail_chronology(self):
  node_test=Path(__file__).resolve().with_name('stadium_text_tail_chronology.test.mjs')
  r=subprocess.run([str(node_runtime(ROOT)),str(node_test)],capture_output=True,text=True)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  self.assertIn('PASS actual cleanup chronology before/after unload',r.stdout)
if __name__=='__main__':unittest.main()
