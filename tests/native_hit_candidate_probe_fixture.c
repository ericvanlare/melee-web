/* Actual selected helper + extracted original function bodies; services below
 * are explicit controlled synthetic ABI adapters, never production fallbacks. */
#include "gameplay_hit_transition_probe.h"
#include "candidate_source_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
StaticPlayer test_players[4];Fighter test_fighters[4];HSD_GObj test_entities[4];
uint32_t test_seed=0x12345678,*seed_ptr=&test_seed;
static int selected;
static HSD_JObj bone;
static int service_calls[8],service_order[1024],order_count;
static int overlap_result=1,eligibility_result;
static struct {float x128,x714,x7A8;} common={0.5f,2.0f,1.0f};
void* unused_common;
StaticPlayer* Player_GetPtrForSlot(unsigned slot){return slot<4?&test_players[slot]:NULL;}
int Player_GetPlayerSlotType(unsigned slot){return slot<4?Gm_PKind_Cpu:Gm_PKind_NA;}
int melee_web_hit_probe_test_selected(size_t cursor){return selected&&cursor>=5238&&cursor<=5240;}
void melee_web_hit_probe_test_emit(const char* text){puts(text);}
void test_tracker_init(void);
static void called(int id){assert(order_count<1024);service_calls[id]++;service_order[order_count++]=id;}
void lb_8000B1CC(HSD_JObj* joint,Vec3* input,Vec3* output)
{assert(joint==&bone);called(0);*output=*input;}
MtxPtr HSD_JObjGetMtxPtr(HSD_JObj* joint){assert(joint==&bone);called(1);return joint->matrix;}
void PSMTXConcat(Mtx a,Mtx b,Mtx result){called(2);for(int i=0;i<3;++i)for(int j=0;j<4;++j)result[i][j]=a[i][j]+b[i][j];}
int lbColl_80006E58(Vec3* a,Vec3* b,Vec3* c,Vec3* d,Vec3* e,Vec3* f,Mtx matrix,
 Vec3* position,float* distance,float radius,float hurt_radius,float factor)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;called(3);assert(matrix);position->x=radius;position->y=hurt_radius;position->z=factor;*distance=2.0f;return overlap_result;}
void ftColl_80076808(Fighter* a,HitCapsule* b,int c,void* d,int e)
{(void)a;(void)b;(void)c;(void)d;(void)e;called(4);}
int lbColl_80008688(HitCapsule* hit,int mode,void* target)
{(void)hit;(void)mode;(void)target;called(5);return 0;}
void lbColl_80008820(HitCapsule* hit,int mode,void* target)
{(void)hit;(void)mode;(void)target;called(5);}
void ftColl_80078488(Fighter* fp){(void)fp;called(6);}
void ftColl_8007891C(HSD_GObj* a,HSD_GObj* b,float damage){(void)a;(void)b;(void)damage;called(6);}
void efSync_Spawn(int id,void* owner,Vec3* pos){(void)id;(void)owner;(void)pos;called(7);}
int lbColl_8000ACFC(Fighter* fp,HitCapsule* hit){(void)fp;(void)hit;called(5);return eligibility_result;}
void synthetic_assert(void){abort();}
/* Include exact extracted bodies in this TU so their private log arrays,
 * inline routines and original float expressions remain accessible to controls. */
#define p_ftCommonData (&common)
#define PAD_STACK(n)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define GET_FIGHTER(g) ((Fighter*)(g)->user_data)
#define HSD_ASSERTREPORT(...) synthetic_assert()
#pragma GCC diagnostic push
/* Original routines mix signed log indices with authored unsigned bounds. */
#pragma GCC diagnostic ignored "-Wsign-compare"
#include "candidate_source_bodies.inc"
#pragma GCC diagnostic pop
static void reset(void)
{
 memset(test_fighters,0,sizeof(test_fighters));memset(test_players,0,sizeof(test_players));
 memset(test_entities,0,sizeof(test_entities));memset(service_calls,0,sizeof(service_calls));
 memset(service_order,0,sizeof(service_order));memset(dmg_log0,0,sizeof(dmg_log0));memset(dmg_log1,0,sizeof(dmg_log1));
 dmg_log0_idx=dmg_log1_idx=order_count=0;test_seed=0x12345678;
 for(unsigned i=0;i<4;++i){test_entities[i].user_data=&test_fighters[i];test_players[i].player_entity[0]=&test_entities[i];
  test_fighters[i].gobj=&test_entities[i];test_fighters[i].player_id=i;test_fighters[i].kind=i==1?FTKIND_KOOPA:0;test_fighters[i].dmg.x182c_behavior=1.0f;}
 for(unsigned i=0;i<4;++i){HitCapsule* h=&test_fighters[0].x914[i];h->state=HitCapsule_Enabled;h->x4=i;h->damage=11;h->scale=2;h->unk_count=8;h->x42_b5=h->x40_b3=1;}
 test_fighters[1].hurt_capsules_len=2;
 for(unsigned n=0;n<2;++n){HurtCapsule* h=&test_fighters[1].hurt_capsules[n].capsule;h->bone=&bone;h->scale=3;h->a_offset.x=1;h->b_offset.y=2;}
 test_tracker_init();
}
static int exercise(const char* mode)
{
 Fighter* attacker=&test_fighters[0];Fighter* receiver=&test_fighters[1];HitCapsule* hit=&attacker->x914[0];HurtCapsule* hurt=&receiver->hurt_capsules[0].capsule;
 if(!strncmp(mode,"phantom",7)){hit->coll_distance=0.0f;}
 if(!strcmp(mode,"hurt15"))receiver->hurt_capsules_len=15;
 if(!strcmp(mode,"hurt16"))receiver->hurt_capsules_len=16;
 if(!strcmp(mode,"intangible"))hurt->state=HurtCapsule_Intangible;
 if(!strcmp(mode,"cache"))hurt->skip_update_pos=1;
 if(!strcmp(mode,"zero_overlap"))overlap_result=0;
 if(!strcmp(mode,"hit_disabled"))hit->state=HitCapsule_Disabled;
 if(!strcmp(mode,"catch"))hit->element=HitElement_Catch;
 if(!strcmp(mode,"flag_zero"))hit->x42_b5=0;
 if(!strcmp(mode,"air_miss"))receiver->ground_or_air=GA_Air;
 if(!strcmp(mode,"grab_blocked")){hit->hit_grabbed_victim_only=1;attacker->victim_gobj=&test_entities[2];attacker->x221B_b5=1;}
 if(!strcmp(mode,"eligibility_blocked"))eligibility_result=1;
 if(!strcmp(mode,"phantom_existing"))hit->victims_2[0].victim=receiver;
 if(!strcmp(mode,"phantom_busy"))receiver->dmg.x189C_unk_num_frames=1;
 if(!strcmp(mode,"phantom_invulnerable")||!strcmp(mode,"normal_invulnerable"))receiver->x1988=1;
 if(!strcmp(mode,"normal_armored")){receiver->x221C_b4=1;receiver->dmg.x1834=50;}
 unsigned owner=melee_web_hit_probe_begin(&test_entities[1],0,0,0,0,0,0,0,0);
 unsigned pass=melee_web_hit_probe_pass_begin(&test_entities[1]);
 unsigned pair=melee_web_hit_probe_pair_begin(pass,&test_entities[!strcmp(mode,"wrong_pair")?2:0],3,1);
 if(!strcmp(mode,"duplicate_pair"))melee_web_hit_probe_pair_begin(pass,&test_entities[0],3,1);
 int result=0;
 if(strcmp(mode,"zero")&&strcmp(mode,"wrong_pair")){
  melee_web_hit_probe_candidate(pair,0,hit);
  if(!strcmp(mode,"gap"))melee_web_hit_probe_candidate(pair,2,&attacker->x914[2]);
  int count=!strcmp(mode,"two_geometry")?2:!strcmp(mode,"overflow")?64:1;
  int admitted=actual_admission(receiver,attacker,hit,&test_entities[1]);
  int pre_geometry_calls=service_calls[5];
  if(!strcmp(mode,"hit_disabled")||!strcmp(mode,"catch")||!strcmp(mode,"flag_zero")||!strcmp(mode,"air_miss")||!strcmp(mode,"grab_blocked"))assert(!admitted&&pre_geometry_calls==0);
  else assert(pre_geometry_calls==1);
  for(int n=0;admitted&&n<count;++n){
   HurtCapsule* selected_hurt=&receiver->hurt_capsules[n%receiver->hurt_capsules_len].capsule;
   if(!strcmp(mode,"missing_geometry")){melee_web_hit_probe_geometry_begin(hit,selected_hurt,NULL,0,1,1,0);break;}
   result=lbColl_8000805C(hit,selected_hurt,!strcmp(mode,"matrix")?bone.matrix:NULL,!strcmp(mode,"mode")?1:0,1.0f,1.0f,-0.0f);
   if(selected&&!strcmp(mode,"duplicate_geometry"))melee_web_hit_probe_geometry_end(1,result);
   if(result&&strcmp(mode,"two_geometry")&&strcmp(mode,"mode")&&strcmp(mode,"matrix")&&strcmp(mode,"cache")&&strcmp(mode,"overflow")){
    if(!strncmp(mode,"phantom",7))hit->coll_distance=0.0f;
    result=ftColl_80076ED8(attacker,hit,receiver,(HitCapsule*)&receiver->hurt_capsules[0]);
   }
  }
 }
 if(!strcmp(mode,"missing_pair")){melee_web_hit_probe_pass_end(pass);return result;}
 melee_web_hit_probe_pair_end(pair);
 if(!strcmp(mode,"duplicate_pair_return"))melee_web_hit_probe_pair_end(pair);
 if(!strcmp(mode,"missing_pass")){melee_web_hit_probe_scheduler_return();return result;}
 melee_web_hit_probe_pass_end(pass);
 for(unsigned kind=0;kind<2;++kind){
  size_t len=kind?dmg_log1_idx:dmg_log0_idx;
  unsigned consumer=melee_web_hit_probe_begin(&test_entities[1],1,0,0,0,0,0,len,!kind);
  for(size_t i=0;i<len;++i)melee_web_hit_probe_log(consumer,i,1,0,&test_entities[0],hit,hurt,1,2,3,11,8);
  melee_web_hit_probe_end(&test_entities[1],consumer);
 }
 melee_web_hit_probe_end(&test_entities[1],owner);
 melee_web_hit_probe_scheduler_return();return result;
}
int main(int argc,char** argv)
{
 if(argc!=2)return 2;
 reset();selected=0;melee_web_hit_probe_cursor(5239);int disabled=exercise(argv[1]);
 Fighter expected[4];memcpy(expected,test_fighters,sizeof(expected));int calls[8],order[1024];memcpy(calls,service_calls,sizeof(calls));memcpy(order,service_order,sizeof(order));int expected_count=order_count;
 DmgLogEntry logs0[20],logs1[20];memcpy(logs0,dmg_log0,sizeof(logs0));memcpy(logs1,dmg_log1,sizeof(logs1));int log0=dmg_log0_idx,log1=dmg_log1_idx;
 reset();selected=1;melee_web_hit_probe_cursor(5238);melee_web_hit_probe_scheduler_return();melee_web_hit_probe_cursor(5239);int enabled=exercise(argv[1]);
 assert(enabled==disabled&&memcmp(expected,test_fighters,sizeof(expected))==0);
 assert(memcmp(calls,service_calls,sizeof(calls))==0&&memcmp(order,service_order,sizeof(order))==0&&order_count==expected_count);
 assert(log0==dmg_log0_idx&&log1==dmg_log1_idx&&memcmp(logs0,dmg_log0,sizeof(logs0))==0&&memcmp(logs1,dmg_log1,sizeof(logs1))==0);
 assert(test_seed==0x12345678);
 melee_web_hit_probe_cursor(5240);melee_web_hit_probe_scheduler_return();
 fprintf(stderr,"SOURCE_OBSERVATION_CONTROL result=%d log0=%d log1=%d calls=%d ordered=%d exact_state_writes=1\n",enabled,log0,log1,order_count,expected_count);
 return 0;
}
