#pragma once
#include "dat_audio.hpp"
#include "dat_audio_programs.hpp"
#include "gameplay_audio.h"
#include <memory>
namespace melee_web {
// Keeps all decoded PCM and source descriptors alive through native publication.
// Four-tap PCM conversion uses the supplied DSP coefficient file. Original
// SEM selection, voice controls, envelopes and audio clock remain source.
class GameplayAudioBank {
public:
 GameplayAudioBank(std::span<const uint8_t> sem, const std::vector<std::span<const uint8_t>>& banks, std::span<const uint8_t> coefficients);
 GameplayAudioBank(std::span<const uint8_t> sem,
                   std::vector<std::shared_ptr<const DatAudioBank>> banks,
                   std::span<const uint8_t> coefficients);
 ~GameplayAudioBank();
 GameplayAudioBank(const GameplayAudioBank&)=delete;
 GameplayAudioBank& operator=(const GameplayAudioBank&)=delete;
 MeleeWebAudio* get() const {return audio_;}
private:
 DatAudioPrograms programs_;
 std::vector<int16_t> coefficients_;
 std::vector<std::shared_ptr<const DatAudioBank>> banks_;
 std::vector<MeleeWebAudioSample> samples_;
 MeleeWebAudioInput input_{};
 MeleeWebAudio* audio_=nullptr;
 void start(std::span<const uint8_t> coefficients);
};
}
