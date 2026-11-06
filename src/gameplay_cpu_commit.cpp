#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_interval.h"
#include <cstdio>
#include <memory>
namespace melee_web {
GameplayCpuCommitResult gameplay_cpu_commit(const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.actions || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token || !o.texture_bounds || !o.texture_world) return R::MissingOwner;
    std::unique_ptr<MeleeWebTextureBoundsPrepared,
        decltype(&melee_web_texture_bounds_prepared_destroy)>
        texture(nullptr,melee_web_texture_bounds_prepared_destroy);
    texture.reset(melee_web_texture_bounds_prepare(o.texture_bounds,o.texture_world));
    if(!texture)return R::TextureBounds;
    if (!slippi_lobj_anchor_admit(o.lobj_anchor)) return R::LObj;
    if (!slippi_tobj_anchor_admit(o.tobj_anchor)) return R::TObj;
    if (!slippi_particle_presentation_anchor_admit(o.particle_anchor)) return R::Particle;
    if (!melee_web_cpu_interval_admit(o.effect_token)) return R::Effect;
    if (melee_web_action_interval_admit(o.actions)!=1) return R::Action;
    if (melee_web_audio_snapshot_admit(o.audio,o.audio_snapshot)!=1) return R::Audio;
    if (melee_web_match_flow_cpu_snapshot_admit(o.flow,o.flow_snapshot,nullptr,0)!=1) return R::Flow;
    if (melee_web_gameplay_bootstrap_snapshot_admit(o.bootstrap,nullptr,0)!=1) return R::Bootstrap;
    if (melee_web_match_context_snapshot_admit(o.context,o.context_snapshot,nullptr,0)!=1) return R::Context;
    if (melee_web_source_memory_payload_snapshot_admit(o.payload)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
        return R::Payload;
    if (!o.statics->prepare_commit()) return R::Statics;
    if(!melee_web_texture_bounds_admit(texture.get(),o.texture_world))return R::TextureBounds;
    /* There is no yield/callback/allocation after the final admission. See the
     * fixed read/write dependency table: each remaining writer's predicates
     * concern owner/lifetime/topology and are unchanged by earlier writes.
     * ActionStore must precede Fighter payload: its admission reads final
     * Fighter clip fields; its writer modifies only private slots/cache. */
    if (melee_web_action_interval_restore(o.actions)!=1) return R::UnexpectedWriterFailure;
    if (melee_web_audio_snapshot_restore(o.audio,o.audio_snapshot)!=1) return R::UnexpectedWriterFailure;
    if (melee_web_match_flow_cpu_snapshot_restore(o.flow,o.flow_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_gameplay_bootstrap_snapshot_restore(o.bootstrap,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_match_context_snapshot_restore(o.context,o.context_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_source_memory_payload_snapshot_restore(o.payload)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
        return R::UnexpectedWriterFailure;
    melee_web_texture_bounds_commit_prevalidated(texture.get());
    if (!o.statics->commit_prevalidated()) return R::UnexpectedWriterFailure;
    return R::Ok;
}
GameplayCpuCommitResult gameplay_cpu_commit_corrected(const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.actions || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token || !o.texture_bounds || !o.texture_world) return R::MissingOwner;
    std::unique_ptr<MeleeWebTextureBoundsPrepared,
        decltype(&melee_web_texture_bounds_prepared_destroy)>
        texture(nullptr,melee_web_texture_bounds_prepared_destroy);
    texture.reset(melee_web_texture_bounds_prepare(o.texture_bounds,o.texture_world));
    if(!texture)return R::TextureBounds;
    if (!slippi_lobj_anchor_admit(o.lobj_anchor)) return R::LObj;
    if (!slippi_tobj_anchor_admit(o.tobj_anchor)) return R::TObj;
    if (!slippi_particle_presentation_anchor_admit(o.particle_anchor)) return R::Particle;
    if (!melee_web_cpu_interval_admit(o.effect_token)) return R::Effect;
    if (melee_web_action_interval_admit_corrected(o.actions)!=1) return R::Action;
    if (melee_web_audio_snapshot_admit(o.audio,o.audio_snapshot)!=1) return R::Audio;
    if (melee_web_match_flow_cpu_snapshot_admit(o.flow,o.flow_snapshot,nullptr,0)!=1) return R::Flow;
    if (melee_web_gameplay_bootstrap_snapshot_admit(o.bootstrap,nullptr,0)!=1) return R::Bootstrap;
    if (melee_web_match_context_snapshot_admit(o.context,o.context_snapshot,nullptr,0)!=1) return R::Context;
    if (melee_web_source_memory_payload_snapshot_admit(o.payload)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
        return R::Payload;
    if (!o.statics->prepare_commit()) return R::Statics;
    if(!melee_web_texture_bounds_admit(texture.get(),o.texture_world))return R::TextureBounds;
    /* Match the original dependency order exactly. Corrected ActionStore
     * changes private slots/cache only; Fighter payload remains its owner. */
    if (melee_web_action_interval_restore_corrected(o.actions)!=1) return R::UnexpectedWriterFailure;
    if (melee_web_audio_snapshot_restore(o.audio,o.audio_snapshot)!=1) return R::UnexpectedWriterFailure;
    if (melee_web_match_flow_cpu_snapshot_restore(o.flow,o.flow_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_gameplay_bootstrap_snapshot_restore(o.bootstrap,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_match_context_snapshot_restore(o.context,o.context_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_source_memory_payload_snapshot_restore(o.payload)!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
        return R::UnexpectedWriterFailure;
    melee_web_texture_bounds_commit_prevalidated(texture.get());
    if (!o.statics->commit_prevalidated()) return R::UnexpectedWriterFailure;
    return R::Ok;
}

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
bool gameplay_cpu_history_audio_disjoint(const GameplayCpuCommitOwners& o) noexcept
{
    const auto begin=reinterpret_cast<std::uintptr_t>(o.arena_base);
    if(!begin||!o.arena_bytes||o.arena_bytes>UINTPTR_MAX-begin)return false;
    struct Arena {std::uintptr_t begin,end;};
    Arena arena{begin,begin+o.arena_bytes};
    const auto visitor=[](const MeleeWebAudioOwnedRange* row,void* arg)->int {
        if(!row||!row->address||!row->byte_extent)return 0;
        const auto& arena=*static_cast<const Arena*>(arg);
        const auto address=reinterpret_cast<std::uintptr_t>(row->address);
        return row->byte_extent<=UINTPTR_MAX-address&&
            (address>=arena.end||address+row->byte_extent<=arena.begin);
    };
    const auto generation=melee_web_audio_generation(o.audio);
    const auto scalar=melee_web_audio_cpu_storage(o.audio,generation);
    const MeleeWebAudioOwnedRange command{
        {},scalar.command_budget,sizeof(std::uint32_t),o.audio,generation};
    const MeleeWebAudioOwnedRange mode{
        {},scalar.sound_mode,sizeof(std::uint32_t),o.audio,generation};
    return generation&&visitor(&command,&arena)&&visitor(&mode,&arena)&&
        melee_web_audio_visit_owned_ranges(o.audio,generation,MELEE_WEB_AUDIO_OWNED_RANGE_MAX,
                                           visitor,&arena)==MELEE_WEB_AUDIO_RANGE_VISIT_COMPLETE;
}

#endif

GameplayCpuCommitResult gameplay_cpu_commit_history_pre(
    const GameplayCpuCommitOwners& o, char* refusal, std::size_t refusal_bytes) noexcept {
    using R=GameplayCpuCommitResult;
    const auto payload_refusal=[&](const char* leaf, unsigned status,
                                   const MeleeWebSourceMemoryHistory* changed=nullptr){
        if(refusal&&refusal_bytes){
            const auto used=std::snprintf(refusal,refusal_bytes,"leaf=%s status=%u",leaf,status);
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
            if(changed&&used>=0&&static_cast<std::size_t>(used)+1<refusal_bytes){
                refusal[used]=' ';
                melee_web_source_memory_history_describe_pre_change(
                    changed,refusal+used+1,refusal_bytes-static_cast<std::size_t>(used)-1);
            }
#else
            (void)used;(void)changed;
#endif
        }
        return R::Payload;
    };
    const auto texture_refusal=[&](const char* stage){
        if(refusal&&refusal_bytes)
            std::snprintf(refusal,refusal_bytes,"leaf=texture-bounds-%s status=0",stage);
        return R::TextureBounds;
    };
    if (!o.action_history || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token || !o.history_incarnation || !o.history_end_frame ||
        !o.texture_bounds || !o.texture_world || (bool(o.arena)!=bool(o.shadow)))
        return R::MissingOwner;

    std::unique_ptr<MeleeWebTextureBoundsPrepared,
        decltype(&melee_web_texture_bounds_prepared_destroy)>
        texture(nullptr,melee_web_texture_bounds_prepared_destroy);

#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
    std::unique_ptr<MeleeWebGameplayHeapHistoryPrepared,
        decltype(&melee_web_gameplay_heap_history_prepared_destroy)>
        arena(nullptr,melee_web_gameplay_heap_history_prepared_destroy);
    std::unique_ptr<MeleeWebSourceMemoryHistoryPrepared,
        decltype(&melee_web_source_memory_history_prepared_destroy)>
        shadow(nullptr,melee_web_source_memory_history_prepared_destroy);
    if(o.arena){
        const auto physical=melee_web_gameplay_heap_history_prepare(o.arena);
        arena.reset(physical.prepared);
        if(physical.status!=MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK||!arena)
            return payload_refusal("sdk-prepare",physical.status);
        const auto mirror=melee_web_source_memory_history_prepare_arena_coupled(
            o.shadow,arena.get());
        shadow.reset(mirror.prepared);
        if(mirror.status!=MELEE_WEB_SOURCE_MEMORY_HISTORY_OK||!shadow)
            return payload_refusal("shadow-prepare",mirror.status,
                mirror.status==MELEE_WEB_SOURCE_MEMORY_HISTORY_PRE_CHANGED?o.shadow:nullptr);
    }

#else
    if(o.arena||o.shadow)return R::MissingOwner;
#endif

    texture.reset(melee_web_texture_bounds_prepare(o.texture_bounds,o.texture_world));
    if(!texture)return texture_refusal("prepare");

    /* Allocate/select all prepared banks before the final all-leaf check. */
    if (!o.action_history->prepare_restore(o.history_incarnation)) return R::Action;
    if (!o.statics->history_prepare_commit(o.history_incarnation,o.history_end_frame))
        return R::Statics;

    if (!slippi_lobj_anchor_admit(o.lobj_anchor)) return R::LObj;
    if (!slippi_tobj_anchor_admit(o.tobj_anchor)) return R::TObj;
    if (!slippi_particle_presentation_anchor_admit(o.particle_anchor)) return R::Particle;
    if (!melee_web_cpu_interval_admit(o.effect_token)) return R::Effect;
    if (!melee_web_cpu_interval_motor_history_restore_admit(
            o.effect_token,o.history_incarnation,o.history_end_frame)) return R::Effect;
    if (!o.action_history->admit_restore(o.history_incarnation)) return R::Action;
    if (!melee_web_audio_snapshot_admit(o.audio,o.audio_snapshot)) return R::Audio;
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
    if(arena&&!gameplay_cpu_history_audio_disjoint(o))return R::Audio;
    if(arena&&!slippi_particle_presentation_captured_pools_admit(o.particle_anchor))return R::Particle;
#endif
    if (!melee_web_match_flow_cpu_snapshot_admit(o.flow,o.flow_snapshot,nullptr,0)) return R::Flow;
    if (!melee_web_gameplay_bootstrap_snapshot_admit(o.bootstrap,nullptr,0)) return R::Bootstrap;
    if (!melee_web_match_context_snapshot_admit(o.context,o.context_snapshot,nullptr,0)) return R::Context;
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
    if(arena){
        const auto physical=melee_web_gameplay_heap_history_admit(arena.get());
        if(physical!=MELEE_WEB_GAMEPLAY_HEAP_HISTORY_OK)
            return payload_refusal("sdk-admit",physical);
        const auto mirror=melee_web_source_memory_history_admit(shadow.get());
        if(mirror!=MELEE_WEB_SOURCE_MEMORY_HISTORY_OK)
            return payload_refusal("shadow-admit",mirror,
                mirror==MELEE_WEB_SOURCE_MEMORY_HISTORY_PRE_CHANGED?o.shadow:nullptr);
    }else
#endif
    {
        const auto payload=melee_web_source_memory_payload_snapshot_admit(o.payload);
        if(payload!=MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK)
            return payload_refusal("payload-admit",payload);
    }
    if (!o.statics->history_admit_commit(o.history_incarnation,o.history_end_frame))
        return R::Statics;
    if(!melee_web_texture_bounds_admit(texture.get(),o.texture_world))
        return texture_refusal("admit");

    /* No callbacks or allocations after final admission. Preserve the original
     * fixed dependency order; ActionStore changes only its private banks, and
     * static PRE bytes remain the final source writer. */
    if (!o.action_history->restore_pre(o.history_incarnation)) return R::UnexpectedWriterFailure;
    if (melee_web_audio_snapshot_restore(o.audio,o.audio_snapshot)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_match_flow_cpu_snapshot_restore(o.flow,o.flow_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_gameplay_bootstrap_snapshot_restore(o.bootstrap,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
    if (melee_web_match_context_snapshot_restore(o.context,o.context_snapshot,nullptr,0)!=1)
        return R::UnexpectedWriterFailure;
#if defined(MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC) && MELEE_WEB_MATCH_HISTORY_DIAGNOSTIC
    if(arena){
        melee_web_gameplay_heap_history_commit_prevalidated(arena.get());
        if(melee_web_source_memory_history_commit_prevalidated(shadow.get())!=MELEE_WEB_SOURCE_MEMORY_HISTORY_OK)
            return R::UnexpectedWriterFailure;
    }else
#endif
    if(melee_web_source_memory_payload_snapshot_restore(o.payload)!=
             MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) return R::UnexpectedWriterFailure;
    melee_web_texture_bounds_commit_prevalidated(texture.get());
    o.statics->history_commit_prevalidated();
    melee_web_cpu_interval_motor_history_restore_commit(
        o.effect_token,o.history_incarnation,o.history_end_frame);
    return R::Ok;
}
}
