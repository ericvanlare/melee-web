#include "fighter_binding.hpp"
#include "animation_fixture.hpp"
#include <functional>
#include <iostream>
#include <map>
using namespace melee_web;
using namespace animation_test;
namespace {
struct ArchiveFixture {
    Bytes data;
    std::vector<uint32_t> slots;
    std::string name;
    explicit ArchiveFixture(size_t size, std::string symbol) : data(size), name(std::move(symbol)) {}
    void link(uint32_t slot, uint32_t target) {
        put32(data, slot, target);
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
    }
    void unlink(uint32_t slot) { put32(data, slot, 0); std::erase(slots, slot); }
    DatArchive archive() const {
        const auto publics = uint32_t(32 + data.size() + slots.size() * 4);
        Bytes bytes(publics + 8 + name.size() + 1);
        put32(bytes, 0, uint32_t(bytes.size())); put32(bytes, 4, uint32_t(data.size()));
        put32(bytes, 8, uint32_t(slots.size())); put32(bytes, 12, 1);
        std::copy(data.begin(), data.end(), bytes.begin() + 32);
        for (size_t i = 0; i < slots.size(); ++i) put32(bytes, 32 + data.size() + i * 4, slots[i]);
        // Public root deliberately points at data offset zero.
        std::copy(name.begin(), name.end(), bytes.begin() + publics + 8);
        return DatArchive(bytes);
    }
};
const FighterCostume& mario() { return resolve_fighter_costume("PlyMario5K_Share_joint"); }
struct Common : ArchiveFixture {
    Common() : ArchiveFixture(340, "ftLoadCommonData") {
        link(16, 32); link(20, 164); link(32, 296);
        link(296, 308); link(300, 312); put32(data, 304, 2);
    }
    DatCommonFighterLayout read() const { return {archive(), mario()}; }
};
struct Actions : ArchiveFixture {
    static constexpr uint32_t table = 32;
    static constexpr uint32_t row(uint32_t id) { return table + id * 24; }
    Actions() : ArchiveFixture(7520, "ftDataMario") {
        link(12, table);
        const std::string wait = "source_exact_wait", walk = "source_exact_walk";
        std::copy(wait.begin(), wait.end(), data.begin() + 7320);
        std::copy(walk.begin(), walk.end(), data.begin() + 7360);
        for (auto id : {2U, 6U}) {
            link(row(id), 7320); put32(data, row(id) + 8, 64);
        }
        link(row(7), 7360); put32(data, row(7) + 4, 64); put32(data, row(7) + 8, 96);
        put32(data, row(6) + 16, 0x80000000);
    }
    DatFighterActions read() const { return {archive(), mario()}; }
};
FighterAnimationBinding bind(const DatCommonFighterLayout& co, const DatFighterActions& actions,
                            const DatAnimation& animation = Fixture().animation()) {
    return bind_fighter_animation(mario(), co, actions, mario().model_symbol, 2,
                                  "source_exact_wait", animation, 64);
}
void registry_identity() {
    check(fighter_kind_count() == 33 && fighter_costumes().size() > 100,
          "all source fighter kinds and costume rows are generated");
    const auto& fox = resolve_fighter_costume("PlyFox5K_Share_joint");
    check(fox.fighter_kind == 1 && fox.costume_index == 0 && fox.motion_count == 327 &&
          fox.fighter_symbol == "ftDataFox" && fox.animation_filename == "PlFxAJ.dat",
          "exact source registries bind model, data, motion count and container");
    check(mario().material_animation_symbol == "PlyMario5K_Share_matanim_joint",
          "material animation identity is the actual third source costume string");
    rejects([] { (void) resolve_fighter_costume("PlyFox5K_Share_ACTION_Wait1_figatree"); });
    const auto shared = std::find_if(fighter_costumes().begin(), fighter_costumes().end(),
                                   [](const auto& item) { return item.fighter_kind == 24; });
    check(shared != fighter_costumes().end(), "GameWatch fixture is present in source registry");
    rejects([&] { (void) resolve_fighter_costume(shared->model_symbol); });
    auto forged = mario(); forged.motion_count = 2;
    rejects([&] { (void) DatFighterActions(Actions().archive(), forged); });
}
void common_layout() {
    const auto common = Common().read();
    check(common.part_count == 2 && common.descriptor_offset == 296 &&
          !common.has_alternate_descriptor, "common table is indexed by source fighter kind");
    Common fixture;
    fixture.link(164, 316); fixture.link(316, 324); put32(fixture.data, 320, 1);
    fixture.data[324] = 1; fixture.data[325] = 0; fixture.data[326] = 0; fixture.data[327] = 255;
    const auto alternate = fixture.read();
    check(alternate.has_alternate_descriptor && alternate.alternate_parts.size() == 1 &&
          alternate.alternate_parts[0].source_joint == 255, "alternate metadata remains explicit");
    rejects([&] { (void) bind(alternate, Actions().read()); });
    fixture.data[324] = 2; rejects([&] { (void) fixture.read(); });
    for (auto count : {0U, 141U, 0xffffffffU}) {
        fixture = Common(); put32(fixture.data, 304, count); rejects([&] { (void) fixture.read(); });
    }
    fixture = Common(); fixture.unlink(16); rejects([&] { (void) fixture.read(); });
    fixture = Common(); fixture.link(32, 297); rejects([&] { (void) fixture.read(); });
    fixture = Common(); fixture.link(300, 340); rejects([&] { (void) fixture.read(); });
}
void action_membership() {
    const auto actions = Actions().read();
    check(actions.actions.size() == 3 && actions.actions[0].motion_id == 2 &&
          actions.actions[1].motion_id == 6 && actions.actions[2].motion_id == 7,
          "empty motion slots are filtered without renumbering source IDs");
    check(actions.find("source_exact_wait", 64).motion_id == 2 &&
          actions.actions[1].motion_flags == 0x80000000 && actions.contains("source_exact_walk"),
          "shared archive aliases retain distinct flags and exact source names");
    rejects([&] { (void) actions.find("source_exact_wait", 63); });
    rejects([&] { (void) actions.find("invented_prefix_ACTION_Wait", 64); });
    Actions fixture; put32(fixture.data, Actions::row(6) + 4, 32);
    rejects([&] { (void) fixture.read().find("source_exact_wait", 64); });
    for (auto field : {4U, 8U, 20U}) {
        fixture = Actions(); put32(fixture.data, Actions::row(0) + field, 32);
        rejects([&] { (void) fixture.read(); });
    }
    fixture = Actions(); fixture.link(Actions::row(0), 7320);
    rejects([&] { (void) fixture.read(); });
    fixture = Actions(); put32(fixture.data, Actions::row(2) + 8, 0x8001);
    rejects([&] { (void) fixture.read(); });
    fixture = Actions(); put32(fixture.data, Actions::row(2) + 4, 1);
    rejects([&] { (void) fixture.read(); });
    fixture = Actions(); fixture.unlink(Actions::row(2));
    rejects([&] { (void) fixture.read(); });
}
void container_slicing() {
    auto actions = Actions().read();
    Bytes container(160); container[64] = 42;
    actions.validate_container(container);
    const auto slice = actions.slice(container, 7);
    check(slice.size() == 96 && slice.data() == container.data() + 64 && slice[0] == 42,
          "selected clip is a bounded view of the exact source offset and length");
    rejects([&] { actions.validate_container(std::span(container).first(159)); });
    rejects([&] { (void) actions.slice(std::span(container).first(159), 7); });
    rejects([&] { (void) actions.slice(container, 0); });
    rejects([&] { actions.validate_container({}); });
    actions.actions[0].container_offset = 0xffffffff;
    rejects([&] { actions.validate_container(container); });
}
void binding_order() {
    auto common = Common().read(); auto actions = Actions().read();
    const auto binding = bind(common, actions);
    check(binding.animation_node_to_model_joint == std::vector<uint32_t>({0, 1}) &&
          binding.fighter_kind == 0 && binding.motion_id == 2,
          "ordinary present parts bind each FigaTree node in original preorder");
    ++common.part_count; rejects([&] { (void) bind(common, actions); });
    common = Common().read(); ++common.fighter_kind; rejects([&] { (void) bind(common, actions); });
    common = Common().read(); ++actions.fighter_kind; rejects([&] { (void) bind(common, actions); });
    actions = Actions().read(); auto animation = Fixture().animation();
    animation.node_counts.pop_back(); rejects([&] { (void) bind(common, actions, animation); });
    rejects([&] { (void) bind_fighter_animation(mario(), common, actions, "PlyFox5K_Share_joint",
                    2, "source_exact_wait", Fixture().animation(), 64); });
}
}
int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"registry_identity", registry_identity}, {"common_layout", common_layout},
        {"action_membership", action_membership}, {"container_slicing", container_slicing},
        {"binding_order", binding_order}};
    if (argc != 2 || !cases.contains(argv[1])) return 2;
    try { cases.at(argv[1])(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
