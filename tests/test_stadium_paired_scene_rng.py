"""Actual read-only scene RNG leaf with modeled world ownership; no parity claim."""
from pathlib import Path
import os
import shlex
import subprocess
from owned_test_workspace import OwnedWorkspaceTests
from test_first_css_final_pending_draw_host import _function
ROOT=Path(__file__).resolve().parents[1]
class StadiumPairedSceneRngTests(OwnedWorkspaceTests):
    def test_actual_phase_owner_leaf_and_refusals(self):
        source=(ROOT/'src/gameplay_match_context.c').read_text()
        header=(ROOT/'src/gameplay_match_context.h').read_text()
        types=header[header.index('typedef struct MeleeWebMatchSceneRngPoint {'):header.index('void melee_web_match_scene_rng_note(unsigned phase);')]
        code=r'''#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
'''+types+r'''
/* Modeled world/camera validator, with explicit invalid ownership refusal. */
typedef struct MeleeWebMatchContext {unsigned seed;MeleeWebMatchSceneRngSnapshot scene_rng;} MeleeWebMatchContext;
static MeleeWebMatchContext* owner;static unsigned* seed_ptr;static int valid=1;
static int live(MeleeWebMatchContext* h,char* e,size_t n){(void)e;(void)n;return valid&&h&&h==owner&&seed_ptr==&h->seed;}
static int fail(char* e,size_t n,const char* m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
'''
        # Actual leaf accesses generation as well as installed seed identity.
        code=code.replace('unsigned seed;MeleeWebMatchSceneRngSnapshot','uint64_t generation;unsigned seed;MeleeWebMatchSceneRngSnapshot')
        code+=_function(source,'void melee_web_match_scene_rng_note(')+'\n'+_function(source,'int melee_web_match_scene_rng_snapshot(')
        code+=r'''
static void reset(MeleeWebMatchContext* h){*h=(MeleeWebMatchContext){0};h->generation=7;h->seed=17;owner=h;seed_ptr=&h->seed;valid=1;h->scene_rng.count=1;h->scene_rng.points[0]=(MeleeWebMatchSceneRngPoint){7,(uintptr_t)seed_ptr,17};}
int main(void){MeleeWebMatchContext h;MeleeWebMatchSceneRngSnapshot out;char error[256];unsigned foreign=17;
 reset(&h);melee_web_match_scene_rng_note(1);assert(h.seed==17);assert(melee_web_match_scene_rng_snapshot(&h,&out,error,sizeof(error)));assert(out.count==2);
 h.seed=19;melee_web_match_scene_rng_note(2);h.seed=23;melee_web_match_scene_rng_note(3);assert(melee_web_match_scene_rng_snapshot(&h,&out,error,sizeof(error)));assert(out.count==4&&out.points[0].seed==17&&out.points[2].seed==19&&out.points[3].seed==23&&h.seed==23);
 melee_web_match_scene_rng_note(3);assert(h.scene_rng.invalid);assert(!melee_web_match_scene_rng_snapshot(&h,&out,error,sizeof(error)));
 reset(&h);melee_web_match_scene_rng_note(2);assert(h.scene_rng.invalid&&h.scene_rng.count==1);
 reset(&h);seed_ptr=&foreign;melee_web_match_scene_rng_note(1);assert(h.scene_rng.invalid&&foreign==17&&h.seed==17);
 reset(&h);valid=0;melee_web_match_scene_rng_note(1);assert(h.scene_rng.invalid);
 reset(&h);h.scene_rng.points[0].world_generation=8;melee_web_match_scene_rng_note(1);assert(!melee_web_match_scene_rng_snapshot(&h,&out,error,sizeof(error)));
 reset(&h);owner=NULL;melee_web_match_scene_rng_note(1);assert(h.scene_rng.count==1);
 return 0;}
'''
        scratch=self.new_workspace(ROOT,'paired-scene-rng-')
        src=scratch/'control.c';exe=scratch/'control';src.write_text(code)
        compile_result=subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],capture_output=True,text=True)
        (scratch/'compile.stdout').write_text(compile_result.stdout);(scratch/'compile.stderr').write_text(compile_result.stderr)
        self.assertEqual(compile_result.returncode,0,compile_result.stderr)
        self.assertEqual(subprocess.run([str(exe)],capture_output=True,text=True).returncode,0)
    def test_actual_installation_and_exit_hook_source_order(self):
        context=(ROOT/'src/gameplay_match_context.c').read_text()
        start=context.index('    seed_ptr=&h->seed;')
        self.assertLess(context.index('h->scene_rng.points[0]=',start),context.index('plStale_InitAttackInstance();',start))
        leaf=_function(context,'void melee_web_match_scene_rng_note(')
        self.assertNotIn('HSD_Rand',leaf)
        self.assertNotIn('*seed_ptr=',leaf)
        patch=(ROOT/'patches/melee-gameplay.patch').read_text()
        self.assertEqual(patch.count('+    melee_web_match_scene_rng_note(2);'),1)
        self.assertEqual(patch.count('+    melee_web_match_scene_rng_note(3);'),1)
        session=(ROOT/'src/gameplay_match_session.cpp').read_text()
        advance=_function(session,'bool GameplayMatchSession::advance_construction(')
        self.assertLess(advance.index('storage_->advance_construction()'),advance.index('melee_web_match_scene_rng_note(1)'))
        finish=_function(session,'void GameplayMatchSession::finish_vs(')
        self.assertLess(finish.index('melee_web_match_rules_publish_result()'),finish.index('&scene_rng_retained_'))
        self.assertLess(finish.index('&scene_rng_retained_'),finish.index('storage_->close()'))
