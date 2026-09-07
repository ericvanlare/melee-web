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
    rejects([&] { (void) fixture.animation(); });
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

int main(int argc, char** argv)
{
    const std::map<std::string, void (*)()> cases{
        {"preserves_channels", preserves_channels}, {"tree_bounds", tree_bounds},
        {"unsupported_channels_and_formats", unsupported_channels_and_formats},
        {"malformed_operands", malformed_operands}, {"encoding_and_integer_edges", encoding_and_integer_edges},
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
