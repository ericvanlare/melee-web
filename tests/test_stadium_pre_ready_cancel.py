"""Checked pre-Ready cancellation: actual owner bodies, synthetic service capabilities.
No original Ready/OnStart or full Session/runtime acceptance is exercised.
"""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from test_stadium_on_init_boundary import function_body
ROOT=Path(__file__).resolve().parents[1]
SOURCE_ROOT=Path(os.environ.get('MELEE_WEB_CANDIDATE_SOURCE_ROOT',ROOT))
REFERENCE_ROOT=Path(os.environ.get('MELEE_WEB_REFERENCE_SOURCE_ROOT',ROOT))
def whole_function(source,signature):
 start=source.index(signature);opening=source.index('{',start)
 return source[start:opening+1]+function_body(source,signature)+'}'

class StadiumPreReadyCancelTests(unittest.TestCase):
 def test_actual_stage_cancel_and_session_preflight_order(self):
  cc=shutil.which('clang') or shutil.which('cc'); cxx=shutil.which('clang++') or shutil.which('c++')
  if not cc or not cxx:self.skipTest('Native C/C++ compilers required')
  stage=(SOURCE_ROOT/'src/gameplay_stage_last.c').read_text()
  memory=(SOURCE_ROOT/'src/gameplay_source_memory_runtime.h').read_text()
  service=(SOURCE_ROOT/'src/gameplay_source_files.c').read_text()
  memory=memory[memory.index('typedef enum MeleeWebSourceMemoryReadStatus'):memory.index('/* Fighter_Create')]
  display=(SOURCE_ROOT/'src/gameplay_stadium_display_owner.h').read_text()
  journal=display[display.index('#define MELEE_WEB_STADIUM_FIRST_TRANSFORM_EVENTS'):display.index('} MeleeWebStadiumFirstTransformJournal;')+len('} MeleeWebStadiumFirstTransformJournal;')]
  journal_helper=''
  if 'static inline unsigned melee_web_stadium_first_transform_event_count(' in display:
   journal_helper=whole_function(display,'static inline unsigned melee_web_stadium_first_transform_event_count(')+'\n'
  header=(SOURCE_ROOT/'src/gameplay_source_files.h').read_text()
  completion=header[header.index('typedef struct MeleeWebSourceFileRecordedCompletionStatus {'):header.index('} MeleeWebSourceFileRecordedCompletionStatus;')+len('} MeleeWebSourceFileRecordedCompletionStatus;')]
  region=stage[stage.index('typedef struct MeleeWebStadiumMap2WritableRegion'):stage.index('static int stadium_map2_region_lease_matches')]
  program=r"""
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define St_Kind_PStadium 3
#define HSD_GOBJ_CLASS_STAGE 3
typedef struct HSD_GObj {struct HSD_GObj* next;int classifier;void* user_data;} HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
typedef void (*HSD_GObjEvent)(HSD_GObj*);
typedef struct {int map_id;union {struct {void* xF4;} display;}u;} Ground;

"""+memory+journal+completion+r"""
/* Unused map-2 fallback storage is explicit in this cancellation-only fixture.
 * Any attempt to activate the new file capability refuses; focused region
 * controls separately exercise registered leases and pending/closing state. */
typedef struct MeleeWebSourceFileScope {
 size_t count;void* files;void* copies;void* regions;void* requests;
 void* request_tail;size_t request_count;int pumping;
 struct {MeleeWebSourceFileRecordedCompletionStatus status;} recorded;
} MeleeWebSourceFileScope;
enum {MELEE_WEB_STADIUM_MAP2_BUFFER_ORIGIN_OWNED_FALLBACK=2};
typedef struct {int captured;HSD_GObj *map0_ground,*display_ground,*map2_ground,*nested_map5_ground;
 void* buffer;int origin;MeleeWebSourceMemoryAllocation buffer_allocation_lease;int buffer_allocation_lease_captured;} MeleeWebStadiumMap2BufferOwner;
"""+region+r"""
typedef struct {int diagnostic_only,stage_kind,ground_kind;} Definition;
typedef struct MeleeWebStageLast {
 uint64_t generation;Definition* definition;int source_ordered,stadium_started,stadium_ready_route,stadium_ready_armed;
 void *stadium_ready_object,*stadium_ready_proc,*stadium_subject;
 struct {void* object;void* proc;}stadium_manager;
 MeleeWebSourceMemoryAllocation stadium_manager_lease,stadium_manager_proc_lease;
 void *stadium_camera_owner,*stadium_generator,*stadium_display_owner;uint64_t stadium_cancel_transfer_token;
 MeleeWebStadiumMap2BufferOwner stadium_map2_buffer_owner;
 MeleeWebStadiumMap2WritableRegion stadium_map2_region;
 MeleeWebSourceMemoryContext stadium_start_context;
 MeleeWebStadiumFirstTransformJournal stadium_first_transform;
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
static int transfer_prepare_calls,transfer_abort_calls,transfer_publish_calls;
static int transfer_prepare_ok;static uint64_t transfer_token;
static uint64_t generation=19;
static uint64_t melee_web_gameplay_generation(void){return generation;}
static struct {uint64_t generation;} current_stats;
#define melee_web_gameplay_stats() current_stats
static int file_capability_calls;
static MeleeWebSourceFileScope source_scope;
static MeleeWebSourceFileScope* active_scope;
static MeleeWebSourceFileScope* melee_web_source_files_current_scope(void){++file_capability_calls;return NULL;}
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
static int melee_web_stadium_display_owner_prepare_cancel_transfer(void* owner,const void* stage,uint64_t world,uint64_t* out,char* e,size_t n)
{++transfer_prepare_calls;assert(owner==&display_ok&&stage==&h&&world==h.generation);if(!transfer_prepare_ok)return fail(e,n,"synthetic transfer preparation refusal");transfer_token=UINT64_C(0x4A110C);*out=transfer_token;return 1;}
static void melee_web_stadium_display_owner_abort_cancel_transfer(void* owner,const void* stage,uint64_t world,uint64_t token)
{assert(owner==&display_ok&&stage==&h&&world==h.generation&&token==transfer_token);assert(!generator_cancels&&!frees&&stage_info.x6A4==headers);++transfer_abort_calls;}
static void melee_web_stadium_display_owner_publish_cancel_transfer(void* owner,uint64_t token)
{assert(owner==&display_ok&&token==transfer_token);assert(generator_cancels==1&&frees==2&&!stage_info.x6A4&&!h.stadium_started&&!h.stadium_ready_route&&!h.stadium_ready_armed);assert(h.stadium_cancel_transfer_token==token);++transfer_publish_calls;}
/* The pre-Ready fixture cannot authenticate an entered transformed graph. */
static int melee_web_stadium_display_owner_first_transform_graph(void* p,int retiring,char* e,size_t n)
{(void)p;(void)retiring;return fail(e,n,"transformed graph outside pre-Ready fixture");}
static int melee_web_stadium_generator_cancel_prepared(void* p,char* e,size_t n)
{if(!generator_ok||p!=&generator_ok)return fail(e,n,"synthetic generator refusal");++generator_cancels;return 1;}
static void HSD_Free(void* p){assert(p==&headers[frees]);assert(!stage_info.x6A4);assert(generator_cancels==1);++frees;}
"""
  program+=whole_function(service,'int melee_web_source_files_quiescent_for(')+'\n'
  program+=whole_function(stage,'static int stadium_map2_region_lease_matches')+'\n'+whole_function(stage,'static int stadium_map2_region_preflight')+'\n'+whole_function(stage,'int melee_web_stage_last_source_work_allowed')+'\n'+whole_function(stage,'static int stadium_maps_live')+'\n'+whole_function(stage,'int melee_web_stage_last_stadium_cancel_prepared_ready')
  program+=r"""
MeleeWebSourceFileScope* fixture_source_scope(void){return &source_scope;}
void fixture_set_source_busy(int busy){source_scope.requests=busy?(void*)1:NULL;source_scope.request_count=busy?1:0;source_scope.pumping=0;}
void fixture_setup(void){
 memset(&h,0,sizeof(h));memset(maps,0,sizeof(maps));memset(grounds,0,sizeof(grounds));memset(&stage_info,0,sizeof(stage_info));
 memset(entities,0,sizeof(entities));memset(&source_scope,0,sizeof(source_scope));active_scope=&source_scope;active=&h;generation=19;current_stats.generation=19;file_capability_calls=0;frees=generator_cancels=0;display_ok=camera_ok=generator_ok=1;
 transfer_prepare_calls=transfer_abort_calls=transfer_publish_calls=0;transfer_prepare_ok=1;transfer_token=0;
 context=(MeleeWebSourceMemoryContext){7,19,9};h.generation=19;h.definition=&def;h.source_ordered=1;
 h.stadium_started=h.stadium_ready_route=h.stadium_ready_armed=1;h.stadium_camera_owner=&camera_ok;
 h.stadium_generator=&generator_ok;h.stadium_display_owner=&display_ok;h.stadium_start_context=context;
 int ids[]={0,1,2,5};for(int i=0;i<4;i++){grounds[i].map_id=ids[i];maps[i].user_data=&grounds[i];maps[i].classifier=3;maps[i].next=i<3?&maps[i+1]:NULL;stage_info.map_gobjs[ids[i]]=&maps[i];}
 entities[5]=maps;stage_info.grkind=7;h.stadium_map2_buffer_owner=(MeleeWebStadiumMap2BufferOwner){.captured=1,.map0_ground=maps,.display_ground=maps+1,.map2_ground=maps+2,.nested_map5_ground=maps+3};
 headers[0]=(struct Pending){headers+1,maps+2,fn_801D13C8};headers[1]=(struct Pending){NULL,maps+1,fn_801D11E4};stage_info.x6A4=headers;
 for(int i=0;i<2;i++){leases[i]=(MeleeWebSourceMemoryAllocation){.source_heap_handle=7,.requested_bytes=sizeof(struct Pending),.world_generation=19,.allocation_generation=(uint64_t)i+1,.live=1};h.stadium_headers[i]=headers+i;h.stadium_header_leases[i]=leases[i];}
}
void* fixture_stage(void){return &h;}
int fixture_cancel(void* p,char* e,size_t n){return melee_web_stage_last_stadium_cancel_prepared_ready(p,e,n);}
void fixture_check(int success){if(success){assert(frees==2&&generator_cancels==1&&!stage_info.x6A4&&!h.stadium_started&&!h.stadium_generator);assert(transfer_prepare_calls==1&&transfer_abort_calls==0&&transfer_publish_calls==1&&h.stadium_cancel_transfer_token==transfer_token);}else{assert(!frees&&!generator_cancels&&stage_info.x6A4==headers&&h.stadium_generator==&generator_ok);}}
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
 fixture_setup();h.stadium_map2_region.closing=1;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(e[0]&&!frees&&!generator_cancels&&!transfer_prepare_calls&&!file_capability_calls);
 fixture_setup();h.stadium_map2_region.scope=(MeleeWebSourceFileScope*)&h;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(e[0]&&!frees&&!generator_cancels&&!transfer_prepare_calls&&!file_capability_calls);
 for(int route=0;route<2;route++){fixture_setup();h.stadium_started=2;h.stadium_ready_route=route;h.stadium_ready_armed=0;
 assert(fixture_cancel(&h,e,sizeof(e)));assert(!frees&&!generator_cancels&&stage_info.x6A4==headers);assert(!transfer_prepare_calls&&!transfer_abort_calls&&!transfer_publish_calls);}
 fixture_setup();h.stadium_first_transform.failed=1;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(!frees&&!generator_cancels&&!transfer_prepare_calls);
 fixture_setup();transfer_prepare_ok=0;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(e[0]&&!frees&&!generator_cancels&&stage_info.x6A4==headers&&h.stadium_generator==&generator_ok);
 assert(transfer_prepare_calls==1&&!transfer_abort_calls&&!transfer_publish_calls);
for(int i=0;i<16;i++){
 fixture_setup();fixture_bad(i);void* root=stage_info.x6A4;void* gen=h.stadium_generator;
 assert(!fixture_cancel(&h,e,sizeof(e)));assert(e[0]&&!frees&&!generator_cancels&&stage_info.x6A4==root&&h.stadium_generator==gen);
 if(i==8)assert(transfer_prepare_calls==1&&transfer_abort_calls==1&&!transfer_publish_calls);else assert(!transfer_prepare_calls&&!transfer_abort_calls&&!transfer_publish_calls);
 fixture_restore_bad(i);
 }
}
"""
  session=(SOURCE_ROOT/'src/gameplay_match_session.cpp').read_text()
  close=session[session.index('    void close(){'):]
  close=close[:close.index('        if(render)')]+ '    }\n'
  world=(SOURCE_ROOT/'src/gameplay_world.cpp').read_text()
  method=whole_function(world,'void GameplayWorld::cancel_prepared_stadium_ready')
  quiescent_method=whole_function(world,'void GameplayWorld::ensure_source_files_quiescent()const{')
  cpp=r"""
#include <cassert>
#include <stdexcept>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
using DatError=std::runtime_error;
typedef struct HSD_GObj HSD_GObj;
typedef struct HSD_GObjProc HSD_GObjProc;
"""+journal+journal_helper+completion+r"""
struct MeleeWebSourceFileScope;
extern "C" {int melee_web_source_files_quiescent_for(MeleeWebSourceFileScope*,char*,size_t);void fixture_setup();void* fixture_stage();MeleeWebSourceFileScope* fixture_source_scope();void fixture_set_source_busy(int);int fixture_cancel(void*,char*,size_t);void fixture_check(int);void fixture_bad(int);void fixture_refusal_checks();}
static void check(bool v,const char* e){if(!v)throw std::runtime_error(e);}
static int melee_web_stage_last_stadium_cancel_prepared_ready(void* p,char* e,size_t n){return fixture_cancel(p,e,n);}
struct GameplayWorld {struct Storage {bool stadium_diagnostic;void* stage_last;MeleeWebSourceFileScope* source_files;}*storage_;bool diagnostic_stadium_first_transform_can_end(char* e,size_t n)const{std::snprintf(e,n,"first-Fire outside cancellation fixture");return false;}
 bool diagnostic_stadium_first_transform_snapshot(MeleeWebStadiumFirstTransformJournal* out,char* e,size_t n)const{(void)out;std::snprintf(e,n,"no first-Fire graph capability");return false;}
 bool diagnostic_stadium_first_transform_completion_status(MeleeWebSourceFileRecordedCompletionStatus* out,char* e,size_t n)const{(void)out;std::snprintf(e,n,"no recorded completion capability");return false;}
 void cancel_prepared_stadium_ready();void ensure_source_files_quiescent()const;void end_stage(){ensure_source_files_quiescent();++stage_ends;}static int stage_ends;};int GameplayWorld::stage_ends;
"""+quiescent_method+method+r"""
static int hud_ends;
static int melee_web_hud_end(void*,char*,size_t){++hud_ends;return 1;}
static int melee_web_match_rules_publish_result(){assert(0);return 0;}
struct Session {GameplayWorld* world;bool stadium_diagnostic=true,first_fire_diagnostic=false,first_water_diagnostic=false;MeleeWebStadiumFirstTransformJournal first_transform_retired{};MeleeWebSourceFileRecordedCompletionStatus first_completion_retired{};void* match=nullptr;void* hud=this;struct {bool opening_demo=false;}selected;int flow_ends=0;void end_flow(){++flow_ends;}
"""+close+r"""
};
int main(){fixture_refusal_checks();for(int run=0;run<2;run++){
 fixture_setup();GameplayWorld::Storage st{true,fixture_stage(),fixture_source_scope()};GameplayWorld w{&st};Session s{};s.world=&w;
 s.first_fire_diagnostic=true;bool fire_refused=false;try{s.close();}catch(const std::runtime_error&){fire_refused=true;}
 assert(fire_refused&&!s.flow_ends&&!hud_ends&&!GameplayWorld::stage_ends&&s.hud);fixture_check(0);s.first_fire_diagnostic=false;
 fixture_set_source_busy(1);bool refused=false;try{s.close();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&!s.flow_ends&&!hud_ends&&!GameplayWorld::stage_ends&&s.hud);fixture_check(0);
 fixture_setup();st={true,fixture_stage(),fixture_source_scope()};refused=false;fixture_bad(8);try{s.close();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&!s.flow_ends&&!hud_ends&&!GameplayWorld::stage_ends&&s.hud);fixture_check(0);
 fixture_setup();st={true,fixture_stage(),fixture_source_scope()};s.close();fixture_check(1);assert(s.flow_ends==1&&hud_ends==1&&GameplayWorld::stage_ends==1&&!s.hud);
 hud_ends=GameplayWorld::stage_ends=0;
 }puts("Actual stage cancellation/maps and composed Session close prefix: source quiescence and cancellation refusal precede flow/HUD teardown; idle scope closes");}
"""
  scratch_parent=ROOT/'work';scratch_parent.mkdir(parents=True,exist_ok=True)
  scratch=Path(tempfile.mkdtemp(prefix='stadium-pre-ready-',dir=scratch_parent));passed=False
  try:
   c=scratch/'stage.c';c.write_text(program);cxxfile=scratch/'session.cpp';cxxfile.write_text(cpp);obj=scratch/'stage.o';binary=scratch/'control'
   commands=[[cc,'-std=c11','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-c',str(c),'-o',str(obj)],
    [cxx,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(cxxfile),str(obj),'-o',str(binary)],[str(binary)]]
   import json
   (scratch/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
   print('Cancellation control commands: '+json.dumps(commands),flush=True)
   for i,command in enumerate(commands):
    r=subprocess.run(command,capture_output=True,text=True,timeout=60);(scratch/f'{i}.stdout').write_text(r.stdout);(scratch/f'{i}.stderr').write_text(r.stderr)
    self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   print(r.stdout,end='',flush=True);passed=True
  finally:
   if passed:shutil.rmtree(scratch)
   else:print(f'Retained cancellation control: {scratch}',flush=True)

 def test_actual_public_end_stage_checks_real_source_scope_before_mutation(self):
  """Actual end_stage and C quiescence code; stage mutation leaves are modeled."""
  cxx=shutil.which('clang++') or shutil.which('c++')
  if not cxx:self.skipTest('Native C++ compiler required')
  world=(SOURCE_ROOT/'src/gameplay_world.cpp').read_text()
  service=(SOURCE_ROOT/'src/gameplay_source_files.c').read_text()
  storage_end=whole_function(world,'    void end_stage(){')
  public_end=whole_function(world,'void GameplayWorld::end_stage(){')
  check_body=whole_function(world,'void check(')
  fail_body=whole_function(service,'static int fail(')
  quiescent_body=whole_function(service,'int melee_web_source_files_quiescent_for(')
  fixture=r"""
#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <stdexcept>
using DatError=std::runtime_error;
typedef struct MeleeWebSourceFileScope {
 size_t count;void* files;void* copies;void* regions;void* requests;
 void* request_tail;size_t request_count;int pumping;
} MeleeWebSourceFileScope;
static MeleeWebSourceFileScope* active_scope=nullptr;
"""+fail_body+r"""
"""+quiescent_body+r"""
static int mutation_order[3],mutation_count=0;
static int melee_web_stage_numeric_clear_quakes(void*,char*,size_t){mutation_order[mutation_count++]=1;return 1;}
static int melee_web_stage_last_end(void*,char*,size_t){mutation_order[mutation_count++]=2;return 1;}
static int melee_web_stage_visual_end(void*,char*,size_t){mutation_order[mutation_count++]=3;return 1;}
"""+check_body+r"""
struct Storage {
 MeleeWebSourceFileScope* source_files=nullptr;void* numeric=nullptr;
 void* stage_last=nullptr;void* stage_visual=nullptr;char error[128]{};
"""+storage_end+r"""
};
struct GameplayWorld {Storage* storage_=nullptr;void end_stage();};
"""+public_end+r"""
static void reset_counts(){mutation_count=0;std::memset(mutation_order,0,sizeof(mutation_order));}
static Storage make_storage(MeleeWebSourceFileScope* source){Storage s;s.source_files=source;s.numeric=(void*)1;s.stage_last=(void*)2;s.stage_visual=(void*)3;return s;}
int main(){
 MeleeWebSourceFileScope request_scope{};request_scope.requests=(void*)1;request_scope.request_count=1;active_scope=&request_scope;
 Storage pending=make_storage(&request_scope);GameplayWorld pending_world{&pending};bool refused=false;
 try{pending_world.end_stage();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&mutation_count==0&&pending.numeric==(void*)1&&pending.stage_last==(void*)2&&pending.stage_visual==(void*)3);

 reset_counts();MeleeWebSourceFileScope callback_scope{};callback_scope.pumping=1;active_scope=&callback_scope;
 Storage callback=make_storage(&callback_scope);GameplayWorld callback_world{&callback};refused=false;
 try{callback_world.end_stage();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&mutation_count==0&&callback.numeric==(void*)1&&callback.stage_last==(void*)2&&callback.stage_visual==(void*)3);

 reset_counts();MeleeWebSourceFileScope foreign_scope{},active{};active_scope=&active;
 Storage foreign=make_storage(&foreign_scope);GameplayWorld foreign_world{&foreign};refused=false;
 try{foreign_world.end_stage();}catch(const std::runtime_error&){refused=true;}
 assert(refused&&mutation_count==0&&foreign.numeric==(void*)1&&foreign.stage_last==(void*)2&&foreign.stage_visual==(void*)3);

 reset_counts();MeleeWebSourceFileScope idle_scope{};int registered_idle_region=1;idle_scope.regions=&registered_idle_region;active_scope=&idle_scope;
 Storage idle=make_storage(&idle_scope);GameplayWorld idle_world{&idle};idle_world.end_stage();
 assert(mutation_count==3&&mutation_order[0]==1&&mutation_order[1]==2&&mutation_order[2]==3);
 assert(idle.stage_last==nullptr&&idle.stage_visual==nullptr);

 reset_counts();Storage legacy=make_storage(nullptr);GameplayWorld legacy_world{&legacy};legacy_world.end_stage();
 assert(mutation_count==3&&mutation_order[0]==1&&mutation_order[1]==2&&mutation_order[2]==3);
 std::puts("actual public end_stage + actual C quiescence: queued/pumping/foreign refused before mutations; idle registered region and no-scope cleanup preserved");
}
"""
  scratch_parent=ROOT/'work';scratch_parent.mkdir(parents=True,exist_ok=True)
  scratch=Path(tempfile.mkdtemp(prefix='stadium-end-stage-',dir=scratch_parent));passed=False
  try:
   source=scratch/'end-stage.cpp';binary=scratch/'end-stage';source.write_text(fixture)
   command=[cxx,'-std=c++20','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(source),'-o',str(binary)]
   (scratch/'command.json').write_text(__import__('json').dumps(command,indent=2)+'\n')
   compiled=subprocess.run(command,capture_output=True,text=True,timeout=60)
   (scratch/'compile.stdout').write_text(compiled.stdout);(scratch/'compile.stderr').write_text(compiled.stderr)
   self.assertEqual(compiled.returncode,0,compiled.stdout+compiled.stderr)
   ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=60)
   (scratch/'run.stdout').write_text(ran.stdout);(scratch/'run.stderr').write_text(ran.stderr)
   self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
   print(ran.stdout,end='',flush=True);passed=True
  finally:
   if passed:shutil.rmtree(scratch)
   else:print(f'Retained end-stage control: {scratch}',flush=True)
