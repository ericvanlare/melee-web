#include "dat_fighter.hpp"
#include <algorithm>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace melee_web;
using Bytes = std::vector<uint8_t>;
namespace {
void check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F f) { try { f(); } catch (const DatError&) { return; } throw std::runtime_error("Expected DatError"); }
void put(Bytes& b, uint32_t p, uint32_t n) { for (int i = 0; i < 4; ++i) b.at(p + i) = uint8_t(n >> (24 - 8 * i)); }
struct Fixture {
    Bytes data = Bytes(288); std::vector<uint32_t> reloc;
    void link(uint32_t slot, uint32_t target) {
        put(data, slot, target);
        if (std::find(reloc.begin(), reloc.end(), slot) == reloc.end()) reloc.push_back(slot);
    }
    void unlink(uint32_t slot) { put(data, slot, 0); std::erase(reloc, slot); }
    Fixture() {
        // ftData+8 points to a relocated-zero FtPartsDesc, two groups, two
        // costume rows. Row1 falls back per category to the original row0.
        link(40, 0); put(data, 0, 2); link(4, 64);
        link(64, 128); link(68, 144); link(72, 192);
        put(data, 128, 1); link(132, 160);
        put(data, 136, 1); link(140, 168);
        put(data, 144, 1); link(148, 176);
        put(data, 192, 1); link(196, 208);
        put(data, 160, 2); link(164, 240); data[240] = 0; data[241] = 2;
        put(data, 168, 1); link(172, 244); data[244] = 4;
        put(data, 176, 2); link(180, 248); data[248] = 1; data[249] = 3;
        put(data, 208, 1); link(212, 252); data[252] = 7; // Separate metal-list index.
    }
    DatArchive archive() const {
        const auto publics = uint32_t(32 + data.size() + reloc.size() * 4);
        const std::string name = "fighter_data";
        Bytes bytes(publics + 8 + name.size() + 1);
        put(bytes, 0, uint32_t(bytes.size())); put(bytes, 4, uint32_t(data.size()));
        put(bytes, 8, uint32_t(reloc.size())); put(bytes, 12, 1);
        std::copy(data.begin(), data.end(), bytes.begin() + 32);
        for (uint32_t i = 0; i < reloc.size(); ++i) put(bytes, uint32_t(32 + data.size()) + i * 4, reloc[i]);
        put(bytes, publics, 32);
        std::copy(name.begin(), name.end(), bytes.begin() + publics + 8);
        return DatArchive(bytes);
    }
    DatFighterParts read(uint32_t costume = 0) const { return DatFighterParts(archive(), "fighter_data", costume); }
};
void normal_selection() {
    auto f = Fixture().read();
    check(f.descriptor_offset == 0 && f.root_offset == 32 && f.model_count == 2,
          "source root and relocated-zero descriptor retain identity");
    check(f.normal_dobj_indices(6) == std::vector<uint32_t>({0,2,4,5}),
          "normal categories override hidden alternates while unlisted DObjs stay visible");
    check(f.representations[2]->groups[0].variants[0].dobj_indices == std::vector<uint8_t>({7}),
          "metal indices belong to a separate array and are not applied to this costume");
    check(!f.representations[3], "null optional category stays absent");
}
void costume_fallback() {
    Fixture f;
    check(f.read(1).normal_dobj_indices(6) == f.read().normal_dobj_indices(6),
          "null costume category falls back independently to costume0");
    f.link(80, 144); // Costume1 normal category explicitly uses the alternate table.
    rejects([&] { (void) f.read(1).normal_dobj_indices(6); }); // Its group1 has zero variants.
    rejects([&] { (void) f.read(4); }); // Requested row crosses the next referenced data region.
    rejects([&] { (void) f.read(32); });
}
void ambiguous_variants() {
    Fixture f;
    f.data.resize(320);
    put(f.data, 128, 2); f.link(132, 288);
    put(f.data, 288, 2); f.link(292, 240);
    put(f.data, 296, 1); f.link(300, 244);
    const auto decoded = f.read();
    check(decoded.representations[0]->groups[0].variants.size() == 2, "decode retains multiple valid choices");
    rejects([&] { (void) decoded.normal_dobj_indices(6); });
    put(f.data, 128, 0); f.unlink(132);
    rejects([&] { (void) f.read().normal_dobj_indices(6); });
}
void counts_and_indices() {
    Fixture f;
    for (const auto n : {0U, 12U, 0xffffffffU}) { put(f.data, 0, n); rejects([&] { (void) f.read(); }); }
    f = Fixture(); put(f.data, 128, 129); rejects([&] { (void) f.read(); });
    f = Fixture(); put(f.data, 160, 125); rejects([&] { (void) f.read(); });
    f = Fixture(); f.data[240] = 124; rejects([&] { (void) f.read(); });
    f = Fixture(); f.data[252] = 32; rejects([&] { (void) f.read(); });
    f = Fixture(); f.data[248] = 6; rejects([&] { (void) f.read().normal_dobj_indices(6); });
    f = Fixture(); rejects([&] { (void) f.read().normal_dobj_indices(0); });
    rejects([&] { (void) f.read().normal_dobj_indices(125); });
}
void malformed_regions() {
    for (const auto slot : {40U, 4U, 132U, 164U}) {
        Fixture f; f.unlink(slot); rejects([&] { (void) f.read(); });
    }
    Fixture f; f.link(64, 129); rejects([&] { (void) f.read(); });
    f = Fixture(); f.link(164, 287); rejects([&] { (void) f.read(); });
    f = Fixture(); f.link(164, 243); rejects([&] { (void) f.read(); }); // Referenced index array ends at244.
    f = Fixture(); f.unlink(64); const auto no_normal = f.read();
    rejects([&] { (void) no_normal.normal_dobj_indices(6); });
    rejects([&] { (void) DatFighterParts(f.archive(), "missing", 0); });
}
}
int main(int argc, char** argv) {
    std::map<std::string, std::function<void()>> cases = {
        {"normal_selection", normal_selection}, {"costume_fallback", costume_fallback},
        {"ambiguous_variants", ambiguous_variants}, {"counts_and_indices", counts_and_indices},
        {"malformed_regions", malformed_regions}};
    if (argc != 2 || !cases.contains(argv[1])) return 2;
    try { cases.at(argv[1])(); } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
