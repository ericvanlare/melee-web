"""Compiled actual observer + actual patched allocator/proc bodies.

HSD tables and source-memory context are explicit modeled leaves. This is not
an original/native runtime allocation or registration parity result.
"""
from pathlib import Path
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

SOURCE = Path(os.environ.get('MELEE_REGISTRATION_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
SUPPORT = Path(os.environ.get('MELEE_REGISTRATION_SUPPORT_ROOT', SOURCE))
sys.path.insert(0, str(SUPPORT/'scripts'))
from canonicalize_gameplay_patch import _environment, applied_tree

MODEL = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stadium_registration_observer.h"
#include "gameplay_source_memory_runtime.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
typedef struct HSD_GObj HSD_GObj;typedef struct HSD_GObjProc HSD_GObjProc;
struct HSD_GObj {u16 classifier;u8 p_link;HSD_GObj *next,*prev;HSD_GObjProc* proc;};
struct HSD_GObjProc {HSD_GObjProc *child,*next,*prev;u8 s_link;
 unsigned flags_1:1,flags_2:1,flags_3:2,flags_4:2;HSD_GObj* gobj;void(*on_invoke)(HSD_GObj*);};
#define HSD_ASSERT(line,condition) assert(condition)
'''
LEAVES = r'''
static struct {u32 top,remain;} obj_heap;
static int HSD_GetHeap(void) {assert(!"unsupported modeled heap branch");return 0;}
static u32 OSCheckHeap(int heap) {(void)heap;assert(!"unsupported modeled heap branch");return 0;}
static int HSD_ObjAllocAddFree(HSD_ObjAllocData* data,unsigned n) {
 (void)data;(void)n;assert(!"unsupported modeled pool-growth branch");return 0;
}
HSD_ObjAllocData gobj_alloc_data,gobjproc_alloc_data;
HSD_GObjProc *HSD_GObj_804D7844[1024],*HSD_GObj_804D7840[256];
HSD_GObjProc *HSD_GObj_804D7838,*HSD_GObj_804D7830;
unsigned HSD_GObj_804D7834;
struct {unsigned p_link_max,gproc_pri_max;} HSD_GObjLibInitData={15,31};
struct {unsigned b0:1,b2:1;} HSD_GObj_804CE3E4;
static int context_active;static uint64_t context_generation;
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* out) {
 if(!out) return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
 memset(out,0,sizeof(*out));if(!context_active)return MELEE_WEB_SOURCE_MEMORY_READ_INACTIVE;
 out->world_generation=context_generation;out->source_heap_handle=1;return MELEE_WEB_SOURCE_MEMORY_READ_OK;
}
static void stage(HSD_GObj* p){(void)p;}static void particle(HSD_GObj* p){(void)p;}
static void fighter(HSD_GObj* p){(void)p;}static void unknown(HSD_GObj* p){(void)p;}
static const MeleeWebRngDrawOwnerBindings binding={stage,particle,fighter,NULL};
static HSD_GObj objects[2];static HSD_GObjProc procs[2];
static void pools(void) {
 memset(&gobj_alloc_data,0,sizeof(gobj_alloc_data));memset(&gobjproc_alloc_data,0,sizeof(gobjproc_alloc_data));
 memset(objects,0,sizeof(objects));memset(procs,0,sizeof(procs));
 *(void**)&objects[0]=&objects[1];*(void**)&procs[0]=&procs[1];
 gobj_alloc_data.size=sizeof(HSD_GObj);gobj_alloc_data.free=2;gobj_alloc_data.freehead=(HSD_ObjAllocLink*)objects;
 gobjproc_alloc_data.size=sizeof(HSD_GObjProc);gobjproc_alloc_data.free=2;gobjproc_alloc_data.freehead=(HSD_ObjAllocLink*)procs;
 memset(HSD_GObj_804D7844,0,sizeof(HSD_GObj_804D7844));memset(HSD_GObj_804D7840,0,sizeof(HSD_GObj_804D7840));
 HSD_GObj_804D7838=HSD_GObj_804D7830=NULL;HSD_GObj_804CE3E4.b0=HSD_GObj_804CE3E4.b2=0;
}
static void begin(uint64_t g) {pools();context_generation=g;context_active=1;melee_web_stadium_registration_world_begin(g);}
static void end(void) {melee_web_stadium_registration_world_end(context_generation);context_active=0;}
'''
CONTROL = r'''
static void dump(int rows) {
 puts(melee_web_stadium_registration_status_json());
 if(rows)for(size_t i=0;i<melee_web_stadium_registration_count();++i)puts(melee_web_stadium_registration_read_json((uint32_t)i));
}
int main(int argc,char** argv) {
 assert(argc==2);melee_web_stadium_registration_bind(&binding,&gobj_alloc_data,&gobjproc_alloc_data);
 assert(melee_web_stadium_registration_arm_checked());
 if(!strcmp(argv[1],"happy")) {
  uint64_t previous=0;
  for(uint64_t world=1;world<=2;++world) {
   begin(world);HSD_GObj* g=HSD_ObjAlloc(&gobj_alloc_data);memset(g,0,sizeof(*g));g->classifier=3;g->p_link=1;
   HSD_GObjProc* p=HSD_GObj_SetupProc(g,world==1?stage:particle,1);
   assert(g->proc==p);MeleeWebRegistrationRow row={0};
   assert(melee_web_stadium_registration_read(melee_web_stadium_registration_count()-1,&row));
   assert(row.kind==MW_REG_SETUP_RETURN && row.proc_generation>previous);previous=row.proc_generation;
   HSD_GObjProc_8038FE24(p);assert(!g->proc);HSD_ObjFree(&gobj_alloc_data,g);end();
  }
  assert(melee_web_stadium_registration_seal());dump(1);return 0;
 }
 if(!strcmp(argv[1],"missing")) {
  begin(1);HSD_GObj fabricated={0};fabricated.p_link=1;
  HSD_GObj_SetupProc(&fabricated,stage,1);end();assert(!melee_web_stadium_registration_seal());dump(0);return 0;
 }
 if(!strcmp(argv[1],"deferred")) {
  begin(1);HSD_GObj* g=HSD_ObjAlloc(&gobj_alloc_data);memset(g,0,sizeof(*g));g->p_link=1;
  HSD_GObjProc* p=HSD_GObj_SetupProc(g,fighter,1);HSD_GObj_804D7838=p;
  HSD_GObjProc_8038FE24(p);assert(HSD_GObj_804CE3E4.b2 && g->proc==p);
  end();assert(melee_web_stadium_registration_seal());dump(1);return 0;
 }
 if(!strcmp(argv[1],"overflow")) {
  begin(1);for(unsigned i=0;i<23000;++i) {void* p=HSD_ObjAlloc(&gobj_alloc_data);HSD_ObjFree(&gobj_alloc_data,p);}
  end();assert(!melee_web_stadium_registration_seal());dump(0);return 0;
 }
 if(!strcmp(argv[1],"unknown")) {
  begin(1);HSD_GObj* g=HSD_ObjAlloc(&gobj_alloc_data);memset(g,0,sizeof(*g));g->p_link=1;
  HSD_GObjProc* p=HSD_GObj_SetupProc(g,unknown,1);HSD_GObjProc_8038FE24(p);HSD_ObjFree(&gobj_alloc_data,g);
  end();assert(melee_web_stadium_registration_seal());dump(1);return 0;
 }
 assert(!"unknown control");return 1;
}
'''


class RegistrationSourceControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang') or shutil.which('cc')
        if compiler is None:
            raise unittest.SkipTest('C compiler required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='stadium-registration-')
        cls.scratch = Path(cls.tmp.name)
        cls.tmp._finalizer.detach()  # Explicit cleanup only after success; retain failures.
        cls.preserve = False
        cls.addClassCleanup(lambda: None if cls.preserve else cls.tmp.cleanup())
        repo = SUPPORT/'.deps/melee'
        commit = json.loads((SUPPORT/'dependencies.lock.json').read_text())['repositories']['melee']['commit']
        env = _environment(repo, cls.scratch)
        tree = applied_tree(repo, commit, (SOURCE/'patches/melee-gameplay.patch').read_bytes(), env)
        def text(path):
            return subprocess.check_output(['git','show',tree+':'+path],cwd=repo,env=env,text=True)
        header = (repo/'src/sysdolphin/baselib/objalloc.h').read_text()
        declarations = header[header.index('typedef struct _HSD_ObjAllocLink'):header.index('ASSERT_SIZE')]
        allocator = text('src/sysdolphin/baselib/objalloc.c')
        allocator = allocator[allocator.index('void* HSD_ObjAlloc('):allocator.index('static inline void removeAll')]
        proc = text('src/sysdolphin/baselib/gobjproc.c')
        proc = '\n'.join(line for line in proc.splitlines() if not line.startswith('#include'))
        # Retail-only stack padding is unused in the host compiler. Keep every
        # actual source operation/hook; mark that exact declaration as used.
        assert proc.count('u8 _[8];') == 1
        proc = proc.replace('u8 _[8];', 'u8 _[8]; (void) _;')
        source = MODEL + declarations + LEAVES + allocator + proc + CONTROL
        path = cls.scratch/'control.c';path.write_text(source)
        cls.binary = cls.scratch/'control'
        command=[compiler,'-std=c11','-Wall','-Wextra','-Werror','-UNDEBUG',
                 '-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC=1','-I',str(SOURCE/'src'),'-I',str(SUPPORT/'src'),
                 str(path),str(SOURCE/'src/stadium_registration_observer.c'),'-o',str(cls.binary)]
        result=subprocess.run(command,capture_output=True,text=True)
        (cls.scratch/'compile.stdout').write_text(result.stdout)
        (cls.scratch/'compile.stderr').write_text(result.stderr)
        if result.returncode:
            cls.preserve=True
            raise AssertionError(f'Actual observer/source fixture compile failed; retained {cls.scratch}:\n{result.stderr}')

    def run_case(self, name):
        result=subprocess.run([str(self.binary),name],capture_output=True,text=True)
        (self.scratch/(name+'.stdout')).write_text(result.stdout)
        (self.scratch/(name+'.stderr')).write_text(result.stderr)
        if result.returncode:
            type(self).preserve=True
        self.assertEqual(result.returncode,0,result.stderr)
        lines=[json.loads(line) for line in result.stdout.splitlines()]
        return lines[0],lines[1:]

    def test_actual_source_nested_registration_free_and_reused_generations(self):
        status,rows=self.run_case('happy')
        self.assertTrue(status['sealed']);self.assertFalse(status['invalid']);self.assertEqual(status['worlds'],2)
        self.assertEqual([r['sequence'] for r in rows],list(range(len(rows))))
        self.assertEqual([r['kind'] for r in rows[:7]],[1,3,6,3,12,8,7])
        self.assertEqual(rows[4]['neighbors_observed'],0)
        self.assertEqual(rows[5]['neighbors_observed'],1)
        first=[r for r in rows if r['world_generation']=='1']
        self.assertEqual([r['kind'] for r in first[7:14]],[11,14,13,9,10,4,5])
        returned=next(r for r in first if r['kind']==15)
        self.assertEqual(returned['snapshot_is_entry'],1)
        entry=first[returned['call_index']]
        self.assertEqual(returned['proc_generation'],entry['proc_generation'])
        self.assertEqual([r['residual_live'] for r in rows if r['kind']==2],[0,0])
        allocations=[r for r in rows if r['kind']==3]
        self.assertEqual([int(r['object_generation']) for r in allocations],[1,2,3,4])
        self.assertEqual({r['owner_role'] for r in rows if r['kind']==7},{1,2})

    def test_missing_birth_refuses_instead_of_first_seen_generation(self):
        status,_=self.run_case('missing')
        self.assertTrue(status['invalid']);self.assertFalse(status['sealed'])

    def test_deferred_request_is_not_unlink_or_free(self):
        status,rows=self.run_case('deferred')
        self.assertTrue(status['sealed']);self.assertFalse(status['invalid'])
        self.assertTrue(any(r['kind']==11 and r['deferred']==1 for r in rows))
        self.assertFalse(any(r['kind'] in (4,5,9,10) for r in rows))
        self.assertEqual(rows[-1]['residual_live'],2)
        self.assertEqual({r['owner_role'] for r in rows if r['kind']==7},{3})

    def test_fixed_capacity_overflow_is_incomplete_and_unknown_not_selected(self):
        status,_=self.run_case('overflow')
        self.assertTrue(status['overflow']);self.assertTrue(status['invalid']);self.assertFalse(status['sealed'])
        self.assertEqual(status['count'],65536)
        status,rows=self.run_case('unknown')
        self.assertTrue(status['sealed']);self.assertFalse(status['invalid'])
        self.assertGreater(status['unselected_proc_hook_calls'],0)
        self.assertFalse(any(r['kind'] in (6,7,8,9,10,11,12,13,14,15) for r in rows))
        self.assertEqual(len([r for r in rows if r['kind']==3]),2)

if __name__=='__main__':unittest.main()
