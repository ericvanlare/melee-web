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
unsigned fighter_fields=9, private_slots=9,audio_value=9,flow_value=9,bootstrap_value=9,context_value=9;
unsigned static_value=3, invariant=5;
bool stable_owner=true,stable_topology=true;
std::unique_ptr<GameplayCpuStatics> statics(){
    CpuStaticRow rows[2]={{uint32_t(uintptr_t(&static_value)),sizeof(static_value),CpuStaticPolicy::Restore,CpuStaticDomain::OriginalSourceC},
                         {uint32_t(uintptr_t(&invariant)),sizeof(invariant),CpuStaticPolicy::Invariant,CpuStaticDomain::OriginalSourceC}};
    if(rows[1].address<rows[0].address)std::swap(rows[0],rows[1]);
    CpuStaticBounds bounds{rows[0].address,std::uintptr_t(rows[1].address)+rows[1].bytes};
    auto s=GameplayCpuStatics::configure(rows,bounds,{});assert(s&&s->capture());
    static_value=9;assert(s->seal_post());return s;
}
bool admitted(unsigned n){return fail_admission!=n&&stable_owner&&stable_topology;}
void write(unsigned n){assert(order+1==n);order=n;++writes;}
}
extern "C" {
int melee_web_cpu_interval_admit(uint64_t t){return t==4&&admitted(1);}
int melee_web_action_interval_admit(const MeleeWebActionInterval*){return admitted(2)&&fighter_fields==9&&private_slots==9;}
int melee_web_audio_snapshot_admit(MeleeWebAudio*,const MeleeWebAudioSnapshot*){return admitted(3);}
int melee_web_match_flow_cpu_snapshot_admit(MeleeWebMatchFlow*,const MeleeWebMatchFlowCpuSnapshot*,char*,size_t){return admitted(4);}
int melee_web_gameplay_bootstrap_snapshot_admit(const MeleeWebGameplayBootstrapSnapshot*,char*,size_t){return admitted(5);}
int melee_web_match_context_snapshot_admit(MeleeWebMatchContext*,const MeleeWebMatchContextSnapshot*,char*,size_t){return admitted(6);}
MeleeWebSourceMemoryPayloadSnapshotStatus melee_web_source_memory_payload_snapshot_admit(const MeleeWebSourceMemoryPayloadSnapshot*){return admitted(7)?MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK:MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_TOPOLOGY_CHANGED;}
int melee_web_action_interval_restore(MeleeWebActionInterval* a){if(melee_web_action_interval_admit(a)!=1)return 0;write(1);private_slots=3;assert(fighter_fields==9);return 1;}
int melee_web_audio_snapshot_restore(MeleeWebAudio* a,const MeleeWebAudioSnapshot* s){if(!melee_web_audio_snapshot_admit(a,s))return 0;write(2);audio_value=3;return 1;}
int melee_web_match_flow_cpu_snapshot_restore(MeleeWebMatchFlow* f,const MeleeWebMatchFlowCpuSnapshot* s,char*,size_t){if(!melee_web_match_flow_cpu_snapshot_admit(f,s,nullptr,0))return 0;write(3);flow_value=3;return 1;}
int melee_web_gameplay_bootstrap_snapshot_restore(const MeleeWebGameplayBootstrapSnapshot* s,char*,size_t){if(!melee_web_gameplay_bootstrap_snapshot_admit(s,nullptr,0))return 0;write(4);bootstrap_value=3;return 1;}
int melee_web_match_context_snapshot_restore(MeleeWebMatchContext* c,const MeleeWebMatchContextSnapshot* s,char*,size_t){if(!melee_web_match_context_snapshot_admit(c,s,nullptr,0))return 0;write(5);context_value=3;return 1;}
MeleeWebSourceMemoryPayloadSnapshotStatus melee_web_source_memory_payload_snapshot_restore(const MeleeWebSourceMemoryPayloadSnapshot* s){auto r=melee_web_source_memory_payload_snapshot_admit(s);if(r)return r;write(6);fighter_fields=3;return r;}
}
int main(){
    using R=GameplayCpuCommitResult;
    for(unsigned fail=1;fail<=8;++fail){
        writes=order=0;fighter_fields=private_slots=audio_value=flow_value=bootstrap_value=context_value=9;
        static_value=3;invariant=5;auto s=statics();
        GameplayCpuCommitOwners o{reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
            reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
            reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),s.get(),4};
        fail_admission=fail;if(fail==8)invariant=6;
        assert(gameplay_cpu_commit(o)!=R::Ok);assert(writes==0&&order==0&&fighter_fields==9&&private_slots==9&&static_value==9);
        assert(audio_value==9&&flow_value==9&&bootstrap_value==9&&context_value==9);
    }
    fail_admission=0;writes=order=0;invariant=5;static_value=3;auto s=statics();
    GameplayCpuCommitOwners o{reinterpret_cast<MeleeWebActionInterval*>(1),reinterpret_cast<MeleeWebAudio*>(2),reinterpret_cast<MeleeWebAudioSnapshot*>(3),
        reinterpret_cast<MeleeWebMatchFlow*>(4),reinterpret_cast<MeleeWebMatchFlowCpuSnapshot*>(5),reinterpret_cast<MeleeWebGameplayBootstrapSnapshot*>(6),
        reinterpret_cast<MeleeWebMatchContext*>(7),reinterpret_cast<MeleeWebMatchContextSnapshot*>(8),reinterpret_cast<MeleeWebSourceMemoryPayloadSnapshot*>(9),s.get(),4};
    assert(gameplay_cpu_commit(o)==R::Ok);assert(writes==6&&order==6);
    assert(fighter_fields==3&&private_slots==3&&audio_value==3&&flow_value==3&&bootstrap_value==3&&context_value==3&&static_value==3);
    puts("Actual fixed commit coordinator: eight admission refusals write zero; ordered successful chain preserves later predicates");
}
