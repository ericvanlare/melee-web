#include "gameplay_action_store.hpp"
#include "gameplay_result_motion_table.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <new>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>
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
} // namespace melee_web

namespace { MeleeWebActionInterval* active_interval = nullptr; }
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
namespace { bool interval_test_fail_next_event_push = false; bool interval_test_fail_next_seal_capture = false; }
extern "C" void melee_web_action_interval_test_fail_next_event_push(void)
{ interval_test_fail_next_event_push = true; }
extern "C" void melee_web_action_interval_test_fail_next_seal_capture(void)
{ interval_test_fail_next_seal_capture = true; }
#endif

struct MeleeWebActionInterval {
    using Store = melee_web::GameplayActionStore;
    using Clip = Store::Clip;
    using Cache = melee_web::DatFighterAnimationStore;
    using CacheEntry = Cache::Entry;

    enum class Phase : uint8_t { Captured, Recording, Sealed, Replaying, Finished, Failed, Disarmed };
    enum class EventKind : uint8_t { CacheHit, CacheMiss, ActionSelection };

    struct SlotState {
        std::array<std::shared_ptr<Clip>, 2> active;
        std::array<uint32_t, 2> motions{UINT32_MAX, UINT32_MAX};
        std::array<::FigaTree*, 2> trees{};
        std::array<void*, 2> identities{};
        void* table = nullptr;
    };
    struct BindingState {
        MeleeWebActionBindingSnapshot binding{};
        Store* store = nullptr;
        SlotState before;
        SlotState before_lease;
        SlotState after;
    };
    struct CacheState {
        Store* root = nullptr;
        Store* owner = nullptr;
        Cache* cache = nullptr;
        const void* interval_owner = nullptr;
        std::shared_ptr<const melee_web::DatFighterRuntime> runtime;
        std::array<CacheEntry, 2> before;
        std::array<CacheEntry, 2> after;
        Cache::CacheHooks hooks{};
        std::array<std::shared_ptr<const melee_web::DatAnimation>, 2> before_animation_leases;
        std::size_t before_next = 0;
        std::size_t after_next = 0;
    };
    struct Event {
        EventKind kind = EventKind::ActionSelection;
        Cache* cache = nullptr;
        const melee_web::DatFighterRuntime* runtime = nullptr;
        const melee_web::DatRuntimeAction* row = nullptr;
        std::shared_ptr<const melee_web::DatFighterRuntime> runtime_owner;
        std::shared_ptr<const melee_web::DatAnimation> animation;
        melee_web::DatAnimationPolicy policy = melee_web::DatAnimationPolicy::NativeFighterAction;
        bool cache_hit = false;
        Store* destination = nullptr;
        Store* source = nullptr;
        uint64_t destination_generation = 0;
        uint64_t source_generation = 0;
        void* source_table = nullptr;
        std::shared_ptr<MeleeWebNativeActionRows> row_owner;
        uint32_t motion = 0;
        unsigned slot = 0;
        bool result_domain = false;
        std::shared_ptr<Clip> clip;
    };

    Phase phase = Phase::Captured;
    uint32_t failure = MELEE_WEB_ACTION_INTERVAL_OK;
    std::array<MeleeWebActionBindingSnapshot, 6> bindings{};
    std::array<BindingState, 6> stores{};
    std::vector<CacheState> caches;
    std::vector<Event> events;
    std::size_t replay_cursor = 0;

    bool initialize()
    {
        std::size_t live = 0;
        for (uint32_t i = 0; i < bindings.size(); ++i) {
            if (!melee_web_action_binding_snapshot(i, &bindings[i])) return false;
            stores[i].binding = bindings[i];
            const auto& b = bindings[i];
            if (!b.fighter && !b.context && !b.select && !b.transfer) continue;
            if (!b.fighter || !b.context || !b.generation ||
                b.select != &Store::select || b.transfer != &Store::transfer ||
                !melee_web_action_binding_matches(&b)) return false;
            /* The current C row and exact production callbacks are validated
             * before the opaque C++ context or Fighter is dereferenced. */
            auto* store = static_cast<Store*>(b.context);
            if (store->fighter_ != b.fighter || store->binding_slot_ != i ||
                store->binding_generation_ != b.generation) return false;
            stores[i].store = store;
            if (!capture_slots(*store, stores[i].before) ||
                !collect_caches(store, store)) return false;
            stores[i].before_lease = stores[i].before;
            ++live;
        }
        return live != 0;
    }

    bool capture_slots(Store& store, SlotState& state) const
    {
        if (!store.fighter_) return false;
        void* table = melee_web_action_fighter_table(store.fighter_);
        if (table != melee_web_action_rows(store.rows_.get()) &&
            (!store.result_rows_ || table != melee_web_action_rows(store.result_rows_.get()))) return false;
        state.table = table;
        state.active = store.active_;
        state.motions = store.motions_;
        MeleeWebActionFighterClipState fighter_state{};
        if (!melee_web_action_fighter_clip_state(store.fighter_, &fighter_state)) return false;
        state.trees = {fighter_state.trees[0], fighter_state.trees[1]};
        state.identities = {fighter_state.identities[0], fighter_state.identities[1]};
        return slots_consistent(state);
    }

    static bool slots_consistent(const SlotState& state) noexcept
    {
        for (unsigned slot = 0; slot < 2; ++slot) {
            const auto& clip = state.active[slot];
            if (!clip) {
                if (state.motions[slot] != UINT32_MAX || state.trees[slot] || state.identities[slot]) return false;
                continue;
            }
            if (state.motions[slot] == UINT32_MAX || !clip->identity_rows) return false;
            if (state.trees[slot] != melee_web_native_clip_tree(clip->native.get()) ||
                state.identities[slot] != melee_web_action_identity(
                    clip->identity_rows.get(), state.motions[slot])) return false;
        }
        return true;
    }

    bool collect_caches(Store* root, Store* owner)
    {
        if (!owner || owner->store_.interval_owner_ != owner) return false;
        CacheState cache;
        cache.root = root;
        cache.owner = owner;
        cache.cache = &owner->store_;
        cache.interval_owner = owner->store_.interval_owner_;
        cache.runtime = owner->runtime_;
        cache.hooks = owner->store_.cache_hooks_;
        cache.before = owner->store_.cache_;
        for (std::size_t i = 0; i < cache.before.size(); ++i)
            cache.before_animation_leases[i] = cache.before[i].animation;
        cache.before_next = owner->store_.next_;
        caches.push_back(std::move(cache));
        return !owner->nana_popo_fallback_ ||
            collect_caches(root, owner->nana_popo_fallback_.get());
    }

    CacheState* cache_state(const Cache* cache) noexcept
    {
        for (auto& entry : caches) if (entry.cache == cache) return &entry;
        return nullptr;
    }
    const CacheState* cache_state(const Cache* cache) const noexcept
    {
        for (const auto& entry : caches) if (entry.cache == cache) return &entry;
        return nullptr;
    }
    const BindingState* store_state(const Store* store) const noexcept
    {
        for (const auto& entry : stores) if (entry.store == store) return &entry;
        return nullptr;
    }
    bool tree_contains_cache(const Store* current, const Store* expected_owner,
                             const Cache* expected_cache) const noexcept
    {
        if (!current) return false;
        if (current == expected_owner && &current->store_ == expected_cache) return true;
        return current->nana_popo_fallback_ &&
            tree_contains_cache(current->nana_popo_fallback_.get(), expected_owner, expected_cache);
    }

    static bool same_binding(const MeleeWebActionBindingSnapshot& a,
                             const MeleeWebActionBindingSnapshot& b) noexcept
    {
        return a.slot == b.slot && a.reserved == b.reserved && a.generation == b.generation &&
            a.fighter == b.fighter && a.context == b.context &&
            a.select == b.select && a.transfer == b.transfer;
    }
    bool live_bindings() const noexcept
    {
        for (uint32_t i = 0; i < bindings.size(); ++i) {
            MeleeWebActionBindingSnapshot current{};
            if (!melee_web_action_binding_snapshot(i, &current) ||
                !same_binding(bindings[i], current)) return false;
            if (bindings[i].fighter &&
                (!melee_web_action_binding_matches(&bindings[i]) ||
                 bindings[i].select != &Store::select || bindings[i].transfer != &Store::transfer)) return false;
        }
        return true;
    }

    static bool same_slot_state(const SlotState& a, const SlotState& b) noexcept
    {
        if (a.table != b.table) return false;
        for (unsigned i = 0; i < 2; ++i) {
            if (a.active[i].get() != b.active[i].get() || a.motions[i] != b.motions[i] ||
                a.trees[i] != b.trees[i] || a.identities[i] != b.identities[i]) return false;
        }
        return true;
    }
    static bool same_cache_entry(const CacheEntry& a, const CacheEntry& b) noexcept
    {
        return a.offset == b.offset && a.size == b.size && a.symbol == b.symbol &&
            a.native_action == b.native_action && a.animation.get() == b.animation.get();
    }
    static bool same_cache_hooks(const typename Cache::CacheHooks& a,
                                 const typename Cache::CacheHooks& b) noexcept
    {
        return a.hit == b.hit && a.miss_replay == b.miss_replay &&
            a.miss_record == b.miss_record;
    }
    static bool same_cache_state(const CacheState& saved, const Cache& current,
                                 bool final) noexcept
    {
        const auto& expected = final ? saved.after : saved.before;
        const std::size_t next = final ? saved.after_next : saved.before_next;
        if (current.next_ != next) return false;
        for (unsigned i = 0; i < 2; ++i) if (!same_cache_entry(current.cache_[i], expected[i])) return false;
        return true;
    }

    bool live_cache_owners() const noexcept
    {
        for (const auto& cache : caches) {
            const BindingState* root = store_state(cache.root);
            if (!root || !root->store || cache.root->binding_generation_ != root->binding.generation ||
                !tree_contains_cache(cache.root, cache.owner, cache.cache)) return false;
            if (&cache.owner->store_ != cache.cache || cache.owner->runtime_.get() != cache.runtime.get() ||
                cache.cache->fighter_.get() != cache.runtime.get() ||
                cache.interval_owner != cache.owner || cache.cache->interval_owner_ != cache.interval_owner ||
                !same_cache_hooks(cache.hooks, cache.cache->cache_hooks_)) return false;
        }
        return true;
    }
    bool live_cache_owner(const Cache* cache) const noexcept
    {
        return live_bindings() && live_cache_owners() && cache_state(cache);
    }

    bool capture_after()
    {
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
        if (interval_test_fail_next_seal_capture) {
            interval_test_fail_next_seal_capture = false;
            throw std::bad_alloc();
        }
#endif
        if (!live_bindings() || !live_cache_owners()) return false;
        for (auto& binding : stores) if (binding.store) {
            if (!capture_slots(*binding.store, binding.after)) return false;
            /* The interval owns action selections and decoded caches; the
             * Fighter's borrowed action-table field remains externally
             * owned, so it must be identical at both boundaries. */
            if (binding.before.table != binding.after.table) return false;
        }
        for (auto& state : caches) {
            state.after = state.cache->cache_;
            state.after_next = state.cache->next_;
        }
        return true;
    }

    bool state_matches(bool final) const noexcept
    {
        if (!live_bindings() || !live_cache_owners()) return false;
        for (const auto& binding : stores) if (binding.store) {
            SlotState current;
            if (!capture_slots(*binding.store, current) ||
                !same_slot_state(current, final ? binding.after : binding.before)) return false;
        }
        for (const auto& cache : caches) {
            if (!same_cache_state(cache, *cache.cache, final)) return false;
        }
        return true;
    }

    bool begin()
    {
        if (phase != Phase::Captured || failure || active_interval) return false;
        if (!state_matches(false)) return false;
        events.clear();
        replay_cursor = 0;
        phase = Phase::Recording;
        active_interval = this;
        return true;
    }
    int seal()
    {
        if (phase != Phase::Recording || active_interval != this) return 0;
        active_interval = nullptr;
        bool captured = false;
        try { captured = !failure && capture_after(); }
        catch (...) {
            fail(MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED);
            phase = Phase::Failed;
            return 0;
        }
        if (!captured) {
            fail(failure ? failure : MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED);
            phase = Phase::Failed;
            return 0;
        }
        replay_cursor = 0;
        phase = Phase::Sealed;
        return 1;
    }

    uint32_t validation_failure() const noexcept
    {
        if (!live_bindings()) return MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED;
        if (!live_cache_owners() || !state_matches(true)) return MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED;
        return MELEE_WEB_ACTION_INTERVAL_OK;
    }
    int admit() const noexcept
    {
        return phase == Phase::Sealed && !failure && !active_interval &&
            validation_failure() == MELEE_WEB_ACTION_INTERVAL_OK;
    }
    int restore()
    {
        if (phase != Phase::Sealed || failure || active_interval) {
            fail(MELEE_WEB_ACTION_INTERVAL_BAD_PHASE);
            return 0;
        }
        const uint32_t invalid = validation_failure();
        if (invalid) { fail(invalid); return 0; }
        /* All fallible and ownership checks finish above. Swaps preserve the
         * displaced references in the capsule, so commit cannot allocate or
         * release a native Clip to its last owner. The Fighter's four clip
         * fields are owned by the later source-payload restore; this component
         * changes only its C++ active slots/motions and decoder cache. */
        for (auto& state : caches) {
            state.cache->cache_.swap(state.before);
            std::swap(state.cache->next_, state.before_next);
        }
        for (auto& state : stores) if (state.store) {
            state.store->active_.swap(state.before.active);
            state.store->motions_.swap(state.before.motions);
        }
        replay_cursor = 0;
        phase = Phase::Replaying;
        active_interval = this;
        return 1;
    }

    int finish()
    {
        if (phase != Phase::Replaying || active_interval != this) {
            fail(MELEE_WEB_ACTION_INTERVAL_BAD_PHASE);
            return 0;
        }
        active_interval = nullptr;
        if (failure) { phase = Phase::Failed; return 0; }
        if (replay_cursor != events.size()) {
            fail(MELEE_WEB_ACTION_INTERVAL_REPLAY_INCOMPLETE);
            phase = Phase::Failed;
            return 0;
        }
        const uint32_t invalid = validation_failure();
        if (invalid) {
            fail(invalid);
            phase = Phase::Failed;
            return 0;
        }
        phase = Phase::Finished;
        return 1;
    }
    void disarm() noexcept
    {
        if (active_interval == this) active_interval = nullptr;
        if (phase == Phase::Finished || phase == Phase::Failed || phase == Phase::Disarmed) return;
        if (!failure) fail(MELEE_WEB_ACTION_INTERVAL_DISARMED);
        phase = Phase::Disarmed;
    }

    void fail(uint32_t code) noexcept
    {
        if (!failure) failure = code ? code : MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED;
    }
    void owner_unbound(const Store* store) noexcept
    {
        if (active_interval == this &&
            (phase == Phase::Recording || phase == Phase::Replaying) && store_state(store))
            fail(MELEE_WEB_ACTION_INTERVAL_BINDING_CHANGED);
    }
    void push(Event&& event) noexcept
    {
        if (failure) return;
        try {
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
            if (interval_test_fail_next_event_push) {
                interval_test_fail_next_event_push = false;
                throw std::bad_alloc();
            }
#endif
            events.push_back(std::move(event));
        }
        catch (...) { fail(MELEE_WEB_ACTION_INTERVAL_RECORD_FAILED); }
    }

    bool cache_event_matches(const Event& event, EventKind kind, const Cache* cache,
        const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
        const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy,
        const std::shared_ptr<const melee_web::DatAnimation>& animation = {}) const noexcept
    {
        return event.kind == kind && event.cache == cache && event.runtime == runtime.get() &&
            event.row == row && event.policy == policy &&
            event.animation.get() == animation.get();
    }
    Event* next_event(EventKind kind) noexcept
    {
        if (phase != Phase::Replaying) return nullptr;
        if (replay_cursor >= events.size()) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return nullptr;
        }
        Event* event = &events[replay_cursor++];
        if (event->kind != kind) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return nullptr;
        }
        return event;
    }
    void cache_hit(const Cache* cache,
        const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
        const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy,
        const std::shared_ptr<const melee_web::DatAnimation>& animation) noexcept
    {
        if (!live_cache_owner(cache)) { fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH); return; }
        if (phase == Phase::Recording) {
            Event event; event.kind = EventKind::CacheHit; event.cache = const_cast<Cache*>(cache);
            event.runtime = runtime.get(); event.row = row; event.runtime_owner = runtime;
            event.policy = policy; event.cache_hit = true; event.animation = animation;
            push(std::move(event));
        } else if (phase == Phase::Replaying) {
            Event* event = next_event(EventKind::CacheHit);
            if (event && !cache_event_matches(*event, EventKind::CacheHit, cache, runtime, row, policy, animation)) {
                fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            }
        }
    }
    std::shared_ptr<const melee_web::DatAnimation> cache_miss_replay(const Cache* cache,
        const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
        const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy) noexcept
    {
        if (phase != Phase::Replaying) return {};
        if (!live_cache_owner(cache)) { fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH); return {}; }
        Event* event = next_event(EventKind::CacheMiss);
        if (!event || event->cache != cache || event->runtime != runtime.get() ||
            event->row != row || event->policy != policy || !event->animation) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return {};
        }
        return event->animation;
    }
    void cache_miss_record(const Cache* cache,
        const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
        const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy,
        const std::shared_ptr<const melee_web::DatAnimation>& animation) noexcept
    {
        if (phase != Phase::Recording) return;
        if (!live_cache_owner(cache)) { fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH); return; }
        Event event; event.kind = EventKind::CacheMiss; event.cache = const_cast<Cache*>(cache);
        event.runtime = runtime.get(); event.row = row; event.runtime_owner = runtime;
        event.policy = policy; event.cache_hit = false; event.animation = animation;
        push(std::move(event));
    }

    bool selection_key_matches(const Event& event, Store* destination, Store* source,
        void* source_table, bool result_domain, uint32_t motion, unsigned slot,
        const std::shared_ptr<MeleeWebNativeActionRows>& row_owner,
        const melee_web::DatRuntimeAction& row,
        const melee_web::DatSelectedAction& selected,
        const std::shared_ptr<melee_web::DatCommands>& command_owner) const noexcept
    {
        const auto& action = event.clip->selected.action;
        return event.kind == EventKind::ActionSelection && event.destination == destination &&
            event.source == source && event.destination_generation == destination->binding_generation_ &&
            event.source_generation == source->binding_generation_ && event.source_table == source_table &&
            event.result_domain == result_domain && event.motion == motion && event.slot == slot &&
            event.row_owner.get() == row_owner.get() && action.container_offset == row.container_offset &&
            action.archive_bytes == row.archive_bytes && action.symbol == row.symbol &&
            event.clip->selected.animation.get() == selected.animation.get() &&
            event.clip->command_owner.get() == command_owner.get();
    }
    void record_selection(Store* destination, Store* source, void* source_table,
        bool result_domain, uint32_t motion, unsigned slot,
        const std::shared_ptr<MeleeWebNativeActionRows>& row_owner,
        const std::shared_ptr<Clip>& clip) noexcept
    {
        if (phase != Phase::Recording) return;
        if (!store_state(destination) || !store_state(source) || !row_owner || !clip) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return;
        }
        Event event; event.kind = EventKind::ActionSelection;
        event.destination = destination; event.source = source;
        event.destination_generation = destination->binding_generation_;
        event.source_generation = source->binding_generation_;
        event.source_table = source_table; event.result_domain = result_domain;
        event.motion = motion; event.slot = slot; event.row_owner = row_owner; event.clip = clip;
        push(std::move(event));
    }
    std::shared_ptr<Clip> replay_selection(Store* destination, Store* source, void* source_table,
        bool result_domain, uint32_t motion, unsigned slot,
        const std::shared_ptr<MeleeWebNativeActionRows>& row_owner,
        const melee_web::DatRuntimeAction& row,
        const melee_web::DatSelectedAction& selected,
        const std::shared_ptr<melee_web::DatCommands>& command_owner,
        const std::shared_ptr<Clip>& active_match) noexcept
    {
        if (phase != Phase::Replaying) return active_match;
        Event* event = next_event(EventKind::ActionSelection);
        if (!event || !event->clip ||
            !selection_key_matches(*event, destination, source, source_table, result_domain,
                                   motion, slot, row_owner, row, selected, command_owner)) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return active_match;
        }
        if (active_match && active_match.get() != event->clip.get()) {
            fail(MELEE_WEB_ACTION_INTERVAL_SELECTION_MISMATCH);
            return active_match;
        }
        return event->clip;
    }

};

namespace {
void action_interval_cache_hit_hook(const melee_web::DatFighterAnimationStore* cache,
    const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
    const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy,
    const std::shared_ptr<const melee_web::DatAnimation>& animation)
{
    if (active_interval) active_interval->cache_hit(cache, runtime, row, policy, animation);
}
std::shared_ptr<const melee_web::DatAnimation> action_interval_cache_miss_replay_hook(
    const melee_web::DatFighterAnimationStore* cache,
    const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
    const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy)
{
    return active_interval ? active_interval->cache_miss_replay(cache, runtime, row, policy)
                           : std::shared_ptr<const melee_web::DatAnimation>{};
}
void action_interval_cache_miss_record_hook(const melee_web::DatFighterAnimationStore* cache,
    const std::shared_ptr<const melee_web::DatFighterRuntime>& runtime,
    const melee_web::DatRuntimeAction* row, melee_web::DatAnimationPolicy policy,
    const std::shared_ptr<const melee_web::DatAnimation>& animation)
{
    if (active_interval) active_interval->cache_miss_record(cache, runtime, row, policy, animation);
}
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
void inert_action_interval_cache_hit_hook(const melee_web::DatFighterAnimationStore*,
    const std::shared_ptr<const melee_web::DatFighterRuntime>&,
    const melee_web::DatRuntimeAction*, melee_web::DatAnimationPolicy,
    const std::shared_ptr<const melee_web::DatAnimation>&) {}
std::shared_ptr<const melee_web::DatAnimation> inert_action_interval_cache_miss_replay_hook(
    const melee_web::DatFighterAnimationStore*,
    const std::shared_ptr<const melee_web::DatFighterRuntime>&,
    const melee_web::DatRuntimeAction*, melee_web::DatAnimationPolicy)
{ return {}; }
void inert_action_interval_cache_miss_record_hook(const melee_web::DatFighterAnimationStore*,
    const std::shared_ptr<const melee_web::DatFighterRuntime>&,
    const melee_web::DatRuntimeAction*, melee_web::DatAnimationPolicy,
    const std::shared_ptr<const melee_web::DatAnimation>&) {}
#endif
}

namespace melee_web {
GameplayActionStore::GameplayActionStore(std::shared_ptr<const DatArchive> archive, const FighterCostume& costume,
    std::span<const uint8_t> container, std::shared_ptr<const DatArchive> result_motion,
    std::shared_ptr<const DatArchive> nana_popo_archive,
    const FighterCostume* nana_popo_identity,
    std::span<const uint8_t> nana_popo_container)
    : runtime_(std::make_shared<const DatFighterRuntime>(archive, costume)), store_(runtime_, container),
      rows_(nullptr, melee_web_action_rows_destroy), result_motion_(std::move(result_motion))
{
    store_.set_interval_owner(this);
    store_.set_interval_cache_hooks({action_interval_cache_hit_hook,
        action_interval_cache_miss_replay_hook, action_interval_cache_miss_record_hook});
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
    // Yoshi Egg Lay and Kirby's copied Yoshi special install the shared
    // ftCo_SM_KirbyYoshiEgg action (source submotion 294) on the victim.
    // Every fighter can be that victim, so retain its source command root in
    // each fighter's owned action store rather than dispatching the sentinel.
    command_motions_.insert(294);
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
        const auto result_spec = result_motion_archive_spec(costume.fighter_kind);
        require(!result_spec.root.empty(),
                "Fighter has no source Results motion root");
        const auto root = std::find_if(
            result_motion_->public_symbols().begin(),
            result_motion_->public_symbols().end(),
            [&](const auto& symbol) { return symbol.name == result_spec.root; });
        require(root != result_motion_->public_symbols().end(),
                "Fighter Results archive is missing its source motion root");
        const auto result_root_offset = root->data_offset;
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
            require(result_root_offset <= bytes.size() &&
                        action.container_offset <= bytes.size() - result_root_offset,
                    "Result demo action offset exceeds its authored public root");
            const auto nested_offset = std::size_t{result_root_offset} +
                                       action.container_offset;
            require(action.archive_bytes <= bytes.size() - nested_offset,
                    "Result demo action range exceeds its authored archive");
            std::shared_ptr<const DatArchive> nested;
            try {
                nested = std::make_shared<const DatArchive>(
                    bytes.subspan(nested_offset, action.archive_bytes));
            } catch (const DatError& error) {
                throw DatError("Result demo action " + action.symbol +
                    " for fighter kind " + std::to_string(costume.fighter_kind) +
                    " at archive offset " + std::to_string(nested_offset) +
                    " (root " + std::to_string(result_root_offset) + "): " +
                    error.what());
            }
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
GameplayActionStore::~GameplayActionStore()
{
    unbind();
    store_.set_interval_cache_hooks({});
    store_.set_interval_owner(nullptr);
}
#if defined(MELEE_WEB_ACTION_INTERVAL_TEST)
void GameplayActionStore::interval_test_disable_cache_hooks()
{ store_.set_interval_cache_hooks({}); }
void GameplayActionStore::interval_test_change_cache_hooks()
{ store_.set_interval_cache_hooks({inert_action_interval_cache_hit_hook,
    inert_action_interval_cache_miss_replay_hook, inert_action_interval_cache_miss_record_hook}); }
void GameplayActionStore::interval_test_restore_cache_hooks()
{ store_.set_interval_cache_hooks({action_interval_cache_hit_hook,
    action_interval_cache_miss_replay_hook, action_interval_cache_miss_record_hook}); }
void GameplayActionStore::interval_test_change_cache_owner()
{ store_.set_interval_owner(nullptr); }
void GameplayActionStore::interval_test_restore_cache_owner()
{ store_.set_interval_owner(this); }
#endif
void GameplayActionStore::bind(Fighter* fighter)
{
    require(!fighter_, "Action store is already bound");
    require(melee_web_action_bind(fighter, this, select, transfer), "Fighter action binding is unavailable");
    MeleeWebActionBindingSnapshot binding{};
    if (!melee_web_action_binding_for_context(this, &binding) || binding.fighter != fighter ||
        binding.select != &select || binding.transfer != &transfer) {
        melee_web_action_unbind(fighter);
        throw DatError("Fighter action binding identity could not be retained");
    }
    fighter_ = fighter;
    binding_slot_ = binding.slot;
    binding_generation_ = binding.generation;
}
void GameplayActionStore::unbind()
{
    if (fighter_) {
        if (active_interval) active_interval->owner_unbound(this);
        melee_web_action_unbind(fighter_);
        fighter_ = nullptr;
        binding_slot_ = UINT32_MAX;
        binding_generation_ = 0;
        active_ = {};
        motions_.fill(UINT32_MAX);
    }
}
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
    MeleeWebActionBindingSnapshot binding{};
    if (!melee_web_action_binding_for_context(context, &binding) ||
        binding.select != &select || binding.transfer != &transfer) return 0;
    try {
        auto& self = *static_cast<GameplayActionStore*>(context);
        if (self.fighter_ != binding.fighter || self.binding_slot_ != binding.slot ||
            self.binding_generation_ != binding.generation) return 0;
        return self.select_from(self, motion, slot, tree, identity, error, error_size);
    } catch (const std::exception& e) {
        if (error && error_size) std::snprintf(error, error_size, "%s", e.what()); return 0;
    }
}
int GameplayActionStore::transfer(void* destination_context, void* source_context,
    Fighter* destination, Fighter* source, int motion, unsigned slot, FigaTree** tree,
    void** identity, char* error, size_t error_size)
{
    MeleeWebActionBindingSnapshot destination_binding{}, source_binding{};
    if (!melee_web_action_binding_for_context(destination_context, &destination_binding) ||
        !melee_web_action_binding_for_context(source_context, &source_binding) ||
        destination_binding.select != &select || destination_binding.transfer != &transfer ||
        source_binding.select != &select || source_binding.transfer != &transfer ||
        destination_binding.fighter != destination || source_binding.fighter != source) return 0;
    try {
        auto& target = *static_cast<GameplayActionStore*>(destination_context);
        auto& origin = *static_cast<GameplayActionStore*>(source_context);
        if (target.binding_slot_ != destination_binding.slot ||
            target.binding_generation_ != destination_binding.generation ||
            origin.binding_slot_ != source_binding.slot ||
            origin.binding_generation_ != source_binding.generation) return 0;
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
    if (active_interval && active_interval->phase == MeleeWebActionInterval::Phase::Replaying) {
        clip = active_interval->replay_selection(this, &source, source_table, result_domain,
            uint32_t(motion), slot, identity_rows, row, selected, command_owner, clip);
    }
    if (!clip) {
        clip = std::make_shared<Clip>(std::move(selected));
        clip->identity_rows = identity_rows;
        clip->command_owner = std::move(command_owner);
    }
    if (active_interval && active_interval->phase == MeleeWebActionInterval::Phase::Recording) {
        active_interval->record_selection(this, &source, source_table, result_domain,
            uint32_t(motion), slot, identity_rows, clip);
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

namespace {
MeleeWebActionInterval* interval_create() noexcept
{
    auto* interval = new (std::nothrow) MeleeWebActionInterval();
    if (!interval) return nullptr;
    try {
        if (!interval->initialize()) {
            delete interval;
            return nullptr;
        }
    } catch (...) {
        delete interval;
        return nullptr;
    }
    return interval;
}
}

extern "C" MeleeWebActionInterval* melee_web_action_interval_capture(void)
{
    return interval_create();
}
extern "C" int melee_web_action_interval_begin_record(MeleeWebActionInterval* interval)
{
    if (!interval) return 0;
    if (interval->begin()) return 1;
    interval->fail(interval->phase == MeleeWebActionInterval::Phase::Captured
        ? MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED : MELEE_WEB_ACTION_INTERVAL_BAD_PHASE);
    return 0;
}
extern "C" int melee_web_action_interval_seal_record(MeleeWebActionInterval* interval)
{
    return interval && interval->seal() == 1;
}
extern "C" int melee_web_action_interval_admit(const MeleeWebActionInterval* interval)
{
    return interval && interval->admit() == 1;
}
extern "C" int melee_web_action_interval_restore(MeleeWebActionInterval* interval)
{
    return interval && interval->restore() == 1;
}
extern "C" int melee_web_action_interval_finish_replay(MeleeWebActionInterval* interval)
{
    return interval && interval->finish() == 1;
}
extern "C" int melee_web_action_interval_matches_finished(const MeleeWebActionInterval* interval)
{
    return interval && interval->phase == MeleeWebActionInterval::Phase::Finished &&
        !interval->failure && !active_interval &&
        interval->validation_failure() == MELEE_WEB_ACTION_INTERVAL_OK;
}
extern "C" uint32_t melee_web_action_interval_failure_code(const MeleeWebActionInterval* interval)
{
    if (!interval) return MELEE_WEB_ACTION_INTERVAL_CAPTURE_REFUSED;
    if (interval->failure) return interval->failure;
    if (interval->phase == MeleeWebActionInterval::Phase::Captured && !interval->state_matches(false))
        return MELEE_WEB_ACTION_INTERVAL_OWNER_STATE_CHANGED;
    if ((interval->phase == MeleeWebActionInterval::Phase::Sealed ||
         interval->phase == MeleeWebActionInterval::Phase::Replaying) &&
        interval->validation_failure() != MELEE_WEB_ACTION_INTERVAL_OK)
        return interval->validation_failure();
    return MELEE_WEB_ACTION_INTERVAL_OK;
}
extern "C" void melee_web_action_interval_disarm(MeleeWebActionInterval* interval)
{
    if (interval) interval->disarm();
}
extern "C" void melee_web_action_interval_destroy(MeleeWebActionInterval* interval)
{
    if (!interval) return;
    if (active_interval == interval) active_interval = nullptr;
    delete interval;
}
