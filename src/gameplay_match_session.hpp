#pragma once
#include "gameplay_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_context.h"
#include <memory>
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
#include "pipeline_provenance.h"
#endif
namespace melee_web {
enum class GameplayMatchConstruction { Immediate, Deferred };
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
    ~GameplayMatchSession();
    GameplayMatchSession(const GameplayMatchSession&)=delete;
    GameplayMatchSession& operator=(const GameplayMatchSession&)=delete;
    void tick(const PADStatus[4]);
    void draw();
    // Requires source completion; retires flow and publishes original terminal
    // data before capturing live RNG/PAD, closing the world and handing off.
    void finish_sudden_death(MeleeWebMenuMatchContinuation& results);
    int outcome(int& winner) const;
    bool ready() const;
    bool ending() const;
    bool complete() const;
    bool opening_demo() const;
    bool sudden_death() const;
    bool paused() const;
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
    bool advance_construction();
    bool construction_complete() const;
    void close();
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
    MeleeWebPipelineSourceContext provenance_context() const;
#endif
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
