#pragma once
#include "gameplay_world.hpp"
#include "gameplay_audio.h"
#include "gameplay_pad_state.h"
#include "gameplay_compat.h"
#include <dolphin/pad.h>

struct MeleeWebMenuHost;
namespace melee_web {
class GameplayPrizeSession {
public:
    GameplayPrizeSession(const RuntimeFiles&, MeleeWebMenuHost*, uint32_t,
                         const MeleeWebPadState&);
    ~GameplayPrizeSession();
    GameplayPrizeSession(const GameplayPrizeSession&) = delete;
    GameplayPrizeSession& operator=(const GameplayPrizeSession&) = delete;
    void tick(const PADStatus[4]);
    void draw();
    void exit_scene();
    int requested() const;
    uint32_t random_seed() const;
    uint32_t source_frames() const;
    MeleeWebAudio* audio() const;
    void close();
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
