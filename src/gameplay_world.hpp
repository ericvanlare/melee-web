#pragma once
#include "gameplay_collision.h"
#include "gameplay_match_context.h"
#include "gameplay_render.h"
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
#include "gameplay_stage_last.h"
#include "gameplay_source_files.h"
#endif
#include "dat_menu_support.hpp"
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <algorithm>
#include <utility>

struct StartMeleeData;
namespace melee_web {
// Logical archive bytes preserve the existing vector API. The independently
// read DVD tail is transport metadata, never appended to size()/archive hashes.
class RuntimeFileBytes : public std::vector<uint8_t> {
    using Base = std::vector<uint8_t>;
    std::array<uint8_t,31> transfer_tail_{};
    std::size_t transfer_tail_size_=0, transfer_logical_size_=0;
    void clear_transfer_tail() noexcept { transfer_tail_size_=transfer_logical_size_=0; }
public:
    using Base::Base;
    RuntimeFileBytes()=default;
    RuntimeFileBytes(const Base& bytes):Base(bytes){}
    RuntimeFileBytes(Base&& bytes):Base(std::move(bytes)){}
    RuntimeFileBytes(const RuntimeFileBytes&)=default;
    RuntimeFileBytes(RuntimeFileBytes&&) noexcept=default;
    RuntimeFileBytes& operator=(const RuntimeFileBytes&)=default;
    RuntimeFileBytes& operator=(RuntimeFileBytes&&) noexcept=default;
    RuntimeFileBytes& operator=(const Base& bytes){Base::operator=(bytes);clear_transfer_tail();return *this;}
    RuntimeFileBytes& operator=(Base&& bytes){Base::operator=(std::move(bytes));clear_transfer_tail();return *this;}
    RuntimeFileBytes& operator=(std::initializer_list<uint8_t> bytes){Base::operator=(bytes);clear_transfer_tail();return *this;}
    void set_transfer_tail(std::span<const uint8_t> tail){
        const auto expected=(32-size()%32)%32;
        if(!tail.empty()&&tail.size()!=expected)
            throw std::runtime_error("Runtime file DVD tail does not match its logical extent");
        if(tail.size()>transfer_tail_.size())
            throw std::runtime_error("Runtime file DVD tail exceeds its aligned extent");
        std::copy(tail.begin(),tail.end(),transfer_tail_.begin());
        transfer_tail_size_=tail.size();transfer_logical_size_=size();
    }
    [[nodiscard]] std::span<const uint8_t> transfer_tail()const{
        if(transfer_tail_size_&&size()!=transfer_logical_size_)
            throw std::runtime_error("Runtime file logical extent changed after DVD tail admission");
        return {transfer_tail_.data(),transfer_tail_size_};
    }
};
using RuntimeFiles = std::map<std::string, RuntimeFileBytes, std::less<>>;
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
    // Borrowed only during construction, including SourceOrdered: restore the
    // menu PAD history before fn_8016D8AC selects the held-A startup form.
    const MeleeWebPadState* source_initial_input=nullptr;
    // Opening demos retain their authored four-CPU/99-stock setup and
    // gm_80183218 source callback. This never applies to public VS matches.
    bool opening_demo=false;
    // Stable layout across private diagnostic libraries and ordinary consumers.
    // Non-diagnostic builds reject this request rather than admitting Stadium.
    bool stadium_diagnostic=false;
    // Original Sudden Death scene setup uses source-indexed survivors, which
    // may be sparse after the authored winner filter.
    bool sudden_death=false;
    std::array<MeleeWebPlayerSettings,4> source_players{};
    // Opt-in for first-transform form preparation; false keeps the existing
    // Stadium idle/paired diagnostic asset path unchanged.
    bool stadium_first_transform_diagnostic=false; // existing first-Fire policy
    bool stadium_first_water_diagnostic=false; // separate first Water9, never a Fire->Water cycle
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
    void close(const std::function<void()>& after_effect_runtime_end = {});
    // Refuses source pump, tick, or draw work once the owned Stadium source
    // region has begun retirement. A world without a stage owner is allowed.
    void ensure_source_work_allowed() const;
    // Completes pending original DVD/DevCom requests for this world's exact
    // RuntimeFiles owner. Match ticks call this before advancing source state.
    void pump_source_files();
    // Refuses teardown while a source callback is still owned by the world.
    void ensure_source_files_quiescent() const;
    void enable_stage_visual();
    void enable_full_stage(bool defer_start = false);
    void end_stage();
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
    void prepare_stadium_ready(MeleeWebMatchContext*);
    void cancel_prepared_stadium_ready();
    // Exercises the same read-only Toy owner preflight used immediately before
    // the original reset, without beginning world teardown.
    bool diagnostic_toy_owner_preflight() const;
    void diagnostic_stadium_go_alignment_arm(int expected_branch,char*,size_t);
    bool diagnostic_stadium_go_alignment_snapshot(
        MeleeWebStadiumGoAlignmentSnapshot*,char*,size_t) const;
    bool diagnostic_stadium_idle_snapshot(
        MeleeWebStadiumIdleSnapshot*,char*,size_t) const;
    // Read-only retirement admission; call before any flow/HUD/quakes mutation.
    bool diagnostic_stadium_first_transform_schedule(uint32_t,uint32_t,uint32_t,char*,size_t);
    bool diagnostic_stadium_first_transform_completion_status(
        MeleeWebSourceFileRecordedCompletionStatus*,char*,size_t) const;
    bool diagnostic_stadium_first_transform_can_end(char*,size_t) const;
    // Reads the checked live first-form graph, or the sealed terminal receipt
    // after end_stage and before the World is destroyed.
    bool diagnostic_stadium_first_transform_snapshot(
        MeleeWebStadiumFirstTransformJournal*,char*,size_t) const;
    bool diagnostic_stadium_setup_snapshot(
        MeleeWebStadiumSetupSnapshot*,char*,size_t) const;
#endif
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
