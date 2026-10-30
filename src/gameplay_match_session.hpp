#pragma once
#include "gameplay_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_context.h"
#include "gameplay_match_flow.h"
#include <memory>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance.h"
#endif
namespace melee_web {
enum class GameplayMatchConstruction { Immediate, Deferred };
/* Private diagnostic named-owner bridge. No value snapshot or restore
 * authority: callers must re-read this tuple from the live session before
 * using opaque component handles, then use each owner's admission API. */
struct GameplayMatchCpuOwners {
    const void* storage = nullptr;
    MeleeWebMatchContext* context = nullptr;
    MeleeWebMatchFlow* flow = nullptr;
    MeleeWebAudio* audio = nullptr;
    uint64_t world = 0;
    uint64_t context_epoch = 0;
    uint32_t frame = 0;
};
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
enum class GameplayMatchDiagnostic { StadiumReady, StadiumFirstFireReady, StadiumFirstWaterReady };
// Pointer-free result of one opt-in Water collision-query component at an
// explicitly selected, completed source tick.
struct GameplayStadiumWaterCollisionProbe {
    struct State {
        uint64_t world_generation=0,world_ticks=0;
        uint32_t source_frame=0,random_seed=0;
        uint32_t journal_armed=0,selected_form=0,journal_failed=0,event_count=0;
        int32_t map2_gate=0,countdown=0,phase=0,form=0,history_e0=0,history_e2=0,pending=0;
        int32_t display_mode=0,display_e6=0,display_e8=0,display_previous_mode=0;
        int32_t display_selected_player=0,display_f2=0;
        uint32_t buffer_uses=0,display_flags=0,scalar_sample_valid=0;
    };
    uint64_t world_generation=0,source_tick=0;
    State state_before{},state_after{};
    MeleeWebCollisionReadiness readiness_before{},readiness_after{};
    int32_t line_index=-1;
    float chain_min_x=0,chain_max_x=0,hit_x=0,query_y=0,left_miss_x=0,right_miss_x=0;
    MeleeWebCollisionLineResult line{};
    MeleeWebCollisionFloorResult hit{},left_miss{},right_miss{};
};
#endif
// Shared source match lifecycle. The confirmed menu payload supplies player
// identity and source RNG; scene resources close before returning to a menu.
class GameplayMatchSession {
public:
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&);
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         const MeleeWebPadState&);
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&);
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&,GameplayMatchConstruction);
    // The reference snapshot must outlive deferred construction.
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&,GameplayMatchConstruction,
                         const MeleeWebPadState&);
    // Claims the exact host-owned Sudden Death continuation and borrows its
    // retained VS mode lease until this source world has fully closed.
    GameplayMatchSession(const RuntimeFiles&,MeleeWebMenuHost*,
                         const MeleeWebMenuMatchContinuation&,
                         RuntimeArchiveCache&,GameplayMatchConstruction,
                         const MeleeWebPadState&);
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&,GameplayMatchConstruction,
                         const MeleeWebPadState&,GameplayMatchDiagnostic);
#endif
    ~GameplayMatchSession();
    GameplayMatchSession(const GameplayMatchSession&)=delete;
    GameplayMatchSession& operator=(const GameplayMatchSession&)=delete;
    void tick(const PADStatus[4]);
    void draw();
    // Requires source completion; retires flow and publishes original terminal
    // data before capturing live RNG/PAD, closing the world and handing off.
    void finish_sudden_death(MeleeWebMenuMatchContinuation& results);
    // Completed ordinary VS only: canonical publication then live RNG/PAD
    // capture, then teardown. Typed menu routing belongs to the closed host.
    void finish_vs(uint32_t& seed, uint8_t input[MELEE_WEB_PAD_STATE_BYTES]);
    int outcome(int& winner) const;
    bool ready() const;
    bool ending() const;
    bool complete() const;
    bool opening_demo() const;
    bool sudden_death() const;
    bool paused() const;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    bool diagnostic_pause_observation(MeleeWebMatchPauseObservation&) const;
#endif
    uint32_t source_frames() const;
    int hud_damage(unsigned player) const;
    uint32_t random_seed() const;
    // Capture final source RNG and the complete raw PAD bank before closing a
    // Sudden Death world and restoring its host-owned global pointers.
    void capture_handoff(uint32_t& seed,
                         uint8_t input[MELEE_WEB_PAD_STATE_BYTES]) const;
    int fighter_kind(unsigned index) const;
    const StartMeleeData& start_data() const;
    // Copied menu selection only; safe during deferred construction and never
    // traverses live fighters or serializes player/save data.
    const StartMeleeData* diagnostic_start_data() const noexcept;
    MeleeWebMatchStats player_stats(unsigned index) const;
    MeleeWebAudio* audio() const;
    [[nodiscard]] GameplayMatchCpuOwners diagnostic_cpu_owners() const noexcept;
    // Exact source RuntimeFiles quiescence for this live match world only.
    // Does not establish GPU/Asyncify idleness or network confirmation.
    [[nodiscard]] bool diagnostic_source_files_quiescent() const noexcept;
    MeleeWebMatchContextRangeResult diagnostic_match_context_range(
        MeleeWebMatchContextRange*,char*,size_t) const;
    [[nodiscard]] GameplayWorldStageArenaInventory diagnostic_stage_arena_allocations(
        std::size_t first, std::size_t count,
        bool (*visitor)(const NativeDatAllocationRange&, void*) noexcept,
        void* context) const noexcept;
    bool advance_construction();
    bool construction_complete() const;
    void close();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    bool diagnostic_stadium_scene_rng_snapshot(
        MeleeWebMatchSceneRngSnapshot*,char*,size_t) const;
    bool diagnostic_stadium_pad_queue_observation(
        MeleeWebStadiumPadQueueObservation*,char*,size_t) const;
    // Test-only access to the world's pure Toy owner/scope preflight. It does
    // not mutate state or execute original cleanup.
    bool diagnostic_toy_owner_preflight() const;
    void diagnostic_stadium_go_alignment_arm(int expected_branch,char*,size_t);
    bool diagnostic_stadium_go_alignment_snapshot(
        MeleeWebStadiumGoAlignmentSnapshot*,char*,size_t) const;
    bool diagnostic_stadium_first_transform_snapshot(
        MeleeWebStadiumFirstTransformJournal*,char*,size_t) const;
    bool diagnostic_stadium_first_transform_completion_status(
        MeleeWebSourceFileRecordedCompletionStatus*,char*,size_t) const;
    bool diagnostic_stadium_first_transform_schedule(uint32_t,uint32_t,uint32_t,char*,size_t);
    bool diagnostic_stadium_water_collision_probe(
        uint64_t,GameplayStadiumWaterCollisionProbe*,char*,size_t) const;
    bool diagnostic_stadium_idle_snapshot(
        MeleeWebStadiumIdleSnapshot*,char*,size_t) const;
    bool diagnostic_stadium_setup_snapshot(
        MeleeWebStadiumSetupSnapshot*,char*,size_t) const;
#endif
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
    MeleeWebPipelineSourceContext provenance_context() const;
#endif
private:
#if defined(MELEE_WEB_CPU_ENDPOINT_TEST)
    friend struct GameplayCpuEndpointTestFixture;
    GameplayMatchSession() noexcept;
#endif
    struct Storage;
    std::unique_ptr<Storage> storage_;
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    MeleeWebMatchSceneRngSnapshot scene_rng_retained_{};
    MeleeWebStadiumFirstTransformJournal first_transform_retained_{};
    unsigned first_transform_selected_form_=0;
    MeleeWebSourceFileRecordedCompletionStatus first_completion_retained_{};
    bool scene_rng_entry_complete_=false;
#endif
};
}
