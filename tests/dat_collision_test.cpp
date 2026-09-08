#include "dat_collision.hpp"
#include "dat_stage.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace melee_web;
using Bytes = std::vector<std::uint8_t>;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) {
    try { f(); } catch (const DatError&) { return; }
    throw std::runtime_error("Expected DatError");
}
void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = std::uint8_t(value >> 8); bytes.at(offset + 1) = std::uint8_t(value);
}
void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, std::uint16_t(value >> 16)); put16(bytes, offset + 2, std::uint16_t(value));
}
void putf(Bytes& bytes, std::size_t offset, float value) { put32(bytes, offset, std::bit_cast<std::uint32_t>(value)); }
struct Fixture {
    static constexpr std::uint32_t lines = 64, joint = 128, root = 192, stage = 256, entry = 320, binding = 384;
    Bytes data = Bytes(416);
    std::vector<std::uint32_t> relocations;
    bool has_scale = true;
    std::uint32_t scale_offset = 400;
    Fixture() {
        const std::array<DatCollisionVertex, 4> points = {{{-3.5f, 0}, {4.25f, 0}, {4.25f, -2}, {-3.5f, -2}}};
        for (std::size_t i = 0; i < points.size(); ++i) {
            putf(data, i * 8, points[i].x); putf(data, i * 8 + 4, points[i].y);
        }
        const std::array<std::array<std::uint16_t, 4>, 4> edges = {{{0,1,3,2}, {2,3,2,3}, {1,2,0,1}, {3,0,1,0}}};
        for (std::uint16_t i = 0; i < 4; ++i) {
            const auto d = lines + i * 16;
            for (std::size_t k = 0; k < 4; ++k) put16(data, d + k * 2, edges[i][k]);
            put16(data, d + 8, 0xffff); put16(data, d + 10, 0xffff);
            put16(data, d + 12, std::uint16_t(1U << i));
            for (const auto base : {root + 16, joint}) {
                put16(data, base + i * 4, i); put16(data, base + i * 4 + 2, 1);
            }
        }
        put16(data, lines + 14, 0x305); // Material low byte plus source platform/ledge bits.
        put16(data, lines + 8, 2); put16(data, lines + 10, 3);
        putf(data, joint + 20, -12); putf(data, joint + 24, -10);
        putf(data, joint + 28, 12); putf(data, joint + 32, 8);
        put16(data, joint + 38, 4);
        link(root, 0); put32(data, root + 4, 4);
        link(root + 8, lines); put32(data, root + 12, 4);
        link(root + 36, joint); put32(data, root + 40, 1);
        link(stage + 8, entry); put32(data, stage + 12, 1);
        putf(data, scale_offset, 2.5f);
    }
    void link(std::uint32_t slot, std::uint32_t target) {
        put32(data, slot, target);
        if (std::find(relocations.begin(), relocations.end(), slot) == relocations.end()) relocations.push_back(slot);
    }
    void unlink(std::uint32_t slot) { put32(data, slot, 0); std::erase(relocations, slot); }
    DatArchive archive() const {
        const std::string names("coll_data\0map_head\0grGroundParam\0", has_scale ? 33 : 19);
        const auto public_bytes = has_scale ? 24U : 16U;
        const auto public_offset = std::uint32_t(32 + data.size() + relocations.size() * 4);
        Bytes bytes(public_offset + public_bytes + names.size());
        put32(bytes, 0, std::uint32_t(bytes.size())); put32(bytes, 4, std::uint32_t(data.size()));
        put32(bytes, 8, std::uint32_t(relocations.size())); put32(bytes, 12, has_scale ? 3 : 2);
        std::copy(data.begin(), data.end(), bytes.begin() + 32);
        for (std::size_t i = 0; i < relocations.size(); ++i) put32(bytes, 32 + data.size() + i * 4, relocations[i]);
        put32(bytes, public_offset, root); put32(bytes, public_offset + 8, stage); put32(bytes, public_offset + 12, 10);
        if (has_scale) { put32(bytes, public_offset + 16, scale_offset); put32(bytes, public_offset + 20, 19); }
        std::copy(names.begin(), names.end(), bytes.begin() + public_offset + public_bytes);
        return DatArchive(bytes);
    }
    DatCollision read() const { return DatCollision(archive()); }
};
void source_stage_scale() {
    Fixture f;
    check(read_dat_stage_scale(f.archive()) == 2.5f, "named source GroundParam.y decoded without assuming one");
    f.has_scale = false; rejects([&] { (void) read_dat_stage_scale(f.archive()); });
    for (auto bits : {0U, 0xbf800000U, 0x7f800000U, 0x7fc00000U}) {
        f = Fixture(); put32(f.data, f.scale_offset, bits);
        rejects([&] { (void) read_dat_stage_scale(f.archive()); });
    }
    f = Fixture(); f.link(f.scale_offset, 0);
    rejects([&] { (void) read_dat_stage_scale(f.archive()); });
    f = Fixture(); f.scale_offset = 401;
    rejects([&] { (void) read_dat_stage_scale(f.archive()); });
    f = Fixture(); f.link(408, 402);
    rejects([&] { (void) read_dat_stage_scale(f.archive()); });
    f = Fixture(); f.data.resize(402);
    rejects([&] { (void) read_dat_stage_scale(f.archive()); });
}
void source_metadata() {
    Fixture f;
    put32(f.data, Fixture::root + 44, 0x10203040);
    const auto collision = f.read();
    check(collision.symbol == "coll_data" && collision.root_offset == Fixture::root && collision.vertex_offset == 0 &&
          collision.line_offset == Fixture::lines && collision.joint_offset == Fixture::joint,
          "source root and relocated zero vertex-array identities survive");
    check(collision.vertices.size() == 4 && collision.lines.size() == 4 && collision.joints.size() == 1 &&
          collision.vertices[0].x == -3.5f && collision.vertices[2].y == -2,
          "typed coordinates are copied exactly without stage scale or rendering transforms");
    check(collision.lines[0].prev0 == 3 && collision.lines[0].next0 == 2 && collision.lines[0].prev1 == 2 &&
          collision.lines[0].next1 == 3 && collision.lines[1].next1 == -1 &&
          collision.lines[0].hi_flags == 1 && collision.lines[0].lo_flags == 0x305 && collision.lines[0].terrain() == 5,
          "primary and alternate adjacency plus material/type words stay distinct");
    check(collision.line_ranges[2].start == 2 && collision.line_ranges[2].count == 1 &&
          collision.line_ranges[4].count == 0 && collision.joints[0].vertices.count == 4 &&
          collision.joints[0].left == -12 && collision.joints[0].top == 8 &&
          collision.source_reserved_2c == 0x10203040, "source category ranges, expanded joint bounds and inferred field preserved");
    // Degenerate source lines are legal input to original mpPruneEmptyLines.
    putf(f.data, 8, -3.5f); put16(f.data, Fixture::lines + 12, 0x81);
    const auto unpruned = f.read();
    check(unpruned.vertices[0].x == unpruned.vertices[1].x && unpruned.lines[0].prev0 == 3 &&
          unpruned.lines[0].next1 == 3 && unpruned.lines[0].hi_flags == 0x81,
          "decoder neither prunes zero-length lines nor rewrites their links");
    // The returned data owns every scalar, independent of the temporary archive.
    f.data.assign(f.data.size(), 0);
    check(collision.vertices[2].x == 4.25f && collision.lines[3].kind() == 8, "no archive-backed dangling spans");
}
void counts_and_arrays() {
    for (const auto [field, limit] : std::array<std::array<std::uint32_t,2>,3>{{{4,2048},{12,1536},{40,256}}})
        for (const auto count : {0U, limit + 1, 0xffffffffU}) {
            Fixture f; put32(f.data, Fixture::root + field, count); rejects([&] { (void) f.read(); });
        }
    for (const auto slot : {Fixture::root, Fixture::root + 8, Fixture::root + 36}) {
        Fixture f; f.unlink(slot); rejects([&] { (void) f.read(); });
        f = Fixture(); f.link(slot, 3); rejects([&] { (void) f.read(); });
        f = Fixture(); f.link(slot, 412); rejects([&] { (void) f.read(); });
    }
    Fixture f; f.link(408, Fixture::lines + 32);
    rejects([&] { (void) f.read(); }); // Referenced-region boundary bisects the required array.
    const auto archive = Fixture().archive();
    rejects([&] { (void) DatCollision(archive, "not_collision"); });
}
void indices_and_ranges() {
    for (const auto field : {0U, 2U}) {
        Fixture f; put16(f.data, Fixture::lines + field, 4); rejects([&] { (void) f.read(); });
    }
    for (const auto field : {4U, 6U, 8U, 10U}) for (const auto id : {4U, 0xfffeU}) {
        Fixture f; put16(f.data, Fixture::lines + field, std::uint16_t(id)); rejects([&] { (void) f.read(); });
    }
    for (const auto base : {Fixture::root + 16, Fixture::joint, Fixture::joint + 36}) {
        for (const auto field : {0U, 2U}) {
            Fixture f; put16(f.data, base + field, 0xffff); rejects([&] { (void) f.read(); });
            f = Fixture(); put16(f.data, base + field, 5); rejects([&] { (void) f.read(); });
        }
    }
    Fixture f; put16(f.data, Fixture::root + 20, 0); rejects([&] { (void) f.read(); }); // Overlap.
    f = Fixture(); put16(f.data, Fixture::root + 18, 0); rejects([&] { (void) f.read(); }); // Uninitialized gap.
    f = Fixture(); put16(f.data, Fixture::joint, 1); rejects([&] { (void) f.read(); }); // Joint category disagrees.
    f = Fixture(); put16(f.data, Fixture::lines + 12, 4); rejects([&] { (void) f.read(); }); // Static kind disagrees.
    f = Fixture(); put16(f.data, Fixture::joint + 38, 0); rejects([&] { (void) f.read(); }); // mpJointFromLine would return -1.
}
void finite_coordinates_and_bounds() {
    for (const auto bits : {0x7f800000U, 0xff800000U, 0x7fc00001U})
        for (const auto offset : {0U, 4U, Fixture::joint + 20, Fixture::joint + 24, Fixture::joint + 28, Fixture::joint + 32}) {
            Fixture f; put32(f.data, offset, bits); rejects([&] { (void) f.read(); });
        }
    Fixture f; putf(f.data, Fixture::joint + 20, 13); rejects([&] { (void) f.read(); });
    f = Fixture(); putf(f.data, Fixture::joint + 24, 9); rejects([&] { (void) f.read(); });
}
void dynamic_metadata_and_bindings() {
    Fixture f;
    // Reclassify the fourth record into the original dynamic range. Its raw
    // initial kind stays unchanged; only original mpJointUpdateDynamics may change it.
    for (const auto base : {Fixture::root + 16, Fixture::joint}) {
        put16(f.data, base + 14, 0); put16(f.data, base + 16, 3); put16(f.data, base + 18, 1);
    }
    f.link(Fixture::entry + 32, Fixture::binding); put32(f.data, Fixture::entry + 36, 1);
    put16(f.data, Fixture::binding + 2, 99); put16(f.data, Fixture::binding + 4, 2);
    const auto archive = f.archive(); const DatCollision collision(archive); const DatStage stage(archive);
    const auto bindings = read_dat_collision_bindings(archive, stage, collision);
    check(collision.line_ranges[4].start == 3 && collision.line_ranges[4].count == 1 && collision.lines[3].kind() == 8,
          "dynamic source partition preserved without inventing a collision update");
    check(bindings.size() == 1 && bindings[0].descriptor_offset == Fixture::binding && bindings[0].stage_entry == 0 &&
          bindings[0].collision_joint == 0 && bindings[0].source_stage_entry == 99 && bindings[0].render_joint == 2,
          "archive binding uses owning entry and preserves raw GrJoint.y plus original render index");
    put32(f.data, Fixture::entry + 36, 2);
    put16(f.data, Fixture::binding + 10, 3);
    const auto repeated = f.archive();
    const auto repeated_bindings = read_dat_collision_bindings(repeated, DatStage(repeated), DatCollision(repeated));
    check(repeated_bindings.size() == 2 && repeated_bindings[1].descriptor_offset == Fixture::binding + 6 &&
          repeated_bindings[1].collision_joint == 0 && repeated_bindings[1].render_joint == 3,
          "multiple source bindings of one collision joint retain their order without deduplication");
    auto over_budget = stage;
    over_budget.entries[0].collision_bindings.count = DatCollision::max_archive_bindings + 1;
    rejects([&] { (void) read_dat_collision_bindings(archive, over_budget, collision); });
    f = Fixture(); const auto empty_archive = f.archive();
    check(read_dat_collision_bindings(empty_archive, DatStage(empty_archive), DatCollision(empty_archive)).empty(),
          "no synthetic binding is inferred from an empty archive table");
    for (const auto field : {0U, 4U}) {
        f = Fixture(); f.link(Fixture::entry + 32, Fixture::binding); put32(f.data, Fixture::entry + 36, 1);
        put16(f.data, Fixture::binding + field, 0xffff);
        const auto invalid = f.archive();
        rejects([&] { (void) read_dat_collision_bindings(invalid, DatStage(invalid), DatCollision(invalid)); });
    }
    f = Fixture(); f.link(Fixture::entry + 32, Fixture::binding); put32(f.data, Fixture::entry + 36, 1);
    put16(f.data, Fixture::binding, 1); const auto outside = f.archive();
    rejects([&] { (void) read_dat_collision_bindings(outside, DatStage(outside), DatCollision(outside)); });
}
}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"source_metadata", source_metadata}, {"counts_and_arrays", counts_and_arrays},
        {"indices_and_ranges", indices_and_ranges}, {"finite_coordinates_and_bounds", finite_coordinates_and_bounds},
        {"dynamic_metadata_and_bindings", dynamic_metadata_and_bindings}, {"source_stage_scale", source_stage_scale}};
    if (argc != 2 || !cases.contains(argv[1])) return 2;
    try { cases.at(argv[1])(); } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
