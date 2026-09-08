#include "dat_audio.hpp"
#include <algorithm>
#include <limits>
namespace melee_web {
namespace {
void require(bool ok,const char* why){if(!ok)throw DatError(why);}
struct Reader {
 std::span<const uint8_t> bytes;
 void range(size_t o,size_t n)const{require(o<=bytes.size()&&n<=bytes.size()-o,"SSM byte range exceeds file");}
 uint16_t half(size_t o)const{range(o,2);return uint16_t(bytes[o])<<8|bytes[o+1];}
 uint32_t word(size_t o)const{range(o,4);return uint32_t(half(o))<<16|half(o+2);}
};
size_t sample_index(uint32_t a){return size_t(a/16)*14+(a%16)-2;}
}
void decode_audio_adpcm(std::span<const uint8_t> payload,const AudioChannel& c,uint32_t start,uint16_t ps,int16_t h1,int16_t h2,std::vector<int16_t>& out,size_t& budget){
 require(c.end_nibble<uint64_t(payload.size())*2,"DSP address exceeds payload");
 require(out.empty(),"DSP decode output must initially be empty");
 require(start<=c.end_nibble&&start%16>=2&&c.end_nibble%16>=2,"SSM addresses must select DSP sample nibbles");
 const size_t count=sample_index(c.end_nibble)-sample_index(start)+1;
 require(budget<=64U*1024U*1024U&&count<=(64U*1024U*1024U-budget)/2,"SSM decoded PCM exceeds bank budget");budget+=count*2;out.reserve(count);
 require((ps&0xff80)==0,"Invalid DSP initial predictor/scale");
 for(uint32_t at=start;at<=c.end_nibble;){
  if(at%16==0){ps=payload[at/2];require((ps&0x80)==0,"Invalid DSP frame predictor");at+=2;}
  if(at>c.end_nibble)break;
  const unsigned raw=(at&1)?payload[at/2]&15:payload[at/2]>>4;
  const int nibble=raw<8?int(raw):int(raw)-16;
  const int64_t acc=int64_t(c.coefficients[(ps>>4)*2])*h1+int64_t(c.coefficients[(ps>>4)*2+1])*h2+int64_t(nibble)*(int64_t{1}<<(ps&15))*2048;
  // DSP rounding and saturation, matching Aurora's THP ADPCM decoder. Express
  // signed floor division explicitly rather than relying on right-shift rules.
  const int64_t rounded=std::clamp(acc*32+0x8000,int64_t(INT32_MIN),int64_t(INT32_MAX));
  const int16_t value=int16_t(rounded>=0?rounded/65536:-((-rounded+65535)/65536));
  out.push_back(value);h2=h1;h1=value;++at;
 }
 require(out.size()==count,"SSM DSP sample-count mismatch");
}
DatAudioBank::DatAudioBank(std::span<const uint8_t> bytes){
 require(bytes.size()<=64U*1024U*1024U,"SSM input exceeds bank budget");Reader r{bytes};r.range(0,16);
 const uint32_t header=r.word(0),payload_size=r.word(4),count=r.word(8);base_id=r.word(12);
 require(header>=16&&header<=bytes.size()-16&&count>0&&count<=4096&&base_id<=UINT32_MAX-count,"Invalid SSM header or sample ID range");
 const size_t header_end=size_t(header)+16,payload_at=(header_end+31)&~size_t(31);
 r.range(payload_at,payload_size);require(payload_at+payload_size==bytes.size(),"SSM sample payload length differs from file");
 const auto payload=bytes.subspan(payload_at,payload_size);size_t cursor=16,budget=0;samples.reserve(count);
 for(uint32_t i=0;i<count;i++){
  require(cursor<=header_end&&header_end-cursor>=8,"SSM sample entry crosses header");
  const uint32_t voices=r.word(cursor),rate=r.word(cursor+4);cursor+=8;
  require(voices>=1&&voices<=2&&rate>0&&rate<=192000,"Invalid SSM channel count or sample rate");
  require(voices*64<=header_end-cursor,"SSM channel descriptors cross header");
  AudioSample sample;sample.id=base_id+i;sample.sample_rate=rate;sample.channels.reserve(voices);
  for(uint32_t v=0;v<voices;v++,cursor+=64){
   AudioChannel c;const auto loop=r.half(cursor),format=r.half(cursor+2);
   require(loop<=1&&format==0,"Only source DSP ADPCM SSM channels are supported");c.looping=loop;
   c.loop_nibble=r.word(cursor+4);c.end_nibble=r.word(cursor+8);c.current_nibble=r.word(cursor+12);
   for(unsigned j=0;j<16;j++)c.coefficients[j]=int16_t(r.half(cursor+16+j*2));
   require(r.half(cursor+48)==0,"Unsupported DSP ADPCM gain");
   c.predictor_scale=r.half(cursor+50);c.history1=int16_t(r.half(cursor+52));c.history2=int16_t(r.half(cursor+54));
   c.loop_predictor_scale=r.half(cursor+56);c.loop_history1=int16_t(r.half(cursor+58));c.loop_history2=int16_t(r.half(cursor+60));
   require(c.end_nibble<uint64_t(payload.size())*2&&c.current_nibble<=c.end_nibble,"SSM DSP address exceeds sample payload");
   decode_audio_adpcm(payload,c,c.current_nibble,c.predictor_scale,c.history1,c.history2,c.pcm,budget);
   if(c.looping){require(c.loop_nibble>=c.current_nibble&&c.loop_nibble<=c.end_nibble,"SSM loop address exceeds channel range");decode_audio_adpcm(payload,c,c.loop_nibble,c.loop_predictor_scale,c.loop_history1,c.loop_history2,c.loop_pcm,budget);}
   sample.channels.push_back(std::move(c));
  }
  samples.push_back(std::move(sample));
 }
 require(cursor==header_end,"SSM header has unexplained trailing bytes");
}
const AudioSample& DatAudioBank::sample(uint32_t id)const{
 require(id>=base_id&&uint64_t(id)-base_id<samples.size(),"SSM sample ID is absent from bank");return samples[id-base_id];
}
}
