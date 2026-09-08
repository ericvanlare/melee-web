#include "gameplay_audio_bank.hpp"
#include <algorithm>
#include <cstdlib>
namespace melee_web {
GameplayAudioBank::GameplayAudioBank(std::span<const uint8_t> sem,const std::vector<std::span<const uint8_t>>& banks,std::span<const uint8_t> coefficients):programs_(sem){
 if(coefficients.size()!=4096)throw DatError("DSP coefficient file must contain2048 big-endian halfwords");
 for(size_t i=0;i<coefficients.size();i+=2)coefficients_.push_back(int16_t(uint16_t(coefficients[i])<<8|coefficients[i+1]));
 for(auto bytes:banks)banks_.push_back(std::make_unique<DatAudioBank>(bytes));
 for(auto& bank:banks_)for(auto& sample:bank->samples){
  MeleeWebAudioSample out{};out.id=sample.id;out.rate=sample.sample_rate;out.channels=sample.channels.size();
  for(unsigned i=0;i<out.channels;i++){
   auto& c=sample.channels[i];auto& d=out.channel[i];d.pcm=c.pcm.data();d.frames=c.pcm.size();d.loop_pcm=c.loop_pcm.data();d.loop_frames=c.loop_pcm.size();
   d.current_nibble=c.current_nibble;d.end_nibble=c.end_nibble;d.loop_nibble=c.loop_nibble;
   std::copy(c.coefficients.begin(),c.coefficients.end(),d.coefficients);
   d.predictor_scale=c.predictor_scale;d.loop_predictor_scale=c.loop_predictor_scale;d.history1=c.history1;d.history2=c.history2;d.loop_history1=c.loop_history1;d.loop_history2=c.loop_history2;d.looping=c.looping;
  }
  samples_.push_back(out);
 }
 input_={samples_.data(),uint32_t(samples_.size()),programs_.words.data(),uint32_t(programs_.words.size()),programs_.tables[2].data(),uint32_t(programs_.tables[2].size()),programs_.tables[3].data(),uint32_t(programs_.tables[3].size()),coefficients_.data(),uint32_t(coefficients_.size())};
 char error[256];audio_=melee_web_audio_begin(&input_,error,sizeof(error));if(!audio_)throw DatError(error);
}
GameplayAudioBank::~GameplayAudioBank(){if(audio_){char error[256];if(!melee_web_audio_end(audio_,error,sizeof(error)))std::abort();}}
}
