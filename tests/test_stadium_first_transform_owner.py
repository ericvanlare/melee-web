"""Actual first-fire journal reducer and authoritative source hook controls.
No source World, transformed graph, timing or original parity is claimed.
"""
from pathlib import Path
import os,hashlib,json,shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT',ROOT))
REFERENCE=Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT',ROOT))
def body(s,name):
 a=s.index(name);p=s.index('{',a);n=1;b=p+1
 while n:n+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
class StadiumFirstTransformOwnerTests(unittest.TestCase):
 def test_actual_fixed_first_fire_call_order_reducer(self):
  cc=shutil.which('clang') or shutil.which('cc')
  if not cc:self.skipTest('Native C compiler required')
  scratch=Path(tempfile.mkdtemp(prefix='stadium-first-fire-journal-'));passed=False
  try:
   code=r"""
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#include "gameplay_stadium_display_owner.h"
#include <assert.h>
/* Local identity tokens only: these do not model a live Ground/World. */
int main(void){
 HSD_GObj* base=(HSD_GObj*)(uintptr_t)16;HSD_GObj* fire=(HSD_GObj*)(uintptr_t)32;
 HSD_GObj* foreign=(HSD_GObj*)(uintptr_t)48;
 MeleeWebStadiumFirstTransformJournal initial={0};initial.armed=1;
 initial.world_generation=7;initial.initial_map5=base;
 MeleeWebStadiumFirstTransformJournal j=initial;
 assert(melee_web_stadium_first_transform_record(&j,1,3,fire)&&j.count==1);
 assert(melee_web_stadium_first_transform_record(&j,2,5,base));
 assert(melee_web_stadium_first_transform_record(&j,3,5,base));
 assert(melee_web_stadium_first_transform_record(&j,2,3,fire));
 assert(melee_web_stadium_first_transform_record(&j,3,3,fire)&&j.count==5&&!j.failed);
 assert(!melee_web_stadium_first_transform_record(&j,1,3,fire)&&j.failed&&j.count==5);
 j=initial;assert(!melee_web_stadium_first_transform_record(&j,1,9,fire)&&j.failed&&!j.count);
 j=initial;assert(!melee_web_stadium_first_transform_record(&j,2,5,base)&&j.failed);
 j=initial;assert(!melee_web_stadium_first_transform_record(&j,1,3,base)&&j.failed);
 j=initial;assert(melee_web_stadium_first_transform_record(&j,1,3,fire));
 assert(!melee_web_stadium_first_transform_record(&j,1,3,fire)&&j.count==1&&j.failed);
 j=initial;assert(melee_web_stadium_first_transform_record(&j,1,3,fire));
 assert(!melee_web_stadium_first_transform_record(&j,3,5,base)&&j.failed);
 j=initial;assert(melee_web_stadium_first_transform_record(&j,1,3,fire));
 assert(!melee_web_stadium_first_transform_record(&j,2,5,foreign)&&j.failed);
 j=initial;j.world_generation=0;assert(!melee_web_stadium_first_transform_record(&j,1,3,fire)&&j.failed);
 j=initial;j.armed=0;assert(!melee_web_stadium_first_transform_record(&j,1,3,fire)&&j.failed);
 return 0;
}
"""
   (scratch/'control.c').write_text(code)
   for name,argv in [('compile',[cc,'-std=c11','-Wall','-Wextra','-Werror','-I',str(SOURCE/'src'),'-I',str(REFERENCE/'src'),str(scratch/'control.c'),'-o',str(scratch/'control')]),('run',[str(scratch/'control')])]:
    r=subprocess.run(argv,capture_output=True,text=True,timeout=30)
    (scratch/(name+'.stdout')).write_text(r.stdout);(scratch/(name+'.stderr')).write_text(r.stderr)
    self.assertEqual(r.returncode,0,str(scratch)+'\n'+r.stdout+r.stderr)
   passed=True
  finally:
   if passed:shutil.rmtree(scratch)
   else:print('Retained first-fire reducer failure: '+str(scratch),flush=True)
 def test_actual_source_hook_order_and_immutable_initial_journal(self):
  git=shutil.which('git');upstream=REFERENCE/'.deps/melee'
  if not git or not (upstream/'.git').exists():self.skipTest('Pinned Melee source required')
  lock=json.loads((REFERENCE/'dependencies.lock.json').read_text());commit=lock['repositories']['melee']['commit']
  self.assertEqual(subprocess.check_output([git,'rev-parse','HEAD'],cwd=upstream,text=True).strip(),commit)
  scratch=Path(tempfile.mkdtemp(prefix='stadium-first-fire-source-'));passed=False
  try:
   for relative in ['src/melee/gr/grpstadium.c','src/melee/gr/ground.c']:
    p=scratch/relative;p.parent.mkdir(parents=True,exist_ok=True)
    p.write_bytes(subprocess.check_output([git,'show',commit+':'+relative],cwd=upstream))
   argv=[git,'apply','--no-index','--include=src/melee/gr/grpstadium.c','--include=src/melee/gr/ground.c',str(SOURCE/'patches/melee-gameplay.patch')]
   r=subprocess.run(argv,cwd=scratch,capture_output=True,text=True,timeout=30)
   (scratch/'apply.stdout').write_text(r.stdout);(scratch/'apply.stderr').write_text(r.stderr)
   self.assertEqual(r.returncode,0,str(scratch)+'\n'+r.stdout+r.stderr)
   gr=(scratch/'src/melee/gr/grpstadium.c').read_text();ground=(scratch/'src/melee/gr/ground.c').read_text()
   self.assertIn('stadium_first_transform_current',gr,'authoritative patch must contain new actual source hooks')
   initial=body(gr,'static int stadium_owner_source_journal_matches(')
   self.assertEqual(hashlib.sha256(initial.encode()).hexdigest(),'fc6f2899bbab4d3a00239c7bb6ef593b42619c07bc4a1282c8543f7450d0d243')
   destroy=body(ground,'void Ground_801C4A08(')
   self.assertEqual(destroy.count('melee_web_stadium_display_owner_note_ground_retire(gobj,1);'),1)
   self.assertEqual(destroy.count('melee_web_stadium_display_owner_note_ground_retire(gobj,0);'),1)
   self.assertLess(destroy.index('note_ground_retire(gobj,1)'),destroy.index('gp = gobj->user_data'))
   self.assertLess(destroy.index('gp->x1C_callback(gobj)'),destroy.index('HSD_GObjPLink_80390228(gobj)'))
   self.assertLess(destroy.index('HSD_GObjPLink_80390228(gobj)'),destroy.index('note_ground_retire(gobj,0)'))
   returned=body(gr,'void melee_web_stadium_display_owner_note_ground_retire(')
   self.assertNotIn('object->',returned,'return receipt must never dereference freed GObj')
   graph=body(gr,'static int stadium_first_transform_graph_matches(')
   self.assertIn('form!=3',graph);self.assertIn('owner->map2_buffer_use_count!=2',graph)
   self.assertIn('melee_web_stage_map_lookup(3)!=gp->u.stadium.xD0',graph)
   self.assertIn('stadium_owner_first_fire_proc_chain_matches(j->fire_map3,proc)',graph)
   fire_chain=body(gr,'static int stadium_owner_first_fire_proc_chain_matches(')
   self.assertIn('expected_head->on_invoke == grStadium_801D19D8',fire_chain)
   self.assertIn('retiring&&(phase!=0||j->count!=3)',graph)
   passed=True
  finally:
   if passed:shutil.rmtree(scratch)
   else:print('Retained first-fire source reconstruction: '+str(scratch),flush=True)
if __name__=='__main__':unittest.main()
