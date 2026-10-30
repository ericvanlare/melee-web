#include "gameplay_action_store.hpp"

#include <emscripten/heap.h>
#include <melee/lb/lbanim.h>

#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <malloc.h>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace melee_web;
using Bytes = std::vector<std::uint8_t>;
using Key = std::tuple<std::uint32_t, std::uint32_t, std::string>;

void check(bool condition, const std::string& message)
{
    if (!condition) throw DatError(message);
}

Bytes read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    check(bool(input), std::string("Cannot open actual Mario input: ") + path);
    const auto end = input.tellg();
    check(end > 0 && end <= std::streamoff(DatArchive::max_archive_bytes),
          std::string("Actual Mario input has invalid size: ") + path);
    Bytes result(static_cast<std::size_t>(end));
    input.seekg(0);
    check(bool(input.read(reinterpret_cast<char*>(result.data()), end)),
          std::string("Actual Mario input is truncated: ") + path);
    return result;
}

std::string json_string(const std::string& value)
{
    std::ostringstream out;
    out << '"';
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) out << "\\u" << std::hex << std::setw(4)
                               << std::setfill('0') << unsigned(ch) << std::dec;
            else out << static_cast<char>(ch);
        }
    }
    out << '"';
    return out.str();
}

std::string pointer_json(const void* pointer)
{
    if (!pointer) return "null";
    std::ostringstream out;
    out << json_string([&] {
        std::ostringstream value;
        value << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(pointer);
        return value.str();
    }());
    return out.str();
}

std::uint64_t add_bytes(std::uint64_t total, std::uint64_t amount,
                        const char* description)
{
    check(amount <= std::numeric_limits<std::uint64_t>::max() - total,
          std::string("Resident catalog byte total overflows: ") + description);
    return total + amount;
}

struct Memory {
    std::size_t wasm_heap = 0;
    std::int64_t allocator_arena = 0;
    std::int64_t allocator_live = 0;
    std::int64_t allocator_free = 0;
    std::int64_t allocator_top_free = 0;
};

Memory memory()
{
    const auto info = mallinfo();
    return {emscripten_get_heap_size(), static_cast<std::int64_t>(info.arena),
            static_cast<std::int64_t>(info.uordblks),
            static_cast<std::int64_t>(info.fordblks),
            static_cast<std::int64_t>(info.keepcost)};
}

struct RowFact {
    const DatRuntimeAction* source = nullptr;
    GameplayActionStore::ResidentCatalogTestRow resident{};
    std::uint32_t representative = UINT32_MAX;
};

struct CatalogEntry {
    Key key;
    std::uint32_t representative = UINT32_MAX;
    std::vector<std::uint32_t> motions;
    RowFact fact;
    std::uint64_t node_bytes = 0;
    std::uint64_t track_bytes = 0;
    std::uint64_t stream_bytes = 0;
    std::vector<std::uint16_t> stream_lengths;
};
} // namespace

int main(int argc, char** argv)
{
    using namespace melee_web;
    try {
        check(argc == 3, "Expected exactly PlMr.dat and PlMrAJ.dat paths");
        const auto& costume = resolve_fighter_costume("PlyMario5K_Share_joint");
        check(costume.fighter_kind == 0 && costume.costume_index == 0 &&
                  costume.fighter_filename == "PlMr.dat" &&
                  costume.animation_filename == "PlMrAJ.dat",
              "Canonical source costume did not resolve to base Mario");

        const auto fighter_bytes = read_file(argv[1]);
        const auto animation_bytes = read_file(argv[2]);
        auto archive = std::make_shared<const DatArchive>(fighter_bytes);
        const auto before = memory();
        const auto started = std::chrono::steady_clock::now();
        GameplayActionStore store(archive, costume, animation_bytes, {}, {}, nullptr, {},
                                  GameplayActionStorePolicy::ResidentOrdinaryActions);
        const auto stopped = std::chrono::steady_clock::now();
        const auto after = memory();
        const double constructor_ms =
            std::chrono::duration<double, std::milli>(stopped - started).count();

        check(store.interval_test_resident_catalog_ready(),
              "Actual Mario resident ordinary catalog is not ready");
        const auto& actions = store.runtime().actions();
        check(actions.size() == costume.motion_count,
              "Actual Mario runtime action rows differ from the authored costume bound");
        check(store.runtime().archive_actions().fighter_kind == costume.fighter_kind,
              "Runtime archive action identity differs from base Mario");

        std::map<Key, CatalogEntry> entries;
        std::vector<RowFact> rows(actions.size());
        std::uint64_t nonempty_rows = 0;
        std::uint64_t empty_rows = 0;
        std::uint64_t command_rows = 0;
        for (std::uint32_t motion = 0; motion < actions.size(); ++motion) {
            const auto& action = actions[motion];
            check(action.motion_id == motion,
                  "Runtime action table is not in authored motion order at row " +
                      std::to_string(motion));
            const Key key{action.container_offset, action.archive_bytes, action.symbol};
            auto [it, inserted] = entries.try_emplace(key);
            auto& entry = it->second;
            if (inserted) {
                entry.key = key;
                entry.representative = motion;
            }
            entry.motions.push_back(motion);
            rows[motion].source = &action;
            rows[motion].representative = entry.representative;
            if (action.archive_bytes) ++nonempty_rows;
            else ++empty_rows;
            if (action.command_offset) ++command_rows;
        }

        std::set<const void*> unique_clip_ptrs;
        std::set<const DatAnimation*> unique_animation_ptrs;
        std::set<const FigaTree*> unique_tree_ptrs;
        for (auto& [key, entry] : entries) {
            (void)key;
            const auto motion = entry.representative;
            auto& row = rows[motion];
            check(store.interval_test_resident_catalog_row(motion, &row.resident),
                  "Resident catalog query failed for authored row " +
                      std::to_string(motion));
            check(row.resident.representative_motion == motion,
                  "Resident catalog representative differs at authored row " +
                      std::to_string(motion));
            check(row.resident.clip != nullptr,
                  "Resident catalog has no retained Clip for authored row " +
                      std::to_string(motion));
            check(unique_clip_ptrs.insert(row.resident.clip).second,
                  "Distinct source keys unexpectedly share a resident Clip");

            entry.fact = row;
            if (!row.source->archive_bytes) {
                check(!row.resident.animation && !row.resident.tree,
                      "Authored empty row acquired a resident animation or native tree at row " +
                          std::to_string(motion));
                continue;
            }

            check(!row.source->symbol.empty(),
                  "Nonempty authored row has no source symbol at row " +
                      std::to_string(motion));
            check(row.resident.animation && row.resident.tree,
                  "Nonempty authored row lacks its decoded animation or native tree at row " +
                      std::to_string(motion));
            check(unique_animation_ptrs.insert(row.resident.animation).second,
                  "Distinct source keys unexpectedly share a decoded animation");
            check(unique_tree_ptrs.insert(row.resident.tree).second,
                  "Distinct source keys unexpectedly share a native FigaTree");

            const auto& animation = *row.resident.animation;
            const auto& tree = *row.resident.tree;
            check(tree.nodes && tree.tracks,
                  "Resident native tree has missing node or track storage at row " +
                      std::to_string(motion));
            check(animation.tree_type == static_cast<std::uint32_t>(tree.type) &&
                      animation.flags == tree.flags &&
                      std::bit_cast<std::uint32_t>(animation.end_frame) ==
                          std::bit_cast<std::uint32_t>(tree.frames),
                  "Native FigaTree scalar metadata differs from its retained animation");
            std::uint64_t node_track_count = 0;
            for (std::size_t node = 0; node < animation.node_counts.size(); ++node) {
                check(tree.nodes[node] >= 0 &&
                          static_cast<std::uint8_t>(tree.nodes[node]) == animation.node_counts[node],
                      "Native FigaTree node counts differ from retained animation");
                node_track_count = add_bytes(node_track_count, animation.node_counts[node],
                                             "node track count");
            }
            check(tree.nodes[animation.node_counts.size()] == -1,
                  "Native FigaTree node sentinel is missing");
            check(node_track_count == animation.tracks.size(),
                  "Retained animation track count differs from node descriptors");

            std::uint64_t stream_bytes = 0;
            entry.stream_lengths.reserve(animation.tracks.size());
            for (std::size_t index = 0; index < animation.tracks.size(); ++index) {
                const auto& decoded = animation.tracks[index];
                const auto& native = tree.tracks[index];
                check(decoded.bytes.size() <= UINT16_MAX &&
                          native.length == decoded.bytes.size() &&
                          native.startframe == decoded.start_frame &&
                          native.obj_type == decoded.type &&
                          native.frac_value == decoded.value_format &&
                          native.frac_slope == decoded.slope_format &&
                          native.ad_head == decoded.bytes.data(),
                      "Native FigaTrack no longer borrows its exact retained stream descriptor");
                entry.stream_lengths.push_back(native.length);
                stream_bytes = add_bytes(stream_bytes, decoded.bytes.size(),
                                         "retained stream bytes");
            }
            entry.node_bytes = animation.node_counts.size() + 1;
            entry.track_bytes = animation.tracks.size() * sizeof(FigaTrack);
            entry.stream_bytes = stream_bytes;
        }

        // Every authored row, including empty rows and aliases, must be queried
        // and resolve to the first row with its exact source key.
        for (std::uint32_t motion = 0; motion < actions.size(); ++motion) {
            auto& row = rows[motion];
            check(row.source != nullptr, "Authored row inventory has a hole");
            check(store.interval_test_resident_catalog_row(motion, &row.resident),
                  "Resident catalog query failed for authored row " +
                      std::to_string(motion));
            check(row.resident.representative_motion == row.representative,
                  "Resident alias representative is not the first exact source-key row at " +
                      std::to_string(motion));
            const auto& representative = rows[row.representative];
            if (row.representative != motion) {
                check(row.resident.clip == representative.resident.clip &&
                          row.resident.animation == representative.resident.animation &&
                          row.resident.tree == representative.resident.tree,
                      "Exact-key aliases do not share one resident Clip/animation/tree");
            }
            if (!row.source->archive_bytes)
                check(!row.resident.animation && !row.resident.tree,
                      "Empty authored row is not preserved as an empty resident selection");
        }

        std::uint64_t unique_native_keys = 0;
        std::uint64_t alias_rows = 0;
        std::uint64_t empty_alias_rows = 0;
        std::uint64_t alias_groups = 0;
        std::uint64_t nonempty_alias_groups = 0;
        std::uint64_t empty_key_groups = 0;
        std::uint64_t distinct_native_nodes = 0;
        std::uint64_t distinct_native_tracks = 0;
        std::uint64_t native_wrapper_tree_bytes = 0;
        std::uint64_t native_node_bytes = 0;
        std::uint64_t native_track_array_bytes = 0;
        std::uint64_t retained_stream_bytes = 0;
        std::ostringstream catalog_json;
        catalog_json << '[';
        bool first_entry = true;
        for (const auto& [key, entry] : entries) {
            const auto& [offset, size, symbol] = key;
            if (entry.motions.size() > 1) ++alias_groups;
            if (size) {
                if (entry.motions.size() > 1) ++nonempty_alias_groups;
                alias_rows = add_bytes(alias_rows, entry.motions.size() - 1, "nonempty alias rows");
            } else {
                ++empty_key_groups;
                empty_alias_rows = add_bytes(empty_alias_rows, entry.motions.size() - 1,
                                             "empty-key alias rows");
            }
            if (!first_entry) catalog_json << ',';
            first_entry = false;
            catalog_json << "{\"representative_motion\":" << entry.representative
                         << ",\"container_offset\":" << offset
                         << ",\"archive_bytes\":" << size
                         << ",\"native_action_key\":" << (size ? "true" : "false")
                         << ",\"symbol\":" << json_string(symbol)
                         << ",\"motions\":[";
            for (std::size_t i = 0; i < entry.motions.size(); ++i) {
                if (i) catalog_json << ',';
                catalog_json << entry.motions[i];
            }
            catalog_json << "]";
            if (size) {
                ++unique_native_keys;
                const auto& animation = *entry.fact.resident.animation;
                const auto& tree = *entry.fact.resident.tree;
                const auto tree_bytes = sizeof(FigaTree);
                const auto node_bytes = entry.node_bytes;
                const auto track_bytes = entry.track_bytes;
                native_wrapper_tree_bytes = add_bytes(native_wrapper_tree_bytes,
                    tree_bytes, "FigaTree and one-member clip wrapper bytes");
                native_node_bytes = add_bytes(native_node_bytes, node_bytes,
                                              "native node bytes");
                native_track_array_bytes = add_bytes(native_track_array_bytes, track_bytes,
                                                     "native FigaTrack array bytes");
                retained_stream_bytes = add_bytes(retained_stream_bytes,
                                                  entry.stream_bytes,
                                                  "retained stream bytes");
                distinct_native_nodes = add_bytes(distinct_native_nodes,
                    animation.node_counts.size(), "native node count");
                distinct_native_tracks = add_bytes(distinct_native_tracks,
                    animation.tracks.size(), "native track count");
                catalog_json << ",\"clip_ptr\":" << pointer_json(entry.fact.resident.clip)
                             << ",\"animation_ptr\":" << pointer_json(&animation)
                             << ",\"tree_ptr\":" << pointer_json(&tree)
                             << ",\"node_count\":" << animation.node_counts.size()
                             << ",\"track_count\":" << animation.tracks.size()
                             << ",\"node_counts\":[";
                for (std::size_t i = 0; i < animation.node_counts.size(); ++i) {
                    if (i) catalog_json << ',';
                    catalog_json << unsigned(animation.node_counts[i]);
                }
                catalog_json << "],\"track_stream_lengths\":[";
                for (std::size_t i = 0; i < entry.stream_lengths.size(); ++i) {
                    if (i) catalog_json << ',';
                    catalog_json << entry.stream_lengths[i];
                }
                catalog_json << "],\"retained_stream_bytes\":" << entry.stream_bytes
                             << ",\"native_clip_wrapper_bytes\":" << tree_bytes
                             << ",\"figa_tree_bytes\":" << tree_bytes
                             << ",\"native_node_bytes\":" << node_bytes
                             << ",\"native_figatrack_array_bytes\":" << track_bytes;
            } else {
                catalog_json << ",\"clip_ptr\":" << pointer_json(entry.fact.resident.clip)
                             << ",\"animation_ptr\":null,\"tree_ptr\":null"
                             << ",\"node_count\":0,\"track_count\":0"
                             << ",\"node_counts\":[],\"track_stream_lengths\":[]"
                             << ",\"retained_stream_bytes\":0"
                             << ",\"native_clip_wrapper_bytes\":0,\"figa_tree_bytes\":0"
                             << ",\"native_node_bytes\":0,\"native_figatrack_array_bytes\":0";
            }
            catalog_json << '}';
        }
        catalog_json << ']';

        std::ostringstream rows_json;
        rows_json << '[';
        for (std::uint32_t motion = 0; motion < actions.size(); ++motion) {
            if (motion) rows_json << ',';
            const auto& action = actions[motion];
            const auto& resident = rows[motion].resident;
            rows_json << "{\"motion\":" << motion
                      << ",\"container_offset\":" << action.container_offset
                      << ",\"archive_bytes\":" << action.archive_bytes
                      << ",\"symbol\":" << json_string(action.symbol)
                      << ",\"command_offset\":";
            if (action.command_offset) rows_json << *action.command_offset;
            else rows_json << "null";
            rows_json << ",\"representative_motion\":" << rows[motion].representative
                      << ",\"clip_ptr\":" << pointer_json(resident.clip)
                      << ",\"animation_ptr\":" << pointer_json(resident.animation)
                      << ",\"tree_ptr\":" << pointer_json(resident.tree)
                      << ",\"empty\":" << (action.archive_bytes ? "false" : "true")
                      << '}';
        }
        rows_json << ']';

        const auto signed_delta = [](std::int64_t after_value, std::int64_t before_value) {
            return after_value - before_value;
        };
        const auto heap_delta = static_cast<std::int64_t>(after.wasm_heap) -
                                static_cast<std::int64_t>(before.wasm_heap);
        std::cout << std::setprecision(17)
                  << "{\"schema\":\"gameplay-action-store-resident-catalog-probe-v1\""
                  << ",\"catalog_ready\":true,\"scope\":\"base Mario ordinary authored action domain\""
                  << ",\"fighter_kind\":" << costume.fighter_kind
                  << ",\"costume_index\":" << costume.costume_index
                  << ",\"model_symbol\":" << json_string(std::string(costume.model_symbol))
                  << ",\"motion_count\":" << costume.motion_count
                  << ",\"row_count\":" << actions.size()
                  << ",\"nonempty_rows\":" << nonempty_rows
                  << ",\"empty_rows\":" << empty_rows
                  << ",\"command_rows\":" << command_rows
                  << ",\"authored_archive_action_rows\":"
                  << store.runtime().archive_actions().actions.size()
                  << ",\"unique_native_keys\":" << unique_native_keys
                  << ",\"source_key_groups_including_empty\":" << entries.size()
                  << ",\"alias_rows\":" << alias_rows
                  << ",\"empty_key_alias_rows\":" << empty_alias_rows
                  << ",\"alias_groups_including_empty_keys\":" << alias_groups
                  << ",\"nonempty_alias_groups\":" << nonempty_alias_groups
                  << ",\"empty_key_groups\":" << empty_key_groups
                  << ",\"distinct_native_nodes\":" << distinct_native_nodes
                  << ",\"distinct_native_tracks\":" << distinct_native_tracks
                  << ",\"native_payload_bytes\":{\"native_clip_wrapper_bytes\":"
                  << native_wrapper_tree_bytes
                  << ",\"figa_tree_bytes\":" << native_wrapper_tree_bytes
                  << ",\"native_node_bytes\":" << native_node_bytes
                  << ",\"native_figatrack_array_bytes\":" << native_track_array_bytes
                  << ",\"retained_stream_bytes\":" << retained_stream_bytes
                  << ",\"native_owned_bytes\":"
                  << add_bytes(add_bytes(native_wrapper_tree_bytes, native_node_bytes,
                                         "native owned bytes"), native_track_array_bytes,
                               "native owned bytes")
                  << "},\"constructor_measurement\":{\"elapsed_ms\":" << constructor_ms
                  << ",\"wasm_heap_before_bytes\":" << before.wasm_heap
                  << ",\"wasm_heap_after_bytes\":" << after.wasm_heap
                  << ",\"wasm_heap_delta_bytes\":" << heap_delta
                  << ",\"allocator_arena_before_bytes\":" << before.allocator_arena
                  << ",\"allocator_arena_after_bytes\":" << after.allocator_arena
                  << ",\"allocator_live_before_bytes\":" << before.allocator_live
                  << ",\"allocator_live_after_bytes\":" << after.allocator_live
                  << ",\"allocator_live_delta_bytes\":"
                  << signed_delta(after.allocator_live, before.allocator_live)
                  << ",\"allocator_free_before_bytes\":" << before.allocator_free
                  << ",\"allocator_free_after_bytes\":" << after.allocator_free
                  << ",\"allocator_top_free_before_bytes\":" << before.allocator_top_free
                  << ",\"allocator_top_free_after_bytes\":" << after.allocator_top_free
                  << ",\"scope_note\":\"Whole GameplayActionStore constructor; the fighter DatArchive and animation container are read before timing. Includes runtime, commands, rows, and resident catalog setup; allocator delta is not attributed per catalog row.\"}"
                  << ",\"catalog_entries\":" << catalog_json.str()
                  << ",\"rows\":" << rows_json.str() << "}\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
