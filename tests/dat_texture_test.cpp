#include "dat_texture.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using melee_web::DatArchive;
using melee_web::DatError;
using melee_web::DatTexture;
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

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes.at(offset) = static_cast<std::uint8_t>(value >> 8);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    put16(bytes, offset, static_cast<std::uint16_t>(value >> 16));
    put16(bytes, offset + 2, static_cast<std::uint16_t>(value));
}

void putf32(Bytes& bytes, std::size_t offset, float value)
{
    put32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

// Independent byte-level specimen matching HSD TObjDesc/ImageDesc/TlutDesc/
// TexLODDesc. Pixel bytes begin at offset zero to exercise valid relocation-zero
// ownership. Padding leaves room for varied formats without embedding any game
// data. Tests supply explicit expected tiled sizes instead of reusing the reader.
struct Fixture {
    static constexpr std::uint32_t tobj = 0x20000, image = 0x20060,
        tlut = 0x20080, lod = 0x20090, palette = 0x20100;
    Bytes data = Bytes(palette + 32768, 0);
    std::vector<std::uint32_t> relocations;

    Fixture()
    {
        put32(data, tobj + 12, 4); // GX_TG_TEX0.
        for (std::uint32_t axis = 0; axis < 3; ++axis)
            putf32(data, tobj + 28 + axis * 4, 1.F);
        data[tobj + 60] = data[tobj + 61] = 1;
        put32(data, tobj + 64, 0x30010); // UV diffuse, BLEND endpoint one, alpha NONE.
        putf32(data, tobj + 68, 1.F);
        put32(data, tobj + 72, 1); // Linear magnification.
        link(tobj + 76, image);
        link(image, 0);
        dimensions(8, 32, 14); // CMPR, four 8x8 tiles = 128 bytes.
    }

    void link(std::uint32_t slot, std::uint32_t target)
    {
        put32(data, slot, target);
        if (std::find(relocations.begin(), relocations.end(), slot) == relocations.end())
            relocations.push_back(slot);
    }

    void unlink(std::uint32_t slot)
    {
        put32(data, slot, 0);
        std::erase(relocations, slot);
    }

    void dimensions(std::uint16_t width, std::uint16_t height, std::uint32_t format)
    {
        put16(data, image + 4, width);
        put16(data, image + 6, height);
        put32(data, image + 8, format);
    }

    void add_palette(std::uint16_t count, std::uint32_t format = 0)
    {
        link(tobj + 80, tlut);
        link(tlut, palette);
        put32(data, tlut + 4, format);
        put16(data, tlut + 12, count);
    }

    void add_lod(std::uint32_t filter = 5)
    {
        link(tobj + 84, lod);
        put32(data, lod, filter);
    }

    std::shared_ptr<const DatArchive> archive() const
    {
        const auto publics = 32 + data.size() + relocations.size() * 4;
        const auto names = publics + 8;
        Bytes bytes(names + 2, 0);
        put32(bytes, 0, static_cast<std::uint32_t>(bytes.size()));
        put32(bytes, 4, static_cast<std::uint32_t>(data.size()));
        put32(bytes, 8, static_cast<std::uint32_t>(relocations.size()));
        put32(bytes, 12, 1);
        std::copy(data.begin(), data.end(), bytes.begin() + 32);
        for (std::size_t i = 0; i < relocations.size(); ++i)
            put32(bytes, 32 + data.size() + i * 4, relocations[i]);
        put32(bytes, publics, tobj);
        bytes[names] = 't';
        return std::make_shared<DatArchive>(bytes);
    }

    // Keep the owner together with the returned view so tests cannot accidentally
    // rely on data from a destroyed archive.
    std::pair<std::shared_ptr<const DatArchive>, DatTexture> read(bool native = false) const
    {
        auto owner = archive();
        auto textures = melee_web::read_dat_texture_chain(*owner, tobj, native);
        check(textures.size() == 1, "single-texture fixture must retain one descriptor");
        auto texture = std::move(textures.front());
        return {std::move(owner), std::move(texture)};
    }
};

void real_shape_metadata()
{
    Fixture fixture;
    for (std::size_t i = 0; i < 128; ++i) fixture.data[i] = static_cast<std::uint8_t>(i);
    const auto [owner, texture] = fixture.read();
    check(texture.image.width == 8 && texture.image.height == 32 && texture.image.format == 14,
          "ordinary CMPR image metadata");
    check(texture.image.bytes.size() == 128 && texture.image.mip_levels == 1,
          "four GameCube tiles, not a linear RGBA size");
    check(texture.image.bytes.data() == owner->data().data() && texture.image.bytes[127] == 127,
          "unchanged tiles owned by the archive, including relocated target zero");
    check(texture.blending == 1 && texture.source_flags == 0x30010,
          "original HSD flags and blend inputs are preserved without approximation");
    check(texture.sampler.min_filter == 5 && texture.sampler.mag_filter == 1 &&
          !texture.palette, "HSD non-mipmapped default sampler");
}

void tiled_format_sizes()
{
    const std::array<std::pair<std::uint32_t, std::size_t>, 11> formats = {{
        {0, 64}, {1, 128}, {2, 128}, {3, 192}, {4, 192}, {5, 192},
        {6, 384}, {8, 64}, {9, 128}, {10, 192}, {14, 64},
    }};
    for (const auto& [format, expected] : formats) {
        Fixture fixture;
        fixture.dimensions(9, 5, format); // Deliberately crosses tile boundaries.
        if (format == 8 || format == 9 || format == 10) fixture.add_palette(1);
        const auto result = fixture.read();
        check(result.second.image.bytes.size() == expected, "format-specific padded tile size");
    }
}

void mip_chains()
{
    Fixture fixture;
    fixture.dimensions(8, 8, 6);
    put32(fixture.data, Fixture::image + 12, 1);
    putf32(fixture.data, Fixture::image + 20, 2.F);
    auto result = fixture.read();
    check(result.second.image.mip_levels == 3 && result.second.image.bytes.size() == 384,
          "RGBA8 8x8, 4x4 and 2x2 levels include tile padding independently");
    putf32(fixture.data, Fixture::image + 20, 1.75F);
    result = fixture.read();
    check(result.second.image.mip_levels == 2 && result.second.image.bytes.size() == 320,
          "fractional LOD follows Aurora's floor(maxLOD)+1 upload count");
    putf32(fixture.data, Fixture::image + 20, 3.F);
    result = fixture.read();
    check(result.second.image.bytes.size() == 448, "one-by-one mip still occupies a full tile");
    fixture.link(0x1fff0, 320); // Another referenced payload begins before all mip bytes end.
    rejects([&] { (void) fixture.read(); });
}

void palette_formats_and_counts()
{
    for (const auto& [format, limit] : std::array<std::pair<std::uint32_t, std::uint16_t>, 3>{
             {{8, 16}, {9, 256}, {10, 16384}}}) {
        for (std::uint32_t palette_format = 0; palette_format < 3; ++palette_format) {
            Fixture fixture;
            fixture.dimensions(8, 8, format);
            fixture.add_palette(limit, palette_format);
            const auto result = fixture.read();
            check(result.second.palette->bytes.size() == std::size_t{limit} * 2 &&
                  result.second.palette->format == palette_format, "validated palette word span");
            put16(fixture.data, Fixture::tlut + 12, static_cast<std::uint16_t>(limit + 1));
            rejects([&] { (void) fixture.read(); });
        }
    }
    Fixture fixture;
    fixture.dimensions(8, 8, 8);
    rejects([&] { (void) fixture.read(); });
    fixture.add_palette(0);
    rejects([&] { (void) fixture.read(); });
    fixture.add_palette(1, 3);
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    fixture.add_palette(1);
    rejects([&] { (void) fixture.read(); });
}

void palette_indices()
{
    for (const auto format : {8U, 9U, 10U}) {
        Fixture fixture;
        fixture.dimensions(1, 1, format);
        fixture.add_palette(1);
        if (format == 8) fixture.data[0] = 0x01; // Low nibble is an unused padded texel.
        else if (format == 9) fixture.data[1] = 1;
        else { put16(fixture.data, 0, 0xc000); put16(fixture.data, 2, 1); }
        (void) fixture.read();
        if (format == 8) fixture.data[0] = 0x10;
        else if (format == 9) fixture.data[0] = 1;
        else put16(fixture.data, 0, 0xc001);
        rejects([&] { (void) fixture.read(); });
    }
    Fixture fixture;
    fixture.dimensions(8, 8, 8);
    fixture.add_palette(1);
    put32(fixture.data, Fixture::image + 12, 1);
    putf32(fixture.data, Fixture::image + 20, 2.F);
    fixture.data[64] = 0x10; // First texel in the third mip, after two 32-byte tiles.
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    fixture.dimensions(9, 5, 9);
    fixture.add_palette(1);
    fixture.data[96] = 1; // First texel of the fourth C8 tile, x=8, y=4.
    rejects([&] { (void) fixture.read(); });
}

void lod_sampler()
{
    Fixture fixture;
    fixture.dimensions(8, 8, 8);
    fixture.add_palette(1);
    put32(fixture.data, Fixture::image + 12, 1);
    putf32(fixture.data, Fixture::image + 20, 2.F);
    auto result = fixture.read();
    check(result.second.sampler.min_filter == 5 && !result.second.lod_descriptor_offset,
          "source CI sampler default is preserved for original HSD adjustment");
    fixture.add_lod(4);
    putf32(fixture.data, Fixture::lod + 4, -.5F);
    fixture.data[Fixture::lod + 8] = fixture.data[Fixture::lod + 9] = 1;
    put32(fixture.data, Fixture::lod + 12, 2);
    result = fixture.read();
    check(result.second.sampler.min_filter == 4 && result.second.sampler.lod_bias == -.5F &&
          result.second.sampler.bias_clamp && result.second.sampler.edge_lod &&
          result.second.sampler.anisotropy == 2 && result.second.lod_descriptor_offset == Fixture::lod,
          "explicit LOD fields survive parsing");
    for (const auto& [offset, value] : std::array<std::pair<std::uint32_t, std::uint32_t>, 2>{
             {{0, 6}, {12, 3}}}) {
        auto invalid = fixture;
        put32(invalid.data, Fixture::lod + offset, value);
        rejects([&] { (void) invalid.read(); });
    }
    for (const auto offset : {8U, 9U}) {
        auto invalid = fixture;
        invalid.data[Fixture::lod + offset] = 2;
        rejects([&] { (void) invalid.read(); });
    }
    for (const auto bias : {-4.1F, 4.F}) {
        auto invalid = fixture;
        putf32(invalid.data, Fixture::lod + 4, bias);
        rejects([&] { (void) invalid.read(); });
    }
}

void operations_and_modes()
{
    for (std::uint32_t color = 0; color <= 8; ++color) {
        for (std::uint32_t alpha = 0; alpha <= 7; ++alpha) {
            Fixture fixture;
            const auto flags = (color << 16) | (alpha << 20) | 0x10;
            put32(fixture.data, Fixture::tobj + 64, flags);
            putf32(fixture.data, Fixture::tobj + 68, .25F);
            const auto result = fixture.read();
            check(result.second.source_flags == flags && result.second.blending == .25F,
                  "all standard HSD operations and intermediate blend inputs remain raw");
        }
    }
    for (const auto flags : {0x90010U, 0x830010U, 0x30012U, 0x30110U,
                            0x1030010U, 0x30000U, 0x10030010U}) {
        Fixture fixture;
        put32(fixture.data, Fixture::tobj + 64, flags);
        rejects([&] { (void) fixture.read(); });
    }
}

void native_bump_descriptor()
{
    Fixture fixture;
    constexpr std::uint32_t bump = 0x01040010; // TEX_BUMP, diffuse lightmap, UV/TEX0.
    put32(fixture.data, Fixture::tobj + 64, bump);
    const auto [owner, texture] = fixture.read(true);
    check(texture.source_flags == bump,
          "native texture hydration preserves the original TEX_BUMP source flags");
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    constexpr std::uint32_t bump_only = 0x01000000; // PlCo EntryStart accessory.
    put32(fixture.data, Fixture::tobj + 64, bump_only);
    const auto [bump_owner, bump_texture] = fixture.read(true);
    check(bump_texture.source_flags == bump_only,
          "native hydration preserves a bump-only TObj without inventing color operations");
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    put32(fixture.data, Fixture::tobj + 64, 0x00000000); // Non-bump flags lack required operations.
    rejects([&] { (void) fixture.read(true); });
    (void) bump_owner;
    for (const auto unsupported : {0x02040010U, 0x01040110U}) {
        put32(fixture.data, Fixture::tobj + 64, unsupported);
        rejects([&] { (void) fixture.read(true); });
    }
    (void) owner;
}

void unsupported_graphs_and_transforms()
{
    for (const auto offset : {0U, 4U, 88U}) {
        Fixture fixture;
        fixture.link(Fixture::tobj + offset, Fixture::tobj);
        rejects([&] { (void) fixture.read(); });
    }
    for (const auto& [offset, value] : std::array<std::pair<std::uint32_t, std::uint32_t>, 5>{
             {{8, 8}, {12, 0}, {52, 3}, {56, 3}, {72, 2}}}) {
        Fixture fixture;
        put32(fixture.data, Fixture::tobj + offset, value);
        rejects([&] { (void) fixture.read(); });
    }
    for (const auto offset : {16U, 28U, 40U}) {
        Fixture fixture;
        putf32(fixture.data, Fixture::tobj + offset, 1e20F);
        rejects([&] { (void) fixture.read(); });
    }
    for (const auto offset : {60U, 61U}) {
        Fixture fixture;
        fixture.data[Fixture::tobj + offset] = 0;
        rejects([&] { (void) fixture.read(); });
    }
}

void inactive_tev_descriptors()
{
    Fixture fixture;
    constexpr std::uint32_t tev = Fixture::palette;
    fixture.link(Fixture::tobj + 88, tev);
    // Disabled source expression bytes may contain enum-looking values; HSD
    // does not interpret them when active==0. Preserve descriptor identity.
    std::fill_n(fixture.data.begin() + tev, 28, 0xff);
    const auto result = fixture.read();
    check(result.second.inactive_tev_descriptor_offset == tev &&
          result.second.source_flags == 0x30010,
          "inactive custom expression retains identity and standard source behavior");
    check(!result.second.native_tev,"viewer does not construct native TEV state");
    const auto native=fixture.read(true);
    check(native.second.native_tev && native.second.native_tev->active==0 &&
          std::all_of(native.second.native_tev->fields.begin(),native.second.native_tev->fields.end(),
                      [](uint8_t value){return value==0xff;}),
          "native inactive TEV retains all uninterpreted source bytes");
    for (const auto active : {1U, 0x40000000U, 0x80000000U, 0xffffffffU}) {
        put32(fixture.data, tev + 28, active);
        rejects([&] { (void) fixture.read(); });
    }
    put32(fixture.data, tev + 28, 0);
    fixture.link(Fixture::tobj + 88, static_cast<std::uint32_t>(fixture.data.size() - 28));
    rejects([&] { (void) fixture.read(); });
    fixture.link(Fixture::tobj + 88, tev + 1);
    rejects([&] { (void) fixture.read(); });
    fixture.link(Fixture::tobj + 88, tev);
    fixture.link(Fixture::tobj + 84, tev + 16); // A referenced region truncates TEV.
    rejects([&] { (void) fixture.read(); });
}

void reflection_srt_and_chains()
{
    Fixture fixture;
    put32(fixture.data, Fixture::tobj + 8, 1);
    put32(fixture.data, Fixture::tobj + 12, 5); // Raw source is ignored by HSD reflection mode.
    put32(fixture.data, Fixture::tobj + 64, 0x30081); // Extension lightmap and reflection.
    putf32(fixture.data, Fixture::tobj + 68, .2F);
    putf32(fixture.data, Fixture::tobj + 16, .5F);
    putf32(fixture.data, Fixture::tobj + 28, 2.F);
    putf32(fixture.data, Fixture::tobj + 40, -3.F);
    put32(fixture.data, Fixture::tobj + 52, 2);
    put32(fixture.data, Fixture::tobj + 56, 2);
    fixture.data[Fixture::tobj + 60] = fixture.data[Fixture::tobj + 61] = 2;
    const auto result = fixture.read();
    check(result.second.source_flags == 0x30081 && result.second.blending == .2F &&
          result.second.rotation[0] == .5F && result.second.scale[0] == 2.F &&
          result.second.translation[0] == -3.F && result.second.repeat_t == 2 &&
          result.second.sampler.wrap_t == 2,
          "reflection inputs retain full SRT, mirror repeats and intermediate blend");

    fixture = Fixture();
    fixture.data.resize(0x29000, 0);
    auto previous = Fixture::tobj;
    for (std::uint32_t index = 1; index <= 8; ++index) {
        const auto next = 0x28000U + (index - 1) * 96;
        std::copy_n(fixture.data.begin() + Fixture::tobj, 92, fixture.data.begin() + next);
        put32(fixture.data, next + 4, 0);
        fixture.link(next + 76, Fixture::image);
        fixture.link(previous + 4, next);
        previous = next;
        const auto owner = fixture.archive();
        if (index < 8) {
            const auto chain = melee_web::read_dat_texture_chain(*owner, Fixture::tobj);
            check(chain.size() == index + 1 && chain.back().descriptor_offset == next,
                  "ordered chain preserves shared image payloads and source IDs");
        } else {
            rejects([&] { (void) melee_web::read_dat_texture_chain(*owner, Fixture::tobj); });
        }
    }
    fixture.unlink(previous + 76);
    fixture.link(Fixture::tobj + 4, Fixture::tobj); // A cycle fails independently of count.
    const auto cyclic = fixture.archive();
    rejects([&] { (void) melee_web::read_dat_texture_chain(*cyclic, Fixture::tobj); });
}

void dimensions_and_finite_values()
{
    for (const auto dimension : {0U, 1025U, 65535U}) {
        Fixture fixture;
        put16(fixture.data, Fixture::image + 4, static_cast<std::uint16_t>(dimension));
        rejects([&] { (void) fixture.read(); });
    }
    for (const auto format : {7U, 11U, 0xffffffffU}) {
        Fixture fixture;
        put32(fixture.data, Fixture::image + 8, format);
        rejects([&] { (void) fixture.read(); });
    }
    for (const auto offset : {Fixture::tobj + 16, Fixture::tobj + 28, Fixture::tobj + 40,
                            Fixture::tobj + 68, Fixture::image + 16, Fixture::image + 20}) {
        for (const auto bits : {0x7f800000U, 0x7fc00001U}) {
            Fixture fixture;
            put32(fixture.data, offset, bits);
            rejects([&] { (void) fixture.read(); });
        }
    }
    Fixture fixture;
    put32(fixture.data, Fixture::image + 12, 2);
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    putf32(fixture.data, Fixture::image + 20, 1.F);
    rejects([&] { (void) fixture.read(); });
    put32(fixture.data, Fixture::image + 12, 1);
    putf32(fixture.data, Fixture::image + 16, 2.F);
    rejects([&] { (void) fixture.read(); });
    putf32(fixture.data, Fixture::image + 16, 0.F);
    putf32(fixture.data, Fixture::image + 20, 6.F); // 8x32 only has six levels including base.
    rejects([&] { (void) fixture.read(); });
}

void pointers_and_region_bounds()
{
    for (const auto slot : {Fixture::tobj + 76, Fixture::image}) {
        Fixture fixture;
        fixture.unlink(slot);
        rejects([&] { (void) fixture.read(); });
    }
    Fixture fixture;
    fixture.link(Fixture::image, 1);
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    fixture.link(0x1fff0, 64); // Whole archive contains 128 bytes, referenced image region does not.
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    fixture.link(0x1fff0, Fixture::image + 20); // Split a fixed-size descriptor.
    rejects([&] { (void) fixture.read(); });
    fixture = Fixture();
    fixture.dimensions(8, 8, 8);
    fixture.add_palette(16);
    fixture.link(0x1fff0, Fixture::palette + 16);
    rejects([&] { (void) fixture.read(); });
}

} // namespace

int main(int argc, char** argv)
{
    const std::map<std::string, std::function<void()>> cases = {
        {"real_shape_metadata", real_shape_metadata}, {"tiled_format_sizes", tiled_format_sizes},
        {"mip_chains", mip_chains}, {"palette_formats_and_counts", palette_formats_and_counts},
        {"palette_indices", palette_indices}, {"lod_sampler", lod_sampler},
        {"operations_and_modes", operations_and_modes},
        {"native_bump_descriptor", native_bump_descriptor},
        {"reflection_srt_and_chains", reflection_srt_and_chains},
        {"inactive_tev_descriptors", inactive_tev_descriptors},
        {"unsupported_graphs_and_transforms", unsupported_graphs_and_transforms},
        {"dimensions_and_finite_values", dimensions_and_finite_values},
        {"pointers_and_region_bounds", pointers_and_region_bounds},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "Usage: dat_texture_test <known case name>\n";
        return 2;
    }
    try { cases.at(argv[1])(); }
    catch (const std::exception& error) {
        std::cerr << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    return 0;
}
