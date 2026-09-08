#pragma once
#include "dat_audio.hpp"
#include <optional>
namespace melee_web {
struct AudioStreamBlock {
    uint32_t file_offset=0, payload_size=0, end_nibble=0, next_offset=0;
    // Source block predictor/history, independent of the preceding traversal.
    std::vector<AudioChannel> channels;
};
// Original HALPST container. Blocks retain their file offsets and channel DSP
// state so the original synth's three-slot transfer scheduler can consume them.
class DatAudioStream {
public:
    explicit DatAudioStream(std::span<const uint8_t> file);
    uint32_t sample_rate=0;
    std::vector<AudioChannel> channel_headers;
    std::vector<AudioStreamBlock> blocks;
    std::optional<size_t> loop_block;
    const AudioStreamBlock& block(uint32_t file_offset) const;
};
}
