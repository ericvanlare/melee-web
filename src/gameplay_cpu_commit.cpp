#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_interval.h"
namespace melee_web {
GameplayCpuCommitResult gameplay_cpu_commit(const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.actions || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token) return R::MissingOwner;
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
    if (!o.statics->commit_prevalidated()) return R::UnexpectedWriterFailure;
    return R::Ok;
}
GameplayCpuCommitResult gameplay_cpu_commit_corrected(const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.actions || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token) return R::MissingOwner;
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
    if (!o.statics->commit_prevalidated()) return R::UnexpectedWriterFailure;
    return R::Ok;
}

GameplayCpuCommitResult gameplay_cpu_commit_history_pre(
    const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.action_history || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token || !o.history_incarnation || !o.history_end_frame)
        return R::MissingOwner;

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
    if (!melee_web_match_flow_cpu_snapshot_admit(o.flow,o.flow_snapshot,nullptr,0)) return R::Flow;
    if (!melee_web_gameplay_bootstrap_snapshot_admit(o.bootstrap,nullptr,0)) return R::Bootstrap;
    if (!melee_web_match_context_snapshot_admit(o.context,o.context_snapshot,nullptr,0)) return R::Context;
    if (melee_web_source_memory_payload_snapshot_admit(o.payload)!=
        MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) return R::Payload;
    if (!o.statics->history_admit_commit(o.history_incarnation,o.history_end_frame))
        return R::Statics;

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
    if (melee_web_source_memory_payload_snapshot_restore(o.payload)!=
        MELEE_WEB_SOURCE_MEMORY_PAYLOAD_SNAPSHOT_OK) return R::UnexpectedWriterFailure;
    o.statics->history_commit_prevalidated();
    melee_web_cpu_interval_motor_history_restore_commit(
        o.effect_token,o.history_incarnation,o.history_end_frame);
    return R::Ok;
}
}
