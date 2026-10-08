/* Actual selected helper + extracted original function bodies; services below
 * are explicit controlled synthetic ABI adapters, never production fallbacks. */
#include "gameplay_hit_transition_probe.h"
#include "candidate_source_fixture.h"
#include <assert.h>
#include <limits.h>
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
int melee_web_retail_primary_identity(unsigned,HSD_GObj*,uint32_t*,uint32_t*);
StaticPlayer* Player_GetPtrForSlot(unsigned slot){return slot<4?&test_players[slot]:NULL;}
int Player_GetPlayerSlotType(unsigned slot){return slot<4?Gm_PKind_Cpu:Gm_PKind_NA;}
int melee_web_hit_probe_test_selected(size_t cursor){return selected&&cursor>=5238&&cursor<=5240;}
void melee_web_hit_probe_test_emit(const char* text)
{puts(text);if(strstr(text,"\"overflowed\":true"))fprintf(stderr,"ACTUAL_HELPER_CONTROL attempted_geometry_calls=%d retained_records=64\n",service_calls[3]);}
void test_tracker_init(void);
static void called(int id){assert(order_count<1024);service_calls[id]++;service_order[order_count++]=id;}

#ifdef MELEE_WEB_HIT_PROBE_COUNTING_ADAPTER
/* Fixture-only dynamic recorder; it does not model production storage. */
typedef struct ProbeCountEvent {
 const char* phase;unsigned kind,ordinal;int hit,hurt,result,branch,log_index;
} ProbeCountEvent;
static ProbeCountEvent* count_events;
static size_t count_event_len,count_event_cap;
static int count_active,count_geometry_result,count_geometry_consumed,count_geometry_open;
static HSD_GObj* count_victim;
static Fighter* count_attacker;
static const HitCapsule* count_candidate;
static unsigned count_invocations,count_pass,count_pair,count_geometries,count_producers;
static unsigned count_invocation_kind[64];
static unsigned count_pass_candidates;
static int count_hit_index,count_hurt_index,count_producer_phase;
static void record_event(const char* phase,unsigned kind,unsigned ordinal,
 int hit,int hurt,int result,int branch,int log_index)
{
 if(!count_active)return;
 if(count_event_len==count_event_cap){
  size_t next=count_event_cap?count_event_cap*2:128;
  ProbeCountEvent* grown=realloc(count_events,next*sizeof(*grown));
  if(!grown)abort();count_events=grown;count_event_cap=next;
 }
 count_events[count_event_len++]=(ProbeCountEvent){phase,kind,ordinal,hit,hurt,result,branch,log_index};
}
static void reset_counter(void)
{
 count_event_len=0;count_active=0;count_victim=NULL;count_attacker=NULL;count_candidate=NULL;
 count_invocations=count_pass=count_pair=count_geometries=count_producers=0;
 memset(count_invocation_kind,0,sizeof(count_invocation_kind));
 count_pass_candidates=0;count_hit_index=count_hurt_index=-1;
 count_geometry_result=count_geometry_consumed=count_geometry_open=0;count_producer_phase=-1;
}
void melee_web_hit_probe_cursor(size_t cursor)
{
 reset_counter();count_active=selected&&cursor==5239;
 if(count_active){count_victim=&test_entities[1];record_event("scheduler_start",3,0,-1,-1,-1,-1,-1);}
}
void melee_web_hit_probe_scheduler_return(void)
{
 if(!count_active)return;
 if(count_pass||count_pair||count_candidate)abort();
 record_event("scheduler_return",3,0,-1,-1,-1,-1,-1);count_active=0;
}
unsigned melee_web_hit_probe_begin(HSD_GObj* gobj,unsigned kind,int requested,unsigned flags,
 float frame,float speed,float blend,size_t log_count,int log_kind)
{
 (void)requested;(void)flags;(void)frame;(void)speed;(void)blend;(void)log_count;(void)log_kind;
 if(!count_active||gobj!=count_victim)return 0;
 if(count_invocations>=sizeof(count_invocation_kind)/sizeof(count_invocation_kind[0]))abort();
 unsigned id=++count_invocations;count_invocation_kind[id-1]=kind;
 record_event("entry",kind,id,-1,-1,-1,-1,-1);return id;
}
void melee_web_hit_probe_log(unsigned id,size_t index,int entity_kind,int fighter_kind,
 HSD_GObj* source,const void* hit,const void* hurt,float x,float y,float z,float damage,size_t hit_bytes)
{
 (void)entity_kind;(void)fighter_kind;(void)source;(void)hit;(void)hurt;(void)x;(void)y;(void)z;(void)damage;(void)hit_bytes;
 if(id)record_event("log",1,id,-1,-1,-1,-1,(int)index);
}
void melee_web_hit_probe_end(HSD_GObj* gobj,unsigned id)
{if(id){if(!count_active||gobj!=count_victim||id>count_invocations)abort();
 record_event("return",count_invocation_kind[id-1],id,-1,-1,-1,-1,-1);}}
unsigned melee_web_hit_probe_pass_begin(HSD_GObj* receiver)
{
 if(!count_active)return 0;
 if(receiver!=count_victim||count_pass||count_pair)abort();
 count_pass=1;record_event("pass_entry",4,count_pass,-1,-1,-1,-1,-1);return count_pass;
}
void melee_web_hit_probe_pass_end(unsigned pass)
{if(pass){if(pass!=count_pass||count_pair)abort();record_event("pass_return",4,pass,-1,-1,-1,-1,-1);count_pass=0;}}
unsigned melee_web_hit_probe_pair_begin(unsigned pass,HSD_GObj* attacker,unsigned encounter,int self_seen)
{
 (void)encounter;(void)self_seen;
 if(!pass)return 0;
 uint32_t attacker_match,attacker_generation;
 if(pass!=count_pass||count_pair||attacker!=&test_entities[0]||
    !melee_web_retail_primary_identity(0,attacker,&attacker_match,&attacker_generation)||
    attacker_match!=0||attacker_generation!=0)abort();
 count_attacker=attacker->user_data;count_pair=1;count_pass_candidates=0;
 count_candidate=NULL;count_hit_index=count_hurt_index=-1;
 record_event("pair_entry",4,count_pair,-1,-1,-1,-1,-1);return count_pair;
}
void melee_web_hit_probe_pair_end(unsigned pair)
{if(pair){if(pair!=count_pair||count_geometry_open)abort();record_event("pair_return",4,pair,-1,-1,-1,-1,-1);count_pair=0;count_attacker=NULL;count_candidate=NULL;}}
void melee_web_hit_probe_candidate(unsigned pair,unsigned index,const HitCapsule* hit)
{
 if(!pair)return;
 if(pair!=count_pair||index!=count_pass_candidates||index>=4||hit!=&count_attacker->x914[index])abort();
 count_candidate=hit;count_hit_index=(int)index;count_hurt_index=-1;count_geometry_result=count_geometry_consumed=0;
 ++count_pass_candidates;record_event("candidate",4,pair,count_hit_index,-1,-1,-1,-1);
}
unsigned melee_web_hit_probe_geometry_begin(const HitCapsule* hit,const HurtCapsule* hurt,
 const void* matrix,int mode,float attacker_scale,float receiver_scale,float z)
{
 (void)matrix;(void)mode;(void)attacker_scale;(void)receiver_scale;(void)z;
 if(!count_pair||hit!=count_candidate)return 0;
 if(count_geometries==UINT_MAX||count_geometry_open)abort();
 Fighter* receiver=count_victim->user_data;count_hurt_index=-1;
 for(unsigned i=0;i<receiver->hurt_capsules_len;i++)if(hurt==&receiver->hurt_capsules[i].capsule)count_hurt_index=(int)i;
 if(count_hurt_index<0)abort();count_geometry_open=1;return ++count_geometries;
}
void melee_web_hit_probe_geometry_end(unsigned geometry,int result)
{
 if(!geometry)return;
 if(geometry!=count_geometries||!count_pair||!count_geometry_open||count_hurt_index<0)abort();
 record_event("geometry",4,geometry,count_hit_index,count_hurt_index,result,-1,-1);
 count_geometry_open=0;count_geometry_result=result;count_geometry_consumed=0;
}
unsigned melee_web_hit_probe_producer_begin(Fighter* attacker,const HitCapsule* hit,
 Fighter* receiver,const void* hurt,size_t log0,size_t log1)
{
 if(!count_pair||attacker!=count_attacker||receiver!=count_victim->user_data||hit!=count_candidate)return 0;
 if(
    !count_geometry_result||count_geometry_consumed||count_geometry_open||count_hurt_index<0||
    hurt!=&receiver->hurt_capsules[count_hurt_index]||log0>20||log1>20)abort();
 count_geometry_consumed=1;count_producer_phase=-1;
 unsigned id=++count_producers;record_event("producer_entry",4,id,count_hit_index,count_hurt_index,-1,-1,-1);return id;
}
void melee_web_hit_probe_producer_branch(unsigned producer,unsigned branch,size_t log0,size_t log1)
{
 (void)log0;(void)log1;if(!producer)return;
 int expected=branch==0||branch==3?-1:branch==1?0:branch==2?1:branch==4?3:4;
 if(producer!=count_producers||branch>5||count_producer_phase!=expected)abort();
 count_producer_phase=(int)branch;
 record_event("producer_branch",4,producer,count_hit_index,count_hurt_index,-1,(int)branch,-1);
}
void melee_web_hit_probe_producer_end(unsigned producer,int result,size_t log0,size_t log1)
{
 (void)log0;(void)log1;if(!producer)return;
 if(producer!=count_producers||count_producer_phase<0||
    (count_producer_phase==0?result!=0:result!=1))abort();
 record_event("producer_return",4,producer,count_hit_index,count_hurt_index,result,-1,-1);
 count_hurt_index=-1;
}
static void write_counting_adapter(const char* mode)
{
 printf("{\"adapter_scope\":\"fixture-only counting adapter; dynamic recorder; not runtime storage evidence\",\"mode\":\"%s\",\"event_count\":%zu,\"events\":[",mode,count_event_len);
 for(size_t i=0;i<count_event_len;i++){
  ProbeCountEvent* e=&count_events[i];
  printf("%s{\"sequence\":%zu,\"phase\":\"%s\",\"kind\":%u,\"ordinal\":%u,\"hit_index\":%d,\"hurt_index\":%d,\"result\":%d,\"branch\":%d,\"log_index\":%d}",
   i?",":"",i,e->phase,e->kind,e->ordinal,e->hit,e->hurt,e->result,e->branch,e->log_index);
 }
 puts("]}");
}
#endif

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
 for(unsigned n=0;n<15;++n){HurtCapsule* h=&test_fighters[1].hurt_capsules[n].capsule;h->bone=&bone;h->scale=3;h->a_offset.x=1;h->b_offset.y=2;}
 test_tracker_init();
}

#ifdef MELEE_WEB_HIT_PROBE_COUNTING_ADAPTER
typedef struct FixtureSnapshot {
 Fighter fighters[4];StaticPlayer players[4];HSD_GObj entities[4];
 DmgLogEntry logs0[20],logs1[20];int log0,log1,calls[8],order[1024],order_count;uint32_t seed;
} FixtureSnapshot;
static void take_snapshot(FixtureSnapshot* out)
{
 memset(out,0,sizeof(*out));
 memcpy(out->fighters,test_fighters,sizeof(test_fighters));memcpy(out->players,test_players,sizeof(test_players));
 memcpy(out->entities,test_entities,sizeof(test_entities));memcpy(out->logs0,dmg_log0,sizeof(dmg_log0));
 memcpy(out->logs1,dmg_log1,sizeof(dmg_log1));out->log0=dmg_log0_idx;out->log1=dmg_log1_idx;
 memcpy(out->calls,service_calls,sizeof(service_calls));memcpy(out->order,service_order,sizeof(service_order));
 out->order_count=order_count;out->seed=test_seed;
}
#endif
static uint64_t pointer_token(const void* pointer)
{
 if(!pointer)return 0;
 uintptr_t value=(uintptr_t)pointer;
 for(unsigned i=0;i<4;i++){
  uintptr_t base=(uintptr_t)&test_fighters[i];if(value>=base&&value<base+sizeof(Fighter))return 0x100000u+i*0x10000u+(value-base);
  base=(uintptr_t)&test_entities[i];if(value>=base&&value<base+sizeof(HSD_GObj))return 0x200000u+i*0x10000u+(value-base);
  base=(uintptr_t)&test_players[i];if(value>=base&&value<base+sizeof(StaticPlayer))return 0x300000u+i*0x10000u+(value-base);
 }
 uintptr_t base=(uintptr_t)&bone;if(value>=base&&value<base+sizeof(bone))return 0x400000u+(value-base);
 abort();
}
static void hash_bytes(uint64_t* hash,const void* bytes,size_t length)
{const unsigned char* p=bytes;for(size_t i=0;i<length;i++){*hash^=p[i];*hash*=1099511628211ull;}}
static void hash_pointer(uint64_t* hash,const void* pointer)
{uint64_t token=pointer_token(pointer);hash_bytes(hash,&token,sizeof(token));}
static void canonicalize_capsule(HitCapsule* copy,const HitCapsule* original,uint64_t* hash)
{
 for(unsigned i=0;i<12;i++){hash_pointer(hash,original->victims_2[i].victim);copy->victims_2[i].victim=NULL;}
 hash_pointer(hash,original->owner);copy->owner=NULL;
}
static uint64_t fixture_state_hash(void)
{
 uint64_t hash=1469598103934665603ull;Fighter fighters[4];StaticPlayer players[4];HSD_GObj entities[4];
 DmgLogEntry logs0[20],logs1[20];memcpy(fighters,test_fighters,sizeof(fighters));
 memcpy(players,test_players,sizeof(players));memcpy(entities,test_entities,sizeof(entities));
 memcpy(logs0,dmg_log0,sizeof(logs0));memcpy(logs1,dmg_log1,sizeof(logs1));
 for(unsigned i=0;i<4;i++){
  hash_pointer(&hash,test_fighters[i].gobj);fighters[i].gobj=NULL;
  hash_pointer(&hash,test_fighters[i].victim_gobj);fighters[i].victim_gobj=NULL;
  hash_pointer(&hash,test_fighters[i].x1064_thrownHitbox.owner);fighters[i].x1064_thrownHitbox.owner=NULL;
  hash_pointer(&hash,test_fighters[i].dmg.x1868_source);fighters[i].dmg.x1868_source=NULL;
  for(unsigned j=0;j<4;j++)canonicalize_capsule(&fighters[i].x914[j],&test_fighters[i].x914[j],&hash);
  canonicalize_capsule(&fighters[i].x1064_thrownHitbox,&test_fighters[i].x1064_thrownHitbox,&hash);
  for(unsigned j=0;j<15;j++){
   hash_pointer(&hash,test_fighters[i].hurt_capsules[j].capsule.bone);
   fighters[i].hurt_capsules[j].capsule.bone=NULL;
  }
  for(unsigned j=0;j<2;j++){hash_pointer(&hash,test_players[i].player_entity[j]);players[i].player_entity[j]=NULL;}
  hash_pointer(&hash,test_entities[i].user_data);entities[i].user_data=NULL;
 }
 for(unsigned i=0;i<20;i++){
  hash_pointer(&hash,dmg_log0[i].gobj);logs0[i].gobj=NULL;hash_pointer(&hash,dmg_log0[i].hit0);logs0[i].hit0=NULL;
  hash_pointer(&hash,dmg_log0[i].hit1);logs0[i].hit1=NULL;hash_pointer(&hash,dmg_log0[i].unk_anim0);logs0[i].unk_anim0=NULL;
  hash_pointer(&hash,dmg_log0[i].hurt1);logs0[i].hurt1=NULL;
  hash_pointer(&hash,dmg_log1[i].gobj);logs1[i].gobj=NULL;hash_pointer(&hash,dmg_log1[i].hit0);logs1[i].hit0=NULL;
  hash_pointer(&hash,dmg_log1[i].hit1);logs1[i].hit1=NULL;hash_pointer(&hash,dmg_log1[i].unk_anim0);logs1[i].unk_anim0=NULL;
  hash_pointer(&hash,dmg_log1[i].hurt1);logs1[i].hurt1=NULL;
 }
 hash_bytes(&hash,fighters,sizeof(fighters));hash_bytes(&hash,players,sizeof(players));
 hash_bytes(&hash,entities,sizeof(entities));hash_bytes(&hash,logs0,sizeof(logs0));hash_bytes(&hash,logs1,sizeof(logs1));
 hash_bytes(&hash,&dmg_log0_idx,sizeof(dmg_log0_idx));hash_bytes(&hash,&dmg_log1_idx,sizeof(dmg_log1_idx));
 hash_bytes(&hash,service_calls,sizeof(service_calls));hash_bytes(&hash,service_order,sizeof(service_order));
 hash_bytes(&hash,&order_count,sizeof(order_count));hash_bytes(&hash,&test_seed,sizeof(test_seed));return hash;
}
static int exercise_authored(const char* mode)
{
 Fighter* attacker=&test_fighters[0];Fighter* receiver=&test_fighters[1];
 receiver->hurt_capsules_len=15;overlap_result=!strcmp(mode,"authored_off")||!strcmp(mode,"authored_overflow")||!strcmp(mode,"count_all_false")?0:1;
 for(unsigned i=0;i<4;i++){
  HitCapsule* hit=&attacker->x914[i];
  int phantom=!strcmp(mode,"count_phantom")||(!strcmp(mode,"count_mixed")&&i<3);
  if(phantom)hit->victims_2[0].victim=receiver;
 }
 unsigned owner=melee_web_hit_probe_begin(&test_entities[1],0,0,0,0,0,0,0,0);
 unsigned pass=melee_web_hit_probe_pass_begin(&test_entities[1]);
 unsigned pair=melee_web_hit_probe_pair_begin(pass,&test_entities[0],0,0);
 for(unsigned i=0;i<4;i++){
  HitCapsule* hit=&attacker->x914[i];melee_web_hit_probe_candidate(pair,i,hit);
  int admitted=actual_admission(receiver,attacker,hit,&test_entities[1]);assert(admitted);
  for(unsigned n=0;n<receiver->hurt_capsules_len;n++){
   HurtCapsule* hurt=&receiver->hurt_capsules[n].capsule;
   int collided=lbColl_8000805C(hit,hurt,NULL,0,1.0f,1.0f,-0.0f);
   if(collided){
    int phantom=!strcmp(mode,"count_phantom")||(!strcmp(mode,"count_mixed")&&i<3);
    if(phantom)hit->coll_distance=0.0f;
    (void)ftColl_80076ED8(attacker,hit,receiver,(HitCapsule*)hurt);
    break; /* The source exits this hurt scan after the first true geometry result. */
   }
  }
 }
 melee_web_hit_probe_pair_end(pair);melee_web_hit_probe_pass_end(pass);
 for(unsigned kind=0;kind<2;kind++){
  size_t len=kind?(size_t)dmg_log1_idx:(size_t)dmg_log0_idx;
  unsigned consumer=melee_web_hit_probe_begin(&test_entities[1],1,0,0,0,0,0,len,!kind);
  for(size_t i=0;i<len;i++){
   DmgLogEntry* entry=kind?&dmg_log1[i]:&dmg_log0[i];
   melee_web_hit_probe_log(consumer,i,1,0,entry->gobj,entry->hit0,entry->hit1,
    entry->pos.x,entry->pos.y,entry->pos.z,entry->x20,(size_t)entry->size_of_xC);
  }
  melee_web_hit_probe_end(&test_entities[1],consumer);
 }
 melee_web_hit_probe_end(&test_entities[1],owner);melee_web_hit_probe_scheduler_return();return 0;
}
static int run_authored_off(void)
{
 reset();selected=0;melee_web_hit_probe_cursor(5239);exercise_authored("authored_off");
 if(service_calls[3]!=60||order_count<60)abort();
 fprintf(stderr,"AUTHORED_SELECTOR_OFF geometry_calls=%d emitted_rows=0 state_hash=%016llx\n",
  service_calls[3],(unsigned long long)fixture_state_hash());return 0;
}
static int run_authored_overflow(void)
{
 reset();selected=1;melee_web_hit_probe_cursor(5239);exercise_authored("authored_overflow");
 return 0; /* Reaching here means the actual 64-row control failed to overflow. */
}
#ifdef MELEE_WEB_HIT_PROBE_COUNTING_ADAPTER
static int run_counting_adapter(const char* mode)
{
 FixtureSnapshot disabled,enabled;
 reset();selected=0;melee_web_hit_probe_cursor(5239);exercise_authored(mode);take_snapshot(&disabled);
 reset();selected=1;melee_web_hit_probe_cursor(5239);exercise_authored(mode);take_snapshot(&enabled);
 if(memcmp(&disabled,&enabled,sizeof(disabled))!=0)abort();
 if(service_calls[3]==0||order_count==0)abort();
 fprintf(stderr,"COUNTING_ADAPTER source_state_writes_equal=1 service_counts_and_order_equal=1 geometry_calls=%d ordered_calls=%d\n",
  service_calls[3],order_count);
 write_counting_adapter(mode);free(count_events);count_events=NULL;count_event_cap=count_event_len=0;return 0;
}
#endif
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
 if(!strcmp(argv[1],"authored_off"))return run_authored_off();
 if(!strcmp(argv[1],"authored_overflow"))return run_authored_overflow();
#ifdef MELEE_WEB_HIT_PROBE_COUNTING_ADAPTER
 if(!strcmp(argv[1],"count_all_false")||!strcmp(argv[1],"count_phantom")||!strcmp(argv[1],"count_mixed"))
  return run_counting_adapter(argv[1]);
#endif
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
