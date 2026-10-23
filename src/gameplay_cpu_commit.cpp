#include "gameplay_cpu_commit.hpp"
#include "gameplay_cpu_interval.h"
namespace melee_web {
GameplayCpuCommitResult gameplay_cpu_commit(const GameplayCpuCommitOwners& o) noexcept {
    using R=GameplayCpuCommitResult;
    if (!o.actions || !o.audio || !o.audio_snapshot || !o.flow || !o.flow_snapshot ||
        !o.bootstrap || !o.context || !o.context_snapshot || !o.payload || !o.statics ||
        !o.effect_token) return R::MissingOwner;
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
}
