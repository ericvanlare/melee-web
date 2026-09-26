// Exercise the private scalar adapter in its real translation unit. Unused
// runtime owners are garbage-collected; no HSD/runtime success stubs are linked.
#include "../src/gameplay_kirby_copy_assets.cpp"

#include <fstream>
#include <iostream>
#include <iterator>

using namespace melee_web;
using Bytes = std::vector<std::uint8_t>;

namespace {
void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void put(Bytes& bytes, std::uint32_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = value >> (24 - i * 8);
}

std::uint32_t native32(const Bytes& bytes, std::uint32_t offset)
{
    std::uint32_t value;
    std::memcpy(&value, bytes.data() + 32 + offset, sizeof(value));
    return value;
}

std::uint16_t native16(const Bytes& bytes, std::uint32_t offset)
{
    std::uint16_t value;
    std::memcpy(&value, bytes.data() + 32 + offset, sizeof(value));
    return value;
}

struct Fixture {
    Bytes data = Bytes(512);
    std::vector<std::uint32_t> reloc;
    KirbyCopyArchiveRequirement root{"synthetic.dat", "copy", FTKIND_FOX, false};

    void link(std::uint32_t slot, std::uint32_t target)
    {
        put(data, slot, target);
        reloc.push_back(slot);
    }

    explicit Fixture(bool joint)
    {
        const auto descriptor = joint ? 20U : 16U;
        if (joint) link(16, 384);
        put(data, descriptor, 1);
        link(descriptor + 4, 64);
        link(64, 192);
        link(68, 192);
        put(data, 192, 2); link(196, 224);
        put(data, 224, 2); link(228, 256);
        put(data, 232, 1); link(236, 258);
        data[256] = 0; data[257] = 1; data[258] = 2;
        if (joint) {
            // The next object is not a second costume row. Like PlKbCpFx,
            // a valid relocation leads to a non-pointer word at target+4.
            link(80, 384);
            put(data, 388, 0x20080008);
        } else {
            // Costume 1 falls back, costume 2 overrides only category zero.
            link(96, 200);
            put(data, 200, 1); link(204, 240);
            put(data, 240, 1); link(244, 264); data[264] = 3;
            // A texture list also falls back independently, per ftAnim_80070200.
            put(data, 24, 2); link(28, 320); link(320, 352);
            data[353] = 3; data[355] = 4;
        }
    }

    Bytes bytes() const
    {
        const auto publics = std::uint32_t(32 + data.size() + 4 * reloc.size());
        Bytes result(publics + 8 + root.symbol.size() + 1);
        put(result, 0, result.size()); put(result, 4, data.size());
        put(result, 8, reloc.size()); put(result, 12, 1);
        std::copy(data.begin(), data.end(), result.begin() + 32);
        for (unsigned i = 0; i < reloc.size(); ++i)
            put(result, 32 + data.size() + i * 4, reloc[i]);
        put(result, publics, 16);
        std::copy(root.symbol.begin(), root.symbol.end(), result.begin() + publics + 8);
        return result;
    }
};

Bytes adapt(const Bytes& input, const KirbyCopyArchiveRequirement& root,
            const std::vector<unsigned>& costumes)
{
    const auto original = input;
    const DatArchive archive(input, DatExternalPolicy::ResolveNull);
    const Bytes checked_before(archive.data().begin(), archive.data().end());
    auto bytes = input;
    adapt_for_native_source_parser(bytes, archive);
    adapt_kirby_copy_parts_count(bytes, archive, root, costumes);
    check(input == original, "Input archive changed");
    check(std::equal(checked_before.begin(), checked_before.end(), archive.data().begin()),
          "Checked archive changed");
    return bytes;
}

template<class Function> void rejects(Function function)
{
    try { function(); } catch (const DatError&) { return; }
    throw std::runtime_error("Expected malformed consumed data to be rejected");
}

void joint_row_zero()
{
    Fixture fixture(true);
    const auto input = fixture.bytes();
    const auto neutral = adapt(input, fixture.root, {0});
    check(native32(neutral, 192) == 2 && native32(neutral, 224) == 2,
          "Neutral hat counts were not converted");
    for (unsigned costume = 1; costume < 6; ++costume)
        check(adapt(input, fixture.root, {costume}) == neutral,
              "Joint-backed hat did not select the original row zero");
    check(adapt(input, fixture.root, {5, 1, 0, 3}) == neutral,
          "Multiple Kirby costumes changed shared hat adaptation");
}

void costume_fallback()
{
    Fixture fixture(false);
    // An unselected malformed row must not be interpreted as a pointer.
    put(fixture.data, 112, 0x12345678);
    const auto bytes = adapt(fixture.bytes(), fixture.root, {1});
    check(native32(bytes, 192) == 2 && native32(bytes, 224) == 2,
          "Null costume visibility did not adapt the row-zero fallback");
    check(native16(bytes, 352) == 3 && native16(bytes, 354) == 4,
          "Null costume texture list did not adapt the row-zero fallback");
    check(read_be32(bytes, 32 + 200) == 1,
          "Unselected costume override was converted");
    const auto overridden = adapt(fixture.bytes(), fixture.root, {2});
    check(native32(overridden, 200) == 1 && native32(overridden, 240) == 1 &&
              native32(overridden, 192) == 2,
          "Per-category override and fallback were not both adapted");
}

void malformed_consumed_rows()
{
    Fixture hat(true);
    std::erase(hat.reloc, 64); // Nonzero selected pointer still requires relocation.
    rejects([&] { adapt(hat.bytes(), hat.root, {1}); });
    Fixture parts(false);
    std::erase(parts.reloc, 64);
    rejects([&] { adapt(parts.bytes(), parts.root, {1}); });
    Fixture texture(false);
    std::erase(texture.reloc, 320);
    rejects([&] { adapt(texture.bytes(), texture.root, {1}); });
    Fixture selected(false);
    put(selected.data, 80, 0x12345678);
    rejects([&] { adapt(selected.bytes(), selected.root, {1}); });
    Fixture valid(true);
    rejects([&] { adapt(valid.bytes(), valid.root, {6}); });
    put(valid.data, 192, 129);
    rejects([&] { adapt(valid.bytes(), valid.root, {4}); });
}

void real_archive(const char* filename, const char* symbol, unsigned kind)
{
    std::ifstream stream(filename, std::ios::binary);
    check(bool(stream), "Owned copy archive unavailable");
    const Bytes input{std::istreambuf_iterator<char>(stream), {}};
    const KirbyCopyArchiveRequirement root{filename, symbol, kind, false};
    const DatArchive archive(input, DatExternalPolicy::ResolveNull);
    const auto offset = public_root(archive, root.symbol).data_offset;
    const bool joint = archive.has_relocation(offset);
    const auto neutral = adapt(input, root, {0});
    for (unsigned costume = 0; costume < 6; ++costume) {
        const auto output = adapt(input, root, {costume});
        if (joint) check(output == neutral, "Body costume changed joint-hat rows");
        else {
            // Independently check the authored row-zero fallback's scalar counts.
            const auto table = *archive.pointer(offset + 4, 96);
            for (unsigned category = 0; category < 4; ++category) {
                auto lookup = archive.pointer(table + costume * 16 + category * 4, 8);
                if (!lookup) lookup = archive.pointer(table + category * 4, 8);
                if (lookup)
                    check(native32(output, *lookup) == archive.be32(*lookup),
                          "Owned fallback count was not converted");
            }
        }
    }
    (void)adapt(input, root, {1, 5, 0, 2});
    std::cout << symbol << ": all six costumes and shared selection passed\n";
}
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 4) real_archive(argv[1], argv[2], std::stoul(argv[3]));
        else if (argc == 2) {
            const std::string name = argv[1];
            if (name == "joint_row_zero") joint_row_zero();
            else if (name == "costume_fallback") costume_fallback();
            else if (name == "malformed_consumed_rows") malformed_consumed_rows();
            else throw std::runtime_error("Unknown case");
            std::cout << name << ": passed\n";
        } else return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
