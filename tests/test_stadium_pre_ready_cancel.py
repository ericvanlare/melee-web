"""Checked pre-Ready cancellation: actual owner bodies, synthetic service capabilities.
No original Ready/OnStart or full Session/runtime acceptance is exercised.
"""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_stadium_on_init_boundary import function_body
ROOT=Path(__file__).resolve().parents[1]
def whole_function(source,signature):
 start=source.index(signature);opening=source.index('{',start)
 return source[start:opening+1]+function_body(source,signature)+'}'

class StadiumPreReadyCancelTests(unittest.TestCase):
 def test_actual_stage_cancel_and_session_preflight_order(self):
  cc=shutil.which('clang') or shutil.which('cc'); cxx=shutil.which('clang++') or shutil.which('c++')
  if not cc or not cxx:self.skipTest('Native C/C++ compilers required')
  stage=(ROOT/'src/gameplay_stage_last.c').read_text()
  memory=(ROOT/'src/gameplay_source_memory_runtime.h').read_text()
  memory=memory[memory.index('typedef enum MeleeWebSourceMemoryReadStatus'):memory.index('/* Fighter_Create')]
  program=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define St_Kind_PStadium 3
#define HSD_GOBJ_CLASS_STAGE 3
typedef struct HSD_GObj {struct HSD_GObj* next;int classifier;void* user_data;} HSD_GObj;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef struct {int map_id;union {struct {void* xF4;} display;}u;} Ground;
typedef struct {int captured;HSD_GObj *map0_ground,*display_ground,*map2_ground,*nested_map5_ground;} MeleeWebStadiumMap2BufferOwner;
"""+memory+r"""
typedef struct {int diagnostic_only,stage_kind,ground_kind;} Definition;
typedef struct MeleeWebStageLast {
 uint64_t generation;Definition* definition;int source_ordered,stadium_started,stadium_ready_route,stadium_ready_armed;
 void *stadium_ready_object,*stadium_ready_proc,*stadium_subject;
 struct {void* object;void* proc;}stadium_manager;
 MeleeWebSourceMemoryAllocation stadium_manager_lease,stadium_manager_proc_lease;
 void *stadium_camera_owner,*stadium_generator,*stadium_display_owner;
 MeleeWebStadiumMap2BufferOwner stadium_map2_buffer_owner;
 MeleeWebSourceMemoryContext stadium_start_context;
 void* stadium_headers[2];MeleeWebSourceMemoryAllocation stadium_header_leases[2];
} MeleeWebStageLast;
struct Pending {void* next;HSD_GObj* object;HSD_GObjEvent callback;};
static struct {int grkind;void* x6A4;HSD_GObj* map_gobjs[6];} stage_info;
static HSD_GObj* entities[6];static void* HSD_GObj_Entities=entities;
static void *HSD_GObj_804D781C,*HSD_GObj_804D7814,*HSD_GObj_804D7818,*HSD_GObj_804D7830,*HSD_GObj_804D7838;
static MeleeWebStageLast h,*active;static Definition def={1,3,7};
static HSD_GObj maps[4];static Ground grounds[4];static struct Pending headers[2];
static MeleeWebSourceMemoryAllocation leases[2];static MeleeWebSourceMemoryContext context;
static int frees,generator_cancels,display_ok,camera_ok,generator_ok;
static uint64_t generation=19;
static uint64_t melee_web_gameplay_generation(void){return generation;}
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int ok(char* e,size_t n){if(e&&n)*e=0;return 1;}
static void fn_801D13C8(HSD_GObj* g){(void)g;assert(0);}
static void fn_801D11E4(HSD_GObj* g){(void)g;assert(0);}
static const HSD_GObjEvent stadium_pending_callbacks[]={fn_801D13C8,fn_801D11E4};
static int melee_web_stage_selection_preflight(int k){return k==3;}
static int melee_web_match_camera_available(void* p,char* e,size_t n){(void)e;(void)n;return camera_ok&&p==&camera_ok;}
static int melee_web_stadium_display_owner_go_alignment_view(void* p,MeleeWebStadiumMap2BufferOwner* m,int started,int* gate,int* mode)
{assert(started==0);*gate=0;*mode=0;return display_ok&&p==&display_ok&&m==&h.stadium_map2_buffer_owner;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_context_read(MeleeWebSourceMemoryContext* out){*out=context;return MELEE_WEB_SOURCE_MEMORY_READ_OK;}
MeleeWebSourceMemoryReadStatus melee_web_source_memory_allocation_read(const void* p,MeleeWebSourceMemoryAllocation* out)
{for(int i=0;i<2;i++)if(p==&headers[i]){*out=leases[i];return MELEE_WEB_SOURCE_MEMORY_READ_OK;}return MELEE_WEB_SOURCE_MEMORY_READ_INVALID_ARGUMENT;}
static int melee_web_stadium_generator_cancel_prepared(void* p,char* e,size_t n)
{if(!generator_ok||p!=&generator_ok)return fail(e,n,"synthetic generator refusal");++generator_cancels;return 1;}
static void HSD_Free(void* p){assert(p==&headers[frees]);assert(!stage_info.x6A4);assert(generator_cancels==1);++frees;}
"""
  program+=whole_function(stage,'static int stadium_maps_live')+'\n'+whole_function(stage,'int melee_web_stage_last_stadium_cancel_prepared_ready')
  program+=r"""
void fixture_setup(void){
 memset(&h,0,sizeof(h));memset(maps,0,sizeof(maps));memset(grounds,0,sizeof(grounds));memset(&stage_info,0,sizeof(stage_info));
 memset(entities,0,sizeof(entities));active=&h;generation=19;frees=generator_cancels=0;display_ok=camera_ok=generator_ok=1;
 context=(MeleeWebSourceMemoryContext){7,19,9};h.generation=19;h.definition=&def;h.source_ordered=1;
 h.stadium_started=h.stadium_ready_route=h.stadium_ready_armed=1;h.stadium_camera_owner=&camera_ok;
 h.stadium_generator=&generator_ok;h.stadium_display_owner=&display_ok;h.stadium_start_context=context;
 int ids[]={0,1,2,5};for(int i=0;i<4;i++){grounds[i].map_id=ids[i];maps[i].user_data=&grounds[i];maps[i].classifier=3;maps[i].next=i<3?&maps[i+1]:NULL;stage_info.map_gobjs[ids[i]]=&maps[i];}
 entities[5]=maps;stage_info.grkind=7;h.stadium_map2_buffer_owner=(MeleeWebStadiumMap2BufferOwner){1,maps,maps+1,maps+2,maps+3};
 headers[0]=(struct Pending){headers+1,maps+2,fn_801D13C8};headers[1]=(struct Pending){NULL,maps+1,fn_801D11E4};stage_info.x6A4=headers;
 for(int i=0;i<2;i++){leases[i]=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.requested_bytes=sizeof(struct Pending),.world_generation=19,.allocation_generation=(uint64_t)i+1,.live=1};h.stadium_headers[i]=headers+i;h.stadium_header_leases[i]=leases[i];}
}
void* fixture_stage(void){return &h;}
int fixture_cancel(void* p,char* e,size_t n){return melee_web_stage_last_stadium_cancel_prepared_ready(p,e,n);}
void fixture_check(int success){if(success){assert(frees==2&&generator_cancels==1&&!stage_info.x6A4&&!h.stadium_started&&!h.stadium_generator);}else{assert(!frees&&!generator_cancels&&stage_info.x6A4==headers&&h.stadium_generator==&generator_ok);}}
void fixture_bad(int which){switch(which){
 case 0:stage_info.x6A4=headers+1;break;case 1:headers[0].next=NULL;break;
 case 2:leases[1].allocation_generation++;break;case 3:leases[0].live=0;break;
 case 4:h.generation++;break;case 5:h.stadium_ready_armed=2;break;
 case 6:headers[1].object=maps;break;case 7:headers[1].next=headers;break;
 case 8:generator_ok=0;break;case 9:h.stadium_subject=&h;break;
 case 10:h.stadium_manager.object=&h;break;case 11:display_ok=0;break;
 case 12:entities[5]=maps+1;break;case 13:context.source_heap_handle++;break;
 case 14:grounds[1].u.display.xF4=&h;break;case 15:HSD_GObj_804D7838=&h;break;
 default:assert(0);}}
void fixture_restore_bad(int which){if(which==15)HSD_GObj_804D7838=NULL;}
void fixture_refusal_checks(void){char e[256];
 for(int route=0;route<2;route++){fixture_setup();h.stadium_started=2;h.stadium_ready_route=route;h.stadium_ready_armed=0;
 assert(fixture_cancel(&h,e,sizeof(e)));assert(!frees&&!generator_cancels&&stage_info.x6A4==headers);}
for(int i=0;i<16;i++){
 fixture_setup();fixture_bad(i);void* root=stage_info.x6A4;void* gen=h.stadium_generator;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(e[0]&&!frees&&!generator_cancels&&stage_info.x6A4==root&&h.stadium_generator==gen);fixture_restore_bad(i);
 }
}
"""
  session=(ROOT/'src/gameplay_match_session.cpp').read_text()
  close=session[session.index('    void close(){'):]
  close=close[:close.index('        if(render)')]+ '    }\n'
  world=(ROOT/'src/gameplay_world.cpp').read_text()
  method=whole_function(world,'void GameplayWorld::cancel_prepared_stadium_ready')
  cpp=r"""
#include <cassert>
#include <stdexcept>
#include <cstdio>
#include <cstddef>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
extern "C" {void fixture_setup();void* fixture_stage();int fixture_cancel(void*,char*,size_t);void fixture_check(int);void fixture_bad(int);void fixture_refusal_checks();}
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
static int melee_web_stage_last_stadium_cancel_prepared_ready(void* p,char* e,size_t n){return fixture_cancel(p,e,n);}
struct GameplayWorld {struct Storage {bool stadium_diagnostic;void* stage_last;}*storage_;void cancel_prepared_stadium_ready();void end_stage(){++stage_ends;}static int stage_ends;};int GameplayWorld::stage_ends;
"""+method+r"""
static int hud_ends;
static int melee_web_hud_end(void*,char*,size_t){++hud_ends;return 1;}
static int melee_web_match_rules_publish_result(){assert(0);return 0;}
struct Session {GameplayWorld* world;bool stadium_diagnostic=true;void* match=nullptr;void* hud=this;struct {bool opening_demo=false;}selected;int flow_ends=0;void end_flow(){++flow_ends;}
"""+close+r"""
};
int main(){fixture_refusal_checks();for(int run=0;run<2;run++){
 fixture_setup();GameplayWorld::Storage st{true,fixture_stage()};GameplayWorld w{&st};Session s{};s.world=&w;
 fixture_bad(8);bool refused=false;try{s.close();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&!s.flow_ends&&!hud_ends&&!GameplayWorld::stage_ends&&s.hud);fixture_check(0);
 fixture_setup();s.close();fixture_check(1);assert(s.flow_ends==1&&hud_ends==1&&GameplayWorld::stage_ends==1&&!s.hud);
 hud_ends=GameplayWorld::stage_ends=0;
 }puts("Actual stage cancellation/maps and World/Session close prefix: two successes; 16 adverse refusals; no callbacks; refusal precedes flow/HUD teardown");}
"""
  scratch=Path(tempfile.mkdtemp(prefix='stadium-pre-ready-',dir=ROOT/'work'));passed=False
  try:
   c=scratch/'stage.c';c.write_text(program);cxxfile=scratch/'session.cpp';cxxfile.write_text(cpp);obj=scratch/'stage.o';binary=scratch/'control'
   commands=[[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-c',str(c),'-o',str(obj)],
    [cxx,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cxxfile),str(obj),'-o',str(binary)],[str(binary)]]
   import json
   (scratch/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
   for i,command in enumerate(commands):
    r=subprocess.run(command,capture_output=True,text=True,timeout=60);(scratch/f'{i}.stdout').write_text(r.stdout);(scratch/f'{i}.stderr').write_text(r.stderr)
    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   print(r.stdout,end='',flush=True);passed=True
  finally:
   if passed:shutil.rmtree(scratch)
   else:print(f'Retained cancellation control: {scratch}',flush=True)
