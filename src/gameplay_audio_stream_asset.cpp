#include "gameplay_audio_stream_asset.hpp"
#include <algorithm>
#include <cstdlib>
namespace melee_web {
static MeleeWebAudioChannel channel(const AudioChannel& c){
 MeleeWebAudioChannel d{};d.pcm=c.pcm.data();d.frames=c.pcm.size();d.current_nibble=c.current_nibble;d.end_nibble=c.end_nibble;d.loop_nibble=c.loop_nibble;d.looping=c.looping;
 std::copy(c.coefficients.begin(),c.coefficients.end(),d.coefficients);d.predictor_scale=c.predictor_scale;d.history1=c.history1;d.history2=c.history2;return d;
}
GameplayAudioStream::GameplayAudioStream(MeleeWebAudio* audio,const char* path,std::span<const uint8_t> bytes):decoded_(bytes,false){
 input_.path=path;input_.bytes=bytes.data();input_.size=bytes.size();input_.rate=decoded_.sample_rate;input_.channels=decoded_.channel_headers.size();
 for(unsigned i=0;i<input_.channels;i++)input_.header[i]=channel(decoded_.channel_headers[i]);
 for(const auto& b:decoded_.blocks){MeleeWebAudioStreamBlock out{};out.offset=b.file_offset;out.size=b.payload_size;out.end=b.end_nibble;out.next=b.next_offset;out.channel_bytes=b.payload_size/input_.channels;for(unsigned i=0;i<input_.channels;i++){out.payload[i]=b.payloads[i].data();out.channel[i]=channel(b.channels[i]);}blocks_.push_back(out);}
 input_.blocks=blocks_.data();input_.count=blocks_.size();char error[256];stream_=melee_web_audio_stream_begin(audio,&input_,error,sizeof(error));if(!stream_)throw DatError(error);
}
GameplayAudioStream::~GameplayAudioStream(){char error[256];if(stream_&&!melee_web_audio_stream_end(stream_,error,sizeof(error)))std::abort();}
}
