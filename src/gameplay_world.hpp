#pragma once
#include "gameplay_collision.h"
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace melee_web {
using RuntimeFiles = std::map<std::string, std::vector<uint8_t>, std::less<>>;
// Shared by the browser and source regression harness. Owns one original SDK
// world and its assets. Match/render contexts must close before this owner.
class GameplayWorld {
public:
    explicit GameplayWorld(const RuntimeFiles&);
    ~GameplayWorld();
    GameplayWorld(const GameplayWorld&) = delete;
    GameplayWorld& operator=(const GameplayWorld&) = delete;
    void close();
    void enable_stage_visual();
    void enable_full_stage();
    void end_stage();
    MeleeWebCollision* collision() const;
    float floor_height(float x) const;
    uint32_t unresolved_fighter_fields() const;
    void verify_immutable_archives() const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
