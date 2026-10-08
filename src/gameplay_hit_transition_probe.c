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
#define HIT_PROBE_ROWS 64 /* Diagnostic storage limit, never a source table bound. */
#define HIT_PROBE_ROW_BYTES 2048
static char rows[HIT_PROBE_ROWS][HIT_PROBE_ROW_BYTES];
static char line[HIT_PROBE_ROWS * HIT_PROBE_ROW_BYTES + 512];
static size_t cursor, count;
static unsigned invocations, calls[3];
static unsigned open_invocations[HIT_PROBE_ROWS], invocation_kind[HIT_PROBE_ROWS];
static unsigned invocation_logs[HIT_PROBE_ROWS], invocation_log_count[HIT_PROBE_ROWS];
static int active;
static unsigned stack[HIT_PROBE_ROWS], depth;
static HSD_GObj* victim;
static uint32_t match_index, generation;

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
static char* start(const char* phase, unsigned kind, unsigned invocation, size_t* used)
{
    if(count == HIT_PROBE_ROWS) {
        fprintf(stderr,"HIT_TRANSITION_PROBE diagnostic 64-row overflow\n");
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
       invocations==HIT_PROBE_ROWS)abort();
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
void melee_web_hit_probe_scheduler_return(void)
{
    if(!active)return;
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
    put(line,sizeof(line),&used,"]}");
#ifdef MELEE_WEB_HIT_PROBE_SYNTHETIC
    extern void melee_web_hit_probe_test_emit(const char*);
    melee_web_hit_probe_test_emit(line);
#elif defined(__EMSCRIPTEN__)
    EM_ASM({
        if(typeof window.meleeHitTransitionObservation!=='function')
            throw Error('Missing hit transition observer consumer');
        window.meleeHitTransitionObservation(UTF8ToString($0));
    },line);
#else
    fprintf(stderr,"HIT_TRANSITION_PROBE %s\n",line);
#endif
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
#endif
