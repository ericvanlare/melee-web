"""Actual C probe cache/selection controls; JS and source event leaves are modeled."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(os.environ.get('MELEE_LONG_B_SUPPORT_ROOT',Path(__file__).resolve().parents[1]))

class StadiumLongBRngActivationTests(unittest.TestCase):
    def test_actual_cached_probe_empty_prefix_then_action_selection(self):
        compiler=shutil.which('clang') or shutil.which('cc')
        if not compiler:self.skipTest('C compiler required')
        probe_path=Path(os.environ.get('MELEE_LONG_B_RNG_PROBE_SOURCE',ROOT/'src/gameplay_rng_draw_probe.c'))
        source=probe_path.read_text()
        bindings_header=(probe_path.parent/'gameplay_rng_draw_owner.h').read_text()
        declarations=source[source.index('typedef struct {'):source.index('#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)')]
        draw=source[source.index('void melee_web_cpu_observation_rng_draw('):source.index('static void emit_rng_draw_probe(void)\n{')]
        emit=source[source.index('static void emit_rng_draw_probe(void)\n{'):]
        mock=bindings_header+r'''
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define __EMSCRIPTEN__ 1
#define RNG_DRAW_PROBE_MAX_PER_FRAME 1024
#define RNG_DRAW_PROBE_LINE_CAPACITY (512 + 1024 * 448)
#define MELEE_WEB_RNG_DRAW_RAND 1
#define MELEE_WEB_RNG_DRAW_RANDF 2
static int activated=0,ordinary=0,rows=0;
static int modeled_js(const char* code,size_t index){
 if(strstr(code,"return Array.isArray(window.__meleeRngDrawProbeCursors)"))return 1;
 if(strstr(code,"return window.__meleeRngDrawProbeFighterOwners"))return !ordinary;
 if(strstr(code,"return window.__meleeRngDrawProbeOwners"))return !ordinary;
 assert(strstr(code,"cursors.includes"));
 return ordinary?index==12:activated&&index>=168&&index<=7167;
}
static void modeled_callback(const char* line){++rows;puts(line);}
#define EM_ASM_INT(code,arg) modeled_js(#code,arg)
#define EM_ASM(code,arg) modeled_callback(arg)
void grStadium_801D1390(struct HSD_GObj* p){(void)p;}
void efLib_particles_proc_main(struct HSD_GObj* p){(void)p;}
void Fighter_8006A360(struct HSD_GObj* p){(void)p;}
'''
        owner='static int modeled_membership(const struct HSD_GObj* g,unsigned* a,unsigned* b,unsigned* c){(void)g;(void)a;(void)b;(void)c;return 0;}\nstatic void capture_rng_draw_owner(RngDrawProbeEntry* e){e->gobj=e->proc=e->callback=0;e->classifier=e->p_link=e->s_link=0;e->fighter_observed=0;}\n'
        main=r'''
int main(int argc,char** argv){
 (void)argv;ordinary=argc>1;
 /* Modeled capability only: this test exercises actual cached cursor/emitter
  * selection, not Fighter membership. The owner callback remains modeled. */
 static const MeleeWebRngDrawOwnerBindings bindings={
  grStadium_801D1390,efLib_particles_proc_main,Fighter_8006A360,modeled_membership};
 melee_web_rng_draw_probe_bind_source_owners(&bindings);
 if(ordinary){
  melee_web_cpu_observation_set_rng_draw_cursor(11);melee_web_rng_draw_probe_scheduler_return();assert(rows==0);
  melee_web_cpu_observation_set_rng_draw_cursor(12);melee_web_cpu_observation_rng_draw(0x12345678,1);melee_web_rng_draw_probe_scheduler_return();assert(rows==1);
  melee_web_cpu_observation_set_rng_draw_cursor(13);melee_web_rng_draw_probe_scheduler_return();assert(rows==1);
 }else{
  for(size_t i=0;i<=951;i++){melee_web_cpu_observation_set_rng_draw_cursor(i);melee_web_cpu_observation_rng_draw(0x12345678,1);melee_web_rng_draw_probe_scheduler_return();}
  assert(rows==0&&rng_draw_probe_configuration_checked&&rng_draw_probe_configured&&rng_draw_probe_owners&&rng_draw_probe_fighter_owners);
  activated=1;
  melee_web_cpu_observation_set_rng_draw_cursor(168);melee_web_rng_draw_probe_scheduler_return();assert(rows==1);
  melee_web_cpu_observation_set_rng_draw_cursor(169);melee_web_cpu_observation_rng_draw(0xabcdef01,2);melee_web_rng_draw_probe_scheduler_return();assert(rows==2);
  melee_web_cpu_observation_set_rng_draw_cursor(7168);melee_web_rng_draw_probe_scheduler_return();assert(rows==2);
 }
}
'''
        with tempfile.TemporaryDirectory(prefix='long-b-probe-selection-') as tmp:
            c=Path(tmp)/'control.c';binary=Path(tmp)/'control';c.write_text(mock+declarations+owner+draw+emit+main)
            x=subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(x.returncode,0,x.stderr)
            for args,version,cursors in [([],3,[168,169]),(['ordinary'],1,[12])]:
                x=subprocess.run([str(binary),*args],capture_output=True,text=True);self.assertEqual(x.returncode,0,x.stderr)
                rows=[json.loads(line) for line in x.stdout.splitlines()]
                self.assertEqual([r['source_cursor'] for r in rows],cursors);self.assertTrue(all(r['version']==version and not r['overflowed'] for r in rows))
                if not args:self.assertEqual(rows[0]['draws'],[]);self.assertEqual(rows[1]['draws'][0]['seed_after'],'abcdef01')
