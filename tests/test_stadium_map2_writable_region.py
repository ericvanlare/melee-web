"""Actual StageLast region helpers with labeled modeled file/memory capabilities.
No source tick, Ground destructor, async transport, or transformed graph passes.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT', ROOT))
REFERENCE = Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT', ROOT))


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening+1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class StadiumMap2WritableRegionTests(unittest.TestCase):
    def test_actual_helpers_require_exact_lease_scope_and_idle_before_unregister(self):
        compiler = shutil.which('clang') or shutil.which('cc')
        if not compiler:
            self.skipTest('Native C compiler required')
        stage = (SOURCE/'src/gameplay_stage_last.c').read_text()
        display = (REFERENCE/'src/gameplay_stadium_display_owner.h').read_text()
        memory = (REFERENCE/'src/gameplay_source_memory_runtime.h').read_text()
        memory = memory[memory.index('typedef enum MeleeWebSourceMemoryReadStatus'):memory.index('/* Fighter_Create')]
        origin = display[display.index('typedef enum MeleeWebStadiumMap2BufferOrigin'):display.index('} MeleeWebStadiumMap2BufferOrigin;')+len('} MeleeWebStadiumMap2BufferOrigin;')]
        owner = display[display.index('typedef struct MeleeWebStadiumMap2BufferOwner'):display.index('} MeleeWebStadiumMap2BufferOwner;')+len('} MeleeWebStadiumMap2BufferOwner;')]
        journal = display[display.index('#define MELEE_WEB_STADIUM_FIRST_TRANSFORM_EVENTS'):display.index('} MeleeWebStadiumFirstTransformJournal;')+len('} MeleeWebStadiumFirstTransformJournal;')]
        helpers = stage[stage.index('typedef struct MeleeWebStadiumMap2WritableRegion'):stage.index('\n#endif\nstruct MeleeWebStageLast')]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
typedef struct HSD_GObj HSD_GObj;
typedef struct MeleeWebSourceFileScope { int identity; } MeleeWebSourceFileScope;
'''+memory+origin+owner+'\n'+journal+r'''
/* Explicit service models: these are refusal-capable capabilities, not native
 * transport evidence. Pending remains true throughout a modeled async wait. */
static MeleeWebSourceFileScope first={1},second={2};
static MeleeWebSourceFileScope* scope=&first;
static int pending,pumping,registers,unregisters,fail_register,fail_unregister;
static void* registered_base;
static size_t registered_size;
static MeleeWebSourceMemoryAllocation actual;
static MeleeWebSourceMemoryContext context;
static MeleeWebSourceMemoryReadStatus memory_status=MELEE_WEB_SOURCE_MEMORY_READ_OK;
MeleeWebSourceFileScope* melee_web_source_files_current_scope(void){return scope;}
int melee_web_source_files_quiescent_for(MeleeWebSourceFileScope* expected,char* e,size_t n){
 if(!expected||scope!=expected||pending||pumping){if(e&&n)snprintf(e,n,"modeled file scope busy/foreign");return 0;}
 return 1;
}
int melee_web_source_file_region_register(void* base,size_t size,char* e,size_t n){
 (void)e;(void)n;
 if(fail_register||registered_base||!scope)return 0;
 ++registers;registered_base=base;registered_size=size;return 1;
}
int melee_web_source_file_region_unregister(void* base,size_t size,char* e,size_t n){
 (void)e;(void)n;
 if(fail_unregister||pending||pumping||base!=registered_base||size!=registered_size)return 0;
 ++unregisters;registered_base=NULL;registered_size=0;return 1;
}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* out){*out=context;return memory_status;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(const void* base,MeleeWebSourceMemoryAllocation* out){
 if(base!=(void*)0x100000)return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;
 *out=actual;return memory_status;
}
'''+helpers+r'''
typedef struct MeleeWebStageLast { uint64_t generation;MeleeWebStadiumMap2WritableRegion stadium_map2_region;MeleeWebStadiumFirstTransformJournal stadium_first_transform;} MeleeWebStageLast;
static MeleeWebStageLast* active;
static struct {uint64_t generation;} stats={7};
#define melee_web_gameplay_stats() stats
'''+function(stage,'int melee_web_stage_last_source_work_allowed(')+r'''
static MeleeWebStadiumMap2BufferOwner maps;
static MeleeWebStadiumMap2WritableRegion region;
static char error[160];
static void reset(void){
 memset(&maps,0,sizeof(maps));memset(&region,0,sizeof(region));
 actual=(MeleeWebSourceMemoryAllocation){.source_heap_handle=3,.requested_bytes=0x50000,.world_generation=7,.allocation_generation=91,.live=1};
 context=(MeleeWebSourceMemoryContext){.source_heap_handle=3,.world_generation=7,.allocation_generation_watermark=91};
 maps.captured=1;maps.buffer=(void*)0x100000;maps.origin=MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK;
 maps.buffer_allocation_lease=actual;maps.buffer_allocation_lease_captured=1;
 scope=&first;pending=pumping=registers=unregisters=fail_register=fail_unregister=0;
 registered_base=NULL;registered_size=0;memory_status=MELEE_WEB_SOURCE_MEMORY_READ_OK;
}
int main(void){
 for(int mutation=0;mutation<10;++mutation){
  reset();
  switch(mutation){
   case 0:maps.origin=MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_BORROWED_PRELOAD;break;
   case 1:maps.buffer_allocation_lease_captured=0;break;
   case 2:actual.requested_bytes=0x50001;break;
   case 3:actual.world_generation=8;break;
   case 4:actual.allocation_generation=92;break;
   case 5:actual.source_heap_handle=4;break;
   case 6:actual.live=0;break;
   case 7:scope=NULL;break;
   case 8:pending=1;break;
   case 9:memory_status=MELEE_WEB_SOURCE_MEMORY_READ_UNHEALTHY;break;
  }
  assert(!stadium_map2_region_bind(&region,&maps,7,error,sizeof(error)));
  assert(registers==0&&!region.registered&&!region.closing);
 }
 reset();fail_register=1;
 assert(!stadium_map2_region_bind(&region,&maps,7,error,sizeof(error)));
 assert(!region.scope&&!region.buffer&&!region.registered);
 reset();assert(stadium_map2_region_bind(&region,&maps,7,error,sizeof(error)));
 assert(registers==1&&registered_base==maps.buffer&&registered_size==0x50000);
 assert(!stadium_map2_region_bind(&region,&maps,7,error,sizeof(error))&&registers==1);
 for(int mutation=0;mutation<5;++mutation){
  pending=pumping=0;scope=&first;actual=maps.buffer_allocation_lease;
  if(mutation==0)pending=1; /* Includes queued request during Asyncify wait. */
  if(mutation==1)pumping=1; /* Includes callback after request dequeue. */
  if(mutation==2)scope=&second;
  if(mutation==3)actual.allocation_generation++;
  if(mutation==4)maps.buffer=(void*)0x100020;
  assert(!stadium_map2_region_preflight(&region,&maps,7,error,sizeof(error)));
  assert(!stadium_map2_region_unregister(&region,&maps,7,error,sizeof(error)));
  assert(region.registered&&!region.closing&&unregisters==0);
  maps.buffer=(void*)0x100000;
 }
 pending=pumping=0;scope=&first;actual=maps.buffer_allocation_lease;fail_unregister=1;
 assert(!stadium_map2_region_unregister(&region,&maps,7,error,sizeof(error)));
 assert(region.registered&&!region.closing);
 fail_unregister=0;assert(stadium_map2_region_unregister(&region,&maps,7,error,sizeof(error)));
 assert(unregisters==1&&!region.registered&&region.closing);
 assert(!stadium_map2_region_preflight(&region,&maps,7,error,sizeof(error)));
 assert(!stadium_map2_region_bind(&region,&maps,7,error,sizeof(error)));
 MeleeWebStageLast h={.generation=7,.stadium_map2_region=region},foreign={.generation=7};
 active=&h;assert(!melee_web_stage_last_source_work_allowed(&h));
 assert(!melee_web_stage_last_source_work_allowed(&foreign));
 assert(melee_web_stage_last_source_work_allowed(NULL));
 h.stadium_map2_region.closing=0;assert(melee_web_stage_last_source_work_allowed(&h));
 h.stadium_first_transform.failed=1;assert(!melee_web_stage_last_source_work_allowed(&h));
 h.stadium_first_transform.failed=0;
 h.generation=8;assert(!melee_web_stage_last_source_work_allowed(&h));
 reset();assert(stadium_map2_region_preflight(&region,&maps,7,error,sizeof(error)));
 region.scope=&first;assert(!stadium_map2_region_preflight(&region,&maps,7,error,sizeof(error)));
 return 0;
}
'''
        scratch=Path(tempfile.mkdtemp(prefix='stadium-map2-region-'));passed=False
        try:
            source=scratch/'controls.c';binary=scratch/'controls';source.write_text(program)
            for index,command in enumerate(([compiler,'-std=c11','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],[str(binary)])):
                completed=subprocess.run(command,capture_output=True,text=True,timeout=60)
                (scratch/f'{index}.stdout').write_text(completed.stdout)
                (scratch/f'{index}.stderr').write_text(completed.stderr)
                self.assertEqual(completed.returncode,0,completed.stdout+completed.stderr)
            passed=True
        finally:
            if passed:shutil.rmtree(scratch)
            else:print(f'Retained map2-region controls: {scratch}',flush=True)

    def test_actual_binding_and_teardown_order_preserves_existing_graph_guards(self):
        stage=(SOURCE/'src/gameplay_stage_last.c').read_text()
        begin=function(stage,'static MeleeWebStageLast* begin_stage(')
        self.assertLess(begin.index('melee_web_stadium_display_owner_capture('),begin.index('stadium_map2_region_bind('))
        end=function(stage,'int melee_web_stage_last_end(')
        self.assertLess(end.index('stadium_map2_region_preflight('),end.index('melee_web_stadium_generator_end('))
        self.assertLess(end.index('melee_web_stadium_display_owner_preflight('),end.index('stadium_map2_region_unregister('))
        self.assertLess(end.index('stadium_map2_region_unregister('),end.index('melee_web_stadium_generator_end('))
        self.assertLess(end.rindex('stadium_map2_region_unregister('),end.index('melee_web_stadium_display_owner_end('))
        self.assertLess(end.index('stadium_map2_region_unregister('),end.index('Ground_801C4A08('))
        self.assertLess(end.index('Ground_801C4A08('),end.index('melee_web_stadium_map2_buffer_owner_end('))
        cancel=function(stage,'int melee_web_stage_last_stadium_cancel_prepared_ready(')
        self.assertLess(cancel.index('stadium_map2_region_preflight('),cancel.index('melee_web_stadium_generator_cancel_prepared('))
        for name in ('static int stadium_prepare_start(','int melee_web_stage_last_stadium_on_load('):
            self.assertIn('melee_web_stage_last_source_work_allowed(',function(stage,name))
        self.assertIn('melee_web_stage_last_source_work_allowed(',function(stage,'static int stadium_ready_phase('))


if __name__=='__main__':unittest.main()
