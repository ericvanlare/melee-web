/* Private opt-in observations only. This storage never feeds source state. */
#include "gameplay_hit_transition_probe.h"
#include "gameplay_retail_state.h"
#include <melee/ft/types.h>
#include <melee/pl/player.h>
#include <melee/gm/forward.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/random.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef MELEE_WEB_RNG_DRAW_OBSERVER
#define HIT_PROBE_ROWS 76 /* Scoped candidate-cursor diagnostic capacity, not a source bound. */
#define HIT_PROBE_LEGACY_ROWS 64
#define HIT_PROBE_INVOCATIONS 64
#define HIT_PROBE_ROW_BYTES 2048
static char rows[HIT_PROBE_ROWS][HIT_PROBE_ROW_BYTES];
static char line[HIT_PROBE_ROWS * HIT_PROBE_ROW_BYTES + 512];
static size_t cursor, count;
static unsigned invocations, calls[3];
static unsigned open_invocations[HIT_PROBE_INVOCATIONS], invocation_kind[HIT_PROBE_INVOCATIONS];
static unsigned invocation_logs[HIT_PROBE_INVOCATIONS], invocation_log_count[HIT_PROBE_INVOCATIONS];
static int active;
static unsigned stack[HIT_PROBE_INVOCATIONS], depth;
static int candidate_enabled;
static HSD_GObj* victim;
static uint32_t match_index, generation;
static unsigned passes, pairs, geometries, producers;
static unsigned pass_open, pair_open, geometry_open, producer_open;
static unsigned pair_candidates, pair_encounter;
static int pair_self_seen, hit_index, hurt_index, geometry_result, geometry_consumed, producer_phase;
static Fighter* pair_attacker;
static const HitCapsule* candidate_hit;
static const HurtCapsule* geometry_hurt;
static uint32_t geometry_before[18], geometry_matrix[12], geometry_arguments[3];
static int geometry_matrix_present, geometry_mode, geometry_cache_before;


static uint32_t bits(float value)
{
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}
static void put(char* out, size_t capacity, size_t* used, const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int n = vsnprintf(out + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if(n < 0 || (size_t)n >= capacity - *used) abort();
    *used += (size_t)n;
}
static int identity(HSD_GObj* gobj, int strict)
{
    StaticPlayer* player = Player_GetPtrForSlot(1);
    if(!player || player->player_entity[0] != gobj) {
        if(strict) abort();
        return 0;
    }
    uint32_t actual_match, actual_generation;
    Fighter* fp = gobj ? gobj->user_data : NULL;
    if(!fp || fp->kind != FTKIND_KOOPA ||
       !melee_web_retail_primary_identity(1, gobj, &actual_match, &actual_generation))
        abort();
    if(victim && (victim != gobj || match_index != actual_match ||
                  generation != actual_generation)) abort();
    victim = gobj;
    match_index = actual_match;
    generation = actual_generation;
    return 1;
}
static int source_slot(HSD_GObj* gobj)
{
    int found = -1;
    if(!gobj) return found;
    for(unsigned slot=0; slot<4; ++slot) {
        StaticPlayer* player = Player_GetPtrForSlot(slot);
        if(player && player->player_entity[0] == gobj) {
            uint32_t source_match, source_generation;
            if(found != -1 ||
               !melee_web_retail_primary_identity(slot,gobj,&source_match,&source_generation) ||
               source_match != match_index) abort();
            found = (int)slot;
        }
    }
    return found;
}
static void emit_line(void)
{
#ifdef MELEE_WEB_HIT_PROBE_SYNTHETIC
    extern void melee_web_hit_probe_test_emit(const char*);
    melee_web_hit_probe_test_emit(line);
    fflush(stdout);
#elif defined(__EMSCRIPTEN__)
    EM_ASM({
        if(typeof window.meleeHitTransitionObservation!=='function')
            throw Error('Missing hit transition observer consumer');
        window.meleeHitTransitionObservation(UTF8ToString($0));
    },line);
#else
    fprintf(stderr,"HIT_TRANSITION_PROBE %s\n",line);
#endif
}
static void retain_overflow(void)
{
    size_t used=0;
    put(line,sizeof(line),&used,
        "{\"schema\":\"melee-web-hit-transition-probe\",\"version\":1,\"source_cursor\":%zu,"
        "\"overflowed\":true,\"hook_counts\":[%u,%u,%u],\"events\":[",
        cursor,calls[0],calls[1],calls[2]);
    for(size_t i=0;i<count;++i)put(line,sizeof(line),&used,"%s%s",i?",":"",rows[i]);
    put(line,sizeof(line),&used,"]}");
    emit_line();
}
static size_t row_capacity(void)
{
    return cursor==5239 && candidate_enabled ? HIT_PROBE_ROWS : HIT_PROBE_LEGACY_ROWS;
}
static char* start(const char* phase, unsigned kind, unsigned invocation, size_t* used)
{
    if(count == row_capacity()) {
        retain_overflow();
        fprintf(stderr,"HIT_TRANSITION_PROBE diagnostic %zu-row overflow\n",row_capacity());
        abort();
    }
    if(!seed_ptr) abort();
    char* row=rows[count];
    *used=0;
    put(row,HIT_PROBE_ROW_BYTES,used,
        "{\"sequence\":%zu,\"phase\":\"%s\",\"kind\":%u,\"invocation\":%u,"
        "\"slot\":1,\"entity_index\":0,\"match_index\":%u,\"generation\":%u,"
        "\"fighter_player_id\":1,\"fighter_gobj_linked\":true,\"rng\":\"%08x\"",
        count,phase,kind,invocation,match_index,generation,*seed_ptr);
    ++count;
    return row;
}
static void snapshot(char* row,size_t* used)
{
    if(!identity(victim,1)) abort();
    const Fighter* fp=victim->user_data;
    put(row,HIT_PROBE_ROW_BYTES,used,
        ",\"motion\":%d,\"animation\":%d,\"gate_x221f_b3\":%s,\"gate_x2219_b1\":%s,"
        "\"damage_bits\":\"%08x\",\"damage_applied\":%d,\"damage_temp_bits\":\"%08x\","
        "\"knockback_bits\":[\"%08x\",\"%08x\",\"%08x\"],\"hitlag_bits\":\"%08x\","
        "\"source_gobj\":\"%016llx\",\"source_slot\":%d,\"source_player\":%u,"
        "\"time_since_hit\":%d,\"x18a0_bits\":\"%08x\"",
        fp->motion_id,fp->anim_id,fp->x221F_b3?"true":"false",fp->x2219_b1?"true":"false",
        bits(fp->dmg.x1830_percent),fp->dmg.x183C_applied,
        bits(fp->dmg.x1838_percentTemp),bits(fp->x8c_kb_vel.x),bits(fp->x8c_kb_vel.y),
        bits(fp->x8c_kb_vel.z),bits(fp->dmg.x195c_hitlag_frames),
        (unsigned long long)(uintptr_t)fp->dmg.x1868_source,source_slot(fp->dmg.x1868_source),
        (unsigned)fp->dmg.x18c4_source_ply,fp->dmg.x18ac_time_since_hit,bits(fp->dmg.x18a0));
}
void melee_web_hit_probe_cursor(size_t index)
{
    /* A selected cursor may not advance before its scheduler-return marker. */
    if(active) abort();
    cursor=index;
    active=0;
    candidate_enabled=0;
#ifdef MELEE_WEB_HIT_PROBE_SYNTHETIC
    extern int melee_web_hit_probe_test_selected(size_t);
    active=melee_web_hit_probe_test_selected(index);
#elif defined(__EMSCRIPTEN__)
    active=EM_ASM_INT({
        return Array.isArray(window.__meleeHitTransitionProbeCursors) &&
            window.__meleeHitTransitionProbeCursors.includes($0);
    },index);
#endif
    if(!active) return;
    if(index!=5238 && index!=5239 && index!=5240) abort();
    victim=NULL;count=0;invocations=0;depth=0;
    passes=pairs=geometries=producers=0;
    pass_open=pair_open=geometry_open=producer_open=0;
    memset(calls,0,sizeof(calls));
    memset(open_invocations,0,sizeof(open_invocations));
    StaticPlayer* player=Player_GetPtrForSlot(1);
    if(!player||!player->player_entity[0]||!identity(player->player_entity[0],1))abort();
    size_t used;
    char* row=start("scheduler_start",3,0,&used);
    snapshot(row,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");
}
unsigned melee_web_hit_probe_begin(HSD_GObj* gobj,unsigned kind,int requested,
    unsigned flags,float frame,float speed,float blend,size_t log_count,int log_kind)
{
    if(!active||!identity(gobj,0))return 0;
    if(kind>2 || (kind==1 && (log_count>20 || (log_kind!=0 && log_kind!=1))) ||
       invocations==HIT_PROBE_INVOCATIONS || depth==HIT_PROBE_INVOCATIONS)abort();
    unsigned id=++invocations;
    stack[depth++]=id;
    open_invocations[id-1]=1;invocation_kind[id-1]=kind;
    invocation_logs[id-1]=0;invocation_log_count[id-1]=(unsigned)log_count;
    ++calls[kind];
    size_t used;
    char* row=start("entry",kind,id,&used);
    snapshot(row,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,
        ",\"requested_motion\":%d,\"flags\":\"%08x\",\"frame_bits\":\"%08x\","
        "\"speed_bits\":\"%08x\",\"blend_bits\":\"%08x\",\"log_count\":%zu,\"log_kind\":%d}",
        requested,flags,bits(frame),bits(speed),bits(blend),log_count,log_kind);
    return id;
}
void melee_web_hit_probe_log(unsigned id,size_t index,int entity_kind,int fighter_kind,
    HSD_GObj* source,const void* hit,const void* hurt,float x,float y,float z,float damage,
    size_t hit_bytes)
{
    if(!id)return;
    if(!active||id>invocations||!depth||stack[depth-1]!=id||!open_invocations[id-1]||invocation_kind[id-1]!=1||
       index!=invocation_logs[id-1]||index>=invocation_log_count[id-1])abort();
    ++invocation_logs[id-1];
    size_t used;
    char* row=start("log",1,id,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,
        ",\"log_index\":%zu,\"entity_kind\":%d,\"fighter_kind\":%d,"
        "\"source_gobj\":\"%016llx\",\"source_slot\":%d,\"hit\":\"%016llx\","
        "\"hurt\":\"%016llx\",\"position_bits\":[\"%08x\",\"%08x\",\"%08x\"],"
        "\"damage_bits\":\"%08x\",\"hit_bytes\":%zu}",
        index,entity_kind,fighter_kind,(unsigned long long)(uintptr_t)source,
        source_slot(source),(unsigned long long)(uintptr_t)hit,
        (unsigned long long)(uintptr_t)hurt,bits(x),bits(y),bits(z),bits(damage),hit_bytes);
}
void melee_web_hit_probe_end(HSD_GObj* gobj,unsigned id)
{
    if(!id)return;
    if(!active||!identity(gobj,1)||id>invocations||!depth||stack[depth-1]!=id||!open_invocations[id-1]||
       (invocation_kind[id-1]==1 &&
        invocation_logs[id-1]!=invocation_log_count[id-1]))abort();
    open_invocations[id-1]=0;
    --depth;
    size_t used;
    char* row=start("return",invocation_kind[id-1],id,&used);
    snapshot(row,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");
}

static char* candidate_row(const char* phase, unsigned ordinal, size_t* used)
{
    char* row=start(phase,4,ordinal,used);
    snapshot(row,used);
    put(row,HIT_PROBE_ROW_BYTES,used,
        ",\"pass\":%u,\"pair\":%u,\"attacker_slot\":0,\"receiver_slot\":1,"
        "\"hit_index\":%d,\"hurt_index\":%d",pass_open,pair_open,hit_index,hurt_index);
    return row;
}
static void vec_bits(uint32_t* out,const Vec3* v)
{ out[0]=bits(v->x);out[1]=bits(v->y);out[2]=bits(v->z); }
static void bit_array(char* row,size_t* used,const char* name,const uint32_t* values,size_t n)
{
    put(row,HIT_PROBE_ROW_BYTES,used,",\"%s\":[",name);
    for(size_t i=0;i<n;++i)put(row,HIT_PROBE_ROW_BYTES,used,"%s\"%08x\"",i?",":"",values[i]);
    put(row,HIT_PROBE_ROW_BYTES,used,"]");
}
unsigned melee_web_hit_probe_pass_begin(HSD_GObj* receiver)
{
    if(!active||cursor!=5239||!identity(receiver,0))return 0;
    if(pass_open||pair_open||geometry_open||producer_open)abort();
    candidate_enabled=1;
    pass_open=++passes;hit_index=hurt_index=-1;
    size_t used;char* row=candidate_row("pass_entry",pass_open,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");return pass_open;
}
void melee_web_hit_probe_pass_end(unsigned pass)
{
    if(!pass)return;
    if(pass!=pass_open||pair_open||geometry_open||producer_open)abort();
    size_t used;char* row=candidate_row("pass_return",pass,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");pass_open=0;
}
unsigned melee_web_hit_probe_pair_begin(unsigned pass,HSD_GObj* attacker,unsigned encounter,int self_seen)
{
    if(!pass)return 0;
    if(pass!=pass_open||pair_open||geometry_open||producer_open)abort();
    StaticPlayer* player=Player_GetPtrForSlot(0);
    if(!player||player->player_entity[0]!=attacker)return 0;
    uint32_t attacker_match,attacker_generation;
    if(!melee_web_retail_primary_identity(0,attacker,&attacker_match,&attacker_generation)||
       attacker_match!=match_index)abort();
    const Fighter* receiver=victim->user_data;
    /* These are authored array bounds, not diagnostic storage or a clamp. */
    _Static_assert(sizeof(receiver->x914)/sizeof(receiver->x914[0])==4,"authored hit bound");
    _Static_assert(sizeof(receiver->hurt_capsules)/sizeof(receiver->hurt_capsules[0])==15,"authored hurt bound");
    if(receiver->hurt_capsules_len>sizeof(receiver->hurt_capsules)/sizeof(receiver->hurt_capsules[0]))abort();
    pair_attacker=attacker->user_data;pair_open=++pairs;pair_encounter=encounter;
    pair_self_seen=self_seen;pair_candidates=0;candidate_hit=NULL;hit_index=hurt_index=-1;geometry_result=geometry_consumed=0;
    size_t used;char* row=candidate_row("pair_entry",pair_open,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,
        ",\"encounter\":%u,\"self_seen\":%s,\"attacker_generation\":%u,"
        "\"hurt_length\":%u,\"receiver_ground_air\":%d,\"receiver_x1988\":%d,"
        "\"receiver_x198c\":%d,\"receiver_shield\":%s,\"receiver_x221d_b6\":%s}",
        encounter,self_seen?"true":"false",attacker_generation,(unsigned)receiver->hurt_capsules_len,
        receiver->ground_or_air,receiver->x1988,receiver->x198C,
        receiver->x221B_b0?"true":"false",receiver->x221D_b6?"true":"false");
    return pair_open;
}
void melee_web_hit_probe_pair_end(unsigned pair)
{
    if(!pair)return;
    if(pair!=pair_open||geometry_open||producer_open)abort();
    size_t used;char* row=candidate_row("pair_return",pair,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,",\"encounter\":%u,\"self_seen\":%s,\"candidates\":%u}",
        pair_encounter,pair_self_seen?"true":"false",pair_candidates);
    pair_open=0;candidate_hit=NULL;hit_index=hurt_index=-1;
}
void melee_web_hit_probe_candidate(unsigned pair,unsigned index,const HitCapsule* hit)
{
    if(!pair)return;
    if(pair!=pair_open||geometry_open||producer_open||index!=pair_candidates||index>=4||
       hit!=&pair_attacker->x914[index])abort();
    candidate_hit=hit;hit_index=(int)index;hurt_index=-1;geometry_result=geometry_consumed=0;++pair_candidates;
    size_t used;char* row=candidate_row("candidate",pair,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,
        ",\"hit_state\":%d,\"hit_element\":%u,\"hit_flags\":[%u,%u,%u,%u,%u,%u],"
        "\"hit_damage_bits\":\"%08x\",\"hit_radius_bits\":\"%08x\"}",
        hit->state,(unsigned)hit->element,(unsigned)hit->x42_b5,(unsigned)hit->x40_b2,
        (unsigned)hit->x40_b3,(unsigned)hit->hit_grabbed_victim_only,(unsigned)hit->x43_b2,
        (unsigned)hit->x40_b0,bits(hit->damage),bits(hit->scale));
}
unsigned melee_web_hit_probe_geometry_begin(const HitCapsule* hit,const HurtCapsule* hurt,
    const void* matrix,int mode,float attacker_scale,float receiver_scale,float z)
{
    if(!pair_open||hit!=candidate_hit)return 0;
    if(geometry_open||producer_open)abort();
    const Fighter* receiver=victim->user_data;hurt_index=-1;
    for(unsigned i=0;i<receiver->hurt_capsules_len;++i)
        if(hurt==&receiver->hurt_capsules[i].capsule)hurt_index=(int)i;
    if(hurt_index<0)abort();
    geometry_hurt=hurt;geometry_open=++geometries;
    vec_bits(geometry_before, &hit->x58);vec_bits(geometry_before+3,&hit->x4C);
    vec_bits(geometry_before+6,&hurt->a_pos);vec_bits(geometry_before+9,&hurt->b_pos);
    vec_bits(geometry_before+12,&hit->hurt_coll_pos);
    geometry_before[15]=bits(hit->coll_distance);geometry_before[16]=bits(hit->scale);
    geometry_before[17]=bits(hurt->scale);geometry_cache_before=hurt->skip_update_pos;
    geometry_matrix_present=matrix!=NULL;
    if(matrix)memcpy(geometry_matrix,matrix,sizeof(geometry_matrix));
    else memset(geometry_matrix,0,sizeof(geometry_matrix));
    geometry_mode=mode;geometry_arguments[0]=bits(attacker_scale);
    geometry_arguments[1]=bits(receiver_scale);geometry_arguments[2]=bits(z);
    return geometry_open;
}
void melee_web_hit_probe_geometry_end(unsigned geometry,int result)
{
    if(!geometry)return;
    if(geometry!=geometry_open||!pair_open||producer_open)abort();
    uint32_t after[10];vec_bits(after,&geometry_hurt->a_pos);vec_bits(after+3,&geometry_hurt->b_pos);
    vec_bits(after+6,&candidate_hit->hurt_coll_pos);after[9]=bits(candidate_hit->coll_distance);
    size_t used;char* row=candidate_row("geometry",geometry,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,",\"result\":%d,\"mode\":%d,"
        "\"cache_before\":%s,\"cache_after\":%s,\"matrix_present\":%s",
        result,geometry_mode,geometry_cache_before?"true":"false",
        geometry_hurt->skip_update_pos?"true":"false",geometry_matrix_present?"true":"false");
    bit_array(row,&used,"geometry_before",geometry_before,18);
    bit_array(row,&used,"geometry_after",after,10);
    bit_array(row,&used,"arguments",geometry_arguments,3);
    bit_array(row,&used,"matrix_bits",geometry_matrix,12);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");geometry_open=0;geometry_result=result;geometry_consumed=0;
}
unsigned melee_web_hit_probe_producer_begin(Fighter* attacker,const HitCapsule* hit,
    Fighter* receiver,const void* hurt,size_t log0,size_t log1)
{
    if(!pair_open||attacker!=pair_attacker||receiver!=victim->user_data||hit!=candidate_hit)return 0;
    if(geometry_open||producer_open||!geometry_result||geometry_consumed||hurt_index<0||
       hurt!=&receiver->hurt_capsules[hurt_index]||log0>20||log1>20)abort();
    producer_open=++producers;geometry_consumed=1;producer_phase=-1;
    size_t used;char* row=candidate_row("producer_entry",producer_open,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,",\"log0_count\":%zu,\"log1_count\":%zu}",log0,log1);
    return producer_open;
}
void melee_web_hit_probe_producer_branch(unsigned producer,unsigned branch,size_t log0,size_t log1)
{
    if(!producer)return;
    if(producer!=producer_open||!pair_open||branch>5||log0>20||log1>20)abort();
    int expected=branch==0||branch==3?-1:branch==1?0:branch==2?1:branch==4?3:4;
    if(producer_phase!=expected)abort();
    producer_phase=(int)branch;
    size_t used;char* row=candidate_row("producer_branch",producer,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,",\"branch\":%u,\"log0_count\":%zu,\"log1_count\":%zu}",branch,log0,log1);
}
void melee_web_hit_probe_producer_end(unsigned producer,int result,size_t log0,size_t log1)
{
    if(!producer)return;
    if(producer!=producer_open||!pair_open||log0>20||log1>20||
       producer_phase<0||(producer_phase==0?result!=0:result!=1))abort();
    size_t used;char* row=candidate_row("producer_return",producer,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,",\"result\":%d,\"log0_count\":%zu,\"log1_count\":%zu}",result,log0,log1);
    producer_open=0;
}
void melee_web_hit_probe_scheduler_return(void)
{
    if(!active)return;
    if(pass_open||pair_open||geometry_open||producer_open)abort();
    for(unsigned i=0;i<invocations;++i)if(open_invocations[i])abort();
    size_t used;
    char* row=start("scheduler_return",3,0,&used);
    snapshot(row,&used);
    put(row,HIT_PROBE_ROW_BYTES,&used,"}");
    used=0;
    put(line,sizeof(line),&used,
        "{\"schema\":\"melee-web-hit-transition-probe\",\"version\":1,\"source_cursor\":%zu,"
        "\"overflowed\":false,\"hook_counts\":[%u,%u,%u],\"events\":[",
        cursor,calls[0],calls[1],calls[2]);
    for(size_t i=0;i<count;++i)put(line,sizeof(line),&used,"%s%s",i?",":"",rows[i]);
    put(line,sizeof(line),&used,"]");
    if(cursor==5239)put(line,sizeof(line),&used,",\"candidate_enabled\":true,\"candidate_passes\":%u,\"candidate_pairs\":%u",passes,pairs);
    put(line,sizeof(line),&used,"}");
    emit_line();
    active=0;
}
#else
void melee_web_hit_probe_cursor(size_t cursor){(void)cursor;}
void melee_web_hit_probe_scheduler_return(void){}
unsigned melee_web_hit_probe_begin(struct HSD_GObj* g,unsigned k,int r,unsigned f,
 float a,float b,float c,size_t n,int t)
{(void)g;(void)k;(void)r;(void)f;(void)a;(void)b;(void)c;(void)n;(void)t;return 0;}
void melee_web_hit_probe_log(unsigned a,size_t b,int c,int d,struct HSD_GObj* e,
 const void* f,const void* g,float h,float i,float j,float k,size_t l)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;(void)j;(void)k;(void)l;}
void melee_web_hit_probe_end(struct HSD_GObj* g,unsigned id){(void)g;(void)id;}

unsigned melee_web_hit_probe_pass_begin(struct HSD_GObj* g){(void)g;return 0;}
void melee_web_hit_probe_pass_end(unsigned a){(void)a;}
unsigned melee_web_hit_probe_pair_begin(unsigned a,struct HSD_GObj* g,unsigned b,int c)
{(void)a;(void)g;(void)b;(void)c;return 0;}
void melee_web_hit_probe_pair_end(unsigned a){(void)a;}
void melee_web_hit_probe_candidate(unsigned a,unsigned b,const struct HitCapsule* c){(void)a;(void)b;(void)c;}
unsigned melee_web_hit_probe_geometry_begin(const struct HitCapsule* a,const struct HurtCapsule* b,
 const void* c,int d,float e,float f,float g)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;return 0;}
void melee_web_hit_probe_geometry_end(unsigned a,int b){(void)a;(void)b;}
unsigned melee_web_hit_probe_producer_begin(struct Fighter* a,const struct HitCapsule* b,
 struct Fighter* c,const void* d,size_t e,size_t f)
{(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;return 0;}
void melee_web_hit_probe_producer_branch(unsigned a,unsigned b,size_t c,size_t d)
{(void)a;(void)b;(void)c;(void)d;}
void melee_web_hit_probe_producer_end(unsigned a,int b,size_t c,size_t d)
{(void)a;(void)b;(void)c;(void)d;}
#endif
