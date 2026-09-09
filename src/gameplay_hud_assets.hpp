#pragma once
#include "gameplay_world.hpp"
#include <memory>

namespace melee_web {
// Owns typed interface archive graphs across original ifAll startup/teardown.
// Construct inside a live SDK world. Release after its scene heap is destroyed.
class GameplayHudAssets {
public:
    explicit GameplayHudAssets(const RuntimeFiles&);
    ~GameplayHudAssets();
    GameplayHudAssets(const GameplayHudAssets&) = delete;
    GameplayHudAssets& operator=(const GameplayHudAssets&) = delete;
    void verify() const;
    void close();
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
