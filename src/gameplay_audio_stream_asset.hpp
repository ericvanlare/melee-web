#pragma once
#include "dat_audio_stream.hpp"
#include "gameplay_audio_stream.h"
namespace melee_web {
class GameplayAudioStream {
public:
 GameplayAudioStream(MeleeWebAudio*,const char* original_path,std::span<const uint8_t>);
 ~GameplayAudioStream();
 GameplayAudioStream(const GameplayAudioStream&)=delete;
 GameplayAudioStream& operator=(const GameplayAudioStream&)=delete;
private:
 DatAudioStream decoded_;
 std::vector<MeleeWebAudioStreamBlock> blocks_;
 MeleeWebAudioStreamInput input_{};
 MeleeWebAudioStream* stream_=nullptr;
};
}
