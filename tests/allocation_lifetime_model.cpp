// JSON-lines adapter for independently derived allocation lifetimes. Source
// algorithms live in src/; this adapter binds their identities and services.
#include "allocation_model_support.hpp"
#include "source_game_heap_context.hpp"

#include <algorithm>
#include <iterator>

namespace {
namespace game_heap = melee_web::source_game_heap;
namespace source_handle = melee_web::source_handle;

struct LifetimeModel : Model {
    game_heap::Context game;
    std::vector<game_heap::Descriptor> descriptors;
    std::map<std::uint32_t, Address> pool_descriptors;
    std::map<std::string, Address> references;
    std::map<std::string, std::uint32_t> reference_heaps;
    std::map<std::string, std::uint32_t> reference_pools;
    struct PoolBacking {
        std::uint32_t heap;
        Address base;
        std::uint64_t bytes;
    };
    std::map<std::uint32_t, std::vector<PoolBacking>> pool_backings;
    Registry registry;
    std::map<std::uint32_t, std::pair<std::uint32_t, std::uint32_t>> inactive_heads;
    std::uint32_t os_limit = 0, descriptor_base = 0;
    std::uint32_t arena_start = 0, arena_end = 0;
    std::uint32_t original_main_lo = 0, original_main_hi = 0;
    std::uint32_t hsd_lo = 0, hsd_hi = 0;
    std::uint32_t object_size = 0, object_remain = 0;
    std::int32_t os_selected = -1, hsd_selected = -1, pending_hsd = -1;
    bool replacing_hsd = false;
    unsigned replacement_phase = 0;
    bool replacement_forgot_pools = false;

    static void require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    Address reference(const std::string& label) const
    {
        const auto found = references.find(label);
        if (found == references.end()) throw std::runtime_error("unknown modeled producer label");
        return found->second;
    }

    void emit_begin(const std::string& op, const char* status = "ok")
    {
        std::cout << "{\"op\":\"" << op << "\",\"status\":\"" << status << "\"";
    }

    void emit_os()
    {
        std::cout << ",\"heaps\":{\"HeapArray\":" << descriptor_base
                  << ",\"NumHeaps\":" << os_limit
                  << ",\"ArenaStart\":" << arena_start << ",\"ArenaEnd\":" << arena_end
                  << ",\"__OSCurrHeap\":" << static_cast<std::uint32_t>(os_selected)
                  << ",\"current_heap\":" << static_cast<std::uint32_t>(hsd_selected)
                  << ",\"descriptors\":[";
        for (std::uint32_t i = 0; i < os_limit; ++i) {
            if (i) std::cout << ',';
            const auto found = heaps.find(i);
            if (found == heaps.end() || !found->second->initialized()) {
                const auto heads = inactive_heads[i];
                std::cout << "[4294967295," << heads.first << "," << heads.second << "]";
            } else {
                const auto& state = found->second->state();
                std::cout << '[' << state.heap_bytes << ','
                          << (state.free.empty() ? 0 : state.free.front().start.value()) << ','
                          << (state.allocated.empty() ? 0 : state.allocated.front().start.value()) << ']';
            }
        }
        std::cout << "]}";
    }

    void emit_game()
    {
        require(game.initialized(), "game heap context missing");
        const auto state = game.snapshot();
        std::cout << ",\"game_heap_words\":[" << state.bounds.arena_lo.value() << ','
                  << state.bounds.arena_hi.value() << ',' << state.bounds.aram_lo.value() << ','
                  << state.bounds.aram_hi.value();
        for (const auto& item : state.heaps) {
            std::cout << ',' << static_cast<std::uint32_t>(item.id) << ',' << item.handle.value()
                      << ',' << item.start.value() << ',' << item.size << ',' << item.type
                      << ',' << static_cast<std::uint32_t>(item.transient)
                      << ',' << static_cast<std::uint32_t>(item.status);
        }
        std::cout << ']';
    }

    game_heap::Request request(game_heap::RequestKind kind)
    {
        const auto value = game.next_request();
        require(value.has_value() && value->kind == kind, "source game-heap request order differs");
        return *value;
    }

    void complete(game_heap::RequestResult result = {})
    {
        require(game.complete_request(result) == game_heap::Status::ok,
                "game-heap request completion failed");
    }

    void emit_request()
    {
        const auto value = game.next_request();
        if (!value) { std::cout << ",\"request\":null"; return; }
        std::cout << ",\"request\":{\"kind\":\"" << game_heap::request_kind_name(value->kind)
                  << "\",\"index\":" << value->heap_index
                  << ",\"id\":" << static_cast<std::uint32_t>(value->id)
                  << ",\"handle\":" << value->handle.value()
                  << ",\"lo\":" << value->lo.value() << ",\"hi\":" << value->hi.value() << '}';
    }

    std::uint32_t create_os(std::uint32_t lo, std::uint32_t hi)
    {
        for (std::uint32_t id = 0; id < os_limit; ++id) {
            auto& target = heaps[id];
            if (!target) target = std::make_unique<Heap>();
            if (!target->initialized()) {
                require(target->create_empty(Address(lo), Address(hi)) == Status::ok,
                        "source OS heap creation rejected derived bounds");
                return id;
            }
        }
        throw std::runtime_error("original OS heap descriptor slots exhausted");
    }

    void destroy_os(std::uint32_t id)
    {
        auto& target = heap(id);
        const auto& state = target.state();
        inactive_heads[id] = {state.free.empty() ? 0 : state.free.front().start.value(),
                              state.allocated.empty() ? 0 : state.allocated.front().start.value()};
        target.clear();
        for (auto it = reference_heaps.begin(); it != reference_heaps.end();) {
            if (it->second == id) {
                references.erase(it->first);
                // A pool object can retain the same backing heap identity as
                // its allocation.  Retire the pool ownership together with
                // the heap reference; looking the heap up after this loop
                // would miss it because reference_heaps is already erased.
                reference_pools.erase(it->first);
                it = reference_heaps.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pool_backings.begin(); it != pool_backings.end();) {
            auto& backings = it->second;
            backings.erase(std::remove_if(backings.begin(), backings.end(),
                [id](const PoolBacking& backing) { return backing.heap == id; }),
                backings.end());
            if (backings.empty()) it = pool_backings.erase(it);
            else ++it;
        }
    }

    void emit_handle_words(HandleAddress identity)
    {
        const auto snapshot = handles.snapshot();
        const auto found = std::find_if(snapshot.active.begin(), snapshot.active.end(),
            [&](const auto& item) { return item.identity == identity; });
        require(found != snapshot.active.end(), "modeled handle descriptor is missing");
        std::cout << ",\"handle_words\":[" << found->next.value() << ',' << found->lo.value()
                  << ',' << found->hi.value() << ',' << found->prev.value() << ']';
    }

    void emit_retired_payloads()
    {
        const auto active = handles.snapshot().active;
        std::cout << ",\"retired_payloads\":[";
        bool comma = false;
        for (auto it = payload_labels.begin(); it != payload_labels.end();) {
            const auto identity = handle_labels.at(it->first);
            const auto found = std::find_if(active.begin(), active.end(),
                [&](const auto& item) { return item.identity == identity; });
            if (found != active.end()) { ++it; continue; }
            if (comma) std::cout << ',';
            comma = true;
            std::cout << "{\"handle\":" << identity.value()
                      << ",\"payload\":" << it->second.value() << '}';
            handle_labels.erase(it->first);
            it = payload_labels.erase(it);
        }
        // A destroyed source handle may have had no payloads, so the payload
        // walk above would otherwise leave its diagnostic labels alive for
        // the rest of a long replay.  Labels are provenance only; once the
        // source descriptor is retired, retaining them would both consume
        // memory and make stale identities look usable.
        for (auto it = handle_labels.begin(); it != handle_labels.end();) {
            const auto found = std::find_if(active.begin(), active.end(),
                [&](const auto& item) { return item.identity == it->second; });
            if (found == active.end()) it = handle_labels.erase(it);
            else ++it;
        }
        std::cout << ']';
    }

    static const char* compact_phase_name(source_handle::CompactPhase phase)
    {
        switch (phase) {
        case source_handle::CompactPhase::idle: return "idle";
        case source_handle::CompactPhase::awaiting_callback: return "awaiting_callback";
        case source_handle::CompactPhase::waiting_ram_alarm: return "waiting_ram_alarm";
        case source_handle::CompactPhase::waiting_devcom: return "waiting_devcom";
        case source_handle::CompactPhase::awaiting_completion: return "awaiting_completion";
        case source_handle::CompactPhase::complete: return "complete";
        case source_handle::CompactPhase::rejected: return "rejected";
        }
        return "unknown";
    }

    static const char* transfer_kind_name(source_handle::TransferKind transfer)
    {
        switch (transfer) {
        case source_handle::TransferKind::none: return "none";
        case source_handle::TransferKind::ram_alarm: return "ram_alarm";
        case source_handle::TransferKind::devcom_1b: return "devcom_1b";
        }
        return "unknown";
    }

    void emit_compact_state(const source_handle::CompactState& state)
    {
        const auto& move = state.move;
        std::cout << ",\"compact\":{\"phase\":\"" << compact_phase_name(state.phase)
                  << "\",\"heap\":" << state.heap.value()
                  << ",\"callback\":" << state.callback.value()
                  << ",\"callback_arg\":" << state.callback_arg
                  << ",\"callback_handle\":" << state.callback_handle.value()
                  << ",\"callback_generation\":" << state.callback_generation
                  << ",\"cursor\":" << state.cursor.value()
                  << ",\"callback_invoked\":" << (state.callback_invoked ? "true" : "false")
                  << ",\"move\":{\"handle\":" << move.handle.value()
                  << ",\"source\":" << move.source.value()
                  << ",\"destination\":" << move.destination.value()
                  << ",\"next\":" << move.next.value()
                  << ",\"size\":" << move.size
                  << ",\"offset\":" << move.offset
                  << ",\"generation\":" << move.generation
                  << ",\"transfer\":\"" << transfer_kind_name(move.transfer)
                  << "\"}}";
    }

    void emit_compact_step(const std::string& op, const source_handle::CompactStepResult& result)
    {
        emit_begin(op, source_handle::status_name(result.status));
        std::cout << ",\"copied\":" << result.copied
                  << ",\"transfer_complete\":" << (result.transfer_complete ? "true" : "false")
                  << ",\"callback_invoked\":" << (result.callback_invoked ? "true" : "false");
        emit_compact_state(result.state);
        std::cout << "}\n";
    }

    void emit_pool(std::uint32_t id)
    {
        const auto& state = pool(id).state();
        const auto next = registry.next(pool_descriptors.at(id));
        std::cout << ",\"pool_words\":[0," << (state.free.empty() ? 0 : state.free.front().value())
                  << ',' << state.used << ',' << state.free.size() << ',' << state.peak
                  << ",4294967295,0,4294967295," << state.size << ',' << state.align_mask
                  << ',' << (next ? next->value() : 0) << ']'
                  << ",\"object_heap_words\":[0,0," << object_size << ',' << object_remain << ']';
    }

    void run(const std::string& line)
    {
        const auto op = field(line, "op");
        if (op == "configure") {
            require(os_limit == 0, "source context was already configured");
            os_limit = number(field(line, "heap_count"));
            require(os_limit > 0 && os_limit <= 32, "unsupported heap descriptor count");
            descriptor_base = number(field(line, "descriptor_base"));
            arena_start = number(field(line, "arena_start"));
            arena_end = number(field(line, "arena_end"));
            original_main_lo = hsd_lo = number(field(line, "main_lo"));
            original_main_hi = hsd_hi = number(field(line, "main_hi"));
            hsd_selected = static_cast<std::int32_t>(number(field(line, "initial_hsd_heap")));
            std::istringstream pool_values(field(line, "pools"));
            std::uint32_t id, address;
            while (pool_values >> id >> address) {
                require(id && !pool_descriptors.count(id), "duplicate pool definition");
                pool_descriptors[id] = Address(address);
            }
            std::istringstream values(field(line, "descriptors"));
            game_heap::Descriptor descriptor;
            while (values >> descriptor.index >> descriptor.type >> descriptor.previous >> descriptor.size)
                descriptors.push_back(descriptor);
            emit_begin(op); std::cout << "}\n";
            return;
        }
        if (op == "os_snapshot") {
            emit_begin(op); emit_os(); std::cout << "}\n"; return;
        }
        if (op == "bootstrap_heap") {
            const auto index = number(field(line, "index"));
            require(index <= 1, "unexpected bootstrap heap");
            const auto lo = index == 0 ? arena_start : original_main_lo;
            const auto hi = index == 0 ? original_main_lo : original_main_hi;
            const auto id = create_os(lo, hi);
            require(id == index, "bootstrap heap descriptor order differs");
            if (index == 1) pending_hsd = static_cast<std::int32_t>(id);
            emit_begin(op); std::cout << ",\"result\":" << id
                                      << ",\"args\":[" << lo << ',' << hi << ']';
            emit_os(); std::cout << "}\n"; return;
        }
        if (op == "os_select_hsd") {
            require(pending_hsd >= 0, "no independently created HSD heap to select");
            if (replacing_hsd) {
                require(replacement_phase == 3, "HSD replacement selection precedes creation");
                replacement_phase = 4;
            }
            hsd_selected = pending_hsd;
            const auto previous = os_selected;
            os_selected = hsd_selected;
            emit_begin(op); std::cout << ",\"result\":" << static_cast<std::uint32_t>(previous)
                                      << ",\"args\":[" << hsd_selected << "]}\n"; return;
        }
        if (op == "hsd_select") {
            const auto selected = number(field(line, "heap"));
            require(heap(selected).initialized(), "selected HSD heap has no lifetime");
            hsd_selected = static_cast<std::int32_t>(selected);
            emit_begin(op); std::cout << "}\n"; return;
        }
        if (op == "object_heap_set") {
            if (replacing_hsd) {
                require(replacement_phase == 4, "object heap reset precedes HSD selection");
                replacement_phase = 5;
            }
            object_size = object_remain = hsd_hi - hsd_lo;
            emit_begin(op); std::cout << ",\"args\":[" << object_size << ",0],\"object_heap_words\":[0,0," << object_size << "," << object_remain << "]}\n"; return;
        }
        if (op == "raw_alloc") {
            const auto id = number(field(line, "heap"));
            const auto result = heap(id).allocate(number(field(line, "requested")));
            if (result.status == Status::ok) {
                const auto label = field(line, "label");
                references[label] = result.address;
                reference_heaps[label] = id;
            }
            emit_begin(op, status_name(result.status).c_str());
            std::cout << ",\"address\":" << result.address.value();
            emit_os(); std::cout << "}\n"; return;
        }
        if (op == "raw_free") {
            const auto label = field(line, "label");
            const auto status = heap(number(field(line, "heap"))).release(reference(label));
            if (status == Status::ok) {
                references.erase(label);
                reference_heaps.erase(label);
            }
            emit_begin(op, status_name(status).c_str()); emit_os(); std::cout << "}\n"; return;
        }
        if (op == "pool_reset") {
            const auto id = number(field(line, "pool"));
            require(pool_descriptors.count(id) && hsd_selected >= 0, "pool context is undefined");
            auto& target = pools[id];
            if (!target) target = std::make_unique<ObjectPool>(heap(static_cast<std::uint32_t>(hsd_selected)));
            const auto status = target->reset(number(field(line, "size")), number(field(line, "align")));
            if (status == Status::ok) {
                pool_heaps[id] = static_cast<std::uint32_t>(hsd_selected);
                pool_backings.erase(id);
                for (auto it = reference_pools.begin(); it != reference_pools.end();) {
                    if (it->second == id) {
                        references.erase(it->first);
                        reference_heaps.erase(it->first);
                        it = reference_pools.erase(it);
                    } else {
                        ++it;
                    }
                }
                require(registry.initialize(pool_descriptors.at(id)) == Status::ok, "source pool registry reset failed");
            }
            emit_begin(op, status_name(status).c_str());
            if (status == Status::ok) emit_pool(id);
            std::cout << "}\n"; return;
        }
        if (op == "pool_registry_forget") {
            require(replacing_hsd, "pool registry forget lacks source replacement context");
            require(replacement_phase == 1 && !replacement_forgot_pools, "pool forget order differs");
            replacement_forgot_pools = true;
            registry.forget_memory();
            emit_begin(op); std::cout << ",\"args\":[" << hsd_lo << ',' << hsd_hi << "]}\n"; return;
        }
        if (op == "pool_adopt") {
            const auto id = number(field(line, "pool"));
            const auto count = number(field(line, "count"));
            const auto backing = reference(field(line, "label"));
            const auto backing_heap = number(field(line, "heap"));
            const auto status = pool(id).adopt_backing(heap(backing_heap), backing, count);
            if (status == Status::ok) {
                object_remain -= pool(id).state().size * count;
                pool_backings[id].push_back({backing_heap, backing,
                    std::uint64_t(pool(id).state().size) * count});
            }
            emit_begin(op, status_name(status).c_str());
            std::cout << ",\"result\":" << (status == Status::ok ? count : 0);
            if (status == Status::ok) emit_pool(id);
            std::cout << "}\n"; return;
        }
        if (op == "pool_pop") {
            const auto id = number(field(line, "pool"));
            const auto result = pool(id).allocate_existing();
            std::uint32_t backing_heap = 0;
            if (result.status == Status::ok) {
                const auto label = field(line, "label");
                const auto& backings = pool_backings[id];
                const auto found = std::find_if(backings.begin(), backings.end(),
                    [&](const PoolBacking& backing) {
                        const auto address = std::uint64_t(result.address.value());
                        return address >= backing.base.value() &&
                               address < std::uint64_t(backing.base.value()) + backing.bytes;
                    });
                const auto second = found == backings.end() ? found :
                    std::find_if(std::next(found), backings.end(),
                        [&](const PoolBacking& backing) {
                            const auto address = std::uint64_t(result.address.value());
                            return address >= backing.base.value() &&
                                   address < std::uint64_t(backing.base.value()) + backing.bytes;
                        });
                require(found != backings.end() && second == backings.end(),
                        "pool object lacks a unique derived backing heap");
                backing_heap = found->heap;
                references[label] = result.address;
                reference_heaps[label] = found->heap;
                reference_pools[label] = id;
            }
            emit_begin(op, status_name(result.status).c_str());
            std::cout << ",\"address\":" << result.address.value();
            if (result.status == Status::ok) {
                std::cout << ",\"backing_heap\":" << backing_heap;
            }
            emit_pool(id); std::cout << "}\n"; return;
        }
        if (op == "pool_release") {
            const auto id = number(field(line, "pool"));
            const auto label = field(line, "label");
            const auto status = pool(id).release(reference(label));
            if (status == Status::ok) {
                references.erase(label);
                reference_heaps.erase(label);
                reference_pools.erase(label);
            }
            emit_begin(op, status_name(status).c_str()); emit_pool(id); std::cout << "}\n"; return;
        }
        if (op == "game_init") {
            require(handles.initialized(), "ARAM handle root not initialized");
            const auto status = game.initialize({game_heap::Address(original_main_lo),
                game_heap::Address(original_main_hi), game_heap::Address(handles.arena().lo.value()),
                game_heap::Address(handles.arena().hi.value())}, descriptors);
            emit_begin(op, game_heap::status_name(status));
            if (status == game_heap::Status::ok) emit_game();
            std::cout << "}\n"; return;
        }
        if (op == "game_set") {
            const auto status = game.set_transient(number(field(line, "index")),
                static_cast<std::int32_t>(number(field(line, "value"))));
            emit_begin(op, game_heap::status_name(status));
            if (status == game_heap::Status::ok) emit_game();
            std::cout << "}\n"; return;
        }
        if (op == "game_begin") {
            const auto status = game.begin_rebuild();
            emit_begin(op, game_heap::status_name(status));
            if (status == game_heap::Status::ok) emit_request();
            std::cout << "}\n"; return;
        }
        if (op == "hsd_replace_begin") {
            const auto next = request(game_heap::RequestKind::replace_hsd_main);
            require(!replacing_hsd, "nested HSD main-heap replacement");
            replacing_hsd = true;
            replacement_phase = 1;
            replacement_forgot_pools = false;
            emit_begin(op); std::cout << ",\"args\":[" << next.lo.value() << ',' << next.hi.value()
                                      << "],\"old_bounds\":[" << hsd_lo << ',' << hsd_hi << "]}\n"; return;
        }
        if (op == "hsd_replace_destroy") {
            require(replacing_hsd && hsd_selected >= 0, "HSD heap replacement is not active");
            require(replacement_phase == 1 && replacement_forgot_pools, "HSD destruction order differs");
            replacement_phase = 2;
            destroy_os(static_cast<std::uint32_t>(hsd_selected));
            emit_begin(op); std::cout << ",\"args\":[" << hsd_selected << ']';
            emit_os(); std::cout << "}\n"; return;
        }
        if (op == "hsd_replace_create") {
            require(replacing_hsd, "HSD heap replacement is not active");
            require(replacement_phase == 2, "HSD creation precedes destruction");
            replacement_phase = 3;
            const auto next = request(game_heap::RequestKind::replace_hsd_main);
            hsd_lo = next.lo.value(); hsd_hi = next.hi.value();
            pending_hsd = static_cast<std::int32_t>(create_os(hsd_lo, hsd_hi));
            emit_begin(op); std::cout << ",\"result\":" << pending_hsd
                                      << ",\"args\":[" << hsd_lo << ',' << hsd_hi << ']';
            emit_os(); std::cout << "}\n"; return;
        }
        if (op == "hsd_replace_end") {
            require(replacing_hsd && hsd_selected == pending_hsd, "HSD replacement has not selected its new heap");
            require(replacement_phase == 5, "HSD replacement callbacks are unfinished");
            complete({game_heap::Status::ok, hsd_selected, {}});
            replacing_hsd = false;
            emit_begin(op); std::cout << ",\"result\":" << hsd_selected;
            emit_request(); std::cout << "}\n"; return;
        }
        if (op == "game_destroy_handle" || op == "game_destroy_current") {
            const auto current = op == "game_destroy_current";
            const auto next = request(current ? game_heap::RequestKind::destroy_current_handle
                                               : game_heap::RequestKind::destroy_handle);
            const auto status = current ? handles.destroy_current()
                : handles.destroy(HandleAddress(next.handle.value()));
            require(status == HandleStatus::ok, "source handle destruction failed");
            complete();
            emit_begin(op);
            emit_retired_payloads();
            if (!current) std::cout << ",\"args\":[" << next.handle.value() << ']';
            emit_request(); std::cout << "}\n"; return;
        }
        if (op == "game_new_handle" || op == "game_new_current") {
            const auto current = op == "game_new_current";
            const auto next = request(current ? game_heap::RequestKind::new_current_handle
                                               : game_heap::RequestKind::new_handle);
            const auto result = current
                ? handles.new_current(HandleAddress(next.lo.value()), HandleAddress(next.hi.value()))
                : handles.new_handle(HandleAddress(next.lo.value()), HandleAddress(next.hi.value()));
            require(result.status == HandleStatus::ok, "source handle creation failed");
            complete({game_heap::Status::ok, -1, game_heap::Address(result.handle.value())});
            handle_labels[field(line, "label")] = result.handle;
            if (current) handle_labels["current"] = result.handle;
            emit_begin(op); std::cout << ",\"result\":" << result.handle.value()
                                      << ",\"args\":[" << next.lo.value() << ',' << next.hi.value() << ']';
            emit_handle_words(result.handle); emit_request(); std::cout << "}\n"; return;
        }
        if (op == "game_end" || op == "game_snapshot") {
            if (op == "game_end") {
                require(!game.next_request().has_value(), "game heap rebuild is unfinished");
                const auto phase = handles.compact_state().phase;
                require(phase != source_handle::CompactPhase::awaiting_callback &&
                            phase != source_handle::CompactPhase::waiting_ram_alarm &&
                            phase != source_handle::CompactPhase::waiting_devcom &&
                            phase != source_handle::CompactPhase::awaiting_completion,
                        "source handle compaction is still pending");
            }
            emit_begin(op); emit_game(); std::cout << "}\n"; return;
        }
        if (op == "game_owner") {
            require(game.initialized(), "game owner context missing");
            const auto& owner = game.heap(number(field(line, "index")));
            require(owner.status == game_heap::HeapStatus::create, "game owner has no lifetime");
            emit_begin(op); std::cout << ",\"id\":" << static_cast<std::uint32_t>(owner.id)
                << ",\"type\":" << owner.type << ",\"handle\":" << owner.handle.value();
            if (owner.type != 0) emit_handle_words(HandleAddress(owner.handle.value()));
            std::cout << "}\n";
            return;
        }
        if (op == "game_handle_read") {
            const auto identity = handle_labels.at(field(line, "label"));
            emit_begin(op);
            std::cout << ",\"handle\":" << identity.value();
            emit_handle_words(identity);
            std::cout << "}\n";
            return;
        }
        if (op == "game_handle_alloc" || op == "game_handle_compact" || op == "game_handle_free") {
            const auto& owner = game.heap(number(field(line, "index")));
            require(owner.status == game_heap::HeapStatus::create && owner.type != 0,
                    "game heap is not a created handle owner");
            if (op == "game_handle_free") {
                const auto label = field(line, "label");
                const auto address = payload(label);
                const auto status = handles.free_payload(HandleAddress(owner.handle.value()), address);
                emit_begin(op, handle_status_name(status));
                if (status == HandleStatus::ok) {
                    emit_retired_payloads();
                    // A payload label and its handle label describe the same
                    // allocation result.  Both become stale after the source
                    // free, while the owning handle itself remains active.
                    // emit_retired_payloads normally removes these because
                    // the allocation descriptor is inactive; erase again so
                    // this stays safe if that diagnostic list is narrowed.
                    payload_labels.erase(label);
                    handle_labels.erase(label);
                }
                std::cout << ",\"owner\":" << owner.handle.value()
                          << ",\"payload\":" << address.value() << "}\n";
                return;
            }
            if (op == "game_handle_compact") {
                const auto status = handles.compact(HandleAddress(owner.handle.value()));
                emit_begin(op, handle_status_name(status));
                std::cout << ",\"result\":0,\"owner\":" << owner.handle.value() << "}\n"; return;
            }
            const auto result = handles.allocate(HandleAddress(owner.handle.value()),
                                                 number(field(line, "requested")));
            if (result.status == HandleStatus::ok) {
                handle_labels[field(line, "label")] = result.handle;
                payload_labels[field(line, "label")] = result.payload;
            }
            emit_begin(op, handle_status_name(result.status));
            std::cout << ",\"result\":" << result.handle.value() << ",\"payload\":" << result.payload.value()
                      << ",\"owner\":" << owner.handle.value() << ",\"owner_type\":" << owner.type;
            if (result.status == HandleStatus::ok) emit_handle_words(result.handle);
            std::cout << "}\n"; return;
        }
        if (op == "game_handle_compact_begin") {
            const auto index = number(field(line, "index"));
            const auto& owner = game.heap(index);
            require(owner.status == game_heap::HeapStatus::create && owner.type != 0,
                    "game heap is not a created handle owner");
            const auto result = handles.compact_begin(
                HandleAddress(owner.handle.value()),
                HandleAddress(number(field(line, "callback"))),
                number(field(line, "callback_arg")));
            emit_begin(op, source_handle::status_name(result.status));
            std::cout << ",\"result\":" << (result.started ? 1 : 0)
                      << ",\"owner\":" << owner.handle.value();
            emit_compact_state(result.state);
            std::cout << "}\n";
            return;
        }
        if (op == "game_handle_compact_callback") {
            const auto result = handles.compact_callback_transition(
                number(field(line, "generation")), field(line, "cancelled") == "1");
            if (result.status == HandleStatus::compact_started) {
                for (auto& item : payload_labels) {
                    if (handle_labels.at(item.first) == result.state.move.handle)
                        item.second = result.state.move.destination;
                }
            }
            emit_compact_step(op, result);
            return;
        }
        if (op == "game_handle_compact_ram_chunk") {
            const auto result = handles.compact_ram_alarm_chunk(
                number(field(line, "generation")));
            emit_compact_step(op, result);
            return;
        }
        if (op == "game_handle_compact_devcom_complete") {
            const auto result = handles.compact_devcom_complete(
                number(field(line, "generation")), field(line, "cancelled") == "1");
            emit_compact_step(op, result);
            return;
        }
        if (op == "game_handle_compact_observe") {
            const auto& state = handles.compact_state();
            require(state.phase != source_handle::CompactPhase::idle,
                    "source handle callback observed without compaction");
            emit_begin(op);
            emit_compact_state(state);
            std::cout << "}\n";
            return;
        }
        Model::run(line);
    }
};
} // namespace

int main()
{
    LifetimeModel model;
    std::string line;
    try {
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            model.run(line);
            std::cout.flush();
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
