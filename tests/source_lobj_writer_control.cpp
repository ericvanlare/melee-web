/* Actual runtime/class allocator + actual producer. Shared OS backing,
 * plain-position/type getters below are explicit model services. */
#define main original_class_piece_control_main
#include "source_class_piece_control.cpp"
#undef main
extern "C" {
#include <sysdolphin/baselib/lobj.h>
#include <sysdolphin/baselib/wobj.h>
void writer_actual_producer(HSD_LObj*,Mtx);
}
static unsigned type_calls=0,position_calls=0,vector_calls=0;
extern "C" u32 HSD_LObjGetType(HSD_LObj* p){++type_calls;return p->flags&LOBJ_TYPE_MASK;}
extern "C" melee_source_bool HSD_LObjGetPosition(HSD_LObj* p,Vec3* out){++position_calls;*out=p->position->pos;return true;}
extern "C" void HSD_LObjGetLightVector(HSD_LObj* p,Vec3* out){++vector_calls;*out=p->position->pos;}
int main(int argc,char** argv){
 const char* mode=argc>1?argv[1]:"phase";
#if !defined(MELEE_WEB_CPU_INTERVAL_ENABLED) || !MELEE_WEB_CPU_INTERVAL_ENABLED
 auto* l=static_cast<HSD_LObj*>(hsdAllocMemPiece(212));check(l,"ordinary class backing");
 std::memset(l,0,sizeof(*l));HSD_WObj position{};position.pos={1,2,3};l->position=&position;l->flags=LOBJ_POINT|LOBJ_SPECULAR;
 Mtx m={{1,0,0,0},{0,1,0,0},{0,0,1,0}};writer_actual_producer(l,m);
 check(type_calls==1&&position_calls==1&&!vector_calls&&l->lvec.x==1,"ordinary source calls/function pointers unchanged");
 std::puts("INACTIVE original producer preserves calls and links without runtime observer");return 0;
#else
 check(melee_web_source_memory_begin(77,3),"source owner begin");check(hsdAllocMemPiece(32),"actual cold handshake");
 ClassInfoInit(&hsdClass);HSD_ClassInfo parent{},info{};
 hsdInitClassInfo(&parent,&hsdClass,(char*)"fixture",(char*)"parent",sizeof(parent),212);
 hsdInitClassInfo(&info,&parent,(char*)"fixture",(char*)"writer_light_shape",sizeof(info),212);
 auto* light=static_cast<HSD_LObj*>(hsdNew(&info));check(light,"actual class allocation");
 HSD_WObj position{};position.pos={1,2,3};light->position=&position;light->flags=LOBJ_POINT|LOBJ_SPECULAR;
 light->color={120,130,140,150};Mtx matrix={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
 writer_actual_producer(light,matrix); // Warm all other producer cache fields.
 auto* pre=snapshot();auto anchor=melee_web_class_piece_arm();check(anchor.ready,"arm PRE piece table");
 uint64_t session=melee_web_lobj_writer_arm(pre,anchor,8);check(session,"writer arm");
 type_calls=position_calls=vector_calls=0;deny_allocation=true;
 if(!std::strcmp(mode,"bounds")){
  const auto ticket=melee_web_lobj_writer_before(light,reinterpret_cast<unsigned char*>(light)+61,212,"invalid",1);
  check(!ticket,"bad offset refused before read");check(melee_web_lobj_writer_evidence(session)->reason==5,"bad span reason");return 0;
 }
 if(!std::strcmp(mode,"retired")){
  deny_allocation=false;check(melee_web_source_memory_end(77),"source owner retired");deny_allocation=true;
  writer_actual_producer(light,matrix);check(melee_web_lobj_writer_evidence(session)->reason==4,"retired owner refused");return 0;
 }
 if(!std::strcmp(mode,"stale-session")){
  melee_web_lobj_writer_disarm(session);const auto newer=melee_web_lobj_writer_arm(pre,anchor,8);check(newer>session,"session never rewinds");
  melee_web_lobj_writer_disarm(session);check(melee_web_lobj_writer_evidence(newer)->session==newer,"old disarm cannot clear new owner");return 0;
 }
 if(!std::strcmp(mode,"recreated")){
  deny_allocation=false;check(melee_web_source_memory_end(77),"retire old epoch");check(melee_web_source_memory_begin(77,3),"recreate same world with new epoch");deny_allocation=true;
  const auto saved=light->lvec;
  check(!melee_web_lobj_writer_before(light,&light->lvec,sizeof(*light),"recreated",1),"recreated owner refuses borrowed PRE before reading");
  check(melee_web_lobj_writer_evidence(session)->reason==4&&!std::memcmp(&saved,&light->lvec,12),"recreated diagnostic does not write source");return 0;
 }
 if(!std::strcmp(mode,"mirror")){
  deny_allocation=false;MeleeWebSourceMemorySnapshot* mirror=nullptr;check(!melee_web_source_memory_snapshot_capture(&mirror),"actual mirror capture");
  check(!melee_web_source_memory_snapshot_restore(mirror),"actual mirror restore");deny_allocation=true;
  check(!melee_web_lobj_writer_before(light,&light->lvec,sizeof(*light),"mirror",1),"mirror topology invalidates borrowed PRE");
  check(melee_web_lobj_writer_evidence(session)->reason==4,"mirror is not a new observed allocation event");return 0;
 }
 if(!std::strcmp(mode,"unknown")){
  // Explicit malformed/unknown frozen metadata injection, not a live class claim.
  size_t row_index;const HostAllocation* owner=nullptr;check(writer_row(reinterpret_cast<uintptr_t>(&light->lvec),row_index,owner),"actual containing ledger row");
  const auto src=owner->source.value()+reinterpret_cast<uintptr_t>(light)-owner->host;
  const auto index=(src-piece_owner.begin)/32;check(index<piece_owner.count,"fixture target cell");piece_owner.cells[index].tag=0;
  writer_actual_producer(light,matrix);const auto* e=melee_web_lobj_writer_evidence(session);
  check(e->count==1&&e->events[0].status==2&&e->events[0].reason==6&&e->events[0].flags==2&&!e->events[0].class_info,"unknown PRE class metadata remains UNKNOWN");return 0;
 }
 if(!std::strcmp(mode,"wrong-ticket")){
  const auto saved=light->lvec;const auto t=melee_web_lobj_writer_before(light,&light->lvec,sizeof(*light),"ticket",1);check(t,"actual before ticket");
  melee_web_lobj_writer_after(t+256);const auto* e=melee_web_lobj_writer_evidence(session);
  check(e->status==2&&e->reason==2&&!e->events[0].status&&!std::memcmp(&saved,&light->lvec,12),"wrong session ticket neither completes event nor writes source");return 0;
 }
 if(!std::strcmp(mode,"extent")){
  size_t row_index;const HostAllocation* row=nullptr;check(writer_row(reinterpret_cast<uintptr_t>(&light->lvec),row_index,row),"original containing outer row");const auto host=row->host;const auto bytes=row->requested;
  deny_allocation=false;check(melee_web_source_memory_free(3,reinterpret_cast<void*>(host)),"actual outer free");check(melee_web_source_memory_alloc(3,reinterpret_cast<void*>(host),bytes-32),"actual changed-extent new generation");deny_allocation=true;
  writer_actual_producer(light,matrix);check(melee_web_lobj_writer_evidence(session)->reason==4,"changed topology refused before observation reads");return 0;
 }
 if(!std::strcmp(mode,"pending")){
  const auto t=melee_web_lobj_writer_before(light,&light->lvec,sizeof(*light),"pending",1);check(t,"pending ticket");
  melee_web_lobj_writer_mark(session,1);check(melee_web_lobj_writer_evidence(session)->status==2,"pending pair cannot seal complete endpoint");return 0;
 }
 if(!std::strcmp(mode,"overflow")){
  for(unsigned i=0;i<129;++i)writer_actual_producer(light,matrix);
  const auto* e=melee_web_lobj_writer_evidence(session);check(e->count==128&&e->seen==129&&e->overflow&&e->status==2,"overflow not truncated proof");return 0;
 }
 if(!std::strcmp(mode,"reuse")){
  // Actual free/reuse leaves PRE table historical; never claim same live piece.
  info.destroy(reinterpret_cast<HSD_Class*>(light));auto* replacement=static_cast<HSD_LObj*>(hsdNew(&info));check(replacement==light,"actual same-cell reuse");
  replacement->position=&position;replacement->flags=LOBJ_POINT|LOBJ_SPECULAR;replacement->color={120,130,140,150};
  writer_actual_producer(replacement,matrix);const auto* e=melee_web_lobj_writer_evidence(session);
  check(e->count==1&&(e->events[0].flags&2),"reuse preserves UNKNOWN live piece lifetime, never LIVE claim");return 0;
 }
 if(!std::strcmp(mode,"missing")){
  light->lvec.x=4; // Explicit fixture-only unobserved writer negative.
  auto* post=snapshot();melee_web_lobj_writer_mark(session,1);melee_web_lobj_writer_mark(session,2);
  check(melee_web_source_memory_payload_snapshot_restore(pre)==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,"negative restore PRE");
  auto difference=melee_web_source_memory_payload_snapshot_compare_live(post);check(difference.status==2,"missing-event actual difference");
  melee_web_lobj_writer_finalize(pre,post,difference,anchor,session);const auto* e=melee_web_lobj_writer_evidence(session);
  check(e->status==2&&!e->matched_count&&e->reason==7,"missing event remains UNKNOWN");return 0;
 }
 position.pos={4,5,6};melee_web_lobj_writer_phase(9);writer_actual_producer(light,matrix);
 check(type_calls==1&&position_calls==1&&!vector_calls,"observer adds no source getter/type evaluations");
 auto* post=snapshot();melee_web_lobj_writer_mark(session,1); // POST before draw301.
 position.pos={7,8,9};melee_web_lobj_writer_phase(10);deny_allocation=true;writer_actual_producer(light,matrix);
 melee_web_lobj_writer_mark(session,2);
 check(melee_web_source_memory_payload_snapshot_restore(pre)==MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK,"real payload restore");
 const auto comparison=melee_web_source_memory_payload_snapshot_compare_live(post);check(comparison.status==2,"real strict first difference");
 if(!std::strcmp(mode,"wrong-comparison")){
  auto wrong=comparison;wrong.expected_byte^=1;
  melee_web_lobj_writer_finalize(pre,post,wrong,anchor,session);check(melee_web_lobj_writer_evidence(session)->reason==10,"invented comparison refused");return 0;
 }
 if(!std::strcmp(mode,"wrong-row")){
  check(comparison.allocation_index!=UINT32_MAX,"row mutation bounded");
  lobj_writer.evidence.events[0].pre_row=comparison.allocation_index+1; // explicit malformed diagnostic fixture
  melee_web_lobj_writer_finalize(pre,post,comparison,anchor,session);
  check(melee_web_lobj_writer_evidence(session)->status==2&&melee_web_lobj_writer_evidence(session)->reason==7,"different PRE row never qualifies despite otherwise identical event");return 0;
 }
 melee_web_lobj_writer_finalize(pre,post,comparison,anchor,session);
 const auto* evidence=melee_web_lobj_writer_evidence(session);
 check(evidence->status==1&&evidence->first_difference_matches&&evidence->complete&&evidence->post_count==1&&evidence->drawn_count==2&&evidence->matched_index==0&&evidence->matched_count==1,"POST event selected, never global-last draw301");
 check(evidence->events[0].pre_row==comparison.allocation_index,"actual PRE/POST ledger order is stable");
 check(evidence->events[0].command==9&&evidence->events[1].command==10&&evidence->events[0].flags==3,"phase and historical-only qualifier retained");
 deny_allocation=false;EM_ASM({console.log('ACTUAL_LOBJ_WRITER_BASE64='+Buffer.from(HEAPU8.subarray($0,$0+16784)).toString('base64'));},evidence);
 std::puts("Actual writer occurrence correlated to POST; class lifetime remains UNKNOWN");
 melee_web_lobj_writer_disarm(session);melee_web_class_piece_disarm(anchor);
 melee_web_source_memory_payload_snapshot_destroy(post);melee_web_source_memory_payload_snapshot_destroy(pre);
#endif
 return 0;
}
