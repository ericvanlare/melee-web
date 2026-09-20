#include "animation_fixture.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>

using namespace animation_test;

void preserves_channels()
{
    Fixture fixture;
    auto animation = fixture.animation();
    check(animation.node_counts == Bytes{0, 1} && animation.tracks.size() == 1,
          "empty nodes remain in the attachment ordering");
    check(animation.tree_type == 1 && animation.flags == 0 && animation.end_frame == 10 &&
          animation.tracks[0].type == 1 && animation.tracks[0].start_frame == 0,
          "FigaTree and FigaTrack metadata survives");
    check(animation.tracks[0].bytes == segment(2, 0, 10, 10),
          "packed little-endian stream at relocated offset zero is preserved byte-for-byte");
    fixture.data[0] = 0;
    check(animation.tracks[0].bytes[0] == 0x12, "typed animation owns its stream lifetime");
}

void tree_bounds()
{
    for (auto change : {0U, 4U, 8U}) {
        Fixture fixture;
        put32(fixture.data, Fixture::root + change, change == 8 ? 0x7f800000 : 0xffffffff);
        rejects([&] { (void) fixture.animation(); });
    }
    Fixture fixture;
    fixture.slots.pop_back();
    rejects([&] { (void) fixture.animation(); });
    fixture = Fixture();
    put32(fixture.data, Fixture::track + 8, 67);
    rejects([&] { (void) fixture.animation(); });
    fixture = Fixture();
    fixture.data[Fixture::nodes + 2] = 0; // No terminator before the data ends.
    rejects([&] { (void) fixture.animation(); });
    fixture = Fixture();
    fixture.data[Fixture::nodes + 1] = 128;
    rejects([&] { (void) fixture.animation(); });
    fixture = Fixture();
    fixture.data[Fixture::track + 7] = 1;
    check(fixture.animation().tracks[0].bytes == segment(2, 0, 10, 10),
          "unnamed FigaTrack alignment padding is ignored like lbAnim_InitFrames");
}

void unsupported_channels_and_formats()
{
    for (auto type : {0, 4, 11, 12, 20, 255}) {
        Fixture fixture(segment(2, 0, 10, 10), std::uint8_t(type));
        rejects([&] { (void) fixture.animation(); });
    }
    for (auto format : {1, 31, 63, 95, 127, 159, 160, 255}) {
        Fixture fixture;
        fixture.data[Fixture::track + 5] = std::uint8_t(format);
        rejects([&] { (void) fixture.animation(); });
    }
}

void native_action_channel_policy()
{
    Bytes node_stream = segment(2, 0, 10, 10);
    char error[256];
    for (std::uint8_t type : {11, 12}) {
        MeleeWebAnimationTrack track{node_stream.data(), node_stream.size(), 0, type, 0, 0};
        check(!melee_web_animation_validate_track(&track, error, sizeof(error)) && error[0],
              "generic inspection validation rejects source visibility channels");
        check(!melee_web_animation_validate_native_track(&track, error, sizeof(error)) && error[0],
              "generic native pose validation rejects source visibility channels");
        check(melee_web_animation_validate_native_action_track(&track, error, sizeof(error)) && !error[0],
              "native fighter action validation admits source visibility channels");
        Fixture fixture(node_stream, type);
        rejects([&] { (void) fixture.animation(); });
        const auto action = melee_web::DatAnimation(
            fixture.archive(), Fixture::root, melee_web::DatAnimationPolicy::NativeFighterAction);
        check(action.tracks.size() == 1 && action.tracks[0].type == type,
              "native fighter action policy retains the checked visibility track");
    }

    for (auto type : {0, 4, 20, 255}) {
        MeleeWebAnimationTrack unsupported{node_stream.data(), node_stream.size(), 0,
                                           std::uint8_t(type), 0, 0};
        check(!melee_web_animation_validate_native_action_track(&unsupported, error, sizeof(error)) && error[0],
              "native fighter action validation rejects unrelated channels");
    }
    const Bytes malformed{1, 0};
    MeleeWebAnimationTrack truncated{malformed.data(), malformed.size(), 0, 11, 0, 0};
    check(!melee_web_animation_validate_native_action_track(&truncated, error, sizeof(error)) && error[0],
          "native fighter action validation still rejects malformed node streams");
}

void malformed_operands()
{
    const std::vector<Bytes> invalid{
        {}, {0}, {7}, {0x81}, {0x81, 0x80, 0x80, 0x80},
        {1, 0, 0}, // truncated float
        {1, 0, 0, 0, 0, 0x80}, // truncated wait
        {1, 0, 0, 0, 0, 0x80, 0x80, 4}, // wait exceeds u16
        {0x21, 0, 0, 0, 0}, // advertises three values but stores one
        {1, 0, 0, 0, 0}, // no second interpolation datum
        {5, 0, 0, 0, 0}, // slope cannot precede a value
        {1, 0, 0, 0x80, 0x7f}, // infinity in packed little endian
        {4, 0, 0, 0, 0, 0, 0, 0xc0, 0x7f}, // NaN slope
    };
    for (const auto& stream : invalid) {
        const MeleeWebAnimationTrack track{stream.data(), stream.size(), 0, 1, 0, 0};
        char error[256];
        check(!melee_web_animation_validate_track(&track, error, sizeof(error)) && error[0],
              "malformed packed data fails before original interpolation reads it");
    }
}

void encoding_and_integer_edges()
{
    for (const auto format : {0x20, 0x40, 0x60, 0x80, 0x2f, 0x8f}) {
        Bytes bytes{0x11, 255};
        if ((format >> 5) <= 2) bytes.push_back(255);
        bytes.push_back(1);
        bytes.push_back(255);
        if ((format >> 5) <= 2) bytes.push_back(255);
        MeleeWebAnimationTrack track{bytes.data(), bytes.size(), 65535, 1, std::uint8_t(format), 0};
        char error[256];
        check(melee_web_animation_validate_track(&track, error, sizeof(error)),
              "supported fixed-point widths and signed start-frame bits are accepted");
    }
    Bytes bytes{0x11, 0, 0, 0, 0, 255, 255, 3, 0, 0, 0, 0}; // maximum HSD u16 wait
    MeleeWebAnimationTrack track{bytes.data(), bytes.size(), 0, 1, 0, 0};
    check(melee_web_animation_validate_track(&track, nullptr, 0), "maximum u16 wait is bounded and valid");
    bytes = {0x81, 1}; // 1 + (1 << 3): nine constant data in one extended packet
    for (unsigned i = 0; i < 9; ++i) {
        little_float(bytes, float(i));
        if (i != 8) bytes.push_back(1);
    }
    track.bytes = bytes.data(); track.length = bytes.size();
    check(melee_web_animation_validate_track(&track, nullptr, 0), "extended packet count uses HSD's three-bit first group");
}

void native_single_value_guard()
{
    auto native_valid=[](Bytes& bytes) {
        const MeleeWebAnimationTrack track{bytes.data(), bytes.size(), 0, 1, 0, 0};
        char error[256];
        return melee_web_animation_validate_native_track(&track, error, sizeof(error));
    };
    Bytes con{0x01}; little_float(con, 2.5f);
    Bytes lin{0x02}; little_float(lin, 2.5f);
    Bytes spl0{0x03}; little_float(spl0, 2.5f);
    Bytes key{0x06}; little_float(key, 2.5f);
    check(native_valid(con), "native terminal single-CON stream uses the guarded constant path");
    check(!native_valid(lin), "native terminal single-LIN stream cannot produce an interpolation value");
    check(!native_valid(spl0), "native terminal single-SPL stream cannot produce an interpolation value");
    check(native_valid(key), "native single-KEY stream retains source key semantics");
    for (auto* bytes : {&con, &lin, &spl0, &key}) {
        const MeleeWebAnimationTrack track{bytes->data(), bytes->size(), 0, 12, 0, 0};
        char error[256];
        const bool valid=melee_web_animation_validate_native_action_track(&track,error,sizeof(error));
        check(valid,
              "native fighter streams retain singleton bytes for guarded source execution");
    }
    Bytes delayed_zero{1,0,0};
    const MeleeWebAnimationTrack delayed{delayed_zero.data(),delayed_zero.size(),0xffde,12,0x88,0x88};
    char error[256];
    check(melee_web_animation_validate_native_action_track(&delayed,error,sizeof(error)),
          "Donkey delayed zero branch stream retains signed start bits and native CON policy");
}

int main(int argc, char** argv)
{
    const std::map<std::string, void (*)()> cases{
        {"preserves_channels", preserves_channels}, {"tree_bounds", tree_bounds},
        {"unsupported_channels_and_formats", unsupported_channels_and_formats},
        {"native_action_channel_policy", native_action_channel_policy},
        {"malformed_operands", malformed_operands}, {"encoding_and_integer_edges", encoding_and_integer_edges},
        {"native_single_value_guard", native_single_value_guard},
    };
    try {
        if (argc == 3 && std::string(argv[1]) == "local") {
            std::ifstream file(argv[2], std::ios::binary);
            Bytes bytes{std::istreambuf_iterator<char>(file), {}};
            melee_web::DatArchive archive(bytes);
            melee_web::DatAnimation animation(archive, archive.public_symbols().at(0).data_offset);
            std::cout << animation.node_counts.size() << " nodes, " << animation.tracks.size()
                      << " tracks, " << animation.end_frame << " frames\n";
        } else {
            if (argc != 2 || !cases.contains(argv[1])) return 2;
            cases.at(argv[1])();
        }
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
