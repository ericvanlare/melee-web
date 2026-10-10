"""Actual generator constructor/owner bodies with explicit synthetic HSD services.

Native sanitizer coverage proves owner refusal/order, not the SDK allocation ABI,
map callbacks, Stage dispatch, scheduler execution, or full Stadium acceptance.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_stadium_on_init_boundary import function_body

ROOT = Path(__file__).resolve().parents[1]


class StadiumGeneratorOwnerTests(unittest.TestCase):
    def test_actual_constructor_owner_two_lifetimes_and_refusal(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        source = ROOT / ".deps/melee/src/melee/gr/grzakogenerator.c"
        if not compiler or not source.is_file():
            self.skipTest("Pinned original generator and native compiler required")
        original = source.read_text()
        types = (source.parent / "types.h").read_text()
        data_types = types[types.index("typedef struct grZakoGenerator_Entry {"):]
        data_types = data_types[:data_types.index("} grZakoGenerator_Data;") + len("} grZakoGenerator_Data;")]
        private = (source.parent / "grzakogenerator.static.h").read_text()
        private = private[private.index("static struct {"):private.index("#endif")]
        memory = (ROOT / "src/gameplay_source_memory_runtime.h").read_text()
        # Read the authoritative diagnostic records; no native mirror of them.
        memory = memory[memory.index("typedef enum MeleeWebSourceMemoryReadStatus"):memory.index("/* Fighter_Create")]
        owner = (ROOT / "src/gameplay_stadium_start.c").read_text()
        owner = "\n".join(line for line in owner.splitlines()
                          if not line.startswith("#include") and not line.startswith("#if") and line != "#endif")
        patch = (ROOT / "patches/melee-gameplay.patch").read_text()
        bridge_patch = patch[patch.index("+size_t melee_web_stadium_zako_snapshot_size"):]
        bridge_patch = bridge_patch[:bridge_patch.index("diff --git")]
        bridge = "\n".join(line[1:] for line in bridge_patch.splitlines()
                           if line.startswith("+") and line != "+#endif")
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define PAD_STACK(bytes) do { __attribute__((unused)) unsigned char _[(bytes)]; } while (0)
#define HSD_ASSERT(line,condition) assert(condition)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef int16_t s16; typedef int32_t s32; typedef uint8_t u8;
typedef struct HSD_GObj HSD_GObj;
typedef HSD_GObj Item_GObj;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef struct grZakoGenerator_SpawnDesc { int unused; } grZakoGenerator_SpawnDesc;
typedef struct HSD_GObjProc {
 struct HSD_GObjProc* child; HSD_GObj* gobj; HSD_GObjEvent on_invoke; unsigned s_link;
} HSD_GObjProc;
struct HSD_GObj {
 HSD_GObj* next; HSD_GObjProc* proc;
 unsigned classifier,p_link; void* hsd_obj; void* user_data; void* user_data_remove_func;
};
typedef struct MeleeWebStadiumGenerator MeleeWebStadiumGenerator;
static HSD_GObj* entities[8];
static void* HSD_GObj_Entities=entities;
static void *HSD_GObj_804D781C,*HSD_GObj_804D7814;
static uint64_t melee_web_gameplay_generation(void){return 1;}
static void fn_801CADBC(HSD_GObj* g){(void)g;abort();} /* Never dispatch. */
'''
        program += data_types + private + memory
        program += r'''
static void* payload;static MeleeWebSourceMemoryAllocation allocation;
static uint64_t watermark;static unsigned object_frees,data_frees;
static MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read_impl(MeleeWebSourceMemoryContext* out)
{*out=(MeleeWebSourceMemoryContext){7,1,watermark};return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
#define melee_web_source_memory_context_read melee_web_source_memory_context_read_impl
static MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read_impl(const void* p,MeleeWebSourceMemoryAllocation* out)
{assert(p==payload);*out=allocation;
 if(!allocation.live)*out=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.world_generation=1};
 return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
#define melee_web_source_memory_allocation_read melee_web_source_memory_allocation_read_impl
static void* HSD_MemAlloc(size_t size)
{
 assert(!allocation.live);payload=calloc(1,size);assert(payload);
 allocation=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.requested_bytes=(uint32_t)size,
 .world_generation=1,.allocation_generation=++watermark,.live=1};return payload;
}
static void HSD_Free(void* p)
{assert(p==payload&&allocation.live);allocation.live=0;++data_frees;free(p);}
static HSD_GObj* GObj_Create(unsigned classifier,unsigned link,unsigned priority)
{
 assert(classifier==2&&link==4&&priority==0&&!entities[4]);
 HSD_GObj* g=calloc(1,sizeof(*g));assert(g);g->classifier=classifier;g->p_link=link;entities[4]=g;return g;
}
static void HSD_GObj_SetupProc(HSD_GObj* g,HSD_GObjEvent callback,unsigned link)
{assert(link==0);g->proc=calloc(1,sizeof(*g->proc));assert(g->proc);g->proc->gobj=g;g->proc->on_invoke=callback;g->proc->s_link=link;}
static void HSD_GObjPLink_80390228(HSD_GObj* g)
{assert(g==entities[4]&&allocation.live);entities[4]=NULL;free(g->proc);free(g);++object_frees;}
static void OSReport(const char* format,const char* file,int line)
{(void)format;(void)file;(void)line;abort();}
/* These extracted cold-lifecycle controls never enter the HUD-ready path. */
static unsigned hud_ready_context_calls;
static int melee_web_hud_stadium_ready_context(void* object,void* proc)
{(void)object;(void)proc;++hud_ready_context_calls;return 0;}
int melee_web_stadium_generator_preflight(MeleeWebStadiumGenerator*,char*,size_t);
'''
        program += "grZakoGenerator_Data* grZakoGenerator_801CA67C(void){" + function_body(original, "grZakoGenerator_Data* grZakoGenerator_801CA67C(") + "}\n"
        program += "HSD_GObj* grZakoGenerator_801CAE04(grZakoGenerator_SpawnDesc* arg0){" + function_body(original, "HSD_GObj* grZakoGenerator_801CAE04(") + "}\n"
        program += bridge + owner
        program += r'''
int main(void)
{
 char error[256];unsigned char snapshot[sizeof(lbl_8049F030)];
 memcpy(snapshot,&lbl_8049F030,sizeof(snapshot));
 for(unsigned repeat=0;repeat<2;++repeat){
  MeleeWebStadiumGenerator* h=melee_web_stadium_generator_prepare(error,sizeof(error));assert(h);
  assert(grZakoGenerator_801CAE04(NULL));
  assert(melee_web_stadium_generator_capture(h,error,sizeof(error)));
  unsigned before_data=data_frees,before_object=object_frees;
  HSD_GObj borrowed={0};
  h->data->entries[ARRAY_SIZE(h->data->entries)-1].x4=&borrowed;
  assert(!melee_web_stadium_generator_end(h,error,sizeof(error)));
  assert(strstr(error,"item borrowers")&&data_frees==before_data&&object_frees==before_object);
  h->data->entries[ARRAY_SIZE(h->data->entries)-1].x4=NULL;
  ++allocation.allocation_generation;
  assert(!melee_web_stadium_generator_end(h,error,sizeof(error)));
  assert(data_frees==before_data&&object_frees==before_object);
  --allocation.allocation_generation;
  h->proc->s_link=1;
  assert(!melee_web_stadium_generator_end(h,error,sizeof(error)));
  assert(data_frees==before_data&&object_frees==before_object);h->proc->s_link=0;
  lbl_8049F030.x0=(void*)&borrowed;
  assert(!melee_web_stadium_generator_end(h,error,sizeof(error)));
  assert(data_frees==before_data&&object_frees==before_object);lbl_8049F030.x0=NULL;
  assert(melee_web_stadium_generator_end(h,error,sizeof(error)));
  assert(!allocation.live&&!entities[4]);
  assert(data_frees==before_data+1&&object_frees==before_object+1);
  assert(!memcmp(snapshot,&lbl_8049F030,sizeof(snapshot)));
 }
 assert(hud_ready_context_calls==0);
 puts("Actual original generator constructor/owner repeated; synthetic HSD services; item/lease/proc/root refusal preserves owners");
 return 0;
}
'''
        scratch = Path(tempfile.mkdtemp(prefix="stadium-generator-native-",dir=ROOT / "work"))
        passed = False
        try:
            path = scratch / "generator.c"; path.write_text(program)
            binary = scratch / "generator"
            command = [compiler,"-std=c11","-O1","-Wall","-Wextra","-Werror",
                       "-ffp-contract=off","-fsanitize=address,undefined",str(path),"-o",str(binary)]
            (scratch / "command.txt").write_text(" ".join(command)+"\n")
            build = subprocess.run(command,capture_output=True,text=True,timeout=60)
            (scratch / "compile.stdout").write_text(build.stdout)
            (scratch / "compile.stderr").write_text(build.stderr)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run = subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            (scratch / "stdout").write_text(run.stdout);(scratch / "stderr").write_text(run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end="",flush=True);passed=True
        finally:
            if passed: shutil.rmtree(scratch)
            else: print(f"Retained generator native reducer: {scratch}",flush=True)

    def test_actual_camera_partition_allocate_return_and_refusal(self):
        compiler = shutil.which("clang") or shutil.which("cc")
        source = ROOT / ".deps/melee/src/melee/cm/camera.c"
        if not compiler or not source.is_file():
            self.skipTest("Pinned original Camera and native compiler required")
        original = source.read_text()
        types = (source.parent / "types.h").read_text()
        types = types[types.index("typedef struct CmSubjectExtents {"):types.index("struct CameraTransformState {")]
        enum = (source.parent / "forward.h").read_text()
        enum = enum[enum.index("typedef enum CmSubjectState {"):enum.index("} CmSubjectState;")+len("} CmSubjectState;")]
        match = (ROOT / "src/gameplay_match_context.c").read_text()
        helpers = match[match.index("static int camera_index("):match.index("MeleeWebMatchContext* melee_web_match_begin(")]
        helpers = helpers.replace("#endif", "")
        memory = (ROOT / "src/gameplay_source_memory_runtime.h").read_text()
        memory = memory[memory.index("typedef enum MeleeWebSourceMemoryReadStatus"):memory.index("/* Fighter_Create")]
        program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef uint8_t u8;typedef int16_t s16;typedef uint32_t u32;
typedef struct Vec2 {float x,y;} Vec2;typedef struct Vec3 {float x,y,z;} Vec3;
typedef struct CmSubject CmSubject;
'''+enum+types+memory+r'''
typedef struct MeleeWebMatchContext {
 uint64_t generation;uint32_t seed,camera_count;CmSubject* pool;
 MeleeWebSourceMemoryAllocation camera_lease;
} MeleeWebMatchContext;
static MeleeWebMatchContext* owner;
static u32* seed_ptr;
static CmSubject *cm_804D6458,*cm_804D645C,*cm_804D6460,*cm_804D6468;
static void *HSD_GObj_804D781C,*HSD_GObj_804D7814;
static MeleeWebSourceMemoryAllocation lease;
static uint64_t melee_web_gameplay_generation(void){return 1;}
static int fail(char* e,size_t n,const char* m){snprintf(e,n,"%s",m);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* c)
{*c=(MeleeWebSourceMemoryContext){7,1,lease.allocation_generation};return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(const void* p,MeleeWebSourceMemoryAllocation* l)
{assert(p==owner->pool);*l=lease;return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
static void OSReport(const char* format,int arg){(void)format;(void)arg;abort();}
/* This synthetic pool control checks non-ready owner paths only. */
static unsigned hud_ready_context_calls;
static int melee_web_hud_stadium_ready_context(void* object,void* proc)
{(void)object;(void)proc;++hud_ready_context_calls;return 0;}
'''
        program += "static int owned(MeleeWebMatchContext* h,char* e,size_t n){"+function_body(match,"static int owned(")+"}\n"
        program += "void Camera_80028F5C(CmSubject* subject,CmSubjectState state){"+function_body(original,"void Camera_80028F5C(")+"}\n"
        program += "CmSubject* Camera_80029044(int arg0){"+function_body(original,"CmSubject* Camera_80029044(")+"}\n"
        program += "void Camera_800290D4(CmSubject* subject){"+function_body(original,"void Camera_800290D4(")+"}\n"
        program += helpers+r'''
int main(void)
{
 char error[256];
 /* Explicit synthetic MatchContext/pool construction; actual original Camera
  * allocation/reset/return and downstream checked owner bodies follow. */
 for(unsigned repeat=0;repeat<2;++repeat){
  CmSubject pool[3]={0};pool[0].prev=&pool[1];pool[1].prev=&pool[2];
  lease=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.world_generation=1,
   .allocation_generation=repeat+1,.requested_bytes=sizeof(pool),.live=1};
  MeleeWebMatchContext h={.generation=1,.camera_count=3,.pool=pool,.camera_lease=lease};
  owner=&h;seed_ptr=&h.seed;cm_804D645C=cm_804D6458=pool;cm_804D6460=cm_804D6468=NULL;
  assert(melee_web_match_camera_available(&h,error,sizeof(error)));
  CmSubject* other=Camera_80029044(CmSubjectState_Auto);
  CmSubject* stage=Camera_80029044(CmSubjectState_Inactive);
  assert(melee_web_match_camera_subject_preflight(&h,stage,error,sizeof(error)));
  CmSubject saved_other=*other, saved_pool[3], foreign={0};memcpy(saved_pool,pool,sizeof(pool));
  CmSubject *head=cm_804D6460,*tail=cm_804D6468,*free_head=cm_804D6458;
  assert(!melee_web_match_camera_subject_return(&h,&foreign,error,sizeof(error)));
  assert(!memcmp(saved_pool,pool,sizeof(pool))&&cm_804D6460==head&&cm_804D6468==tail&&cm_804D6458==free_head);
  stage->next=other; /* Cycle: validator must refuse before original return. */
  memcpy(saved_pool,pool,sizeof(pool));
  assert(!melee_web_match_camera_subject_return(&h,stage,error,sizeof(error)));
  assert(!memcmp(saved_pool,pool,sizeof(pool)));stage->next=NULL;
  cm_804D6458=other; /* Free/active alias. */
  memcpy(saved_pool,pool,sizeof(pool));
  assert(!melee_web_match_camera_subject_return(&h,stage,error,sizeof(error)));
  assert(!memcmp(saved_pool,pool,sizeof(pool))&&cm_804D6458==other);cm_804D6458=free_head;
  ++lease.allocation_generation;
  memcpy(saved_pool,pool,sizeof(pool));
  assert(!melee_web_match_camera_subject_return(&h,stage,error,sizeof(error)));
  assert(!memcmp(saved_pool,pool,sizeof(pool)));--lease.allocation_generation;
  assert(melee_web_match_camera_subject_return(&h,stage,error,sizeof(error)));
  /* Returning the appended subject changes other's next to NULL; every other
   * state/float byte must remain unchanged. */
  saved_other.next=NULL;assert(!memcmp(&saved_other,other,sizeof(*other)));
  assert(cm_804D6460==other&&cm_804D6468==other&&cm_804D6458==stage);
  assert(melee_web_match_camera_subject_return(&h,other,error,sizeof(error)));
  assert(!cm_804D6460&&!cm_804D6468&&melee_web_match_camera_available(&h,error,sizeof(error)));
 }
 assert(hud_ready_context_calls==0);
 puts("Actual original Camera allocate/reset/return and MatchContext partition guards repeated; synthetic context construction; foreign/cycle/alias/lease refusal preserves pool");
 return 0;
}
'''
        scratch = Path(tempfile.mkdtemp(prefix="stadium-camera-native-",dir=ROOT / "work"))
        passed = False
        try:
            path=scratch/"camera.c";path.write_text(program);binary=scratch/"camera"
            command=[compiler,"-std=c11","-O1","-Wall","-Wextra","-Werror","-ffp-contract=off",
                     "-fsanitize=address,undefined",str(path),"-o",str(binary)]
            (scratch/"command.txt").write_text(" ".join(command)+"\n")
            build=subprocess.run(command,capture_output=True,text=True,timeout=60)
            (scratch/"compile.stdout").write_text(build.stdout);(scratch/"compile.stderr").write_text(build.stderr)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            (scratch/"stdout").write_text(run.stdout);(scratch/"stderr").write_text(run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end="",flush=True);passed=True
        finally:
            if passed:shutil.rmtree(scratch)
            else:print(f"Retained camera native reducer: {scratch}",flush=True)

    def test_actual_selected_stage_guard_positive_and_foreign_pair(self):
        import re
        compiler=shutil.which("clang") or shutil.which("cc")
        if not compiler:self.skipTest("Native compiler required")
        original=(ROOT/".deps/melee/src/melee/gr/stage.c").read_text()
        table=original[original.index("struct StageIdMapEntry stage_id_map[] = {"):]
        table=table[:table.index("};")+2]
        # Keep the authored initializer and its bound. Synthetic enum values
        # only make this pointer-identity test independent of unrelated enums.
        names=sorted(set(re.findall(r"Gr_Kind_\w+",table)))
        enums="enum {"+",".join(names)+"};\n"
        patch=(ROOT/"patches/melee-gameplay.patch").read_text()
        block=patch[patch.index("+static struct StageSelection melee_web_saved_stage_selection;"):]
        block=block[:block.index(" GrKind Stage_8022519C")]
        helpers="\n".join(line[1:] for line in block.splitlines() if line.startswith("+") and not line.startswith("+#"))
        self.assertIn("int melee_web_stage_selection_preflight",helpers)
        program=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
struct StageIdMapEntry {int grkind,flag0,flag1;};
struct StageSelection {int stkind;struct StageIdMapEntry* entry;};
static struct StageSelection selected_stage;
'''+enums+table+helpers+r'''
int main(void)
{
 for(unsigned i=0;i<ARRAY_SIZE(stage_id_map);++i){
  assert(melee_web_stage_selection_begin(i));
  assert(melee_web_stage_selection_preflight(i));
  struct StageSelection saved=selected_stage;
  struct StageIdMapEntry foreign=*selected_stage.entry;
  selected_stage.entry=&foreign;
  struct StageSelection altered=selected_stage;
  assert(!melee_web_stage_selection_preflight(i));
  assert(!memcmp(&altered,&selected_stage,sizeof(altered)));
  selected_stage=saved;selected_stage.stkind=(int)ARRAY_SIZE(stage_id_map);
  altered=selected_stage;
  assert(!melee_web_stage_selection_preflight(i));
  assert(!memcmp(&altered,&selected_stage,sizeof(altered)));
  selected_stage=saved;
  assert(!melee_web_stage_selection_preflight(-1));
  assert(!melee_web_stage_selection_preflight(ARRAY_SIZE(stage_id_map)));
  assert(!memcmp(&saved,&selected_stage,sizeof(saved)));
  assert(melee_web_stage_selection_end());
  saved=selected_stage;assert(!melee_web_stage_selection_preflight(i));
  assert(!memcmp(&saved,&selected_stage,sizeof(saved)));
 }
 puts("Actual selected-stage ownership guard: authored bounds, positive identity, foreign equal-value pair and unowned/refusal preserve state");return 0;
}
'''
        scratch=Path(tempfile.mkdtemp(prefix="stadium-selection-native-",dir=ROOT/"work"));passed=False
        try:
            path=scratch/"selection.c";path.write_text(program);binary=scratch/"selection"
            # Existing selection_begin uses an int/sizeof comparison. Keep that
            # original source warning visible rather than rewrite its body.
            command=[compiler,"-std=c11","-O1","-Wall","-Wextra","-Werror",
                     "-Wno-error=sign-compare","-fsanitize=address,undefined",str(path),"-o",str(binary)]
            (scratch/"command.txt").write_text(" ".join(command)+"\n")
            build=subprocess.run(command,capture_output=True,text=True,timeout=60)
            (scratch/"compile.stdout").write_text(build.stdout);(scratch/"compile.stderr").write_text(build.stderr)
            if build.stderr:print(build.stderr,end="",flush=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            (scratch/"stdout").write_text(run.stdout);(scratch/"stderr").write_text(run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end="",flush=True);passed=True
        finally:
            if passed:shutil.rmtree(scratch)
            else:print(f"Retained selected-stage native control: {scratch}",flush=True)

    def test_actual_tracker_erases_and_recycles_exact_payload(self):
        compiler=shutil.which("clang++") or shutil.which("c++")
        if not compiler:self.skipTest("Native C++ compiler required")
        program=r'''
#include "gameplay_source_memory_runtime.h"
#include <cassert>
#include <cstdio>
int main(void)
{
 alignas(32) unsigned char payload[64]={};
 assert(melee_web_source_memory_begin(19,7));
 MeleeWebSourceMemoryContext before{};
 assert(melee_web_source_memory_context_read(&before)==MELEE_WEB_SOURCE_MEMORY_READ_OK);
 assert(melee_web_source_memory_alloc(7,payload,sizeof(payload)));
 MeleeWebSourceMemoryAllocation live{};
 assert(melee_web_source_memory_allocation_read(payload,&live)==MELEE_WEB_SOURCE_MEMORY_READ_OK);
 assert(live.live&&live.requested_bytes==sizeof(payload)&&live.allocation_generation>before.allocation_generation_watermark);
 MeleeWebSourceMemoryContext pre_drain{};
 assert(melee_web_source_memory_context_read(&pre_drain)==MELEE_WEB_SOURCE_MEMORY_READ_OK);
 assert(melee_web_source_memory_free(7,payload));
 MeleeWebSourceMemoryAllocation absent{};
 assert(melee_web_source_memory_allocation_read(payload,&absent)==MELEE_WEB_SOURCE_MEMORY_READ_OK);
 assert(!absent.live&&!absent.requested_bytes&&!absent.allocation_generation&&absent.world_generation==19&&absent.source_heap_handle==7);
 assert(melee_web_source_memory_alloc(7,payload,sizeof(payload)));
 MeleeWebSourceMemoryAllocation replacement{};
 assert(melee_web_source_memory_allocation_read(payload,&replacement)==MELEE_WEB_SOURCE_MEMORY_READ_OK);
 assert(replacement.live&&replacement.allocation_generation>pre_drain.allocation_generation_watermark&&replacement.world_generation==19&&replacement.source_heap_handle==7);
 assert(melee_web_source_memory_free(7,payload));
 assert(melee_web_source_memory_healthy());assert(melee_web_source_memory_end(19));
 puts("Actual source-memory tracker: erased record has zero live/request/generation; same-address replacement advances pre-drain watermark; explicit synthetic host allocation events");return 0;
}
'''
        scratch=Path(tempfile.mkdtemp(prefix="stadium-tracker-native-",dir=ROOT/"work"));passed=False
        try:
            path=scratch/"tracker.cpp";path.write_text(program);binary=scratch/"tracker"
            command=[compiler,"-std=c++20","-O1","-Wall","-Wextra","-Werror","-ffp-contract=off",
                     "-fsanitize=address,undefined","-I",str(ROOT/"src"),str(path),
                     str(ROOT/"src/gameplay_source_memory_runtime.cpp"),str(ROOT/"src/source_address_context.cpp"),
                     str(ROOT/"src/source_game_heap_context.cpp"),"-o",str(binary)]
            (scratch/"command.txt").write_text(" ".join(command)+"\n")
            build=subprocess.run(command,capture_output=True,text=True,timeout=60)
            (scratch/"compile.stdout").write_text(build.stdout);(scratch/"compile.stderr").write_text(build.stderr)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            (scratch/"stdout").write_text(run.stdout);(scratch/"stderr").write_text(run.stderr)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout,end="",flush=True);passed=True
        finally:
            if passed:shutil.rmtree(scratch)
            else:print(f"Retained tracker native control: {scratch}",flush=True)
