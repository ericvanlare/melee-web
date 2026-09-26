#pragma once
#include "gameplay_collision.h"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#include "dat_menu_support.hpp"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

struct StartMeleeData;
namespace melee_web {
using RuntimeFiles = std::map<std::string, std::vector<uint8_t>, std::less<>>;
// Source lbFileGetFullName treats a trailing-dot basename as a language
// selector. This bridge applies its setting/saved-language rules and then
// requires the selected archive to exist; it never changes source identity
// by selecting another available locale.
[[nodiscard]] std::string_view melee_web_runtime_file_name(
    const RuntimeFiles&, std::string_view authored_name,
    DatMenuSupportLanguage setting_language = DatMenuSupportLanguage::English,
    DatMenuSupportLanguage saved_language = DatMenuSupportLanguage::English);
class RuntimeArchiveCache;
class DatArchive;
enum class GameplayWorldPurpose { Match, Results };
enum class GameplayWorldConstruction { Immediate, Deferred, SourceOrdered };
// Original FTKind and GrKind values, never CSS/SSS grid indices. Defaults keep
// existing Mario/FD probes scoped to their original fixture.
struct GameplayWorldSelection {
    unsigned player_count=2;
    std::array<unsigned,4> fighter_kinds{0,0,0,0};
    std::array<unsigned,4> costume_indices{0,0,0,0};
    int ground_kind=37;
    GameplayWorldPurpose purpose=GameplayWorldPurpose::Match;
    // Full VS startup creates the player/camera context at the source scene
    // entry boundary, before later shared-world owners are hydrated.
    bool begin_source_match=false;
    uint32_t source_camera_subjects=0;
    uint32_t source_random_seed=0;
    // Complete menu payload retained by the match session. SourceOrdered
    // startup applies fn_8016DCC0 between the original camera and refraction
    // boundaries, before it loads effects and stage-owned services.
    const StartMeleeData* source_start_data=nullptr;
    std::array<MeleeWebPlayerSettings,4> source_players{};
};
// Shared by the browser and source regression harness. Owns one original SDK
// world and its assets. Match/render contexts must close before this owner.
class GameplayWorld {
public:
    explicit GameplayWorld(const RuntimeFiles&);
    GameplayWorld(const RuntimeFiles&, const GameplayWorldSelection&);
    GameplayWorld(const RuntimeFiles&, const GameplayWorldSelection&,
                  GameplayWorldConstruction);
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
    MeleeWebMatchContext* take_match_context();
    MeleeWebRender* take_render_context();
    void install_result_demo(unsigned fighter_kind, std::shared_ptr<const DatArchive>);
    void verify_result_source_loads() const;
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
