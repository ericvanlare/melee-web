#include "dat_material.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatError;
using Bytes = std::vector<std::uint8_t>;

namespace {

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template <typename F> void rejects(F operation)
{
    try { operation(); }
    catch (const DatError&) { return; }
    throw std::runtime_error("expected DatError");
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> ((3 - i) * 8));
}

struct Fixture {
    static constexpr std::uint32_t mobj = 32;
    Bytes data = Bytes(64, 0);
    std::vector<std::uint32_t> slots = {mobj + 12};

    Fixture()
    {
        put32(data, 0, 0x01020304);
        put32(data, 4, 0x456789ab);
        put32(data, 8, 0xfedcba98);
        put32(data, 12, std::bit_cast<std::uint32_t>(1.F));
        put32(data, 16, std::bit_cast<std::uint32_t>(50.F));
        put32(data, mobj + 4, 0xc);
    }

    void link(std::uint32_t slot, std::uint32_t target)
    {
        put32(data, slot, target);
        if (std::find(slots.begin(), slots.end(), slot) == slots.end()) slots.push_back(slot);
    }

    DatArchive archive() const
    {
        const auto publics = 32 + data.size() + slots.size() * 4;
        Bytes bytes(publics + 10, 0);
        put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
        put32(bytes, 4, static_cast<std::uint32_t>(data.size()));
        put32(bytes, 8, static_cast<std::uint32_t>(slots.size()));
        put32(bytes, 12, 1);
        std::copy(data.begin(), data.end(), bytes.begin() + 32);
        for (std::size_t i = 0; i < slots.size(); ++i) put32(bytes, 32 + data.size() + i * 4, slots[i]);
        put32(bytes, publics, mobj);
        bytes[publics + 8] = 'm';
        return DatArchive(bytes);
    }
};

void preserves_material_inputs()
{
    const auto archive = Fixture().archive();
    const auto material = melee_web::read_dat_material(archive, Fixture::mobj);
    check(material.descriptor_offset == 32 && material.material_offset == 0,
          "material color descriptor may occupy relocated data offset zero");
    check(material.ambient == std::array<std::uint8_t, 4>{1, 2, 3, 4} &&
          material.diffuse == std::array<std::uint8_t, 4>{0x45, 0x67, 0x89, 0xab} &&
          material.specular == std::array<std::uint8_t, 4>{0xfe, 0xdc, 0xba, 0x98},
          "all original RGBA components survive without forcing alpha to255");
    check(material.alpha == 1 && material.shininess == 50 && material.render_mode == 0xc &&
          material.textures.empty(), "original opaque specular inputs");
    for (const auto flags : {0U, 1U, 2U, 4U, 5U, 8U, 0xcU, 0x14U, 0x1cU, 0x3cU, 0xfffU}) {
        Fixture fixture;
        put32(fixture.data, Fixture::mobj + 4, flags);
        const auto owner = fixture.archive();
        check(melee_web::read_dat_material(owner, Fixture::mobj).render_mode == flags,
              "supported source render bits stay intact, independently of texture-list presence");
    }
}

void unsupported_material_services()
{
    for (const auto slot : {Fixture::mobj, Fixture::mobj + 16, Fixture::mobj + 20}) {
        Fixture fixture;
        fixture.link(slot, 0);
        const auto archive = fixture.archive();
        rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    }
    for (const auto flags : {0x1000U, 0x2000U, 0x04000000U, 0x08000000U,
                            0x20000000U, 0x40000000U, 0x80000000U}) {
        Fixture fixture;
        put32(fixture.data, Fixture::mobj + 4, flags);
        const auto archive = fixture.archive();
        rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    }
}

void finite_material_parameters()
{
    for (const auto bits : {0x7f800000U, 0xff800000U, 0x7fc00001U}) {
        for (const auto offset : {12U, 16U}) {
            Fixture fixture;
            put32(fixture.data, offset, bits);
            const auto archive = fixture.archive();
            rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
        }
    }
    for (const auto alpha : {0.F, .5F, 2.F}) {
        Fixture fixture;
        put32(fixture.data, 12, std::bit_cast<std::uint32_t>(alpha));
        const auto archive = fixture.archive();
        rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    }
    for (const auto shine : {-1.F}) {
        Fixture fixture;
        put32(fixture.data, 16, std::bit_cast<std::uint32_t>(shine));
        const auto archive = fixture.archive();
        rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    }
}

void source_render_passes()
{
    using melee_web::DatMaterialPass;
    for (const auto& [flags, expected] : std::map<std::uint32_t, DatMaterialPass>{
        {0xcU, DatMaterialPass::Opaque}, {0x60000002U, DatMaterialPass::Translucent},
        {0x40000001U, DatMaterialPass::TextureEdge}}) {
        Fixture fixture;
        put32(fixture.data, Fixture::mobj + 4, flags);
        const auto archive = fixture.archive();
        check(melee_web::read_dat_material_pass(archive, Fixture::mobj) == expected,
              "original DObjLoad blend flags determine the pass before payload hydration");
    }
    Fixture invalid;
    put32(invalid.data, Fixture::mobj + 4, 0x20000001U);
    const auto archive = invalid.archive();
    rejects([&] { (void) melee_web::read_dat_material_pass(archive, Fixture::mobj); });
    for (const float shine : {129.F, 296.363586F, std::numeric_limits<float>::max()}) {
        Fixture fixture;
        put32(fixture.data, 16, std::bit_cast<std::uint32_t>(shine));
        const auto owner = fixture.archive();
        check(melee_web::read_dat_material(owner, Fixture::mobj).shininess == shine,
              "source shininess survives without an invented format cap or normalization");
    }
}

void material_pointer_bounds()
{
    Fixture fixture;
    fixture.slots.clear(); // A zero pointer becomes null if it has no relocation.
    auto archive = fixture.archive();
    rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    fixture = Fixture();
    fixture.link(Fixture::mobj + 12, 1);
    archive = fixture.archive();
    rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    fixture = Fixture();
    fixture.link(60, 16); // A referenced boundary splits the20-byte material.
    archive = fixture.archive();
    rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    fixture = Fixture();
    fixture.link(Fixture::mobj + 8, 60); // Required92-byte TObj cannot fit.
    archive = fixture.archive();
    rejects([&] { (void) melee_web::read_dat_material(archive, Fixture::mobj); });
    archive = Fixture().archive();
    rejects([&] { (void) melee_web::read_dat_material(archive, 33); });
    rejects([&] { (void) melee_web::read_dat_material(archive, 48); });
}

} // namespace

int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases = {
        {"preserves_material_inputs", preserves_material_inputs},
        {"unsupported_material_services", unsupported_material_services},
        {"finite_material_parameters", finite_material_parameters},
        {"material_pointer_bounds", material_pointer_bounds},
        {"source_render_passes", source_render_passes},
    };
    if (argc != 2 || !cases.contains(argv[1])) return 2;
    try { cases.at(argv[1])(); }
    catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
