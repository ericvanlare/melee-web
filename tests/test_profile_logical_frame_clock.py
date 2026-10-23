"""Actual prepared clock/flow/cadence with explicit modeled services; no browser."""
from pathlib import Path
import os,sys,json,hashlib,tempfile,subprocess,unittest,shutil
ROOT=Path(os.environ.get('MELEE_LOGICAL_CLOCK_REPO_ROOT',Path(__file__).resolve().parents[1])).resolve()
SOURCE=Path(os.environ.get('MELEE_LOGICAL_CLOCK_CANDIDATE_ROOT',ROOT)).resolve()
PREPARED=Path(os.environ.get('MELEE_LOGICAL_CLOCK_PREPARED_ROOT',ROOT/'build/gameplay-source/src')).resolve()
class LogicalClockControl(unittest.TestCase):
 def _run_actual_clock(self,enabled):
  gm=PREPARED/'melee/gm/gm_1A45.c';cadence=PREPARED/'melee/lb/lb_0195.c'
  if 'MELEE_LOGICAL_CLOCK_PREPARED_ROOT' in os.environ:
   self.assertTrue(gm.is_file() and cadence.is_file(),'Explicit prepared-source override is invalid; no preparation fallback')
  elif not gm.is_file() or not cadence.is_file():
   self.skipTest('Generated original clock/cadence source unavailable; run normal build configure separately')
  if not (ROOT/'.deps/emsdk/upstream/emscripten/emcc').is_file():
   self.skipTest('Pinned Emscripten toolchain unavailable')
  original={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in [gm,cadence,ROOT/'src/gameplay_match_flow.c',ROOT/'src/gameplay_bootstrap.c']}
  output=Path(os.environ.get('MELEE_LOGICAL_CLOCK_OUTPUT_ROOT',ROOT/'work'));output.mkdir(parents=True,exist_ok=True)
  out=Path(tempfile.mkdtemp(prefix='logical-clock-',dir=output));(out/'tmp').mkdir();env=dict(os.environ,TMPDIR=str(out/'tmp'))
  # Apply only the new gm overlay hunk to an external copy; existing prepared profile is immutable.
  patch=(SOURCE/'src/slippi_profile/patches/melee-profile-hooks.patch').read_text()
  marker='diff --git a/src/melee/gm/gm_1A45.c b/src/melee/gm/gm_1A45.c'
  self.assertEqual(patch.count(marker),1)
  tree=out/'source';target=tree/'src/melee/gm/gm_1A45.c';target.parent.mkdir(parents=True);shutil.copyfile(gm,target)
  new_hunk=patch[patch.index(marker):]
  if 'static u32 melee_source_clock_logical_phase;' in target.read_text():
   # Normal post-integration prepared source already includes this overlay.
   reverse=subprocess.run(['git','apply','--reverse','-'],input=new_hunk,cwd=tree,text=True,capture_output=True)
   (out/'reverse-existing.json').write_text(json.dumps({'exit_code':reverse.returncode,'stderr':reverse.stderr},indent=2));self.assertEqual(reverse.returncode,0,reverse.stderr)
  applied=subprocess.run(['git','apply','-'],input=new_hunk,cwd=tree,text=True,capture_output=True)
  (out/'apply.json').write_text(json.dumps({'exit_code':applied.returncode,'stderr':applied.stderr},indent=2));self.assertEqual(applied.returncode,0,applied.stderr)
  flow=out/'flow.c';flow.write_text((ROOT/'src/gameplay_match_flow.c').read_text())
  control=out/'control.c';control.write_text(((SOURCE/'tests/profile_logical_clock_control.c.in').read_text() if enabled else (ROOT/'tests/flow_cpu_checkpoint_control.c').read_text().replace('#include "../src/gameplay_match_flow.c"','#include "@FLOW_SOURCE@"')).replace('@FLOW_SOURCE@',str(flow)))
  bootstrap=out/'bootstrap.c';bootstrap.write_text((ROOT/'src/gameplay_bootstrap.c').read_text()+"\nvoid fixture_bootstrap_owner_begin(void){arena=(void*)0x1000;tables_live=1;generation=9;disabled_links=0;}\nvoid fixture_bootstrap_owner_rebind(void){++generation;}\nu64* fixture_bootstrap_mask(void){return &disabled_links;}\n")
  command=[str(ROOT/'.deps/emsdk/upstream/emscripten/emcc'),'-std=gnu11','-O1','-UNDEBUG','-DTARGET_PC',f'-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK={enabled}','-DMELEE_WEB_SLIPPI_PROFILE=1','-DMELEE_WEB_CPU_INTERVAL_ENABLED=1','-ffp-contract=off','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
   '-I'+str(SOURCE/'src'),'-I'+str(ROOT/'src'),'-I'+str(PREPARED),'-I'+str(PREPARED/'melee/gm'),'-I'+str(PREPARED/'melee/lb'),'-I'+str(ROOT/'.deps/melee/src'),'-I'+str(ROOT/'.deps/aurora/include'),'-I'+str(ROOT/'.deps/melee/extern/dolphin/include'),'-include',str(ROOT/'src/gameplay_compat.h'),f'-DMELEE_WEB_TEST_CLOCK_SOURCE="{target}"',f'-DMELEE_WEB_TEST_CADENCE_SOURCE="{cadence}"',str(control),str(bootstrap),str(SOURCE/'src/slippi_profile/logical_frame_clock.c'),'-sENVIRONMENT=node','-sEXIT_RUNTIME=1','-sASSERTIONS=2','-o',str(out/'control.js')]
  result=subprocess.run(command,cwd=out,env=env,text=True,capture_output=True,timeout=120);(out/'compile.json').write_text(json.dumps({'argv':command,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2));self.assertEqual(result.returncode,0,result.stderr)
  node=os.environ.get('MELEE_LOGICAL_CLOCK_NODE',shutil.which('node'));self.assertTrue(node)
  result=subprocess.run([node,str(out/'control.js')],cwd=out,env=env,text=True,capture_output=True,timeout=30);(out/'run.json').write_text(json.dumps({'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2));self.assertEqual(result.returncode,0,result.stdout+result.stderr)
  for p,h in original.items():self.assertEqual(hashlib.sha256(p.read_bytes()).hexdigest(),h)
  self.assertIn('Actual v6 logical clock' if enabled else 'Actual flow clock cadence CPU restore',result.stdout);print(out);print(result.stdout)
  if 'MELEE_LOGICAL_CLOCK_OUTPUT_ROOT' not in os.environ:shutil.rmtree(out)

 def test_actual_clock_phase_and_rejected_pre(self):self._run_actual_clock(1)
 def test_default_v5_actual_clock_unchanged(self):self._run_actual_clock(0)

 def test_actual_profile_identity_and_pure_transition(self):
  if not shutil.which('cc'):self.skipTest('Native C compiler unavailable')
  output=Path(os.environ.get('MELEE_LOGICAL_CLOCK_OUTPUT_ROOT',ROOT/'work'));output.mkdir(parents=True,exist_ok=True)
  out=Path(tempfile.mkdtemp(prefix='logical-profile-',dir=output))
  c=out/'identity.c';c.write_text('''
#include <assert.h>
#include <string.h>
#include "slippi_profile/profile.h"
#include "slippi_profile/logical_frame_clock.h"
int main(void){
 assert(slippi_profile_identity_name(SLIPPI_PROFILE_ACTIVE_IDENTITY));
 assert(slippi_profile_source_draw_decision(SLIPPI_PROFILE_ACTIVE_IDENTITY,SLIPPI_TICK_PRESENT)==SLIPPI_SOURCE_DRAW_ALLOW);
 assert(slippi_profile_source_draw_decision(SLIPPI_PROFILE_ACTIVE_IDENTITY,SLIPPI_TICK_CORRECTION)==SLIPPI_SOURCE_DRAW_SKIP);
#if defined(MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK) && MELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK
 assert(!strcmp(slippi_profile_identity_name(SLIPPI_PROFILE_ACTIVE_IDENTITY),SLIPPI_PROFILE_LOGICAL_FRAME_V6_ID));
 assert(!slippi_profile_identity_name(SLIPPI_PROFILE_IDENTITY_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5));
#else
 assert(!strcmp(slippi_profile_identity_name(SLIPPI_PROFILE_ACTIVE_IDENTITY),SLIPPI_PROFILE_RENDER_DEPENDENCIES_DBOOC_SHIELD_DROP_V5_ID));
 assert(!slippi_profile_identity_name(SLIPPI_PROFILE_IDENTITY_LOGICAL_FRAME_V6));
#endif
 SlippiLogicalClockTransition output={99,99};
 assert(!slippi_logical_clock_transition(99,SLIPPI_CLOCK_NEXT_PRE,&output));assert(output.phase==99&&output.advance==99);
 assert(!slippi_logical_clock_transition(SLIPPI_CLOCK_PENDING,99,&output));assert(output.phase==99&&output.advance==99);
 assert(!slippi_logical_clock_transition(SLIPPI_CLOCK_COLD,SLIPPI_CLOCK_PRESENT,&output));assert(output.phase==99&&output.advance==99);
 assert(!slippi_logical_clock_transition(SLIPPI_CLOCK_PENDING,SLIPPI_CLOCK_PRESENT,0));
 assert(slippi_logical_clock_transition(SLIPPI_CLOCK_PENDING,SLIPPI_CLOCK_NEXT_PRE,&output));assert(output.phase==2&&output.advance==1);
 assert(slippi_logical_clock_transition(SLIPPI_CLOCK_COMPLETED,SLIPPI_CLOCK_PRESENT,&output));assert(output.phase==2&&output.advance==0);
 return 0;
}
''')
  for enabled in (0,1):
   command=[shutil.which('cc'),'-std=c11','-Wall','-Wextra','-Werror',f'-DMELEE_WEB_SLIPPI_LOGICAL_FRAME_CLOCK={enabled}','-I'+str(SOURCE/'src'),str(c),str(SOURCE/'src/slippi_profile/profile.c'),str(SOURCE/'src/slippi_profile/logical_frame_clock.c'),'-o',str(out/f'identity-{enabled}')]
   result=subprocess.run(command,cwd=out,text=True,capture_output=True,timeout=30);(out/f'compile-{enabled}.json').write_text(json.dumps({'argv':command,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2));self.assertEqual(result.returncode,0,result.stderr)
   result=subprocess.run([str(out/f'identity-{enabled}')],cwd=out,capture_output=True,text=True,timeout=10);(out/f'run-{enabled}.json').write_text(json.dumps({'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr},indent=2));self.assertEqual(result.returncode,0,result.stderr)
  print('Actual profile v5/v6 identity/pure transition:',out)
  if 'MELEE_LOGICAL_CLOCK_OUTPUT_ROOT' not in os.environ:shutil.rmtree(out)

 def test_actual_build_optin_and_cli(self):
  import importlib.util
  from unittest.mock import patch
  with patch.object(sys,'path',[str(ROOT/'scripts'),*sys.path]):
   spec=importlib.util.spec_from_file_location('logical_build_control',SOURCE/'scripts/build.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  root=Path('/fixture/repo')
  old=module.build_directory(root,target='gameplay_slippi_synctest',configuration='Release')
  self.assertEqual(old,root/'build/slippi-synctest-release')
  seen=set()
  for aurora in (False,True):
   for stage in (False,True):
    value=module.build_directory(root,target='gameplay_slippi_synctest',configuration='Release',slippi_logical_frame_clock=True,aurora_staging_shadow_diagnostic=aurora,stage_arena_owner_diagnostic=stage)
    self.assertIn('logical-frame',value.name);seen.add(value)
  self.assertEqual(len(seen),4);self.assertNotIn(old,seen)
  for target,configuration in [('runtime','Release'),('gameplay_slippi_synctest','RelWithDebInfo')]:
   with self.assertRaises(ValueError):module.build_directory(root,target=target,configuration=configuration,slippi_logical_frame_clock=True)
  with patch.object(module,'build') as build,patch.object(sys,'argv',['build.py','--trace-target','gameplay_slippi_synctest','--configuration','Release','--slippi-logical-frame-clock']):
   module.main();self.assertTrue(build.call_args.kwargs['slippi_logical_frame_clock'])
  # Invalid scope must refuse before bootstrap/tool/configure functions.
  with patch.object(module,'prepare_sources',side_effect=AssertionError('source preparation must not run')):
   with self.assertRaises(ValueError):module.build(1,target='runtime',configuration='Release',slippi_logical_frame_clock=True)
  print('Actual opt-in directories/defaultOFF/privateRelease/CLI checks passed')
if __name__=='__main__':unittest.main()
