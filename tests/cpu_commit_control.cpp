/* Executes the production fixed coordinator and static writer. Named external
 * owner leaves model their documented predicate dependencies; component
 * controls separately execute the actual owners. This is not a fabricated
 * replacement coordinator or a complete source-game restore test. */
#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_interval.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
using namespace melee_web;
namespace {
unsigned fail_admission=0, writes=0, order=0;
unsigned legacy_action_admits=0,corrected_action_admits=0;
unsigned legacy_action_writes=0,corrected_action_writes=0;
unsigned fighter_fields=9, private_slots=9,audio_value=9,flow_value=9,bootstrap_value=9,context_value=9;
unsigned static_value=3, invariant=5;
bool stable_owner=true,stable_topology=true,corrected_action_ok=true;
std::unique_ptr<GameplayCpuStatics> statics(bool history=false){
    CpuStaticRow rows[2]={{uint32_t(uintptr_t(&static_value)),sizeof(static_value),CpuStaticPolicy::Restore,CpuStaticDomain::OriginalSourceC},
                         {uint32_t(uintptr_t(&invariant)),sizeof(invariant),CpuStaticPolicy::Invariant,CpuStaticDomain::OriginalSourceC}};
    if(rows[1].address<rows[0].address)std::swap(rows[0],rows[1]);
    CpuStaticBounds bounds{rows[0].address,std::uintptr_t(rows[1].address)+rows[1].bytes};
    auto s=GameplayCpuStatics::configure(rows,bounds,{});assert(s&&s->capture());
    if(history)assert(s->begin_history(100)&&s->history_begin_incarnation(1,100));
    static_value=9;
    if(history)assert(s->history_seal_endpoint(1,102)&&s->history_close_endpoint(1,102));
    else assert(s->seal_post());
    return s;
}
bool admitted(unsigned n){return fail_admission!=n&&stable_owner&&stable_topology;}
void write(unsigned n){assert(order+1==n);order=n;++writes;}
}
extern "C" {
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
int slippi_particle_presentation_anchor_admit(SlippiParticlePresentationAnchor){return admitted(11);}
#endif
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
int slippi_tobj_anchor_admit(SlippiTObjAnchor){return admitted(10);}
#endif
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
int slippi_lobj_anchor_admit(SlippiLObjAnchor){return admitted(9);}
#endif
int melee_web_cpu_interval_admit(uint64_t t){return t==4&&admitted(1);}
int melee_web_action_interval_admit(const MeleeWebActionInterval*){++legacy_action_admits;return admitted(2)&&fighter_fields==9&&private_slots==9;}
int melee_web_action_interval_admit_corrected(const MeleeWebActionInterval*){++corrected_action_admits;return corrected_action_ok&&admitted(2)&&fighter_fields==9&&private_slots==9;}
int melee_web_audio_snapshot_admit(MeleeWebAudio*,const MeleeWebAudioSnapshot*){return admitted(3);}
int melee_web_match_flow_cpu_snapshot_admit(MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t){return admitted(4);}
int melee_web_gameplay_bootstrap_snapshot_admit(const MeleeWebGameplayBootstrapSnapshot*,char*,size_t){return admitted(5);}
int melee_web_match_context_snapshot_admit(MeleeWebMatchContext*,const MeleeWebMatchContextSnapshot*,char*,size_t){return admitted(6);}
MeleeWebSourceMemoryPayloadSnapshotStatus melee_web_source_memory_payload_snapshot_admit(const MeleeWebSourceMemoryPayloadSnapshot*){return admitted(7)?MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK:MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;}
int melee_web_action_interval_restore(MeleeWebActionInterval*){if(!admitted(2)||fighter_fields!=9||private_slots!=9)return 0;++legacy_action_writes;write(1);private_slots=3;assert(fighter_fields==9);return 1;}
int melee_web_action_interval_restore_corrected(MeleeWebActionInterval*){if(!corrected_action_ok||!admitted(2)||fighter_fields!=9||private_slots!=9)return 0;++corrected_action_writes;write(1);private_slots=3;assert(fighter_fields==9);return 1;}
int melee_web_audio_snapshot_restore(MeleeWebAudio* a,const MeleeWebAudioSnapshot* s){if(!melee_web_audio_snapshot_admit(a,s))return 0;write(2);audio_value=3;return 1;}
int melee_web_match_flow_cpu_snapshot_restore(MeleeWebMatchFlow* f,const MeleeWebMatchFlowCpuSnapshot* s,char*,size_t){if(!melee_web_match_flow_cpu_snapshot_admit(f,s,nullptr,0))return 0;write(3);flow_value=3;return 1;}
int melee_web_gameplay_bootstrap_snapshot_restore(const MeleeWebGameplayBootstrapSnapshot* s,char*,size_t){if(!melee_web_gameplay_bootstrap_snapshot_admit(s,nullptr,0))return 0;write(4);bootstrap_value=3;return 1;}
int melee_web_match_context_snapshot_restore(MeleeWebMatchContext* c,const MeleeWebMatchContextSnapshot* s,char*,size_t){if(!melee_web_match_context_snapshot_admit(c,s,nullptr,0))return 0;write(5);context_value=3;return 1;}
MeleeWebSourceMemoryPayloadSnapshotStatus melee_web_source_memory_payload_snapshot_restore(const MeleeWebSourceMemoryPayloadSnapshot* s){auto r=melee_web_source_memory_payload_snapshot_admit(s);if(r)return r;write(6);fighter_fields=3;return r;}
}
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
namespace {
unsigned arena_refusal;
unsigned char physical_arena[128];
uint32_t audio_scalar;
}
GameplayActionStoreHistory::GameplayActionStoreHistory(MeleeWebActionInterval* capsule) noexcept
    :capsule_(capsule){}
GameplayActionStoreHistory::~GameplayActionStoreHistory()=default;
std::unique_ptr<GameplayActionStoreHistory> GameplayActionStoreHistory::capture()
{ return std::unique_ptr<GameplayActionStoreHistory>(new GameplayActionStoreHistory(nullptr)); }
bool GameplayActionStoreHistory::prepare_restore(uint64_t incarnation) noexcept
{ assert(incarnation==1&&!writes);return true; }
bool GameplayActionStoreHistory::admit_restore(uint64_t incarnation) const noexcept
{ assert(incarnation==1&&!writes);return true; }
bool GameplayActionStoreHistory::restore_pre(uint64_t incarnation) noexcept
{ assert(incarnation==1&&fighter_fields==9);write(1);private_slots=3;return true; }
extern "C" {
uint64_t melee_web_audio_generation(MeleeWebAudio*){return 1;}
MeleeWebAudioCpuStorage melee_web_audio_cpu_storage(MeleeWebAudio*,uint64_t)
{ return {&audio_scalar,&audio_scalar}; }
MeleeWebAudioRangeVisitResult melee_web_audio_visit_owned_ranges(
    MeleeWebAudio* audio,uint64_t generation,size_t,MeleeWebAudioOwnedRangeVisitor visit,void* context)
{
    assert(!writes&&generation==1);
    MeleeWebAudioOwnedRange range{MELEE_WEB_AUDIO_RANGE_AUDIO_OWNER,
        arena_refusal==2?static_cast<const void*>(physical_arena):&audio_scalar,
        sizeof(audio_scalar),audio,generation};
    return visit(&range,context)?MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE:MELEE_WEB_AUDIO_RANGE_VISIT_STOPPED;
}
int slippi_particle_presentation_captured_pools_admit(SlippiParticlePresentationAnchor)
{ assert(!writes);return arena_refusal!=3; }
int melee_web_cpu_interval_motor_history_restore_admit(uint64_t token,uint64_t inc,uint32_t end)
{ assert(!writes);return token==4&&inc==1&&end==102; }
void melee_web_cpu_interval_motor_history_restore_commit(uint64_t,uint64_t,uint32_t)
{ assert(static_value==3&&fighter_fields==3);write(8); }
MeleeWebGameplayHeapHistoryPrepareResult melee_web_gameplay_heap_history_prepare(
    const MeleeWebGameplayHeapHistorySnapshot*)
{
    assert(!writes);
    return arena_refusal==1?MeleeWebGameplayHeapHistoryPrepareResult{
        MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT,nullptr}:
        MeleeWebGameplayHeapHistoryPrepareResult{MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK,
            reinterpret_cast<MeleeWebGameplayHeapHistoryPrepared*>(1)};
}
MeleeWebGameplayHeapHistoryStatus melee_web_gameplay_heap_history_admit(
    MeleeWebGameplayHeapHistoryPrepared*)
{
    assert(!writes);
    return arena_refusal==4?MELEE_WEB_GAMEPLAY_HEAP_HISTORY_INVALID_ARGUMENT:
        MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK;
}
void melee_web_gameplay_heap_history_commit_prevalidated(MeleeWebGameplayHeapHistoryPrepared*)
{
    assert(private_slots==3&&audio_value==3&&flow_value==3&&bootstrap_value==3&&context_value==3);
    assert(fighter_fields==9&&static_value==9);write(6);fighter_fields=3;
}
void melee_web_gameplay_heap_history_prepared_destroy(MeleeWebGameplayHeapHistoryPrepared*){}
MeleeWebSourceMemoryHistoryPrepareResult melee_web_source_memory_history_prepare(
    const MeleeWebSourceMemoryHistory*)
{
    assert(!writes);
    if(arena_refusal==6)return {MELEE_WEB_SOURCE_MEMORY_HISTORY_PRE_CHANGED,nullptr};
    return {MELEE_WEB_SOURCE_MEMORY_HISTORY_OK,
            reinterpret_cast<MeleeWebSourceMemoryHistoryPrepared*>(1)};
}
void melee_web_source_memory_history_describe_pre_change(
    const MeleeWebSourceMemoryHistory*,char* out,size_t bytes)
{ assert(!writes);std::snprintf(out,bytes,"modeled first mismatch"); }
MeleeWebSourceMemoryHistoryStatus melee_web_source_memory_history_admit(
    const MeleeWebSourceMemoryHistoryPrepared*)
{
    assert(!writes);
    return arena_refusal==5?MELEE_WEB_SOURCE_MEMORY_HISTORY_STALE_PREPARE:
        MELEE_WEB_SOURCE_MEMORY_HISTORY_OK;
}
MeleeWebSourceMemoryHistoryStatus melee_web_source_memory_history_commit_prevalidated(
    MeleeWebSourceMemoryHistoryPrepared*) noexcept
{ assert(fighter_fields==3&&static_value==9);write(7);return MELEE_WEB_SOURCE_MEMORY_HISTORY_OK; }
void melee_web_source_memory_history_prepared_destroy(MeleeWebSourceMemoryHistoryPrepared*){}
}
static void arena_coordinator()
{
    using R=GameplayCpuCommitResult;
    const R expected[]{R::Ok,R::Payload,R::Audio,R::Particle,R::Payload,R::Payload,R::Payload};
    const char* detail[]{"","leaf=sdk-prepare status=1","","","leaf=sdk-admit status=1",
        "leaf=shadow-admit status=6","leaf=shadow-prepare status=5 modeled first mismatch"};
    for(arena_refusal=0;arena_refusal<sizeof(expected)/sizeof(*expected);++arena_refusal){
        writes=order=fail_admission=0;
        fighter_fields=private_slots=audio_value=flow_value=bootstrap_value=context_value=9;
        static_value=3;invariant=5;
        auto s=statics(true);
        auto actions=GameplayActionStoreHistory::capture();
        GameplayCpuCommitOwners o{nullptr,reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
            reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),
            reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),reinterpret_cast<MeleeWebMatchContext*>(7),
            reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),s.get(),4};
        o.action_history=actions.get();o.history_incarnation=1;o.history_end_frame=102;
        o.arena=reinterpret_cast<const MeleeWebGameplayHeapHistorySnapshot*>(1);
        o.shadow=reinterpret_cast<MeleeWebSourceMemoryHistory*>(1);
        o.arena_base=physical_arena;o.arena_bytes=sizeof(physical_arena);
        char refusal[96]{};
        assert(gameplay_cpu_commit_history_pre(o,refusal,sizeof(refusal))==expected[arena_refusal]);
        assert(std::strcmp(refusal,detail[arena_refusal])==0);
        if(arena_refusal)
            assert(writes==0&&static_value==9&&fighter_fields==9&&private_slots==9);
        else
            assert(writes==8&&static_value==3&&fighter_fields==3&&private_slots==3);
    }
    puts("Owned arena coordinator rejects before writes and commits in order");
}
#endif
int main(){
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
    arena_coordinator();
#endif
    using R=GameplayCpuCommitResult;
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
    const unsigned last_refusal=9
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
        +1
#endif
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
        +1
#endif
        ;
#else
    const unsigned last_refusal=8;
#endif
    for(unsigned fail=1;fail<=last_refusal;++fail){
        writes=order=0;fighter_fields=private_slots=audio_value=flow_value=bootstrap_value=context_value=9;
        legacy_action_admits=corrected_action_admits=legacy_action_writes=corrected_action_writes=0;
        static_value=3;invariant=5;auto s=statics();
        GameplayCpuCommitOwners o{reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
            reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
            reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),s.get(),4};
        fail_admission=fail;if(fail==8)invariant=6;
        const auto result=gameplay_cpu_commit(o);
        assert(result!=R::Ok);assert(writes==0&&order==0&&fighter_fields==9&&private_slots==9&&static_value==9);
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
        if(fail==11)assert(result==R::Particle);
#endif
        assert(audio_value==9&&flow_value==9&&bootstrap_value==9&&context_value==9);
        assert(corrected_action_admits==0&&corrected_action_writes==0);
        const bool action_reached=fail>=2&&fail<=8;
        assert(legacy_action_admits==(action_reached?1u:0u)&&legacy_action_writes==0);
    }
    fail_admission=0;writes=order=0;invariant=5;static_value=3;
    legacy_action_admits=corrected_action_admits=legacy_action_writes=corrected_action_writes=0;
    auto s=statics();
    GameplayCpuCommitOwners o{reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
        reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
        reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),s.get(),4};
    assert(gameplay_cpu_commit(o)==R::Ok);assert(writes==6&&order==6);
    assert(legacy_action_admits==1&&legacy_action_writes==1&&corrected_action_admits==0&&corrected_action_writes==0);
    assert(fighter_fields==3&&private_slots==3&&audio_value==3&&flow_value==3&&bootstrap_value==3&&context_value==3&&static_value==3);
#if defined(MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_LOBJ_PRESENTATION_CACHE
#if defined(MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_TOBJ_PRESENTATION_CACHE
#if defined(MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE) && MELEE_WEB_SLIPPI_PARTICLE_PRESENTATION_CACHE
    puts("Actual fixed commit coordinator: eleven admission refusals write zero; light/texture/particle leaves modeled; ordered successful chain");
#else
    puts("Actual fixed commit coordinator: ten admission refusals write zero; light/texture leaves modeled; ordered successful chain");
#endif
#else
    puts("Actual fixed commit coordinator: nine admission refusals write zero; light leaf modeled; ordered successful chain preserves later predicates");
#endif
#else
    puts("Actual fixed commit coordinator: eight admission refusals write zero; ordered successful chain preserves later predicates");
#endif
    puts("Corrected coordinator preserves all refusal-before-write checks and selects the corrected ActionStore writer only");
    for(unsigned fail=1;fail<=last_refusal;++fail){
        writes=order=0;fighter_fields=private_slots=audio_value=flow_value=bootstrap_value=context_value=9;
        legacy_action_admits=corrected_action_admits=legacy_action_writes=corrected_action_writes=0;
        static_value=3;invariant=5;auto corrected_statics=statics();
        GameplayCpuCommitOwners corrected_owners{
            reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
            reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
            reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),corrected_statics.get(),4};
        fail_admission=fail;
        if(fail==8)invariant=6;
        const bool action_reached=fail>=2&&fail<=8;
        const auto result=gameplay_cpu_commit_corrected(corrected_owners);
        assert(result!=R::Ok&&writes==0&&order==0);
        assert(fighter_fields==9&&private_slots==9&&audio_value==9&&flow_value==9&&bootstrap_value==9&&context_value==9);
        assert(static_value==9&&corrected_action_writes==0&&legacy_action_writes==0&&legacy_action_admits==0);
        assert(corrected_action_admits==(action_reached?1u:0u));
        if(fail==2)assert(result==R::Action);
    }
    fail_admission=0;writes=order=0;corrected_action_ok=true;invariant=5;static_value=3;
    legacy_action_admits=corrected_action_admits=legacy_action_writes=corrected_action_writes=0;
    auto corrected_statics=statics();
    GameplayCpuCommitOwners corrected_owners{
        reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
        reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
        reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),corrected_statics.get(),4};
    assert(gameplay_cpu_commit_corrected(corrected_owners)==R::Ok);
    assert(writes==6&&order==6&&corrected_action_admits==1&&corrected_action_writes==1);
    assert(legacy_action_admits==0&&legacy_action_writes==0);
    assert(fighter_fields==3&&private_slots==3&&audio_value==3&&flow_value==3&&bootstrap_value==3&&context_value==3&&static_value==3);
    writes=order=0;fighter_fields=private_slots=audio_value=flow_value=bootstrap_value=context_value=9;
    legacy_action_admits=corrected_action_admits=legacy_action_writes=corrected_action_writes=0;
    static_value=3;invariant=5;corrected_action_ok=false;auto refused_corrected_statics=statics();
    corrected_owners.statics=refused_corrected_statics.get();
    assert(gameplay_cpu_commit_corrected(corrected_owners)==R::Action);
    assert(writes==0&&order==0&&corrected_action_admits==1&&corrected_action_writes==0&&legacy_action_admits==0);
    assert(fighter_fields==9&&private_slots==9&&audio_value==9&&flow_value==9&&bootstrap_value==9&&context_value==9&&static_value==9);
}
