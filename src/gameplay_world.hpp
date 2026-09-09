#pragma once
#include "gameplay_collision.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

struct StartMeleeData;
namespace melee_web {
using RuntimeFiles = std::map<std::string, std::vector<uint8_t>, std::less<>>;
class RuntimeArchiveCache;
enum class GameplayWorldConstruction { Immediate, Deferred };
// Original FTKind and GrKind values, never CSS/SSS grid indices. Defaults keep
// existing Mario/FD probes scoped to their original fixture.
struct GameplayWorldSelection {
    std::array<unsigned,2> fighter_kinds{0,0};
    std::array<unsigned,2> costume_indices{0,0};
    int ground_kind=37;
};
// Shared by the browser and source regression harness. Owns one original SDK
// world and its assets. Match/render contexts must close before this owner.
class GameplayWorld {
public:
    explicit GameplayWorld(const RuntimeFiles&);
    GameplayWorld(const RuntimeFiles&, const GameplayWorldSelection&);
    GameplayWorld(const RuntimeFiles&, const GameplayWorldSelection&,
                  RuntimeArchiveCache&);
    GameplayWorld(const RuntimeFiles&, const GameplayWorldSelection&,
                  RuntimeArchiveCache&, GameplayWorldConstruction);
    ~GameplayWorld();
    GameplayWorld(const GameplayWorld&) = delete;
    GameplayWorld& operator=(const GameplayWorld&) = delete;
    void close();
    void enable_stage_visual();
    void enable_full_stage(bool defer_start = false);
    void end_stage();
    void initialize_match(const StartMeleeData&);
    MeleeWebCollision* collision() const;
    float floor_height(float x) const;
    std::array<float, 3> player_spawn(unsigned slot) const;
    uint32_t unresolved_fighter_fields() const;
    void verify_immutable_archives() const;
    bool advance_construction();
    bool construction_complete() const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
}
