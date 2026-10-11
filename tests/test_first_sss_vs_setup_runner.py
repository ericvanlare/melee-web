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
  route=source[source.index('  if (firstSssVsSetup) assert.equal',source.index('async function main()')):source.index('  assert.equal(preflight.source_revision',source.index('async function main()'))]
  js="import assert from 'node:assert/strict';import {parseArgs} from 'node:util';import {pathToFileURL} from 'node:url';\n"
  js+='const cli='+repr(cli)+',route='+repr(route)+';\n'
  js+=_function(source,'function assertFirstSssVsSetupHistory(native, before)')
  js+=_function(source,'function assertFirstSssVsSetupOwners(setup)')
  js+=_function(source,'async function readFirstSssVsEntryObservation()')
  js+='const expectedPath='+repr(os.environ.get('MELEE_WEB_SSS_VS_SETUP_EXPECTED',''))+';\n'
  js+='const helperPath='+repr(os.environ.get('MELEE_WEB_SSS_VS_SETUP_COMPARATOR',''))+';\n'
  js+='const loaderSource='+repr(_function(source,'async function loadFirstSssVsSetupInputs()'))+';\n'
  native=(ROOT/'src/gameplay_menu_browser.cpp').read_text()
  begin=_function(native,'int melee_web_native_menu_stadium_first_sss_vs_setup_begin()')
  a=begin.index('check(EM_ASM_INT({')+len('check(EM_ASM_INT({')
  b=begin.index('})==1,',a)
  js+='const approvalSource='+repr(begin[a:b])+';\n'

  js+=r'''
const approve=new Function('globalThis',approvalSource);
assert.equal(approve({}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>false}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>{throw Error('refused');}}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>Promise.resolve(true)}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>1}),0);assert.equal(approve({__meleeWebStadiumFirstSssVsEntryApproved:()=>true}),1);
const parse=args=>new Function('parseArgs','args',cli+';return {values,firstSssVsSetup,firstSssVsEntry,firstSssPrefix,firstSssConsumedPadTick,firstSssConstructorPair,firstCssBrowserDraw,firstCssBrowserStream,firstCssBrowserFinalDraw};')(parseArgs,args);
const args=['--first-sss-vs-setup'];for(const name of ['first-sss-vs-setup-expected','first-sss-vs-entry-expected','first-sss-prefix-expected','first-sss-prefix-input','first-sss-draw-expected','first-sss-pair-expected','first-sss-tick-expected','first-sss-tick-input'])args.push('--'+name,'fixture');
const flags=parse(args);assert.equal(flags.firstSssVsSetup,true);assert.equal(flags.firstSssPrefix,true);
for(const missing of ['first-sss-vs-setup-expected','first-sss-vs-entry-expected','first-sss-prefix-input']){const bad=[...args],i=bad.indexOf('--'+missing);bad.splice(i,2);assert.throws(()=>parse(bad));}
assert.throws(()=>parse([...args,'--first-sss-vs-entry']));assert.throws(()=>parse(['--first-sss-vs-setup-expected','orphan']));
const names=['assert','preflight',...Object.keys(flags)];const routeCheck=new Function(...names,route);
routeCheck(assert,{route_mode:'first-sss-vs-setup'},...Object.values(flags));assert.throws(()=>routeCheck(assert,{route_mode:'first-sss-prefix'},...Object.values(flags)));
const before={running:0,native_phase:20,first_sss_prefix:{complete:true},first_sss_vs_entry:{complete:true}};
const native={...structuredClone(before),native_phase:21,first_sss_vs_setup:{attempted:true}};
let readCalls=0;async function readFirstSssPrefixObservation(){++readCalls;return {native};}
assert.deepEqual(await readFirstSssVsEntryObservation(),native);assert.equal(readCalls,1);
assertFirstSssVsSetupHistory(native,before);assert.throws(()=>assertFirstSssVsSetupHistory({...native,running:1},before));assert.throws(()=>assertFirstSssVsSetupHistory({...native,first_sss_prefix:{complete:false}},before));
const setup={menu_retired:true,parent_seed_owner_value_restored:true,parent_seed_before_retirement:17,parent_seed_after_retirement:17,requested_generation:8,committed_generation:8,match_world_generation:9,prior_world_generation:7,audio_render_calls_before:274,audio_render_calls_after:274,audio_render_frames_before:146133,audio_render_frames_after:146133,current_owners:{host:true,match:true,cache:true,source_world_exists:true,menu_world:false,host_entered:false,running:false,pending:false,first_use_draw_pending:false,host_phase:5,source_world_generation:9,pending_scope_generation:0,asset_generation:8,active_files:2}};
assertFirstSssVsSetupOwners(setup);for(const key of ['parent_seed_owner_value_restored','menu_retired'])assert.throws(()=>assertFirstSssVsSetupOwners({...setup,[key]:false}));assert.throws(()=>assertFirstSssVsSetupOwners({...setup,audio_render_calls_after:275}));assert.throws(()=>assertFirstSssVsSetupOwners({...setup,current_owners:{...setup.current_owners,pending:true}}));
if(expectedPath){
 const fs=await import('node:fs/promises');const expected=JSON.parse(await fs.readFile(expectedPath,'utf8'));
 const entry={expected:{provenance:structuredClone(expected.provenance)}};
 const preflight={first_sss_vs_setup:{diagnostic_export:'_melee_web_native_menu_stadium_first_sss_vs_setup_begin',expected_state_host_only:true}};
 const bound=async()=>({expected,helperPath});
 const load=new Function('loadBoundFirstSssExpected','firstSssVsSetup','preflight','firstSssVsEntryInputs','pathToFileURL','assert',loaderSource+';return loadFirstSssVsSetupInputs();');
 await load(bound,true,preflight,entry,pathToFileURL,assert);
 await assert.rejects(()=>load(bound,true,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,diagnostic_export:'wrong'}},entry,pathToFileURL,assert));
 await assert.rejects(()=>load(bound,true,{first_sss_vs_setup:{...preflight.first_sss_vs_setup,expected_state_host_only:false}},entry,pathToFileURL,assert));
 entry.expected.provenance.observer_sha256='wrong';await assert.rejects(()=>load(bound,true,preflight,entry,pathToFileURL,assert));
 console.log('PASS actual retained Setup expected through actual loader; wrong export/host-only/provenance refusals');
}
console.log('PASS actual Setup CLI/route/reader/history/owner controls; no browser');
'''
  w=self.new_workspace(ROOT,'setup-runner-');p=w/'control.mjs';p.write_text(js)
  r=subprocess.run([str(node_runtime()),str(p)],capture_output=True,text=True);(w/'run.log').write_text(r.stdout+r.stderr);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
