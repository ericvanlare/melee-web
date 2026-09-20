#include "gameplay_action_store.hpp"
#include <cstdio>
#include <set>
namespace melee_web {
namespace { void require(bool v, const char* m) { if (!v) throw DatError(m); } }
struct GameplayActionStore::Clip {
    DatSelectedAction selected;
    /* The original x5A4/x5A8 identity points into the source row table. Keep
     * that allocation alive when a victim outlives the thrower. */
    std::shared_ptr<MeleeWebNativeActionRows> identity_rows;
    /* The selected row's xC command pointer is also borrowed from the source
     * command graph and is installed in the destination fighter state. */
    std::shared_ptr<DatCommands> command_owner;
    std::unique_ptr<MeleeWebNativeClip, decltype(&melee_web_native_clip_destroy)> native;
    explicit Clip(DatSelectedAction value) : selected(std::move(value)), native(nullptr, melee_web_native_clip_destroy)
    {
        if (!selected.animation) return;
        const auto& a = *selected.animation;
        std::vector<MeleeWebAnimationTrack> tracks;
        for (const auto& t : a.tracks) tracks.push_back({t.bytes.data(), t.bytes.size(), t.start_frame,
                                                       t.type, t.value_format, t.slope_format});
        native.reset(melee_web_native_clip_create(a.tree_type, a.flags, a.end_frame,
            a.node_counts.data(), a.node_counts.size(), tracks.data(), tracks.size()));
        require(bool(native), "Native FigaTree creation failed");
    }
};
GameplayActionStore::GameplayActionStore(std::shared_ptr<const DatArchive> archive, const FighterCostume& costume,
    std::span<const uint8_t> container)
    : runtime_(std::make_shared<const DatFighterRuntime>(archive, costume)), store_(runtime_, container),
      rows_(nullptr, melee_web_action_rows_destroy)
{
    const bool mario = costume.fighter_kind == 0 || costume.fighter_kind == 21;
    const bool fox_family = costume.fighter_kind == 1 || costume.fighter_kind == 22;
    const bool mars = costume.fighter_kind == 18 || costume.fighter_kind == 26;
    // Exact source FTKIND_LINK/FTKIND_CLINK values from the pinned ft forward
    // enum. Keep this small C++ boundary independent of the source include
    // path used by the standalone Wasm fixture compiler.
    const bool link_family = costume.fighter_kind == 6 || costume.fighter_kind == 20;
    const bool purin = costume.fighter_kind == 15;
    const bool donkey = costume.fighter_kind == 3;
    const bool luigi = costume.fighter_kind == 17;
    const bool pikachu_family = costume.fighter_kind == 12 || costume.fighter_kind == 23;
    const bool captain = costume.fighter_kind == 2;
    const bool ganon = costume.fighter_kind == 25;
    require(mario || fox_family || mars || link_family || luigi || pikachu_family || purin || donkey || captain || ganon, "Native action store has no checked fighter command schema for this kind");
    std::vector<DatCommandRoot> roots;
    // Explicit source ftCo submotion groups. This certifies command operand
    // graphs only, not readiness of every original world service they invoke.
    command_motions_ = {0,1,2,3,6};
    auto group=[&](uint32_t first,uint32_t last){for(uint32_t id=first;id<=last;++id)command_motions_.insert(id);};
    group(7,31);group(34,77);                     // locomotion through grounded/aerial attacks and landings
    // Link-family bombs enter the common light-item pickup/throw actions.
    // Opponents may also catch and throw those same original items.
    group(78,88);group(96,103);                  // light pickup, normal/air throws and smash throws
    for(auto absent:{54U,56U,61U,63U,65U})command_motions_.erase(absent);
    group(165,181);group(183,204);                // damage, knockdown and techs
    command_motions_.insert(205);group(209,217);group(219,228);                // ledge actions
    command_motions_.insert(238);                // original EntryStart; Mario script is END
    group(239,240);                              // ftCo_SM_AppealSR/SL action rows; states 264/265 select these rows
    group(242,258);group(262,265);                // grab, pummel, throws and Mario capture reactions
    // Donkey's cargo moves drive the victim's original Shouldered/ThrownF*
    // actions. These source command graphs belong to every possible victim.
    group(267,275);
    group(286,291);                              // shield-break knockdown
    if (mario) group(295,302);                   // Mario/Dr. Mario specials; taunts use common rows 239/240 above
    else if (link_family) group(295,313);        // Link-family action tables end at 313
    // The Captain-family range is source-complete through the final authored
    // action. Captain rows 295/297 include command opcodes 45/42 whose
    // original consumers require live sword/parasol item services; the
    // shared command readiness guard intentionally remains explicit there.
    else if (captain || ganon) group(295,costume.motion_count-1);
    else if (luigi) group(295,costume.motion_count-1); // Luigi's authored special rows end at 311.
    else if (pikachu_family) group(295,costume.motion_count-1); // Both authored tables end at 319.
    else if (purin) group(295,costume.motion_count-1); // Purin five aerial jumps and original specials.
    else if (donkey) group(295,costume.motion_count-1); // Heavy carry, cargo throws and Donkey specials.
    else group(295,326);                         // Fox/Falco/Marth/Roy source special command rows
    for (auto choice : runtime_->wait_choices()) command_motions_.insert(choice.motion_id);
    for (auto choice : runtime_->squat_wait_choices()) command_motions_.insert(choice.motion_id);
    for (auto id : command_motions_) {
        const auto& action = runtime_->action(id);
        if (!action.command_offset) continue;
        const auto selected = store_.select_native_action(id);
        const float end_frame = selected.animation ? selected.animation->end_frame : 0.0f;
        roots.push_back({*action.command_offset, end_frame,
                         static_cast<uint8_t>((action.motion_flags & (1U << 30)) != 0)});
    }
    if (!roots.empty()) commands_ = std::make_shared<DatCommands>(archive, roots);
    std::vector<MeleeWebActionRow> rows;
    std::vector<MeleeWebWaitChoice> waits;
    for (const auto& a : runtime_->actions()) {
        void* command = nullptr;
        if (a.command_offset) command = command_motions_.contains(a.motion_id) ? commands_->at(*a.command_offset) : melee_web_commands_unsupported();
        rows.push_back({a.symbol.c_str(), a.container_offset, a.archive_bytes, a.motion_flags, command,
                       {a.blend_dynamics[0], a.blend_dynamics[1]}});
    }
    for (auto w : runtime_->wait_choices()) waits.push_back({w.motion_id, w.weight});
    rows_.reset(melee_web_action_rows_create(rows.data(), rows.size(), waits.data(), waits.size()),
                melee_web_action_rows_destroy);
    require(bool(rows_), "Native action rows allocation failed");
}
GameplayActionStore::~GameplayActionStore() { unbind(); }
void GameplayActionStore::bind(Fighter* fighter)
{
    require(!fighter_, "Action store is already bound");
    require(melee_web_action_bind(fighter, this, select, transfer), "Fighter action binding is unavailable"); fighter_ = fighter;
}
void GameplayActionStore::unbind()
{ if (fighter_) { melee_web_action_unbind(fighter_); fighter_ = nullptr; active_ = {}; motions_.fill(UINT32_MAX); } }
void* GameplayActionStore::action_rows() const { return melee_web_action_rows(rows_.get()); }
void* GameplayActionStore::blend_rows() const { return melee_web_action_blends(rows_.get()); }
void* GameplayActionStore::wait_choices() const { return melee_web_action_waits(rows_.get()); }
uint32_t GameplayActionStore::selected_motion(unsigned slot) const { return motions_.at(slot); }
int GameplayActionStore::select(void* context, int motion, unsigned slot, FigaTree** tree, void** identity,
    char* error, size_t error_size)
{
    try {
        auto& self = *static_cast<GameplayActionStore*>(context);
        return self.select_from(self, motion, slot, tree, identity, error, error_size);
    } catch (const std::exception& e) {
        if (error && error_size) std::snprintf(error, error_size, "%s", e.what()); return 0;
    }
}
int GameplayActionStore::transfer(void* destination_context, void* source_context,
    Fighter* destination, Fighter* source, int motion, unsigned slot, FigaTree** tree,
    void** identity, char* error, size_t error_size)
{
    try {
        auto& target = *static_cast<GameplayActionStore*>(destination_context);
        auto& origin = *static_cast<GameplayActionStore*>(source_context);
        require(target.fighter_ == destination, "Destination action store binding is stale");
        require(origin.fighter_ == source, "Source action store binding is stale");
        require(destination != source, "Cross-fighter transfer received identical fighters");
        return target.select_from(origin, motion, slot, tree, identity, error, error_size);
    } catch (const std::exception& e) {
        if (error && error_size) std::snprintf(error, error_size, "%s", e.what()); return 0;
    }
}
int GameplayActionStore::select_from(GameplayActionStore& source, int motion, unsigned slot,
    FigaTree** tree, void** identity, char* error, size_t error_size)
{
    require(motion >= 0 && slot < 2, "Action selection is out of range");
    require(fighter_, "Destination action store is unbound");
    require(bool(source.rows_), "Source action rows are unavailable");
    const auto& row = source.runtime_->action(uint32_t(motion));
    auto selected = source.store_.select_native_action(uint32_t(motion));
    std::shared_ptr<Clip> clip;
    for (const auto& active : active_) if (active &&
        active->identity_rows.get() == source.rows_.get() &&
        active->selected.action.container_offset == row.container_offset &&
        active->selected.action.archive_bytes == row.archive_bytes && active->selected.action.symbol == row.symbol) clip = active;
    if (!clip) {
        clip = std::make_shared<Clip>(std::move(selected));
        clip->identity_rows = source.rows_;
        clip->command_owner = source.commands_;
    }
    active_[slot] = clip; motions_[slot] = uint32_t(motion);
    *tree = melee_web_native_clip_tree(clip->native.get());
    /* x5A4/x5A8 are source-row identities in ftData_80085CD8. */
    *identity = melee_web_action_identity(source.rows_.get(), uint32_t(motion));
    if (error && error_size) error[0] = 0;
    return 1;
}
}
