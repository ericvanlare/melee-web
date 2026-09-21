#include "gameplay_audio_stream_asset.hpp"
#include <algorithm>
#include <cstdlib>
namespace melee_web {
static MeleeWebAudioChannel channel(const AudioChannel& c){
 MeleeWebAudioChannel d{};d.pcm=c.pcm.data();d.frames=c.pcm.size();d.current_nibble=c.current_nibble;d.end_nibble=c.end_nibble;d.loop_nibble=c.loop_nibble;d.looping=c.looping;
 std::copy(c.coefficients.begin(),c.coefficients.end(),d.coefficients);d.predictor_scale=c.predictor_scale;d.history1=c.history1;d.history2=c.history2;return d;
}
GameplayAudioStream::GameplayAudioStream(MeleeWebAudio* audio,const char* path,std::span<const uint8_t> bytes){
 GameplayAudioStreamFile file{path,bytes};initialize(audio,std::span<const GameplayAudioStreamFile>(&file,1));
}
GameplayAudioStream::GameplayAudioStream(MeleeWebAudio* audio,std::span<const GameplayAudioStreamFile> files){
 initialize(audio,files);
}
void GameplayAudioStream::initialize(MeleeWebAudio* audio,std::span<const GameplayAudioStreamFile> files){
 if(files.empty())throw DatError("HPS registry cannot be empty");
 decoded_.reserve(files.size());blocks_.reserve(files.size());paths_.reserve(files.size());inputs_.reserve(files.size());
 for(const auto& file:files){
  if(!file.path||!*file.path)throw DatError("HPS registry path is empty");
  paths_.emplace_back(file.path);decoded_.emplace_back(file.bytes,false);blocks_.emplace_back();
  const DatAudioStream& decoded=decoded_.back();auto& blocks=blocks_.back();const unsigned channels=decoded.channel_headers.size();
  for(const auto& b:decoded.blocks){MeleeWebAudioStreamBlock out{};out.offset=b.file_offset;out.size=b.payload_size;out.end=b.end_nibble;out.next=b.next_offset;out.channel_bytes=b.payload_size/channels;for(unsigned i=0;i<channels;i++){out.payload[i]=b.payloads[i].data();out.channel[i]=channel(b.channels[i]);}blocks.push_back(out);}
  MeleeWebAudioStreamInput input{};input.path=paths_.back().c_str();input.bytes=file.bytes.data();input.size=file.bytes.size();input.rate=decoded.sample_rate;input.channels=channels;
  for(unsigned i=0;i<channels;i++)input.header[i]=channel(decoded.channel_headers[i]);input.blocks=blocks.data();input.count=blocks.size();inputs_.push_back(input);
 }
 char error[256];stream_=melee_web_audio_stream_begin_registry(audio,inputs_.data(),inputs_.size(),error,sizeof(error));if(!stream_)throw DatError(error);
}
GameplayAudioStream::~GameplayAudioStream(){char error[256];if(stream_&&!melee_web_audio_stream_end(stream_,error,sizeof(error)))std::abort();}
}
