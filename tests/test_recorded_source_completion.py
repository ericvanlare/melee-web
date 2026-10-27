"""Actual C resident gate and raw-step function controls; modeled scheduler leaves.
No browser I/O, original timing distribution or full World is represented.
"""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from contextlib import contextmanager
import sys
ROOT=Path(os.environ.get('MELEE_RECORDED_COMPLETION_ROOT',Path(__file__).resolve().parents[1]))
def body(s,name):
 a=s.index(name);p=s.index('{',a);depth=1;b=p+1
 while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
@contextmanager
def owned_scratch(prefix):
 d=Path(tempfile.mkdtemp(prefix=prefix));failed=False
 try:
  yield d
 except BaseException:
  failed=True
  raise
 finally:
  # unittest.subTest records assertions without propagating them through this
  # context manager, so failed subprocesses also leave an explicit marker.
  if failed or (d/'FAILED').exists():
   print(f'Retained recorded completion fixture: {d}',file=sys.stderr)
  else:shutil.rmtree(d)
def logged_run(d,label,argv,timeout):
 try:
  r=subprocess.run(argv,capture_output=True,text=True,timeout=timeout)
 except BaseException as exc:
  for name in ('stdout','stderr'):
   output=getattr(exc,name,'') or ''
   if isinstance(output,bytes):output=output.decode(errors='replace')
   (d/f'{label}.{name}').write_text(output)
  (d/'FAILED').write_text(f'{label}: {type(exc).__name__}: {exc}\n')
  raise
 (d/f'{label}.stdout').write_text(r.stdout)
 (d/f'{label}.stderr').write_text(r.stderr)
 if r.returncode:(d/'FAILED').write_text(f'{label}: exit {r.returncode}\n')
 return r
class RecordedCompletionTests(unittest.TestCase):
 def compile(self,d,files,flags=()):
  compiler=shutil.which('clang') or shutil.which('cc')
  if not compiler:self.skipTest('Native C compiler required')
  binary=d/'control'
  r=logged_run(d,'compile',[compiler,'-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-I',str(ROOT/'src'),*flags,*map(str,files),'-o',str(binary)],timeout=30)
  self.assertEqual(r.returncode,0,r.stdout+r.stderr);return binary
 def service(self,modes):
  with owned_scratch('recorded-resident-') as d:
   binary=self.compile(d,[ROOT/'tests/gameplay_recorded_source_completion_trace.c',ROOT/'src/gameplay_source_files.c'],['-DMELEE_WEB_STADIUM_C1A_DIAGNOSTIC=1'])
   for mode in modes:
    with self.subTest(mode=mode):
     r=logged_run(d,'run-'+mode,[str(binary),mode],timeout=10)
     self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_default_and_eligible_copy_receipt_callback(self):self.service(['default','success'])
 def test_actual_wrong_owner_coordinates_request_and_pending_refusals(self):self.service(['wrongplan','wrongpath','wrongentry','wrongextent','wrongregion','late','skip','early','missing','wrongidentity','wrongcallback','cancel','supply','duplicate'])
 def test_external_unavailable_bytes_are_not_admitted(self):self.service(['external'])
 def test_ordinary_service_compiles_without_selector(self):
  with owned_scratch('recorded-default-') as d:
   p=d/'default.c';p.write_text('#include "gameplay_source_files.h"\nint main(void){return melee_web_source_files_active();}\n')
   self.compile(d,[p,ROOT/'src/gameplay_source_files.c'])
 def test_actual_raw_step_preconsume_order_and_failure_guard(self):
  s=(ROOT/'src/gameplay_match_context.c').read_text();h=(ROOT/'src/gameplay_match_context.h').read_text()
  hooktype=h[h.index('typedef int (*MeleeWebMatchRecordedPreconsume)'):h.index(';',h.index('typedef int (*MeleeWebMatchRecordedPreconsume)'))+1]
  observation=h[h.index('typedef struct MeleeWebStadiumPadQueueObservation'):h.index('} MeleeWebStadiumPadQueueObservation;')+len('} MeleeWebStadiumPadQueueObservation;')]
  alt=s[s.index('    if(!live(h,e,n))return 0;',s.index('int melee_web_match_step_inputs(')):s.index('    if(!samples)',s.index('int melee_web_match_step_inputs('))]
  code=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define MELEE_WEB_STADIUM_C1A_DIAGNOSTIC 1
#define MELEE_WEB_MATCH_MAX_PLAYERS 4u
typedef struct { int button; signed char err; } PADStatus;
typedef struct { PADStatus stat[4]; } Queue;
'''+hooktype+'\n'+observation+r'''
typedef struct MeleeWebMatchContext {
 uint64_t generation,ticks;uint32_t player_count,slots[4],controllers[4];Queue input_queue;
 MeleeWebMatchRecordedPreconsume recorded_preconsume;void* recorded_preconsume_context;
 uint64_t recorded_preconsume_generation;int recorded_preconsume_active,recorded_preconsume_failed;
 MeleeWebStadiumPadQueueObservation pad_queue_observation;
 int pad_queue_observation_candidate,pad_queue_observation_complete;
} MeleeWebMatchContext;
typedef int (*MeleeWebMatchTickPhase)(void*,char*,size_t);
static MeleeWebMatchContext* owner;
static struct {Queue* queue;unsigned qnum,qcount,qread,qwrite;} HSD_PadLibData;
static void* HSD_GObj_804D781C;static void* HSD_GObj_804D7814;
static int events[20],count,mode;static uint64_t generation=7;
static uint64_t melee_web_gameplay_generation(void){return generation;}
static int fail(char* e,size_t n,const char* s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int ok(char*e,size_t n){if(e&&n)*e=0;return 1;}
static int owned(MeleeWebMatchContext*h,char*e,size_t n){return h==owner&&h->generation==generation?1:fail(e,n,"modeled owner");}
static int live(MeleeWebMatchContext*h,char*e,size_t n){return owned(h,e,n);}
static struct {void* player_entity[1];} player={{(void*)(uintptr_t)1}};
static __typeof__(player)* Player_GetPtrForSlot(unsigned slot){(void)slot;return &player;}
static void HSD_PadRumbleInterpret(void){events[count++]=1;assert(HSD_PadLibData.qcount==1);}
static void HSD_PadRenewMasterStatus(void){events[count++]=3;assert(HSD_PadLibData.qcount==1);HSD_PadLibData.qcount=0;}
static void HSD_PadRenewCopyStatus(void){}static void HSD_PadRenewGameStatus(void){}
static int melee_web_gameplay_step(char*e,size_t n){(void)e;(void)n;events[count++]=4;return 1;}
'''+body(s,'int melee_web_match_recorded_preconsume_arm(')+'\n'+body(s,'int melee_web_match_step_raw_phased(')+'\nstatic int alternative_admission(MeleeWebMatchContext* h,char* e,size_t n){\n'+alt+'\nreturn 1; }\n'+r'''
static int hook(void* context,uint64_t cursor,char*e,size_t n){
 MeleeWebMatchContext* h=context;assert(h==owner&&cursor==h->ticks&&HSD_PadLibData.qcount==1);events[count++]=2;
 if(mode==1)HSD_PadLibData.qcount=0;
 if(mode==2){PADStatus raw[4]={{0}};assert(!melee_web_match_step_raw_phased(h,raw,0,0,0,0,e,n));}
 return 1;
}
int main(void){char e[160];PADStatus raw[4]={{0}};raw[0].button=42;
 for(mode=0;mode<3;mode++){
  MeleeWebMatchContext h={0};owner=&h;h.generation=7;h.player_count=1;HSD_PadLibData.queue=&h.input_queue;HSD_PadLibData.qnum=1;HSD_PadLibData.qcount=0;count=0;
  assert(alternative_admission(&h,e,sizeof(e)));
  assert(melee_web_match_recorded_preconsume_arm(&h,hook,&h,e,sizeof(e)));
  assert(!alternative_admission(&h,e,sizeof(e)));
  assert(!melee_web_match_recorded_preconsume_arm(&h,hook,&h,e,sizeof(e)));
  int result=melee_web_match_step_raw_phased(&h,raw,0,0,0,0,e,sizeof(e));
  if(!mode){assert(result&&h.ticks==1&&count==4&&events[0]==1&&events[1]==2&&events[2]==3&&events[3]==4);assert(h.pad_queue_observation_complete);}
  else{assert(!result&&!h.ticks&&h.recorded_preconsume_failed&&count==2);}
 }
 return 0;
}
'''
  with owned_scratch('recorded-preconsume-') as d:
   p=d/'actual-step.c';p.write_text(code);binary=self.compile(d,[p]);r=logged_run(d,'run',[str(binary)],timeout=10);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
