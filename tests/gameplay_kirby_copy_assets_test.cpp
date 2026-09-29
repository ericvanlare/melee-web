// Exercise the private scalar adapter in its real translation unit. Unused
// runtime owners are garbage-collected; no HSD/runtime success stubs are linked.
#include "../src/gameplay_kirby_copy_assets.cpp"

#include <fstream>
#include <filesystem>
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

void put16(Bytes& bytes, std::uint32_t offset, std::uint16_t value)
{
    bytes.at(offset) = value >> 8;
    bytes.at(offset + 1) = value;
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
            const std::vector<unsigned>& costumes, unsigned body_models = 2)
{
    const auto original = input;
    const DatArchive archive(input, DatExternalPolicy::ResolveNull);
    const Bytes checked_before(archive.data().begin(), archive.data().end());
    auto bytes = input;
    adapt_for_native_source_parser(bytes, archive);
    adapt_kirby_copy_dynamics(bytes, archive, root);
    adapt_kirby_copy_added_parts_mask(bytes, archive, root);
    adapt_kirby_copy_parts_count(bytes, archive, root, costumes, body_models);
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
    put(fixture.data, 32, 0x1800); // public root+0x10: added-parts mask
    const auto input = fixture.bytes();
    const auto neutral = adapt(input, fixture.root, {0});
    check(native32(neutral, 32) == 0x1800,
          "Unrelocated Kirby added-parts mask was not converted to host order");
    check(native32(neutral, 192) == 2 && native32(neutral, 224) == 2,
          "Neutral hat counts were not converted");
    for (unsigned costume = 1; costume < 6; ++costume)
        check(adapt(input, fixture.root, {costume}) == neutral,
              "Joint-backed hat did not select the original row zero");
    check(adapt(input, fixture.root, {5, 1, 0, 3}) == neutral,
          "Multiple Kirby costumes changed shared hat adaptation");
}

void sheik_copy_dynamics()
{
    Fixture fixture(false);
    fixture.root.filename = "PlKbCpSk.dat";
    fixture.root.symbol = "ftDataKirbyCopySeak";
    fixture.root.fighter_kind = FTKIND_SEAK;
    fixture.link(36, 384); // KirbyHatStruct::hat_dynamics[2]
    put(fixture.data, 384, 1);
    fixture.link(388, 416); // ftDynamics::ftDynamicBones
    put(fixture.data, 416, 3); // BoneDynamicsDesc::bone_id
    put(fixture.data, 420, 0); // nullable DynamicsDesc::data
    put(fixture.data, 424, 2); // DynamicsDesc::count
    put(fixture.data, 428, 0x3f800000);
    put(fixture.data, 432, 0x3f000000);
    put(fixture.data, 436, 0xbe800000);

    const auto converted = adapt(fixture.bytes(), fixture.root, {0});
    check(native32(converted, 384) == 1 && native32(converted, 416) == 3 &&
              native32(converted, 424) == 2 &&
              native32(converted, 428) == 0x3f800000 &&
              native32(converted, 432) == 0x3f000000 &&
              native32(converted, 436) == 0xbe800000,
          "Sheik copy dynamics did not preserve authored PPC scalar values");

    put(fixture.data, 384, 10);
    rejects([&] { (void)adapt(fixture.bytes(), fixture.root, {0}); });
    put(fixture.data, 384, 1);
    std::erase(fixture.reloc, 388);
    rejects([&] { (void)adapt(fixture.bytes(), fixture.root, {0}); });
}

void source_copy_dynamics_rows()
{
    struct Case {
        unsigned kind;
        const char* filename;
        const char* symbol;
        std::uint32_t index;
    };
    static constexpr Case cases[] = {
        {FTKIND_SEAK, "PlKbCpSk.dat", "ftDataKirbyCopySeak", 2},
        {FTKIND_ZELDA, "PlKbCpZd.dat", "ftDataKirbyCopyZelda", 0},
        {FTKIND_KOOPA, "PlKbCpKp.dat", "ftDataKirbyCopyKoopa", 1},
        {FTKIND_LINK, "PlKbCpLk.dat", "ftDataKirbyCopyLink", 2},
        {FTKIND_CLINK, "PlKbCpCl.dat", "ftDataKirbyCopyClink", 2},
        {FTKIND_PIKACHU, "PlKbCpPk.dat", "ftDataKirbyCopyPikachu", 2},
        {FTKIND_PICHU, "PlKbCpPc.dat", "ftDataKirbyCopyPichu", 2},
        {FTKIND_MARS, "PlKbCpMs.dat", "ftDataKirbyCopyMars", 1},
        {FTKIND_MEWTWO, "PlKbCpMt.dat", "ftDataKirbyCopyMewtwo", 4},
        {FTKIND_PURIN, "PlKbCpPr.dat", "ftDataKirbyCopyPurin", 3},
        {FTKIND_EMBLEM, "PlKbCpFe.dat", "ftDataKirbyCopyEmblem", 1},
    };
    for (const auto& test : cases) {
        Fixture fixture(false);
        fixture.root.filename = test.filename;
        fixture.root.symbol = test.symbol;
        fixture.root.fighter_kind = test.kind;
        const auto descriptor = 16U + 0x0CU + test.index * 4U;
        std::erase(fixture.reloc, descriptor);
        fixture.link(descriptor, 384);
        put(fixture.data, 384, 1);
        fixture.link(388, 416);
        put(fixture.data, 416, 3);
        put(fixture.data, 424, 2);
        put(fixture.data, 428, 0x3f800000);
        put(fixture.data, 432, 0x3f000000);
        put(fixture.data, 436, 0xbe800000);

        const auto input = fixture.bytes();
        const DatArchive archive(input, DatExternalPolicy::ResolveNull);
        auto converted = input;
        adapt_kirby_copy_dynamics(converted, archive, fixture.root);
        check(native32(converted, 384) == 1 &&
                  native32(converted, 416) == 3 &&
                  native32(converted, 424) == 2 &&
                  native32(converted, 428) == 0x3f800000 &&
                  native32(converted, 432) == 0x3f000000 &&
                  native32(converted, 436) == 0xbe800000,
              "Source Kirby copy dynamics row was not converted");
    }

    Fixture unrelated(false);
    unrelated.root.filename = "unrelated.dat";
    unrelated.root.symbol = "ftDataKirbyCopySeak";
    unrelated.root.fighter_kind = FTKIND_SEAK;
    unrelated.link(36, 384);
    put(unrelated.data, 384, 1);
    const auto input = unrelated.bytes();
    const DatArchive archive(input, DatExternalPolicy::ResolveNull);
    auto unchanged = input;
    adapt_kirby_copy_dynamics(unchanged, archive, unrelated.root);
    check(read_be32(unchanged, 32U + 384U) == 1,
          "Kirby dynamics adapter converted an unowned source root");
}

void copy_effect_empty_source_bank()
{
    KirbyCopyEffectRequirement requirement;
    check(!copy_effect_requirement_from_source(FTKIND_YOSHI, 40, nullptr,
              nullptr, true, requirement),
          "Yoshi's source-authored empty Kirby effect bank must remain empty");
    check(copy_effect_requirement_from_source(FTKIND_FALCO, 32,
              "EfFxData.dat", "effFoxDataTable", true, requirement) &&
              requirement.filename == "EfFxData.dat" &&
              requirement.symbol == "effFoxDataTable" &&
              requirement.effect_bank == 32,
          "Complete source Kirby effect identity was not retained");
    rejects([&] { (void)copy_effect_requirement_from_source(
        FTKIND_YOSHI, 40, nullptr, "effYoshiDataTable", true, requirement); });
    rejects([&] { (void)copy_effect_requirement_from_source(
        FTKIND_YOSHI, 40, nullptr, nullptr, false, requirement); });
    rejects([&] { (void)copy_effect_requirement_from_source(
        FTKIND_YOSHI, 51, nullptr, nullptr, true, requirement); });
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

void source_costume_cache_rows()
{
    // G&W's six source rows alias one archive, but acquisition indexes the
    // cache by Kirby's body color. Archive deduplication must not drop rows.
    for (unsigned color = 1; color < 6; ++color) {
        const auto rows = copy_costume_requirements({FTKIND_GAMEWATCH}, {color});
        check(rows.size() == 2 && rows[0].costume_id == 0 &&
                  rows[1].costume_id == color,
              "Selected Kirby copy cache row is missing from preload");
        check(rows[1].source == &ftKb_Init_803CB3E8[FTKIND_GAMEWATCH][color],
              "Copy cache row lost its original source identity");
        check(std::strcmp(rows[0].source->dat_filename,
                          rows[1].source->dat_filename) == 0,
              "Expected original G&W archive alias");
        check(copy_archive_requirements({FTKIND_GAMEWATCH}, rows).size() == 3,
              "Aliased cache rows duplicated copy/joint/material roots");
    }
    const auto shared = copy_costume_requirements({FTKIND_GAMEWATCH}, {5, 1, 5});
    check(shared.size() == 3 && shared[1].costume_id == 5 &&
              shared[2].costume_id == 1,
          "Multiple Kirby selections lost or duplicated cache rows");
    check(copy_costume_requirements({FTKIND_FOX}, {1}).empty(),
          "Joint-backed Fox copy unexpectedly needs body-costume cache rows");
    rejects([] { copy_costume_requirements({FTKIND_GAMEWATCH}, {6}); });
    const auto neutral = kirby_copy_archive_requirements(
        std::vector<unsigned>{FTKIND_GAMEWATCH, FTKIND_FOX});
    check(neutral.size() == 4 && neutral[0].symbol == "ftDataKirbyCopyGamewatch" &&
              neutral[1].symbol == "PlyKirbyGw_Share_joint" &&
              neutral[2].symbol == "PlyKirbyGw_Share_matanim_joint" &&
              neutral[3].symbol == "ftDataKirbyCopyFox",
          "Neutral donor archive/root publication order changed");

    // Distinct filenames use the same owner contract: selected roots must
    // be imported before the original selected-color loader can resolve them.
    MeleeWebMenuMatchSelection selection{};
    for (auto& player : selection.start.players) player.slot_type = Gm_PKind_NA;
    selection.start.players[0].slot_type = Gm_PKind_Human;
    selection.start.players[0].ckind = CKIND_KIRBY;
    selection.start.players[0].color = 1;
    selection.start.players[1].slot_type = Gm_PKind_Human;
    selection.start.players[1].ckind = CKIND_DONKEY;
    const auto roots = kirby_copy_archive_requirements(selection);
    for (unsigned color : {0U, 1U}) {
        const auto& source = ftKb_Init_803CB3E8[FTKIND_DONKEY][color];
        for (const auto* symbol : {source.joint_name, source.matanim_joint_name})
            check(std::any_of(roots.begin(), roots.end(), [&](const auto& root) {
                return root.costume_root && root.filename == source.dat_filename &&
                       root.symbol == symbol && root.fighter_kind == FTKIND_DONKEY;
            }), "Selected source costume root is missing from the upload manifest");
    }
    check(roots.size() == 5, "Unselected source costume roots were requested");
}

void borrowed_signed_visibility_tail()
{
    Fixture fixture(false);
    fixture.root.fighter_kind = FTKIND_GAMEWATCH;
    fixture.link(40, 272); // source callback's secondary lookup, root + 0x18
    put(fixture.data, 272, 1); fixture.link(276, 224);
    fixture.link(280, 384); // next apparent count is an adjacent relocated pointer
    (void)adapt(fixture.bytes(), fixture.root, {0}, 2);
    rejects([&] { adapt(fixture.bytes(), fixture.root, {0}, 0); });
    std::erase(fixture.reloc, 280);
    // A shorter copy descriptor alone cannot justify omitting a body row.
    rejects([&] { adapt(fixture.bytes(), fixture.root, {0}, 2); });
    put(fixture.data, 280, 0);
    (void)adapt(fixture.bytes(), fixture.root, {0}, 2);
    put(fixture.data, 280, 0xffffffffU);
    (void)adapt(fixture.bytes(), fixture.root, {0}, 2);
    put(fixture.data, 280, 1);
    rejects([&] { adapt(fixture.bytes(), fixture.root, {0}, 2); });
}

void native_pobj_fields()
{
    Fixture fixture(false);
    constexpr std::uint32_t source = 400;
    put16(fixture.data, source + 12, 0xa001); // authored ENVELOPE + cull flag
    put16(fixture.data, source + 14, 0x0106); // 262 32-byte display records
    const auto input = fixture.bytes();
    const DatArchive checked(input, DatExternalPolicy::ResolveNull);
    Bytes converted = input;
    adapt_for_native_source_parser(converted, checked);

    MeleeWebNativePObjDesc pobj{};
    pobj.source_offset = source;
    pobj.geometry.flags = 0xa001;
    pobj.geometry.display_byte_size = 0x0106U * 32U;
    MeleeWebNativeGraph graph{};
    graph.pobjs = &pobj;
    graph.pobj_count = 1;
    std::unordered_set<std::uint32_t> adapted;
    adapt_native_source_pobj_fields(converted, checked, graph, adapted);
    check(native16(converted, source + 12) == 0xa001 &&
              native16(converted, source + 14) == 0x0106,
          "PObj native scalar fields were not converted");
    adapt_native_source_pobj_fields(converted, checked, graph, adapted);
    check(adapted.size() == 1, "Shared PObj descriptor was adapted twice");

    pobj.geometry.flags = 0;
    rejects([&] {
        std::unordered_set<std::uint32_t> invalid;
        adapt_native_source_pobj_fields(converted, checked, graph, invalid);
    });
}

void real_archive(const char* filename, const char* symbol, unsigned kind,
                  const char* body_filename)
{
    std::ifstream body_stream(body_filename, std::ios::binary);
    check(bool(body_stream), "Owned Kirby body archive unavailable");
    const Bytes body_input{std::istreambuf_iterator<char>(body_stream), {}};
    const DatArchive body(body_input, DatExternalPolicy::ResolveNull);
    const auto body_root = public_root(body, "ftDataKirby").data_offset;
    const auto body_parts = body.pointer(body_root + 8U, 8);
    check(bool(body_parts), "Owned Kirby FtPartsDesc unavailable");
    const auto body_models = body.be32(*body_parts);
    std::ifstream stream(filename, std::ios::binary);
    check(bool(stream), "Owned copy archive unavailable");
    const Bytes input{std::istreambuf_iterator<char>(stream), {}};
    const auto source_filename = std::filesystem::path(filename).filename().string();
    const KirbyCopyArchiveRequirement root{source_filename, symbol, kind, false};
    const DatArchive archive(input, DatExternalPolicy::ResolveNull);
    const auto offset = public_root(archive, root.symbol).data_offset;
    const bool joint = archive.has_relocation(offset);
    const auto neutral = adapt(input, root, {0}, body_models);
    if (!archive.has_relocation(offset + 0x10U))
        check(native32(neutral, offset + 0x10U) ==
                  archive.be32(offset + 0x10U),
              "Owned Kirby added-parts mask was not converted");
    if (kind == FTKIND_SEAK) {
        const auto dynamics = archive.pointer(offset + 0x14U, 8);
        check(bool(dynamics), "Owned Sheik copy dynamics pointer is missing");
        const auto count = archive.be32(*dynamics);
        check(native32(neutral, *dynamics) == count,
              "Owned Sheik copy dynamics count was not converted");
        if (count) {
            const auto bones = archive.pointer(
                *dynamics + 4U, std::size_t{count} * 24U);
            check(bool(bones), "Owned Sheik copy dynamic bones are missing");
            check(native32(neutral, *bones) == archive.be32(*bones) &&
                      native32(neutral, *bones + 8U) ==
                          archive.be32(*bones + 8U),
                  "Owned Sheik copy dynamic row scalars were not converted");
        }
    }
    for (unsigned costume = 0; costume < 6; ++costume) {
        const auto output = adapt(input, root, {costume}, body_models);
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
    (void)adapt(input, root, {1, 5, 0, 2}, body_models);
    if (kind == FTKIND_GAMEWATCH) {
        const auto lookup = *archive.pointer(offset + 0x18, body_models * 8);
        check(archive.be32(offset) == 1 && body_models == 2 &&
                  archive.has_relocation(lookup + 8),
              "Owned borrowed tail is not the audited pointer/count alias");
        std::cout << "Owned borrowed visibility: body_models=" << body_models
                  << " owner_models=1 tail_count_is_relocation=1\n";
    }
    if (kind == FTKIND_FALCO) {
        check(!archive.has_relocation(offset + 0x10U) &&
                  archive.be32(offset + 0x10U) == 0x1800U &&
                  native32(neutral, offset + 0x10U) == 0x1800U,
              "Owned Falco copy added-parts mask lost its source bits");
        std::cout << "Falco Kirby source added-parts mask=0x1800 converted=0x1800\n";
    }
    std::cout << symbol << ": all six costumes and shared selection passed\n";
}

void real_all_copy_archives(const char* directory, const char* body_filename)
{
    unsigned donor_count = 0;
    for (unsigned kind = 0; kind < FTKIND_MAX; ++kind) {
        const auto& source = ftKb_Init_803CA9D0[kind];
        if (!source.filename && !source.name) continue;
        check(source.filename && source.name,
              "Original Kirby copy table has a partial donor row");
        const std::string filename = std::string(directory) + "/" + source.filename;
        real_archive(filename.c_str(), source.name, kind, body_filename);
        ++donor_count;
    }
    check(donor_count == 25,
          "Original Kirby copy table donor count changed; update the explicit acceptance receipt");
    std::cout << "All " << donor_count
              << " non-null source Kirby copy rows passed structural adaptation for all six body costumes\n";
}
} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 4 && std::string(argv[1]) == "real_all_copy_archives")
            real_all_copy_archives(argv[2], argv[3]);
        else if (argc == 5) real_archive(argv[1], argv[2], std::stoul(argv[3]), argv[4]);
        else if (argc == 2) {
            const std::string name = argv[1];
            if (name == "joint_row_zero") joint_row_zero();
            else if (name == "sheik_copy_dynamics") sheik_copy_dynamics();
            else if (name == "source_copy_dynamics_rows") source_copy_dynamics_rows();
            else if (name == "copy_effect_empty_source_bank") copy_effect_empty_source_bank();
            else if (name == "costume_fallback") costume_fallback();
            else if (name == "malformed_consumed_rows") malformed_consumed_rows();
            else if (name == "source_costume_cache_rows") source_costume_cache_rows();
            else if (name == "borrowed_signed_visibility_tail") borrowed_signed_visibility_tail();
            else if (name == "native_pobj_fields") native_pobj_fields();
            else throw std::runtime_error("Unknown case");
            std::cout << name << ": passed\n";
        } else return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
