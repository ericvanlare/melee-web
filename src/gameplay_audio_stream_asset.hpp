#pragma once
#include "dat_audio_stream.hpp"
#include "gameplay_audio_stream.h"
#include <string>
namespace melee_web {
struct GameplayAudioStreamFile {
 const char* path;
 std::span<const uint8_t> bytes;
};
class GameplayAudioStream {
public:
 GameplayAudioStream(MeleeWebAudio*,const char* original_path,std::span<const uint8_t>);
 GameplayAudioStream(MeleeWebAudio*,std::span<const GameplayAudioStreamFile>);
 ~GameplayAudioStream();
 GameplayAudioStream(const GameplayAudioStream&)=delete;
 GameplayAudioStream& operator=(const GameplayAudioStream&)=delete;
private:
 std::vector<DatAudioStream> decoded_;
 std::vector<std::vector<MeleeWebAudioStreamBlock>> blocks_;
 std::vector<std::string> paths_;
 std::vector<MeleeWebAudioStreamInput> inputs_;
 MeleeWebAudioStream* stream_=nullptr;
 void initialize(MeleeWebAudio*,std::span<const GameplayAudioStreamFile>);
};
}
