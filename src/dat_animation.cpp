#include "dat_animation.hpp"

#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using melee_web::DatError;

void require(bool condition, const char* reason)
{
    if (!condition) throw DatError(reason);
}

bool srt_channel(std::uint8_t type)
{
    return (type >= 1 && type <= 3) || (type >= 5 && type <= 10);
}

class Stream {
public:
    explicit Stream(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool ended() const { return cursor_ == bytes_.size(); }
    std::uint8_t byte()
    {
        require(cursor_ < bytes_.size(), "Animation stream has a truncated operand");
        return bytes_[cursor_++];
    }
    void number(std::uint8_t format)
    {
        if (format == 0) {
            std::uint32_t bits = byte();
            for (unsigned shift = 8; shift <= 24; shift += 8)
                bits |= std::uint32_t(byte()) << shift;
            require(std::isfinite(std::bit_cast<float>(bits)), "Animation stream contains a nonfinite value");
        } else {
            (void) byte();
            if ((format >> 5) == 1 || (format >> 5) == 2) (void) byte();
        }
    }
    std::uint32_t continuation(std::uint32_t initial, unsigned shift, std::uint8_t previous)
    {
        std::uint32_t value = initial;
        while (previous & 0x80) {
            require(shift < 16, "Animation variable integer exceeds the HSD 16-bit field");
            previous = byte();
            value += std::uint32_t(previous & 0x7f) << shift;
            require(value <= 65535, "Animation variable integer exceeds the HSD 16-bit field");
            shift += 7;
        }
        return value;
    }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t cursor_ = 0;
};

void check_format(std::uint8_t format)
{
    // parseFloat uses a signed 1 << fraction denominator; fraction 31 is not a
    // positive fixed-point denominator and is deliberately outside this gate.
    require(format == 0 || ((format >> 5) >= 1 && (format >> 5) <= 4 && (format & 31) < 31),
            "Unsupported animation scalar encoding");
}

void validate(const MeleeWebAnimationTrack& track)
{
    require(srt_channel(track.type), "Unsupported animation channel (requires ordinary joint SRT)");
    require(track.bytes && track.length > 0 && track.length <= 65535,
            "Animation track length is outside the FigaTrack limit");
    check_format(track.value_format);
    check_format(track.slope_format);
    Stream stream({track.bytes, track.length});
    bool has_value = false;
    std::size_t operands = 0;
    unsigned first_opcode = 0;
    while (!stream.ended()) {
        const auto header = stream.byte();
        const unsigned opcode = header & 15;
        require(opcode >= 1 && opcode <= 6, "Unsupported animation opcode");
        if (!first_opcode) first_opcode = opcode;
        const auto count = stream.continuation(((header >> 4) & 7) + 1, 3, header);
        for (std::uint32_t i = 0; i < count; ++i) {
            ++operands;
            if (opcode == 5) {
                require(has_value, "Animation slope precedes its first value");
                stream.number(track.slope_format);
            } else {
                stream.number(track.value_format);
                has_value = true;
                if (opcode == 4) stream.number(track.slope_format);
                // HSD permits the last value to end at length without a wait.
                // Every earlier value is followed by an unsigned wait varint.
                if (!stream.ended()) {
                    const auto first = stream.byte();
                    (void) stream.continuation(first & 0x7f, 7, first);
                }
            }
            require(!stream.ended() || i + 1 == count,
                    "Animation packet ends before its advertised element count");
        }
    }
    require(has_value, "Animation track contains no value");
    // FObj assigns op_intrp when loading the next datum. A single non-key
    // datum reaches FObjUpdateAnim with op_intrp=NONE and an unset output.
    require(operands >= 2 || first_opcode == 6,
            "Animation interpolation requires a value pair or a key opcode");
}
} // namespace

extern "C" int melee_web_animation_validate_track(const MeleeWebAnimationTrack* track,
                                                   char* error, std::size_t error_size)
{
    try {
        require(track != nullptr, "Missing animation track");
        validate(*track);
        if (error && error_size) error[0] = '\0';
        return 1;
    } catch (const std::exception& exception) {
        if (error && error_size) std::snprintf(error, error_size, "%s", exception.what());
        return 0;
    }
}

namespace melee_web {
DatAnimation::DatAnimation(const DatArchive& archive, std::uint32_t root)
    : descriptor_offset(root)
{
    require((root & 3) == 0, "FigaTree descriptor is not aligned");
    (void) archive.range(root, 20);
    tree_type = archive.be32(root);
    flags = archive.be32(root + 4);
    end_frame = archive.f32(root + 8);
    require(tree_type <= 1, "Unsupported FigaTree type");
    require((flags & ~0x30000000U) == 0, "Unsupported FigaTree animation flags");
    require(std::isfinite(end_frame) && end_frame > 0 && end_frame <= max_frames,
            "Animation end frame must be finite and within the inspection limit");
    const auto nodes = archive.pointer(root + 12);
    const auto descriptors = archive.pointer(root + 16, 12);
    require(nodes && descriptors, "FigaTree requires node counts and tracks");
    require((*descriptors & 3) == 0, "FigaTrack array is not aligned");
    std::size_t total_tracks = 0;
    for (std::size_t node = 0;; ++node) {
        require(node <= max_nodes, "Animation node count exceeds the fighter part limit");
        const auto count = archive.range(*nodes + static_cast<std::uint32_t>(node), 1)[0];
        if (count == 255) break;
        require(node < max_nodes, "Animation node count exceeds the fighter part limit");
        require(count <= 9, "Animation node has too many ordinary SRT channels");
        node_counts.push_back(count);
        total_tracks += count;
    }
    require(!node_counts.empty() && total_tracks > 0 && total_tracks <= max_tracks,
            "Animation requires a bounded nonempty node/track list");
    (void) archive.range(*descriptors, total_tracks * 12);
    std::size_t total_bytes = 0, index = 0;
    for (const auto count : node_counts) {
        unsigned channels = 0;
        for (unsigned i = 0; i < count; ++i, ++index) {
            const auto offset = *descriptors + static_cast<std::uint32_t>(index * 12);
            const auto record = archive.range(offset, 12);
            const auto length = archive.be16(offset);
            DatAnimationTrack track{offset, archive.be16(offset + 2), record[4], record[5], record[6], {}};
            require(record[7] == 0, "FigaTrack reserved byte is nonzero");
            require(srt_channel(track.type), "Unsupported animation channel (requires ordinary joint SRT)");
            require(!(channels & (1U << track.type)), "Animation node has duplicate SRT channels");
            channels |= 1U << track.type;
            require(length > 0, "Animation track is empty");
            const auto bytes = archive.pointer(offset + 8, length);
            require(bytes.has_value(), "Animation track has no byte stream");
            total_bytes += length;
            require(total_bytes <= max_stream_bytes, "Animation streams exceed the memory limit");
            const auto source = archive.range(*bytes, length);
            track.bytes.assign(source.begin(), source.end());
            const MeleeWebAnimationTrack view{track.bytes.data(), track.bytes.size(), track.start_frame,
                track.type, track.value_format, track.slope_format};
            validate(view);
            tracks.push_back(std::move(track));
        }
    }
}
} // namespace melee_web
