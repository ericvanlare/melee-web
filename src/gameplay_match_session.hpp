#pragma once
#include "gameplay_world.hpp"
#include "gameplay_menu_host.h"
#include "gameplay_match_context.h"
#include <memory>
namespace melee_web {
// Shared source match lifecycle. The confirmed menu payload supplies player
// identity and source RNG; scene resources close before returning to a menu.
class GameplayMatchSession {
public:
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&);
    GameplayMatchSession(const RuntimeFiles&,const MeleeWebMenuMatchSelection&,
                         RuntimeArchiveCache&);
    ~GameplayMatchSession();
    GameplayMatchSession(const GameplayMatchSession&)=delete;
    GameplayMatchSession& operator=(const GameplayMatchSession&)=delete;
    void tick(const PADStatus[4]);
    void draw();
    int outcome(int& winner) const;
    bool ready() const;
    bool ending() const;
    bool complete() const;
    bool paused() const;
    uint32_t source_frames() const;
    int hud_damage(unsigned player) const;
    uint32_t random_seed() const;
    MeleeWebMatchStats player_stats(unsigned index) const;
    MeleeWebAudio* audio() const;
    void close();
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
