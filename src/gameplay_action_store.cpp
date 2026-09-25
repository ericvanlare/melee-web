#include "gameplay_action_store.hpp"
#include <algorithm>
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
    std::span<const uint8_t> container, std::shared_ptr<const DatArchive> result_motion,
    std::shared_ptr<const DatArchive> nana_popo_archive,
    const FighterCostume* nana_popo_identity,
    std::span<const uint8_t> nana_popo_container)
    : runtime_(std::make_shared<const DatFighterRuntime>(archive, costume)), store_(runtime_, container),
      rows_(nullptr, melee_web_action_rows_destroy), result_motion_(std::move(result_motion))
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
    const bool koopa = costume.fighter_kind == 5;
    const bool ness = costume.fighter_kind == 8;
    const bool peach = costume.fighter_kind == 9;
    const bool mewtwo = costume.fighter_kind == 16;
    const bool luigi = costume.fighter_kind == 17;
    const bool pikachu_family = costume.fighter_kind == 12 || costume.fighter_kind == 23;
    const bool captain = costume.fighter_kind == 2;
    const bool ganon = costume.fighter_kind == 25;
    const bool gamewatch = costume.fighter_kind == 24;
    const bool kirby = costume.fighter_kind == 4;
    const bool ice_climber = costume.fighter_kind == 10 || costume.fighter_kind == 11;
    const bool samus = costume.fighter_kind == 13;
    const bool yoshi = costume.fighter_kind == 14;
    const bool zelda_sheik = costume.fighter_kind == 7 || costume.fighter_kind == 19;
    require(mario || fox_family || mars || link_family || luigi || pikachu_family || purin || donkey || koopa || ness || peach || mewtwo || captain || ganon || gamewatch || kirby || ice_climber || samus || yoshi || zelda_sheik, "Native action store has no checked fighter command schema for this kind");
    std::vector<DatCommandRoot> roots;
    // Explicit source ftCo submotion groups. This certifies command operand
    // graphs only, not readiness of every original world service they invoke.
    command_motions_ = {0,1,2,3,6};
    auto group=[&](uint32_t first,uint32_t last){for(uint32_t id=first;id<=last;++id)command_motions_.insert(id);};
    command_motions_.insert(284);                    // ftCo_SM_ThrownCopyStar (motion state 291 selects submotion 284)
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
    // Yoshi's tongue/egg capture installs ftCo_MS_YoshiEgg on its victim;
    // the victim executes its own fighter-kind row 277 while held.
    command_motions_.insert(277);
    // Koopa's side special drives the common CaptureKoopa/CaptureDamageKoopa/
    // CaptureWaitKoopa and ThrownKoopa ground/air rows on its victim. These
    // rows likewise belong to every possible victim action store. The common
    // MS rows 278..287 map to source submotions SM_None,
    // SM_CaptureDamageKoopa..SM_ThrownKoopaAirB; the native action store is
    // indexed by those authored submotion rows, so retain exactly SM 278..283.
    group(278,283);
    /* Mewtwo's side special capture drives the common ThrownMewtwo rows on
     * its victim. These source command graphs belong to every possible
     * victim action store, like Koopa's capture rows above. */
    group(292,293);
    group(286,291);                              // shield-break knockdown
    if (mario) group(295,302);                   // Mario/Dr. Mario specials; taunts use common rows 239/240 above
    else if (link_family) group(295,313);        // Link-family action tables end at 313
    // The Captain-family range is source-complete through the final authored
    // action. Captain rows 295/297 include command opcodes 45/42; 42
    // (ftCommon_8007E83C parasol item rate) is now admitted globally, so
    // executing those rows without a held parasol trips the consumer's own
    // HSD_ASSERT rather than this guard — both outcomes are loud, and the
    // retail path cannot reach them either.
    // A dive catch runs the common CaptureCaptain submotion row 276 on the
    // catcher's own store (grab_cb -> ftCo_8009CA0C); without admission that
    // row dispatches the unsupported-command sentinel and aborts mid-match.
    else if (captain || ganon) {
        group(295,costume.motion_count-1);
        command_motions_.insert(276);
    }
    else if (luigi) group(295,costume.motion_count-1); // Luigi's authored special rows end at 311.
    else if (pikachu_family) group(295,costume.motion_count-1); // Both authored tables end at 319.
    else if (purin) group(295,costume.motion_count-1); // Purin five aerial jumps and original specials.
    else if (donkey) group(295,costume.motion_count-1); // Heavy carry, cargo throws and Donkey specials.
    else if (koopa) group(295,costume.motion_count-1); // Koopa's authored self rows end at 315.
    // Ness's 326-row table ends at 325: the Yo-Yo smash rows 295-298 plus
    // original SpecialN/S/Hi/Lw rows 299-325. Ness's own victim-side rows
    // 259-261 and 266-285 are empty motions, so the victim groups inserted
    // above carry no command words for a captured/shouldered Ness; the
    // animation identity still comes from the thrower's store.
    else if (ness) group(295,costume.motion_count-1);
    // Peach's 318-row table ends at 317: the Float/float-aerial rows 295-297,
    // the three AttackS4 weapons, original SpecialN/S/Hi/Lw rows 298-315 and
    // the authored ItemParasolOpen/Fall rows 316/317. The final else group's
    // (295,326) extent would overrun her table.
    else if (peach) group(295,costume.motion_count-1);
    else if (mewtwo) group(295,costume.motion_count-1); // Mewtwo's authored self rows end at 313.
    else if (gamewatch) group(295,costume.motion_count-1); // Game & Watch's source special rows are fighter-owned.
    else if (kirby) group(295,costume.motion_count-1); // Kirby's authored copy and fighter-special rows end at 478.
    else if (ice_climber) group(295,costume.motion_count-1); // Popo and Nana have separate 321-row source tables.
    else if (samus) group(295,costume.motion_count-1); // Samus's 313-row source table ends at 312.
    else if (yoshi) group(295,costume.motion_count-1); // Yoshi's 314-row source table ends at 313.
    else if (zelda_sheik) group(295,costume.motion_count-1); // Both transformation forms own their exact special-motion command rows.
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
    // Result/demo rows live in ftData + 0x14 and use the demo motion ID
    // domain (0..ftData_UnkIntPairs[kind].count-1), independent of the
    // ordinary gameplay rows above. Hydrate their real nested archives before
    // creating the shared command graph so demo command roots retain source
    // command ownership too.
    std::set<std::string> result_expected;
    std::vector<std::pair<DatFighterAction, std::shared_ptr<const DatAnimation>>> hydrated_results;
    std::vector<std::optional<DatFighterAction>> result_table;
    if (result_motion_) {
        const DatFighterActions demo_actions(*archive, costume, 0x14,
                                              fighter_demo_motion_count(costume.fighter_kind));
        result_table.resize(fighter_demo_motion_count(costume.fighter_kind));
        for (const auto& action : demo_actions.actions) {
            result_table.at(action.motion_id) = action;
            if (action.symbol.find("_ACTION_Win") == std::string::npos &&
                action.symbol.find("_ACTION_Selected") == std::string::npos &&
                action.symbol.find("_ACTION_Lose") == std::string::npos)
                continue;
            if (!action.archive_bytes) continue;
            result_expected.insert(action.symbol);
            const auto bytes = result_motion_->data();
            require(action.container_offset <= bytes.size() &&
                        action.archive_bytes <= bytes.size() - action.container_offset,
                    "Result demo action range exceeds its authored archive");
            auto nested = std::make_shared<const DatArchive>(bytes.subspan(
                action.container_offset, action.archive_bytes));
            const auto root = std::find_if(nested->public_symbols().begin(),
                                           nested->public_symbols().end(),
                                           [&](const auto& symbol) {
                                               return symbol.name == action.symbol;
                                           });
            require(root != nested->public_symbols().end(),
                    "Result demo archive is missing its authored motion symbol");
            auto animation = std::make_shared<const DatAnimation>(
                *nested, root->data_offset,
                DatAnimationPolicy::NativeFighterAction);
            if (action.command_offset) {
                roots.push_back({*action.command_offset, animation->end_frame,
                                 static_cast<uint8_t>((action.motion_flags & (1U << 30)) != 0)});
            }
            hydrated_results.emplace_back(action, std::move(animation));
        }
        require(!result_expected.empty() && hydrated_results.size() == result_expected.size(),
                "Result demo archive does not cover every authored result action");
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

    for (auto& result : hydrated_results) {
        result_actions_.emplace(result.first.motion_id,
            DatSelectedAction{DatRuntimeAction{result.first.motion_id, 0, result.first.motion_flags,
                                               result.first.container_offset, result.first.archive_bytes,
                                               result.first.symbol, {}, result.first.command_offset},
                              std::move(result.second), std::nullopt});
    }
    if (!result_table.empty()) {
        result_row_specs_.resize(result_table.size());
        for (std::size_t id = 0; id < result_table.size(); ++id) {
            const auto& row = result_table[id];
            if (!row) continue; // The authored empty slot remains unavailable.
            const auto found = result_actions_.find(static_cast<uint32_t>(id));
            if (found == result_actions_.end()) continue;
            void* command = nullptr;
            if (row->command_offset) command = commands_->at(*row->command_offset);
            result_row_specs_[id] = {found->second.action.symbol.c_str(),
                row->container_offset, row->archive_bytes, row->motion_flags,
                command, {row->blend_dynamics[0], row->blend_dynamics[1]}};
        }
        result_rows_.reset(melee_web_action_rows_create(result_row_specs_.data(),
                                                        result_row_specs_.size(),
                                                        nullptr, 0),
                           melee_web_action_rows_destroy);
        require(bool(result_rows_), "Native result action rows allocation failed");
    }
    if (costume.fighter_kind == 11) {
        require(nana_popo_archive && nana_popo_identity && !nana_popo_container.empty(),
                "Nana requires the authored Popo animation fallback");
        require(nana_popo_identity->fighter_kind == 10 && nana_popo_identity->costume_index == 0,
                "Nana animation fallback is not the base Popo identity");
        nana_popo_fallback_ = std::make_unique<GameplayActionStore>(
            std::move(nana_popo_archive), *nana_popo_identity, nana_popo_container);
    } else {
        require(!nana_popo_archive && !nana_popo_identity && nana_popo_container.empty(),
                "Popo animation fallback was supplied to a non-Nana fighter");
    }
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
void* GameplayActionStore::demo_action_rows() const
{ return result_rows_ ? melee_web_action_rows(result_rows_.get()) : nullptr; }
void* GameplayActionStore::demo_blend_rows() const
{ return result_rows_ ? melee_web_action_blends(result_rows_.get()) : nullptr; }
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
    void* source_table = melee_web_action_fighter_table(source.fighter_);
    const bool result_domain = source.result_rows_ &&
        source_table == melee_web_action_rows(source.result_rows_.get());
    const bool ordinary_domain = source_table == melee_web_action_rows(source.rows_.get());
    require(result_domain || ordinary_domain,
            "Fighter action table identity is not owned by its action store");
    const auto& source_rows = result_domain ? source.result_rows_ : source.rows_;
    require(uint32_t(motion) < melee_web_action_row_count(source_rows.get()),
            "Fighter action ID is outside its selected source table");
    std::shared_ptr<MeleeWebNativeActionRows> identity_rows;
    std::shared_ptr<DatCommands> command_owner;
    auto selected = source.select_source_action(uint32_t(motion), result_domain,
                                                &identity_rows, &command_owner);
    // The result table is a second authored motion domain.  Compare the
    // selected row itself so a result clip can be retained in the active
    // slot instead of being compared with the ordinary Pl*AJ row at the
    // same numeric ID.
    const auto& row = selected.action;
    std::shared_ptr<Clip> clip;
    for (const auto& active : active_) if (active &&
        active->identity_rows.get() == identity_rows.get() &&
        active->selected.action.container_offset == row.container_offset &&
        active->selected.action.archive_bytes == row.archive_bytes && active->selected.action.symbol == row.symbol) clip = active;
    if (!clip) {
        clip = std::make_shared<Clip>(std::move(selected));
        clip->identity_rows = identity_rows;
        clip->command_owner = std::move(command_owner);
    }
    active_[slot] = clip; motions_[slot] = uint32_t(motion);
    *tree = melee_web_native_clip_tree(clip->native.get());
    /* x5A4/x5A8 are source-row identities in ftData_80085CD8. */
    *identity = melee_web_action_identity(identity_rows.get(), uint32_t(motion));
    if (error && error_size) error[0] = 0;
    return 1;
}

DatSelectedAction GameplayActionStore::select_source_action(uint32_t motion, bool result_domain,
    std::shared_ptr<MeleeWebNativeActionRows>* identity_rows,
    std::shared_ptr<DatCommands>* command_owner)
{
    if (result_domain) {
        if (const auto found = result_actions_.find(motion); found != result_actions_.end())
        {
            if (identity_rows) *identity_rows = result_rows_;
            if (command_owner) *command_owner = commands_;
            return found->second;
        }
        throw DatError("Selected result demo motion has no authored archive");
    }
    // Source ftData_80085FD4 routes non-demo Nana rows with a null x14
    // archive pointer through Popo's exact source row at the same motion ID.
    if (runtime_->costume().fighter_kind == 11 &&
        runtime_->action(motion).archive_bytes == 0) {
        require(bool(nana_popo_fallback_), "Nana Popo animation fallback is unavailable");
        if (identity_rows) *identity_rows = nana_popo_fallback_->rows_;
        if (command_owner) *command_owner = nana_popo_fallback_->commands_;
        return nana_popo_fallback_->select_source_action(motion, false, nullptr, nullptr);
    }
    if (identity_rows) *identity_rows = rows_;
    if (command_owner) *command_owner = commands_;
    return store_.select_native_action(motion);
}
}
