#pragma once
#include "gameplay_action_store.h"
#include "gameplay_audio.h"
#include "gameplay_bootstrap.h"
#include "gameplay_match_context.h"
#include "gameplay_match_flow.h"
#include "gameplay_source_memory_runtime.h"
#include "gameplay_cpu_statics.hpp"
#include "slippi_profile/lobj_presentation_cache.h"
#include "slippi_profile/tobj_presentation_cache.h"
#include "slippi_profile/particle_presentation.h"
#include <cstdint>
namespace melee_web {
/* Fixed named domains, not a callback registry. All handles are capsule-owned;
 * the live session tuple and pure runtime boundary must be checked by caller.
 * No supplied pointer constitutes independent provenance authority. */
struct GameplayCpuCommitOwners {
    MeleeWebActionInterval* actions{};
    MeleeWebAudio* audio{};
    const MeleeWebAudioSnapshot* audio_snapshot{};
    MeleeWebMatchFlow* flow{};
    const MeleeWebMatchFlowCpuSnapshot* flow_snapshot{};
    const MeleeWebGameplayBootstrapSnapshot* bootstrap{};
    MeleeWebMatchContext* context{};
    const MeleeWebMatchContextSnapshot* context_snapshot{};
    const MeleeWebSourceMemoryPayloadSnapshot* payload{};
    GameplayCpuStatics* statics{};
    std::uint64_t effect_token{};
    SlippiLObjAnchor lobj_anchor{};
    SlippiTObjAnchor tobj_anchor{};
    SlippiParticlePresentationAnchor particle_anchor{};
};
enum class GameplayCpuCommitResult : std::uint32_t {
    Ok=0, MissingOwner, Effect, Action, Audio, Flow, Bootstrap, Context, Payload,
    Statics, UnexpectedWriterFailure, LObj, TObj, Particle
};
/* Caller must perform no callback/await/source work between admit and commit.
 * This method itself repeats every admission, then prepares the statics last.
 * Every normal refusal happens before the first source-state write. */
GameplayCpuCommitResult gameplay_cpu_commit(const GameplayCpuCommitOwners&) noexcept;
}
