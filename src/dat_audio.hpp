#pragma once
#include "dat_archive.hpp"
#include <array>
namespace melee_web {
struct AudioChannel {
    std::array<int16_t,16> coefficients{};
    uint32_t current_nibble=0, end_nibble=0, loop_nibble=0;
    uint16_t predictor_scale=0, loop_predictor_scale=0;
    int16_t history1=0, history2=0, loop_history1=0, loop_history2=0;
    bool looping=false;
    // Initial traversal and a loop traversal decoded with its own saved DSP
    // history. Non-frame-aligned loops must not reuse the initial PCM slice.
    std::vector<int16_t> pcm, loop_pcm;
};
// Shared SSM/HPS DSP block decoder. The cumulative byte budget is bounded.
void decode_audio_adpcm(std::span<const uint8_t> payload,const AudioChannel& channel,
    uint32_t start,uint16_t predictor_scale,int16_t history1,int16_t history2,
    std::vector<int16_t>& output,size_t& byte_budget);
struct AudioSample {uint32_t id=0, sample_rate=0;std::vector<AudioChannel> channels;};
// Owned SSM DSP-ADPCM bank; preserves original sample IDs and rates. No playback,
// device, SEM interpretation, or gameplay sound-ID substitution is performed.
class DatAudioBank {
public:
    explicit DatAudioBank(std::span<const uint8_t> file);
    uint32_t base_id=0;
    std::vector<AudioSample> samples;
    const AudioSample& sample(uint32_t id) const;
};
}
