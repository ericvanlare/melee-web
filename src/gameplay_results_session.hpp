#pragma once

#include "gameplay_world.hpp"
#include "gameplay_audio.h"
#include "gameplay_pad_state.h"
#include "gameplay_compat.h"
#include <dolphin/pad.h>

struct ResultsMatchInfo;

namespace melee_web {
// Owns the original Results scene and its content. The outer VS mode remains
// responsible for producing ResultsMatchInfo and committing its mode exit.
class GameplayResultsSession {
public:
    GameplayResultsSession(const RuntimeFiles&, const ResultsMatchInfo&,
                           uint32_t seed, const MeleeWebPadState&);
    ~GameplayResultsSession();
    GameplayResultsSession(const GameplayResultsSession&) = delete;
    GameplayResultsSession& operator=(const GameplayResultsSession&) = delete;
    void tick(const PADStatus[4]);
    void draw();
    // The enclosing mode must call this before its own OnExit while all
    // Results assets and the source world remain live.
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
