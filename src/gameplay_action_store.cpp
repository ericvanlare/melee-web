#include "gameplay_action_store.hpp"
#include <cstdio>
#include <set>
namespace melee_web {
namespace { void require(bool v, const char* m) { if (!v) throw DatError(m); } }
struct GameplayActionStore::Clip {
    DatSelectedAction selected;
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
    require(costume.fighter_kind == 0, "Native action store currently requires Mario Wait command semantics");
    std::vector<uint32_t> roots;
    std::set<uint32_t> supported_motions{2, 6, 20};
    for (auto choice : runtime_->wait_choices()) supported_motions.insert(choice.motion_id);
    for (auto id : supported_motions) if (auto offset = runtime_->action(id).command_offset) roots.push_back(*offset);
    if (!roots.empty()) commands_ = std::make_unique<DatCommands>(archive, roots);
    std::vector<MeleeWebActionRow> rows;
    std::vector<MeleeWebWaitChoice> waits;
    for (const auto& a : runtime_->actions()) {
        void* command = nullptr;
        if (a.command_offset) command = supported_motions.contains(a.motion_id) ? commands_->at(*a.command_offset) : melee_web_commands_unsupported();
        rows.push_back({a.symbol.c_str(), a.container_offset, a.archive_bytes, a.motion_flags, command,
                       {a.blend_dynamics[0], a.blend_dynamics[1]}});
    }
    for (auto w : runtime_->wait_choices()) waits.push_back({w.motion_id, w.weight});
    rows_.reset(melee_web_action_rows_create(rows.data(), rows.size(), waits.data(), waits.size()));
    require(bool(rows_), "Native action rows allocation failed");
}
GameplayActionStore::~GameplayActionStore() { unbind(); }
void GameplayActionStore::bind(Fighter* fighter)
{
    require(!fighter_, "Action store is already bound");
    require(melee_web_action_bind(fighter, this, select), "Fighter action binding is unavailable"); fighter_ = fighter;
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
        require(motion >= 0 && slot < 2, "Action selection is out of range");
        const auto& row = self.runtime_->action(uint32_t(motion));
        auto selected = self.store_.select(uint32_t(motion));
        std::shared_ptr<Clip> clip;
        for (const auto& active : self.active_) if (active &&
            active->selected.action.container_offset == row.container_offset &&
            active->selected.action.archive_bytes == row.archive_bytes && active->selected.action.symbol == row.symbol) clip = active;
        if (!clip) clip = std::make_shared<Clip>(std::move(selected));
        self.active_[slot] = clip; self.motions_[slot] = uint32_t(motion);
        *tree = melee_web_native_clip_tree(clip->native.get());
        *identity = melee_web_action_identity(self.rows_.get(), uint32_t(motion));
        if (error && error_size) error[0] = 0;
        return 1;
    } catch (const std::exception& e) {
        if (error && error_size) std::snprintf(error, error_size, "%s", e.what()); return 0;
    }
}
}
